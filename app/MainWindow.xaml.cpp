#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "AppState.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::NpuStudio::implementation
{
    void MainWindow::InitializeComponent()
    {
        MainWindowT::InitializeComponent();

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        AppWindow().Resize({ 1280, 860 });
        if (auto presenter = AppWindow().Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>())
        {
            presenter.PreferredMinimumWidth(900);
            presenter.PreferredMinimumHeight(600);
        }

        // Free the engines before the window goes away (OpenVINO dislikes static teardown).
        Closed([](auto&&, auto&&) {
            auto& s = ::NpuStudio::AppState::Get();
            s.text.reset();
            s.image.reset();
        });

        Nav().SelectedItem(Nav().MenuItems().GetAt(0));
        DetectDevices();
    }

    void MainWindow::Nav_SelectionChanged(NavigationView const&, NavigationViewSelectionChangedEventArgs const& args)
    {
        auto item = args.SelectedItem().try_as<NavigationViewItem>();
        if (!item) return;
        auto tag = unbox_value<hstring>(item.Tag());
        Windows::UI::Xaml::Interop::TypeName page;
        if (tag == L"chat") page = xaml_typename<NpuStudio::ChatPage>();
        else if (tag == L"image") page = xaml_typename<NpuStudio::ImagePage>();
        else page = xaml_typename<NpuStudio::ModelsPage>();
        if (ContentFrame().CurrentSourcePageType().Name != page.Name)
            ContentFrame().Navigate(page);
    }

    void MainWindow::TitleBar_PaneToggleRequested(TitleBar const&, IInspectable const&)
    {
        Nav().IsPaneOpen(!Nav().IsPaneOpen());
    }

    fire_and_forget MainWindow::DetectDevices()
    {
        auto strong = get_strong();
        auto dq = DispatcherQueue();
        co_await resume_background();
        std::vector<hfstudio::DeviceInfo> devices;
        try { devices = hfstudio::list_devices(); } catch (...) {}
        co_await wil::resume_foreground(dq);

        ::NpuStudio::AppState::Get().devices = devices;
        std::wstring summary;
        for (auto& d : devices)
        {
            if (d.id == "CPU") continue;  // CPU is always there; don't clutter
            if (!summary.empty()) summary += L"  ·  ";
            summary += std::wstring(to_hstring(d.full_name));
        }
        AppTitleBar().Subtitle(hstring{ summary });
    }
}
