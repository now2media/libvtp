#include "VTPDiscovery.h"
#include <algorithm>
#include <asio.hpp>
#include <cstring>
#include <iostream>

namespace vtp {

constexpr const char *MULTICAST_ADDRESS = "239.255.42.99";
constexpr uint16_t MULTICAST_PORT = 9999;

// VTPDiscoveryAdvertiser

VTPDiscoveryAdvertiser::VTPDiscoveryAdvertiser(const std::string &name,
                                               uint16_t port, int width,
                                               int height, int fps,
                                               bool enable_alpha,
                                               bool enable_audio)
    : name_(name), port_(port), width_(width), height_(height), fps_(fps),
      enable_alpha_(enable_alpha), enable_audio_(enable_audio) {}

VTPDiscoveryAdvertiser::~VTPDiscoveryAdvertiser() { Stop(); }

void VTPDiscoveryAdvertiser::Start() {
  if (running_)
    return;
  running_ = true;
  thread_ = std::thread(&VTPDiscoveryAdvertiser::Run, this);
}

void VTPDiscoveryAdvertiser::Stop() {
  if (!running_)
    return;
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

void VTPDiscoveryAdvertiser::Run() {
  try {
    asio::io_context io_context;
    asio::ip::udp::socket socket(
        io_context, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
    socket.set_option(asio::socket_base::broadcast(true));

    asio::ip::udp::endpoint multicast_endpoint(
        asio::ip::make_address(MULTICAST_ADDRESS), MULTICAST_PORT);

    VTPSource adv_info;
    std::memset(&adv_info, 0, sizeof(adv_info));
    std::strncpy(adv_info.name, name_.c_str(), sizeof(adv_info.name) - 1);
    adv_info.port = port_;
    adv_info.width = width_;
    adv_info.height = height_;
    adv_info.fps = fps_;
    adv_info.has_alpha = enable_alpha_;
    adv_info.has_audio = enable_audio_;

    while (running_) {
      asio::error_code ec;
      socket.send_to(asio::buffer(&adv_info, sizeof(adv_info)),
                     multicast_endpoint, 0, ec);
      if (ec) {
        std::cerr << "[VTP Discovery] Send failed: " << ec.message()
                  << std::endl;
      }

      for (int i = 0; i < 15 && running_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[VTP Discovery] Advertiser exception: " << e.what()
              << std::endl;
  }
}

// VTPDiscoveryListener

VTPDiscoveryListener::VTPDiscoveryListener() {}

VTPDiscoveryListener::~VTPDiscoveryListener() { Stop(); }

void VTPDiscoveryListener::Start() {
  if (running_)
    return;
  running_ = true;
  thread_ = std::thread(&VTPDiscoveryListener::Run, this);
}

void VTPDiscoveryListener::Stop() {
  if (!running_)
    return;
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

int VTPDiscoveryListener::GetSources(VTPSource *out_sources, int max_sources) {
  CleanStaleSources();
  std::lock_guard<std::mutex> lock(mutex_);
  int count = 0;
  for (const auto &src : sources_) {
    if (count >= max_sources)
      break;
    out_sources[count++] = src.source;
  }
  return count;
}

void VTPDiscoveryListener::CleanStaleSources() {
  std::lock_guard<std::mutex> lock(mutex_);
  auto now = std::chrono::steady_clock::now();
  sources_.erase(
      std::remove_if(sources_.begin(), sources_.end(),
                     [now](const DiscoveredSource &src) {
                       return std::chrono::duration_cast<std::chrono::seconds>(
                                  now - src.last_seen)
                                  .count() >= 3.5;
                     }),
      sources_.end());
}

void VTPDiscoveryListener::Run() {
  try {
    asio::io_context io_context;
    asio::ip::udp::socket socket(io_context);
    asio::ip::udp::endpoint listen_endpoint(asio::ip::address_v4::any(),
                                            MULTICAST_PORT);

    socket.open(listen_endpoint.protocol());
    socket.set_option(asio::socket_base::reuse_address(true));
    socket.bind(listen_endpoint);

    socket.set_option(asio::ip::multicast::join_group(
        asio::ip::make_address(MULTICAST_ADDRESS)));

    socket.non_blocking(true);

    VTPSource recv_buffer;

    while (running_) {
      asio::ip::udp::endpoint sender_endpoint;
      asio::error_code ec;

      size_t bytes_recvd =
          socket.receive_from(asio::buffer(&recv_buffer, sizeof(recv_buffer)),
                              sender_endpoint, 0, ec);

      if (!ec && bytes_recvd == sizeof(VTPSource)) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string sender_ip = sender_endpoint.address().to_string();
        std::strncpy(recv_buffer.ip, sender_ip.c_str(),
                     sizeof(recv_buffer.ip) - 1);

        bool found = false;
        auto now = std::chrono::steady_clock::now();
        for (auto &src : sources_) {
          if (std::strcmp(src.source.name, recv_buffer.name) == 0 &&
              std::strcmp(src.source.ip, recv_buffer.ip) == 0 &&
              src.source.port == recv_buffer.port) {
            src.last_seen = now;
            found = true;
            break;
          }
        }

        if (!found) {
          DiscoveredSource new_src;
          new_src.source = recv_buffer;
          new_src.last_seen = now;
          sources_.push_back(new_src);
        }
      } else if (ec == asio::error::would_block ||
                 ec == asio::error::try_again) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      } else if (ec) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "[VTP Discovery] Listener exception: " << e.what()
              << std::endl;
  }
}

} // namespace vtp
