#pragma once

#include "ModelsPage.g.h"

#include <hfstudio/hub_client.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace winrt::NpuStudio::implementation
{
    struct ModelsPage : ModelsPageT<ModelsPage>
    {
        ModelsPage() {}
        void InitializeComponent();

        void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const&);
        void Tabs_SelectionChanged(Microsoft::UI::Xaml::Controls::SelectorBar const&,
                                   Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
        void Search_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SearchBox_KeyDown(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);

    private:
        // Download state lives apart from the widgets, so rows can be rebuilt any time.
        struct Download
        {
            double fraction = 0;
            std::wstring status;
            std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
            bool failed = false;
        };
        // The action area of a row showing a repo, plus the live progress widgets (if downloading).
        struct Row
        {
            Microsoft::UI::Xaml::Controls::StackPanel actions{ nullptr };
            Microsoft::UI::Xaml::Controls::ProgressBar bar{ nullptr };
            Microsoft::UI::Xaml::Controls::TextBlock status{ nullptr };
            bool installedTab = false;
        };

        fire_and_forget RunSearch();
        fire_and_forget StartDownload(std::string repo);
        fire_and_forget RemoveModel(std::string repo);

        void RenderResults();
        void RenderInstalled();
        void RenderActions(std::string const& repo);   // every visible row of this repo
        void RenderRowActions(std::string const& repo, Row& row);
        void UpdateProgress(std::string const& repo);
        Microsoft::UI::Xaml::UIElement MakeRow(std::string const& repo, Microsoft::UI::Xaml::UIElement info, bool installedTab);

        std::vector<hfstudio::ModelSummary> m_results;
        std::map<std::string, Download> m_downloads;
        std::multimap<std::string, Row> m_rows;
        bool m_searched = false;
    };
}

namespace winrt::NpuStudio::factory_implementation
{
    struct ModelsPage : ModelsPageT<ModelsPage, implementation::ModelsPage>
    {
    };
}
