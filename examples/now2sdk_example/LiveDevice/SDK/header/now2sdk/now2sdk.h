#ifndef NOW2SDK_H
#define NOW2SDK_H

#include "now2sdk_global.h"
#include "LLicenseManager.h"
#include "LFile.h"
#include "LAnimation.h"
#include "LPreview.h"
#include "LVideoWidget.h"
#include "LReader.h"
#include "LRecorder.h"
#include "LLive.h"
#include "LMixer.h"
#include "LSwitcher.h"
#include "LSignal.h"
#include "LStinger.h"
#include "LCharacter.h"
#include "LOutput.h"
#include "LFilter.h"
#include "LReplay.h"
#include "LConvert.h"
#include "LStreamPlay.h"
#include "LStream.h"

// SDK Metadata functions
extern "C" {
    NOW2SDK_EXPORT const char* now2sdk_get_version();
    NOW2SDK_EXPORT const char* now2sdk_get_author();
    NOW2SDK_EXPORT const char* now2sdk_get_company();
}

#endif // NOW2SDK_H
