#pragma once
#include <Arduino.h>
#include <SD.h>
#include <MediaCore.hpp>
#include <freertos/semphr.h>

// Bounded metadata only; audio objects remain exclusively in the audio task.
class MusicInfo {
    SemaphoreHandle_t mutex_ = nullptr;
    bool read(File& file,uint32_t at,uint8_t* bytes,unsigned count);
public:
    int track=-1;
    std::vector<uint8_t> cover;
    std::vector<pocket::Lyric> lines;
    bool png=false;
    unsigned width=0,height=0;
    String warning;
    void begin(SemaphoreHandle_t lock){mutex_=lock;}
    void load(const String& path,int index);
    unsigned current(uint32_t elapsed) const;
};
