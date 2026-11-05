#pragma once
// Process-wide state shared by the pages: the model library and the loaded engines.
// Engines are only touched from one background task at a time (each page disables its
// controls while a task runs), so no locking is needed here.

#include <hfstudio/devices.hpp>
#include <hfstudio/image_engine.hpp>
#include <hfstudio/model_manager.hpp>
#include <hfstudio/text_engine.hpp>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace NpuStudio
{
    struct AppState
    {
        static AppState& Get();

        hfstudio::ModelManager models;
        std::vector<hfstudio::DeviceInfo> devices;  // filled once at startup

        std::unique_ptr<hfstudio::TextEngine> text;
        std::string textRepo, textDevice;

        std::unique_ptr<hfstudio::ImageEngine> image;
        std::string imageRepo, imageDevice;

        // True while a background task uses the engine (load/generate). Removing a model
        // whose engine is busy must wait.
        std::atomic<bool> textBusy{ false };
        std::atomic<bool> imageBusy{ false };

        // Where generated images are saved: Pictures\NPU Studio
        std::filesystem::path imagesDir;

    private:
        AppState();
    };

    // UTF-8 <-> hstring
    inline winrt::hstring ToH(std::string const& s) { return winrt::to_hstring(s); }
    inline std::string ToU8(winrt::hstring const& s) { return winrt::to_string(s); }

    std::wstring FormatBytes(uint64_t bytes);
    std::wstring FormatCount(uint64_t n);  // 12345 -> "12,3 mil"
}
