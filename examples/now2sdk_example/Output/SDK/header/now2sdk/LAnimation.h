#pragma once

#include "LObject.h"
#include "now2sdk_global.h"
#include "LFormat.h"
#include <string>

class NOW2SDK_EXPORT LAnimation : public LObject {
public:
    LAnimation();
    virtual ~LAnimation();

    // Licensing
    bool setLicense(const std::string& licenseKey);
    bool isLicensed() const;

    // Çoklu Katman (Multi-layer) Lottie Desteği
    bool loadTemplate(int layerId, const std::string& jsonPath);
    void update(int layerId, const std::string& key, const std::string& value);
    void updateImage(int layerId, const std::string& assetId, const std::string& imagePath);
    void updateColor(int layerId, const std::string& layerName, const std::string& hexColor1, const std::string& hexColor2 = "", const std::string& propertyName = "Color");
    void addMarker(int layerId, const std::string& name, int frame);
    void stop(int layerId);
    void close(int layerId);
    void play(int layerId);

    // Geriye dönük uyumluluk için tek katmanlı fonksiyonlar (Layer 0 kullanır)
    bool loadTemplate(const std::string& jsonPath) { return loadTemplate(0, jsonPath); }
    void update(const std::string& key, const std::string& value) { update(0, key, value); }
    void updateImage(const std::string& assetId, const std::string& imagePath) { updateImage(0, assetId, imagePath); }
    void updateColor(const std::string& layerName, const std::string& hexColor1, const std::string& hexColor2 = "", const std::string& propertyName = "Color") { updateColor(0, layerName, hexColor1, hexColor2, propertyName); }
    void addMarker(const std::string& name, int frame) { addMarker(0, name, frame); }
    void stop() { stop(0); }
    void close() { close(0); }
    void play() { play(0); }
    
    // Video Format Ayarı
    void setVideoFormat(const videoFormatProps& props);
    void getVideoFormat(videoFormatProps& props);

    // Durum Bilgisi (LObject'ten override)
    virtual double getFPS() override;
    virtual int getWidth() override;
    virtual int getHeight() override;

private:
    class Impl;
    Impl* pimpl;
    
    void animationLoop();
};
