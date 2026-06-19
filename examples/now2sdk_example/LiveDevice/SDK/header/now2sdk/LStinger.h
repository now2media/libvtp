#include "LObject.h"
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

class LStinger : public LObject {
public:
    LStinger();
    ~LStinger();

    // Licensing
    bool setLicense(const std::string& licenseKey);
    bool isLicensed() const;

    bool load(const std::string& path, int targetWidth, int targetHeight, double fps = 25.0);
    void clear();
    
    // Obje olarak başlat/durdur
    void playOnce();
    void stop();

    bool isLoaded() const { return m_loaded; }
    int getFrameCount() const { return (int)m_frames.size(); }
    void setFPS(double fps) { m_fps = fps; }
    void statusGet(int& status) override { status = m_playing ? 1 : 0; }
    double getFPS() override { return m_fps; }

private:
    void playbackLoop();

    std::vector<AVFrame*> m_frames;
    bool m_loaded = false;
    std::atomic<bool> m_playing{false};
    std::thread m_playThread;
    double m_fps = 25.0;
    std::mutex m_mutex;
};
