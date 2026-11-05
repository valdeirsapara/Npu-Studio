#include "pch.h"
#include "ImagePage.xaml.h"
#if __has_include("ImagePage.g.cpp")
#include "ImagePage.g.cpp"
#endif

#include "AppState.h"
#include "UiHelpers.h"

#include <hfstudio/http.hpp>

#include <shellapi.h>

#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media::Imaging;
using ::NpuStudio::AppState;
using ::NpuStudio::ToH;

namespace
{
    std::wstring Fmt1(double v)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f", v);
        std::wstring s = buf;
        for (auto& c : s) if (c == L'.') c = L',';
        return s;
    }

    // RGB pixels -> WriteableBitmap (BGRA, premultiplied; alpha is opaque).
    WriteableBitmap ToBitmap(hfstudio::GeneratedImage const& img)
    {
        WriteableBitmap wb(img.width, img.height);
        uint8_t* dst = wb.PixelBuffer().data();
        const uint8_t* src = img.rgb.data();
        for (size_t i = 0, n = (size_t)img.width * img.height; i < n; ++i)
        {
            dst[i * 4 + 0] = src[i * 3 + 2];
            dst[i * 4 + 1] = src[i * 3 + 1];
            dst[i * 4 + 2] = src[i * 3 + 0];
            dst[i * 4 + 3] = 255;
        }
        wb.Invalidate();
        return wb;
    }

    std::wstring Timestamp()
    {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        wchar_t buf[32];
        wcsftime(buf, 32, L"%Y%m%d-%H%M%S", &tm);
        return buf;
    }
}

namespace winrt::NpuStudio::implementation
{
    void ImagePage::InitializeComponent()
    {
        ImagePageT::InitializeComponent();
    }

    void ImagePage::OnNavigatedTo(Navigation::NavigationEventArgs const&)
    {
        RefreshModels();
        RefreshDevices();
        SetBusy(m_busy);
    }

    void ImagePage::RefreshModels()
    {
        auto& s = AppState::Get();
        std::string current = SelectedRepo();
        if (current.empty()) current = s.imageRepo;
        m_repos.clear();
        ModelCombo().Items().Clear();
        for (auto const& m : s.models.list())
        {
            if (m.kind != hfstudio::ModelKind::Image) continue;
            m_repos.push_back(m.repo);
            ModelCombo().Items().Append(box_value(ToH(m.repo)));
        }
        for (size_t i = 0; i < m_repos.size(); ++i)
            if (m_repos[i] == current) ModelCombo().SelectedIndex((int32_t)i);
        if (ModelCombo().SelectedIndex() < 0 && !m_repos.empty()) ModelCombo().SelectedIndex(0);
        if (m_repos.empty())
            EmptyText().Text(L"Nenhum modelo de imagem instalado. Baixe um na tela Modelos (ex.: LCM_Dreamshaper).");
    }

    void ImagePage::RefreshDevices()
    {
        std::string current = SelectedDevice();
        if (current.empty()) current = "GPU";
        DeviceCombo().Items().Clear();
        std::vector<std::string> ids;
        for (auto& d : AppState::Get().devices) ids.push_back(d.id);
        if (ids.empty()) ids = { "GPU", "CPU", "NPU" };
        for (auto& id : ids) DeviceCombo().Items().Append(box_value(ToH(id)));
        for (uint32_t i = 0; i < ids.size(); ++i)
            if (ids[i] == current) DeviceCombo().SelectedIndex((int32_t)i);
        if (DeviceCombo().SelectedIndex() < 0) DeviceCombo().SelectedIndex(0);
    }

    std::string ImagePage::SelectedRepo()
    {
        int i = ModelCombo().SelectedIndex();
        return i >= 0 && i < (int)m_repos.size() ? m_repos[i] : std::string{};
    }

    std::string ImagePage::SelectedDevice()
    {
        auto item = DeviceCombo().SelectedItem();
        return item ? ::NpuStudio::ToU8(unbox_value<hstring>(item)) : std::string{};
    }

    void ImagePage::SetBusy(bool busy)
    {
        m_busy = busy;
        for (Control c : std::initializer_list<Control>{ ModelCombo(), DeviceCombo(), PromptBox(), NegativeBox(),
                                                         SizeCombo(), StepsBox(), SeedBox() })
            c.IsEnabled(!busy);
        GenerateButton().IsEnabled(!busy && !m_repos.empty());
        CancelButton().IsEnabled(busy);
        Progress().Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
    }

    void ImagePage::Generate_Click(IInspectable const&, RoutedEventArgs const&) { Generate(); }
    void ImagePage::Cancel_Click(IInspectable const&, RoutedEventArgs const&) { m_cancel->store(true); }

    void ImagePage::OpenFolder_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto dir = AppState::Get().imagesDir;
        std::filesystem::create_directories(dir);
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    fire_and_forget ImagePage::Generate()
    {
        auto strong = get_strong();
        auto dq = DispatcherQueue();
        auto& s = AppState::Get();
        std::string repo = SelectedRepo();
        std::string device = SelectedDevice();
        std::string prompt = ::NpuStudio::ToU8(PromptBox().Text());
        auto model = s.models.find(repo);
        if (!model) co_return;
        if (prompt.find_first_not_of(" \t\r\n") == std::string::npos)
        {
            StatusText().Text(L"Escreva uma descrição para a imagem.");
            co_return;
        }

        hfstudio::ImageOptions opts;
        static const int sizes[][2] = { {512, 512}, {768, 512}, {512, 768}, {768, 768}, {1024, 1024} };
        int si = std::max(0, SizeCombo().SelectedIndex());
        opts.width = sizes[si][0];
        opts.height = sizes[si][1];
        double steps = StepsBox().Value(), seed = SeedBox().Value();
        opts.steps = std::isnan(steps) ? 0 : (size_t)steps;
        opts.seed = std::isnan(seed) ? 0 : (uint64_t)seed;
        opts.negative_prompt = ::NpuStudio::ToU8(NegativeBox().Text());

        ErrorBar().IsOpen(false);
        m_cancel->store(false);
        SetBusy(true);
        s.imageBusy = true;
        Progress().IsIndeterminate(true);
        bool needLoad = !s.image || s.imageRepo != repo || s.imageDevice != device;
        if (needLoad)
        {
            bool first = !std::filesystem::exists(model->path / ".ov_cache" / device);
            StatusText().Text(first ? hstring{ L"Preparando o modelo para " + ToH(device) + L" pela primeira vez (pode levar ~30 s)..." }
                                    : hstring{ L"Carregando modelo..." });
        }

        auto weak = get_weak();
        auto cancel = m_cancel;
        hfstudio::GeneratedImage img;
        std::string error;
        bool cancelled = false;
        std::filesystem::path saved;

        co_await resume_background();
        try
        {
            if (needLoad)
            {
                s.image.reset();
                s.imageRepo.clear();
                s.image = std::make_unique<hfstudio::ImageEngine>(model->path, device);
                s.imageRepo = repo;
                s.imageDevice = device;
            }
            dq.TryEnqueue([weak] {
                if (auto self = weak.get()) { self->Progress().IsIndeterminate(false); self->StatusText().Text(L"Gerando..."); }
            });
            img = s.image->generate(prompt, opts, [=](size_t step, size_t total) {
                dq.TryEnqueue([weak, step, total] {
                    if (auto self = weak.get())
                    {
                        self->Progress().Value(100.0 * step / total);
                        self->StatusText().Text(hstring{ L"Passo " + std::to_wstring(step) + L" de " + std::to_wstring(total) });
                    }
                });
                return !cancel->load();
            });
            std::filesystem::create_directories(s.imagesDir);
            saved = s.imagesDir / (Timestamp() + L"_" + std::to_wstring(img.seed) + L".png");
            hfstudio::save_png(img, saved);
        }
        catch (hfstudio::Cancelled const&) { cancelled = true; }
        catch (std::exception const& e) { error = e.what(); }
        co_await wil::resume_foreground(dq);

        SetBusy(false);
        s.imageBusy = false;
        Progress().Value(0);
        if (cancelled) { StatusText().Text(L"Cancelado."); co_return; }
        if (!error.empty())
        {
            StatusText().Text(L"");
            ErrorBar().Message(ToH(error));
            ErrorBar().IsOpen(true);
            co_return;
        }

        auto bitmap = ToBitmap(img);
        Preview().Source(bitmap);
        EmptyState().Visibility(Visibility::Collapsed);
        StatusText().Text(hstring{ L"Pronto em " + Fmt1(img.seconds) + L" s" });
        InfoText().Text(hstring{ std::to_wstring(img.width) + L"×" + std::to_wstring(img.height) + L"  ·  " +
                                 std::to_wstring(img.steps) + L" passos  ·  seed " + std::to_wstring(img.seed) + L"  ·  " +
                                 Fmt1(img.seconds) + L" s  ·  " + ToH(device).c_str() + L"  ·  salvo em " +
                                 saved.filename().wstring() });

        // Thumbnail in the history strip; click shows it again.
        Button thumb;
        thumb.Padding(ThicknessHelper::FromUniformLength(2));
        Image ti;
        ti.Source(bitmap);
        ti.Height(72);
        ti.Width(72.0 * img.width / img.height);
        ti.Stretch(Media::Stretch::UniformToFill);
        thumb.Content(ti);
        hstring info = InfoText().Text();
        thumb.Click([weak, bitmap, info](auto&&, auto&&) {
            if (auto self = weak.get()) { self->Preview().Source(bitmap); self->InfoText().Text(info); }
        });
        HistoryPanel().Children().InsertAt(0, thumb);
    }
}
