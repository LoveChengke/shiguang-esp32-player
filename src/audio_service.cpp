#include "audio_service.hpp"
#include "board.hpp"
#include <AudioFileSource.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <SD.h>
#include <driver/i2s.h>
#include <freertos/queue.h>
#include <memory>
#include <new>

namespace {
SemaphoreHandle_t sdMutex;
QueueHandle_t commands;
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
Playback shared;
enum class Kind { Play, Pause, Volume };
struct Command { Kind kind; int value; char path[board::maxPathBytes]; };
class Lock {
public:
    Lock() { xSemaphoreTake(sdMutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(sdMutex); }
};
// All file operations, including reads from the audio task, use the same lock.
class SDSource : public AudioFileSource {
    File file;
    uint32_t size = 0, position = 0;
public:
    bool open(const char* path) override {
        Lock lock; file = SD.open(path); size = file ? file.size() : 0; position = 0; return bool(file);
    }
    uint32_t read(void* data, uint32_t length) override {
        Lock lock;
        if (!file) return 0;
        int n = file.read(static_cast<uint8_t*>(data), length);
        if (n > 0) position += n;
        return n > 0 ? n : 0;
    }
    bool seek(int32_t offset, int direction) override {
        Lock lock;
        int64_t target = direction == SEEK_SET ? offset : direction == SEEK_CUR ? int64_t(position) + offset : int64_t(size) + offset;
        if (!file || target < 0 || target > size || !file.seek(target)) return false;
        position = target; return true;
    }
    bool close() override { Lock lock; file.close(); return true; }
    bool isOpen() override { Lock lock; return bool(file); }
    uint32_t getSize() override { return size; }
    uint32_t getPos() override { return position; }
    ~SDSource() override { close(); }
};
class Output : public AudioOutputI2S {
public:
    uint32_t samples = 0;
    Output() : AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 16) {}
    bool ConsumeSample(int16_t sample[2]) override {
        bool accepted = AudioOutputI2S::ConsumeSample(sample);
        if (accepted) ++samples;
        return accepted;
    }
};
void publish(const Playback& state) { portENTER_CRITICAL(&statusMux); shared = state; portEXIT_CRITICAL(&statusMux); }
void task(void*) {
    Output output;
    output.SetMclk(false);
    output.SetPinout(board::i2sBclk, board::i2sLrc, board::i2sDout, -1);
    output.SetOutputModeMono(true);
    uint8_t volume = 10;
    output.SetGain(volume / 21.0f * 0.7f);
    std::unique_ptr<SDSource> source;
    std::unique_ptr<AudioFileSourceBuffer> buffer;
    std::unique_ptr<AudioGeneratorMP3> decoder;
    Playback state;
    auto stop = [&] {
        if (decoder) decoder->stop();
        decoder.reset(); buffer.reset(); source.reset();
    };
    uint32_t published = 0;
    for (;;) {
        Command command;
        while (xQueueReceive(commands, &command, 0) == pdTRUE) {
            if (command.kind == Kind::Play) {
                stop();
                output.samples = 0;
                state.track = command.value; state.progress = 0;
                source.reset(new (std::nothrow) SDSource());
                bool ok = source && source->open(command.path);
                if (ok) {
                    buffer.reset(new (std::nothrow) AudioFileSourceBuffer(source.get(), 8192));
                    decoder.reset(new (std::nothrow) AudioGeneratorMP3());
                    ok = buffer && decoder && decoder->begin(buffer.get(), &output);
                }
                state.state = ok ? AudioState::Playing : AudioState::Error;
                if (!ok) { stop(); Serial.println("AUDIO open/decode failed"); }
                publish(state);
            } else if (command.kind == Kind::Pause && decoder) {
                if (state.state == AudioState::Playing) {
                    i2s_stop(I2S_NUM_0);
                    i2s_zero_dma_buffer(I2S_NUM_0);
                    state.state = AudioState::Paused;
                } else if (state.state == AudioState::Paused) {
                    i2s_start(I2S_NUM_0);
                    state.state = AudioState::Playing;
                }
                publish(state);
            } else if (command.kind == Kind::Volume) {
                volume = constrain(command.value, 0, 21);
                output.SetGain(volume / 21.0f * 0.7f);
            }
        }
        if (decoder && state.state == AudioState::Playing) {
            if (!decoder->loop()) {
                // Treat a premature decoder stop as an error, avoiding a corrupt-file loop.
                const bool eof = output.samples >= 576 && source->getSize() && source->getPos() >= source->getSize();
                // Drain the final DMA samples before uninstalling the live output.
                if (eof && decoder->isRunning()) output.flush();
                stop();
                state.state = eof ? AudioState::Stopped : AudioState::Error;
                if (eof) { ++state.finished; state.progress = 100; }
                publish(state);
            } else if (millis() - published >= 250) {
                auto size = source->getSize();
                auto pos = source->getPos();
                auto fill = buffer->getFillLevel();
                state.progress = size ? uint64_t(pos > fill ? pos - fill : 0) * 100 / size : 0;
                publish(state); published = millis();
            }
        }
        vTaskDelay(1);
    }
}
bool send(const Command& command) { return commands && xQueueSend(commands, &command, 0) == pdTRUE; }
}
namespace player {
bool begin(SemaphoreHandle_t lock) {
    sdMutex = lock;
    commands = xQueueCreate(6, sizeof(Command));
    if (!commands) return false;
    if (xTaskCreatePinnedToCore(task, "mp3", 8192, nullptr, 2, nullptr, 0) != pdPASS) {
        vQueueDelete(commands); commands = nullptr; return false;
    }
    return true;
}
bool play(const char* path, int index) {
    if (strlen(path) >= board::maxPathBytes) return false;
    Command command{}; command.kind = Kind::Play; command.value = index;
    strlcpy(command.path, path, sizeof(command.path));
    return send(command);
}
bool pause() { Command c{}; c.kind = Kind::Pause; return send(c); }
bool volume(uint8_t value) { Command c{}; c.kind = Kind::Volume; c.value = value; return send(c); }
Playback status() { portENTER_CRITICAL(&statusMux); auto copy = shared; portEXIT_CRITICAL(&statusMux); return copy; }
}
