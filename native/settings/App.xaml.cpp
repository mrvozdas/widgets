#include "pch.h"
#include "App.xaml.h"
#include "strings/tr-TR.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation;
namespace text = native_settings::text;

namespace {

TextBlock Paragraph(wchar_t const* value, double size = 14) {
    TextBlock result;
    result.Text(value);
    result.FontSize(size);
    result.TextWrapping(TextWrapping::Wrap);
    return result;
}

Button Action(wchar_t const* label) {
    Button result;
    result.Content(box_value(label));
    return result;
}

ComboBox Choices(wchar_t const* header, std::initializer_list<wchar_t const*> values, int selected) {
    ComboBox result;
    result.Header(box_value(header));
    result.HorizontalAlignment(HorizontalAlignment::Stretch);
    for (auto value : values) result.Items().Append(box_value(value));
    result.SelectedIndex(selected);
    return result;
}

NumberBox Number(wchar_t const* header, double value, double minimum, double maximum) {
    NumberBox result;
    result.Header(box_value(header));
    result.Minimum(minimum);
    result.Maximum(maximum);
    result.Value(value);
    result.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
    return result;
}

NavigationViewItem NavigationItem(wchar_t const* label, wchar_t const* tag, Symbol icon) {
    NavigationViewItem result;
    result.Content(box_value(label));
    result.Tag(box_value(tag));
    result.Icon(SymbolIcon(icon));
    return result;
}

} // namespace

namespace winrt::w11widget_native_settings::implementation {

App::App() {
    InitializeComponent();
    widgets_ = {
        std::make_shared<WidgetEntry>(WidgetEntry{L"countdown", text::countdown}),
        std::make_shared<WidgetEntry>(WidgetEntry{L"world-clock", text::utc, 0}),
        std::make_shared<WidgetEntry>(WidgetEntry{L"world-clock", text::newYork, 1}),
        std::make_shared<WidgetEntry>(WidgetEntry{L"world-clock", text::istanbul, 2})
    };
}

void App::OnLaunched(LaunchActivatedEventArgs const&) {
    window_ = Window();
    window_.Title(text::title);
    window_.AppWindow().Resize({1060, 760});
    window_.SystemBackdrop(MicaBackdrop());
    navigation_ = NavigationView();
    navigation_.PaneTitle(text::product);
    navigation_.IsSettingsVisible(false);
    navigation_.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
    navigation_.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
    navigation_.OpenPaneLength(220);
    navigation_.MenuItems().Append(NavigationItem(text::widgets, L"widgets", Symbol::AllApps));
    navigation_.MenuItems().Append(NavigationItem(text::layout, L"layout", Symbol::View));
    navigation_.MenuItems().Append(NavigationItem(text::appearance, L"appearance", Symbol::Setting));
    navigation_.SelectionChanged([this](auto const&, NavigationViewSelectionChangedEventArgs const& args) {
        const auto item = args.SelectedItemContainer();
        if (!item) return;
        const auto page = unbox_value<hstring>(item.Tag());
        if (page == L"layout") ShowLayout();
        else if (page == L"appearance") ShowAppearance();
        else ShowWidgets();
    });
    navigation_.SelectedItem(navigation_.MenuItems().GetAt(0));
    window_.Content(navigation_);
    ApplyTheme();
    window_.Closed([this](auto const&, auto const&) {
        if (closeTimer_) closeTimer_.Stop();
        Application::Current().Exit();
    });
    window_.Activate();
    StartTimedClose();
}

StackPanel App::MakePage(wchar_t const* title, wchar_t const* description) {
    widgetList_ = nullptr;
    inspector_ = nullptr;
    navigation_.Header(box_value(title));
    StackPanel page;
    page.Spacing(20);
    page.Margin({28, 12, 28, 28});
    InfoBar notice;
    notice.IsOpen(true);
    notice.IsClosable(false);
    notice.Severity(InfoBarSeverity::Informational);
    notice.Title(text::prototypeTitle);
    notice.Message(text::prototypeMessage);
    page.Children().Append(notice);
    page.Children().Append(Paragraph(description));
    ScrollViewer scroll;
    scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scroll.Content(page);
    navigation_.Content(scroll);
    return page;
}

void App::ShowWidgets() {
    auto page = MakePage(text::widgets, text::widgetsIntro);
    Grid columns;
    ColumnDefinition listColumn;
    listColumn.Width({1, GridUnitType::Star});
    ColumnDefinition detailColumn;
    detailColumn.Width({1.15, GridUnitType::Star});
    columns.ColumnDefinitions().Append(listColumn);
    columns.ColumnDefinitions().Append(detailColumn);
    StackPanel listPane;
    listPane.Spacing(12);
    listPane.Margin({0, 0, 24, 0});
    auto catalog = Choices(text::addWidget, {text::worldClock, text::countdown, text::focus}, 0);
    listPane.Children().Append(catalog);
    auto add = Action(text::addWidget);
    add.Click([this, catalog](auto const&, auto const&) {
        if (widgets_.size() >= 16) return;
        auto entry = std::make_shared<WidgetEntry>();
        switch (catalog.SelectedIndex()) {
        case 1: entry->type = L"countdown"; entry->label = text::countdown; break;
        case 2: entry->type = L"focus-timer"; entry->label = text::focusLabel; break;
        default: entry->type = L"world-clock"; entry->label = text::utc; break;
        }
        widgets_.push_back(entry);
        selected_ = static_cast<int>(widgets_.size()) - 1;
        RefreshWidgets();
    });
    listPane.Children().Append(add);
    widgetList_ = ListView();
    widgetList_.MinHeight(260);
    widgetList_.MaxHeight(380);
    widgetList_.SelectionMode(ListViewSelectionMode::Single);
    Automation::AutomationProperties::SetName(widgetList_, text::widgets);
    widgetList_.SelectionChanged([this](auto const&, auto const&) {
        if (refreshing_) return;
        selected_ = widgetList_.SelectedIndex();
        ShowInspector();
    });
    listPane.Children().Append(widgetList_);
    columns.Children().Append(listPane);
    inspector_ = StackPanel();
    inspector_.Spacing(14);
    Grid::SetColumn(inspector_, 1);
    columns.Children().Append(inspector_);
    page.Children().Append(columns);
    RefreshWidgets();
}

void App::RefreshWidgets() {
    if (!widgetList_ || !inspector_) return;
    refreshing_ = true;
    widgetList_.Items().Clear();
    for (const auto& entry : widgets_) {
        // Distinct containers keep equal labels from sharing UI Automation peers.
        // https://github.com/microsoft/microsoft-ui-xaml/issues/11226
        ListViewItem row;
        row.Content(box_value(hstring(entry->label)));
        Automation::AutomationProperties::SetName(row, entry->label);
        widgetList_.Items().Append(row);
    }
    selected_ = widgets_.empty() ? -1 : std::clamp(selected_, 0, static_cast<int>(widgets_.size()) - 1);
    widgetList_.SelectedIndex(selected_);
    refreshing_ = false;
    ShowInspector();
}

void App::ShowInspector() {
    inspector_.Children().Clear();
    if (selected_ < 0 || selected_ >= static_cast<int>(widgets_.size())) {
        inspector_.Children().Append(Paragraph(text::empty));
        return;
    }
    const auto entry = widgets_[selected_];
    TextBox label;
    label.Header(box_value(text::displayName));
    label.MaxLength(40);
    label.Text(entry->label);
    label.LostFocus([this, entry](IInspectable const& sender, auto const&) {
        const auto updated = std::wstring(sender.as<TextBox>().Text());
        if (entry->label != updated && !updated.empty()) {
            entry->label = updated;
            const auto position = std::find(widgets_.begin(), widgets_.end(), entry);
            if (widgetList_ && position != widgets_.end()) {
                const auto index = static_cast<uint32_t>(position - widgets_.begin());
                auto items = widgetList_.Items();
                if (index < items.Size()) {
                    // Losing focus can occur while the inspector is being removed.
                    // Update only the existing row to avoid rebuilding that inspector.
                    const auto row = items.GetAt(index).as<ListViewItem>();
                    row.Content(box_value(hstring(entry->label)));
                    Automation::AutomationProperties::SetName(row, entry->label);
                }
            }
        }
    });
    inspector_.Children().Append(label);
    ToggleSwitch enabled;
    enabled.Header(box_value(text::enabled));
    enabled.OnContent(box_value(text::switchOn));
    enabled.OffContent(box_value(text::switchOff));
    enabled.IsOn(entry->enabled);
    enabled.Toggled([entry](IInspectable const& sender, auto const&) { entry->enabled = sender.as<ToggleSwitch>().IsOn(); });
    inspector_.Children().Append(enabled);
    ToggleSwitch visible;
    visible.Header(box_value(text::visible));
    visible.OnContent(box_value(text::switchOn));
    visible.OffContent(box_value(text::switchOff));
    visible.IsOn(entry->visible);
    visible.Toggled([entry](IInspectable const& sender, auto const&) { entry->visible = sender.as<ToggleSwitch>().IsOn(); });
    inspector_.Children().Append(visible);
    if (entry->type == L"world-clock") {
        auto zone = Choices(text::timezone, {L"UTC", L"America/New_York", L"Europe/Istanbul"}, entry->zone);
        zone.SelectionChanged([entry](IInspectable const& sender, auto const&) { entry->zone = sender.as<ComboBox>().SelectedIndex(); });
        inspector_.Children().Append(zone);
    } else if (entry->type == L"countdown") {
        TimePicker target;
        target.Header(box_value(text::target));
        target.ClockIdentifier(L"24HourClock");
        target.Time(std::chrono::minutes(entry->targetMinutes));
        target.TimeChanged([entry](auto const&, TimePickerValueChangedEventArgs const& args) {
            entry->targetMinutes = static_cast<int>(std::chrono::duration_cast<std::chrono::minutes>(args.NewTime()).count());
        });
        inspector_.Children().Append(target);
        CheckBox daily;
        daily.Content(box_value(text::daily));
        daily.IsChecked(entry->daily);
        daily.Checked([entry](auto const&, auto const&) { entry->daily = true; });
        daily.Unchecked([entry](auto const&, auto const&) { entry->daily = false; });
        inspector_.Children().Append(daily);
    } else {
        auto duration = Number(text::duration, entry->durationMinutes, 1, 180);
        duration.ValueChanged([entry](auto const&, NumberBoxValueChangedEventArgs const& args) {
            if (std::isfinite(args.NewValue())) entry->durationMinutes = static_cast<int>(std::round(args.NewValue()));
        });
        inspector_.Children().Append(duration);
    }
    StackPanel order;
    order.Orientation(Orientation::Horizontal);
    order.Spacing(8);
    auto up = Action(text::moveUp);
    up.IsEnabled(selected_ > 0);
    up.Click([this](auto const&, auto const&) { MoveWidget(-1); });
    auto down = Action(text::moveDown);
    down.IsEnabled(selected_ + 1 < static_cast<int>(widgets_.size()));
    down.Click([this](auto const&, auto const&) { MoveWidget(1); });
    order.Children().Append(up);
    order.Children().Append(down);
    inspector_.Children().Append(order);
    auto remove = Action(text::remove);
    remove.Click([this](auto const&, auto const&) {
        if (selected_ < 0 || selected_ >= static_cast<int>(widgets_.size())) return;
        widgets_.erase(widgets_.begin() + selected_);
        RefreshWidgets();
    });
    inspector_.Children().Append(remove);
}

void App::MoveWidget(int delta) {
    const auto destination = selected_ + delta;
    if (selected_ < 0 || destination < 0 || destination >= static_cast<int>(widgets_.size())) return;
    std::swap(widgets_[selected_], widgets_[destination]);
    selected_ = destination;
    RefreshWidgets();
}

void App::ShowLayout() {
    auto page = MakePage(text::layout, text::layoutIntro);
    auto monitor = Choices(text::monitor, {text::primaryMonitor}, 0);
    page.Children().Append(monitor);
    auto side = Choices(text::align, {text::left, text::right}, side_);
    side.SelectionChanged([this](IInspectable const& sender, auto const&) { side_ = sender.as<ComboBox>().SelectedIndex(); });
    page.Children().Append(side);
    auto width = Number(text::width, width_, 80, 640);
    width.ValueChanged([this](auto const&, NumberBoxValueChangedEventArgs const& args) {
        if (std::isfinite(args.NewValue())) width_ = args.NewValue();
    });
    page.Children().Append(width);
    auto offset = Number(text::offset, offset_, 0, 20000);
    offset.ValueChanged([this](auto const&, NumberBoxValueChangedEventArgs const& args) {
        if (std::isfinite(args.NewValue())) offset_ = args.NewValue();
    });
    page.Children().Append(offset);
    page.Children().Append(Paragraph(text::placementNote));
}

void App::ShowAppearance() {
    auto page = MakePage(text::appearance, text::appearanceIntro);
    auto theme = Choices(text::theme, {text::systemTheme, text::lightTheme, text::darkTheme}, theme_);
    theme.SelectionChanged([this](IInspectable const& sender, auto const&) {
        theme_ = sender.as<ComboBox>().SelectedIndex();
        ApplyTheme();
    });
    page.Children().Append(theme);
    auto opacity = Number(text::opacity, opacity_, 25, 100);
    opacity.ValueChanged([this](auto const&, NumberBoxValueChangedEventArgs const& args) {
        if (std::isfinite(args.NewValue())) opacity_ = args.NewValue();
    });
    page.Children().Append(opacity);
    page.Children().Append(Paragraph(text::appearanceNote));
    auto close = Action(text::close);
    close.Click([this](auto const&, auto const&) { window_.Close(); });
    page.Children().Append(close);
}

void App::ApplyTheme() {
    using namespace Microsoft::UI::Windowing;
    navigation_.RequestedTheme(theme_ == 1 ? ElementTheme::Light : theme_ == 2 ? ElementTheme::Dark : ElementTheme::Default);
    if (AppWindowTitleBar::IsCustomizationSupported()) {
        window_.AppWindow().TitleBar().PreferredTheme(
            theme_ == 1 ? TitleBarTheme::Light : theme_ == 2 ? TitleBarTheme::Dark : TitleBarTheme::UseDefaultAppMode);
    }
}

void App::StartTimedClose() {
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    int seconds = 0;
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wstring_view(argv[index]) != L"--duration-seconds") continue;
        wchar_t* end = nullptr;
        const long value = wcstol(argv[index + 1], &end, 10);
        if (end && *end == L'\0' && value >= 1 && value <= 3600) seconds = static_cast<int>(value);
    }
    LocalFree(argv);
    if (seconds == 0) return;
    closeTimer_ = window_.DispatcherQueue().CreateTimer();
    closeTimer_.IsRepeating(false);
    closeTimer_.Interval(std::chrono::seconds(seconds));
    closeTimer_.Tick([this](auto const&, auto const&) { window_.Close(); });
    closeTimer_.Start();
}

} // namespace winrt::w11widget_native_settings::implementation
