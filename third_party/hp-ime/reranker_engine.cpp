#include "../include/reranker_engine.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace hp_ime {

namespace {

inline size_t align64(size_t off) noexcept {
    return (off + 63) & ~size_t(63);
}

// 高精度 5次 Padé 有理式近似による Tanh / Sigmoid (分岐・std::exp 完全排除)
inline float fast_tanh_scalar(float x) noexcept {
    x = std::clamp(x, -4.97f, 4.97f);
    float x2 = x * x;
    float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2));
    return num / den;
}

inline float fast_sigmoid_scalar(float x) noexcept {
    return 0.5f + 0.5f * fast_tanh_scalar(0.5f * x);
}

// =============================================================================
// 1. Scalar Fallback Kernel (Portable ISO C++20)
// =============================================================================
void gru_step_scalar(
    const int8_t* w_ih, const float* s_ih, const float* b_ih,
    const int8_t* w_hh, const float* s_hh, const float* b_hh,
    const float* x_in, int in_dim, const float* h_prev, float* h_out
) {
    alignas(64) float g_ih[384];
    alignas(64) float g_hh[384];

    for (int r = 0; r < 384; ++r) {
        const int8_t* row_ih = w_ih + r * in_dim;
        float acc_ih = 0.0f;
        for (int c = 0; c < in_dim; ++c) acc_ih += static_cast<float>(row_ih[c]) * x_in[c];
        g_ih[r] = acc_ih * s_ih[r] + b_ih[r];

        const int8_t* row_hh = w_hh + r * 128;
        float acc_hh = 0.0f;
        for (int c = 0; c < 128; ++c) acc_hh += static_cast<float>(row_hh[c]) * h_prev[c];
        g_hh[r] = acc_hh * s_hh[r] + b_hh[r];
    }

    for (int i = 0; i < 128; ++i) {
        float r_gate = fast_sigmoid_scalar(g_ih[i] + g_hh[i]);
        float z_gate = fast_sigmoid_scalar(g_ih[128 + i] + g_hh[128 + i]);
        float n_gate = fast_tanh_scalar(g_ih[256 + i] + r_gate * g_hh[256 + i]);
        h_out[i] = (1.0f - z_gate) * n_gate + z_gate * h_prev[i];
    }
}

void proj_score_scalar(
    const int8_t* w_proj, const float* s_proj, const float* b_proj,
    const float* h_in, float* p_out
) {
    for (int r = 0; r < 64; ++r) {
        const int8_t* row = w_proj + r * 128;
        float acc = 0.0f;
        for (int c = 0; c < 128; ++c) acc += static_cast<float>(row[c]) * h_in[c];
        p_out[r] = fast_tanh_scalar(acc * s_proj[r] + b_proj[r]);
    }
}

#if defined(__x86_64__) || defined(_M_X64)
// =============================================================================
// 2. AVX2 + FMA Kernel (8-wide FP32 FMA + Padé SIMD)
// =============================================================================
__attribute__((target("avx2,fma")))
inline __m256 avx2_tanh(__m256 x) noexcept {
    const __m256 vmin = _mm256_set1_ps(-4.97f);
    const __m256 vmax = _mm256_set1_ps(4.97f);
    x = _mm256_max_ps(vmin, _mm256_min_ps(vmax, x));
    __m256 x2 = _mm256_mul_ps(x, x);

    __m256 num = _mm256_fmadd_ps(x2, _mm256_set1_ps(1.0f), _mm256_set1_ps(378.0f));
    num = _mm256_fmadd_ps(x2, num, _mm256_set1_ps(17325.0f));
    num = _mm256_fmadd_ps(x2, num, _mm256_set1_ps(135135.0f));
    num = _mm256_mul_ps(x, num);

    __m256 den = _mm256_fmadd_ps(x2, _mm256_set1_ps(28.0f), _mm256_set1_ps(3150.0f));
    den = _mm256_fmadd_ps(x2, den, _mm256_set1_ps(62370.0f));
    den = _mm256_fmadd_ps(x2, den, _mm256_set1_ps(135135.0f));

    return _mm256_div_ps(num, den);
}

__attribute__((target("avx2,fma")))
inline __m256 avx2_sigmoid(__m256 x) noexcept {
    const __m256 half = _mm256_set1_ps(0.5f);
    return _mm256_fmadd_ps(half, avx2_tanh(_mm256_mul_ps(half, x)), half);
}

__attribute__((target("avx2,fma")))
inline float hsum256_ps(__m256 v) noexcept {
    __m128 lo = _mm256_castps256_ps128(v);
    __m128 hi = _mm256_extractf128_ps(v, 1);
    lo = _mm_add_ps(lo, hi);
    __m128 shuf = _mm_movehdup_ps(lo);
    __m128 sums = _mm_add_ps(lo, shuf);
    shuf = _mm_movehl_ps(shuf, sums);
    sums = _mm_add_ss(sums, shuf);
    return _mm_cvtss_f32(sums);
}

__attribute__((target("avx2,fma")))
void gru_step_avx2(
    const int8_t* w_ih, const float* s_ih, const float* b_ih,
    const int8_t* w_hh, const float* s_hh, const float* b_hh,
    const float* x_in, int in_dim, const float* h_prev, float* h_out
) {
    alignas(64) float g_ih[384];
    alignas(64) float g_hh[384];

    for (int r = 0; r < 384; ++r) {
        const int8_t* row_ih = w_ih + r * in_dim;
        __m256 acc0 = _mm256_setzero_ps();
        __m256 acc1 = _mm256_setzero_ps();
        for (int c = 0; c < in_dim; c += 16) {
            __m128i qi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row_ih + c));
            __m256i wi0 = _mm256_cvtepi8_epi32(qi);
            __m256i wi1 = _mm256_cvtepi8_epi32(_mm_bsrli_si128(qi, 8));
            acc0 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(wi0), _mm256_loadu_ps(x_in + c), acc0);
            acc1 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(wi1), _mm256_loadu_ps(x_in + c + 8), acc1);
        }
        g_ih[r] = hsum256_ps(_mm256_add_ps(acc0, acc1)) * s_ih[r] + b_ih[r];

        const int8_t* row_hh = w_hh + r * 128;
        __m256 hacc0 = _mm256_setzero_ps();
        __m256 hacc1 = _mm256_setzero_ps();
        for (int c = 0; c < 128; c += 16) {
            __m128i qi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row_hh + c));
            __m256i wi0 = _mm256_cvtepi8_epi32(qi);
            __m256i wi1 = _mm256_cvtepi8_epi32(_mm_bsrli_si128(qi, 8));
            hacc0 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(wi0), _mm256_loadu_ps(h_prev + c), hacc0);
            hacc1 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(wi1), _mm256_loadu_ps(h_prev + c + 8), hacc1);
        }
        g_hh[r] = hsum256_ps(_mm256_add_ps(hacc0, hacc1)) * s_hh[r] + b_hh[r];
    }

    const __m256 one = _mm256_set1_ps(1.0f);
    for (int i = 0; i < 128; i += 8) {
        __m256 ih_r = _mm256_load_ps(g_ih + i);
        __m256 hh_r = _mm256_load_ps(g_hh + i);
        __m256 r_gate = avx2_sigmoid(_mm256_add_ps(ih_r, hh_r));

        __m256 ih_z = _mm256_load_ps(g_ih + 128 + i);
        __m256 hh_z = _mm256_load_ps(g_hh + 128 + i);
        __m256 z_gate = avx2_sigmoid(_mm256_add_ps(ih_z, hh_z));

        __m256 ih_n = _mm256_load_ps(g_ih + 256 + i);
        __m256 hh_n = _mm256_load_ps(g_hh + 256 + i);
        __m256 n_gate = avx2_tanh(_mm256_fmadd_ps(r_gate, hh_n, ih_n));

        __m256 hp = _mm256_loadu_ps(h_prev + i);
        __m256 out = _mm256_fmadd_ps(_mm256_sub_ps(one, z_gate), n_gate, _mm256_mul_ps(z_gate, hp));
        _mm256_storeu_ps(h_out + i, out);
    }
}

__attribute__((target("avx2,fma")))
void proj_score_avx2(
    const int8_t* w_proj, const float* s_proj, const float* b_proj,
    const float* h_in, float* p_out
) {
    alignas(64) float tmp[64];
    for (int r = 0; r < 64; ++r) {
        const int8_t* row = w_proj + r * 128;
        __m256 acc0 = _mm256_setzero_ps();
        __m256 acc1 = _mm256_setzero_ps();
        for (int c = 0; c < 128; c += 16) {
            __m128i qi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + c));
            __m256i wi0 = _mm256_cvtepi8_epi32(qi);
            __m256i wi1 = _mm256_cvtepi8_epi32(_mm_bsrli_si128(qi, 8));
            acc0 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(wi0), _mm256_loadu_ps(h_in + c), acc0);
            acc1 = _mm256_fmadd_ps(_mm256_cvtepi32_ps(wi1), _mm256_loadu_ps(h_in + c + 8), acc1);
        }
        tmp[r] = hsum256_ps(_mm256_add_ps(acc0, acc1)) * s_proj[r] + b_proj[r];
    }
    for (int i = 0; i < 64; i += 8) {
        _mm256_storeu_ps(p_out + i, avx2_tanh(_mm256_load_ps(tmp + i)));
    }
}

// =============================================================================
// 3. AVX-512 Kernel (16-wide FP32)
// =============================================================================
__attribute__((target("avx512f,avx512bw,fma")))
void gru_step_avx512(
    const int8_t* w_ih, const float* s_ih, const float* b_ih,
    const int8_t* w_hh, const float* s_hh, const float* b_hh,
    const float* x_in, int in_dim, const float* h_prev, float* h_out
) {
    alignas(64) float g_ih[384];
    alignas(64) float g_hh[384];

    for (int r = 0; r < 384; ++r) {
        const int8_t* row_ih = w_ih + r * in_dim;
        __m512 acc = _mm512_setzero_ps();
        for (int c = 0; c < in_dim; c += 16) {
            __m128i qi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row_ih + c));
            __m512 wf = _mm512_cvtepi32_ps(_mm512_cvtepi8_epi32(qi));
            acc = _mm512_fmadd_ps(wf, _mm512_loadu_ps(x_in + c), acc);
        }
        g_ih[r] = _mm512_reduce_add_ps(acc) * s_ih[r] + b_ih[r];

        const int8_t* row_hh = w_hh + r * 128;
        __m512 hacc = _mm512_setzero_ps();
        for (int c = 0; c < 128; c += 16) {
            __m128i qi = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row_hh + c));
            __m512 wf = _mm512_cvtepi32_ps(_mm512_cvtepi8_epi32(qi));
            hacc = _mm512_fmadd_ps(wf, _mm512_loadu_ps(h_prev + c), hacc);
        }
        g_hh[r] = _mm512_reduce_add_ps(hacc) * s_hh[r] + b_hh[r];
    }

    for (int i = 0; i < 128; i += 8) {
        __m256 ih_r = _mm256_load_ps(g_ih + i);
        __m256 hh_r = _mm256_load_ps(g_hh + i);
        __m256 r_gate = avx2_sigmoid(_mm256_add_ps(ih_r, hh_r));

        __m256 ih_z = _mm256_load_ps(g_ih + 128 + i);
        __m256 hh_z = _mm256_load_ps(g_hh + 128 + i);
        __m256 z_gate = avx2_sigmoid(_mm256_add_ps(ih_z, hh_z));

        __m256 ih_n = _mm256_load_ps(g_ih + 256 + i);
        __m256 hh_n = _mm256_load_ps(g_hh + 256 + i);
        __m256 n_gate = avx2_tanh(_mm256_fmadd_ps(r_gate, hh_n, ih_n));

        __m256 hp = _mm256_loadu_ps(h_prev + i);
        __m256 one = _mm256_set1_ps(1.0f);
        __m256 out = _mm256_fmadd_ps(_mm256_sub_ps(one, z_gate), n_gate, _mm256_mul_ps(z_gate, hp));
        _mm256_storeu_ps(h_out + i, out);
    }
}
#endif

} // namespace

bool RerankerEngine::init(const char* model_path) {
    fd_ = open(model_path, O_RDONLY);
    if (fd_ < 0) return false;
    struct stat st {};
    if (fstat(fd_, &st) != 0) return false;
    mmap_size_ = st.st_size;
    mmap_base_ = mmap(nullptr, mmap_size_, PROT_READ, MAP_SHARED, fd_, 0);
    if (mmap_base_ == MAP_FAILED) return false;

    const char* base = static_cast<const char*>(mmap_base_);
    hdr_ = reinterpret_cast<const RerankerHeader*>(base);
    if (std::memcmp(hdr_->magic, "MYGRULM1", 8) != 0) return false;

    char_map_ = reinterpret_cast<const CharMapEntry*>(base + hdr_->char_map_offset);

    size_t off = hdr_->weights_offset;
    auto next_block = [&](size_t byte_len) -> const void* {
        const void* ptr = base + off;
        off = align64(off + byte_len);
        return ptr;
    };

    size_t V = hdr_->vocab_size;
    q_emb_  = static_cast<const int8_t*>(next_block(V * 64));
    s_emb_  = static_cast<const float*>(next_block(V * sizeof(float)));

    q_ih0_  = static_cast<const int8_t*>(next_block(384 * 64));
    s_ih0_  = static_cast<const float*>(next_block(384 * sizeof(float)));
    b_ih0_  = static_cast<const float*>(next_block(384 * sizeof(float)));
    q_hh0_  = static_cast<const int8_t*>(next_block(384 * 128));
    s_hh0_  = static_cast<const float*>(next_block(384 * sizeof(float)));
    b_hh0_  = static_cast<const float*>(next_block(384 * sizeof(float)));

    q_ih1_  = static_cast<const int8_t*>(next_block(384 * 128));
    s_ih1_  = static_cast<const float*>(next_block(384 * sizeof(float)));
    b_ih1_  = static_cast<const float*>(next_block(384 * sizeof(float)));
    q_hh1_  = static_cast<const int8_t*>(next_block(384 * 128));
    s_hh1_  = static_cast<const float*>(next_block(384 * sizeof(float)));
    b_hh1_  = static_cast<const float*>(next_block(384 * sizeof(float)));

    q_proj_ = static_cast<const int8_t*>(next_block(64 * 128));
    s_proj_ = static_cast<const float*>(next_block(64 * sizeof(float)));
    b_proj_ = static_cast<const float*>(next_block(64 * sizeof(float)));
    b_char_ = static_cast<const float*>(next_block(V * sizeof(float)));

    // Runtime CPU Dispatch (起動時1回のみ判定)
    gru_step_fn_   = gru_step_scalar;
    proj_score_fn_ = proj_score_scalar;
    backend_name_  = "scalar";

#if defined(__x86_64__) || defined(_M_X64)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw")) {
        gru_step_fn_   = gru_step_avx512;
        proj_score_fn_ = proj_score_avx2;
        backend_name_  = "avx512";
    } else if (__builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma")) {
        gru_step_fn_   = gru_step_avx2;
        proj_score_fn_ = proj_score_avx2;
        backend_name_  = "avx2";
    }
#endif
    return true;
}

uint16_t RerankerEngine::lookupCharId(uint32_t cp) const noexcept {
    const auto* end = char_map_ + hdr_->char_map_count;
    auto it = std::lower_bound(char_map_, end, cp,
        [](const CharMapEntry& e, uint32_t k) { return e.codepoint < k; });
    if (it != end && it->codepoint == cp) return it->token_id;
    return 3; // <UNK>
}

float RerankerEngine::scoreNextToken(const float* p_vec, uint16_t target_id) const noexcept {
    const int8_t* emb_row = q_emb_ + static_cast<size_t>(target_id) * 64;
    float scale = s_emb_[target_id];
    float dot = 0.0f;
    for (int i = 0; i < 64; ++i) {
        dot += static_cast<float>(emb_row[i]) * p_vec[i];
    }
    return dot * scale + b_char_[target_id];
}

void RerankerEngine::scoreCandidatesTrie(
    const std::vector<std::string>& candidates,
    float* out_scores,
    size_t& out_evaluated_nodes
) noexcept {
    out_evaluated_nodes = 0;
    if (candidates.empty()) return;

    // Root Node 0: <BOS> (char_id = 1)
    size_t num_nodes = 1;
    std::memset(trie_arena_[0].h0, 0, sizeof(trie_arena_[0].h0));
    std::memset(trie_arena_[0].h1, 0, sizeof(trie_arena_[0].h1));
    trie_arena_[0].cum_logprob = 0.0f;
    trie_arena_[0].parent_idx  = -1;
    trie_arena_[0].char_id     = 1; // <BOS>
    trie_arena_[0].depth       = 0;

    // Root (<BOS>) の隠れ状態更新と次文字予測ベクトル p_vec キャッシュ
    alignas(64) float p_cache[kMaxTrieNodes][64];
    {
        alignas(64) float x_emb[64];
        const int8_t* erow = q_emb_ + 1 * 64;
        float escale = s_emb_[1];
        for (int i = 0; i < 64; ++i) x_emb[i] = static_cast<float>(erow[i]) * escale;

        alignas(64) float zero_h[128] = {0.0f};
        gru_step_fn_(q_ih0_, s_ih0_, b_ih0_, q_hh0_, s_hh0_, b_hh0_, x_emb, 64, zero_h, trie_arena_[0].h0);
        gru_step_fn_(q_ih1_, s_ih1_, b_ih1_, q_hh1_, s_hh1_, b_hh1_, trie_arena_[0].h0, 128, zero_h, trie_arena_[0].h1);
        proj_score_fn_(q_proj_, s_proj_, b_proj_, trie_arena_[0].h1, p_cache[0]);
        ++out_evaluated_nodes;
    }

    // 各候補文字列を UTF-8 -> Unicode Codepoint -> Char ID 列に変換し、Trie 上を遷移
    for (size_t c_idx = 0; c_idx < candidates.size(); ++c_idx) {
        std::string_view sv = candidates[c_idx];
        int16_t cur_node = 0;

        for (size_t i = 0; i < sv.size();) {
            unsigned char b0 = sv[i];
            uint32_t cp = 0;
            size_t step = 1;
            if ((b0 & 0x80) == 0) {
                cp = b0; step = 1;
            } else if ((b0 & 0xE0) == 0xC0 && i + 1 < sv.size()) {
                cp = ((b0 & 0x1F) << 6) | (sv[i + 1] & 0x3F); step = 2;
            } else if ((b0 & 0xF0) == 0xE0 && i + 2 < sv.size()) {
                cp = ((b0 & 0x0F) << 12) | ((sv[i + 1] & 0x3F) << 6) | (sv[i + 2] & 0x3F); step = 3;
            } else if (i + 3 < sv.size()) {
                cp = ((b0 & 0x07) << 18) | ((sv[i + 1] & 0x3F) << 12) | ((sv[i + 2] & 0x3F) << 6) | (sv[i + 3] & 0x3F); step = 4;
            }
            i += step;

            uint16_t cid = lookupCharId(cp);

            // 既存の共通接頭辞子ノードがあるか検索 (CAVEAT-03: Trie Cache Hit)
            int16_t found_child = -1;
            for (size_t n = 1; n < num_nodes; ++n) {
                if (trie_arena_[n].parent_idx == cur_node && trie_arena_[n].char_id == cid) {
                    found_child = static_cast<int16_t>(n);
                    break;
                }
            }

            if (found_child >= 0) {
                cur_node = found_child;
                continue;
            }

            if (num_nodes >= kMaxTrieNodes) break;

            // 新規 Trie ノード生成 & GRU 1ステップ推論
            int16_t new_idx = static_cast<int16_t>(num_nodes++);
            TrieNode& child = trie_arena_[new_idx];
            const TrieNode& parent = trie_arena_[cur_node];

            child.parent_idx  = cur_node;
            child.char_id     = cid;
            child.depth       = parent.depth + 1;
            child.cum_logprob = parent.cum_logprob + scoreNextToken(p_cache[cur_node], cid);

            alignas(64) float x_emb[64];
            const int8_t* erow = q_emb_ + static_cast<size_t>(cid) * 64;
            float escale = s_emb_[cid];
            for (int k = 0; k < 64; ++k) x_emb[k] = static_cast<float>(erow[k]) * escale;

            gru_step_fn_(q_ih0_, s_ih0_, b_ih0_, q_hh0_, s_hh0_, b_hh0_, x_emb, 64, parent.h0, child.h0);
            gru_step_fn_(q_ih1_, s_ih1_, b_ih1_, q_hh1_, s_hh1_, b_hh1_, child.h0, 128, parent.h1, child.h1);
            proj_score_fn_(q_proj_, s_proj_, b_proj_, child.h1, p_cache[new_idx]);

            cur_node = new_idx;
            ++out_evaluated_nodes;
        }

        // <EOS> (char_id = 2) 遷移スコアを加算し、文字長で緩やかに正規化
        float eos_lp = scoreNextToken(p_cache[cur_node], 2);
        float total_lp = trie_arena_[cur_node].cum_logprob + eos_lp;
        float norm_factor = std::pow(static_cast<float>(trie_arena_[cur_node].depth + 1), 0.7f);
        out_scores[c_idx] = total_lp / norm_factor;
    }
}

void RerankerEngine::rerankTopN(
    std::vector<std::string>& candidates,
    const int32_t* stage1_costs,
    float lm_weight
) noexcept {
    size_t n = candidates.size();
    if (n <= 1) return;

    float lm_scores[16] = {0.0f};
    size_t eval_nodes = 0;
    scoreCandidatesTrie(candidates, lm_scores, eval_nodes);

    struct RankedItem {
        float combined_score; // 大きいほど良い
        size_t orig_idx;
    };
    RankedItem items[16];
    for (size_t i = 0; i < n; ++i) {
        // Stage 1 の順位バイアス（または累積コスト）と GRU-LM の対数尤度を融合
        float s1_prior = stage1_costs ? (-0.001f * stage1_costs[i]) : (-0.35f * static_cast<float>(i));
        items[i] = {s1_prior + lm_weight * lm_scores[i], i};
    }

    std::sort(items, items + n, [](const RankedItem& a, const RankedItem& b) {
        return a.combined_score > b.combined_score;
    });

    std::vector<std::string> sorted;
    sorted.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        sorted.push_back(std::move(candidates[items[i].orig_idx]));
    }
    candidates = std::move(sorted);
}

void RerankerEngine::closeMmap() noexcept {
    if (mmap_base_ && mmap_base_ != MAP_FAILED) munmap(mmap_base_, mmap_size_);
    if (fd_ >= 0) close(fd_);
    mmap_base_ = nullptr;
    fd_ = -1;
}

} // namespace hp_ime
