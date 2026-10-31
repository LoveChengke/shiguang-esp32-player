#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <PocketCore.hpp>
#include <freertos/semphr.h>

class Reader {
    SemaphoreHandle_t mutex_ = nullptr;
    Preferences* prefs_ = nullptr;
    File file_;
    String key_;
    uint32_t fingerprint_ = 0, next_ = 0, size_ = 0;
    uint32_t searchAt_ = 0, searchTarget_ = 0, saveAt_ = 0;
    bool searching_ = false, dirty_ = false;
    std::vector<uint32_t> history_;
    pocket::Page readPage(uint32_t at);
    bool load(uint32_t at);
public:
    String path, error;
    pocket::Page page;
    uint32_t offset = 0;
    unsigned font = 16;
    void begin(SemaphoreHandle_t lock, Preferences& prefs) { mutex_ = lock; prefs_ = &prefs; }
    bool open(const String& filePath);
    void close();
    bool next();
    bool previous();
    bool tick();
    void save();
    void setFont(unsigned value);
    bool active() const { return bool(file_); }
    bool searching() const { return searching_; }
    unsigned progress() const { return size_ ? uint64_t(offset) * 100 / size_ : 0; }
    pocket::Layout layout() const { return {216, font == 16 ? 10U : 7U, font}; }
};
