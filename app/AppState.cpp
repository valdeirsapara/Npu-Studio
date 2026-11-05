#include "pch.h"
#include "AppState.h"

#include <shlobj.h>

#include <cstdio>
#include <cstdlib>

namespace NpuStudio
{
    namespace
    {
        std::string Env(const char* name)
        {
            char* v = nullptr;
            size_t len = 0;
            std::string out;
            if (_dupenv_s(&v, &len, name) == 0 && v) out = v;
            free(v);
            return out;
        }

        std::filesystem::path PicturesDir()
        {
            PWSTR p = nullptr;
            std::filesystem::path out;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &p))) out = p;
            CoTaskMemFree(p);
            return out;
        }
    }

    AppState& AppState::Get()
    {
        static AppState s;
        return s;
    }

    AppState::AppState()
        : models(hfstudio::ModelManager::default_root(), hfstudio::HubClient(Env("HF_TOKEN")))
    {
        imagesDir = PicturesDir() / L"NPU Studio";
    }

    namespace
    {
        // pt-BR decimal comma
        std::wstring Comma(wchar_t const* s)
        {
            std::wstring out = s;
            for (auto& c : out) if (c == L'.') c = L',';
            return out;
        }
    }

    std::wstring FormatBytes(uint64_t bytes)
    {
        wchar_t buf[32];
        double mb = bytes / (1024.0 * 1024.0);
        if (mb >= 1024) swprintf_s(buf, L"%.2f GB", mb / 1024);
        else swprintf_s(buf, L"%.0f MB", mb);
        return Comma(buf);
    }

    std::wstring FormatCount(uint64_t n)
    {
        wchar_t buf[32];
        if (n >= 1000000) swprintf_s(buf, L"%.1f mi", n / 1e6);
        else if (n >= 1000) swprintf_s(buf, L"%.1f mil", n / 1e3);
        else swprintf_s(buf, L"%llu", n);
        return Comma(buf);
    }
}
