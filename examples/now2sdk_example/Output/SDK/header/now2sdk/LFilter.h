#ifndef LFILTER_H
#define LFILTER_H

#include "LObject.h"
#include <string>
#include <map>
#include <atomic>
#include <functional>

class LFilter : public LObject {
public:
    enum Type {
        Audio,
        Video
    };

    LFilter(Type type = Audio);
    virtual ~LFilter();

    // Key: Filtre adı (örn: "volume", "highpass"), Value: Parametreler (örn: "2", "f=120")
    void setProps(const std::string& key, const std::string& value);
    
    std::string getFilterString() const;
    Type getType() const { return m_type; }

    void setNotifyCallback(std::function<void()> callback);

private:
    Type m_type;
    std::map<std::string, std::string> m_filters; // Filtreleri burada topluyoruz
    std::function<void()> m_onChanged;
};

#endif
