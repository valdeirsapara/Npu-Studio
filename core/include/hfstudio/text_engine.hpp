#pragma once
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace hfstudio {

struct TextEngineOptions {
    std::string device = "NPU";      // "NPU", "GPU" or "CPU"
    // Prompt budget. On NPU this is compiled into the model (MAX_PROMPT_LEN), so it is a
    // hard limit; older turns are dropped to stay under it. Ignored by GPU/CPU except for
    // the same history trimming.
    size_t max_prompt_tokens = 2048;
    size_t max_new_tokens = 512;     // also MIN_RESPONSE_LEN on NPU
    std::string system_prompt;
};

struct GenerationStats {
    size_t input_tokens = 0;
    size_t output_tokens = 0;
    double ttft_ms = 0;         // time to first token
    double tpot_ms = 0;         // time per output token
    double tokens_per_sec = 0;
    size_t dropped_turns = 0;   // old turns removed to fit the context this time
    bool stopped = false;       // generation stopped by the caller
};

// Called for each streamed text chunk. Return false to stop generating
// (the partial answer is kept in the history).
using TokenFn = std::function<bool(const std::string& chunk)>;

// Multi-turn chat on one model and device. Not thread-safe: use from one thread at a time.
class TextEngine {
public:
    // Loads (and on first use compiles) the model. The compiled model is cached in
    // <model_dir>/.ov_cache/<device>, so later loads take seconds instead of a minute.
    TextEngine(const std::filesystem::path& model_dir, TextEngineOptions options = {});
    ~TextEngine();
    TextEngine(const TextEngine&) = delete;
    TextEngine& operator=(const TextEngine&) = delete;

    double load_seconds() const;
    const TextEngineOptions& options() const;

    std::string send(const std::string& user_message, const TokenFn& on_chunk = {},
                     GenerationStats* stats = nullptr);

    void reset();                    // clear the conversation (keeps the system prompt)
    size_t turns() const;            // user+assistant pairs in the history
    size_t history_tokens() const;   // tokens the current history takes in the prompt

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace hfstudio
