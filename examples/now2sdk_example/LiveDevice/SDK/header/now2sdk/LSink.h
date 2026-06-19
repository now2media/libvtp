#ifndef LSINK_H
#define LSINK_H

struct AVFrame;

// Herhangi bir kaynaktan (LFile, LLive, LMixer) frame alabilen nesneler için arayüz
class LSink {
public:
    virtual ~LSink() = default;
    virtual void pushVideoFrame(AVFrame* frame) = 0;
    virtual void pushAudioFrame(AVFrame* frame) = 0;
    
    // Alıcıların kendi iç kuyruklarını ve hafızalarını temizlemesi için standart komut
    virtual void flush() {} 
    
    // Alıcıları geçici olarak durdurmak veya devam ettirmek için (Buffer silinmez)
    virtual void setPaused(bool paused) {}
};

#endif // LSINK_H
