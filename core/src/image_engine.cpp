#include "hfstudio/image_engine.hpp"

#include "hfstudio/http.hpp"  // Cancelled

#include <openvino/genai/image_generation/text2image_pipeline.hpp>

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace hfstudio {

namespace {

// Few-step models (distilled) look best at ~4 steps; regular SD/SDXL need ~25.
size_t guess_default_steps(const fs::path& dir) {
    std::string name = dir.filename().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    for (const char* fast : {"lcm", "turbo", "schnell", "lightning", "hyper"})
        if (name.find(fast) != std::string::npos) return 4;
    return 25;
}

// 32-bit so it is easy to read, type back and store exactly in a UI number box (double).
uint64_t random_seed() {
    std::random_device rd;
    uint64_t s = rd();
    return s ? s : 1;
}

}  // namespace

struct ImageEngine::Impl {
    std::unique_ptr<ov::genai::Text2ImagePipeline> pipe;
    double load_s = 0;
    size_t default_steps = 25;
};

ImageEngine::ImageEngine(const fs::path& model_dir, const std::string& device) : impl_(std::make_unique<Impl>()) {
    if (!fs::exists(model_dir / "model_index.json"))
        throw std::runtime_error("modelo de imagem nao encontrado em " + model_dir.string());
    ov::AnyMap props{{"CACHE_DIR", (model_dir / ".ov_cache" / device).string()}};
    auto t0 = std::chrono::steady_clock::now();
    impl_->pipe = std::make_unique<ov::genai::Text2ImagePipeline>(model_dir, device, props);
    impl_->load_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    impl_->default_steps = guess_default_steps(model_dir);
}

ImageEngine::~ImageEngine() = default;
double ImageEngine::load_seconds() const { return impl_->load_s; }
size_t ImageEngine::default_steps() const { return impl_->default_steps; }

GeneratedImage ImageEngine::generate(const std::string& prompt, const ImageOptions& o, const StepFn& on_step) {
    if (o.width % 8 || o.height % 8 || o.width < 64 || o.height < 64)
        throw std::runtime_error("largura e altura devem ser multiplos de 8 e no minimo 64");

    GeneratedImage out;
    out.seed = o.seed ? o.seed : random_seed();
    out.steps = o.steps ? o.steps : impl_->default_steps;

    ov::AnyMap props{
        {ov::genai::width.name(), (int64_t)o.width},
        {ov::genai::height.name(), (int64_t)o.height},
        {ov::genai::num_inference_steps.name(), out.steps},
        {ov::genai::rng_seed.name(), (size_t)out.seed},
    };
    if (o.guidance >= 0) props[ov::genai::guidance_scale.name()] = o.guidance;
    if (!o.negative_prompt.empty()) props[ov::genai::negative_prompt.name()] = o.negative_prompt;

    bool cancelled = false;
    std::function<bool(size_t, size_t, ov::Tensor&)> cb = [&](size_t step, size_t total, ov::Tensor&) {
        if (on_step && !on_step(step + 1, total)) {
            cancelled = true;
            return true;  // true = interrupt generation
        }
        return false;
    };
    props[ov::genai::callback.name()] = cb;

    auto t0 = std::chrono::steady_clock::now();
    ov::Tensor image = impl_->pipe->generate(prompt, props);
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (cancelled) throw Cancelled();

    const ov::Shape shape = image.get_shape();  // [1, H, W, 3], u8
    if (image.get_element_type() != ov::element::u8 || shape.size() != 4 || shape[3] != 3)
        throw std::runtime_error("formato de imagem inesperado do pipeline");
    out.height = (int)shape[1];
    out.width = (int)shape[2];
    out.rgb.resize((size_t)out.width * out.height * 3);
    std::memcpy(out.rgb.data(), image.data<uint8_t>(), out.rgb.size());
    return out;
}

void save_png(const GeneratedImage& img, const fs::path& path) {
    auto check = [](HRESULT hr, const char* what) {
        if (FAILED(hr)) throw std::runtime_error(std::string("erro ao salvar PNG (") + what + ")");
    };
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool uninit = SUCCEEDED(init);  // RPC_E_CHANGED_MODE: COM already up in another mode, fine

    {
        ComPtr<IWICImagingFactory> factory;
        check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
              "factory");
        ComPtr<IWICStream> stream;
        check(factory->CreateStream(&stream), "stream");
        if (path.has_parent_path()) fs::create_directories(path.parent_path());
        check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "abrir arquivo");
        ComPtr<IWICBitmapEncoder> encoder;
        check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "encoder");
        check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "init");
        ComPtr<IWICBitmapFrameEncode> frame;
        check(encoder->CreateNewFrame(&frame, nullptr), "frame");
        check(frame->Initialize(nullptr), "frame init");
        check(frame->SetSize(img.width, img.height), "size");
        WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
        check(frame->SetPixelFormat(&fmt), "pixel format");
        if (fmt != GUID_WICPixelFormat24bppBGR) throw std::runtime_error("formato de pixel nao suportado");

        // WIC wants BGR; the pipeline gives RGB.
        const UINT stride = (UINT)img.width * 3;
        std::vector<uint8_t> bgr(img.rgb.size());
        for (size_t i = 0; i + 2 < img.rgb.size(); i += 3) {
            bgr[i] = img.rgb[i + 2];
            bgr[i + 1] = img.rgb[i + 1];
            bgr[i + 2] = img.rgb[i];
        }
        check(frame->WritePixels(img.height, stride, (UINT)bgr.size(), bgr.data()), "pixels");
        check(frame->Commit(), "commit frame");
        check(encoder->Commit(), "commit");
    }
    if (uninit) CoUninitialize();
}

}  // namespace hfstudio
