#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::NpuStudio::implementation
{
    App::App()
    {
        // XAML objects must not call InitializeComponent during construction.
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e) {
            OutputDebugStringW((L"Unhandled: " + e.Message() + L"\n").c_str());
            if (IsDebuggerPresent()) __debugbreak();
        });
    }

    void App::OnLaunched(LaunchActivatedEventArgs const&)
    {
        window = make<MainWindow>();
        window.Activate();
    }
}
