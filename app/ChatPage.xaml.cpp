#include "pch.h"
#include "ChatPage.xaml.h"
#if __has_include("ChatPage.g.cpp")
#include "ChatPage.g.cpp"
#endif

#include "AppState.h"
#include "UiHelpers.h"

#include <cstdio>
#include <filesystem>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace Ui = ::NpuStudio::Ui;
using ::NpuStudio::AppState;
using ::NpuStudio::ToH;

namespace
{
    // Hides reasoning blocks (<think>...</think>, e.g. Qwen3) from the displayed answer.
    std::string VisibleText(std::string const& text)
    {
        std::string out;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t open = text.find("<think>", pos);
            if (open == std::string::npos) { out += text.substr(pos); break; }
            out += text.substr(pos, open - pos);
            size_t close = text.find("</think>", open);
            if (close == std::string::npos) return out.find_first_not_of(" \r\n") == std::string::npos ? "Pensando…" : out;
            pos = close + 8;
        }
        size_t first = out.find_first_not_of(" \r\n");
        return first == std::string::npos ? std::string{} : out.substr(first);
    }

    std::wstring Fmt(double v, int decimals)
    {
        wchar_t buf[32];
        swprintf_s(buf, decimals ? L"%.1f" : L"%.0f", v);
        std::wstring s = buf;
        for (auto& c : s) if (c == L'.') c = L',';
        return s;
    }
}

namespace winrt::NpuStudio::implementation
{
    void ChatPage::InitializeComponent()
    {
        ChatPageT::InitializeComponent();
    }

    void ChatPage::OnNavigatedTo(Navigation::NavigationEventArgs const&)
    {
        RefreshModels();
        RefreshDevices();
        UpdateState();
    }

    void ChatPage::RefreshModels()
    {
        auto& s = AppState::Get();
        std::string current = SelectedRepo();
        if (current.empty()) current = s.textRepo;
        m_repos.clear();
        ModelCombo().Items().Clear();
        for (auto const& m : s.models.list())
        {
            if (m.kind != hfstudio::ModelKind::Text) continue;
            m_repos.push_back(m.repo);
            std::wstring label = std::wstring(ToH(m.repo));
            if (m.npu_compatible) label += L"   (NPU)";
            ModelCombo().Items().Append(box_value(hstring{ label }));
        }
        for (size_t i = 0; i < m_repos.size(); ++i)
            if (m_repos[i] == current) ModelCombo().SelectedIndex((int32_t)i);
        if (ModelCombo().SelectedIndex() < 0 && !m_repos.empty()) ModelCombo().SelectedIndex(0);
        EmptyText().Text(m_repos.empty() ? L"Nenhum modelo de texto instalado. Baixe um na tela Modelos."
                                         : L"Escolha um modelo e clique em Carregar.");
    }

    void ChatPage::RefreshDevices()
    {
        auto& s = AppState::Get();
        std::string current = SelectedDevice();
        DeviceCombo().Items().Clear();
        std::vector<std::string> ids;
        for (auto& d : s.devices) ids.push_back(d.id);
        if (ids.empty()) ids = { "NPU", "GPU", "CPU" };  // device detection still running
        for (auto& id : ids) DeviceCombo().Items().Append(box_value(ToH(id)));

        // Default: NPU for NPU-ready models, else GPU.
        if (current.empty())
        {
            current = "GPU";
            int idx = ModelCombo().SelectedIndex();
            if (idx >= 0)
                if (auto m = s.models.find(m_repos[idx]); m && m->npu_compatible) current = "NPU";
        }
        for (uint32_t i = 0; i < ids.size(); ++i)
            if (ids[i] == current) DeviceCombo().SelectedIndex((int32_t)i);
        if (DeviceCombo().SelectedIndex() < 0) DeviceCombo().SelectedIndex(0);
    }

    std::string ChatPage::SelectedRepo()
    {
        int i = ModelCombo().SelectedIndex();
        return i >= 0 && i < (int)m_repos.size() ? m_repos[i] : std::string{};
    }

    std::string ChatPage::SelectedDevice()
    {
        auto item = DeviceCombo().SelectedItem();
        return item ? ::NpuStudio::ToU8(unbox_value<hstring>(item)) : std::string{};
    }

    void ChatPage::Selection_Changed(IInspectable const&, SelectionChangedEventArgs const&)
    {
        UpdateState();
    }

    // Enables/disables controls from the current state.
    void ChatPage::UpdateState()
    {
        auto& s = AppState::Get();
        bool loaded = s.text && s.textRepo == SelectedRepo() && s.textDevice == SelectedDevice();
        bool busy = m_loading || m_generating;

        ModelCombo().IsEnabled(!busy);
        DeviceCombo().IsEnabled(!busy);
        LoadButton().IsEnabled(!busy && !SelectedRepo().empty() && !loaded);
        LoadButton().Content(box_value(s.text && !loaded ? L"Trocar" : L"Carregar"));
        PromptBox().IsEnabled(loaded && !m_loading);
        SendButton().IsEnabled(loaded && !busy);
        SendButton().Visibility(m_generating ? Visibility::Collapsed : Visibility::Visible);
        StopButton().Visibility(m_generating ? Visibility::Visible : Visibility::Collapsed);
        NewChatButton().IsEnabled(loaded && !busy && MessagesPanel().Children().Size() > 0);
        EmptyState().Visibility(MessagesPanel().Children().Size() == 0 ? Visibility::Visible : Visibility::Collapsed);
        if (!busy && s.text && loaded && LoadStatus().Text().empty())
            LoadStatus().Text(hstring{ L"Pronto em " + ToH(s.textDevice) });
        if (loaded && MessagesPanel().Children().Size() == 0)
            EmptyText().Text(L"Modelo pronto. Escreva uma mensagem abaixo.");
    }

    void ChatPage::Load_Click(IInspectable const&, RoutedEventArgs const&) { LoadModel(); }

    fire_and_forget ChatPage::LoadModel()
    {
        auto strong = get_strong();
        auto dq = DispatcherQueue();
        auto& s = AppState::Get();
        std::string repo = SelectedRepo();
        std::string device = SelectedDevice();
        auto model = s.models.find(repo);
        if (!model) co_return;

        ErrorBar().IsOpen(false);
        m_loading = true;
        s.textBusy = true;
        LoadRing().IsActive(true);
        bool firstCompile = !std::filesystem::exists(model->path / ".ov_cache" / device);
        LoadStatus().Text(firstCompile
            ? hstring{ L"Preparando para " + ToH(device) + L" pela primeira vez (pode levar ~1 min)..." }
            : hstring{ L"Carregando..." });
        UpdateState();

        hfstudio::TextEngineOptions opts;
        opts.device = device;
        opts.max_prompt_tokens = 2048;
        opts.max_new_tokens = 512;
        std::string error;
        double seconds = 0;

        co_await resume_background();
        try
        {
            s.text.reset();  // free memory of the previous model first
            s.textRepo.clear();
            auto engine = std::make_unique<hfstudio::TextEngine>(model->path, opts);
            seconds = engine->load_seconds();
            s.text = std::move(engine);
            s.textRepo = repo;
            s.textDevice = device;
        }
        catch (std::exception const& e) { error = e.what(); }
        co_await wil::resume_foreground(dq);

        m_loading = false;
        s.textBusy = false;
        LoadRing().IsActive(false);
        if (!error.empty())
        {
            LoadStatus().Text(L"");
            ShowError(L"Não foi possível carregar o modelo: " + ToH(error));
        }
        else
        {
            LoadStatus().Text(hstring{ L"Pronto em " + ToH(device) + L"  ·  carregou em " + Fmt(seconds, 1) + L" s" });
            MessagesPanel().Children().Clear();
            StatsText().Text(L"");
            PromptBox().Focus(FocusState::Programmatic);
        }
        UpdateState();
    }

    void ChatPage::NewChat_Click(IInspectable const&, RoutedEventArgs const&)
    {
        if (auto& s = AppState::Get(); s.text) s.text->reset();
        MessagesPanel().Children().Clear();
        StatsText().Text(L"");
        UpdateState();
        PromptBox().Focus(FocusState::Programmatic);
    }

    void ChatPage::PromptBox_PreviewKeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& e)
    {
        if (e.Key() != Windows::System::VirtualKey::Enter) return;
        auto shift = Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(Windows::System::VirtualKey::Shift);
        if ((shift & Windows::UI::Core::CoreVirtualKeyStates::Down) == Windows::UI::Core::CoreVirtualKeyStates::Down) return;
        e.Handled(true);
        if (SendButton().IsEnabled()) SendMessage();
    }

    void ChatPage::Send_Click(IInspectable const&, RoutedEventArgs const&) { SendMessage(); }

    void ChatPage::Stop_Click(IInspectable const&, RoutedEventArgs const&) { m_stop->store(true); }

    TextBlock ChatPage::AddBubble(hstring const& text, bool user)
    {
        Border b;
        b.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        b.Padding(ThicknessHelper::FromLengths(14, 10, 14, 10));
        b.MaxWidth(760);
        b.HorizontalAlignment(user ? HorizontalAlignment::Right : HorizontalAlignment::Left);
        b.Background(Ui::Brush(user ? L"AccentFillColorDefaultBrush" : L"CardBackgroundFillColorDefaultBrush"));
        if (!user)
        {
            b.BorderBrush(Ui::Brush(L"CardStrokeColorDefaultBrush"));
            b.BorderThickness(ThicknessHelper::FromUniformLength(1));
        }
        TextBlock t;
        t.Text(text);
        t.TextWrapping(TextWrapping::Wrap);
        t.IsTextSelectionEnabled(true);
        if (user) t.Foreground(Ui::Brush(L"TextOnAccentFillColorPrimaryBrush"));
        b.Child(t);
        MessagesPanel().Children().Append(b);
        return t;
    }

    void ChatPage::ScrollToEnd()
    {
        MessagesScroll().UpdateLayout();
        MessagesScroll().ChangeView(nullptr, MessagesScroll().ScrollableHeight(), nullptr, true);
    }

    void ChatPage::ShowError(hstring const& message)
    {
        ErrorBar().Message(message);
        ErrorBar().IsOpen(true);
    }

    fire_and_forget ChatPage::SendMessage()
    {
        auto strong = get_strong();
        auto dq = DispatcherQueue();
        auto& s = AppState::Get();
        if (!s.text || m_generating) co_return;
        hstring prompt = PromptBox().Text();
        std::wstring_view trimmed = prompt;
        while (!trimmed.empty() && iswspace(trimmed.back())) trimmed.remove_suffix(1);
        while (!trimmed.empty() && iswspace(trimmed.front())) trimmed.remove_prefix(1);
        if (trimmed.empty()) co_return;

        ErrorBar().IsOpen(false);
        AddBubble(hstring{ trimmed }, true);
        PromptBox().Text(L"");
        TextBlock answer = AddBubble(L"…", false);
        ScrollToEnd();
        m_generating = true;
        s.textBusy = true;
        m_stop->store(false);
        UpdateState();

        std::string message = ::NpuStudio::ToU8(hstring{ trimmed });
        auto stop = m_stop;
        auto weak = get_weak();
        // Accumulate on the worker; push the whole text to the UI (cheap, and order-safe).
        auto text = std::make_shared<std::string>();
        hfstudio::GenerationStats stats;
        std::string error;

        co_await resume_background();
        try
        {
            s.text->send(message, [&, text, answer, weak](std::string const& chunk) {
                *text += chunk;
                hstring snapshot = ToH(VisibleText(*text));
                dq.TryEnqueue([weak, answer, snapshot] {
                    if (auto self = weak.get()) { answer.Text(snapshot); self->ScrollToEnd(); }
                });
                return !stop->load();
            }, &stats);
        }
        catch (std::exception const& e) { error = e.what(); }
        co_await wil::resume_foreground(dq);

        m_generating = false;
        s.textBusy = false;
        if (!error.empty())
        {
            answer.Text(L"(sem resposta)");
            ShowError(ToH(error));
        }
        else
        {
            std::string visible = VisibleText(*text);
            answer.Text(visible.empty() ? hstring{ L"(resposta vazia)" } : ToH(visible));
            std::wstring line = Fmt(stats.tokens_per_sec, 1) + L" tok/s  ·  " + std::to_wstring(stats.output_tokens) +
                                L" tokens  ·  1º token em " + Fmt(stats.ttft_ms / 1000.0, 1) + L" s  ·  contexto " +
                                std::to_wstring(stats.input_tokens + stats.output_tokens) + L"/" +
                                std::to_wstring(s.text->options().max_prompt_tokens) + L" tokens  ·  " + ToH(s.textDevice).c_str();
            if (stats.stopped) line += L"  ·  interrompido";
            if (stats.dropped_turns) line += L"  ·  " + std::to_wstring(stats.dropped_turns) + L" troca(s) antiga(s) esquecida(s)";
            StatsText().Text(hstring{ line });
        }
        UpdateState();
        ScrollToEnd();
        PromptBox().Focus(FocusState::Programmatic);
    }
}
