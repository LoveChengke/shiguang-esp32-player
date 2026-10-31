#include <Arduino.h>
#include <Preferences.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputNull.h>
#include <PocketCore.hpp>
#include "diagnostic_tone.hpp"

class SampleCounter : public AudioOutputNull {
public:
    uint32_t samples = 0;
    bool ConsumeSample(int16_t sample[2]) override { (void)sample; ++samples; return true; }
};
void tests() {
    auto page = pocket::paginate("你好 ESP32\r\n第二行\n第三行", {80, 2, 16}, true);
    bool textOk = page.lines.size() == 2 && page.lines[0] == "你好 ESP32" && page.lines[1] == "第二行";
    Serial.printf("SELFTEST UTF8_PAGE %s consumed=%u\n", textOk ? "PASS" : "FAIL", unsigned(page.consumed));
    AudioFileSourcePROGMEM source(diagnosticTone, sizeof(diagnosticTone));
    SampleCounter output;
    AudioGeneratorMP3 decoder;
    bool started = decoder.begin(&source, &output);
    uint32_t began = millis();
    while (started && decoder.isRunning() && millis() - began < 10000) {
        if (!decoder.loop()) decoder.stop();
        delay(1);
    }
    bool decoded = started && !decoder.isRunning() && output.samples >= 10000;
    decoder.stop();
    Serial.printf("SELFTEST MP3_DECODE %s samples=%u bytes=%u elapsed_ms=%u heap=%u\n", decoded ? "PASS" : "FAIL", output.samples, unsigned(sizeof(diagnosticTone)), millis() - began, ESP.getFreeHeap());
    Serial.printf("SELFTEST RESULT %s\n", textOk && decoded ? "PASS" : "FAIL");
}
void setup() {
    Serial.begin(115200); delay(500);
    bool ramDetected = psramInit();
    Serial.printf("PSRAM_PROBE detected=%d\n", ramDetected);
    Serial.printf("\nPOCKET_DIAGNOSTIC chip=%s rev=%u cores=%u cpuMHz=%u flash=%u psram=%u heap=%u sdk=%s\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz(), ESP.getFlashChipSize(), ESP.getPsramSize(), ESP.getFreeHeap(), ESP.getSdkVersion());
    // Separate diagnostic namespace; observe persistence across reset.
    Preferences prefs;
    if (prefs.begin("pocket-diag", false)) {
        uint32_t boots = prefs.getUInt("boots", 0) + 1;
        bool stored = prefs.putUInt("boots", boots) == sizeof(boots);
        Serial.printf("SELFTEST NVS %s boots=%u\n", stored ? "PASS" : "FAIL", boots);
        prefs.end();
    } else Serial.println("SELFTEST NVS FAIL");
    tests();
    Serial.println("READY: t=repeat tests, ?=memory; display/SD/I2S not initialized");
}
void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 't') tests();
        else if (c == '?') Serial.printf("MEM heap=%u minHeap=%u psram=%u uptime_ms=%u\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getPsramSize(), millis());
    }
    delay(10);
}
