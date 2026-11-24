#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace mokz {

#pragma pack(push, 1)
struct HistoryRecord {
    char yomi[128];
    char word[128];
    uint32_t lastCommitTick;
    uint16_t score;
};
#pragma pack(pop)

class DecayHistory {
public:
    static constexpr uint32_t FILE_MAGIC = 0x44434132;
    static constexpr size_t MAX_CAPACITY = 256;
    static constexpr size_t MAX_WORD_YOMI_BYTES = 24; // 読み8文字(24バイト)以下の単語のみ学習
    static constexpr uint16_t INITIAL_SCORE = 1000;
    static constexpr uint16_t HIT_BONUS = 800;
    static constexpr uint32_t DECAY_INTERVAL_TICKS = 10;

    DecayHistory() : currentTick_(0) {}

    static bool isLearnableWord(std::string_view yomi, std::string_view word) {
        if (yomi.empty() || word.empty()) return false;
        if (yomi.size() > MAX_WORD_YOMI_BYTES || word.size() >= sizeof(HistoryRecord::word)) return false;
        if (word.find("。") != std::string_view::npos || word.find("、") != std::string_view::npos) return false;
        return true;
    }

    void recordCommit(const std::string& yomi, const std::string& word) {
        if (!isLearnableWord(yomi, word)) {
            return;
        }
        currentTick_++;

        for (auto& item : records_) {
            if (std::strcmp(item.yomi, yomi.c_str()) == 0 && std::strcmp(item.word, word.c_str()) == 0) {
                uint32_t diff = currentTick_ - item.lastCommitTick;
                uint32_t shifts = diff / DECAY_INTERVAL_TICKS;
                if (shifts >= 16) item.score = 0;
                else item.score >>= shifts;

                if (static_cast<uint32_t>(item.score) + HIT_BONUS > 65535) item.score = 65535;
                else item.score += HIT_BONUS;

                item.lastCommitTick = currentTick_;
                return;
            }
        }

        HistoryRecord rec;
        std::memset(&rec, 0, sizeof(rec));
        std::strncpy(rec.yomi, yomi.c_str(), sizeof(rec.yomi) - 1);
        std::strncpy(rec.word, word.c_str(), sizeof(rec.word) - 1);
        rec.lastCommitTick = currentTick_;
        rec.score = INITIAL_SCORE;

        if (records_.size() < MAX_CAPACITY) {
            records_.push_back(rec);
        } else {
            auto minIt = std::min_element(records_.begin(), records_.end(),
                [this](const HistoryRecord& a, const HistoryRecord& b) {
                    uint32_t diffA = currentTick_ - a.lastCommitTick;
                    uint16_t scoreA = (diffA / DECAY_INTERVAL_TICKS >= 16) ? 0 : (a.score >> (diffA / DECAY_INTERVAL_TICKS));
                    uint32_t diffB = currentTick_ - b.lastCommitTick;
                    uint16_t scoreB = (diffB / DECAY_INTERVAL_TICKS >= 16) ? 0 : (b.score >> (diffB / DECAY_INTERVAL_TICKS));
                    return scoreA < scoreB;
                });
            if (minIt != records_.end()) {
                *minIt = rec;
            }
        }
    }

    std::vector<std::string> getWordsForExact(const std::string& yomi) const {
        if (yomi.empty()) return {};
        std::vector<std::pair<uint16_t, std::string>> matches;
        for (const auto& item : records_) {
            if (std::strcmp(item.yomi, yomi.c_str()) == 0) {
                uint32_t diff = currentTick_ - item.lastCommitTick;
                uint32_t shifts = diff / DECAY_INTERVAL_TICKS;
                uint16_t currentScore = (shifts >= 16) ? 0 : (item.score >> shifts);
                matches.push_back({currentScore, std::string(item.word)});
            }
        }
        std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
        std::vector<std::string> res;
        for (const auto& m : matches) {
            if (std::find(res.begin(), res.end(), m.second) == res.end()) {
                res.push_back(m.second);
            }
        }
        return res;
    }

    // 前方一致は +4文字(12バイト)以内の短い単語のみを返す（長文ノイズを防止）
    std::vector<std::string> getWordsForPrefix(const std::string& prefix) const {
        if (prefix.empty()) return {};
        std::vector<std::pair<uint16_t, std::string>> matches;
        for (const auto& item : records_) {
            size_t itemYomiLen = std::strlen(item.yomi);
            if (itemYomiLen > prefix.size() && itemYomiLen <= prefix.size() + 12 &&
                std::strncmp(item.yomi, prefix.c_str(), prefix.size()) == 0) {
                uint32_t diff = currentTick_ - item.lastCommitTick;
                uint32_t shifts = diff / DECAY_INTERVAL_TICKS;
                uint16_t currentScore = (shifts >= 16) ? 0 : (item.score >> shifts);
                matches.push_back({currentScore, std::string(item.word)});
            }
        }

        std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });

        std::vector<std::string> res;
        for (const auto& m : matches) {
            if (std::find(res.begin(), res.end(), m.second) == res.end()) {
                res.push_back(m.second);
            }
        }
        return res;
    }

    void removeEntry(const std::string& word) {
        records_.erase(std::remove_if(records_.begin(), records_.end(),
            [&word](const HistoryRecord& r) {
                return std::strcmp(r.word, word.c_str()) == 0;
            }), records_.end());
    }

    void load(const std::string& path) {
        try {
            if (!std::filesystem::exists(path)) return;
            records_.clear();
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs) return;
            uint32_t magic = 0;
            if (!ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic)) || magic != FILE_MAGIC) {
                return;
            }
            ifs.read(reinterpret_cast<char*>(&currentTick_), sizeof(currentTick_));
            uint32_t count = 0;
            ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
            if (count > MAX_CAPACITY) count = MAX_CAPACITY;
            std::vector<HistoryRecord> temp(count);
            ifs.read(reinterpret_cast<char*>(temp.data()), count * sizeof(HistoryRecord));

            // 既に保存されている長文ゴミをロード時に除外してクリーンアップ
            bool purged = false;
            for (const auto& r : temp) {
                if (isLearnableWord(r.yomi, r.word)) {
                    records_.push_back(r);
                } else {
                    purged = true;
                }
            }
            ifs.close();
            if (purged) {
                save(path);
            }
        } catch (...) {
            records_.clear();
        }
    }

    void save(const std::string& path) const {
        try {
            std::string tempPath = path + ".tmp";
            {
                std::ofstream ofs(tempPath, std::ios::binary | std::ios::trunc);
                if (!ofs) return;
                uint32_t magic = FILE_MAGIC;
                ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
                ofs.write(reinterpret_cast<const char*>(&currentTick_), sizeof(currentTick_));
                uint32_t count = static_cast<uint32_t>(records_.size());
                ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));
                ofs.write(reinterpret_cast<const char*>(records_.data()), count * sizeof(HistoryRecord));
                ofs.flush();
            }
            std::error_code ec;
            std::filesystem::rename(tempPath, path, ec);
        } catch (...) {}
    }

private:
    uint32_t currentTick_;
    std::vector<HistoryRecord> records_;
};

} // namespace mokz
