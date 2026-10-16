#pragma once

#include "App.xaml.g.h"
#include "../core/Configuration.h"
#include "../platform/SettingsStore.h"
#include "platform/windows/TaskbarLocator.h"
#include <memory>

namespace winrt::w11widget_native_settings::implementation {

struct App : AppT<App> {
    App();
    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

private:
    Microsoft::UI::Xaml::Controls::StackPanel MakePage(wchar_t const* title, wchar_t const* description);
    static LRESULT CALLBACK WindowMessage(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void SelectWidget(std::wstring const& instanceId);
    void ParseArguments();
    void LoadSettings();
    void SaveSettings();
    void MarkDirty();
    void SetStatus(wchar_t const* title, std::wstring const& message, Microsoft::UI::Xaml::Controls::InfoBarSeverity severity);
    void ShowCurrentPage();
    void ShowWidgets();
    void RefreshWidgets();
    void ShowInspector();
    void MoveWidget(int delta);
    void ShowLayout();
    void UpdatePreview();
    void InvalidateRegion();
    void ShowAppearance();
    void ApplyTheme();
    void StartTimedClose();

    Microsoft::UI::Xaml::Window window_{nullptr};
    Microsoft::UI::Xaml::Controls::NavigationView navigation_{nullptr};
    Microsoft::UI::Xaml::Controls::ListView widgetList_{nullptr};
    Microsoft::UI::Xaml::Controls::StackPanel inspector_{nullptr};
    Microsoft::UI::Xaml::Controls::InfoBar status_{nullptr};
    Microsoft::UI::Xaml::Controls::Button apply_{nullptr};
    Microsoft::UI::Xaml::Controls::CheckBox confirmed_{nullptr};
    Microsoft::UI::Xaml::Controls::Canvas preview_{nullptr};
    Microsoft::UI::Xaml::Controls::TextBlock previewText_{nullptr};
    Microsoft::UI::Dispatching::DispatcherQueueTimer closeTimer_{nullptr};
    std::unique_ptr<w11widget::native::SettingsStore> store_;
    w11widget::native::PanelConfiguration configuration_;
    w11widget::native::SettingsRevision revision_;
    std::vector<std::shared_ptr<w11widget::native::WidgetConfiguration>> widgets_;
    std::optional<w11widget::windows::TaskbarSnapshot> previewSnapshot_;
    std::wstring argumentError_;
    std::wstring initialWidgetId_;
    std::wstring page_ = L"widgets";
    int durationSeconds_ = 0;
    int selected_ = 0;
    bool refreshing_ = false;
    bool dirty_ = false;
    bool loadOk_ = false;
};

} // namespace winrt::w11widget_native_settings::implementation
