#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hfstudio {

struct ImageOptions {
    int width = 512;
    int height = 512;
    size_t steps = 0;             // 0 = sensible default for the model (4 for LCM/turbo/schnell, else 25)
    float guidance = -1.0f;       // < 0 = model default
    uint64_t seed = 0;            // 0 = random (the seed used is returned in GeneratedImage)
    std::string negative_prompt;  // ignored by models without CFG (e.g. FLUX schnell, LCM)
};

struct GeneratedImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;     // width * height * 3, row-major
    uint64_t seed = 0;
    size_t steps = 0;
    double seconds = 0;           // generation time (excludes model load)
};

// Called after each denoising step (step is 1-based). Return false to cancel
// (generate() then throws Cancelled).
using StepFn = std::function<bool(size_t step, size_t total)>;

// Text-to-image on one model and device (GPU by default). Not thread-safe.
class ImageEngine {
public:
    // Loads and compiles the diffusion model; compiled blobs are cached in
    // <model_dir>/.ov_cache/<device>.
    explicit ImageEngine(const std::filesystem::path& model_dir, const std::string& device = "GPU");
    ~ImageEngine();
    ImageEngine(const ImageEngine&) = delete;
    ImageEngine& operator=(const ImageEngine&) = delete;

    double load_seconds() const;
    size_t default_steps() const;

    GeneratedImage generate(const std::string& prompt, const ImageOptions& options = {},
                            const StepFn& on_step = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Saves an image as PNG using the Windows Imaging Component.
void save_png(const GeneratedImage& image, const std::filesystem::path& path);

}  // namespace hfstudio
