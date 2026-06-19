#ifndef LPREVIEW_H
#define LPREVIEW_H

#include <string>
#include <memory>
#include "LObject.h"

/**
 * @brief LPreview: Framework-agnostic video preview controller.
 * Supports both Qt and GTK at runtime.
 */
class LPreview {
public:
    LPreview();
    ~LPreview();

    /**
     * @brief Enable preview on a native window handle or widget.
     * @param parent For Qt: QWidget*, For GTK: GtkWidget*
     */
    void previewEnable(void* parent, bool audio, bool video);
    
    void setProps(const std::string& key, const std::string& value);
    void statusGet(int& status);
    void previewObject(LObject* source);
    void setAudio(bool enable);
    void mute(bool isMute);
    
    void setName(const std::string& name);
    void setStatus(const std::string& status);
    void clear();

public:
    class Impl;
private:
    std::unique_ptr<Impl> pimpl;
};

#endif // LPREVIEW_H
