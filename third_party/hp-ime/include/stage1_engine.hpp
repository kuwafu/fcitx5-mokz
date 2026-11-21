#pragma once
#include "dict_format.hpp"
#include "kenlm4_format.hpp"
#include <marisa.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace hp_ime {

// ゼロヒープ Arena 用探索ノード (32 bytes, キャッシュラインに2個整列)

struct WordSegment {
    std::string yomi;
    std::string surface;
    int start_char;
    int char_length;
    uint16_t left_pos_id;
    uint16_t right_pos_id;
    int32_t cost;
};

struct alignas(32) BeamNode {
    uint32_t token_idx;      // WordToken index (UINT32_MAX = BOS or OOV)
    int32_t  prev_node_idx;  // Arena 内の親ノードインデックス
    int32_t  cum_cost;       // 累積合成コスト
    uint32_t w_hash[3];      // 直近3単語のFNV-1aハッシュ [w_{t-2}, w_{t-1}, w_t]
    uint16_t right_pos_id;   // 右文脈POS ID
    uint8_t  start_char;     // 開始文字インデックス
    uint8_t  end_char;       // 終了文字インデックス
    uint8_t  depth;          // BOSからの単語ステップ数
    uint8_t  pad[3];
};
static_assert(sizeof(BeamNode) == 32, "BeamNode must be 32 bytes for cache efficiency");

class Stage1Engine {
public:
    static inline bool isDigitChar(std::string_view s) noexcept {
        if (s.size() == 1 && s[0] >= '0' && s[0] <= '9') return true;
        if (s.size() == 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
            static_cast<unsigned char>(s[1]) == 0xBC &&
            static_cast<unsigned char>(s[2]) >= 0x90 && static_cast<unsigned char>(s[2]) <= 0x99) return true;
        return false;
    }

public:
    static constexpr size_t  kMaxChars          = 120;
    static constexpr size_t  kMaxArenaNodes     = 8192;
    static constexpr size_t  kPrePruneWidth     = 36;   // Stage 1a: POS粗削り幅
    static constexpr size_t  kBeamWidth         = 20;   // Stage 1b: 4-gram確定ビーム幅
    static constexpr size_t  kMaxTokensPerSlice = 16;   // 同一読みスライスあたり最大展開語数
    static constexpr int32_t kMarginCutoff      = 5500; // 相対マージン足切り閾値

    Stage1Engine() = default;
    ~Stage1Engine() { closeMmap(); }

    bool init(const char* dict_path, const char* kenlm_path) {
        if (!mapFile(dict_path, dict_fd_, dict_base_, dict_size_)) return false;
        if (!mapFile(kenlm_path, lm_fd_, lm_base_, lm_size_)) return false;

        // 1. dict.bin のゼロコピーマッピング
        const char* d_ptr = static_cast<const char*>(dict_base_);
        dict_hdr_ = reinterpret_cast<const DictHeader*>(d_ptr);
        if (std::memcmp(dict_hdr_->magic, "MYIMEDIC", 8) != 0) return false;

        trie_.map(d_ptr + dict_hdr_->marisa_offset, dict_hdr_->marisa_size);
        uint64_t offsets_off = align64(dict_hdr_->marisa_offset + dict_hdr_->marisa_size);
        token_offsets_ = reinterpret_cast<const uint32_t*>(d_ptr + offsets_off);
        tokens_        = reinterpret_cast<const WordToken*>(d_ptr + dict_hdr_->token_table_offset);
        str_pool_      = d_ptr + dict_hdr_->string_pool_offset;
        pos_matrix_    = reinterpret_cast<const int16_t*>(d_ptr + dict_hdr_->pos_matrix_offset);
        pos_dim_       = dict_hdr_->pos_count;

        // 2. kenlm.binary のゼロコピーマッピング
        const char* l_ptr = static_cast<const char*>(lm_base_);
        lm_hdr_ = reinterpret_cast<const KenLM4Header*>(l_ptr);
        if (std::memcmp(lm_hdr_->magic, "MYKENLM4", 8) != 0) return false;

        codebook_ = reinterpret_cast<const QuantCodebook*>(l_ptr + sizeof(KenLM4Header));
        u_table_  = reinterpret_cast<const UnigramEntry*>(l_ptr + lm_hdr_->offsets[0]);
        b_table_  = reinterpret_cast<const BigramEntry*>(l_ptr + lm_hdr_->offsets[1]);
        t_table_  = reinterpret_cast<const TrigramEntry*>(l_ptr + lm_hdr_->offsets[2]);
        f_table_  = reinterpret_cast<const FourgramEntry*>(l_ptr + lm_hdr_->offsets[3]);

        bos_hash_ = hashWord("<s>");
        return true;
    }

    inline int32_t getPosCost(uint16_t rid, uint16_t lid) const noexcept {
        if (rid >= pos_dim_ || lid >= pos_dim_) return 3000;
        return pos_matrix_[static_cast<size_t>(rid) * pos_dim_ + lid];
    }

    // ハイブリッド 4-gram 評価（ヒット次数判定 ＋ バックオフ階層ペナルティ算出）
    inline HybridLMScore evaluateHybrid4Gram(uint32_t w1, uint32_t w2, uint32_t w3, uint32_t w4, uint8_t depth) const noexcept {
        // 1. 4-gram 検索 (文頭から3語以上進んでいる場合のみ)
        if (depth >= 3) {
            uint64_t hi = (uint64_t(w1) << 32) | w2;
            uint64_t lo = (uint64_t(w3) << 32) | w4;
            const auto* end4 = f_table_ + lm_hdr_->counts[3];
            auto it4 = std::lower_bound(f_table_, end4, std::pair<uint64_t, uint64_t>{hi, lo},
                [](const FourgramEntry& e, const std::pair<uint64_t, uint64_t>& k) {
                    return (e.key_hi != k.first) ? (e.key_hi < k.first) : (e.key_lo < k.second);
                });
            if (it4 != end4 && it4->key_hi == hi && it4->key_lo == lo) {
                float lp = codebook_->prob_table[3][it4->prob_q8];
                int32_t adj = static_cast<int32_t>(-380.0f * lp) - 450; // 4-gram 完全一致ボーナス
                return {lp, 4, adj};
            }
        }

        // 2. 3-gram 検索 (文頭から2語以上進んでいる場合のみ)
        if (depth >= 2) {
            uint64_t hi = (uint64_t(w2) << 32) | w3;
            const auto* end3 = t_table_ + lm_hdr_->counts[2];
            auto it3 = std::lower_bound(t_table_, end3, std::pair<uint64_t, uint32_t>{hi, w4},
                [](const TrigramEntry& e, const std::pair<uint64_t, uint32_t>& k) {
                    return (e.key_hi != k.first) ? (e.key_hi < k.first) : (e.w3 < k.second);
                });
            if (it3 != end3 && it3->key_hi == hi && it3->w3 == w4) {
                float lp = codebook_->prob_table[2][it3->prob_q8];
                int32_t adj = static_cast<int32_t>(-340.0f * lp) - 200; // 3-gram 一致ボーナス
                return {lp, 3, adj};
            }
        }

        // 3. 2-gram 検索
        {
            uint64_t key2 = (uint64_t(w3) << 32) | w4;
            const auto* end2 = b_table_ + lm_hdr_->counts[1];
            auto it2 = std::lower_bound(b_table_, end2, key2,
                [](const BigramEntry& e, uint64_t k) { return e.key < k; });
            if (it2 != end2 && it2->key == key2) {
                float lp = codebook_->prob_table[1][it2->prob_q8];
                // 文脈があるのに2-gramへ落ちた場合は軽微なバックオフペナルティ
                int32_t bo_pen = (depth >= 2) ? 120 : 0;
                int32_t adj = static_cast<int32_t>(-300.0f * lp) + bo_pen;
                return {lp, 2, adj};
            }
        }

        // 4. 1-gram 検索 (文脈断絶バックオフペナルティ加算)
        {
            const auto* end1 = u_table_ + lm_hdr_->counts[0];
            auto it1 = std::lower_bound(u_table_, end1, w4,
                [](const UnigramEntry& e, uint32_t k) { return e.w1 < k; });
            if (it1 != end1 && it1->w1 == w4) {
                float lp = codebook_->prob_table[0][it1->prob_q8];
                int32_t bo_pen = (depth >= 2) ? 450 : 200;
                int32_t adj = static_cast<int32_t>(-250.0f * lp) + bo_pen;
                return {lp, 1, adj};
            }
        }

        // 5. UNK (コーパス未観測遷移)
        int32_t unk_pen = (depth >= 2) ? 850 : 400;
        return {-6.5f, 0, unk_pen};
    }

    // Top-N 変換候補を返す（探索中は完全ゼロヒープ）
    
void decodeTopN(std::string_view yomi, size_t top_n, std::vector<std::string>& out_candidates, std::vector<int32_t>* out_costs = nullptr, const std::vector<int>* forced_boundaries = nullptr, std::vector<std::vector<WordSegment>>* out_segments = nullptr) {
        if (out_segments) out_segments->clear();
        if (out_costs) out_costs->clear();
        out_candidates.clear();
        if (yomi.empty()) return;

        // 1. UTF-8文字境界の抽出 (スタック配列)
        uint16_t char_offsets[kMaxChars + 1];
        int n_chars = 0;
        for (size_t i = 0; i < yomi.size() && n_chars < static_cast<int>(kMaxChars);) {
            char_offsets[n_chars++] = static_cast<uint16_t>(i);
            unsigned char c = yomi[i];
            i += ((c & 0x80) == 0) ? 1 : ((c & 0xE0) == 0xC0) ? 2 : ((c & 0xF0) == 0xE0) ? 3 : 4;
        }
        char_offsets[n_chars] = static_cast<uint16_t>(yomi.size());

        // 2. Arena と各文字位置の仮説リングバッファ初期化 (ゼロヒープ)
        arena_size_ = 0;
        uint16_t active_start[kMaxChars + 1] = {0};
        uint16_t active_count[kMaxChars + 1] = {0};

        // 各終了位置ごとの一時候補プール (固定長スタック配列)
        static constexpr size_t kBucketCap = 96;
        BeamNode buckets[kMaxChars + 1][kBucketCap];
        uint16_t bucket_sz[kMaxChars + 1] = {0};

        // BOS ノード配置
        arena_[arena_size_++] = BeamNode{
            UINT32_MAX, -1, 0, {bos_hash_, bos_hash_, bos_hash_}, 0, 0, 0, 0, {0, 0, 0}
        };
        active_start[0] = 0;
        active_count[0] = 1;

        marisa::Agent agent;

        // 3. Time-Synchronous Beam Search ループ
        for (int pos = 0; pos < n_chars; ++pos) {
            // pos > 0 の場合、まず bucket[pos] に対して2段階枝刈り＆KenLM 4-gram評価を実施して確定させる
            if (pos > 0 && bucket_sz[pos] > 0) {
                finalizePositionBeam(buckets[pos], bucket_sz[pos], pos, active_start[pos], active_count[pos]);
            }

            uint16_t b_start = active_start[pos];
            uint16_t b_cnt   = active_count[pos];
            if (b_cnt == 0) continue;

            const char* prefix_ptr = yomi.data() + char_offsets[pos];
            size_t prefix_len      = yomi.size() - char_offsets[pos];

            // --- [A. 任意桁数字の動的注入 (POS: 2044)] ---
            int dig_end = pos;
            while (dig_end < n_chars) {
                std::string_view ch_view(yomi.data() + char_offsets[dig_end], char_offsets[dig_end + 1] - char_offsets[dig_end]);
                if (!isDigitChar(ch_view)) break;
                ++dig_end;
            }
            if (dig_end > pos) {
                int end_char = dig_end;
                bool cross = false;
                if (forced_boundaries) {
                    for (int fb : *forced_boundaries) {
                        if (pos < fb && end_char > fb) { cross = true; break; }
                    }
                }
                if (!cross) {
                    int32_t best_c = 1e9, best_p = -1;
                    for (uint16_t bi = 0; bi < b_cnt; ++bi) {
                        int32_t p_idx = b_start + bi;
                        int32_t c = arena_[p_idx].cum_cost + getPosCost(arena_[p_idx].right_pos_id, 2044) + (3500 + (end_char - pos) * 200);
                        if (c < best_c) { best_c = c; best_p = p_idx; }
                    }
                    if (best_p >= 0) {
                        uint16_t& sz = bucket_sz[end_char];
                        if (sz < kBucketCap) {
                            buckets[end_char][sz++] = BeamNode{
                                UINT32_MAX, best_p, best_c, {0, 0, 0},
                                2044, static_cast<uint8_t>(pos), static_cast<uint8_t>(end_char),
                                static_cast<uint8_t>(arena_[best_p].depth + 1), {0, 0, 0}
                            };
                        }
                    }
                }
            }

            agent.set_query(prefix_ptr, prefix_len);
            bool matched_any = (dig_end > pos);
            while (trie_.common_prefix_search(agent)) {
                size_t match_bytes = agent.key().length();
                int end_char = pos + 1;
                while (end_char <= n_chars && char_offsets[end_char] < char_offsets[pos] + match_bytes) {
                    ++end_char;
                }
                if (end_char > n_chars || char_offsets[end_char] != char_offsets[pos] + match_bytes) continue;
                if (forced_boundaries) {
                    bool cross = false;
                    for (int fb : *forced_boundaries) {
                        if (pos < fb && end_char > fb) { cross = true; break; }
                    }
                    if (cross) continue;
                }
                matched_any = true;

                size_t kid = agent.key().id();
                uint32_t t_start = token_offsets_[kid];
                uint32_t t_total = token_offsets_[kid + 1] - t_start;
                uint32_t t_limit = std::min<uint32_t>(t_total, kMaxTokensPerSlice);

                for (uint32_t ti = 0; ti < t_limit; ++ti) {
                    uint32_t tok_idx = t_start + ti;
                    const WordToken& tok = tokens_[tok_idx];

                    // 同一トークンに対して最も接続が良い上位2個の親仮説を残す（多様性維持）
                    int32_t best_c1 = 1e9, best_c2 = 1e9;
                    int32_t best_p1 = -1,  best_p2 = -1;

                    for (uint16_t bi = 0; bi < b_cnt; ++bi) {
                        int32_t p_idx = b_start + bi;
                        const BeamNode& prev = arena_[p_idx];
                        int32_t c = prev.cum_cost + getPosCost(prev.right_pos_id, tok.left_pos_id) + tok.cost;
                        if (c < best_c1) {
                            best_c2 = best_c1; best_p2 = best_p1;
                            best_c1 = c;       best_p1 = p_idx;
                        } else if (c < best_c2) {
                            best_c2 = c;       best_p2 = p_idx;
                        }
                    }

                    auto push_candidate = [&](int32_t p_idx, int32_t base_cost) {
                        if (p_idx < 0) return;
                        uint16_t& sz = bucket_sz[end_char];
                        if (sz < kBucketCap) {
                            buckets[end_char][sz++] = BeamNode{
                                tok_idx, p_idx, base_cost, {0, 0, 0},
                                tok.right_pos_id, static_cast<uint8_t>(pos), static_cast<uint8_t>(end_char),
                                static_cast<uint8_t>(arena_[p_idx].depth + 1), {0, 0, 0}
                            };
                        } else {
                            // バッファ満杯時は最悪ノードと入れ替え
                            uint16_t worst_i = 0;
                            for (uint16_t k = 1; k < kBucketCap; ++k) {
                                if (buckets[end_char][k].cum_cost > buckets[end_char][worst_i].cum_cost) worst_i = k;
                            }
                            if (base_cost < buckets[end_char][worst_i].cum_cost) {
                                buckets[end_char][worst_i] = BeamNode{
                                    tok_idx, p_idx, base_cost, {0, 0, 0},
                                    tok.right_pos_id, static_cast<uint8_t>(pos), static_cast<uint8_t>(end_char),
                                    static_cast<uint8_t>(arena_[p_idx].depth + 1), {0, 0, 0}
                                };
                            }
                        }
                    };

                    push_candidate(best_p1, best_c1);
                    if (ti < 3 && best_p2 >= 0 && best_c2 < best_c1 + 1200) {
                        push_candidate(best_p2, best_c2);
                    }
                }
            }

            // OOV フォールバック (未知文字でも探索が途切れない保証)
            if (!matched_any && bucket_sz[pos + 1] == 0) {
                int32_t p_idx = b_start;
                bucket_sz[pos + 1] = 1;
                buckets[pos + 1][0] = BeamNode{
                    UINT32_MAX, p_idx, arena_[p_idx].cum_cost + 18000, {0, 0, 0},
                    0, static_cast<uint8_t>(pos), static_cast<uint8_t>(pos + 1),
                    static_cast<uint8_t>(arena_[p_idx].depth + 1), {0, 0, 0}
                };
            }
        }

        // 最終文字位置 (n_chars) のビーム確定
        if (bucket_sz[n_chars] > 0) {
            finalizePositionBeam(buckets[n_chars], bucket_sz[n_chars], n_chars, active_start[n_chars], active_count[n_chars]);
        }

        // 4. EOS 連接評価 ＆ Top-N 文字列パス復元 (重複表記排除つき)
        uint16_t f_start = active_start[n_chars];
        uint16_t f_cnt   = active_count[n_chars];
        if (f_cnt == 0) return;

        struct ScoredPath {
            int32_t total_cost;
            int32_t node_idx;
        };
        ScoredPath finals[kBeamWidth];
        for (uint16_t i = 0; i < f_cnt; ++i) {
            int32_t idx = f_start + i;
            int32_t eos_cost = getPosCost(arena_[idx].right_pos_id, 0);
            finals[i] = {arena_[idx].cum_cost + eos_cost, idx};
        }
        std::sort(finals, finals + f_cnt, [](const ScoredPath& a, const ScoredPath& b) {
            return a.total_cost < b.total_cost;
        });

        std::string_view piece_buf[kMaxChars];
        for (uint16_t i = 0; i < f_cnt && out_candidates.size() < top_n; ++i) {
            int p_len = 0;
            int32_t cur = finals[i].node_idx;
            while (cur > 0 && p_len < static_cast<int>(kMaxChars)) {
                const BeamNode& n = arena_[cur];
                if (n.token_idx == UINT32_MAX) {
                    piece_buf[p_len++] = yomi.substr(char_offsets[n.start_char], char_offsets[n.end_char] - char_offsets[n.start_char]);
                } else {
                    piece_buf[p_len++] = std::string_view(str_pool_ + tokens_[n.token_idx].string_offset);
                }
                cur = n.prev_node_idx;
            }
            std::string candidate;
            candidate.reserve(yomi.size() * 2);
            for (int k = p_len - 1; k >= 0; --k) {
                candidate.append(piece_buf[k]);
            }
            // 品詞ID違いによる同一表記を排除
            if (std::find(out_candidates.begin(), out_candidates.end(), candidate) == out_candidates.end()) {
                out_candidates.push_back(std::move(candidate));
                if (out_costs) out_costs->push_back(finals[i].total_cost);
                if (out_segments) {
                    std::vector<WordSegment> segs;
                    int32_t c_node = finals[i].node_idx;
                    while (c_node > 0) {
                        const BeamNode& bn = arena_[c_node];
                        WordSegment seg;
                        seg.start_char = bn.start_char;
                        seg.char_length = bn.end_char - bn.start_char;
                        seg.yomi = std::string(yomi.substr(char_offsets[bn.start_char], char_offsets[bn.end_char] - char_offsets[bn.start_char]));
                        if (bn.token_idx == UINT32_MAX) {
                            seg.surface = seg.yomi;
                            seg.left_pos_id = bn.right_pos_id;
                            seg.right_pos_id = bn.right_pos_id;
                            seg.cost = 3500;
                        } else {
                            const WordToken& tok = tokens_[bn.token_idx];
                            seg.surface = std::string(str_pool_ + tok.string_offset);
                            seg.left_pos_id = tok.left_pos_id;
                            seg.right_pos_id = tok.right_pos_id;
                            seg.cost = tok.cost;
                        }
                        segs.push_back(std::move(seg));
                        c_node = bn.prev_node_idx;
                    }
                    std::reverse(segs.begin(), segs.end());
                    out_segments->push_back(std::move(segs));
                }
            }
        }
    }


    
    inline void decodeTopNSegments(std::string_view yomi, size_t top_n, std::vector<std::vector<WordSegment>>& out_segments_list, const std::vector<int>* forced_boundaries = nullptr) {
        std::vector<std::string> dummy_cands;
        decodeTopN(yomi, top_n, dummy_cands, nullptr, forced_boundaries, &out_segments_list);
    }

    inline void decodeTopN(std::string_view yomi, size_t top_n, std::vector<std::string>& out_candidates, std::vector<int32_t>& out_costs) {
        decodeTopN(yomi, top_n, out_candidates, &out_costs);
    }

private:
    // 2段階枝刈り: Stage 1a (POS粗削り) -> Stage 1b (Hybrid 4-gram & Margin Cutoff)
    inline void finalizePositionBeam(BeamNode* buf, uint16_t sz, int /*pos*/, uint16_t& out_start, uint16_t& out_cnt) noexcept {
        // Stage 1a: POS + 1-gram コストで上位 kPrePruneWidth に粗削り
        uint16_t pre_k = std::min<uint16_t>(sz, kPrePruneWidth);
        if (sz > pre_k) {
            std::nth_element(buf, buf + pre_k, buf + sz,
                [](const BeamNode& a, const BeamNode& b) { return a.cum_cost < b.cum_cost; });
        }

        // Stage 1b: 精鋭候補のみに KenLM 4-gram スコアとバックオフペナルティを適用
        int32_t best_step_cost = 1e9;
        for (uint16_t i = 0; i < pre_k; ++i) {
            BeamNode& node = buf[i];
            const BeamNode& prev = arena_[node.prev_node_idx];

            uint32_t w4 = 0;
            if (node.token_idx != UINT32_MAX) {
                std::string_view sv(str_pool_ + tokens_[node.token_idx].string_offset);
                w4 = hashWord(sv);
            } else {
                w4 = 1;
            }

            node.w_hash[0] = prev.w_hash[1];
            node.w_hash[1] = prev.w_hash[2];
            node.w_hash[2] = w4;

            HybridLMScore lm = evaluateHybrid4Gram(prev.w_hash[0], prev.w_hash[1], prev.w_hash[2], w4, node.depth);
            node.cum_cost += lm.cost_adjustment;

            if (node.cum_cost < best_step_cost) {
                best_step_cost = node.cum_cost;
            }
        }

        // コスト昇順ソート
        std::sort(buf, buf + pre_k, [](const BeamNode& a, const BeamNode& b) {
            return a.cum_cost < b.cum_cost;
        });

        // 相対マージン足切り (Approach A + B Hybrid) ＆ Arena への書き込み
        out_start = static_cast<uint16_t>(arena_size_);
        out_cnt   = 0;
        for (uint16_t i = 0; i < pre_k && out_cnt < kBeamWidth; ++i) {
            if (i > 0 && (buf[i].cum_cost - best_step_cost) > kMarginCutoff) {
                break; // マージン外に転落したゴミ候補を即座にパージ
            }
            if (arena_size_ < kMaxArenaNodes) {
                arena_[arena_size_++] = buf[i];
                ++out_cnt;
            }
        }
    }

    static bool mapFile(const char* path, int& fd, void*& base, size_t& sz) {
        fd = open(path, O_RDONLY);
        if (fd < 0) return false;
        struct stat st {};
        if (fstat(fd, &st) != 0) return false;
        sz = st.st_size;
        base = mmap(nullptr, sz, PROT_READ, MAP_SHARED, fd, 0);
        if (base == MAP_FAILED) return false;
        madvise(base, sz, MADV_WILLNEED);
        return true;
    }

    void closeMmap() {
        if (dict_base_ && dict_base_ != MAP_FAILED) munmap(dict_base_, dict_size_);
        if (dict_fd_ >= 0) close(dict_fd_);
        if (lm_base_ && lm_base_ != MAP_FAILED) munmap(lm_base_, lm_size_);
        if (lm_fd_ >= 0) close(lm_fd_);
        dict_base_ = lm_base_ = nullptr;
        dict_fd_ = lm_fd_ = -1;
    }

    int    dict_fd_   = -1;
    void*  dict_base_ = nullptr;
    size_t dict_size_ = 0;

    int    lm_fd_     = -1;
    void*  lm_base_   = nullptr;
    size_t lm_size_   = 0;

    const DictHeader*     dict_hdr_      = nullptr;
    marisa::Trie          trie_;
    const uint32_t*       token_offsets_ = nullptr;
    const WordToken*      tokens_        = nullptr;
    const char*           str_pool_      = nullptr;
    const int16_t*        pos_matrix_    = nullptr;
    uint32_t              pos_dim_       = 0;

    const KenLM4Header*   lm_hdr_        = nullptr;
    const QuantCodebook*  codebook_      = nullptr;
    const UnigramEntry*   u_table_       = nullptr;
    const BigramEntry*    b_table_       = nullptr;
    const TrigramEntry*   t_table_       = nullptr;
    const FourgramEntry*  f_table_       = nullptr;
    uint32_t              bos_hash_      = 0;

    alignas(64) BeamNode  arena_[kMaxArenaNodes];
    size_t                arena_size_    = 0;
};

} // namespace hp_ime
