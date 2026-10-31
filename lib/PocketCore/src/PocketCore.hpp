#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pocket {
struct Glyph { uint32_t code; size_t bytes; bool incomplete; };
inline Glyph decode(std::string_view s, size_t at) {
    const auto a = static_cast<uint8_t>(s[at]);
    if (a < 0x80) return {a, 1, false};
    size_t n = a >= 0xC2 && a <= 0xDF ? 2 : a >= 0xE0 && a <= 0xEF ? 3 : a >= 0xF0 && a <= 0xF4 ? 4 : 0;
    if (!n) return {0xFFFD, 1, false};
    if (at + n > s.size()) return {0xFFFD, s.size() - at, true};
    uint32_t cp = a & (0x7F >> n);
    for (size_t i = 1; i < n; ++i) {
        auto b = static_cast<uint8_t>(s[at + i]);
        if ((b & 0xC0) != 0x80) return {0xFFFD, 1, false};
        cp = (cp << 6) | (b & 0x3F);
    }
    if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) ||
        cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return {0xFFFD, 1, false};
    return {cp, n, false};
}
struct Layout {
    unsigned width = 216;
    unsigned rows = 10;
    unsigned font = 16;
};
struct Page {
    std::vector<std::string> lines;
    size_t consumed = 0;
    bool invalidUtf8 = false;
};
// Every offset is an original-file byte offset, never a Unicode character index.
// A window must contain at least one full line. No whole-book allocation occurs.
inline Page paginate(std::string_view text, Layout layout, bool eof, bool fileStart = false) {
    Page page;
    if (!layout.rows || !layout.width || !layout.font) return page;
    size_t at = fileStart && text.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;
    std::string line;
    unsigned width = 0;
    auto flush = [&] { page.lines.push_back(line); line.clear(); width = 0; };
    while (at < text.size() && page.lines.size() < layout.rows) {
        auto g = decode(text, at);
        if (g.incomplete && !eof) break;
        if (g.code == '\r' || g.code == '\n') {
            if (g.code == '\r' && at + 1 == text.size() && !eof) break;
            at += g.bytes;
            if (g.code == '\r' && at < text.size() && text[at] == '\n') ++at;
            flush();
            continue;
        }
        const unsigned glyphWidth = g.code == '\t' ? layout.font * 2 : g.code < 0x80 ? layout.font / 2 : layout.font;
        if (width && width + glyphWidth > layout.width) {
            flush();
            if (page.lines.size() == layout.rows) break;
        }
        if (g.code == '\t') line += "    ";
        else if (g.code == 0xFFFD || g.code < 32 || g.code == 127) {
            line += "?";
            page.invalidUtf8 |= g.code == 0xFFFD;
        } else line.append(text.data() + at, g.bytes);
        width += glyphWidth;
        at += g.bytes;
    }
    if (!line.empty() && page.lines.size() < layout.rows) flush();
    page.consumed = at;
    return page;
}
inline uint32_t hash32(std::string_view value) {
    uint32_t hash = 2166136261U;
    for (unsigned char c : value) hash = (hash ^ c) * 16777619U;
    return hash;
}
inline uint64_t hash64(std::string_view value) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : value) hash = (hash ^ c) * 1099511628211ULL;
    return hash;
}
inline bool extension(std::string path, std::string_view ext) {
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; });
    return path.size() >= ext.size() && path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
}
enum class Press { None, Short, Long };
class Button {
    bool raw_ = false, stable_ = false, longSent_ = false;
    uint32_t changed_ = 0, pressed_ = 0;
public:
    Press update(bool down, uint32_t now) {
        if (down != raw_) { raw_ = down; changed_ = now; }
        if (now - changed_ >= 30 && stable_ != raw_) {
            stable_ = raw_;
            if (stable_) { pressed_ = now; longSent_ = false; }
            else if (!longSent_) return Press::Short;
        }
        if (stable_ && !longSent_ && now - pressed_ >= 650) {
            longSent_ = true;
            return Press::Long;
        }
        return Press::None;
    }
};
struct Bookmark {
    uint32_t version = 1, fileSize = 0, fingerprint = 0, offset = 0;
    bool valid(uint32_t size, uint32_t headHash) const {
        return version == 1 && fileSize == size && fingerprint == headHash && offset < size;
    }
};
}
