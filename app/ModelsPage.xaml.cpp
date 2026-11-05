#include "pch.h"
#include "ModelsPage.xaml.h"
#if __has_include("ModelsPage.g.cpp")
#include "ModelsPage.g.cpp"
#endif

#include "AppState.h"
#include "UiHelpers.h"

#include <hfstudio/http.hpp>

#include <algorithm>
#include <set>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace Ui = ::NpuStudio::Ui;
using ::NpuStudio::AppState;
using ::NpuStudio::ToH;

namespace
{
    // pipeline_tag -> label shown in the UI, and whether the app can run it.
    std::pair<hstring, bool> KindOf(hfstudio::ModelSummary const& m)
    {
        if (m.pipeline_tag == "text-generation") return { L"texto", true };
        if (m.pipeline_tag == "text-to-image") return { L"imagem", true };
        if (m.pipeline_tag.empty()) return { L"tipo não informado", true };
        return { ToH(m.pipeline_tag), false };
    }

    hstring KindLabel(hfstudio::ModelKind k)
    {
        switch (k)
        {
        case hfstudio::ModelKind::Text: return L"texto";
        case hfstudio::ModelKind::Image: return L"imagem";
        default: return L"?";
        }
    }

    StackPanel HStack(double spacing = 8)
    {
        StackPanel s;
        s.Orientation(Orientation::Horizontal);
        s.Spacing(spacing);
        return s;
    }
}

namespace winrt::NpuStudio::implementation
{
    void ModelsPage::InitializeComponent()
    {
        ModelsPageT::InitializeComponent();
    }

    void ModelsPage::OnNavigatedTo(Navigation::NavigationEventArgs const&)
    {
        if (!m_searched)
        {
            m_searched = true;
            RunSearch();  // show the most popular OpenVINO models right away
        }
        RenderInstalled();
        RenderResults();  // "installed" marks may have changed
    }

    void ModelsPage::Tabs_SelectionChanged(SelectorBar const&, SelectorBarSelectionChangedEventArgs const&)
    {
        bool explore = Tabs().SelectedItem() == ExploreTab();
        SearchBar().Visibility(explore ? Visibility::Visible : Visibility::Collapsed);
        ExploreScroll().Visibility(explore ? Visibility::Visible : Visibility::Collapsed);
        InstalledSummary().Visibility(explore ? Visibility::Collapsed : Visibility::Visible);
        InstalledScroll().Visibility(explore ? Visibility::Collapsed : Visibility::Visible);
        if (!explore) RenderInstalled();
    }

    void ModelsPage::Search_Click(IInspectable const&, RoutedEventArgs const&) { RunSearch(); }

    void ModelsPage::SearchBox_KeyDown(IInspectable const&, Input::KeyRoutedEventArgs const& e)
    {
        if (e.Key() == Windows::System::VirtualKey::Enter) { e.Handled(true); RunSearch(); }
    }

    fire_and_forget ModelsPage::RunSearch()
    {
        auto strong = get_strong();
        auto dq = DispatcherQueue();
        std::string query = ::NpuStudio::ToU8(SearchBox().Text());
        std::string author = OfficialOnly().IsChecked().Value() ? "OpenVINO" : "";
        SearchButton().IsEnabled(false);
        SearchRing().IsActive(true);
        ResultsPanel().Children().Clear();
        m_rows.clear();

        std::vector<hfstudio::ModelSummary> results;
        std::string error;
        co_await resume_background();
        try { results = AppState::Get().models.hub().search(query, 40, author); }
        catch (std::exception const& e) { error = e.what(); }
        co_await wil::resume_foreground(dq);

        SearchRing().IsActive(false);
        SearchButton().IsEnabled(true);
        m_results = std::move(results);
        if (!error.empty())
        {
            m_results.clear();
            ResultsPanel().Children().Append(Ui::Text(L"Não foi possível buscar: " + ToH(error)));
            co_return;
        }
        RenderResults();
    }

    UIElement ModelsPage::MakeRow(std::string const& repo, UIElement info, bool installedTab)
    {
        auto card = Ui::Card();
        Grid g;
        g.ColumnSpacing(16);
        g.ColumnDefinitions().Append(ColumnDefinition{});
        ColumnDefinition right;
        right.Width(GridLengthHelper::Auto());
        g.ColumnDefinitions().Append(right);
        g.Children().Append(info);

        StackPanel actions = HStack(8);
        actions.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(actions, 1);
        g.Children().Append(actions);
        card.Child(g);

        Row row;
        row.actions = actions;
        row.installedTab = installedTab;
        auto it = m_rows.emplace(repo, row);
        RenderRowActions(repo, it->second);
        return card;
    }

    void ModelsPage::RenderResults()
    {
        // Drop old explore rows.
        for (auto it = m_rows.begin(); it != m_rows.end();)
            it = it->second.installedTab ? std::next(it) : m_rows.erase(it);
        ResultsPanel().Children().Clear();
        if (m_results.empty())
        {
            if (!SearchRing().IsActive() && m_searched)
                ResultsPanel().Children().Append(Ui::Caption(L"Nenhum modelo encontrado."));
            return;
        }
        for (auto const& m : m_results)
        {
            StackPanel info;
            info.Spacing(4);
            info.Children().Append(Ui::Text(ToH(m.id), L"BodyStrongTextBlockStyle"));
            auto line = HStack(8);
            auto [kind, supported] = KindOf(m);
            line.Children().Append(Ui::Badge(kind, false));
            if (m.likely_npu) line.Children().Append(Ui::Badge(L"NPU", true));
            auto dl = Ui::Caption(hstring{ L"↓ " + ::NpuStudio::FormatCount(m.downloads) + L" downloads" });
            dl.VerticalAlignment(VerticalAlignment::Center);
            line.Children().Append(dl);
            info.Children().Append(line);
            ResultsPanel().Children().Append(MakeRow(m.id, info, false));
        }
    }

    void ModelsPage::RenderInstalled()
    {
        for (auto it = m_rows.begin(); it != m_rows.end();)
            it = it->second.installedTab ? m_rows.erase(it) : std::next(it);
        InstalledPanel().Children().Clear();

        auto& mm = AppState::Get().models;
        auto models = mm.list();
        std::sort(models.begin(), models.end(), [](auto& a, auto& b) { return a.repo < b.repo; });
        uint64_t total = 0;
        for (auto& m : models) total += m.size_bytes;
        InstalledSummary().Text(hstring{ std::to_wstring(models.size()) + L" modelo(s)  ·  " +
                                         ::NpuStudio::FormatBytes(total) + L"  ·  " + mm.root().wstring() });

        for (auto const& m : models)
        {
            StackPanel info;
            info.Spacing(4);
            info.Children().Append(Ui::Text(ToH(m.repo), L"BodyStrongTextBlockStyle"));
            auto line = HStack(8);
            line.Children().Append(Ui::Badge(KindLabel(m.kind), false));
            if (m.npu_compatible) line.Children().Append(Ui::Badge(L"NPU", true));
            std::wstring details = ::NpuStudio::FormatBytes(m.size_bytes);
            if (!m.weight_mode.empty()) details += L"  ·  " + std::wstring(ToH(m.weight_mode));
            if (m.downloaded_at.size() >= 10)  // "YYYY-MM-DD..." -> "DD/MM/YYYY"
                details += L"  ·  baixado em " + std::wstring(ToH(m.downloaded_at.substr(8, 2) + "/" +
                                                                  m.downloaded_at.substr(5, 2) + "/" +
                                                                  m.downloaded_at.substr(0, 4)));
            auto cap = Ui::Caption(hstring{ details });
            cap.VerticalAlignment(VerticalAlignment::Center);
            line.Children().Append(cap);
            info.Children().Append(line);
            InstalledPanel().Children().Append(MakeRow(m.repo, info, true));
        }
        for (auto const& repo : mm.pending_downloads())
        {
            StackPanel info;
            info.Spacing(4);
            info.Children().Append(Ui::Text(ToH(repo), L"BodyStrongTextBlockStyle"));
            info.Children().Append(Ui::Caption(L"Download incompleto"));
            InstalledPanel().Children().Append(MakeRow(repo, info, true));
        }
        if (models.empty() && mm.pending_downloads().empty())
            InstalledPanel().Children().Append(Ui::Caption(L"Nenhum modelo instalado. Use a aba Explorar para baixar."));
    }

    void ModelsPage::RenderActions(std::string const& repo)
    {
        auto range = m_rows.equal_range(repo);
        for (auto it = range.first; it != range.second; ++it) RenderRowActions(repo, it->second);
    }

    void ModelsPage::RenderRowActions(std::string const& repo, Row& row)
    {
        auto panel = row.actions;
        panel.Children().Clear();
        row.bar = nullptr;
        row.status = nullptr;
        auto& mm = AppState::Get().models;

        auto button = [&](hstring text, wchar_t const* glyph, bool accent, auto&& onClick) {
            Button b;
            auto content = HStack(8);
            content.Children().Append(Ui::Icon(glyph, 14));
            TextBlock t;
            t.Text(text);
            content.Children().Append(t);
            b.Content(content);
            Automation::AutomationProperties::SetName(b, text + L" " + ToH(repo));  // screen readers
            if (accent) b.Style(Ui::StyleRes(L"AccentButtonStyle"));
            b.Click(onClick);
            panel.Children().Append(b);
        };
        auto weak = get_weak();

        if (auto it = m_downloads.find(repo); it != m_downloads.end())
        {
            auto& d = it->second;
            if (d.failed)
            {
                auto err = Ui::Caption(hstring{ d.status });
                err.Foreground(Ui::Brush(L"SystemFillColorCriticalBrush"));
                err.MaxWidth(320);
                panel.Children().Append(err);
                button(L"Tentar de novo", L"", false, [weak, repo](auto&&, auto&&) {
                    if (auto self = weak.get()) self->StartDownload(repo);
                });
                return;
            }
            StackPanel col;
            col.Spacing(2);
            ProgressBar bar;
            bar.Width(220);
            bar.Value(d.fraction * 100);
            auto status = Ui::Caption(hstring{ d.status });
            col.Children().Append(bar);
            col.Children().Append(status);
            panel.Children().Append(col);
            row.bar = bar;
            row.status = status;
            auto cancel = d.cancel;
            button(L"Pausar", L"", false, [cancel](auto&&, auto&&) { cancel->store(true); });
            return;
        }

        if (row.installedTab)
        {
            if (mm.find(repo))
            {
                button(L"Remover", L"", false, [weak, repo](auto&&, auto&&) {
                    if (auto self = weak.get()) self->RemoveModel(repo);
                });
            }
            else
            {
                button(L"Continuar", L"", true, [weak, repo](auto&&, auto&&) {
                    if (auto self = weak.get()) self->StartDownload(repo);
                });
                button(L"Descartar", L"", false, [weak, repo](auto&&, auto&&) {
                    if (auto self = weak.get()) self->RemoveModel(repo);
                });
            }
            return;
        }

        // Explore tab
        auto result = std::find_if(m_results.begin(), m_results.end(), [&](auto& r) { return r.id == repo; });
        if (mm.find(repo))
        {
            auto ok = HStack(6);
            auto icon = Ui::Icon(L"", 14);
            icon.Foreground(Ui::Brush(L"SystemFillColorSuccessBrush"));
            ok.Children().Append(icon);
            ok.Children().Append(Ui::Caption(L"Instalado"));
            panel.Children().Append(ok);
        }
        else if (result != m_results.end() && !KindOf(*result).second)
        {
            panel.Children().Append(Ui::Caption(L"Tipo ainda não suportado"));
        }
        else
        {
            bool pending = false;
            for (auto& p : mm.pending_downloads()) pending |= (p == repo);
            button(pending ? L"Continuar" : L"Baixar", L"", true, [weak, repo](auto&&, auto&&) {
                if (auto self = weak.get()) self->StartDownload(repo);
            });
        }
    }

    void ModelsPage::UpdateProgress(std::string const& repo)
    {
        auto it = m_downloads.find(repo);
        if (it == m_downloads.end()) return;
        auto range = m_rows.equal_range(repo);
        for (auto r = range.first; r != range.second; ++r)
        {
            if (!r->second.bar) { RenderRowActions(repo, r->second); continue; }
            r->second.bar.Value(it->second.fraction * 100);
            r->second.status.Text(it->second.status);
        }
    }

    fire_and_forget ModelsPage::StartDownload(std::string repo)
    {
        auto strong = get_strong();
        auto dq = DispatcherQueue();
        m_downloads[repo] = Download{};
        m_downloads[repo].status = L"Preparando...";
        auto cancel = m_downloads[repo].cancel;
        RenderActions(repo);

        auto weak = get_weak();
        std::string error;
        bool cancelled = false;
        co_await resume_background();
        try
        {
            AppState::Get().models.download(repo, [=](hfstudio::DownloadProgress const& p) {
                using S = hfstudio::DownloadProgress::Stage;
                double fraction = p.bytes_total ? double(p.bytes_done) / p.bytes_total : 0;
                std::wstring status;
                if (p.stage == S::Listing) status = L"Listando arquivos...";
                else if (p.stage == S::Verifying) status = L"Verificando " + std::wstring(ToH(p.file)) + L"...";
                else
                {
                    status = ::NpuStudio::FormatBytes(p.bytes_done) + L" de " + ::NpuStudio::FormatBytes(p.bytes_total);
                    if (p.bytes_per_sec > 0) status += L"  ·  " + ::NpuStudio::FormatBytes((uint64_t)p.bytes_per_sec) + L"/s";
                }
                dq.TryEnqueue([weak, repo, fraction, status] {
                    auto self = weak.get();
                    if (!self) return;
                    auto it = self->m_downloads.find(repo);
                    if (it == self->m_downloads.end()) return;
                    it->second.fraction = fraction;
                    it->second.status = status;
                    self->UpdateProgress(repo);
                });
                return !cancel->load();
            });
        }
        catch (hfstudio::Cancelled const&) { cancelled = true; }
        catch (std::exception const& e) { error = e.what(); }
        co_await wil::resume_foreground(dq);

        if (!error.empty())
        {
            m_downloads[repo].failed = true;
            m_downloads[repo].status = std::wstring(ToH(error));
        }
        else
        {
            m_downloads.erase(repo);
        }
        (void)cancelled;  // paused: the row shows "Continuar"
        RenderInstalled();
        RenderActions(repo);
    }

    fire_and_forget ModelsPage::RemoveModel(std::string repo)
    {
        auto strong = get_strong();
        auto dq = DispatcherQueue();
        bool ok = co_await Ui::Confirm(XamlRoot(), L"Remover modelo?",
                                       ToH(repo) + L"\n\nOs arquivos serão apagados do disco.", L"Remover");
        if (!ok) co_return;

        auto& s = AppState::Get();
        if ((s.textRepo == repo && s.textBusy) || (s.imageRepo == repo && s.imageBusy))
        {
            ContentDialog d;
            d.XamlRoot(XamlRoot());
            d.Title(box_value(L"Modelo em uso"));
            d.Content(box_value(L"Espere terminar a resposta ou a imagem em andamento e tente de novo."));
            d.CloseButtonText(L"OK");
            co_await d.ShowAsync();
            co_return;
        }
        // Release the engine first if it holds this model's files open.
        if (s.textRepo == repo) { s.text.reset(); s.textRepo.clear(); }
        if (s.imageRepo == repo) { s.image.reset(); s.imageRepo.clear(); }
        m_downloads.erase(repo);

        std::string error;
        co_await resume_background();
        try { s.models.remove(repo); }
        catch (std::exception const& e) { error = e.what(); }
        co_await wil::resume_foreground(dq);

        if (!error.empty())
        {
            ContentDialog d;
            d.XamlRoot(XamlRoot());
            d.Title(box_value(L"Não foi possível remover"));
            d.Content(box_value(ToH(error)));
            d.CloseButtonText(L"OK");
            co_await d.ShowAsync();
        }
        RenderInstalled();
        RenderActions(repo);
    }
}
