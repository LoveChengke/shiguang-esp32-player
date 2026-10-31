#include "reader.hpp"
#include <array>

pocket::Page Reader::readPage(uint32_t at) {
    std::array<char, 2048> bytes{};
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool seeked = file_.seek(at);
    int count = seeked ? file_.read(reinterpret_cast<uint8_t*>(bytes.data()), bytes.size()) : -1;
    xSemaphoreGive(mutex_);
    if (count <= 0) return {};
    return pocket::paginate(std::string_view(bytes.data(), count), layout(), at + count >= size_, at == 0);
}
bool Reader::load(uint32_t at) {
    auto candidate = readPage(at);
    if (!candidate.consumed) { error = "读取失败，请检查存储卡"; return false; }
    page = std::move(candidate); offset = at; next_ = at + page.consumed;
    dirty_ = true; saveAt_ = millis(); error = "";
    return true;
}
bool Reader::open(const String& filePath) {
    close(); path = filePath; error = "";
    xSemaphoreTake(mutex_, portMAX_DELAY);
    file_ = SD.open(path);
    size_ = file_ ? file_.size() : 0;
    std::array<char, 512> head{};
    int n = file_ ? file_.read(reinterpret_cast<uint8_t*>(head.data()), head.size()) : 0;
    xSemaphoreGive(mutex_);
    if (!size_ || n <= 0) { close(); error = "文件为空或无法打开"; return false; }
    if ((uint8_t(head[0]) == 0xFF && uint8_t(head[1]) == 0xFE) ||
        (uint8_t(head[0]) == 0xFE && uint8_t(head[1]) == 0xFF)) {
        close(); error = "请先将小说转换为 UTF-8"; return false;
    }
    fingerprint_ = pocket::hash32(std::string_view(head.data(), n));
    char key[16];
    snprintf(key, sizeof(key), "b%014llx", pocket::hash64(path.c_str()) & 0x00FFFFFFFFFFFFFFULL);
    key_ = key;
    pocket::Bookmark saved;
    uint32_t start = 0;
    if (prefs_->getBytesLength(key_.c_str()) == sizeof(saved)) {
        prefs_->getBytes(key_.c_str(), &saved, sizeof(saved));
        if (saved.valid(size_, fingerprint_)) start = saved.offset;
    }
    // A same-size modified file can invalidate an offset beyond the head fingerprint.
    // Align forward to a UTF-8 lead byte before presenting restored content.
    if (start) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        file_.seek(start);
        while (start < size_ && (file_.peek() & 0xC0) == 0x80) { file_.read(); ++start; }
        xSemaphoreGive(mutex_);
    }
    if (start >= size_) start = 0;
    if (!load(start)) { close(); return false; }
    return true;
}
void Reader::save() {
    if (!dirty_ || !file_ || !prefs_) return;
    pocket::Bookmark saved{1, size_, fingerprint_, offset};
    if (prefs_->putBytes(key_.c_str(), &saved, sizeof(saved)) == sizeof(saved)) dirty_ = false;
    else { error = "阅读进度保存失败"; saveAt_ = millis(); }
}
void Reader::close() {
    save();
    if (file_) { xSemaphoreTake(mutex_, portMAX_DELAY); file_.close(); xSemaphoreGive(mutex_); }
    searching_ = false; history_.clear(); page = {}; dirty_ = false;
}
bool Reader::next() {
    if (!file_ || searching_ || next_ >= size_) return false;
    uint32_t old = offset;
    if (!load(next_)) return false;
    if (history_.size() >= 64) history_.erase(history_.begin());
    history_.push_back(old);
    return true;
}
bool Reader::previous() {
    if (!file_ || !offset || searching_) return false;
    if (!history_.empty()) {
        uint32_t previous = history_.back();
        if (!load(previous)) return false;
        history_.pop_back(); return true;
    }
    // After restoring an old bookmark, locate the previous page cooperatively.
    // Only one 2KB window per loop; audio remains owned by its separate task.
    searchAt_ = 0; searchTarget_ = offset; searching_ = true;
    return true;
}
bool Reader::tick() {
    bool changed = false;
    if (searching_) {
        auto candidate = readPage(searchAt_);
        if (!candidate.consumed) { searching_ = false; error = "定位失败，请检查存储卡"; changed = true; }
        else if (searchAt_ + candidate.consumed >= searchTarget_) { searching_ = false; load(searchAt_); changed = true; }
        else searchAt_ += candidate.consumed;
    }
    if (dirty_ && millis() - saveAt_ >= 750) { save(); changed |= error.length() > 0; }
    return changed;
}
void Reader::setFont(unsigned value) {
    font = value == 24 ? 24 : 16;
    history_.clear(); searching_ = false;
    if (file_) load(offset);
}
