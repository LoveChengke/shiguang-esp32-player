#include <PocketCore.hpp>
#include <MediaCore.hpp>
#include <iostream>
#include <cstdlib>
#include <limits>
static unsigned checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { std::cerr << "FAIL line " << __LINE__ << ": " #expr "\n"; std::exit(1); } } while (0)
int main() {
    using namespace pocket;
    {
        auto p = paginate("\xEF\xBB\xBF" "你好 ESP32\r\n第二行\n第三行", {80, 2, 16}, true, true);
        CHECK(p.lines.size() == 2);
        CHECK(p.lines[0] == "你好 ESP32"); CHECK(p.lines[1] == "第二行");
        CHECK(p.consumed == std::string("\xEF\xBB\xBF" "你好 ESP32\r\n第二行\n").size());
    }
    for (unsigned font : {16U, 24U}) {
        std::string original;
        for (int i = 0; i < 10000; ++i) original += "山间的风，Hello2026！";
        size_t at = 0; std::string rebuilt;
        while (at < original.size()) {
            auto window = std::string_view(original).substr(at, 2048);
            auto p = paginate(window, {216, font == 16 ? 10U : 7U, font}, at + window.size() == original.size(), at == 0);
            CHECK(p.consumed > 0); CHECK(p.consumed <= window.size());
            CHECK(p.lines.size() <= (font == 16 ? 10 : 7)); CHECK(!p.invalidUtf8);
            for (const auto& line : p.lines) rebuilt += line;
            at += p.consumed;
            if (at < original.size()) CHECK((uint8_t(original[at]) & 0xC0) != 0x80);
        }
        CHECK(rebuilt == original);
    }
    {
        auto p = paginate("abcd\r", {32, 2, 16}, false);
        CHECK(p.consumed == 4); CHECK(p.lines[0] == "abcd");
        auto next = paginate("\r\nef", {32, 2, 16}, true);
        CHECK(next.lines.size() == 2); CHECK(next.consumed == 4);
        auto partial = paginate("a\xE4\xB8", {80, 2, 16}, false);
        CHECK(partial.consumed == 1); CHECK(partial.lines[0] == "a");
        auto final = paginate("a\xE4\xB8", {80, 2, 16}, true);
        CHECK(final.consumed == 3); CHECK(final.invalidUtf8);
        CHECK(paginate("\xC0\xAF\xED\xA0\x80", {80, 2, 16}, true).invalidUtf8);
        CHECK(paginate("", {}, true).consumed == 0);
        CHECK(paginate("\tX", {48, 2, 16}, true).lines[0] == "    X");
        CHECK(paginate("abc", {1, 1, 16}, true).consumed == 1);
    }
    {
        Button b;
        CHECK(b.update(true, 0) == Press::None);
        CHECK(b.update(false, 10) == Press::None);
        CHECK(b.update(true, 15) == Press::None);
        CHECK(b.update(true, 45) == Press::None);
        CHECK(b.update(false, 100) == Press::None);
        CHECK(b.update(false, 130) == Press::Short);
        CHECK(b.update(false, 200) == Press::None);
        CHECK(b.update(true, 300) == Press::None);
        CHECK(b.update(true, 330) == Press::None);
        CHECK(b.update(true, 980) == Press::Long);
        CHECK(b.update(true, 1100) == Press::None);
        CHECK(b.update(false, 1200) == Press::None);
        CHECK(b.update(false, 1230) == Press::None);
        Button rollover;
        rollover.update(true, UINT32_MAX - 40); rollover.update(true, UINT32_MAX - 10);
        CHECK(rollover.update(true, 650) == Press::Long);
    }
    {
        Bookmark saved{1, 100, hash32("first bytes"), 60};
        CHECK(saved.valid(100, hash32("first bytes")));
        CHECK(!saved.valid(101, hash32("first bytes")));
        CHECK(!saved.valid(100, hash32("replaced file")));
        saved.offset = 100; CHECK(!saved.valid(100, hash32("first bytes")));
        CHECK(extension("/music/中文.MP3", ".mp3")); CHECK(!extension("a.mp3.bak", ".mp3"));
        CHECK(hash64("/books/a.txt") != hash64("/books/b.txt"));
    }
    {
        uint8_t frame[417]{};frame[0]=255;frame[1]=251;frame[2]=144;
        auto header=mp3Frame(frame);CHECK(header.bytes==417);CHECK(header.rate==44100);CHECK(header.samples==1152);
        Mp3Timing timing;timing.parse(frame,sizeof(frame),200,1600200);CHECK(timing.duration==100000);CHECK(timing.position(40)==640200);
        std::memcpy(frame+36,"Xing",4);frame[43]=7;frame[46]=3;frame[47]=232; // flags 7, 1000 frames
        frame[48]=0;frame[49]=24;frame[50]=106;frame[51]=0; // 1,600,000 bytes
        for(unsigned i=0;i<100;++i)frame[52+i]=i*2;
        timing.parse(frame,sizeof(frame),200,1600200);CHECK(timing.hasToc);CHECK(timing.duration==26122);CHECK(timing.position(40)==500200);
        frame[1]=0;CHECK(mp3Frame(frame).bytes==0);
        auto lrc=lyrics("[ti:Example]\n[offset:-500]\n[00:02.50][00:04.123]第二行\n[00:01.00]第一行\n");
        CHECK(lrc.size()==3);CHECK(lrc[0].at==500);CHECK(lrc[0].text=="第一行");CHECK(lrc[1].at==2000);CHECK(lrc[2].at==3623);
        auto plain=lyrics("普通歌词\n另一句\n");CHECK(plain.size()==2);CHECK(plain[0].at==0);
        CHECK(lyrics("[bad]\n\n").empty());
        std::string many;for(int i=0;i<200;++i)many+="[00:01.00]test\n";CHECK(lyrics(many).size()==96);
    }
    std::cout << "PASS " << checks << " checks (firmware pagination, UTF-8, bookmarks, debounce, MP3 timing and lyrics)\n";
}
