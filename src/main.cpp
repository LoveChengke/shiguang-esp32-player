#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <PocketCore.hpp>
#include <algorithm>
#include "audio_service.hpp"
#include "board.hpp"
#include "reader.hpp"

class Display : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 panel;
    lgfx::Bus_SPI bus;
public:
    Display() {
        auto b = bus.config();
#if defined(POCKET_BOARD_S3)
        b.spi_host = SPI2_HOST;
#else
        b.spi_host = SPI3_HOST;
#endif
        b.spi_mode = 0; b.freq_write = 40000000; b.freq_read = 16000000;
        b.pin_sclk = board::tftSck; b.pin_mosi = board::tftMosi; b.pin_miso = -1;
        b.pin_dc = board::tftDc; b.dma_channel = SPI_DMA_CH_AUTO; b.use_lock = true;
        bus.config(b); panel.setBus(&bus);
        auto p = panel.config();
        p.pin_cs = board::tftCs; p.pin_rst = board::tftReset; p.pin_busy = -1;
        p.panel_width = p.memory_width = 240; p.panel_height = p.memory_height = 320;
        p.readable = false; p.invert = false; p.rgb_order = false;
        panel.config(p); setPanel(&panel);
    }
};
namespace {
Display lcd;
SPIClass sdSpi(HSPI);
SemaphoreHandle_t sdMutex;
Preferences prefs;
Reader reader;
std::vector<String> music, books;
pocket::Button buttons[5];
enum class View { Home, Music, Books, Player, Reader, Settings };
View view = View::Home;
int selected = 0, homeSelected = 0, settingSelected = 0;
uint8_t volume = 10;
bool sdReady = false, audioReady = false, repeatOne = false, paper = false, redraw = true;
bool settingsDirty = false, catalogTruncated = false;
uint32_t settingsAt = 0, lastRefresh = 0, finishedSeen = 0, toastAt = 0;
String toast;
uint16_t bg, card, ink, muted;
constexpr uint16_t accent = 0x35F8;

void palette() {
    bg = paper ? 0xF79C : 0x0841; card = paper ? 0xE6F9 : 0x10C3;
    ink = paper ? 0x18C3 : 0xEF7D; muted = paper ? 0x6B6D : 0x8C71;
}
String basename(const String& path) {
    int slash = path.lastIndexOf('/'); return slash < 0 ? path : path.substring(slash + 1);
}
String fit(const String& text, unsigned width) {
    if (lcd.textWidth(text) <= int(width)) return text;
    String result;
    std::string_view value(text.c_str());
    for (size_t at = 0; at < value.size();) {
        auto g = pocket::decode(value, at);
        String next = result + text.substring(at, at + g.bytes);
        if (lcd.textWidth(next + "...") > int(width)) break;
        result = next; at += g.bytes;
    }
    return result + "...";
}
void text(const String& value, int x, int y, uint16_t color) {
    lcd.setTextColor(color, bg); lcd.setCursor(x, y); lcd.print(value);
}
void notice(const String& value) { toast = value; toastAt = millis(); redraw = true; Serial.println(value); }
void saveSettings() {
    if (!settingsDirty) return;
    // One NVS blob avoids partially updated settings after power loss.
    uint8_t data[] = {1, volume, uint8_t(reader.font), uint8_t(repeatOne), uint8_t(paper)};
    if (prefs.putBytes("settings", data, sizeof(data)) == sizeof(data)) settingsDirty = false;
    else { settingsAt = millis(); notice("设置保存失败"); }
}
void changedSettings() { settingsDirty = true; settingsAt = millis(); redraw = true; }
void setVolume(int value) {
    uint8_t next = constrain(value, 0, 21);
    if (audioReady && !player::volume(next)) { notice("操作繁忙，请稍后再试"); return; }
    volume = next; changedSettings(); notice("音量 " + String(volume) + " / 21");
}
void scan(const String& root, unsigned depth = 0) {
    if (depth > 3) return;
    File dir = SD.open(root);
    if (!dir || !dir.isDirectory()) return;
    while (File entry = dir.openNextFile()) {
        String name = entry.name();
        String path = name.startsWith("/") ? name : root + "/" + name;
        bool directory = entry.isDirectory(); entry.close();
        if (basename(path).startsWith(".")) continue;
        if (path.length() >= board::maxPathBytes) { catalogTruncated = true; continue; }
        if (directory) scan(path, depth + 1);
        else {
            auto& target = root.startsWith("/music") ? music : books;
            bool accepted = root.startsWith("/music") ? pocket::extension(path.c_str(), ".mp3") : pocket::extension(path.c_str(), ".txt");
            if (accepted) {
                if (target.size() < board::maxFiles) target.push_back(path);
                else catalogTruncated = true;
            }
        }
    }
    dir.close();
}
void startTrack(int index) {
    if (music.empty() || !audioReady) { notice("请先放入 MP3 文件"); return; }
    index = (index + int(music.size())) % int(music.size());
    if (!player::play(music[index].c_str(), index)) notice("播放命令未提交，请重试");
    redraw = true;
}
void togglePause() {
    auto state = player::status();
    if (!audioReady) { notice("音频任务不可用"); return; }
    if (state.state == AudioState::Playing || state.state == AudioState::Paused) {
        if (!player::pause()) notice("操作繁忙，请稍后再试");
    } else startTrack(state.track >= 0 ? state.track : 0);
    redraw = true;
}
void header(const String& title, const String& subtitle) {
    lcd.setFont(&fonts::efontCN_16);
    text(title, 12, 10, ink); text("OFFLINE", 178, 12, accent);
    text(fit(subtitle, 216), 12, 32, muted);
    lcd.drawFastHLine(12, 55, 216, card);
}
void footer(const String& hint) {
    lcd.setFont(&fonts::efontCN_12);
    lcd.fillRect(0, 285, 240, 35, card);
    lcd.setTextColor(muted, card); lcd.setCursor(10, 292);
    lcd.print(fit(toast.length() ? toast : hint, 220));
}
void drawHome() {
    header("拾光 · 随身播放器", sdReady ? "本地音乐与书页，随时继续" : "未检测到存储卡");
    const String titles[] = {"听音乐", "读小说", "偏好设置"};
    const String subtitles[] = {String(music.size()) + " 首 MP3", String(books.size()) + " 本 TXT", "音量 / 字号 / 播放模式"};
    for (int i = 0; i < 3; ++i) {
        int y = 68 + i * 67;
        lcd.fillRoundRect(12, y, 216, 57, 7, card);
        if (homeSelected == i) lcd.drawRoundRect(12, y, 216, 57, 7, accent);
        lcd.setFont(&fonts::efontCN_16); lcd.setTextColor(ink, card); lcd.setCursor(24, y + 8); lcd.print(titles[i]);
        lcd.setFont(&fonts::efontCN_12); lcd.setTextColor(muted, card); lcd.setCursor(24, y + 33); lcd.print(subtitles[i]);
        lcd.setTextColor(accent, card); lcd.setCursor(200, y + 19); lcd.print("0" + String(i + 1));
    }
    footer(sdReady ? "上下选择 · 确认进入 · 模式切换" : "插入 FAT32 卡后按确认重试");
}
void drawLibrary(bool songs) {
    auto& items = songs ? music : books;
    header(songs ? "音乐库" : "我的书架", String(items.size()) + (songs ? " 首 · MP3" : " 本 · UTF-8 TXT"));
    if (items.empty()) {
        text(songs ? "将音乐放入 /music" : "将小说放入 /books", 16, 112, ink);
        text("复制到卡后重新启动", 16, 145, muted);
    } else {
        int first = selected / 6 * 6;
        for (int i = first; i < std::min(first + 6, int(items.size())); ++i) {
            int y = 66 + (i - first) * 35;
            if (i == selected) lcd.fillRoundRect(10, y, 220, 31, 5, card);
            lcd.setTextColor(i == selected ? accent : ink, i == selected ? card : bg);
            lcd.setCursor(16, y + 7); lcd.print(fit(basename(items[i]), 207));
        }
    }
    footer("上下选择 · 确认打开 · 返回主页");
}
void drawPlayer() {
    auto state = player::status();
    header("正在播放", "存储卡本地解码 · I2S 外放");
    lcd.drawCircle(120, 119, 40, accent); lcd.drawCircle(120, 119, 28, card);
    lcd.fillCircle(120, 119, 5, accent);
    String title = state.track >= 0 && state.track < int(music.size()) ? basename(music[state.track]) : "选择一首音乐开始";
    text(fit(title, 216), 12, 176, ink);
    const char* label = state.state == AudioState::Playing ? "播放中" : state.state == AudioState::Paused ? "已暂停" : state.state == AudioState::Error ? "文件无法解码" : "已停止";
    text(String(label) + "   音量 " + volume, 12, 204, muted);
    lcd.fillRoundRect(12, 235, 216, 4, 2, card);
    if (state.progress) lcd.fillRect(12, 235, 216 * state.progress / 100, 4, accent);
    lcd.setFont(&fonts::efontCN_12);
    text("文件进度 " + String(state.progress) + "%", 12, 249, muted);
    footer("确认暂停 · 上下切歌 · 长按上下调音量");
}
void drawReader() {
    header("阅读 " + String(reader.progress()) + "%", basename(reader.path));
    lcd.setFont(reader.font == 24 ? &fonts::efontCN_24 : &fonts::efontCN_16);
    lcd.setTextColor(ink, bg);
    int y = 64, lineHeight = reader.font == 24 ? 30 : 22;
    for (const auto& line : reader.page.lines) { lcd.setCursor(12, y); lcd.print(line.c_str()); y += lineHeight; }
    String hint = reader.searching() ? "正在定位上一页 · 返回可取消" : "上下翻页 · 确认字号 · 长确认暂停音乐";
    if (reader.error.length()) hint = reader.error;
    else if (reader.page.invalidUtf8) hint = "发现无效编码，请先转换为 UTF-8";
    footer(hint);
}
void drawSettings() {
    header("偏好设置", "设置保存在主板，下次继续使用");
    String rows[] = {"音量       " + String(volume) + " / 21", "小说字号   " + String(reader.font),
                     String("播放模式   ") + (repeatOne ? "单曲循环" : "列表循环"),
                     String("界面主题   ") + (paper ? "暖纸" : "深夜")};
    for (int i = 0; i < 4; ++i) {
        int y = 73 + i * 47;
        if (i == settingSelected) lcd.fillRoundRect(12, y, 216, 39, 6, card);
        lcd.setTextColor(i == settingSelected ? accent : ink, i == settingSelected ? card : bg);
        lcd.setCursor(22, y + 10); lcd.print(rows[i]);
    }
    footer("确认更改 · 长按上下调音量 · 返回");
}
void draw() {
    palette(); lcd.fillScreen(bg); lcd.setFont(&fonts::efontCN_16);
    switch (view) {
    case View::Home: drawHome(); break;
    case View::Music: drawLibrary(true); break;
    case View::Books: drawLibrary(false); break;
    case View::Player: drawPlayer(); break;
    case View::Reader: drawReader(); break;
    case View::Settings: drawSettings(); break;
    }
    redraw = false;
}
void event(int key, pocket::Press press) {
    if (press == pocket::Press::Long) {
        if (key < 2) setVolume(int(volume) + (key == 0 ? 1 : -1));
        else if (key == 2 || key == 4) togglePause();
        else { reader.save(); saveSettings(); view = View::Home; redraw = true; }
        return;
    }
    if (press != pocket::Press::Short) return;
    if (key == 4) {
        view = view == View::Player ? (reader.active() ? View::Reader : View::Books) : View::Player;
        selected = 0; redraw = true; return;
    }
    if (key == 3) {
        reader.save(); saveSettings();
        if (view == View::Reader) { reader.close(); view = View::Books; selected = 0; }
        else if (view == View::Player) { view = View::Music; selected = std::max(0, player::status().track); }
        else view = View::Home;
        redraw = true; return;
    }
    int direction = key == 0 ? -1 : 1;
    if (view == View::Home) {
        if (key < 2) homeSelected = (homeSelected + direction + 3) % 3;
        else if (key == 2) {
            if (!sdReady && homeSelected != 2) { ESP.restart(); return; }
            selected = 0;
            view = homeSelected == 0 ? View::Music : homeSelected == 1 ? View::Books : View::Settings;
        }
    } else if (view == View::Music || view == View::Books) {
        auto& items = view == View::Music ? music : books;
        if (items.empty()) return;
        if (key < 2) selected = (selected + direction + int(items.size())) % int(items.size());
        else if (key == 2 && view == View::Music) { startTrack(selected); view = View::Player; }
        else if (key == 2) { if (reader.open(items[selected])) view = View::Reader; else notice(reader.error); }
    } else if (view == View::Player) {
        if (key == 2) togglePause();
        else startTrack(std::max(0, player::status().track) + direction);
    } else if (view == View::Reader) {
        if (key == 0) reader.previous();
        else if (key == 1) reader.next();
        else { reader.setFont(reader.font == 16 ? 24 : 16); changedSettings(); }
    } else if (view == View::Settings) {
        if (key < 2) settingSelected = (settingSelected + direction + 4) % 4;
        else {
            if (settingSelected == 0) setVolume((volume + 1) % 22);
            else if (settingSelected == 1) reader.setFont(reader.font == 16 ? 24 : 16);
            else if (settingSelected == 2) repeatOne = !repeatOne;
            else paper = !paper;
            changedSettings();
        }
    }
    redraw = true;
}
void serialInput() {
    // USB-only UI diagnostics: u/d/o/b/m are the five short presses; p toggles audio.
    for (unsigned n = 0; Serial.available() && n < 8; ++n) {
        char c = Serial.read(); const char* keys = "udobm"; const char* key = strchr(keys, c);
        if (c && key) event(key - keys, pocket::Press::Short);
        else if (c == 'p') togglePause();
        else if (c == '+') setVolume(volume + 1);
        else if (c == '-') setVolume(volume - 1);
        else if (c == '?') Serial.printf("STATE view=%d heap=%u sd=%d music=%u books=%u audio=%d bookByte=%u\n", int(view), ESP.getFreeHeap(), sdReady, unsigned(music.size()), unsigned(books.size()), int(player::status().state), reader.offset);
    }
}
}
void setup() {
    Serial.begin(115200); delay(200);
    Serial.printf("\nPOCKET boot: chip=%s rev=%u cores=%u flash=%u psram=%u heap=%u\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getFlashChipSize(), ESP.getPsramSize(), ESP.getFreeHeap());
    Serial.printf("Reference wiring: %s\n", board::name);
    for (int pin : board::buttons) pinMode(pin, INPUT);
    sdMutex = xSemaphoreCreateMutex();
    if (!sdMutex) { Serial.println("FATAL SD mutex allocation failed"); return; }
    bool preferencesOk = prefs.begin("pocket", false);
    if (!preferencesOk) Serial.println("WARNING NVS unavailable: progress cannot be saved");
    uint8_t settings[5]{};
    if (prefs.getBytesLength("settings") == sizeof(settings)) {
        prefs.getBytes("settings", settings, sizeof(settings));
        if (settings[0] == 1) {
            volume = std::min<uint8_t>(21, settings[1]); reader.font = settings[2] == 24 ? 24 : 16;
            repeatOne = settings[3]; paper = settings[4];
        }
    }
    reader.begin(sdMutex, prefs);
    lcd.init(); lcd.setRotation(0); lcd.setTextWrap(false); lcd.setTextSize(1);
    sdSpi.begin(board::sdSck, board::sdMiso, board::sdMosi, board::sdCs);
    sdReady = SD.begin(board::sdCs, sdSpi, 10000000);
    if (sdReady) {
        scan("/music"); scan("/books");
        auto compare = [](const String& a, const String& b) { return a.compareTo(b) < 0; };
        std::sort(music.begin(), music.end(), compare); std::sort(books.begin(), books.end(), compare);
        audioReady = player::begin(sdMutex);
        if (audioReady) player::volume(volume);
    }
    Serial.printf("READY sd=%d audio=%d tracks=%u books=%u heap=%u\n", sdReady, audioReady, unsigned(music.size()), unsigned(books.size()), ESP.getFreeHeap());
    if (!sdReady) notice("未检测到 SD 卡，插卡后按确认重试");
    else if (catalogTruncated) notice("部分文件超出数量、路径或目录限制");
    else if (!audioReady) notice("音频任务创建失败");
    draw();
}
void loop() {
    if (!sdMutex) { delay(100); return; }
    const uint32_t now = millis();
    for (int i = 0; i < 5; ++i) event(i, buttons[i].update(digitalRead(board::buttons[i]) == LOW, now));
    serialInput();
    if (reader.active() && reader.tick()) redraw = true;
    if (settingsDirty && now - settingsAt >= 750) saveSettings();
    auto state = player::status();
    if (state.finished != finishedSeen) {
        finishedSeen = state.finished;
        if (state.track >= 0) startTrack(state.track + (repeatOne ? 0 : 1));
    }
    if (toast.length() && now - toastAt >= 2500) { toast = ""; redraw = true; }
    if (view == View::Player && now - lastRefresh >= 500) { redraw = true; lastRefresh = now; }
    if (redraw) draw();
    delay(2);
}
