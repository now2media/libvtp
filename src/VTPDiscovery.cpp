#include "VTPDiscovery.h"
#include "VTPLog.h"
#include <algorithm>
#include <asio.hpp>
#include <cstring>

#ifndef _WIN32
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

namespace vtp {

constexpr const char *MULTICAST_ADDRESS = "239.255.42.99";
constexpr uint16_t MULTICAST_PORT = 9999;

static std::vector<asio::ip::address_v4> EnumerateLocalIPv4Interfaces() {
  std::vector<asio::ip::address_v4> addrs;

#ifndef _WIN32
  struct ifaddrs *ifaddr = nullptr;
  if (getifaddrs(&ifaddr) == 0) {
    for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
        continue;
      if (!(ifa->ifa_flags & IFF_UP))
        continue;
      if (ifa->ifa_flags & IFF_LOOPBACK)
        continue;

      const auto *sin =
          reinterpret_cast<const struct sockaddr_in *>(ifa->ifa_addr);
      uint32_t host_addr = ntohl(sin->sin_addr.s_addr);
      asio::ip::address_v4 v4(host_addr);
      if (std::find(addrs.begin(), addrs.end(), v4) == addrs.end()) {
        addrs.push_back(v4);
      }
    }
    freeifaddrs(ifaddr);
  }
#else
  try {
    asio::io_context tmp_io;
    asio::ip::udp::resolver resolver(tmp_io);
    asio::error_code ec;
    auto results = resolver.resolve(asio::ip::udp::v4(),
                                    asio::ip::host_name(ec), "", ec);
    if (!ec) {
      for (const auto &entry : results) {
        auto addr = entry.endpoint().address();
        if (addr.is_v4() && !addr.is_loopback()) {
          auto v4 = addr.to_v4();
          if (std::find(addrs.begin(), addrs.end(), v4) == addrs.end()) {
            addrs.push_back(v4);
          }
        }
      }
    }
  } catch (...) {
  }
#endif

  return addrs;
}

// VTPDiscoveryAdvertiser

VTPDiscoveryAdvertiser::VTPDiscoveryAdvertiser(
    const std::string &name, uint16_t port, int width, int height, int fps,
    bool enable_alpha, bool enable_audio, bool is_udp, bool has_proxy)
    : name_(name), port_(port), width_(width), height_(height), fps_(fps),
      enable_alpha_(enable_alpha), enable_audio_(enable_audio), is_udp_(is_udp),
      has_proxy_(has_proxy) {}

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

void VTPDiscoveryAdvertiser::UpdateStreamInfo(int width, int height, int fps) {
  std::lock_guard<std::mutex> lock(info_mutex_);
  width_ = width;
  height_ = height;
  fps_ = fps;
}

void VTPDiscoveryAdvertiser::Run() {
  try {
    asio::io_context io_context;
    asio::ip::udp::socket socket(
        io_context, asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
    asio::error_code opt_ec;
    socket.set_option(asio::socket_base::broadcast(true), opt_ec);
    socket.set_option(asio::ip::multicast::hops(4), opt_ec);

    asio::ip::udp::endpoint multicast_endpoint(
        asio::ip::make_address(MULTICAST_ADDRESS), MULTICAST_PORT);

    uint8_t flags = 0;
    if (enable_alpha_)
      flags |= VTP_DISC_FLAG_ALPHA;
    if (enable_audio_)
      flags |= VTP_DISC_FLAG_AUDIO;
    if (is_udp_)
      flags |= VTP_DISC_FLAG_UDP;
    if (has_proxy_)
      flags |= VTP_DISC_FLAG_PROXY;

    std::vector<asio::ip::address_v4> ifaces;
    int iface_refresh_counter = 0;

    while (running_) {
      if (iface_refresh_counter % 10 == 0) {
        ifaces = EnumerateLocalIPv4Interfaces();
      }
      ++iface_refresh_counter;

      VTPDiscoveryPacket pkt;
      std::memset(&pkt, 0, sizeof(pkt));
      pkt.magic = VTP_DISCOVERY_MAGIC;
      pkt.version = VTP_PROTOCOL_VERSION;
      pkt.flags = flags;
      pkt.port = port_;
      {
        std::lock_guard<std::mutex> lock(info_mutex_);
        pkt.width = static_cast<uint32_t>(width_);
        pkt.height = static_cast<uint32_t>(height_);
        pkt.fps = static_cast<uint32_t>(fps_);
      }
      std::strncpy(pkt.name, name_.c_str(), sizeof(pkt.name) - 1);

      bool sent_any = false;
      for (const auto &iface : ifaces) {
        asio::error_code ec;
        socket.set_option(asio::ip::multicast::outbound_interface(iface), ec);
        if (!ec) {
          socket.send_to(asio::buffer(&pkt, sizeof(pkt)), multicast_endpoint, 0,
                         ec);
          if (!ec) {
            sent_any = true;
          }
        }
        if (ec) {
          VTP_LOG_DEBUG("Discovery send failed on %s: %s",
                        iface.to_string().c_str(), ec.message().c_str());
        }
      }

      if (!sent_any) {
        asio::error_code ec;
        socket.set_option(
            asio::ip::multicast::outbound_interface(asio::ip::address_v4::any()),
            ec);
        socket.send_to(asio::buffer(&pkt, sizeof(pkt)), multicast_endpoint, 0,
                       ec);
        if (ec) {
          VTP_LOG_DEBUG("Discovery send failed (default): %s",
                        ec.message().c_str());
        }
      }

      for (int i = 0; i < 15 && running_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  } catch (const std::exception &e) {
    VTP_LOG_ERROR("Discovery advertiser exception: %s", e.what());
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
  std::lock_guard<std::mutex> lock(mutex_);
  CleanStaleSourcesLocked();
  if (!out_sources || max_sources <= 0)
    return 0;

  int count = 0;
  for (const auto &src : sources_) {
    if (count >= max_sources)
      break;
    out_sources[count++] = src.source;
  }
  return count;
}

bool VTPDiscoveryListener::FindSource(const char *name, VTPSource *out_source,
                                      uint8_t *out_flags) {
  if (!name)
    return false;
  std::lock_guard<std::mutex> lock(mutex_);
  CleanStaleSourcesLocked();
  for (const auto &src : sources_) {
    if (std::strcmp(src.source.name, name) == 0) {
      if (out_source)
        *out_source = src.source;
      if (out_flags)
        *out_flags = src.flags;
      return true;
    }
  }
  return false;
}

void VTPDiscoveryListener::CleanStaleSourcesLocked() {
  auto now = std::chrono::steady_clock::now();
  sources_.erase(
      std::remove_if(sources_.begin(), sources_.end(),
                     [now](const DiscoveredSource &src) {
                       return std::chrono::duration_cast<
                                  std::chrono::milliseconds>(now -
                                                             src.last_seen)
                                  .count() >= 3500;
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

    const auto mcast_addr = asio::ip::make_address_v4(MULTICAST_ADDRESS);
    auto ifaces = EnumerateLocalIPv4Interfaces();
    bool joined_any = false;

    for (const auto &iface : ifaces) {
      asio::error_code ec;
      socket.set_option(asio::ip::multicast::join_group(mcast_addr, iface), ec);
      if (!ec) {
        joined_any = true;
      } else {
        VTP_LOG_DEBUG("Multicast join on %s failed: %s",
                      iface.to_string().c_str(), ec.message().c_str());
      }
    }

    if (!joined_any) {
      asio::error_code ec;
      socket.set_option(asio::ip::multicast::join_group(mcast_addr), ec);
      if (ec) {
        VTP_LOG_WARN("Default multicast join failed: %s",
                     ec.message().c_str());
      }
    }

    socket.non_blocking(true);

    uint8_t recv_buf[512];

    while (running_) {
      asio::ip::udp::endpoint sender_endpoint;
      asio::error_code ec;

      size_t bytes_recvd = socket.receive_from(
          asio::buffer(recv_buf, sizeof(recv_buf)), sender_endpoint, 0, ec);

      if (!ec && (bytes_recvd == sizeof(VTPDiscoveryPacket) ||
                  bytes_recvd == sizeof(VTPSource))) {
        VTPSource parsed_src;
        std::memset(&parsed_src, 0, sizeof(parsed_src));
        uint8_t pkt_flags = 0;
        bool valid = false;

        if (bytes_recvd == sizeof(VTPDiscoveryPacket)) {
          VTPDiscoveryPacket pkt;
          std::memcpy(&pkt, recv_buf, sizeof(pkt));
          if (pkt.magic == VTP_DISCOVERY_MAGIC) {
            pkt.name[sizeof(pkt.name) - 1] = '\0';
            std::strncpy(parsed_src.name, pkt.name,
                         sizeof(parsed_src.name) - 1);
            parsed_src.port = pkt.port;
            parsed_src.width = pkt.width;
            parsed_src.height = pkt.height;
            parsed_src.fps = pkt.fps;
            parsed_src.has_alpha = (pkt.flags & VTP_DISC_FLAG_ALPHA) != 0;
            parsed_src.has_audio = (pkt.flags & VTP_DISC_FLAG_AUDIO) != 0;
            pkt_flags = pkt.flags;
            valid = true;
          }
        } else if (bytes_recvd == sizeof(VTPSource)) {
          std::memcpy(&parsed_src, recv_buf, sizeof(VTPSource));
          parsed_src.name[sizeof(parsed_src.name) - 1] = '\0';
          valid = true;
        }

        if (valid) {
          std::string sender_ip = sender_endpoint.address().to_string();
          std::strncpy(parsed_src.ip, sender_ip.c_str(),
                       sizeof(parsed_src.ip) - 1);
          parsed_src.ip[sizeof(parsed_src.ip) - 1] = '\0';

          std::lock_guard<std::mutex> lock(mutex_);
          bool found = false;
          auto now = std::chrono::steady_clock::now();
          for (auto &src : sources_) {
            if (std::strcmp(src.source.name, parsed_src.name) == 0 &&
                std::strcmp(src.source.ip, parsed_src.ip) == 0 &&
                src.source.port == parsed_src.port) {
              src.source = parsed_src;
              src.flags = pkt_flags;
              src.last_seen = now;
              found = true;
              break;
            }
          }

          if (!found && sources_.size() < VTP_MAX_SOURCES) {
            DiscoveredSource new_src;
            new_src.source = parsed_src;
            new_src.flags = pkt_flags;
            new_src.last_seen = now;
            sources_.push_back(new_src);
          }
        }
      } else if (ec == asio::error::would_block ||
                 ec == asio::error::try_again) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      } else if (ec) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  } catch (const std::exception &e) {
    VTP_LOG_ERROR("Discovery listener exception: %s", e.what());
  }
}

} // namespace vtp
