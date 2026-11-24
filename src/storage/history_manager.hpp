#pragma once

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <utility>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <fcitx-utils/log.h>
#include "decay_history.hpp"

namespace mokz {

struct ScopedFileLock {
    explicit ScopedFileLock(const std::string& lockPath)
        : fd_(-1), acquired_(false), counted_(false) {
        if (lockPath.empty()) return;

        try {
            std::filesystem::path p(lockPath);
            if (p.has_parent_path()) {
                std::filesystem::create_directories(p.parent_path());
            }
        } catch (...) {}

        int& depth = lockDepth();
        if (depth == 0) {
            fd_ = ::open(lockPath.c_str(), O_CREAT | O_RDWR, 0644);
            if (fd_ >= 0) {
                if (::flock(fd_, LOCK_EX) == 0) {
                    acquired_ = true;
                } else {
                    ::close(fd_);
                    fd_ = -1;
                }
            }
        }

        if (acquired_ || depth > 0) {
            counted_ = true;
            ++depth;
        }
    }

    ~ScopedFileLock() {
        if (counted_) {
            int& depth = lockDepth();
            if (depth > 0) --depth;
            if (acquired_ && depth == 0 && fd_ >= 0) {
                ::flock(fd_, LOCK_UN);
                ::close(fd_);
            }
        }
    }

    ScopedFileLock(const ScopedFileLock&) = delete;
    ScopedFileLock& operator=(const ScopedFileLock&) = delete;

private:
    static int& lockDepth() {
        static thread_local int depth = 0;
        return depth;
    }

    int fd_;
    bool acquired_;
    bool counted_;
};

class HistoryManager {
public:
    static constexpr size_t MAX_UNI_HISTORY = 1000;
    static constexpr size_t MAX_BI_PER_WORD = 5;
    static constexpr size_t MAX_LOAD_HISTORY_SIZE = 200;

    void initFromHome() {
        const char* home = std::getenv("HOME");
        if (!home || !*home) return;

        historyPath_ = std::string(home) + "/.local/share/fcitx5/mokz/user_history.dat";
        decayHistoryPath_ = historyPath_ + ".decay";

        try {
            if (std::filesystem::exists(historyPath_)) {
                loadHistory();
            }
        } catch (const std::exception& e) {
            FCITX_ERROR() << "[HistoryManager] Failed to load history: " << e.what();
        }

        try {
            if (std::filesystem::exists(decayHistoryPath_)) {
                decayHistory_.load(decayHistoryPath_);
            }
        } catch (const std::exception& e) {
            FCITX_ERROR() << "[HistoryManager] Failed to load decay history: " << e.what();
        }
    }

    void save() {
        if (decayHistoryPath_.empty()) return;
        try {
            std::filesystem::path path(decayHistoryPath_);
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
            decayHistory_.save(decayHistoryPath_);
        } catch (...) {}
    }

    void recordCommit(const std::string& yomi, const std::string& word) {
        if (!DecayHistory::isLearnableWord(yomi, word)) return;

        decayHistory_.recordCommit(yomi, word);
        if (!historyPath_.empty()) {
            appendHistoryRecord(1, yomi, word);
        }

        auto it = std::find_if(commitHistory_.begin(), commitHistory_.end(),
            [&](const std::pair<std::string, std::string>& entry) {
                return entry.first == yomi && entry.second == word;
            });
        if (it != commitHistory_.end()) commitHistory_.erase(it);
        commitHistory_.push_front(std::make_pair(yomi, word));
        while (commitHistory_.size() > MAX_UNI_HISTORY) commitHistory_.pop_back();

        if (!lastCommittedWord_.empty() && lastCommittedWord_.size() <= 24) {
            auto& nextWords = bigramHistory_[lastCommittedWord_];
            if (std::find(nextWords.begin(), nextWords.end(), word) == nextWords.end()) {
                nextWords.push_back(word);
                while (nextWords.size() > MAX_BI_PER_WORD) nextWords.pop_back();
            }
            if (!historyPath_.empty()) {
                appendHistoryRecord(2, lastCommittedWord_, word);
            }
        }

        lastCommittedWord_ = word;
        save();
    }

    void purgeWord(const std::string& targetWord) {
        if (targetWord.empty()) return;
        decayHistory_.removeEntry(targetWord);
        save();

        commitHistory_.erase(
            std::remove_if(commitHistory_.begin(), commitHistory_.end(),
                [&](const auto& item) { return item.second == targetWord; }),
            commitHistory_.end());

        bigramHistory_.erase(targetWord);
        for (auto& [prev, nextList] : bigramHistory_) {
            nextList.erase(std::remove(nextList.begin(), nextList.end(), targetWord), nextList.end());
        }

        rewriteHistoryFile();
    }

    const std::deque<std::pair<std::string, std::string>>& commitHistory() const { return commitHistory_; }
    const std::map<std::string, std::vector<std::string>>& bigramHistory() const { return bigramHistory_; }
    const std::string& lastCommittedWord() const { return lastCommittedWord_; }
    const DecayHistory& decay() const { return decayHistory_; }

private:
    void writeBinaryString(std::ostream& out, const std::string& s) const {
        const auto len = static_cast<std::uint16_t>(
            std::min<std::size_t>(s.size(), static_cast<std::size_t>(65535)));
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        if (len > 0) {
            out.write(s.data(), static_cast<std::streamsize>(len));
        }
    }

    bool readBinaryString(std::istream& in, std::string& s) const {
        std::uint16_t len = 0;
        if (!in.read(reinterpret_cast<char*>(&len), sizeof(len))) return false;
        s.resize(len);
        if (len > 0 && !in.read(&s[0], static_cast<std::streamsize>(len))) return false;
        return true;
    }

    void appendHistoryRecord(std::uint8_t type, const std::string& first, const std::string& second) {
        if (historyPath_.empty()) return;
        try {
            std::filesystem::path path(historyPath_);
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
            std::ofstream out(historyPath_, std::ios::binary | std::ios::app);
            if (!out) return;
            out.write(reinterpret_cast<const char*>(&type), sizeof(type));
            writeBinaryString(out, first);
            writeBinaryString(out, second);
        } catch (...) {}
    }

    void rewriteHistoryFile() {
        if (historyPath_.empty()) return;
        try {
            std::filesystem::path path(historyPath_);
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
            std::string tmpPath = historyPath_ + ".tmp";
            {
                std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
                if (!out) return;
                for (const auto& entry : commitHistory_) {
                    const std::uint8_t type = 1;
                    out.write(reinterpret_cast<const char*>(&type), sizeof(type));
                    writeBinaryString(out, entry.first);
                    writeBinaryString(out, entry.second);
                }
                for (const auto& entry : bigramHistory_) {
                    for (const auto& next : entry.second) {
                        const std::uint8_t type = 2;
                        out.write(reinterpret_cast<const char*>(&type), sizeof(type));
                        writeBinaryString(out, entry.first);
                        writeBinaryString(out, next);
                    }
                }
                out.flush();
            }
            std::error_code ec;
            std::filesystem::rename(tmpPath, historyPath_, ec);
        } catch (...) {}
    }

    void loadHistory() {
        if (historyPath_.empty()) return;
        std::ifstream in(historyPath_, std::ios::binary);
        if (!in) return;

        bool purgedLong = false;
        std::uint8_t type = 0;
        while (in.read(reinterpret_cast<char*>(&type), sizeof(type))) {
            if (type == 1) {
                std::string yomi, word;
                if (!readBinaryString(in, yomi) || !readBinaryString(in, word)) break;
                if (yomi.empty() || word.empty()) continue;

                if (!DecayHistory::isLearnableWord(yomi, word)) {
                    purgedLong = true;
                    continue;
                }

                auto it = std::find_if(commitHistory_.begin(), commitHistory_.end(),
                    [&](const auto& entry) { return entry.first == yomi && entry.second == word; });
                if (it != commitHistory_.end()) commitHistory_.erase(it);
                commitHistory_.push_front(std::make_pair(yomi, word));
            } else if (type == 2) {
                std::string prev, next;
                if (!readBinaryString(in, prev) || !readBinaryString(in, next)) break;
                if (prev.empty() || next.empty()) continue;
                if (prev.size() > 24 || next.size() > 24) {
                    purgedLong = true;
                    continue;
                }

                auto& nextWords = bigramHistory_[prev];
                if (std::find(nextWords.begin(), nextWords.end(), next) == nextWords.end()) {
                    nextWords.push_back(next);
                    while (nextWords.size() > MAX_BI_PER_WORD) nextWords.pop_back();
                }
            } else {
                break;
            }
        }
        in.close();
        while (commitHistory_.size() > MAX_LOAD_HISTORY_SIZE) commitHistory_.pop_back();
        if (purgedLong) {
            rewriteHistoryFile();
        }
    }

    std::deque<std::pair<std::string, std::string>> commitHistory_;
    std::map<std::string, std::vector<std::string>> bigramHistory_;
    std::string lastCommittedWord_;
    std::string historyPath_;
    std::string decayHistoryPath_;
    DecayHistory decayHistory_;
};

} // namespace mokz
