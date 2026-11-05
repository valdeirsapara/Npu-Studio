#pragma once

#include "ImagePage.g.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace winrt::NpuStudio::implementation
{
    struct ImagePage : ImagePageT<ImagePage>
    {
        ImagePage() {}
        void InitializeComponent();

        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const&);
        void Generate_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Cancel_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OpenFolder_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        fire_and_forget Generate();
        void RefreshModels();
        void RefreshDevices();
        void SetBusy(bool busy);
        std::string SelectedRepo();
        std::string SelectedDevice();

        std::vector<std::string> m_repos;
        bool m_busy = false;
        std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
    };
}

namespace winrt::NpuStudio::factory_implementation
{
    struct ImagePage : ImagePageT<ImagePage, implementation::ImagePage>
    {
    };
}
