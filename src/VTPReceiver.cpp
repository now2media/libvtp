#include "VTPReceiver.h"
#include <asio.hpp>
#include <turbojpeg.h>
#include <iostream>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <queue>

namespace vtp {

struct VTPReceiver::Impl {
    asio::io_context io_context_;
    asio::ip::tcp::socket socket_;
    std::atomic<bool> connected_{false};

    tjhandle tj_decompressor_ = nullptr;

    // Background thread details
    std::thread receive_thread_;

    // Structs for caching received frames in background thread
    struct VideoFrameCache {
        std::vector<unsigned char> data;
        uint32_t width = 0;
        uint32_t height = 0;
        VTPPixelFormat format = VTP_FORMAT_RGB;
        uint64_t timestamp_ns = 0;
        uint32_t frame_index = 0;
        bool has_alpha = false;
    };

    struct AudioFrameCache {
        std::vector<unsigned char> data;
        uint32_t sample_rate = 0;
        uint8_t channels = 0;
        uint32_t sample_count = 0;
        uint64_t timestamp_ns = 0;
    };

    // Video synchronization
    std::mutex video_mutex_;
    std::condition_variable video_cv_;
    VideoFrameCache latest_video_frame_;
    bool new_video_frame_available_ = false;
    std::vector<unsigned char> decompressed_frame_buffer_; // Stable buffer for client

    // Audio synchronization
    std::mutex audio_mutex_;
    std::condition_variable audio_cv_;
    std::queue<AudioFrameCache> audio_queue_;
    std::vector<unsigned char> decompressed_audio_buffer_; // Stable buffer for client

    Impl() : socket_(io_context_) {}

    void ReceiveLoop(VTPReceiver* parent);
};

VTPReceiver::VTPReceiver() : impl_(std::make_unique<Impl>()) {
    impl_->tj_decompressor_ = tjInitDecompress();
}

VTPReceiver::~VTPReceiver() {
    Disconnect();
    if (impl_->tj_decompressor_) {
        tjDestroy(impl_->tj_decompressor_);
    }
}

bool VTPReceiver::Connect(const std::string& ip, uint16_t port) {
    if (impl_->connected_) return true;

    std::string target_ip = ip;
    uint16_t target_port = port;

    // Parse IP:Port string if present, otherwise fallback to default port if port is 0
    size_t colon_pos = ip.find(':');
    if (colon_pos != std::string::npos) {
        target_ip = ip.substr(0, colon_pos);
        try {
            target_port = static_cast<uint16_t>(std::stoi(ip.substr(colon_pos + 1)));
        } catch (...) {
            target_port = VTP_DEFAULT_PORT;
        }
    } else if (target_port == 0) {
        target_port = VTP_DEFAULT_PORT;
    }

    try {
        asio::ip::tcp::endpoint endpoint(asio::ip::make_address(target_ip), target_port);
        asio::error_code ec;
        impl_->socket_.connect(endpoint, ec);

        if (ec) {
            std::cerr << "[VTPReceiver] Connection to " << target_ip << ":" << target_port << " failed: " << ec.message() << std::endl;
            return false;
        }

        // Disable Nagle's algorithm for low latency
        impl_->socket_.set_option(asio::ip::tcp::no_delay(true));
        impl_->connected_ = true;
        std::cout << "[VTPReceiver] Connected to VTP Sender at " << target_ip << ":" << target_port << std::endl;

        // Start background receive thread
        impl_->receive_thread_ = std::thread(&VTPReceiver::Impl::ReceiveLoop, impl_.get(), this);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[VTPReceiver] Connect exception: " << e.what() << std::endl;
        return false;
    }
}

void VTPReceiver::Disconnect() {
    if (!impl_->connected_) return;
    impl_->connected_ = false;

    asio::error_code ec;
    impl_->socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    impl_->socket_.close(ec);

    if (impl_->receive_thread_.joinable()) {
        impl_->receive_thread_.join();
    }

    // Clear and wake up any waiting threads
    {
        std::lock_guard<std::mutex> lock(impl_->video_mutex_);
        impl_->new_video_frame_available_ = false;
        impl_->video_cv_.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(impl_->audio_mutex_);
        while (!impl_->audio_queue_.empty()) {
            impl_->audio_queue_.pop();
        }
        impl_->audio_cv_.notify_all();
    }
}

void VTPReceiver::Impl::ReceiveLoop(VTPReceiver* parent) {
    // Thread-local decompression buffers
    std::vector<unsigned char> local_fill_jpeg_buf;
    std::vector<unsigned char> local_key_jpeg_buf;
    std::vector<unsigned char> local_alpha_buf;
    std::vector<unsigned char> local_decompressed_buf;

    while (connected_) {
        try {
            asio::error_code ec;
            VTPPacketHeader pkt_header;

            // 1. Read packet header
            asio::read(socket_, asio::buffer(&pkt_header, sizeof(pkt_header)), ec);
            if (ec) {
                if (connected_) {
                    std::cerr << "[VTPReceiver] Error reading packet header: " << ec.message() << std::endl;
                }
                break;
            }

            if (pkt_header.magic != VTP_MAGIC) {
                std::cerr << "[VTPReceiver] Invalid packet magic!" << std::endl;
                break;
            }

            if (pkt_header.type == VTP_PACKET_VIDEO) {
                // Read frame metadata header
                VTPFrameHeader frame_hdr;
                asio::read(socket_, asio::buffer(&frame_hdr, sizeof(frame_hdr)), ec);
                if (ec) break;

                if (frame_hdr.magic != VTP_MAGIC) {
                    std::cerr << "[VTPReceiver] Invalid frame header magic!" << std::endl;
                    break;
                }

                // Read fill JPEG
                local_fill_jpeg_buf.resize(frame_hdr.fill_size);
                asio::read(socket_, asio::buffer(local_fill_jpeg_buf.data(), frame_hdr.fill_size), ec);
                if (ec) break;

                // Read alpha JPEG if exists
                if (frame_hdr.has_alpha) {
                    local_key_jpeg_buf.resize(frame_hdr.key_size);
                    asio::read(socket_, asio::buffer(local_key_jpeg_buf.data(), frame_hdr.key_size), ec);
                    if (ec) break;
                }

                // Determine channels
                int bytes_per_pixel = 3;
                int tj_pf = TJPF_RGB;
                switch (frame_hdr.pixel_format) {
                    case VTP_FORMAT_RGB:  bytes_per_pixel = 3; tj_pf = TJPF_RGB;  break;
                    case VTP_FORMAT_RGBA: bytes_per_pixel = 4; tj_pf = TJPF_RGBA; break;
                    case VTP_FORMAT_BGR:  bytes_per_pixel = 3; tj_pf = TJPF_BGR;  break;
                    case VTP_FORMAT_BGRA: bytes_per_pixel = 4; tj_pf = TJPF_BGRA; break;
                }

                int req_size = frame_hdr.width * frame_hdr.height * bytes_per_pixel;
                local_decompressed_buf.resize(req_size);

                // Decompress video fill
                int decompress_res = tjDecompress2(
                    tj_decompressor_,
                    local_fill_jpeg_buf.data(),
                    frame_hdr.fill_size,
                    local_decompressed_buf.data(),
                    frame_hdr.width,
                    0, // pitch
                    frame_hdr.height,
                    tj_pf,
                    0 // flags
                );

                if (decompress_res == 0) {
                    // Decompress key (alpha) and merge if present
                    if (frame_hdr.has_alpha && (frame_hdr.pixel_format == VTP_FORMAT_RGBA || frame_hdr.pixel_format == VTP_FORMAT_BGRA)) {
                        size_t num_pixels = frame_hdr.width * frame_hdr.height;
                        local_alpha_buf.resize(num_pixels);

                        int key_decompress_res = tjDecompress2(
                            tj_decompressor_,
                            local_key_jpeg_buf.data(),
                            frame_hdr.key_size,
                            local_alpha_buf.data(),
                            frame_hdr.width,
                            0,
                            frame_hdr.height,
                            TJPF_GRAY,
                            0
                        );

                        if (key_decompress_res == 0) {
                            for (size_t i = 0; i < num_pixels; ++i) {
                                local_decompressed_buf[i * 4 + 3] = local_alpha_buf[i];
                            }
                        }
                    }

                    // Save latest frame
                    {
                        std::lock_guard<std::mutex> lock(video_mutex_);
                        latest_video_frame_.data = std::move(local_decompressed_buf);
                        latest_video_frame_.width = frame_hdr.width;
                        latest_video_frame_.height = frame_hdr.height;
                        latest_video_frame_.format = static_cast<VTPPixelFormat>(frame_hdr.pixel_format);
                        latest_video_frame_.timestamp_ns = frame_hdr.timestamp_ns;
                        latest_video_frame_.frame_index = frame_hdr.frame_index;
                        latest_video_frame_.has_alpha = frame_hdr.has_alpha != 0;
                        new_video_frame_available_ = true;
                    }
                    video_cv_.notify_all();
                } else {
                    std::cerr << "[VTPReceiver] Decompression failed: " << tjGetErrorStr2(tj_decompressor_) << std::endl;
                }

            } else if (pkt_header.type == VTP_PACKET_AUDIO) {
                // Read audio metadata
                VTPAudioMetadata audio_meta;
                asio::read(socket_, asio::buffer(&audio_meta, sizeof(audio_meta)), ec);
                if (ec) break;

                // Read PCM bytes
                std::vector<unsigned char> local_pcm_buf(audio_meta.data_size);
                asio::read(socket_, asio::buffer(local_pcm_buf.data(), audio_meta.data_size), ec);
                if (ec) break;

                // Push to audio queue
                {
                    std::lock_guard<std::mutex> lock(audio_mutex_);
                    AudioFrameCache frame;
                    frame.data = std::move(local_pcm_buf);
                    frame.sample_rate = audio_meta.sample_rate;
                    frame.channels = audio_meta.channels;
                    frame.sample_count = audio_meta.sample_count;
                    frame.timestamp_ns = pkt_header.timestamp_ns;

                    audio_queue_.push(std::move(frame));
                    if (audio_queue_.size() > 100) {
                        audio_queue_.pop(); // Pop old to prevent infinite growth
                    }
                }
                audio_cv_.notify_all();
            }

        } catch (const std::exception& e) {
            std::cerr << "[VTPReceiver] Loop exception: " << e.what() << std::endl;
            break;
        }
    }

    // Mark as disconnected if we exited loop
    parent->Disconnect();
}

bool VTPReceiver::ReceiveFrame(VTPFrame* out_frame) {
    if (!out_frame) return false;

    std::unique_lock<std::mutex> lock(impl_->video_mutex_);
    impl_->video_cv_.wait(lock, [this]() {
        return impl_->new_video_frame_available_ || !impl_->connected_;
    });

    if (!impl_->connected_ && !impl_->new_video_frame_available_) {
        return false;
    }

    // Copy to stable output buffer
    impl_->decompressed_frame_buffer_ = impl_->latest_video_frame_.data;

    out_frame->data = impl_->decompressed_frame_buffer_.data();
    out_frame->width = impl_->latest_video_frame_.width;
    out_frame->height = impl_->latest_video_frame_.height;
    out_frame->format = static_cast<vtp_pixel_format_t>(impl_->latest_video_frame_.format);
    out_frame->timestamp_ns = impl_->latest_video_frame_.timestamp_ns;
    out_frame->frame_index = impl_->latest_video_frame_.frame_index;
    out_frame->has_alpha = impl_->latest_video_frame_.has_alpha;

    impl_->new_video_frame_available_ = false;
    return true;
}

bool VTPReceiver::ReceiveAudio(VTPAudioFrame* out_audio) {
    if (!out_audio) return false;

    std::unique_lock<std::mutex> lock(impl_->audio_mutex_);
    impl_->audio_cv_.wait(lock, [this]() {
        return !impl_->audio_queue_.empty() || !impl_->connected_;
    });

    if (impl_->audio_queue_.empty()) {
        return false;
    }

    Impl::AudioFrameCache frame = std::move(impl_->audio_queue_.front());
    impl_->audio_queue_.pop();

    impl_->decompressed_audio_buffer_ = std::move(frame.data);

    out_audio->data = impl_->decompressed_audio_buffer_.data();
    out_audio->sample_rate = frame.sample_rate;
    out_audio->channels = frame.channels;
    out_audio->sample_count = frame.sample_count;
    out_audio->data_size = impl_->decompressed_audio_buffer_.size();
    out_audio->timestamp_ns = frame.timestamp_ns;

    return true;
}

bool VTPReceiver::IsConnected() const {
    return impl_->connected_;
}

} // namespace vtp
