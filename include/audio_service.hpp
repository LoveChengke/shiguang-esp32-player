#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum class AudioState : uint8_t { Stopped, Playing, Paused, Error };
struct Playback {
    AudioState state = AudioState::Stopped;
    int track = -1;
    uint32_t finished = 0;
    uint8_t progress = 0;
};
namespace player {
bool begin(SemaphoreHandle_t sdLock);
bool play(const char* path, int index);
bool pause();
bool volume(uint8_t value);
Playback status();
}
