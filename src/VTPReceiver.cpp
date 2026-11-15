#include "VTPReceiver.h"
#include "VTPAudioCodec.h"
#include "VTPLog.h"
#include "VTPVideoCodec.h"
#include <algorithm>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace vtp {

static uint64_t NowSteadyNs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

static void ParseIpPort(const std::string &ip, uint16_t port,
                        std::string &out_ip, uint16_t &out_port) {
  out_ip = ip;
  out_port = port;
  size_t colon_pos = ip.find(':');
  if (colon_pos != std::string::npos) {
    out_ip = ip.substr(0, colon_pos);
    try {
      int parsed = std::stoi(ip.substr(colon_pos + 1));
      out_port = (parsed > 0 && parsed <= 65535)
                     ? static_cast<uint16_t>(parsed)
                     : VTP_DEFAULT_PORT;
    } catch (...) {
      out_port = VTP_DEFAULT_PORT;
    }
  } else if (out_port == 0) {
    out_port = VTP_DEFAULT_PORT;
  }
}

struct VTPReceiver::Impl {
  vtp_receiver_config_t config_{};
  vtp_transport_t active_transport_ = VTP_TRANSPORT_TCP;

  asio::io_context tcp_io_context_;
  std::unique_ptr<asio::ip::tcp::socket> tcp_socket_;
  asio::io_context udp_io_context_;
  std::unique_ptr<asio::ip::udp::socket> udp_socket_;
  asio::ip::udp::endpoint udp_remote_ep_;

  std::mutex send_mutex_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> stop_threads_{false};

  std::thread receive_thread_;
  std::thread heartbeat_thread_;

  enum class ReconnectMode { NONE, DIRECT, BY_NAME };
  std::mutex reconnect_mutex_;
  ReconnectMode reconnect_mode_ = ReconnectMode::NONE;
  std::string target_ip_;
  uint16_t target_port_ = 0;
  VTPDiscoveryListener *reconnect_listener_ = nullptr;
  std::string reconnect_stream_name_;
  std::atomic<bool> supervisor_running_{false};
  std::thread supervisor_thread_;

  struct VideoSlot {
    DecodedVideoResult res;
  };
  std::unique_ptr<VTPJpegWorkerPool> decode_pool_;
  std::vector<uint8_t> alpha_scratch_;
  std::vector<uint8_t> yuv_scratch_;
  VideoSlot video_slots_[3];
  int write_idx_ = 0;
  int ready_idx_ = 1;
  int read_idx_ = 2;
  std::mutex video_mutex_;
  std::condition_variable video_cv_;
  bool new_video_frame_available_ = false;
  bool has_last_frame_idx_ = false;
  uint32_t last_frame_idx_ = 0;

  struct AudioFrameCache {
    std::vector<uint8_t> data;
    uint32_t sample_rate = 0;
    uint8_t channels = 0;
    vtp_audio_format_t format = VTP_AUDIO_PCM_S16LE;
    uint32_t sample_count = 0;
    uint64_t timestamp_ns = 0;
  };
  std::mutex audio_mutex_;
  std::condition_variable audio_cv_;
  std::queue<AudioFrameCache> audio_queue_;
  std::vector<uint8_t> active_audio_buf_;
  std::vector<int16_t> pcm_s16_buf_;
  audio::VTPOpusDecoder opus_decoder_;
  std::vector<float> opus_float_scratch_;

  struct MetaItem {
    std::string text;
    uint64_t timestamp_ns = 0;
  };
  std::mutex meta_mutex_;
  std::condition_variable meta_cv_;
  std::deque<MetaItem> meta_queue_;

  std::mutex control_state_mutex_;
  vtp_tally_state_t desired_tally_{false, false};
  vtp_stream_tier_t desired_tier_ = VTP_TIER_FULL;

  std::mutex cb_mutex_;
  vtp_receiver_callbacks_t callbacks_{};
  void *cb_user_data_ = nullptr;

  std::atomic<uint64_t> bytes_received_{0};
  std::atomic<uint64_t> frames_received_{0};
  std::atomic<uint64_t> frames_dropped_{0};
  std::atomic<uint64_t> audio_packets_{0};
  std::atomic<uint32_t> last_decode_us_{0};
  std::atomic<double> rtt_ms_{0.0};

  mutable std::mutex stats_rate_mutex_;
  mutable std::chrono::steady_clock::time_point last_stats_time_ =
      std::chrono::steady_clock::now();
  mutable uint64_t last_stats_bytes_ = 0;
  mutable uint64_t last_stats_frames_ = 0;
  mutable double cached_mbps_ = 0.0;
  mutable double cached_fps_ = 0.0;

  struct UdpParityGroup {
    bool have = false;
    uint16_t max_sz = 0;
    uint16_t last_len = 0;
    std::vector<uint8_t> data;
  };
  struct UdpAssembly {
    uint8_t pkt_type = 0;
    uint8_t fec_k = 0;
    uint16_t data_chunk_count = 0;
    uint16_t received_data_chunks = 0;
    std::vector<std::vector<uint8_t>> chunks;
    std::vector<bool> have_chunk;
    std::vector<UdpParityGroup> parity_groups;
  };

  Impl(const vtp_receiver_config_t &cfg) : config_(cfg) {
    active_transport_ = cfg.transport;
    desired_tier_ = cfg.preferred_tier;
    int threads = static_cast<int>(
        std::min(4u, std::max(1u, std::thread::hardware_concurrency())));
    decode_pool_ = std::make_unique<VTPJpegWorkerPool>(threads, false);
  }

  void NotifyConnectionState(bool is_conn) {
    vtp_receiver_callbacks_t cbs{};
    void *ud = nullptr;
    {
      std::lock_guard<std::mutex> lock(cb_mutex_);
      cbs = callbacks_;
      ud = cb_user_data_;
    }
    if (cbs.on_connection_state) {
      cbs.on_connection_state(is_conn, ud);
    }
  }

  void CloseSockets() {
    std::lock_guard<std::mutex> lock(send_mutex_);
    asio::error_code ec;
    if (tcp_socket_) {
      tcp_socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
      tcp_socket_->close(ec);
    }
    if (udp_socket_) {
      udp_socket_->close(ec);
    }
  }

  void WakeWaitersAndClearQueues() {
    {
      std::lock_guard<std::mutex> lock(video_mutex_);
      new_video_frame_available_ = false;
      video_cv_.notify_all();
    }
    {
      std::lock_guard<std::mutex> lock(audio_mutex_);
      while (!audio_queue_.empty())
        audio_queue_.pop();
      audio_cv_.notify_all();
    }
    {
      std::lock_guard<std::mutex> lock(meta_mutex_);
      meta_queue_.clear();
      meta_cv_.notify_all();
    }
  }

  void DisconnectTransport(bool notify_cb) {
    stop_threads_.store(true, std::memory_order_relaxed);
    bool was_connected = connected_.exchange(false);
    CloseSockets();
    WakeWaitersAndClearQueues();

    if (heartbeat_thread_.joinable() &&
        std::this_thread::get_id() != heartbeat_thread_.get_id()) {
      heartbeat_thread_.join();
    }
    if (receive_thread_.joinable() &&
        std::this_thread::get_id() != receive_thread_.get_id()) {
      receive_thread_.join();
    }

    tcp_socket_.reset();
    udp_socket_.reset();

    if (was_connected && notify_cb) {
      NotifyConnectionState(false);
    }
  }

  bool SendRawUpstream(const void *data, size_t len) {
    if (!connected_.load(std::memory_order_relaxed))
      return false;
    std::lock_guard<std::mutex> lock(send_mutex_);
    asio::error_code ec;
    if (active_transport_ == VTP_TRANSPORT_TCP) {
      if (!tcp_socket_ || !tcp_socket_->is_open())
        return false;
      asio::write(*tcp_socket_, asio::buffer(data, len), ec);
    } else {
      if (!udp_socket_ || !udp_socket_->is_open())
        return false;
      udp_socket_->send_to(asio::buffer(data, len), udp_remote_ep_, 0, ec);
    }
    return !ec;
  }

  bool SendUpstreamPacket(VTPPacketType type, const void *payload,
                          uint32_t payload_len) {
    VTPPacketHeader hdr{};
    hdr.magic = VTP_MAGIC;
    hdr.type = static_cast<uint8_t>(type);
    hdr.reserved[1] = VTP_PROTOCOL_VERSION;
    hdr.payload_size = payload_len;
    hdr.timestamp_ns = NowSteadyNs();

    std::vector<uint8_t> buf(sizeof(hdr) + payload_len);
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    if (payload_len > 0 && payload) {
      std::memcpy(buf.data() + sizeof(hdr), payload, payload_len);
    }
    return SendRawUpstream(buf.data(), buf.size());
  }

  void ResendUpstreamPreferences() {
    vtp_tally_state_t tally{};
    vtp_stream_tier_t tier = VTP_TIER_FULL;
    {
      std::lock_guard<std::mutex> lock(control_state_mutex_);
      tally = desired_tally_;
      tier = desired_tier_;
    }
    if (tally.on_program || tally.on_preview) {
      VTPTallyPacket tp{tally.on_program ? uint8_t(1) : uint8_t(0),
                        tally.on_preview ? uint8_t(1) : uint8_t(0)};
      SendUpstreamPacket(VTP_PACKET_TALLY, &tp, sizeof(tp));
    }
    if (tier == VTP_TIER_PROXY) {
      const char *cmd = "tier=proxy";
      SendUpstreamPacket(VTP_PACKET_CONTROL, cmd,
                         static_cast<uint32_t>(std::strlen(cmd)));
    }
  }

  bool ConnectOnce(const std::string &ip, uint16_t port, int timeout_ms,
                   vtp_transport_t transport) {
    DisconnectTransport(false);

    std::string target_ip;
    uint16_t target_port = 0;
    ParseIpPort(ip, port, target_ip, target_port);
    active_transport_ = transport;
    has_last_frame_idx_ = false;
    opus_decoder_.Reset();

    if (timeout_ms <= 0) {
      timeout_ms = config_.connect_timeout_ms > 0 ? config_.connect_timeout_ms
                                                  : 2000;
    }

    try {
      if (transport == VTP_TRANSPORT_TCP) {
        tcp_io_context_.restart();
        tcp_socket_ = std::make_unique<asio::ip::tcp::socket>(tcp_io_context_);
        asio::ip::tcp::resolver resolver(tcp_io_context_);
        asio::error_code res_ec;
        auto endpoints =
            resolver.resolve(target_ip, std::to_string(target_port), res_ec);
        if (res_ec) {
          VTP_LOG_WARN("TCP resolve failed for %s:%u: %s", target_ip.c_str(),
                       target_port, res_ec.message().c_str());
          tcp_socket_.reset();
          return false;
        }

        asio::steady_timer timer(tcp_io_context_,
                                 std::chrono::milliseconds(timeout_ms));
        asio::error_code connect_ec = asio::error::operation_aborted;
        bool timed_out = false;

        timer.async_wait([&](const asio::error_code &ec) {
          if (!ec) {
            timed_out = true;
            asio::error_code close_ec;
            if (tcp_socket_)
              tcp_socket_->close(close_ec);
          }
        });

        asio::async_connect(
            *tcp_socket_, endpoints,
            [&](const asio::error_code &ec, const asio::ip::tcp::endpoint &) {
              connect_ec = ec;
              timer.cancel();
            });

        tcp_io_context_.run();

        if (timed_out || connect_ec) {
          VTP_LOG_WARN("TCP connect to %s:%u failed: %s", target_ip.c_str(),
                       target_port,
                       timed_out ? "timed out" : connect_ec.message().c_str());
          tcp_socket_.reset();
          return false;
        }

        asio::error_code opt_ec;
        tcp_socket_->set_option(asio::ip::tcp::no_delay(true), opt_ec);
      } else {
        udp_io_context_.restart();
        asio::ip::udp::resolver resolver(udp_io_context_);
        asio::error_code res_ec;
        auto endpoints = resolver.resolve(asio::ip::udp::v4(), target_ip,
                                          std::to_string(target_port), res_ec);
        if (res_ec || endpoints.empty()) {
          VTP_LOG_WARN("UDP resolve failed for %s:%u: %s", target_ip.c_str(),
                       target_port, res_ec.message().c_str());
          return false;
        }
        udp_remote_ep_ = *endpoints.begin();
        udp_socket_ = std::make_unique<asio::ip::udp::socket>(
            udp_io_context_, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
        asio::error_code opt_ec;
        udp_socket_->set_option(
            asio::socket_base::receive_buffer_size(4 * 1024 * 1024), opt_ec);
      }

      stop_threads_.store(false, std::memory_order_relaxed);
      connected_.store(true, std::memory_order_relaxed);

      if (transport == VTP_TRANSPORT_UDP) {
        SendUpstreamPacket(VTP_PACKET_UDP_HELLO, nullptr, 0);
      }
      ResendUpstreamPreferences();

      receive_thread_ = std::thread(&Impl::ReceiveLoop, this);
      heartbeat_thread_ = std::thread(&Impl::HeartbeatLoop, this);

      VTP_LOG_INFO("Connected to VTP Sender %s:%u (%s)", target_ip.c_str(),
                   target_port,
                   transport == VTP_TRANSPORT_TCP ? "TCP" : "UDP");
      NotifyConnectionState(true);
      return true;
    } catch (const std::exception &e) {
      VTP_LOG_ERROR("Connect exception: %s", e.what());
      DisconnectTransport(false);
      return false;
    }
  }

  void HeartbeatLoop() {
    int tick = 0;
    while (!stop_threads_.load(std::memory_order_relaxed) &&
           connected_.load(std::memory_order_relaxed)) {
      for (int i = 0; i < 10 && !stop_threads_.load(std::memory_order_relaxed) &&
                      connected_.load(std::memory_order_relaxed);
           ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (stop_threads_.load(std::memory_order_relaxed) ||
          !connected_.load(std::memory_order_relaxed)) {
        break;
      }

      if (active_transport_ == VTP_TRANSPORT_UDP) {
        SendUpstreamPacket(VTP_PACKET_UDP_HELLO, nullptr, 0);
      }
      if ((tick % 2) == 0) {
        VTPPingPacket ping{NowSteadyNs()};
        SendUpstreamPacket(VTP_PACKET_PING, &ping, sizeof(ping));
      }
      ++tick;
    }
  }

  void ProcessIncomingPacket(const VTPPacketHeader &pkt_hdr,
                             const uint8_t *payload, size_t payload_len) {
    vtp_receiver_callbacks_t cbs{};
    void *ud = nullptr;
    {
      std::lock_guard<std::mutex> lock(cb_mutex_);
      cbs = callbacks_;
      ud = cb_user_data_;
    }

    if (pkt_hdr.type == VTP_PACKET_VIDEO) {
      auto t0 = std::chrono::steady_clock::now();
      DecodedVideoResult &slot_res = video_slots_[write_idx_].res;
      if (!DecodeVideoPacketPayload(pkt_hdr, payload, payload_len,
                                    config_.preferred_output_format,
                                    *decode_pool_, slot_res, alpha_scratch_,
                                    yuv_scratch_)) {
        VTP_LOG_WARN("Failed to decode incoming video packet");
        return;
      }
      auto t1 = std::chrono::steady_clock::now();
      last_decode_us_.store(
          static_cast<uint32_t>(
              std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
                  .count()),
          std::memory_order_relaxed);

      if (has_last_frame_idx_ && slot_res.frame_index > last_frame_idx_ + 1) {
        frames_dropped_.fetch_add(slot_res.frame_index - last_frame_idx_ - 1,
                                  std::memory_order_relaxed);
      }
      has_last_frame_idx_ = true;
      last_frame_idx_ = slot_res.frame_index;
      frames_received_.fetch_add(1, std::memory_order_relaxed);

      if (cbs.on_frame) {
        vtp_frame_ex_t cb_frame{};
        cb_frame.data = slot_res.data.data();
        cb_frame.width = slot_res.width;
        cb_frame.height = slot_res.height;
        cb_frame.stride = slot_res.stride;
        cb_frame.format = slot_res.format;
        cb_frame.timestamp_ns = slot_res.timestamp_ns;
        cb_frame.frame_index = slot_res.frame_index;
        cb_frame.has_alpha = slot_res.has_alpha;
        cb_frame.metadata = slot_res.metadata;
        cbs.on_frame(&cb_frame, ud);
      }

      {
        std::lock_guard<std::mutex> lock(video_mutex_);
        std::swap(write_idx_, ready_idx_);
        new_video_frame_available_ = true;
      }
      video_cv_.notify_all();
    } else if (pkt_hdr.type == VTP_PACKET_AUDIO) {
      if (payload_len < sizeof(VTPAudioMetadata))
        return;
      VTPAudioMetadata meta;
      std::memcpy(&meta, payload, sizeof(meta));
      if (meta.channels == 0 || meta.channels > VTP_MAX_CHANNELS ||
          meta.data_size > VTP_MAX_AUDIO_PAYLOAD ||
          sizeof(VTPAudioMetadata) + meta.data_size > payload_len) {
        return;
      }

      const uint8_t *audio_ptr = payload + sizeof(VTPAudioMetadata);
      vtp_audio_codec_t codec = vtp_decode_audio_codec(meta.sample_format);
      vtp_audio_format_t fmt = vtp_decode_audio_format(meta.sample_format);

      AudioFrameCache cache;
      cache.sample_rate = meta.sample_rate;
      cache.channels = meta.channels;
      cache.timestamp_ns = pkt_hdr.timestamp_ns;

      if (codec == VTP_AUDIO_CODEC_OPUS) {
        int decoded = opus_decoder_.DecodeToFloat(
            audio_ptr, static_cast<int>(meta.data_size), meta.sample_rate,
            meta.channels, opus_float_scratch_);
        if (decoded <= 0)
          return;
        size_t bytes = opus_float_scratch_.size() * sizeof(float);
        cache.data.resize(bytes);
        std::memcpy(cache.data.data(), opus_float_scratch_.data(), bytes);
        cache.format = VTP_AUDIO_PCM_F32LE;
        cache.sample_count = static_cast<uint32_t>(decoded);
      } else {
        cache.data.assign(audio_ptr, audio_ptr + meta.data_size);
        cache.format = fmt;
        cache.sample_count = meta.sample_count;
      }

      audio_packets_.fetch_add(1, std::memory_order_relaxed);

      if (cbs.on_audio) {
        vtp_audio_frame_ex_t cb_audio{};
        cb_audio.data = cache.data.data();
        cb_audio.sample_rate = cache.sample_rate;
        cb_audio.channels = cache.channels;
        cb_audio.format = cache.format;
        cb_audio.sample_count = cache.sample_count;
        cb_audio.data_size = static_cast<uint32_t>(cache.data.size());
        cb_audio.timestamp_ns = cache.timestamp_ns;
        cbs.on_audio(&cb_audio, ud);
      }

      {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        audio_queue_.push(std::move(cache));
        if (audio_queue_.size() > 100) {
          audio_queue_.pop();
        }
      }
      audio_cv_.notify_all();
    } else if (pkt_hdr.type == VTP_PACKET_METADATA) {
      std::string text(reinterpret_cast<const char *>(payload), payload_len);
      if (cbs.on_metadata) {
        cbs.on_metadata(text.c_str(), pkt_hdr.timestamp_ns, ud);
      }
      {
        std::lock_guard<std::mutex> lock(meta_mutex_);
        if (meta_queue_.size() >= 64)
          meta_queue_.pop_front();
        meta_queue_.push_back({std::move(text), pkt_hdr.timestamp_ns});
      }
      meta_cv_.notify_all();
    } else if (pkt_hdr.type == VTP_PACKET_PONG) {
      if (payload_len >= sizeof(VTPPingPacket)) {
        VTPPingPacket pong;
        std::memcpy(&pong, payload, sizeof(pong));
        uint64_t now = NowSteadyNs();
        if (now >= pong.send_time_ns) {
          rtt_ms_.store(static_cast<double>(now - pong.send_time_ns) / 1e6,
                        std::memory_order_relaxed);
        }
      }
    }
  }

  void ReceiveLoop() {
    if (active_transport_ == VTP_TRANSPORT_TCP) {
      ReceiveTcpLoop();
    } else {
      ReceiveUdpLoop();
    }

    bool was_conn = connected_.exchange(false);
    WakeWaitersAndClearQueues();
    if (was_conn && !stop_threads_.load(std::memory_order_relaxed)) {
      NotifyConnectionState(false);
    }
  }

  void ReceiveTcpLoop() {
    std::vector<uint8_t> payload_buf;
    while (connected_.load(std::memory_order_relaxed) &&
           !stop_threads_.load(std::memory_order_relaxed)) {
      asio::error_code ec;
      VTPPacketHeader pkt_hdr{};
      size_t hdr_read = asio::read(
          *tcp_socket_, asio::buffer(&pkt_hdr, sizeof(pkt_hdr)), ec);
      if (ec || hdr_read != sizeof(pkt_hdr)) {
        break;
      }
      if (pkt_hdr.magic != VTP_MAGIC ||
          pkt_hdr.payload_size > VTP_MAX_VIDEO_PAYLOAD) {
        VTP_LOG_WARN("Invalid TCP packet magic or size (%u)",
                     pkt_hdr.payload_size);
        break;
      }

      bytes_received_.fetch_add(sizeof(pkt_hdr), std::memory_order_relaxed);
      payload_buf.resize(pkt_hdr.payload_size);
      if (pkt_hdr.payload_size > 0) {
        size_t body_read =
            asio::read(*tcp_socket_, asio::buffer(payload_buf), ec);
        if (ec || body_read != pkt_hdr.payload_size) {
          break;
        }
        bytes_received_.fetch_add(body_read, std::memory_order_relaxed);
      }

      ProcessIncomingPacket(pkt_hdr, payload_buf.data(), payload_buf.size());
    }
  }

  bool TryRecoverFecGroup(UdpAssembly &a, uint16_t group_idx) {
    if (a.fec_k == 0 || group_idx >= a.parity_groups.size())
      return false;
    const auto &pg = a.parity_groups[group_idx];
    if (!pg.have)
      return false;

    uint16_t start = static_cast<uint16_t>(group_idx * a.fec_k);
    uint16_t end =
        std::min<uint16_t>(start + a.fec_k, a.data_chunk_count);
    int missing_idx = -1;
    for (uint16_t i = start; i < end; ++i) {
      if (!a.have_chunk[i]) {
        if (missing_idx != -1)
          return false;
        missing_idx = i;
      }
    }
    if (missing_idx == -1)
      return false;

    uint16_t rec_len = (missing_idx == end - 1) ? pg.last_len : VTP_UDP_MTU_PAYLOAD;
    if (rec_len > pg.max_sz || rec_len > VTP_UDP_MTU_PAYLOAD)
      return false;

    std::vector<uint8_t> recovered(pg.max_sz, 0);
    std::memcpy(recovered.data(), pg.data.data(), pg.max_sz);
    for (uint16_t i = start; i < end; ++i) {
      if (i == static_cast<uint16_t>(missing_idx))
        continue;
      const auto &ch = a.chunks[i];
      size_t clen = std::min(ch.size(), recovered.size());
      for (size_t b = 0; b < clen; ++b) {
        recovered[b] ^= ch[b];
      }
    }
    recovered.resize(rec_len);
    a.chunks[static_cast<size_t>(missing_idx)] = std::move(recovered);
    a.have_chunk[static_cast<size_t>(missing_idx)] = true;
    ++a.received_data_chunks;
    return true;
  }

  void ReceiveUdpLoop() {
    std::vector<uint8_t> dgram(sizeof(VTPUDPChunkHeader) +
                               VTP_UDP_MTU_PAYLOAD + 64);
    std::map<uint32_t, UdpAssembly> assemblies;
    uint32_t highest_completed_seq = 0;

    asio::error_code nb_ec;
    udp_socket_->non_blocking(true, nb_ec);

    while (connected_.load(std::memory_order_relaxed) &&
           !stop_threads_.load(std::memory_order_relaxed)) {
      asio::ip::udp::endpoint sender_ep;
      asio::error_code ec;
      size_t n = 0;
      {
        std::lock_guard<std::mutex> lock(send_mutex_);
        if (!udp_socket_ || !udp_socket_->is_open())
          break;
        n = udp_socket_->receive_from(asio::buffer(dgram), sender_ep, 0, ec);
      }
      if (ec == asio::error::would_block || ec == asio::error::try_again) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      if (ec) {
        break;
      }
      bytes_received_.fetch_add(n, std::memory_order_relaxed);
      if (n < sizeof(VTPUDPChunkHeader))
        continue;

      VTPUDPChunkHeader ch{};
      std::memcpy(&ch, dgram.data(), sizeof(ch));
      if (ch.magic != VTP_UDP_MAGIC || ch.data_chunk_count == 0 ||
          ch.data_chunk_count > 65000 || ch.chunk_size > VTP_UDP_MTU_PAYLOAD ||
          sizeof(ch) + ch.chunk_size > n) {
        continue;
      }

      if (highest_completed_seq > 0 && ch.message_seq <= highest_completed_seq) {
        if (highest_completed_seq - ch.message_seq < 1000u) {
          continue;
        }
        highest_completed_seq = 0;
        assemblies.clear();
      }

      auto &a = assemblies[ch.message_seq];
      if (a.data_chunk_count == 0) {
        a.pkt_type = ch.pkt_type;
        a.fec_k = ch.fec_group_size;
        a.data_chunk_count = ch.data_chunk_count;
        a.chunks.resize(ch.data_chunk_count);
        a.have_chunk.assign(ch.data_chunk_count, false);
        if (ch.fec_group_size > 0) {
          uint16_t groups =
              (ch.data_chunk_count + ch.fec_group_size - 1) / ch.fec_group_size;
          a.parity_groups.resize(groups);
        }
      } else if (a.data_chunk_count != ch.data_chunk_count) {
        continue;
      }

      const uint8_t *chunk_data = dgram.data() + sizeof(ch);
      if (ch.flags & VTP_UDP_FLAG_PARITY) {
        if (ch.fec_group_size > 0 && ch.chunk_idx < a.parity_groups.size()) {
          auto &pg = a.parity_groups[ch.chunk_idx];
          if (!pg.have) {
            pg.have = true;
            pg.max_sz = ch.chunk_size;
            pg.last_len = ch.reserved;
            pg.data.assign(chunk_data, chunk_data + ch.chunk_size);
            TryRecoverFecGroup(a, ch.chunk_idx);
          }
        }
      } else if (ch.chunk_idx < a.data_chunk_count &&
                 !a.have_chunk[ch.chunk_idx]) {
        a.chunks[ch.chunk_idx].assign(chunk_data, chunk_data + ch.chunk_size);
        a.have_chunk[ch.chunk_idx] = true;
        ++a.received_data_chunks;
        if (a.fec_k > 0) {
          TryRecoverFecGroup(a, ch.chunk_idx / a.fec_k);
        }
      }

      if (a.received_data_chunks == a.data_chunk_count) {
        std::vector<uint8_t> full_msg;
        size_t total = 0;
        for (const auto &c : a.chunks)
          total += c.size();
        full_msg.reserve(total);
        for (const auto &c : a.chunks) {
          full_msg.insert(full_msg.end(), c.begin(), c.end());
        }

        if (full_msg.size() >= sizeof(VTPPacketHeader)) {
          VTPPacketHeader ph{};
          std::memcpy(&ph, full_msg.data(), sizeof(ph));
          if (ph.magic == VTP_MAGIC &&
              sizeof(ph) + ph.payload_size <= full_msg.size()) {
            ProcessIncomingPacket(ph, full_msg.data() + sizeof(ph),
                                  ph.payload_size);
          }
        }
        highest_completed_seq = ch.message_seq;
        for (auto it = assemblies.begin(); it != assemblies.end();) {
          if (it->first <= ch.message_seq) {
            if (it->first < ch.message_seq &&
                it->second.pkt_type == VTP_PACKET_VIDEO) {
              frames_dropped_.fetch_add(1, std::memory_order_relaxed);
            }
            it = assemblies.erase(it);
          } else {
            ++it;
          }
        }
      }

      while (assemblies.size() > 16) {
        auto it = assemblies.begin();
        if (it->second.pkt_type == VTP_PACKET_VIDEO) {
          frames_dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        assemblies.erase(it);
      }
    }
  }

  void EnsureSupervisorRunning() {
    if (supervisor_running_.exchange(true))
      return;
    supervisor_thread_ = std::thread(&Impl::SupervisorLoop, this);
  }

  void StopSupervisor() {
    if (!supervisor_running_.exchange(false))
      return;
    if (supervisor_thread_.joinable() &&
        std::this_thread::get_id() != supervisor_thread_.get_id()) {
      supervisor_thread_.join();
    }
  }

  void SupervisorLoop() {
    while (supervisor_running_.load(std::memory_order_relaxed)) {
      if (!connected_.load(std::memory_order_relaxed)) {
        ReconnectMode mode = ReconnectMode::NONE;
        std::string ip;
        uint16_t port = 0;
        VTPDiscoveryListener *listener = nullptr;
        std::string name;
        {
          std::lock_guard<std::mutex> lock(reconnect_mutex_);
          mode = reconnect_mode_;
          ip = target_ip_;
          port = target_port_;
          listener = reconnect_listener_;
          name = reconnect_stream_name_;
        }

        if (mode == ReconnectMode::DIRECT && !ip.empty()) {
          ConnectOnce(ip, port, config_.connect_timeout_ms, config_.transport);
        } else if (mode == ReconnectMode::BY_NAME && listener && !name.empty()) {
          VTPSource src{};
          uint8_t flags = 0;
          if (listener->FindSource(name.c_str(), &src, &flags)) {
            vtp_transport_t t = (flags & VTP_DISC_FLAG_UDP)
                                    ? VTP_TRANSPORT_UDP
                                    : config_.transport;
            ConnectOnce(src.ip, src.port, config_.connect_timeout_ms, t);
          }
        }
      }

      int interval_ms = config_.reconnect_interval_ms > 0
                            ? config_.reconnect_interval_ms
                            : 1000;
      for (int elapsed = 0;
           elapsed < interval_ms &&
           supervisor_running_.load(std::memory_order_relaxed);
           elapsed += 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }
};

VTPReceiver::VTPReceiver() {
  vtp_receiver_config_t cfg{};
  vtp_receiver_config_init(&cfg);
  impl_ = std::make_unique<Impl>(cfg);
}

VTPReceiver::VTPReceiver(const vtp_receiver_config_t &config)
    : impl_(std::make_unique<Impl>(config)) {}

VTPReceiver::~VTPReceiver() { Disconnect(); }

bool VTPReceiver::Connect(const std::string &ip, uint16_t port) {
  return ConnectEx(ip, port, impl_->config_.connect_timeout_ms);
}

bool VTPReceiver::ConnectEx(const std::string &ip, uint16_t port,
                            int timeout_ms) {
  impl_->StopSupervisor();
  {
    std::lock_guard<std::mutex> lock(impl_->reconnect_mutex_);
    impl_->target_ip_ = ip;
    impl_->target_port_ = port;
    impl_->reconnect_mode_ =
        impl_->config_.auto_reconnect ? Impl::ReconnectMode::DIRECT
                                      : Impl::ReconnectMode::NONE;
  }

  bool ok =
      impl_->ConnectOnce(ip, port, timeout_ms, impl_->config_.transport);
  if (impl_->config_.auto_reconnect) {
    impl_->EnsureSupervisorRunning();
  }
  return ok;
}

bool VTPReceiver::ConnectByName(VTPDiscoveryListener *listener,
                                const std::string &name) {
  if (!listener || name.empty())
    return false;

  VTPSource src{};
  uint8_t flags = 0;
  if (!listener->FindSource(name.c_str(), &src, &flags)) {
    return false;
  }

  impl_->StopSupervisor();
  {
    std::lock_guard<std::mutex> lock(impl_->reconnect_mutex_);
    impl_->reconnect_listener_ = listener;
    impl_->reconnect_stream_name_ = name;
    impl_->reconnect_mode_ =
        impl_->config_.auto_reconnect ? Impl::ReconnectMode::BY_NAME
                                      : Impl::ReconnectMode::NONE;
  }

  vtp_transport_t t =
      (flags & VTP_DISC_FLAG_UDP) ? VTP_TRANSPORT_UDP : impl_->config_.transport;
  bool ok =
      impl_->ConnectOnce(src.ip, src.port, impl_->config_.connect_timeout_ms, t);
  if (impl_->config_.auto_reconnect) {
    impl_->EnsureSupervisorRunning();
  }
  return ok;
}

bool VTPReceiver::SetAutoReconnectName(VTPDiscoveryListener *listener,
                                       const std::string &name, bool enable) {
  if (enable) {
    if (!listener || name.empty())
      return false;
    {
      std::lock_guard<std::mutex> lock(impl_->reconnect_mutex_);
      impl_->reconnect_listener_ = listener;
      impl_->reconnect_stream_name_ = name;
      impl_->reconnect_mode_ = Impl::ReconnectMode::BY_NAME;
    }
    impl_->EnsureSupervisorRunning();
    return true;
  } else {
    impl_->StopSupervisor();
    std::lock_guard<std::mutex> lock(impl_->reconnect_mutex_);
    impl_->reconnect_mode_ = Impl::ReconnectMode::NONE;
    return true;
  }
}

void VTPReceiver::Disconnect() {
  impl_->StopSupervisor();
  impl_->DisconnectTransport(true);
}

bool VTPReceiver::ReceiveFrame(VTPFrame *out_frame) {
  if (!out_frame)
    return false;
  vtp_frame_ex_t ex{};
  if (!ReceiveFrameEx(&ex, -1))
    return false;

  out_frame->data = ex.data;
  out_frame->width = ex.width;
  out_frame->height = ex.height;
  out_frame->format = ex.format;
  out_frame->timestamp_ns = ex.timestamp_ns;
  out_frame->frame_index = ex.frame_index;
  out_frame->has_alpha = ex.has_alpha;
  return true;
}

bool VTPReceiver::ReceiveFrameEx(vtp_frame_ex_t *out_frame, int timeout_ms) {
  if (!out_frame)
    return false;

  std::unique_lock<std::mutex> lock(impl_->video_mutex_);
  auto pred = [this]() {
    return impl_->new_video_frame_available_ ||
           !impl_->connected_.load(std::memory_order_relaxed);
  };

  if (timeout_ms < 0) {
    impl_->video_cv_.wait(lock, pred);
  } else if (timeout_ms == 0) {
    if (!impl_->new_video_frame_available_)
      return false;
  } else {
    if (!impl_->video_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                   pred)) {
      return false;
    }
  }

  if (!impl_->new_video_frame_available_) {
    return false;
  }

  std::swap(impl_->ready_idx_, impl_->read_idx_);
  impl_->new_video_frame_available_ = false;
  lock.unlock();

  const DecodedVideoResult &res = impl_->video_slots_[impl_->read_idx_].res;
  out_frame->data = res.data.data();
  out_frame->width = res.width;
  out_frame->height = res.height;
  out_frame->stride = res.stride;
  out_frame->format = res.format;
  out_frame->timestamp_ns = res.timestamp_ns;
  out_frame->frame_index = res.frame_index;
  out_frame->has_alpha = res.has_alpha;
  out_frame->metadata = res.metadata;
  return true;
}

bool VTPReceiver::ReceiveAudio(VTPAudioFrame *out_audio) {
  if (!out_audio)
    return false;
  vtp_audio_frame_ex_t ex{};
  if (!ReceiveAudioEx(&ex, -1))
    return false;

  out_audio->sample_rate = ex.sample_rate;
  out_audio->channels = ex.channels;
  out_audio->sample_count = ex.sample_count;
  out_audio->timestamp_ns = ex.timestamp_ns;

  if (ex.format == VTP_AUDIO_PCM_S16LE) {
    out_audio->data = ex.data;
    out_audio->data_size = ex.data_size;
  } else {
    audio::ConvertToS16LE(ex.data, ex.format, static_cast<int>(ex.sample_count),
                          ex.channels, impl_->pcm_s16_buf_);
    out_audio->data =
        reinterpret_cast<const unsigned char *>(impl_->pcm_s16_buf_.data());
    out_audio->data_size =
        static_cast<uint32_t>(impl_->pcm_s16_buf_.size() * sizeof(int16_t));
  }
  return true;
}

bool VTPReceiver::ReceiveAudioEx(vtp_audio_frame_ex_t *out_audio,
                                 int timeout_ms) {
  if (!out_audio)
    return false;

  std::unique_lock<std::mutex> lock(impl_->audio_mutex_);
  auto pred = [this]() {
    return !impl_->audio_queue_.empty() ||
           !impl_->connected_.load(std::memory_order_relaxed);
  };

  if (timeout_ms < 0) {
    impl_->audio_cv_.wait(lock, pred);
  } else if (timeout_ms == 0) {
    if (impl_->audio_queue_.empty())
      return false;
  } else {
    if (!impl_->audio_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                   pred)) {
      return false;
    }
  }

  if (impl_->audio_queue_.empty()) {
    return false;
  }

  Impl::AudioFrameCache frame = std::move(impl_->audio_queue_.front());
  impl_->audio_queue_.pop();
  lock.unlock();

  impl_->active_audio_buf_ = std::move(frame.data);
  out_audio->data = impl_->active_audio_buf_.data();
  out_audio->sample_rate = frame.sample_rate;
  out_audio->channels = frame.channels;
  out_audio->format = frame.format;
  out_audio->sample_count = frame.sample_count;
  out_audio->data_size = static_cast<uint32_t>(impl_->active_audio_buf_.size());
  out_audio->timestamp_ns = frame.timestamp_ns;
  return true;
}

bool VTPReceiver::ReceiveMetadata(char *out_buf, size_t buf_size,
                                  uint64_t *out_ts_ns, int timeout_ms) {
  if (!out_buf || buf_size == 0)
    return false;

  std::unique_lock<std::mutex> lock(impl_->meta_mutex_);
  auto pred = [this]() {
    return !impl_->meta_queue_.empty() ||
           !impl_->connected_.load(std::memory_order_relaxed);
  };

  if (timeout_ms < 0) {
    impl_->meta_cv_.wait(lock, pred);
  } else if (timeout_ms == 0) {
    if (impl_->meta_queue_.empty())
      return false;
  } else {
    if (!impl_->meta_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                  pred)) {
      return false;
    }
  }

  if (impl_->meta_queue_.empty())
    return false;

  Impl::MetaItem item = std::move(impl_->meta_queue_.front());
  impl_->meta_queue_.pop_front();
  lock.unlock();

  size_t copy_len = std::min(buf_size - 1, item.text.size());
  if (copy_len > 0) {
    std::memcpy(out_buf, item.text.data(), copy_len);
  }
  out_buf[copy_len] = '\0';
  if (out_ts_ns)
    *out_ts_ns = item.timestamp_ns;
  return true;
}

bool VTPReceiver::SetTally(const vtp_tally_state_t *tally) {
  if (!tally)
    return false;
  {
    std::lock_guard<std::mutex> lock(impl_->control_state_mutex_);
    impl_->desired_tally_ = *tally;
  }
  VTPTallyPacket tp{tally->on_program ? uint8_t(1) : uint8_t(0),
                    tally->on_preview ? uint8_t(1) : uint8_t(0)};
  return impl_->SendUpstreamPacket(VTP_PACKET_TALLY, &tp, sizeof(tp));
}

bool VTPReceiver::SendPTZ(const vtp_ptz_command_t *cmd) {
  if (!cmd)
    return false;
  VTPPTZPacket pp{};
  pp.pan = cmd->pan;
  pp.tilt = cmd->tilt;
  pp.zoom = cmd->zoom;
  pp.focus = cmd->focus;
  pp.preset_id = cmd->preset_id;
  pp.action = cmd->action;
  return impl_->SendUpstreamPacket(VTP_PACKET_PTZ, &pp, sizeof(pp));
}

bool VTPReceiver::SendControl(const char *command_utf8) {
  if (!command_utf8)
    return false;
  size_t len = std::strlen(command_utf8);
  if (len > VTP_MAX_META_PAYLOAD)
    return false;
  return impl_->SendUpstreamPacket(VTP_PACKET_CONTROL, command_utf8,
                                   static_cast<uint32_t>(len));
}

bool VTPReceiver::SetStreamTier(vtp_stream_tier_t tier) {
  {
    std::lock_guard<std::mutex> lock(impl_->control_state_mutex_);
    impl_->desired_tier_ = tier;
  }
  const char *cmd =
      (tier == VTP_TIER_PROXY) ? "tier=proxy" : "tier=full";
  return impl_->SendUpstreamPacket(VTP_PACKET_CONTROL, cmd,
                                   static_cast<uint32_t>(std::strlen(cmd)));
}

void VTPReceiver::SetCallbacks(const vtp_receiver_callbacks_t *callbacks,
                               void *user_data) {
  std::lock_guard<std::mutex> lock(impl_->cb_mutex_);
  if (callbacks) {
    impl_->callbacks_ = *callbacks;
  } else {
    std::memset(&impl_->callbacks_, 0, sizeof(impl_->callbacks_));
  }
  impl_->cb_user_data_ = user_data;
}

bool VTPReceiver::IsConnected() const {
  return impl_->connected_.load(std::memory_order_relaxed);
}

bool VTPReceiver::GetStats(vtp_stats_t *out_stats) const {
  if (!out_stats)
    return false;

  uint64_t bytes = impl_->bytes_received_.load(std::memory_order_relaxed);
  uint64_t frames = impl_->frames_received_.load(std::memory_order_relaxed);
  uint64_t dropped = impl_->frames_dropped_.load(std::memory_order_relaxed);
  uint64_t apkts = impl_->audio_packets_.load(std::memory_order_relaxed);
  uint32_t aqueue = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->audio_mutex_);
    aqueue = static_cast<uint32_t>(impl_->audio_queue_.size());
  }

  double mbps = 0.0;
  double fps = 0.0;
  {
    std::lock_guard<std::mutex> lock(impl_->stats_rate_mutex_);
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - impl_->last_stats_time_)
                    .count();
    if (dt >= 0.25) {
      impl_->cached_mbps_ =
          (static_cast<double>(bytes - impl_->last_stats_bytes_) * 8.0) /
          (dt * 1e6);
      impl_->cached_fps_ =
          static_cast<double>(frames - impl_->last_stats_frames_) / dt;
      impl_->last_stats_time_ = now;
      impl_->last_stats_bytes_ = bytes;
      impl_->last_stats_frames_ = frames;
    }
    mbps = impl_->cached_mbps_;
    fps = impl_->cached_fps_;
  }

  std::memset(out_stats, 0, sizeof(*out_stats));
  out_stats->bitrate_mbps = mbps;
  out_stats->fps = fps;
  out_stats->bytes_transferred = bytes;
  out_stats->frames_processed = frames;
  out_stats->frames_dropped = dropped;
  out_stats->audio_packets_processed = apkts;
  out_stats->last_codec_time_us =
      impl_->last_decode_us_.load(std::memory_order_relaxed);
  out_stats->connected_clients = IsConnected() ? 1u : 0u;
  out_stats->audio_queue_depth = aqueue;
  out_stats->rtt_ms = impl_->rtt_ms_.load(std::memory_order_relaxed);
  return true;
}

} // namespace vtp
