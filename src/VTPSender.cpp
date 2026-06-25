#include "VTPSender.h"
#include "VTPDiscovery.h"
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <turbojpeg.h>
#include <vector>

namespace vtp {

struct VTPSender::Impl {
  std::string stream_name_;
  int width_;
  int height_;
  int fps_;
  bool enable_alpha_;
  bool enable_audio_;
  uint16_t port_ = 0;
  std::atomic<int> scale_quality_{90};

  tjhandle tj_compressor_ = nullptr;
  std::vector<unsigned char> alpha_extract_buffer_;
  asio::io_context io_context_;
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;
  std::vector<std::shared_ptr<asio::ip::tcp::socket>> clients_;
  std::mutex clients_mutex_;
  std::thread accept_thread_;
  std::atomic<bool> running_{false};
  std::unique_ptr<VTPDiscoveryAdvertiser> advertiser_;
  uint32_t frame_index_ = 0;

  void AcceptLoop() {
    while (running_) {
      try {
        asio::error_code ec;
        auto socket = std::make_shared<asio::ip::tcp::socket>(io_context_);
        acceptor_->accept(*socket, ec);

        if (!ec) {
          socket->set_option(asio::ip::tcp::no_delay(true));
          std::lock_guard<std::mutex> lock(clients_mutex_);
          clients_.push_back(socket);
          std::cout << "[VTPSender] Client connected from "
                    << socket->remote_endpoint().address().to_string()
                    << std::endl;
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      } catch (...) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }

  void BroadcastBuffers(const std::vector<asio::const_buffer> &buffers) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto it = clients_.begin(); it != clients_.end();) {
      asio::error_code ec;
      asio::write(*(*it), buffers, ec);
      if (ec) {
        std::cout << "[VTPSender] Client disconnected (write failed: "
                  << ec.message() << ")" << std::endl;
        it = clients_.erase(it);
      } else {
        ++it;
      }
    }
  }
};

VTPSender::VTPSender(const std::string &stream_name, int width, int height,
                     int fps, bool enable_alpha, bool enable_audio)
    : impl_(std::make_unique<Impl>()) {
  impl_->stream_name_ = stream_name;
  impl_->width_ = width;
  impl_->height_ = height;
  impl_->fps_ = fps;
  impl_->enable_alpha_ = enable_alpha;
  impl_->enable_audio_ = enable_audio;
  impl_->tj_compressor_ = tjInitCompress();
}

VTPSender::~VTPSender() {
  Stop();
  if (impl_->tj_compressor_) {
    tjDestroy(impl_->tj_compressor_);
  }
}

bool VTPSender::Start() {
  if (impl_->running_)
    return true;
  impl_->running_ = true;

  try {
    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 0);
    impl_->acceptor_ =
        std::make_unique<asio::ip::tcp::acceptor>(impl_->io_context_, endpoint);
    impl_->port_ = impl_->acceptor_->local_endpoint().port();
    std::cout << "[VTPSender] TCP listening on port " << impl_->port_
              << std::endl;
    impl_->accept_thread_ =
        std::thread(&VTPSender::Impl::AcceptLoop, impl_.get());
    impl_->advertiser_ = std::make_unique<VTPDiscoveryAdvertiser>(
        impl_->stream_name_, impl_->port_, impl_->width_, impl_->height_,
        impl_->fps_, impl_->enable_alpha_, impl_->enable_audio_);
    impl_->advertiser_->Start();
  } catch (const std::exception &e) {
    std::cerr << "[VTPSender] Start failed: " << e.what() << std::endl;
    Stop();
    return false;
  }

  return true;
}

void VTPSender::Stop() {
  if (!impl_->running_)
    return;
  impl_->running_ = false;
  if (impl_->advertiser_) {
    impl_->advertiser_->Stop();
    impl_->advertiser_.reset();
  }

  if (impl_->acceptor_) {
    impl_->acceptor_->close();
  }

  if (impl_->accept_thread_.joinable()) {
    impl_->accept_thread_.join();
  }

  std::lock_guard<std::mutex> lock(impl_->clients_mutex_);
  for (auto &client : impl_->clients_) {
    asio::error_code ec;
    client->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    client->close(ec);
  }
  impl_->clients_.clear();
}

bool VTPSender::SendFrame(const unsigned char *data, VTPPixelFormat format,
                          uint64_t timestamp_ns) {
  if (!impl_->running_ || !impl_->tj_compressor_)
    return false;
  {
    std::lock_guard<std::mutex> lock(impl_->clients_mutex_);
    if (impl_->clients_.empty())
      return false;
  }

  if (timestamp_ns == 0) {
    timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
  }

  int tj_pf = TJPF_RGB;
  switch (format) {
  case VTP_FORMAT_RGB:
    tj_pf = TJPF_RGB;
    break;
  case VTP_FORMAT_RGBA:
    tj_pf = TJPF_RGBA;
    break;
  case VTP_FORMAT_BGR:
    tj_pf = TJPF_BGR;
    break;
  case VTP_FORMAT_BGRA:
    tj_pf = TJPF_BGRA;
    break;
  }

  unsigned char *fill_jpeg_buf = nullptr;
  unsigned long fill_jpeg_size = 0;

  int compress_res =
      tjCompress2(impl_->tj_compressor_, data, impl_->width_, 0, impl_->height_,
                  tj_pf, &fill_jpeg_buf, &fill_jpeg_size, TJSAMP_422,
                  impl_->scale_quality_.load(), 0);

  if (compress_res != 0) {
    std::cerr << "[VTPSender] Fill JPEG compression failed: "
              << tjGetErrorStr2(impl_->tj_compressor_) << std::endl;
    return false;
  }

  unsigned char *key_jpeg_buf = nullptr;
  unsigned long key_jpeg_size = 0;
  bool actually_has_alpha =
      impl_->enable_alpha_ &&
      (format == VTP_FORMAT_RGBA || format == VTP_FORMAT_BGRA);
  if (actually_has_alpha) {
    size_t num_pixels = impl_->width_ * impl_->height_;
    if (impl_->alpha_extract_buffer_.size() < num_pixels) {
      impl_->alpha_extract_buffer_.resize(num_pixels);
    }

    for (size_t i = 0; i < num_pixels; ++i) {
      impl_->alpha_extract_buffer_[i] = data[i * 4 + 3];
    }

    int key_compress_res = tjCompress2(
        impl_->tj_compressor_, impl_->alpha_extract_buffer_.data(),
        impl_->width_, 0, impl_->height_, TJPF_GRAY, &key_jpeg_buf,
        &key_jpeg_size, TJSAMP_GRAY, impl_->scale_quality_.load(), 0);

    if (key_compress_res != 0) {
      std::cerr << "[VTPSender] Key JPEG compression failed: "
                << tjGetErrorStr2(impl_->tj_compressor_) << std::endl;
      tjFree(fill_jpeg_buf);
      return false;
    }
  }

  VTPFrameHeader header;
  header.magic = VTP_MAGIC;
  header.frame_index = impl_->frame_index_++;
  header.width = impl_->width_;
  header.height = impl_->height_;
  header.pixel_format = format;
  header.has_alpha = actually_has_alpha ? 1 : 0;
  header.reserved = 0;
  header.fill_size = fill_jpeg_size;
  header.key_size = actually_has_alpha ? key_jpeg_size : 0;
  header.timestamp_ns = timestamp_ns;
  VTPPacketHeader pkt_header;
  pkt_header.magic = VTP_MAGIC;
  pkt_header.type = VTP_PACKET_VIDEO;
  pkt_header.reserved[0] = pkt_header.reserved[1] = pkt_header.reserved[2] = 0;
  pkt_header.payload_size = sizeof(header) + fill_jpeg_size +
                            (actually_has_alpha ? key_jpeg_size : 0);
  pkt_header.timestamp_ns = timestamp_ns;

  std::vector<asio::const_buffer> buffers;
  buffers.push_back(asio::buffer(&pkt_header, sizeof(pkt_header)));
  buffers.push_back(asio::buffer(&header, sizeof(header)));
  buffers.push_back(asio::buffer(fill_jpeg_buf, fill_jpeg_size));
  if (actually_has_alpha && key_jpeg_size > 0) {
    buffers.push_back(asio::buffer(key_jpeg_buf, key_jpeg_size));
  }
  impl_->BroadcastBuffers(buffers);
  if (fill_jpeg_buf)
    tjFree(fill_jpeg_buf);
  if (key_jpeg_buf)
    tjFree(key_jpeg_buf);

  return true;
}

bool VTPSender::SendAudio(const unsigned char *pcm_data, int sample_count,
                          uint32_t sample_rate, uint8_t channels,
                          uint64_t timestamp_ns) {
  if (!impl_->running_ || !impl_->enable_audio_)
    return false;
  {
    std::lock_guard<std::mutex> lock(impl_->clients_mutex_);
    if (impl_->clients_.empty())
      return false;
  }

  if (timestamp_ns == 0) {
    timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
  }

  uint32_t data_size = sample_count * channels * 2;
  VTPAudioMetadata audio_meta;
  audio_meta.sample_rate = sample_rate;
  audio_meta.channels = channels;
  audio_meta.sample_format = 0;
  audio_meta.sample_count = sample_count;
  audio_meta.data_size = data_size;

  VTPPacketHeader pkt_header;
  pkt_header.magic = VTP_MAGIC;
  pkt_header.type = VTP_PACKET_AUDIO;
  pkt_header.reserved[0] = pkt_header.reserved[1] = pkt_header.reserved[2] = 0;
  pkt_header.payload_size = sizeof(audio_meta) + data_size;
  pkt_header.timestamp_ns = timestamp_ns;

  std::vector<asio::const_buffer> buffers;
  buffers.push_back(asio::buffer(&pkt_header, sizeof(pkt_header)));
  buffers.push_back(asio::buffer(&audio_meta, sizeof(audio_meta)));
  buffers.push_back(asio::buffer(pcm_data, data_size));

  impl_->BroadcastBuffers(buffers);
  return true;
}

void VTPSender::SetScaleQuality(int quality) {
  impl_->scale_quality_ = quality;
}

uint16_t VTPSender::GetPort() const { return impl_->port_; }

std::string VTPSender::GetStreamName() const { return impl_->stream_name_; }

} // namespace vtp
