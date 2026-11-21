#pragma once
#include "reranker_format.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hp_ime {

class RerankerEngine {
public:
    static constexpr size_t kEmbedDim     = 64;
    static constexpr size_t kHiddenDim    = 128;
    static constexpr size_t kGateDim      = 384; // 3 * 128 (r, z, n gates)
    static constexpr size_t kMaxTrieNodes = 384; // Top-5 候補の共通接頭辞Trie上限

    // ゼロヒープ Trie ノード (各ノードが GRU 2層分の隠れ状態を保持)
    struct alignas(64) TrieNode {
        float    h0[kHiddenDim];
        float    h1[kHiddenDim];
        float    cum_logprob;
        int16_t  parent_idx;
        uint16_t char_id;
        uint8_t  depth;
        uint8_t  pad[55];
    };

    using GruStepFn = void (*)(
        const int8_t* w_ih, const float* s_ih, const float* b_ih,
        const int8_t* w_hh, const float* s_hh, const float* b_hh,
        const float* x_in, int in_dim, const float* h_prev, float* h_out
    );

    using ProjScoreFn = void (*)(
        const int8_t* w_proj, const float* s_proj, const float* b_proj,
        const float* h_in, float* p_out
    );

    RerankerEngine() = default;
    ~RerankerEngine() { closeMmap(); }

    bool init(const char* model_path);

    const char* activeBackendName() const noexcept { return backend_name_; }

    // Top-5 候補を共通接頭辞 Trie で一括評価し、各候補の文脈対数尤度スコアを返す (完全ゼロヒープ)
    void scoreCandidatesTrie(
        const std::vector<std::string>& candidates,
        float* out_scores,
        size_t& out_evaluated_nodes
    ) noexcept;

    // Stage 1 の累積コストと Stage 2 (GRU-LM) の対数尤度を統合して Top-5 をリランク
    void rerankTopN(
        std::vector<std::string>& candidates,
        const int32_t* stage1_costs = nullptr,
        float lm_weight = 140.0f
    ) noexcept;

private:
    uint16_t lookupCharId(uint32_t cp) const noexcept;
    float scoreNextToken(const float* p_vec, uint16_t target_id) const noexcept;
    void closeMmap() noexcept;

    int    fd_         = -1;
    void*  mmap_base_  = nullptr;
    size_t mmap_size_  = 0;

    const RerankerHeader* hdr_      = nullptr;
    const CharMapEntry*   char_map_ = nullptr;

    // 64B-aligned pointers into mmap
    const int8_t* q_emb_   = nullptr;
    const float*  s_emb_   = nullptr;

    const int8_t* q_ih0_   = nullptr;
    const float*  s_ih0_   = nullptr;
    const float*  b_ih0_   = nullptr;
    const int8_t* q_hh0_   = nullptr;
    const float*  s_hh0_   = nullptr;
    const float*  b_hh0_   = nullptr;

    const int8_t* q_ih1_   = nullptr;
    const float*  s_ih1_   = nullptr;
    const float*  b_ih1_   = nullptr;
    const int8_t* q_hh1_   = nullptr;
    const float*  s_hh1_   = nullptr;
    const float*  b_hh1_   = nullptr;

    const int8_t* q_proj_  = nullptr;
    const float*  s_proj_  = nullptr;
    const float*  b_proj_  = nullptr;
    const float*  b_char_  = nullptr;

    // Runtime CPU Dispatched Kernels
    GruStepFn   gru_step_fn_   = nullptr;
    ProjScoreFn proj_score_fn_ = nullptr;
    const char* backend_name_  = "uninitialized";

    // Zero-Heap Trie Arena
    alignas(64) TrieNode trie_arena_[kMaxTrieNodes];
};

} // namespace hp_ime
