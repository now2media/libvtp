#ifndef LINUXMEDIALIBRARY_SIGNAL_H
#define LINUXMEDIALIBRARY_SIGNAL_H

#include "now2sdk_global.h"
#include "now2sdk.h"
#include "LFormat.h"
#include "LObject.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

class NOW2SDK_EXPORT LSignal : public LObject {
public:
    LSignal();
    virtual ~LSignal();

    void setColor(const std::string& hex);
    void setImage(const std::string& path);
    
    void setVideoFormat(const videoFormatProps& props);
    void getVideoFormat(videoFormatProps& props);

    bool Start();
    void Stop();

private:
    void signalLoop();
    struct AVFrame* createFrame();

    std::string m_color = "#000000";
    std::string m_imagePath = "";
    videoFormatProps m_targetProps;
    
    std::thread m_thread;
    std::atomic<bool> m_running;
    struct AVFrame* m_cachedFrame = nullptr;
    std::mutex m_mtx;
};

#endif // LINUXMEDIALIBRARY_SIGNAL_H
