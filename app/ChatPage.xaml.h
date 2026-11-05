#pragma once

#include "ChatPage.g.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace winrt::NpuStudio::implementation
{
    struct ChatPage : ChatPageT<ChatPage>
    {
        ChatPage() {}
        void InitializeComponent();

        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const&);
        void Selection_Changed(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void Load_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void NewChat_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Send_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Stop_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void PromptBox_PreviewKeyDown(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);

    private:
        fire_and_forget LoadModel();
        fire_and_forget SendMessage();
        void RefreshModels();
        void RefreshDevices();
        void UpdateState();
        Microsoft::UI::Xaml::Controls::TextBlock AddBubble(hstring const& text, bool user);
        void ScrollToEnd();
        void ShowError(hstring const& message);
        std::string SelectedRepo();
        std::string SelectedDevice();

        std::vector<std::string> m_repos;  // ModelCombo index -> repo
        bool m_loading = false;
        bool m_generating = false;
        std::shared_ptr<std::atomic<bool>> m_stop = std::make_shared<std::atomic<bool>>(false);
    };
}

namespace winrt::NpuStudio::factory_implementation
{
    struct ChatPage : ChatPageT<ChatPage, implementation::ChatPage>
    {
    };
}
