#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <charconv>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace minimal_ime {

struct MozcEntry {
    std::string_view yomi;
    uint16_t lid = 0;
    uint16_t rid = 0;
    int32_t cost = 10000;
    std::string_view word;
};

class MozcConnection {
private:
    const int16_t* matrix_ = nullptr;
    uint32_t size_ = 0;
    void* mapped_ = MAP_FAILED;
    size_t mappedBytes_ = 0;

    MozcConnection() {
        init("/usr/share/fcitx5/minimal_addon/connection.bin");
    }

    ~MozcConnection() {
        if (mapped_ != MAP_FAILED && mapped_ != nullptr) {
            munmap(mapped_, mappedBytes_);
        }
    }

public:
    static MozcConnection& instance() {
        static MozcConnection inst;
        return inst;
    }

    bool init(const char* path) {
        if (matrix_ != nullptr) return true;
        int fd = open(path, O_RDONLY);
        if (fd < 0) return false;

        struct stat st;
        if (fstat(fd, &st) < 0 || st.st_size < 4) {
            close(fd);
            return false;
        }

        mappedBytes_ = static_cast<size_t>(st.st_size);
        mapped_ = mmap(nullptr, mappedBytes_, PROT_READ, MAP_SHARED, fd, 0);
        close(fd);

        if (mapped_ == MAP_FAILED) return false;

        size_ = *reinterpret_cast<const uint32_t*>(mapped_);
        matrix_ = reinterpret_cast<const int16_t*>(
            reinterpret_cast<const char*>(mapped_) + sizeof(uint32_t));
        return true;
    }

    inline bool isLoaded() const noexcept {
        return matrix_ != nullptr && size_ > 0;
    }

    inline int32_t getTransitionCost(uint16_t prevRid, uint16_t currLid) const noexcept {
        if (!matrix_ || prevRid >= size_ || currLid >= size_) return 10000;
        return static_cast<int32_t>(matrix_[static_cast<size_t>(prevRid) * size_ + currLid]);
    }
};

// Mozc 5カラム形式 (yomi \t lid \t rid \t cost \t word) ＆ 旧3カラム形式両対応パーサー
inline bool parseMozcKey(std::string_view key, MozcEntry& out) noexcept {
    size_t t1 = key.find('\t');
    if (t1 == std::string_view::npos) return false;
    out.yomi = key.substr(0, t1);

    size_t t2 = key.find('\t', t1 + 1);
    if (t2 == std::string_view::npos) {
        out.word = key.substr(t1 + 1);
        return !out.word.empty();
    }

    size_t t3 = key.find('\t', t2 + 1);
    if (t3 == std::string_view::npos) {
        // 旧3カラム形式: yomi \t cost \t word
        auto col1 = key.substr(t1 + 1, t2 - t1 - 1);
        auto col2 = key.substr(t2 + 1);
        std::from_chars(col1.data(), col1.data() + col1.size(), out.cost);
        out.word = col2;
        return !out.word.empty();
    }

    size_t t4 = key.find('\t', t3 + 1);
    if (t4 == std::string_view::npos) return false;

    // Mozc 5カラム形式: yomi \t lid \t rid \t cost \t word
    auto sLid  = key.substr(t1 + 1, t2 - t1 - 1);
    auto sRid  = key.substr(t2 + 1, t3 - t2 - 1);
    auto sCost = key.substr(t3 + 1, t4 - t3 - 1);
    out.word   = key.substr(t4 + 1);

    std::from_chars(sLid.data(),  sLid.data() + sLid.size(),   out.lid);
    std::from_chars(sRid.data(),  sRid.data() + sRid.size(),   out.rid);
    std::from_chars(sCost.data(), sCost.data() + sCost.size(), out.cost);

    return !out.word.empty();
}

} // namespace minimal_ime
