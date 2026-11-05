#pragma once

#include "MainWindow.g.h"

namespace winrt::NpuStudio::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow() {}
        void InitializeComponent();

        void Nav_SelectionChanged(Microsoft::UI::Xaml::Controls::NavigationView const&,
                                  Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const&);
        void TitleBar_PaneToggleRequested(Microsoft::UI::Xaml::Controls::TitleBar const&,
                                          Windows::Foundation::IInspectable const&);

    private:
        fire_and_forget DetectDevices();
    };
}

namespace winrt::NpuStudio::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
