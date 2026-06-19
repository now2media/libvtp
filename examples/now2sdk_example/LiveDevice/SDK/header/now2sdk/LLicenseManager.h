#ifndef LLICENSEMANAGER_H
#define LLICENSEMANAGER_H

#include "now2sdk_global.h"
#include <string>
#include <map>
#include <mutex>

struct AVFrame;

namespace now2sdk {

class NOW2SDK_EXPORT LLicenseManager {
public:
    static void initialize(const std::string& licenseData);
    static bool isLicensed(const std::string& moduleName);
    static std::string getUpdateExpirationDate(const std::string& moduleName);
    static std::string getIssuedTo(const std::string& moduleName);
    static void applyWatermark(AVFrame* frame);

private:
    static std::mutex m_mutex;
    static std::map<std::string, bool> m_licensedModules;
    static std::map<std::string, std::string> m_updateExpirationDates;
    static std::map<std::string, std::string> m_issuedToMap;

    static bool verifySignature(const std::string& module, const std::string& issuedTo, const std::string& updateExpiration, const std::string& signature);
    static std::string calculateSHA256(const std::string& input);
};

} // namespace now2sdk

#endif // LLICENSEMANAGER_H
