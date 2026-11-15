#include "VTPSender.h"
#include "VTPAudioCodec.h"
#include "VTPDiscovery.h"
#include "VTPLog.h"
#include "VTPSimd.h"
#include "VTPVideoCodec.h"
#include <algorithm>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <turbojpeg.h>
#include <vector>

namespace vtp {

static int ClampQuality(int q) { return std::max(1, std::min(100, q)); }

static uint64_t NowSteadyNs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

struct QueuedPacket {
  std::shared_ptr<const std::vector<uint8_t>> data;
  bool is_video = false;
};

struct SliceWork {
  uint16_t y_start = 0;
  uint16_t slice_height = 0;
  std::vector<uint8_t> jpeg;
};

static void BuildSlices(int height, int requested_slices,
                        std::vector<SliceWork> &out_slices) {
  int slices = std::max(1, requested_slices);
  if (slices > 1) {
    int band_h = ((height / slices) / 16) * 16;
    if (band_h < 16) {
      slices = 1;
    } else {
      out_slices.resize(static_cast<size_t>(slices));
      for (int i = 0; i < slices; ++i) {
        int y0 = i * band_h;
        int sh = (i == slices - 1) ? (height - y0) : band_h;
        out_slices[static_cast<size_t>(i)].y_start = static_cast<uint16_t>(y0);
        out_slices[static_cast<size_t>(i)].slice_height =
            static_cast<uint16_t>(sh);
      }
      return;
    }
  }
  out_slices.resize(1);
  out_slices[0].y_start = 0;
  out_slices[0].slice_height = static_cast<uint16_t>(height);
}

static bool CompressSlicesPacked(VTPJpegWorkerPool &pool, const uint8_t *data,
                                 int width, int stride, int height, int tj_pf,
                                 int subsamp, int quality, int slice_count,
                                 std::vector<SliceWork> &slices) {
  BuildSlices(height, slice_count, slices);
  std::atomic<bool> ok{true};
  pool.RunParallel(slices.size(), [&](size_t idx, tjhandle tj) {
    SliceWork &sw = slices[idx];
    unsigned char *jpeg_buf = nullptr;
    unsigned long jpeg_sz = 0;
    const uint8_t *src = data + static_cast<size_t>(sw.y_start) * stride;
    if (tjCompress2(tj, src, width, stride, sw.slice_height, tj_pf, &jpeg_buf,
                    &jpeg_sz, subsamp, quality, 0) == 0 &&
        jpeg_buf && jpeg_sz > 0) {
      sw.jpeg.assign(jpeg_buf, jpeg_buf + jpeg_sz);
    } else {
      ok.store(false, std::memory_order_relaxed);
    }
    if (jpeg_buf)
      tjFree(jpeg_buf);
  });
  return ok.load(std::memory_order_relaxed);
}

static bool CompressSlicesYUVPlanes(VTPJpegWorkerPool &pool,
                                    const uint8_t *const *planes,
                                    const int *strides, int width, int height,
                                    int subsamp, int quality, int slice_count,
                                    std::vector<SliceWork> &slices) {
  BuildSlices(height, slice_count, slices);
  const int v_div = (subsamp == TJSAMP_420) ? 2 : 1;
  std::atomic<bool> ok{true};
  pool.RunParallel(slices.size(), [&](size_t idx, tjhandle tj) {
    SliceWork &sw = slices[idx];
    unsigned char *jpeg_buf = nullptr;
    unsigned long jpeg_sz = 0;
    const unsigned char *sp[3] = {
        planes[0] + static_cast<size_t>(sw.y_start) * strides[0],
        planes[1] + static_cast<size_t>(sw.y_start / v_div) * strides[1],
        planes[2] + static_cast<size_t>(sw.y_start / v_div) * strides[2]};
    if (tjCompressFromYUVPlanes(tj, sp, width, strides, sw.slice_height,
                                subsamp, &jpeg_buf, &jpeg_sz, quality, 0) == 0 &&
        jpeg_buf && jpeg_sz > 0) {
      sw.jpeg.assign(jpeg_buf, jpeg_buf + jpeg_sz);
    } else {
      ok.store(false, std::memory_order_relaxed);
    }
    if (jpeg_buf)
      tjFree(jpeg_buf);
  });
  return ok.load(std::memory_order_relaxed);
}

static size_t CalcSlicesWireSize(const std::vector<SliceWork> &slices,
                                 bool sliced) {
  if (!sliced) {
    return slices.empty() ? 0 : slices[0].jpeg.size();
  }
  size_t total = 0;
  for (const auto &s : slices) {
    total += sizeof(VTPSliceHeader) + s.jpeg.size();
  }
  return total;
}

static void AppendSlicesToBuffer(std::vector<uint8_t> &dst,
                                 const std::vector<SliceWork> &slices,
                                 bool sliced) {
  if (!sliced) {
    if (!slices.empty()) {
      dst.insert(dst.end(), slices[0].jpeg.begin(), slices[0].jpeg.end());
    }
    return;
  }
  for (const auto &s : slices) {
    VTPSliceHeader sh{};
    sh.y_start = s.y_start;
    sh.slice_height = s.slice_height;
    sh.jpeg_size = static_cast<uint32_t>(s.jpeg.size());
    const uint8_t *sh_bytes = reinterpret_cast<const uint8_t *>(&sh);
    dst.insert(dst.end(), sh_bytes, sh_bytes + sizeof(sh));
    dst.insert(dst.end(), s.jpeg.begin(), s.jpeg.end());
  }
}

struct VTPSender::Impl : public std::enable_shared_from_this<VTPSender::Impl> {
  struct TcpClient : public std::enable_shared_from_this<TcpClient> {
    Impl *owner = nullptr;
    asio::ip::tcp::socket socket;
    std::mutex write_mutex;
    std::deque<QueuedPacket> write_queue;
    bool writing = false;
    bool closed = false;

    std::atomic<vtp_stream_tier_t> tier{VTP_TIER_FULL};
    std::atomic<bool> tally_pgm{false};
    std::atomic<bool> tally_pvw{false};

    VTPPacketHeader rx_hdr{};
    std::vector<uint8_t> rx_payload;

    TcpClient(Impl *o, asio::io_context &io) : owner(o), socket(io) {}

    void Start() { ReadHeader(); }

    void Close() {
      std::lock_guard<std::mutex> lock(write_mutex);
      if (closed)
        return;
      closed = true;
      write_queue.clear();
      asio::error_code ec;
      socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
      socket.close(ec);
    }

    bool Enqueue(const std::shared_ptr<const std::vector<uint8_t>> &pkt,
                 bool is_video, std::atomic<uint64_t> &dropped_counter) {
      std::lock_guard<std::mutex> lock(write_mutex);
      if (closed)
        return false;

      if (is_video) {
        size_t start_idx = writing ? 1 : 0;
        int queued_videos = 0;
        size_t oldest_video_idx = SIZE_MAX;
        for (size_t i = start_idx; i < write_queue.size(); ++i) {
          if (write_queue[i].is_video) {
            if (oldest_video_idx == SIZE_MAX)
              oldest_video_idx = i;
            ++queued_videos;
          }
        }
        if (queued_videos >= 2 && oldest_video_idx != SIZE_MAX) {
          write_queue.erase(write_queue.begin() +
                            static_cast<ptrdiff_t>(oldest_video_idx));
          dropped_counter.fetch_add(1, std::memory_order_relaxed);
        }
      } else if (write_queue.size() >= 128) {
        return false;
      }

      write_queue.push_back({pkt, is_video});
      if (!writing) {
        writing = true;
        DoWriteLocked();
      }
      return true;
    }

  private:
    void DoWriteLocked() {
      if (closed || write_queue.empty()) {
        writing = false;
        return;
      }
      auto self = shared_from_this();
      auto buf_ptr = write_queue.front().data;
      asio::async_write(
          socket, asio::buffer(*buf_ptr),
          [self, buf_ptr](const asio::error_code &ec, std::size_t bytes_written) {
            self->OnWriteComplete(ec, bytes_written);
          });
    }

    void OnWriteComplete(const asio::error_code &ec, std::size_t bytes_written) {
      if (ec) {
        Close();
        owner->RemoveTcpClient(shared_from_this());
        return;
      }
      owner->AddSentBytes(bytes_written);
      std::lock_guard<std::mutex> lock(write_mutex);
      if (!write_queue.empty()) {
        write_queue.pop_front();
      }
      if (!closed && !write_queue.empty()) {
        DoWriteLocked();
      } else {
        writing = false;
      }
    }

    void ReadHeader() {
      auto self = shared_from_this();
      asio::async_read(
          socket, asio::buffer(&rx_hdr, sizeof(rx_hdr)),
          [self](const asio::error_code &ec, std::size_t) {
            if (ec) {
              self->Close();
              self->owner->RemoveTcpClient(self);
              return;
            }
            if (self->rx_hdr.magic != VTP_MAGIC ||
                self->rx_hdr.payload_size > VTP_MAX_META_PAYLOAD) {
              self->Close();
              self->owner->RemoveTcpClient(self);
              return;
            }
            if (self->rx_hdr.payload_size == 0) {
              self->owner->HandleClientUpstreamPacket(
                  self, nullptr, self->rx_hdr, nullptr, 0);
              self->ReadHeader();
            } else {
              self->rx_payload.resize(self->rx_hdr.payload_size);
              asio::async_read(
                  self->socket, asio::buffer(self->rx_payload),
                  [self](const asio::error_code &ec2, std::size_t) {
                    if (ec2) {
                      self->Close();
                      self->owner->RemoveTcpClient(self);
                      return;
                    }
                    self->owner->HandleClientUpstreamPacket(
                        self, nullptr, self->rx_hdr, self->rx_payload.data(),
                        self->rx_payload.size());
                    self->ReadHeader();
                  });
            }
          });
    }
  };

  struct UdpSubscriber {
    asio::ip::udp::endpoint endpoint;
    std::chrono::steady_clock::time_point last_seen;
    vtp_stream_tier_t tier = VTP_TIER_FULL;
    bool tally_pgm = false;
    bool tally_pvw = false;
  };

  std::string stream_name_;
  std::string bind_ip_;
  uint16_t configured_port_ = 0;
  uint16_t port_ = 0;
  int width_ = 1920;
  int height_ = 1080;
  int fps_ = 30;
  bool enable_alpha_ = false;
  bool enable_audio_ = true;
  vtp_transport_t transport_ = VTP_TRANSPORT_TCP;
  std::atomic<int> scale_quality_{90};
  int slice_threads_ = 0;
  bool enable_proxy_ = false;
  int proxy_quality_ = 65;
  vtp_audio_codec_t audio_codec_ = VTP_AUDIO_CODEC_PCM;
  int opus_bitrate_ = 128000;
  bool enable_fec_ = false;

  std::mutex encode_mutex_;
  std::unique_ptr<VTPJpegWorkerPool> compress_pool_;
  std::vector<uint8_t> alpha_extract_buf_;
  std::vector<uint8_t> yuv_scratch_;
  std::vector<uint8_t> proxy_rgb_scratch_;
  std::vector<uint8_t> proxy_alpha_scratch_;
  std::vector<uint8_t> proxy_yuv_scratch_;
  std::vector<SliceWork> fill_slices_;
  std::vector<SliceWork> key_slices_;
  uint32_t frame_index_ = 0;

  std::mutex audio_mutex_;
  audio::VTPOpusEncoder opus_encoder_;

  asio::io_context io_context_;
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>>
      work_guard_;
  std::thread io_thread_;
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;
  std::unique_ptr<asio::ip::udp::socket> udp_socket_;
  std::vector<uint8_t> udp_rx_buf_;
  asio::ip::udp::endpoint udp_rx_endpoint_;

  std::mutex clients_mutex_;
  std::vector<std::shared_ptr<TcpClient>> tcp_clients_;
  std::vector<UdpSubscriber> udp_subscribers_;
  std::atomic<uint32_t> client_count_{0};
  std::atomic<uint32_t> proxy_client_count_{0};
  std::atomic<uint32_t> udp_msg_seq_{1};

  std::atomic<bool> running_{false};
  std::unique_ptr<VTPDiscoveryAdvertiser> advertiser_;

  std::mutex cb_mutex_;
  vtp_sender_callbacks_t callbacks_{};
  void *cb_user_data_ = nullptr;

  std::mutex tally_mutex_;
  vtp_tally_state_t current_tally_{false, false};

  std::atomic<uint64_t> bytes_sent_{0};
  std::atomic<uint64_t> frames_processed_{0};
  std::atomic<uint64_t> frames_dropped_{0};
  std::atomic<uint64_t> audio_packets_{0};
  std::atomic<uint32_t> last_codec_us_{0};

  mutable std::mutex stats_rate_mutex_;
  mutable std::chrono::steady_clock::time_point last_stats_time_ =
      std::chrono::steady_clock::now();
  mutable uint64_t last_stats_bytes_ = 0;
  mutable uint64_t last_stats_frames_ = 0;
  mutable double cached_mbps_ = 0.0;
  mutable double cached_fps_ = 0.0;

  void AddSentBytes(size_t n) {
    bytes_sent_.fetch_add(n, std::memory_order_relaxed);
  }

  void InitFromConfig(const vtp_sender_config_t &cfg) {
    stream_name_ = cfg.stream_name ? cfg.stream_name : "VTP-Stream";
    bind_ip_ = cfg.bind_ip ? cfg.bind_ip : "0.0.0.0";
    configured_port_ = cfg.port;
    width_ = std::max(2, cfg.width);
    height_ = std::max(2, cfg.height);
    fps_ = std::max(1, cfg.fps);
    enable_alpha_ = cfg.enable_alpha;
    enable_audio_ = cfg.enable_audio;
    transport_ = cfg.transport;
    scale_quality_.store(ClampQuality(cfg.quality > 0 ? cfg.quality : 90));
    slice_threads_ = cfg.slice_threads;
    enable_proxy_ = cfg.enable_proxy;
    proxy_quality_ = ClampQuality(cfg.proxy_quality > 0 ? cfg.proxy_quality : 65);
    audio_codec_ = cfg.audio_codec;
    opus_bitrate_ = cfg.opus_bitrate > 0 ? cfg.opus_bitrate : 128000;
    enable_fec_ = cfg.enable_fec;

    int pool_threads =
        slice_threads_ > 0
            ? slice_threads_
            : static_cast<int>(
                  std::min(4u, std::max(1u, std::thread::hardware_concurrency())));
    compress_pool_ = std::make_unique<VTPJpegWorkerPool>(pool_threads, true);
  }

  void DoAccept() {
    if (!running_ || !acceptor_)
      return;
    auto self = shared_from_this();
    auto sock = std::make_shared<asio::ip::tcp::socket>(io_context_);
    acceptor_->async_accept(*sock, [self, sock](const asio::error_code &ec) {
      if (!ec && self->running_) {
        asio::error_code opt_ec;
        sock->set_option(asio::ip::tcp::no_delay(true), opt_ec);
        auto client = std::make_shared<TcpClient>(self.get(), self->io_context_);
        client->socket = std::move(*sock);
        uint32_t new_count = 0;
        {
          std::lock_guard<std::mutex> lock(self->clients_mutex_);
          self->tcp_clients_.push_back(client);
          new_count = self->UpdateClientCountsLocked();
        }
        client->Start();
        self->NotifyClientCount(new_count);
      }
      if (self->running_ && self->acceptor_ && self->acceptor_->is_open()) {
        self->DoAccept();
      }
    });
  }

  void DoUdpReceive() {
    if (!running_ || !udp_socket_)
      return;
    auto self = shared_from_this();
    udp_rx_buf_.resize(4096);
    udp_socket_->async_receive_from(
        asio::buffer(udp_rx_buf_), udp_rx_endpoint_,
        [self](const asio::error_code &ec, std::size_t bytes) {
          if (!ec && bytes >= sizeof(VTPPacketHeader)) {
            VTPPacketHeader hdr;
            std::memcpy(&hdr, self->udp_rx_buf_.data(), sizeof(hdr));
            if (hdr.magic == VTP_MAGIC &&
                sizeof(hdr) + hdr.payload_size <= bytes) {
              self->TouchUdpSubscriber(self->udp_rx_endpoint_);
              self->HandleClientUpstreamPacket(
                  nullptr, &self->udp_rx_endpoint_, hdr,
                  self->udp_rx_buf_.data() + sizeof(hdr), hdr.payload_size);
            }
          }
          if (self->running_ && self->udp_socket_ &&
              self->udp_socket_->is_open()) {
            self->DoUdpReceive();
          }
        });
  }

  void TouchUdpSubscriber(const asio::ip::udp::endpoint &ep) {
    uint32_t new_count = 0;
    bool count_changed = false;
    auto now = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      bool found = false;
      for (auto &sub : udp_subscribers_) {
        if (sub.endpoint == ep) {
          sub.last_seen = now;
          found = true;
          break;
        }
      }
      if (!found) {
        udp_subscribers_.push_back({ep, now, VTP_TIER_FULL, false, false});
        count_changed = true;
      }
      new_count = UpdateClientCountsLocked();
    }
    if (count_changed)
      NotifyClientCount(new_count);
  }

  uint32_t UpdateClientCountsLocked() {
    uint32_t total = static_cast<uint32_t>(tcp_clients_.size() +
                                           udp_subscribers_.size());
    uint32_t proxy = 0;
    for (const auto &c : tcp_clients_) {
      if (c->tier.load(std::memory_order_relaxed) == VTP_TIER_PROXY)
        ++proxy;
    }
    for (const auto &u : udp_subscribers_) {
      if (u.tier == VTP_TIER_PROXY)
        ++proxy;
    }
    client_count_.store(total, std::memory_order_relaxed);
    proxy_client_count_.store(proxy, std::memory_order_relaxed);
    return total;
  }

  void RemoveTcpClient(const std::shared_ptr<TcpClient> &client) {
    uint32_t new_count = 0;
    bool removed = false;
    {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      auto it = std::find(tcp_clients_.begin(), tcp_clients_.end(), client);
      if (it != tcp_clients_.end()) {
        tcp_clients_.erase(it);
        removed = true;
      }
      new_count = UpdateClientCountsLocked();
    }
    if (removed) {
      RecomputeTally();
      NotifyClientCount(new_count);
    }
  }

  void NotifyClientCount(uint32_t count) {
    vtp_sender_callbacks_t cbs{};
    void *ud = nullptr;
    {
      std::lock_guard<std::mutex> lock(cb_mutex_);
      cbs = callbacks_;
      ud = cb_user_data_;
    }
    if (cbs.on_client_count) {
      cbs.on_client_count(count, ud);
    }
  }

  void RecomputeTally() {
    bool pgm = false;
    bool pvw = false;
    {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      for (const auto &c : tcp_clients_) {
        pgm = pgm || c->tally_pgm.load(std::memory_order_relaxed);
        pvw = pvw || c->tally_pvw.load(std::memory_order_relaxed);
      }
      for (const auto &u : udp_subscribers_) {
        pgm = pgm || u.tally_pgm;
        pvw = pvw || u.tally_pvw;
      }
    }

    bool changed = false;
    vtp_tally_state_t state{pgm, pvw};
    {
      std::lock_guard<std::mutex> lock(tally_mutex_);
      if (current_tally_.on_program != pgm ||
          current_tally_.on_preview != pvw) {
        current_tally_ = state;
        changed = true;
      }
    }
    if (changed) {
      vtp_sender_callbacks_t cbs{};
      void *ud = nullptr;
      {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        cbs = callbacks_;
        ud = cb_user_data_;
      }
      if (cbs.on_tally) {
        cbs.on_tally(&state, ud);
      }
    }
  }

  void HandleClientUpstreamPacket(const std::shared_ptr<TcpClient> &tcp_client,
                                  const asio::ip::udp::endpoint *udp_ep,
                                  const VTPPacketHeader &hdr,
                                  const uint8_t *payload, size_t payload_len) {
    vtp_sender_callbacks_t cbs{};
    void *ud = nullptr;
    {
      std::lock_guard<std::mutex> lock(cb_mutex_);
      cbs = callbacks_;
      ud = cb_user_data_;
    }

    switch (hdr.type) {
    case VTP_PACKET_TALLY: {
      if (payload_len >= sizeof(VTPTallyPacket)) {
        VTPTallyPacket tp;
        std::memcpy(&tp, payload, sizeof(tp));
        if (tcp_client) {
          tcp_client->tally_pgm.store(tp.on_program != 0,
                                      std::memory_order_relaxed);
          tcp_client->tally_pvw.store(tp.on_preview != 0,
                                      std::memory_order_relaxed);
        } else if (udp_ep) {
          std::lock_guard<std::mutex> lock(clients_mutex_);
          for (auto &u : udp_subscribers_) {
            if (u.endpoint == *udp_ep) {
              u.tally_pgm = tp.on_program != 0;
              u.tally_pvw = tp.on_preview != 0;
              break;
            }
          }
        }
        RecomputeTally();
      }
      break;
    }
    case VTP_PACKET_PTZ: {
      if (payload_len >= sizeof(VTPPTZPacket) && cbs.on_ptz) {
        VTPPTZPacket pp;
        std::memcpy(&pp, payload, sizeof(pp));
        vtp_ptz_command_t cmd{};
        cmd.pan = pp.pan;
        cmd.tilt = pp.tilt;
        cmd.zoom = pp.zoom;
        cmd.focus = pp.focus;
        cmd.preset_id = pp.preset_id;
        cmd.action = pp.action;
        cbs.on_ptz(&cmd, ud);
      }
      break;
    }
    case VTP_PACKET_CONTROL: {
      std::string cmd_str(reinterpret_cast<const char *>(payload),
                          payload_len);
      while (!cmd_str.empty() && cmd_str.back() == '\0')
        cmd_str.pop_back();
      if (cmd_str == "tier=proxy" || cmd_str == "tier=full") {
        vtp_stream_tier_t new_tier =
            (cmd_str == "tier=proxy") ? VTP_TIER_PROXY : VTP_TIER_FULL;
        if (tcp_client) {
          tcp_client->tier.store(new_tier, std::memory_order_relaxed);
        }
        std::lock_guard<std::mutex> lock(clients_mutex_);
        if (udp_ep) {
          for (auto &u : udp_subscribers_) {
            if (u.endpoint == *udp_ep) {
              u.tier = new_tier;
              break;
            }
          }
        }
        UpdateClientCountsLocked();
      }
      if (cbs.on_control) {
        cbs.on_control(cmd_str.c_str(), ud);
      }
      break;
    }
    case VTP_PACKET_PING: {
      auto pong = std::make_shared<std::vector<uint8_t>>(sizeof(VTPPacketHeader) +
                                                         payload_len);
      VTPPacketHeader ph = hdr;
      ph.type = VTP_PACKET_PONG;
      ph.payload_size = static_cast<uint32_t>(payload_len);
      std::memcpy(pong->data(), &ph, sizeof(ph));
      if (payload_len > 0) {
        std::memcpy(pong->data() + sizeof(ph), payload, payload_len);
      }
      if (tcp_client) {
        tcp_client->Enqueue(pong, false, frames_dropped_);
      } else if (udp_ep && udp_socket_) {
        asio::error_code ec;
        udp_socket_->send_to(asio::buffer(*pong), *udp_ep, 0, ec);
      }
      break;
    }
    default:
      break;
    }
  }

  void BroadcastPacket(const std::shared_ptr<const std::vector<uint8_t>> &pkt,
                       uint8_t pkt_type, bool is_video, bool filter_tier,
                       vtp_stream_tier_t target_tier) {
    if (!running_)
      return;

    if (transport_ == VTP_TRANSPORT_TCP) {
      std::vector<std::shared_ptr<TcpClient>> clients_copy;
      {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_copy = tcp_clients_;
      }
      for (auto &c : clients_copy) {
        if (filter_tier &&
            c->tier.load(std::memory_order_relaxed) != target_tier) {
          continue;
        }
        if (!c->Enqueue(pkt, is_video, frames_dropped_)) {
          c->Close();
          RemoveTcpClient(c);
        }
      }
    } else {
      auto self = shared_from_this();
      asio::post(io_context_, [self, pkt, pkt_type, filter_tier, target_tier]() {
        self->SendUdpOnIoThread(*pkt, pkt_type, filter_tier, target_tier);
      });
    }
  }

  void SendUdpOnIoThread(const std::vector<uint8_t> &msg, uint8_t pkt_type,
                         bool filter_tier, vtp_stream_tier_t target_tier) {
    if (!running_ || !udp_socket_ || !udp_socket_->is_open())
      return;

    std::vector<asio::ip::udp::endpoint> targets;
    bool pruned = false;
    uint32_t new_count = 0;
    auto now = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> lock(clients_mutex_);
      auto it = std::remove_if(
          udp_subscribers_.begin(), udp_subscribers_.end(),
          [now](const UdpSubscriber &s) {
            return std::chrono::duration_cast<std::chrono::seconds>(now -
                                                                    s.last_seen)
                       .count() >= 5;
          });
      if (it != udp_subscribers_.end()) {
        udp_subscribers_.erase(it, udp_subscribers_.end());
        pruned = true;
      }
      new_count = UpdateClientCountsLocked();
      for (const auto &s : udp_subscribers_) {
        if (!filter_tier || s.tier == target_tier) {
          targets.push_back(s.endpoint);
        }
      }
    }
    if (pruned) {
      RecomputeTally();
      NotifyClientCount(new_count);
    }
    if (targets.empty())
      return;

    const size_t total_len = msg.size();
    const uint16_t data_chunks = static_cast<uint16_t>(
        std::max<size_t>(1, (total_len + VTP_UDP_MTU_PAYLOAD - 1) /
                                VTP_UDP_MTU_PAYLOAD));
    const uint32_t seq = udp_msg_seq_.fetch_add(1, std::memory_order_relaxed);
    const uint8_t fec_k = enable_fec_ ? VTP_UDP_FEC_GROUP_SIZE : 0;

    std::vector<uint8_t> dgram(sizeof(VTPUDPChunkHeader) + VTP_UDP_MTU_PAYLOAD);
    std::vector<uint8_t> parity_buf;

    for (uint16_t i = 0; i < data_chunks; ++i) {
      size_t off = static_cast<size_t>(i) * VTP_UDP_MTU_PAYLOAD;
      uint16_t chunk_len = static_cast<uint16_t>(
          std::min<size_t>(VTP_UDP_MTU_PAYLOAD, total_len - off));

      VTPUDPChunkHeader ch{};
      ch.magic = VTP_UDP_MAGIC;
      ch.message_seq = seq;
      ch.pkt_type = pkt_type;
      ch.fec_group_size = fec_k;
      ch.chunk_idx = i;
      ch.data_chunk_count = data_chunks;
      ch.chunk_size = chunk_len;
      ch.flags = 0;
      ch.reserved = 0;

      std::memcpy(dgram.data(), &ch, sizeof(ch));
      if (chunk_len > 0) {
        std::memcpy(dgram.data() + sizeof(ch), msg.data() + off, chunk_len);
      }

      for (const auto &ep : targets) {
        asio::error_code ec;
        size_t sent = udp_socket_->send_to(
            asio::buffer(dgram.data(), sizeof(ch) + chunk_len), ep, 0, ec);
        if (!ec) {
          bytes_sent_.fetch_add(sent, std::memory_order_relaxed);
        }
      }
    }

    if (enable_fec_ && fec_k > 0) {
      uint16_t num_groups = (data_chunks + fec_k - 1) / fec_k;
      for (uint16_t g = 0; g < num_groups; ++g) {
        uint16_t start = static_cast<uint16_t>(g * fec_k);
        uint16_t end = std::min<uint16_t>(start + fec_k, data_chunks);
        uint16_t max_sz = 0;
        uint16_t last_chunk_len = 0;

        for (uint16_t i = start; i < end; ++i) {
          size_t off = static_cast<size_t>(i) * VTP_UDP_MTU_PAYLOAD;
          uint16_t clen = static_cast<uint16_t>(
              std::min<size_t>(VTP_UDP_MTU_PAYLOAD, total_len - off));
          max_sz = std::max(max_sz, clen);
          last_chunk_len = clen;
        }

        parity_buf.assign(sizeof(VTPUDPChunkHeader) + max_sz, 0);
        uint8_t *p_data = parity_buf.data() + sizeof(VTPUDPChunkHeader);
        for (uint16_t i = start; i < end; ++i) {
          size_t off = static_cast<size_t>(i) * VTP_UDP_MTU_PAYLOAD;
          uint16_t clen = static_cast<uint16_t>(
              std::min<size_t>(VTP_UDP_MTU_PAYLOAD, total_len - off));
          const uint8_t *src = msg.data() + off;
          for (uint16_t b = 0; b < clen; ++b) {
            p_data[b] ^= src[b];
          }
        }

        VTPUDPChunkHeader pch{};
        pch.magic = VTP_UDP_MAGIC;
        pch.message_seq = seq;
        pch.pkt_type = pkt_type;
        pch.fec_group_size = fec_k;
        pch.chunk_idx = g;
        pch.data_chunk_count = data_chunks;
        pch.chunk_size = max_sz;
        pch.flags = VTP_UDP_FLAG_PARITY;
        pch.reserved = last_chunk_len;
        std::memcpy(parity_buf.data(), &pch, sizeof(pch));

        for (const auto &ep : targets) {
          asio::error_code ec;
          size_t sent = udp_socket_->send_to(asio::buffer(parity_buf), ep, 0, ec);
          if (!ec) {
            bytes_sent_.fetch_add(sent, std::memory_order_relaxed);
          }
        }
      }
    }
  }

  std::shared_ptr<std::vector<uint8_t>>
  AssembleVideoPacket(int width, int height, VTPPixelFormat format,
                      bool has_alpha, uint32_t frame_idx, uint64_t ts_ns,
                      const vtp_video_metadata_t *meta,
                      const std::vector<SliceWork> &fill_slices,
                      const std::vector<SliceWork> &key_slices, bool is_proxy) {
    const bool sliced = fill_slices.size() > 1;
    const bool has_ext = (meta != nullptr) || sliced || is_proxy;
    const uint32_t fill_sz =
        static_cast<uint32_t>(CalcSlicesWireSize(fill_slices, sliced));
    const uint32_t key_sz =
        has_alpha ? static_cast<uint32_t>(CalcSlicesWireSize(key_slices, sliced))
                  : 0;

    VTPFrameHeader fhdr{};
    fhdr.magic = VTP_MAGIC;
    fhdr.frame_index = frame_idx;
    fhdr.width = static_cast<uint32_t>(width);
    fhdr.height = static_cast<uint32_t>(height);
    fhdr.pixel_format = static_cast<uint8_t>(format);
    fhdr.has_alpha = has_alpha ? 1 : 0;
    fhdr.reserved = 0;
    fhdr.fill_size = fill_sz;
    fhdr.key_size = key_sz;
    fhdr.timestamp_ns = ts_ns;

    VTPFrameExtMeta ext{};
    if (has_ext) {
      if (meta) {
        ext.timecode_ns = meta->timecode_ns;
        ext.aspect_ratio = meta->aspect_ratio > 0.0f
                               ? meta->aspect_ratio
                               : (static_cast<float>(width) / height);
        ext.color_space = static_cast<uint8_t>(meta->color_space);
        ext.scan_mode = static_cast<uint8_t>(meta->scan_mode);
        ext.full_range = meta->full_range ? 1 : 0;
      } else {
        ext.aspect_ratio = static_cast<float>(width) / height;
        ext.full_range = 1;
      }
      ext.slice_count = static_cast<uint8_t>(fill_slices.size());
    }

    uint32_t payload_sz = static_cast<uint32_t>(
        sizeof(fhdr) + (has_ext ? sizeof(ext) : 0) + fill_sz + key_sz);

    VTPPacketHeader phdr{};
    phdr.magic = VTP_MAGIC;
    phdr.type = VTP_PACKET_VIDEO;
    phdr.reserved[0] = (sliced ? VTP_PKT_FLAG_SLICED : 0) |
                       (has_ext ? VTP_PKT_FLAG_EXT_META : 0) |
                       (is_proxy ? VTP_PKT_FLAG_PROXY : 0);
    phdr.reserved[1] = has_ext ? VTP_PROTOCOL_VERSION : 0;
    phdr.reserved[2] = is_proxy ? VTP_TIER_PROXY : VTP_TIER_FULL;
    phdr.payload_size = payload_sz;
    phdr.timestamp_ns = ts_ns;

    auto pkt = std::make_shared<std::vector<uint8_t>>();
    pkt->reserve(sizeof(phdr) + payload_sz);
    const uint8_t *p1 = reinterpret_cast<const uint8_t *>(&phdr);
    pkt->insert(pkt->end(), p1, p1 + sizeof(phdr));
    const uint8_t *p2 = reinterpret_cast<const uint8_t *>(&fhdr);
    pkt->insert(pkt->end(), p2, p2 + sizeof(fhdr));
    if (has_ext) {
      const uint8_t *p3 = reinterpret_cast<const uint8_t *>(&ext);
      pkt->insert(pkt->end(), p3, p3 + sizeof(ext));
    }
    AppendSlicesToBuffer(*pkt, fill_slices, sliced);
    if (has_alpha) {
      AppendSlicesToBuffer(*pkt, key_slices, sliced);
    }
    return pkt;
  }
};

VTPSender::VTPSender(const std::string &stream_name, int width, int height,
                     int fps, bool enable_alpha, bool enable_audio)
    : impl_(std::make_unique<Impl>()) {
  vtp_sender_config_t cfg{};
  vtp_sender_config_init(&cfg);
  cfg.stream_name = stream_name.c_str();
  cfg.width = width;
  cfg.height = height;
  cfg.fps = fps;
  cfg.enable_alpha = enable_alpha;
  cfg.enable_audio = enable_audio;
  impl_->InitFromConfig(cfg);
}

VTPSender::VTPSender(const vtp_sender_config_t &config)
    : impl_(std::make_unique<Impl>()) {
  impl_->InitFromConfig(config);
}

VTPSender::~VTPSender() { Stop(); }

bool VTPSender::Start() {
  if (impl_->running_)
    return true;
  impl_->running_ = true;

  try {
    impl_->io_context_.restart();
    impl_->work_guard_ = std::make_unique<
        asio::executor_work_guard<asio::io_context::executor_type>>(
        asio::make_work_guard(impl_->io_context_));

    asio::ip::address bind_addr = asio::ip::address_v4::any();
    if (!impl_->bind_ip_.empty()) {
      asio::error_code ec;
      auto parsed = asio::ip::make_address(impl_->bind_ip_, ec);
      if (!ec && parsed.is_v4()) {
        bind_addr = parsed;
      }
    }

    if (impl_->transport_ == VTP_TRANSPORT_TCP) {
      asio::ip::tcp::endpoint endpoint(bind_addr, impl_->configured_port_);
      impl_->acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(
          impl_->io_context_, endpoint);
      impl_->port_ = impl_->acceptor_->local_endpoint().port();
      impl_->DoAccept();
      VTP_LOG_INFO("VTPSender TCP listening on %s:%u",
                   bind_addr.to_string().c_str(), impl_->port_);
    } else {
      asio::ip::udp::endpoint endpoint(bind_addr, impl_->configured_port_);
      impl_->udp_socket_ =
          std::make_unique<asio::ip::udp::socket>(impl_->io_context_, endpoint);
      asio::error_code opt_ec;
      impl_->udp_socket_->set_option(
          asio::socket_base::send_buffer_size(4 * 1024 * 1024), opt_ec);
      impl_->port_ = impl_->udp_socket_->local_endpoint().port();
      impl_->DoUdpReceive();
      VTP_LOG_INFO("VTPSender UDP listening on %s:%u",
                   bind_addr.to_string().c_str(), impl_->port_);
    }

    impl_->io_thread_ = std::thread([this]() { impl_->io_context_.run(); });

    impl_->advertiser_ = std::make_unique<VTPDiscoveryAdvertiser>(
        impl_->stream_name_, impl_->port_, impl_->width_, impl_->height_,
        impl_->fps_, impl_->enable_alpha_, impl_->enable_audio_,
        impl_->transport_ == VTP_TRANSPORT_UDP, impl_->enable_proxy_);
    impl_->advertiser_->Start();
  } catch (const std::exception &e) {
    VTP_LOG_ERROR("VTPSender start failed: %s", e.what());
    Stop();
    return false;
  }

  return true;
}

void VTPSender::Stop() {
  if (!impl_->running_.exchange(false))
    return;

  if (impl_->advertiser_) {
    impl_->advertiser_->Stop();
    impl_->advertiser_.reset();
  }

  asio::post(impl_->io_context_, [this]() {
    asio::error_code ec;
    if (impl_->acceptor_) {
      impl_->acceptor_->close(ec);
    }
    if (impl_->udp_socket_) {
      impl_->udp_socket_->close(ec);
    }
    std::lock_guard<std::mutex> lock(impl_->clients_mutex_);
    for (auto &c : impl_->tcp_clients_) {
      c->Close();
    }
    impl_->tcp_clients_.clear();
    impl_->udp_subscribers_.clear();
    impl_->UpdateClientCountsLocked();
    if (impl_->work_guard_) {
      impl_->work_guard_->reset();
    }
  });

  if (impl_->io_thread_.joinable()) {
    impl_->io_thread_.join();
  }
  impl_->io_context_.stop();

  impl_->acceptor_.reset();
  impl_->udp_socket_.reset();
  impl_->work_guard_.reset();
}

bool VTPSender::SendFrame(const unsigned char *data, VTPPixelFormat format,
                          uint64_t timestamp_ns) {
  return SendFrameEx(data, 0, impl_->width_, impl_->height_, format,
                     timestamp_ns, nullptr);
}

bool VTPSender::SendFrameEx(const unsigned char *data, int stride, int width,
                            int height, VTPPixelFormat format,
                            uint64_t timestamp_ns,
                            const vtp_video_metadata_t *metadata) {
  if (!impl_->running_ || !data || width <= 1 || height <= 1 ||
      width > static_cast<int>(VTP_MAX_FRAME_DIM) ||
      height > static_cast<int>(VTP_MAX_FRAME_DIM)) {
    return false;
  }

  if (impl_->client_count_.load(std::memory_order_relaxed) == 0) {
    return false;
  }

  if (timestamp_ns == 0) {
    timestamp_ns = NowSteadyNs();
  }

  std::lock_guard<std::mutex> lock(impl_->encode_mutex_);
  auto t0 = std::chrono::steady_clock::now();

  if (width != impl_->width_ || height != impl_->height_) {
    impl_->width_ = width;
    impl_->height_ = height;
    if (impl_->advertiser_) {
      impl_->advertiser_->UpdateStreamInfo(width, height, impl_->fps_);
    }
  }

  int req_slices = impl_->slice_threads_;
  if (req_slices <= 0) {
    req_slices = (static_cast<int64_t>(width) * height >= 2560 * 1440) ? 4 : 1;
  }
  req_slices = std::min(req_slices, impl_->compress_pool_->ThreadCount());

  const int quality = impl_->scale_quality_.load(std::memory_order_relaxed);
  bool has_alpha = false;
  bool ok = false;

  if (!IsYUVPixelFormat(format)) {
    int tj_pf = TJPF_RGB;
    int default_bpp = 3;
    switch (format) {
    case VTP_FORMAT_RGB:
      tj_pf = TJPF_RGB;
      default_bpp = 3;
      break;
    case VTP_FORMAT_RGBA:
      tj_pf = TJPF_RGBA;
      default_bpp = 4;
      break;
    case VTP_FORMAT_BGR:
      tj_pf = TJPF_BGR;
      default_bpp = 3;
      break;
    case VTP_FORMAT_BGRA:
      tj_pf = TJPF_BGRA;
      default_bpp = 4;
      break;
    default:
      return false;
    }
    int eff_stride = stride > 0 ? stride : (width * default_bpp);
    ok = CompressSlicesPacked(*impl_->compress_pool_, data, width, eff_stride,
                              height, tj_pf, TJSAMP_422, quality, req_slices,
                              impl_->fill_slices_);
    if (!ok) {
      VTP_LOG_ERROR("Fill JPEG compression failed");
      return false;
    }

    has_alpha = impl_->enable_alpha_ &&
                (format == VTP_FORMAT_RGBA || format == VTP_FORMAT_BGRA);
    if (has_alpha) {
      size_t num_px = static_cast<size_t>(width) * height;
      impl_->alpha_extract_buf_.resize(num_px);
      simd::ExtractAlphaStrided(data, eff_stride,
                                impl_->alpha_extract_buf_.data(), width,
                                height);
      if (!CompressSlicesPacked(*impl_->compress_pool_,
                                impl_->alpha_extract_buf_.data(), width, width,
                                height, TJPF_GRAY, TJSAMP_GRAY, quality,
                                static_cast<int>(impl_->fill_slices_.size()),
                                impl_->key_slices_)) {
        VTP_LOG_ERROR("Key JPEG compression failed");
        return false;
      }
    }
  } else {
    if ((width & 1) != 0 || (height & 1) != 0)
      return false;
    const int cw = width / 2;
    const int y_stride = stride > 0 ? stride : width;
    const unsigned char *planes[3] = {nullptr, nullptr, nullptr};
    int strides[3] = {0, 0, 0};
    int subsamp = TJSAMP_420;

    if (format == VTP_FORMAT_UYVY || format == VTP_FORMAT_YUY2) {
      int packed_stride = stride > 0 ? stride : width * 2;
      size_t y_sz = static_cast<size_t>(width) * height;
      size_t c_sz = static_cast<size_t>(cw) * height;
      impl_->yuv_scratch_.resize(y_sz + c_sz * 2);
      uint8_t *yp = impl_->yuv_scratch_.data();
      uint8_t *up = yp + y_sz;
      uint8_t *vp = up + c_sz;
      if (format == VTP_FORMAT_UYVY) {
        simd::UnpackUYVYToYUV422Planes(data, packed_stride, yp, width, up, cw,
                                        vp, cw, width, height);
      } else {
        simd::UnpackYUY2ToYUV422Planes(data, packed_stride, yp, width, up, cw,
                                        vp, cw, width, height);
      }
      planes[0] = yp;
      planes[1] = up;
      planes[2] = vp;
      strides[0] = width;
      strides[1] = cw;
      strides[2] = cw;
      subsamp = TJSAMP_422;
    } else if (format == VTP_FORMAT_NV12) {
      const int ch = height / 2;
      size_t c_sz = static_cast<size_t>(cw) * ch;
      impl_->yuv_scratch_.resize(c_sz * 2);
      uint8_t *up = impl_->yuv_scratch_.data();
      uint8_t *vp = up + c_sz;
      simd::SplitNV12UVToPlanes(data + static_cast<size_t>(y_stride) * height,
                                y_stride, up, cw, vp, cw, cw, ch);
      planes[0] = data;
      planes[1] = up;
      planes[2] = vp;
      strides[0] = y_stride;
      strides[1] = cw;
      strides[2] = cw;
      subsamp = TJSAMP_420;
    } else if (format == VTP_FORMAT_I422) {
      size_t c_sz = static_cast<size_t>(cw) * height;
      planes[0] = data;
      planes[1] = data + static_cast<size_t>(y_stride) * height;
      planes[2] = planes[1] + c_sz;
      strides[0] = y_stride;
      strides[1] = cw;
      strides[2] = cw;
      subsamp = TJSAMP_422;
    } else {
      size_t c_sz = static_cast<size_t>(cw) * (height / 2);
      const uint8_t *p1 = data + static_cast<size_t>(y_stride) * height;
      const uint8_t *p2 = p1 + c_sz;
      planes[0] = data;
      planes[1] = (format == VTP_FORMAT_YV12) ? p2 : p1;
      planes[2] = (format == VTP_FORMAT_YV12) ? p1 : p2;
      strides[0] = y_stride;
      strides[1] = cw;
      strides[2] = cw;
      subsamp = TJSAMP_420;
    }

    ok = CompressSlicesYUVPlanes(*impl_->compress_pool_, planes, strides, width,
                                 height, subsamp, quality, req_slices,
                                 impl_->fill_slices_);
    if (!ok) {
      VTP_LOG_ERROR("YUV JPEG compression failed");
      return false;
    }
  }

  const uint32_t fidx = impl_->frame_index_++;
  auto full_pkt = impl_->AssembleVideoPacket(
      width, height, format, has_alpha, fidx, timestamp_ns, metadata,
      impl_->fill_slices_, impl_->key_slices_, false);

  const bool send_proxy =
      impl_->enable_proxy_ &&
      impl_->proxy_client_count_.load(std::memory_order_relaxed) > 0 &&
      width >= 64 && height >= 64;

  if (send_proxy) {
    impl_->BroadcastPacket(full_pkt, VTP_PACKET_VIDEO, true, true,
                           VTP_TIER_FULL);

    int pw = (width / 2) & ~1;
    int ph = (height / 2) & ~1;
    std::vector<SliceWork> p_fill, p_key;
    bool proxy_ok = false;

    if (!IsYUVPixelFormat(format)) {
      int bpp = (format == VTP_FORMAT_RGBA || format == VTP_FORMAT_BGRA) ? 4 : 3;
      int tj_pf = TJPF_RGB;
      if (format == VTP_FORMAT_RGBA)
        tj_pf = TJPF_RGBA;
      else if (format == VTP_FORMAT_BGR)
        tj_pf = TJPF_BGR;
      else if (format == VTP_FORMAT_BGRA)
        tj_pf = TJPF_BGRA;
      int src_stride = stride > 0 ? stride : width * bpp;
      int dst_stride = pw * bpp;
      impl_->proxy_rgb_scratch_.resize(static_cast<size_t>(dst_stride) * ph);
      if (bpp == 4) {
        simd::DownscaleRGBAHalf(data, src_stride,
                                impl_->proxy_rgb_scratch_.data(), dst_stride,
                                pw, ph);
      } else {
        simd::DownscaleRGBHalf(data, src_stride,
                               impl_->proxy_rgb_scratch_.data(), dst_stride,
                               pw, ph);
      }
      proxy_ok = CompressSlicesPacked(*impl_->compress_pool_,
                                      impl_->proxy_rgb_scratch_.data(), pw,
                                      dst_stride, ph, tj_pf, TJSAMP_420,
                                      impl_->proxy_quality_, 1, p_fill);
      if (proxy_ok && has_alpha) {
        impl_->proxy_alpha_scratch_.resize(static_cast<size_t>(pw) * ph);
        simd::DownscaleGrayHalf(impl_->alpha_extract_buf_.data(), width,
                                impl_->proxy_alpha_scratch_.data(), pw, pw, ph);
        CompressSlicesPacked(*impl_->compress_pool_,
                             impl_->proxy_alpha_scratch_.data(), pw, pw, ph,
                             TJPF_GRAY, TJSAMP_GRAY, impl_->proxy_quality_, 1,
                             p_key);
      }
    } else {
      const uint8_t *sy = data;
      const uint8_t *su = nullptr;
      const uint8_t *sv = nullptr;
      int sys = stride > 0 ? stride : width;
      int scs = width / 2;
      bool src_422 = false;
      if (format == VTP_FORMAT_UYVY || format == VTP_FORMAT_YUY2) {
        sy = impl_->yuv_scratch_.data();
        su = sy + static_cast<size_t>(width) * height;
        sv = su + static_cast<size_t>(width / 2) * height;
        sys = width;
        src_422 = true;
      } else if (format == VTP_FORMAT_NV12) {
        su = impl_->yuv_scratch_.data();
        sv = su + static_cast<size_t>(width / 2) * (height / 2);
      } else if (format == VTP_FORMAT_I422) {
        su = data + static_cast<size_t>(sys) * height;
        sv = su + static_cast<size_t>(scs) * height;
        src_422 = true;
      } else if (format == VTP_FORMAT_YV12) {
        sv = data + static_cast<size_t>(sys) * height;
        su = sv + static_cast<size_t>(scs) * (height / 2);
      } else {
        su = data + static_cast<size_t>(sys) * height;
        sv = su + static_cast<size_t>(scs) * (height / 2);
      }

      int pcw = pw / 2;
      int pch = src_422 ? ph : (ph / 2);
      size_t py_sz = static_cast<size_t>(pw) * ph;
      size_t pc_sz = static_cast<size_t>(pcw) * pch;
      impl_->proxy_yuv_scratch_.resize(py_sz + pc_sz * 2);
      uint8_t *py = impl_->proxy_yuv_scratch_.data();
      uint8_t *pu = py + py_sz;
      uint8_t *pv = pu + pc_sz;

      simd::DownscalePlaneHalf(sy, sys, py, pw, pw, ph);
      simd::DownscalePlaneHalf(su, scs, pu, pcw, pcw, pch);
      simd::DownscalePlaneHalf(sv, scs, pv, pcw, pcw, pch);
      const unsigned char *pplanes[3] = {py, pu, pv};
      int pstrides[3] = {pw, pcw, pcw};
      proxy_ok = CompressSlicesYUVPlanes(
          *impl_->compress_pool_, pplanes, pstrides, pw, ph,
          src_422 ? TJSAMP_422 : TJSAMP_420, impl_->proxy_quality_, 1, p_fill);
    }

    if (proxy_ok) {
      auto proxy_pkt = impl_->AssembleVideoPacket(
          pw, ph, format, has_alpha && !p_key.empty(), fidx, timestamp_ns,
          metadata, p_fill, p_key, true);
      impl_->BroadcastPacket(proxy_pkt, VTP_PACKET_VIDEO, true, true,
                             VTP_TIER_PROXY);
    }
  } else {
    impl_->BroadcastPacket(full_pkt, VTP_PACKET_VIDEO, true, false,
                           VTP_TIER_FULL);
  }

  auto t1 = std::chrono::steady_clock::now();
  impl_->last_codec_us_.store(
      static_cast<uint32_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
              .count()),
      std::memory_order_relaxed);
  impl_->frames_processed_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

bool VTPSender::SendFramePlanar(const unsigned char *const *planes,
                                const int *strides, int width, int height,
                                VTPPixelFormat format, uint64_t timestamp_ns,
                                const vtp_video_metadata_t *metadata) {
  if (!impl_->running_ || !planes || !strides || !planes[0] || width <= 1 ||
      height <= 1 || width > static_cast<int>(VTP_MAX_FRAME_DIM) ||
      height > static_cast<int>(VTP_MAX_FRAME_DIM)) {
    return false;
  }

  if (impl_->client_count_.load(std::memory_order_relaxed) == 0)
    return false;

  if (!IsYUVPixelFormat(format) || format == VTP_FORMAT_UYVY ||
      format == VTP_FORMAT_YUY2) {
    return SendFrameEx(planes[0], strides[0], width, height, format,
                       timestamp_ns, metadata);
  }

  if ((width & 1) != 0 || (height & 1) != 0)
    return false;
  if (timestamp_ns == 0)
    timestamp_ns = NowSteadyNs();

  std::lock_guard<std::mutex> lock(impl_->encode_mutex_);
  auto t0 = std::chrono::steady_clock::now();

  if (width != impl_->width_ || height != impl_->height_) {
    impl_->width_ = width;
    impl_->height_ = height;
    if (impl_->advertiser_) {
      impl_->advertiser_->UpdateStreamInfo(width, height, impl_->fps_);
    }
  }

  const int cw = width / 2;
  const unsigned char *yuv_planes[3] = {nullptr, nullptr, nullptr};
  int yuv_strides[3] = {0, 0, 0};
  int subsamp = TJSAMP_420;

  if (format == VTP_FORMAT_NV12) {
    if (!planes[1])
      return false;
    const int ch = height / 2;
    size_t c_sz = static_cast<size_t>(cw) * ch;
    impl_->yuv_scratch_.resize(c_sz * 2);
    uint8_t *up = impl_->yuv_scratch_.data();
    uint8_t *vp = up + c_sz;
    simd::SplitNV12UVToPlanes(planes[1], strides[1], up, cw, vp, cw, cw, ch);
    yuv_planes[0] = planes[0];
    yuv_planes[1] = up;
    yuv_planes[2] = vp;
    yuv_strides[0] = strides[0];
    yuv_strides[1] = cw;
    yuv_strides[2] = cw;
    subsamp = TJSAMP_420;
  } else {
    if (!planes[1] || !planes[2])
      return false;
    yuv_planes[0] = planes[0];
    if (format == VTP_FORMAT_YV12) {
      yuv_planes[1] = planes[2];
      yuv_planes[2] = planes[1];
      yuv_strides[0] = strides[0];
      yuv_strides[1] = strides[2];
      yuv_strides[2] = strides[1];
    } else {
      yuv_planes[1] = planes[1];
      yuv_planes[2] = planes[2];
      yuv_strides[0] = strides[0];
      yuv_strides[1] = strides[1];
      yuv_strides[2] = strides[2];
    }
    subsamp = (format == VTP_FORMAT_I422) ? TJSAMP_422 : TJSAMP_420;
  }

  int req_slices = impl_->slice_threads_;
  if (req_slices <= 0) {
    req_slices = (static_cast<int64_t>(width) * height >= 2560 * 1440) ? 4 : 1;
  }
  req_slices = std::min(req_slices, impl_->compress_pool_->ThreadCount());

  const int quality = impl_->scale_quality_.load(std::memory_order_relaxed);
  if (!CompressSlicesYUVPlanes(*impl_->compress_pool_, yuv_planes, yuv_strides,
                               width, height, subsamp, quality, req_slices,
                               impl_->fill_slices_)) {
    VTP_LOG_ERROR("Planar YUV JPEG compression failed");
    return false;
  }

  const uint32_t fidx = impl_->frame_index_++;
  auto full_pkt = impl_->AssembleVideoPacket(
      width, height, format, false, fidx, timestamp_ns, metadata,
      impl_->fill_slices_, impl_->key_slices_, false);

  const bool send_proxy =
      impl_->enable_proxy_ &&
      impl_->proxy_client_count_.load(std::memory_order_relaxed) > 0 &&
      width >= 64 && height >= 64;

  if (send_proxy) {
    impl_->BroadcastPacket(full_pkt, VTP_PACKET_VIDEO, true, true,
                           VTP_TIER_FULL);
    int pw = (width / 2) & ~1;
    int ph = (height / 2) & ~1;
    int pcw = pw / 2;
    int pch = (subsamp == TJSAMP_422) ? ph : (ph / 2);
    size_t py_sz = static_cast<size_t>(pw) * ph;
    size_t pc_sz = static_cast<size_t>(pcw) * pch;
    impl_->proxy_yuv_scratch_.resize(py_sz + pc_sz * 2);
    uint8_t *py = impl_->proxy_yuv_scratch_.data();
    uint8_t *pu = py + py_sz;
    uint8_t *pv = pu + pc_sz;
    simd::DownscalePlaneHalf(yuv_planes[0], yuv_strides[0], py, pw, pw, ph);
    simd::DownscalePlaneHalf(yuv_planes[1], yuv_strides[1], pu, pcw, pcw, pch);
    simd::DownscalePlaneHalf(yuv_planes[2], yuv_strides[2], pv, pcw, pcw, pch);
    const unsigned char *pp[3] = {py, pu, pv};
    int ps[3] = {pw, pcw, pcw};
    std::vector<SliceWork> p_fill, p_key;
    if (CompressSlicesYUVPlanes(*impl_->compress_pool_, pp, ps, pw, ph, subsamp,
                                impl_->proxy_quality_, 1, p_fill)) {
      auto proxy_pkt = impl_->AssembleVideoPacket(
          pw, ph, format, false, fidx, timestamp_ns, metadata, p_fill, p_key,
          true);
      impl_->BroadcastPacket(proxy_pkt, VTP_PACKET_VIDEO, true, true,
                             VTP_TIER_PROXY);
    }
  } else {
    impl_->BroadcastPacket(full_pkt, VTP_PACKET_VIDEO, true, false,
                           VTP_TIER_FULL);
  }

  auto t1 = std::chrono::steady_clock::now();
  impl_->last_codec_us_.store(
      static_cast<uint32_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
              .count()),
      std::memory_order_relaxed);
  impl_->frames_processed_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

bool VTPSender::SendAudio(const unsigned char *pcm_data, int sample_count,
                          uint32_t sample_rate, uint8_t channels,
                          uint64_t timestamp_ns) {
  return SendAudioEx(pcm_data, sample_count, sample_rate, channels,
                     VTP_AUDIO_PCM_S16LE, timestamp_ns);
}

bool VTPSender::SendAudioEx(const void *pcm_data, int sample_count,
                            uint32_t sample_rate, uint8_t channels,
                            vtp_audio_format_t format, uint64_t timestamp_ns) {
  if (!impl_->running_ || !impl_->enable_audio_ || !pcm_data ||
      sample_count <= 0 || channels == 0 || channels > VTP_MAX_CHANNELS ||
      sample_rate == 0) {
    return false;
  }

  if (impl_->client_count_.load(std::memory_order_relaxed) == 0)
    return false;

  if (timestamp_ns == 0)
    timestamp_ns = NowSteadyNs();

  std::lock_guard<std::mutex> lock(impl_->audio_mutex_);

  if (impl_->audio_codec_ == VTP_AUDIO_CODEC_OPUS &&
      impl_->opus_encoder_.IsAvailable()) {
    std::vector<audio::EncodedOpusPacket> opus_pkts;
    if (impl_->opus_encoder_.Encode(pcm_data, format, sample_count, sample_rate,
                                    channels, timestamp_ns, opus_pkts)) {
      for (const auto &op : opus_pkts) {
        VTPAudioMetadata meta{};
        meta.sample_rate = sample_rate;
        meta.channels = channels;
        meta.sample_format =
            vtp_encode_audio_format(VTP_AUDIO_CODEC_OPUS, VTP_AUDIO_PCM_F32LE);
        meta.sample_count = op.sample_count;
        meta.data_size = static_cast<uint32_t>(op.data.size());

        VTPPacketHeader pkt_hdr{};
        pkt_hdr.magic = VTP_MAGIC;
        pkt_hdr.type = VTP_PACKET_AUDIO;
        pkt_hdr.reserved[1] = VTP_PROTOCOL_VERSION;
        pkt_hdr.payload_size =
            static_cast<uint32_t>(sizeof(meta) + op.data.size());
        pkt_hdr.timestamp_ns = op.timestamp_ns;

        auto pkt = std::make_shared<std::vector<uint8_t>>(sizeof(pkt_hdr) +
                                                          pkt_hdr.payload_size);
        std::memcpy(pkt->data(), &pkt_hdr, sizeof(pkt_hdr));
        std::memcpy(pkt->data() + sizeof(pkt_hdr), &meta, sizeof(meta));
        if (!op.data.empty()) {
          std::memcpy(pkt->data() + sizeof(pkt_hdr) + sizeof(meta),
                      op.data.data(), op.data.size());
        }
        impl_->BroadcastPacket(pkt, VTP_PACKET_AUDIO, false, false,
                               VTP_TIER_FULL);
        impl_->audio_packets_.fetch_add(1, std::memory_order_relaxed);
      }
      return true;
    }
  }

  const size_t data_size = audio::AudioPayloadSize(format, sample_count, channels);
  if (data_size > VTP_MAX_AUDIO_PAYLOAD)
    return false;

  VTPAudioMetadata meta{};
  meta.sample_rate = sample_rate;
  meta.channels = channels;
  meta.sample_format = vtp_encode_audio_format(VTP_AUDIO_CODEC_PCM, format);
  meta.sample_count = static_cast<uint16_t>(std::min(sample_count, 65535));
  meta.data_size = static_cast<uint32_t>(data_size);

  VTPPacketHeader pkt_hdr{};
  pkt_hdr.magic = VTP_MAGIC;
  pkt_hdr.type = VTP_PACKET_AUDIO;
  pkt_hdr.reserved[1] =
      (format == VTP_AUDIO_PCM_S16LE) ? 0 : VTP_PROTOCOL_VERSION;
  pkt_hdr.payload_size = static_cast<uint32_t>(sizeof(meta) + data_size);
  pkt_hdr.timestamp_ns = timestamp_ns;

  auto pkt = std::make_shared<std::vector<uint8_t>>(sizeof(pkt_hdr) +
                                                    pkt_hdr.payload_size);
  std::memcpy(pkt->data(), &pkt_hdr, sizeof(pkt_hdr));
  std::memcpy(pkt->data() + sizeof(pkt_hdr), &meta, sizeof(meta));
  std::memcpy(pkt->data() + sizeof(pkt_hdr) + sizeof(meta), pcm_data,
              data_size);

  impl_->BroadcastPacket(pkt, VTP_PACKET_AUDIO, false, false, VTP_TIER_FULL);
  impl_->audio_packets_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

bool VTPSender::SendMetadata(const char *metadata_utf8, uint64_t timestamp_ns) {
  if (!impl_->running_ || !metadata_utf8)
    return false;
  if (impl_->client_count_.load(std::memory_order_relaxed) == 0)
    return false;

  size_t len = std::strlen(metadata_utf8);
  if (len > VTP_MAX_META_PAYLOAD)
    return false;
  if (timestamp_ns == 0)
    timestamp_ns = NowSteadyNs();

  VTPPacketHeader pkt_hdr{};
  pkt_hdr.magic = VTP_MAGIC;
  pkt_hdr.type = VTP_PACKET_METADATA;
  pkt_hdr.reserved[1] = VTP_PROTOCOL_VERSION;
  pkt_hdr.payload_size = static_cast<uint32_t>(len);
  pkt_hdr.timestamp_ns = timestamp_ns;

  auto pkt = std::make_shared<std::vector<uint8_t>>(sizeof(pkt_hdr) + len);
  std::memcpy(pkt->data(), &pkt_hdr, sizeof(pkt_hdr));
  if (len > 0) {
    std::memcpy(pkt->data() + sizeof(pkt_hdr), metadata_utf8, len);
  }
  impl_->BroadcastPacket(pkt, VTP_PACKET_METADATA, false, false, VTP_TIER_FULL);
  return true;
}

void VTPSender::SetScaleQuality(int quality) {
  impl_->scale_quality_.store(ClampQuality(quality), std::memory_order_relaxed);
}

void VTPSender::SetCallbacks(const vtp_sender_callbacks_t *callbacks,
                             void *user_data) {
  std::lock_guard<std::mutex> lock(impl_->cb_mutex_);
  if (callbacks) {
    impl_->callbacks_ = *callbacks;
  } else {
    std::memset(&impl_->callbacks_, 0, sizeof(impl_->callbacks_));
  }
  impl_->cb_user_data_ = user_data;
}

uint16_t VTPSender::GetPort() const { return impl_->port_; }

std::string VTPSender::GetStreamName() const { return impl_->stream_name_; }

bool VTPSender::GetTally(vtp_tally_state_t *out_tally) const {
  if (!out_tally)
    return false;
  std::lock_guard<std::mutex> lock(impl_->tally_mutex_);
  *out_tally = impl_->current_tally_;
  return true;
}

uint32_t VTPSender::GetClientCount() const {
  return impl_->client_count_.load(std::memory_order_relaxed);
}

bool VTPSender::GetStats(vtp_stats_t *out_stats) const {
  if (!out_stats)
    return false;

  uint64_t bytes = impl_->bytes_sent_.load(std::memory_order_relaxed);
  uint64_t frames = impl_->frames_processed_.load(std::memory_order_relaxed);
  uint64_t dropped = impl_->frames_dropped_.load(std::memory_order_relaxed);
  uint64_t apkts = impl_->audio_packets_.load(std::memory_order_relaxed);

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
      impl_->last_codec_us_.load(std::memory_order_relaxed);
  out_stats->connected_clients =
      impl_->client_count_.load(std::memory_order_relaxed);
  return true;
}

} // namespace vtp
