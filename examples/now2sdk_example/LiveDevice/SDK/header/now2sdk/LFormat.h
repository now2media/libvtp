#pragma once

// --- Video Format Değerleri (Global Enum) ---
#define VF_VALUES \
    X(Custom) \
    X(Disabled) \
    X(_525i_5994) \
    X(_625i_50) \
    X(HD720_50p) \
    X(HD720_5994p) \
    X(HD720_60p) \
    X(HD1080_2398p) \
    X(HD1080_24p) \
    X(HD1080_25p) \
    X(HD1080_2997p) \
    X(HD1080_30p) \
    X(HD1080_50i) \
    X(HD1080_50p) \
    X(HD1080_5994i) \
    X(HD1080_5994p) \
    X(HD1080_60i) \
    X(HD1080_60p) \
    X(_2K_DCI_2398p) \
    X(_2K_DCI_24p) \
    X(_2K_DCI_25p) \
    X(_2K_DCI_50p) \
    X(_2K_DCI_60p) \
    X(_4K_UHD_2398p) \
    X(_4K_UHD_24p) \
    X(_4K_UHD_25p) \
    X(_4K_UHD_2997p) \
    X(_4K_UHD_30p) \
    X(_4K_UHD_50i) \
    X(_4K_UHD_50p) \
    X(_4K_UHD_5994i) \
    X(_4K_UHD_5994p) \
    X(_4K_UHD_60i) \
    X(_4K_UHD_60p) \
    X(_4K_DCI_2398p) \
    X(_4K_DCI_24p) \
    X(_4K_DCI_25p) \
    X(_4K_DCI_50p) \
    X(_4K_DCI_60p)

typedef enum {
#define X(name) vF_##name,
    VF_VALUES
#undef X
} vF_Type;

namespace vF {
#define X(name) static const vF_Type name = vF_##name;
    VF_VALUES
#undef X
}

// --- ColorSpace Değerleri (Global Enum) ---
#define CS_VALUES \
    X(fcc_Default) \
    X(fcc_I420) \
    X(fcc_IYUV) \
    X(fcc_NV12) \
    X(fcc_r210) \
    X(fcc_RGB24) \
    X(fcc_RGB32) \
    X(fcc_ARGB32) \
    X(fcc_RGB8) \
    X(fcc_UYVY) \
    X(fcc_HDYC) \
    X(fcc_v210) \
    X(fcc_YUY2) \
    X(fcc_YV12) \
    X(fcc_YVYU)

typedef enum {
#define X(name) name,
    CS_VALUES
#undef X
} cS_Type;

namespace cS {
#define X(name) static const cS_Type name = ::name;
    CS_VALUES
#undef X
}

// --- Audio Format Değerleri (Global Enum) ---
#define AF_VALUES \
    X(Custom) \
    X(Disabled) \
    X(_16B_11K_2CH) \
    X(_16B_22K_2CH) \
    X(_16B_44K_2CH) X(_16B_44K_6CH) \
    X(_16B_48K_2CH) X(_16B_48K_6CH) X(_16B_48K_8CH) X(_16B_48K_16CH) \
    X(_16B_96K_2CH) X(_16B_96K_6CH) X(_16B_96K_8CH) \
    X(_16B_192K_2CH) X(_16B_192K_6CH) X(_16B_192K_8CH) \
    X(_24B_44K_2CH) \
    X(_24B_48K_2CH) X(_24B_48K_6CH) X(_24B_48K_8CH) X(_24B_48K_16CH) \
    X(_24B_96K_2CH) X(_24B_96K_6CH) X(_24B_96K_8CH) \
    X(_24B_192K_2CH) X(_24B_192K_6CH) X(_24B_192K_8CH) \
    X(_32F_44K_2CH) \
    X(_32F_48K_2CH) X(_32F_48K_6CH) X(_32F_48K_8CH) X(_32F_48K_16CH) \
    X(_32F_96K_2CH) X(_32F_96K_6CH) X(_32F_96K_8CH) \
    X(_32F_192K_2CH) X(_32F_192K_6CH) X(_32F_192K_8CH)

typedef enum {
#define X(name) aF_##name,
    AF_VALUES
#undef X
} aF_Type;

namespace aF {
#define X(name) static const aF_Type name = aF_##name;
    AF_VALUES
#undef X
}

// --- Yardımcı Yapılar ---
struct videoProps {
    int width;
    int height;
    double fps;
    bool interlaced;
};

struct videoFormatProps {
    vF_Type setVideoFormat = vF_Disabled;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    cS_Type colorSpace = fcc_Default;
};

struct audioProps {
    int bitsPerSample;
    int sampleRate;
    int channels;
};

struct audioFormatProps {
    aF_Type setAudioFormat = aF_Custom;
    int bitsPerSample = 0;
    int sampleRate = 0;
    int channels = 0;
};

// --- Yardımcı Fonksiyonlar ---
inline videoProps getVideoProps(vF_Type format) {
    switch (format) {
        case vF_HD720_50p: return {1280, 720, 50.0, false};
        case vF_HD720_5994p: return {1280, 720, 60000.0/1001.0, false};
        case vF_HD720_60p: return {1280, 720, 60.0, false};
        case vF_HD1080_2398p: return {1920, 1080, 24000.0/1001.0, false};
        case vF_HD1080_24p: return {1920, 1080, 24.0, false};
        case vF_HD1080_25p: return {1920, 1080, 25.0, false};
        case vF_HD1080_2997p: return {1920, 1080, 30000.0/1001.0, false};
        case vF_HD1080_30p: return {1920, 1080, 30.0, false};
        case vF_HD1080_50i: return {1920, 1080, 25.0, true}; 
        case vF_HD1080_50p: return {1920, 1080, 50.0, false};
        case vF_HD1080_5994i: return {1920, 1080, 30000.0/1001.0, true};
        case vF_HD1080_5994p: return {1920, 1080, 60000.0/1001.0, false};
        case vF_HD1080_60i: return {1920, 1080, 30.0, true};
        case vF_HD1080_60p: return {1920, 1080, 60.0, false};
        case vF__2K_DCI_2398p: return {2048, 1080, 24000.0/1001.0, false};
        case vF__2K_DCI_24p: return {2048, 1080, 24.0, false};
        case vF__2K_DCI_25p: return {2048, 1080, 25.0, false};
        case vF__2K_DCI_50p: return {2048, 1080, 50.0, false};
        case vF__2K_DCI_60p: return {2048, 1080, 60.0, false};
        case vF__4K_UHD_2398p: return {3840, 2160, 24000.0/1001.0, false};
        case vF__4K_UHD_24p: return {3840, 2160, 24.0, false};
        case vF__4K_UHD_25p: return {3840, 2160, 25.0, false};
        case vF__4K_UHD_2997p: return {3840, 2160, 30000.0/1001.0, false};
        case vF__4K_UHD_30p: return {3840, 2160, 30.0, false};
        case vF__4K_UHD_50i: return {3840, 2160, 25.0, true};
        case vF__4K_UHD_50p: return {3840, 2160, 50.0, false};
        case vF__4K_UHD_5994i: return {3840, 2160, 30000.0/1001.0, true};
        case vF__4K_UHD_5994p: return {3840, 2160, 60000.0/1001.0, false};
        case vF__4K_UHD_60i: return {3840, 2160, 30.0, true};
        case vF__4K_UHD_60p: return {3840, 2160, 60.0, false};
        case vF__4K_DCI_2398p: return {4096, 2160, 24000.0/1001.0, false};
        case vF__4K_DCI_24p: return {4096, 2160, 24.0, false};
        case vF__4K_DCI_25p: return {4096, 2160, 25.0, false};
        case vF__4K_DCI_50p: return {4096, 2160, 50.0, false};
        case vF__4K_DCI_60p: return {4096, 2160, 60.0, false};
        case vF__525i_5994: return {720, 486, 30000.0/1001.0, true};
        case vF__625i_50: return {720, 576, 25.0, true};
        default: return {0, 0, 0.0, false};
    }
}

inline audioProps getAudioProps(aF_Type format) {
    switch (format) {
        case aF__16B_11K_2CH: return {16, 11025, 2};
        case aF__16B_22K_2CH: return {16, 22050, 2};
        case aF__16B_44K_2CH: return {16, 44100, 2};
        case aF__16B_44K_6CH: return {16, 44100, 6};
        case aF__16B_48K_2CH: return {16, 48000, 2};
        case aF__16B_48K_6CH: return {16, 48000, 6};
        case aF__16B_48K_8CH: return {16, 48000, 8};
        case aF__16B_48K_16CH: return {16, 48000, 16};
        case aF__16B_96K_2CH: return {16, 96000, 2};
        case aF__16B_96K_6CH: return {16, 96000, 6};
        case aF__16B_96K_8CH: return {16, 96000, 8};
        case aF__16B_192K_2CH: return {16, 192000, 2};
        case aF__16B_192K_6CH: return {16, 192000, 6};
        case aF__16B_192K_8CH: return {16, 192000, 8};
        case aF__24B_44K_2CH: return {24, 44100, 2};
        case aF__24B_48K_2CH: return {24, 48000, 2};
        case aF__24B_48K_6CH: return {24, 48000, 6};
        case aF__24B_48K_8CH: return {24, 48000, 8};
        case aF__24B_48K_16CH: return {24, 48000, 16};
        case aF__24B_96K_2CH: return {24, 96000, 2};
        case aF__24B_96K_6CH: return {24, 96000, 6};
        case aF__24B_96K_8CH: return {24, 96000, 8};
        case aF__24B_192K_2CH: return {24, 192000, 2};
        case aF__24B_192K_6CH: return {24, 192000, 6};
        case aF__24B_192K_8CH: return {24, 192000, 8};
        case aF__32F_44K_2CH: return {-32, 44100, 2};
        case aF__32F_48K_2CH: return {-32, 48000, 2};
        case aF__32F_48K_6CH: return {-32, 48000, 6};
        case aF__32F_48K_8CH: return {-32, 48000, 8};
        case aF__32F_48K_16CH: return {-32, 48000, 16};
        case aF__32F_96K_2CH: return {-32, 96000, 2};
        case aF__32F_96K_6CH: return {-32, 96000, 6};
        case aF__32F_96K_8CH: return {-32, 96000, 8};
        case aF__32F_192K_2CH: return {-32, 192000, 2};
        case aF__32F_192K_6CH: return {-32, 192000, 6};
        case aF__32F_192K_8CH: return {-32, 192000, 8};
        default: return {0, 0, 0};
    }
}
