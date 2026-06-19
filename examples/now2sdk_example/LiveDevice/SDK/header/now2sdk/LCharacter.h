#pragma once

#include "now2sdk_global.h"
#include "LObject.h"
#include <vector>
#include <string>
#include <mutex>
#include <map>
#include <thread>

struct _cairo;
struct _cairo_surface;
typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;
struct SwsContext;

// --- 0. Akıllı Renk Yapısı ---
struct NOW2SDK_EXPORT LCGColor {
    uint32_t val = 0xFFFFFFFF;
    LCGColor(uint32_t v = 0xFFFFFFFF) : val(v) {}
    LCGColor(const char* hex) { assign(hex); }
    LCGColor(const std::string& hex) { assign(hex); }
    LCGColor& operator=(const std::string& hex) { assign(hex); return *this; }
    LCGColor& operator=(const char* hex) { assign(hex); return *this; }
    operator uint32_t() const { return val; }
private:
    void assign(std::string hex) {
        if (hex.empty()) { val = 0; return; }
        if (hex[0] == '#') hex = hex.substr(1);
        if (hex.length() == 6) hex = "FF" + hex;
        try { val = (uint32_t)std::stoul(hex, nullptr, 16); } catch (...) { val = 0xFFFFFFFF; }
    }
};

// --- İç Enumar ---
enum class LCGHAlign     { Left, Center, Right };
enum class LCGVAlign     { Top, Center, Bottom };
enum class LCGMove       { None, Horizontal, Vertical };
enum class LCGBackground { None, Color, Gradient };
enum class LCGCrawlMove { Horizontal, Vertical };

// --- 1. PROPS YAPILARI ---

struct NOW2SDK_EXPORT LCGTextProps {
    std::string text = "";
    std::string font = "Arial";
    int fontSize = 40;
    LCGColor color = "#FFFFFF";
    int textAlpha = 255;
    float letterSpacing = 0.0f;
    bool  autoShrink = false;
    int   padding = 0;
    int   outlineSize = 0;
    LCGColor outlineColor = "#000000";
    std::string hAlign = "Left";
    std::string vAlign = "Center";
    std::string bgType = "None";
    LCGColor bgColor = "#000000";
    LCGColor bgGradientStartColor = "#FF0000";
    LCGColor bgGradientEndColor = "#000000";
    int bgAlpha = 255;
    int bgRadius = 0;
    float bgShadowOffsetX = 0.0f;
    float bgShadowOffsetY = 0.0f;
    int   bgShadowAlpha = 0;
    LCGColor bgShadowColor = "#000000";
    
    LCGColor borderColor = "#FFFFFF";
    int borderSize = 0;
    int borderAlpha = 255;
    float textShadowOffsetX = 0.0f;
    float textShadowOffsetY = 0.0f;
    int   textShadowAlpha = 0;
    LCGColor textShadowColor = "#000000";
};

struct NOW2SDK_EXPORT LCGCrawlProps {
    std::string text = "";
    std::string font = "Arial";
    int fontSize = 40;
    LCGColor color = "#FFFFFF";
    int textAlpha = 255;
    float letterSpacing = 0.0f;
    bool  autoShrink = false;
    int   padding = 0;
    int   outlineSize = 0;
    LCGColor outlineColor = "#000000";
    std::string hAlign = "Left";
    std::string vAlign = "Center";
    std::string bgType = "None";
    LCGColor bgColor = "#000000";
    LCGColor bgGradientStartColor = "#FF0000";
    LCGColor bgGradientEndColor = "#000000";
    int bgAlpha = 255;
    int bgRadius = 0;
    float bgShadowOffsetX = 0.0f;
    float bgShadowOffsetY = 0.0f;
    int   bgShadowAlpha = 0;
    LCGColor bgShadowColor = "#000000";
    float textShadowOffsetX = 0.0f;
    float textShadowOffsetY = 0.0f;
    int   textShadowAlpha = 0;
    LCGColor textShadowColor = "#000000";
    std::string textMove = "Horizontal";
    float textSpeed = -5.0f;
    std::string separatorImage = "";   
    std::string textSeparator = " - "; 
    int separatorPadding = 20;
    
    LCGColor borderColor = "#FFFFFF";
    int borderSize = 0;
    int borderAlpha = 255;
};
struct NOW2SDK_EXPORT LCGTickerProps {
    std::string text = "";
    std::string font = "Arial";
    int fontSize = 40;
    LCGColor color = "#FFFFFF";
    int textAlpha = 255;
    float letterSpacing = 0.0f;
    bool  autoShrink = false;
    int   padding = 0;
    int   outlineSize = 0;
    LCGColor outlineColor = "#000000";
    std::string hAlign = "Left";
    std::string vAlign = "Center";
    std::string bgType = "None";
    LCGColor bgColor = "#000000";
    LCGColor bgGradientStartColor = "#FF0000";
    LCGColor bgGradientEndColor = "#000000";
    int bgAlpha = 255;
    int bgRadius = 0;
    float bgShadowOffsetX = 0.0f;
    float bgShadowOffsetY = 0.0f;
    int   bgShadowAlpha = 0;
    LCGColor bgShadowColor = "#000000";
    float textShadowOffsetX = 0.0f;
    float textShadowOffsetY = 0.0f;
    int   textShadowAlpha = 0;
    LCGColor textShadowColor = "#000000";
    
    std::string textMove = "Up"; // "Up" veya "Down"
    float textMoveDuration = 5.0f; // Saniye cinsinden bekleme süresi
    
    LCGColor borderColor = "#FFFFFF";
    int borderSize = 0;
    int borderAlpha = 255;
};

struct NOW2SDK_EXPORT LCGImageProps {
    std::string path = "";
};
struct NOW2SDK_EXPORT LCGVideoProps {
    std::string path = "";
};

struct NOW2SDK_EXPORT LCGRectProps {
    std::string bgType = "Solid"; // Varsayılan olarak Solid olsun
    LCGColor bgColor = "#FFFFFF";
    LCGColor bgGradientStartColor = "#FFFFFF";
    LCGColor bgGradientEndColor = "#000000";
    int bgAlpha = 255;
    int bgRadius = 0;

    LCGColor borderColor = "#FFFFFF";
    int borderSize = 0;
    int borderAlpha = 255;

    // Gölge
    float bgShadowOffsetX = 0.0f;
    float bgShadowOffsetY = 0.0f;
    int   bgShadowAlpha = 0;
    LCGColor bgShadowColor = "#000000";
};

// --- 2. ITEM SINIFLARI ---

class LCGItem;
class LCharacter;

class NOW2SDK_EXPORT LCGGroup {
public:
    std::string id;
    bool show = true;
    float x = 0, y = 0;
    float w = 0, h = 0;
    std::vector<LCGItem*> items;
    
    std::string getID() const { return id; }
    void setID(const std::string& newId) { id = newId; }
};

class NOW2SDK_EXPORT LCGItem {
public:
    std::string id;
    std::string getID() const { return id; }
    float x = 0, y = 0;
    float startX = 0, startY = 0;
    int w = 0, h = 0;
    bool show = true;
    float alpha = 1.0f; 
    LCGGroup* pGroup = nullptr; // Artik string degil, doğrudan gruba bağlı
    std::string move = "None";
    float speed = 0.0f;
    bool loop = true;
    
    virtual ~LCGItem() {}
};

class NOW2SDK_EXPORT LCGTextItem : public LCGItem {
public:
    LCGTextProps props;
};

class NOW2SDK_EXPORT LCGCrawlItem : public LCGItem { 
public:
    LCGCrawlProps crawlProps;
private:
    friend class LCharacter;
    float currentTextX = 0;
    float currentTextY = 0;
    
    // Cache for pre-rendered ticker text
    cairo_surface_t* cachedTextSurface = nullptr;
    double cachedTextWidth = 0;
    double cachedTextHeight = 0;
    std::string cachedRawText = "";
    
public:
    ~LCGCrawlItem() override;
};

class NOW2SDK_EXPORT LCGTickerItem : public LCGItem { 
public:
    LCGTickerProps tickerProps;
private:
    friend class LCharacter;
    // Animasyon durum takibi için
    int currentItemIndex = 0;
    std::chrono::steady_clock::time_point lastChangeTime;
    float transitionProgress = 0.0f; 
    bool inTransition = false;
};

class NOW2SDK_EXPORT LCGImageItem : public LCGItem {
public:
    LCGImageProps imageProps;
private:
    friend class LCharacter;
    struct AVFrame* imageFrame = nullptr;
};

class NOW2SDK_EXPORT LCGVideoItem : public LCGItem {
public:
    LCGVideoProps videoProps;
private:
    friend class LCharacter;
    // FFmpeg yapıları (Açık kalması için)
    struct AVFormatContext* pFormatCtx = nullptr;
    struct AVCodecContext* pCodecCtx = nullptr;
    int videoStreamIndex = -1;
    struct AVFrame* pFrame = nullptr;
    struct SwsContext* swsCtx = nullptr;
    struct AVFrame* pFrameBGRA = nullptr;
    bool isOpened = false;
public:
    virtual ~LCGVideoItem();
};

class NOW2SDK_EXPORT LCGRectItem : public LCGItem {
public:
    LCGRectProps rectProps;
};

// --- 3. ENGINE ---

class NOW2SDK_EXPORT LCharacter : public LObject {
public:
    LCharacter();
    virtual ~LCharacter();
    LCGTextItem*   addItem(std::string id, int x, int y, int w, int h, LCGTextProps props);
    LCGCrawlItem* addItem(std::string id, int x, int y, int w, int h, LCGCrawlProps props);
    LCGTickerItem* addItem(std::string id, int x, int y, int w, int h, LCGTickerProps props);
    LCGImageItem*  addItem(std::string id, int x, int y, int w, int h, LCGImageProps props);
    LCGVideoItem*  addItem(std::string id, int x, int y, int w, int h, LCGVideoProps props);
    LCGRectItem*   addItem(std::string id, int x, int y, int w, int h, LCGRectProps props);
    
    void updateItem(std::string id, int x, int y, int w, int h, float alpha, bool show, LCGTextProps props);
    void updateItem(std::string id, int x, int y, int w, int h, float alpha, bool show, LCGCrawlProps props);
    void updateItem(std::string id, int x, int y, int w, int h, float alpha, bool show, LCGTickerProps props);
    void updateItem(std::string id, int x, int y, int w, int h, float alpha, bool show, LCGImageProps props);
    void updateItem(std::string id, int x, int y, int w, int h, float alpha, bool show, LCGVideoProps props);
    void updateItem(std::string id, int x, int y, int w, int h, float alpha, bool show, LCGRectProps props);
    
    LCGItem* getItem(std::string id);
    int getItemCount() const;
    LCGItem* getItemByIndex(int index);
    void showItem(std::string id_or_group, bool show);
    void remove(const std::string& id);
    virtual void setFPS(double fps);
    void forceUpdate();
    
    // --- XML KAYIT VE YÜKLEME ---
    void saveToXMLFile(const std::string& filePath);
    void loadFromXMLFile(const std::string& filePath);
    void insertFromXMLFile(const std::string& filePath);
    
    // --- GRUP YÖNETİMİ (Medialooks Tarzı) ---
    std::string group(const std::vector<std::string>& itemIDs);
    void unGroup(const std::string& groupID);
    void deleteGroup(const std::string& groupID);
    int groupItemCount(const std::string& groupID);
    LCGItem* getGroupItemByIndex(const std::string& groupID, int index);
    std::string getGroup(const std::string& itemID);
    LCGGroup* getGroupObject(const std::string& groupID);
    int getGroupCount() const;
    LCGGroup* getGroupByIndex(int index);
private:
    static uint32_t hexToUint(std::string hex);
    bool isDynamic();
    void playbackLoop();
    void renderComposition();
    std::string processDynamicTags(const std::string& itemId, const std::string& text);
    std::atomic<bool> m_isDirty;
    std::vector<LCGItem*> m_items;
    std::map<std::string, LCGItem*> m_itemMap;
    std::map<std::string, LCGGroup*> m_groupMap;
    int m_groupCounter = 1; // Otomatik grup ID'leri için sayaç
    mutable std::recursive_mutex m_cgMutex;
    std::map<std::string, std::chrono::steady_clock::time_point> m_timerStarts;
    std::map<std::string, long long> m_timerDurationsMs;
    std::thread m_thread;
    bool m_running = false;
    double m_fps = 50.0;
    int m_width = 1920;
    int m_height = 1080;
    struct AVFrame* m_compFrame = nullptr;
    cairo_surface_t* m_cairoSurface = nullptr;
    cairo_t* m_cairoContext = nullptr;
    struct SwsContext* m_cairoSwsContext = nullptr;
};
