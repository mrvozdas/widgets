#pragma once

#include "App.xaml.g.h"
#include <memory>

namespace winrt::w11widget_native_settings::implementation {

struct WidgetEntry {
    std::wstring type;
    std::wstring label;
    int zone = 0;
    int targetMinutes = 18 * 60;
    int durationMinutes = 25;
    bool enabled = true;
    bool visible = true;
    bool daily = true;
};

struct App : AppT<App> {
    App();
    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

private:
    Microsoft::UI::Xaml::Controls::StackPanel MakePage(wchar_t const* title, wchar_t const* description);
    void ShowWidgets();
    void RefreshWidgets();
    void ShowInspector();
    void MoveWidget(int delta);
    void ShowLayout();
    void ShowAppearance();
    void ApplyTheme();
    void StartTimedClose();

    Microsoft::UI::Xaml::Window window_{nullptr};
    Microsoft::UI::Xaml::Controls::NavigationView navigation_{nullptr};
    Microsoft::UI::Xaml::Controls::ListView widgetList_{nullptr};
    Microsoft::UI::Xaml::Controls::StackPanel inspector_{nullptr};
    Microsoft::UI::Dispatching::DispatcherQueueTimer closeTimer_{nullptr};
    std::vector<std::shared_ptr<WidgetEntry>> widgets_;
    int selected_ = 0;
    int side_ = 0;
    int theme_ = 0;
    double width_ = 330;
    double offset_ = 150;
    double opacity_ = 95;
    bool refreshing_ = false;
};

} // namespace winrt::w11widget_native_settings::implementation
