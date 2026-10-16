#include "pch.h"
#include "App.xaml.h"
#include "strings/tr-TR.h"
#include "../platform/TimeZones.h"
#include "../core/WidgetCatalog.h"
#include <commctrl.h>
#include <microsoft.ui.xaml.window.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Foundation;
namespace text = native_settings::text;
namespace config = w11widget::native;

namespace {

TextBlock Paragraph(std::wstring const& value, double size = 14) {
    TextBlock result;
    result.Text(value);
    result.FontSize(size);
    result.TextWrapping(TextWrapping::Wrap);
    return result;
}

Button Action(wchar_t const* label) {
    Button result;
    result.Content(box_value(label));
    Automation::AutomationProperties::SetName(result, label);
    return result;
}

ComboBox Choices(wchar_t const* header, std::initializer_list<wchar_t const*> values, int selected) {
    ComboBox result;
    result.Header(box_value(header));
    Automation::AutomationProperties::SetName(result, header);
    result.HorizontalAlignment(HorizontalAlignment::Stretch);
    for (auto value : values) result.Items().Append(box_value(value));
    result.SelectedIndex(selected);
    return result;
}

NumberBox Number(wchar_t const* header, double value, double minimum, double maximum) {
    NumberBox result;
    result.Header(box_value(header));
    Automation::AutomationProperties::SetName(result, header);
    result.Minimum(minimum);
    result.Maximum(maximum);
    result.Value(value);
    result.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
    return result;
}

TextBox Field(wchar_t const* header, std::wstring const& value, int length = 128) {
    TextBox result;
    result.Header(box_value(header));
    Automation::AutomationProperties::SetName(result, header);
    result.MaxLength(length);
    result.Text(value);
    return result;
}

ToggleSwitch Toggle(wchar_t const* header, bool value) {
    ToggleSwitch result;
    result.Header(box_value(header));
    Automation::AutomationProperties::SetName(result, header);
    result.OnContent(box_value(text::switchOn));
    result.OffContent(box_value(text::switchOff));
    result.IsOn(value);
    return result;
}

NavigationViewItem NavigationItem(wchar_t const* label, wchar_t const* tag, Symbol icon) {
    NavigationViewItem result;
    result.Content(box_value(label));
    result.Tag(box_value(tag));
    result.Icon(SymbolIcon(icon));
    return result;
}

std::wstring Label(config::WidgetConfiguration const& entry) {
    const auto label = config::stringValue(entry.settings, L"label");
    if (!label.empty()) return label;
    if (entry.typeId == L"world-clock") return config::worldClockSettings(entry).timeZone;
    if (entry.typeId == L"countdown") return text::countdown;
    if (entry.typeId == L"focus-timer") return text::focusLabel;
    return entry.typeId;
}

config::RegionContext Context(w11widget::windows::TaskbarSnapshot const& snapshot) {
    return {snapshot.monitorDevice, snapshot.monitorBounds.left, snapshot.monitorBounds.top,
        snapshot.monitorBounds.right, snapshot.monitorBounds.bottom, snapshot.bounds.left,
        snapshot.bounds.top, snapshot.bounds.right, snapshot.bounds.bottom, static_cast<int>(snapshot.dpi)};
}

w11widget::PlacementRequest Placement(config::PanelConfiguration const& settings) {
    return {settings.side == config::PanelSide::Left ? w11widget::PanelSide::Left : w11widget::PanelSide::Right,
        settings.widthDip, 32, settings.offsetDip, true};
}

std::wstring NewId() {
    GUID id{};
    check_hresult(CoCreateGuid(&id));
    wchar_t value[40]{};
    StringFromGUID2(id, value, 40);
    return std::wstring(value + 1, 36);
}

int IntegerValue(double value) {
    return std::isfinite(value) ? static_cast<int>(std::round(value)) : -1;
}

double UnixMilliseconds() {
    return std::chrono::duration<double, std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

namespace winrt::w11widget_native_settings::implementation {

App::App() { InitializeComponent(); }

void App::ParseArguments() {
    std::filesystem::path path = config::defaultSettingsPath();
    int count = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) {
        argumentError_ = text::invalidArguments;
        return;
    }
    for (int index = 1; index < count; ++index) {
        const std::wstring_view option(arguments[index]);
        if (option == L"--settings-file") {
            if (++index >= count || !std::filesystem::path(arguments[index]).is_absolute()) {
                argumentError_ = text::invalidArguments;
                break;
            }
            path = arguments[index];
        } else if (option == L"--widget-id") {
            if (++index < count) initialWidgetId_ = arguments[index];
        } else if (option == L"--duration-seconds" && index + 1 < count) {
            wchar_t* end = nullptr;
            const long value = wcstol(arguments[++index], &end, 10);
            if (end && *end == L'\0' && value >= 1 && value <= 3600) durationSeconds_ = static_cast<int>(value);
        }
    }
    LocalFree(arguments);
    if (argumentError_.empty()) store_ = std::make_unique<config::SettingsStore>(path);
}

void App::OnLaunched(LaunchActivatedEventArgs const&) {
    ParseArguments();
    window_ = Window();
    window_.Title(std::wstring(text::product) + L" " + to_hstring(W11WIDGET_VERSION).c_str() + text::titleSuffix);
    window_.AppWindow().Resize({1100, 820});
    window_.SystemBackdrop(MicaBackdrop());
    Grid root;
    RowDefinition contentRow;
    contentRow.Height({1, GridUnitType::Star});
    root.RowDefinitions().Append(contentRow);
    RowDefinition footerRow;
    footerRow.Height({1, GridUnitType::Auto});
    root.RowDefinitions().Append(footerRow);
    navigation_ = NavigationView();
    navigation_.PaneTitle(text::product);
    navigation_.IsSettingsVisible(false);
    navigation_.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
    navigation_.PaneDisplayMode(NavigationViewPaneDisplayMode::Auto);
    navigation_.OpenPaneLength(210);
    navigation_.MenuItems().Append(NavigationItem(text::widgets, L"widgets", Symbol::AllApps));
    navigation_.MenuItems().Append(NavigationItem(text::layout, L"layout", Symbol::View));
    navigation_.MenuItems().Append(NavigationItem(text::appearance, L"appearance", Symbol::Setting));
    navigation_.SelectionChanged([this](auto const&, NavigationViewSelectionChangedEventArgs const& args) {
        if (!args.SelectedItemContainer()) return;
        page_ = unbox_value<hstring>(args.SelectedItemContainer().Tag());
        ShowCurrentPage();
    });
    root.Children().Append(navigation_);
    StackPanel footer;
    footer.Spacing(10);
    footer.Margin({24, 8, 24, 18});
    Grid::SetRow(footer, 1);
    status_ = InfoBar();
    status_.IsClosable(false);
    status_.IsOpen(true);
    footer.Children().Append(status_);
    StackPanel actions;
    actions.Orientation(Orientation::Horizontal);
    actions.HorizontalAlignment(HorizontalAlignment::Right);
    actions.Spacing(8);
    auto revert = Action(text::revert);
    revert.Click([this](auto const&, auto const&) { LoadSettings(); });
    actions.Children().Append(revert);
    apply_ = Action(text::apply);
    apply_.Click([this](auto const&, auto const&) { SaveSettings(); });
    actions.Children().Append(apply_);
    auto close = Action(text::close);
    close.Click([this](auto const&, auto const&) { window_.Close(); });
    actions.Children().Append(close);
    footer.Children().Append(actions);
    root.Children().Append(footer);
    window_.Content(root);
    LoadSettings();
    navigation_.SelectedItem(navigation_.MenuItems().GetAt(0));
    window_.Closed([this](auto const&, auto const&) {
        if (closeTimer_) closeTimer_.Stop();
        Application::Current().Exit();
    });
    HWND nativeWindow = nullptr;
    check_hresult(window_.as<::IWindowNative>()->get_WindowHandle(&nativeWindow));
    check_bool(SetWindowSubclass(nativeWindow, WindowMessage, 1, reinterpret_cast<DWORD_PTR>(this)));
    window_.Activate();
    StartTimedClose();
}

LRESULT CALLBACK App::WindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                                    UINT_PTR subclassId, DWORD_PTR reference) {
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, WindowMessage, subclassId);
    if (message == WM_COPYDATA && lparam) {
      try {
        const auto data = reinterpret_cast<COPYDATASTRUCT const*>(lparam);
        // This protocol selects a known widget only; it never edits or executes a command.
        if (data->dwData != 0x57313157 || !data->lpData || data->cbData < 4 ||
            data->cbData > 130 || data->cbData % sizeof(wchar_t) != 0) return FALSE;
        const auto value = static_cast<wchar_t const*>(data->lpData);
        const auto count = data->cbData / sizeof(wchar_t);
        if (value[count - 1] != L'\0') return FALSE;
        const std::wstring id(value, count - 1);
        if (!std::all_of(id.begin(), id.end(), [](wchar_t c) {
            return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' || c == L'.';
        })) return FALSE;
        auto self = reinterpret_cast<App*>(reference);
        const auto exists = std::any_of(self->widgets_.begin(), self->widgets_.end(),
            [&id](auto const& entry) { return entry->instanceId == id; });
        if (!exists) return FALSE;
        const auto weak = self->get_weak();
        return self->window_.DispatcherQueue().TryEnqueue([weak, id] {
            if (const auto app = weak.get()) app->SelectWidget(id);
        }) ? TRUE : FALSE;
      } catch (...) { return FALSE; }
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

void App::SelectWidget(std::wstring const& instanceId) {
    const auto found = std::find_if(widgets_.begin(), widgets_.end(),
        [&instanceId](auto const& entry) { return entry->instanceId == instanceId; });
    if (found == widgets_.end()) return;
    selected_ = static_cast<int>(found - widgets_.begin());
    if (page_ == L"widgets") ShowWidgets();
    else navigation_.SelectedItem(navigation_.MenuItems().GetAt(0));
}

void App::SetStatus(wchar_t const* title, std::wstring const& message, InfoBarSeverity severity) {
    status_.Title(title);
    status_.Message(message);
    status_.Severity(severity);
    status_.IsOpen(true);
}

void App::LoadSettings() {
    loadOk_ = false;
    dirty_ = false;
    apply_.IsEnabled(false);
    widgets_.clear();
    if (!store_) {
        SetStatus(text::loadFailedTitle, argumentError_, InfoBarSeverity::Error);
    } else {
        auto loaded = store_->load();
        if (!loaded.ok) {
            SetStatus(text::loadFailedTitle, std::wstring(text::loadFailedMessage) + loaded.error, InfoBarSeverity::Error);
        } else {
            configuration_ = std::move(loaded.settings);
            revision_ = std::move(loaded.revision);
            loadOk_ = true;
            for (const auto& item : configuration_.widgets) widgets_.push_back(std::make_shared<config::WidgetConfiguration>(item));
            if (!initialWidgetId_.empty()) {
                const auto found = std::find_if(widgets_.begin(), widgets_.end(), [this](auto const& entry) { return entry->instanceId == initialWidgetId_; });
                if (found != widgets_.end()) selected_ = static_cast<int>(found - widgets_.begin());
                initialWidgetId_.clear();
            }
            dirty_ = loaded.missing || loaded.migrated;
            apply_.IsEnabled(dirty_);
            SetStatus(loaded.missing ? text::missingTitle : loaded.migrated ? text::migratedTitle : text::loadedTitle,
                loaded.missing ? text::missingMessage : loaded.migrated ? text::migratedMessage : text::loadedMessage,
                InfoBarSeverity::Informational);
        }
    }
    navigation_.IsEnabled(loadOk_);
    ApplyTheme();
    ShowCurrentPage();
}

void App::SaveSettings() {
    if (!loadOk_ || !store_) return;
    auto candidate = configuration_;
    candidate.widgets.clear();
    for (auto const& entry : widgets_) candidate.widgets.push_back(*entry);
    std::wstring error;
    for (auto& entry : candidate.widgets) {
        if (entry.typeId != L"countdown") continue;
        auto settings = config::countdownSettings(entry);
        if (!config::normalizeCountdown(settings, UnixMilliseconds(), error)) {
            SetStatus(text::invalidTitle, error, InfoBarSeverity::Error);
            return;
        }
        if (settings.targetDate != config::stringValue(entry.settings, L"target_date")) entry.settings[L"target_date"] = settings.targetDate;
    }
    if (!config::validateConfiguration(candidate, error, config::isValidTimeZone)) {
        SetStatus(text::invalidTitle, error, InfoBarSeverity::Error);
        return;
    }
    if (candidate.regionConfirmed) {
        std::string probeError;
        const auto snapshot = w11widget::windows::readTaskbarForMonitor(candidate.monitor, probeError);
        if (!snapshot || Context(*snapshot) != candidate.regionContext ||
            !w11widget::calculatePlacement(snapshot->bounds, snapshot->dpi, snapshot->edge, snapshot->autoHide, Placement(candidate)).ok) {
            InvalidateRegion();
            SetStatus(text::invalidTitle, text::confirmationNeeded, InfoBarSeverity::Warning);
            return;
        }
    }
    const auto saved = store_->save(candidate, revision_);
    if (!saved.ok) {
        SetStatus(saved.conflict ? text::conflictTitle : text::saveFailedTitle,
            saved.conflict ? std::wstring(text::conflictMessage) : saved.error, InfoBarSeverity::Error);
        return;
    }
    configuration_ = std::move(candidate);
    revision_ = saved.revision;
    for (std::size_t index = 0; index < widgets_.size(); ++index) *widgets_[index] = configuration_.widgets[index];
    dirty_ = false;
    apply_.IsEnabled(false);
    ShowCurrentPage();
    SetStatus(text::savedTitle, text::savedMessage, InfoBarSeverity::Success);
}

void App::MarkDirty() {
    if (!loadOk_) return;
    dirty_ = true;
    apply_.IsEnabled(true);
    SetStatus(text::pendingTitle, text::pendingMessage, InfoBarSeverity::Informational);
}

StackPanel App::MakePage(wchar_t const* title, wchar_t const* description) {
    widgetList_ = nullptr;
    inspector_ = nullptr;
    confirmed_ = nullptr;
    preview_ = nullptr;
    previewText_ = nullptr;
    navigation_.Header(box_value(title));
    StackPanel page;
    page.Spacing(18);
    page.Margin({28, 12, 28, 24});
    page.Children().Append(Paragraph(description));
    ScrollViewer scroll;
    scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    scroll.Content(page);
    navigation_.Content(scroll);
    return page;
}

void App::ShowCurrentPage() {
    if (page_ == L"layout") ShowLayout();
    else if (page_ == L"appearance") ShowAppearance();
    else ShowWidgets();
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
    ComboBox catalog;
    catalog.Header(box_value(text::addWidget));
    Automation::AutomationProperties::SetName(catalog, text::addWidget);
    catalog.HorizontalAlignment(HorizontalAlignment::Stretch);
    std::vector<std::wstring> catalogTypes;
    for (const auto& descriptor : config::widgetCatalog()) {
        if (descriptor.availability != config::WidgetAvailability::Implemented) continue;
        catalog.Items().Append(box_value(std::wstring(descriptor.name)));
        catalogTypes.emplace_back(descriptor.typeId);
    }
    catalog.SelectedIndex(catalogTypes.empty() ? -1 : 0);
    listPane.Children().Append(catalog);
    auto add = Action(text::addWidget);
    add.Click([this, catalog, catalogTypes](auto const&, auto const&) {
        if (widgets_.size() >= config::MaximumWidgetCount) {
            SetStatus(text::invalidTitle, text::widgetLimit, InfoBarSeverity::Warning);
            return;
        }
        const auto index = catalog.SelectedIndex();
        if (index < 0 || index >= static_cast<int>(catalogTypes.size())) return;
        const auto& type = catalogTypes[index];
        widgets_.push_back(std::make_shared<config::WidgetConfiguration>(config::defaultWidget(type, NewId())));
        selected_ = static_cast<int>(widgets_.size()) - 1;
        MarkDirty();
        RefreshWidgets();
    });
    listPane.Children().Append(add);
    widgetList_ = ListView();
    widgetList_.MinHeight(260);
    widgetList_.MaxHeight(390);
    widgetList_.SelectionMode(ListViewSelectionMode::Single);
    Automation::AutomationProperties::SetName(widgetList_, text::widgets);
    widgetList_.SelectionChanged([this](auto const&, auto const&) {
        if (refreshing_ || !widgetList_) return;
        selected_ = widgetList_.SelectedIndex();
        ShowInspector();
    });
    listPane.Children().Append(widgetList_);
    columns.Children().Append(listPane);
    inspector_ = StackPanel();
    inspector_.Spacing(12);
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
        // Equal labels need distinct UIA peers: microsoft/microsoft-ui-xaml#11226.
        ListViewItem row;
        row.Content(box_value(Label(*entry)));
        Automation::AutomationProperties::SetName(row, Label(*entry));
        widgetList_.Items().Append(row);
    }
    selected_ = widgets_.empty() ? -1 : std::clamp(selected_, 0, static_cast<int>(widgets_.size()) - 1);
    widgetList_.SelectedIndex(selected_);
    refreshing_ = false;
    ShowInspector();
}

void App::ShowInspector() {
    if (!inspector_) return;
    inspector_.Children().Clear();
    if (selected_ < 0 || selected_ >= static_cast<int>(widgets_.size())) {
        inspector_.Children().Append(Paragraph(text::empty));
        return;
    }
    const auto entry = widgets_[selected_];
    const bool known = config::isImplementedWidgetType(entry->typeId);
    if (known) {
        auto label = Field(text::displayName, config::stringValue(entry->settings, L"label"), 40);
        label.PlaceholderText(Label(*entry));
        label.TextChanged([this, entry](IInspectable const& sender, auto const&) {
            entry->settings[L"label"] = std::wstring(sender.as<TextBox>().Text());
            MarkDirty();
            const auto position = std::find(widgets_.begin(), widgets_.end(), entry);
            if (widgetList_ && position != widgets_.end()) {
                const auto index = static_cast<uint32_t>(position - widgets_.begin());
                if (index < widgetList_.Items().Size()) {
                    auto row = widgetList_.Items().GetAt(index).as<ListViewItem>();
                    row.Content(box_value(Label(*entry)));
                    Automation::AutomationProperties::SetName(row, Label(*entry));
                }
            }
        });
        inspector_.Children().Append(label);
    } else inspector_.Children().Append(Paragraph(text::unknownWidget));
    auto enabled = Toggle(text::enabled, entry->enabled);
    enabled.Toggled([this, entry](IInspectable const& sender, auto const&) { entry->enabled = sender.as<ToggleSwitch>().IsOn(); MarkDirty(); });
    inspector_.Children().Append(enabled);
    auto visible = Toggle(text::visible, entry->visible);
    visible.Toggled([this, entry](IInspectable const& sender, auto const&) { entry->visible = sender.as<ToggleSwitch>().IsOn(); MarkDirty(); });
    inspector_.Children().Append(visible);
    auto widgetWidth = Number(text::widgetWidth, entry->widthDip, 48, 320);
    widgetWidth.ValueChanged([this, entry](auto const&, NumberBoxValueChangedEventArgs const& args) { entry->widthDip = IntegerValue(args.NewValue()); MarkDirty(); });
    inspector_.Children().Append(widgetWidth);
    if (entry->typeId == L"world-clock" || entry->typeId == L"countdown") {
        auto zone = Field(text::timezone, config::stringValue(entry->settings, L"time_zone", entry->typeId == L"countdown" ? L"Europe/Istanbul" : L"UTC"));
        zone.PlaceholderText(text::timezoneHint);
        zone.TextChanged([this, entry](IInspectable const& sender, auto const&) { entry->settings[L"time_zone"] = std::wstring(sender.as<TextBox>().Text()); MarkDirty(); });
        inspector_.Children().Append(zone);
    }
    if (entry->typeId == L"world-clock") {
        auto showDate = Toggle(text::showDate, config::worldClockSettings(*entry).showDate);
        showDate.Toggled([this, entry](IInspectable const& sender, auto const&) { entry->settings[L"show_date"] = sender.as<ToggleSwitch>().IsOn(); MarkDirty(); });
        inspector_.Children().Append(showDate);
    } else if (entry->typeId == L"countdown") {
        const auto settings = config::countdownSettings(*entry);
        TimePicker target;
        target.Header(box_value(text::target));
        Automation::AutomationProperties::SetName(target, text::target);
        target.ClockIdentifier(L"24HourClock");
        if (config::validTime(settings.targetTime)) target.Time(std::chrono::minutes(std::stoi(settings.targetTime.substr(0, 2)) * 60 + std::stoi(settings.targetTime.substr(3, 2))));
        target.TimeChanged([this, entry](auto const&, TimePickerValueChangedEventArgs const& args) {
            const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(args.NewTime()).count();
            wchar_t value[6]{};
            swprintf_s(value, L"%02d:%02d", static_cast<int>(minutes / 60), static_cast<int>(minutes % 60));
            entry->settings[L"target_time"] = value;
            MarkDirty();
        });
        inspector_.Children().Append(target);
        auto date = Field(text::targetDate, settings.targetDate, 10);
        date.PlaceholderText(text::dateHint);
        date.IsEnabled(!settings.dailyRepeat);
        date.TextChanged([this, entry](IInspectable const& sender, auto const&) { entry->settings[L"target_date"] = std::wstring(sender.as<TextBox>().Text()); MarkDirty(); });
        auto daily = Toggle(text::daily, settings.dailyRepeat);
        daily.Toggled([this, entry, date](IInspectable const& sender, auto const&) {
            const auto value = sender.as<ToggleSwitch>().IsOn();
            entry->settings[L"daily_repeat"] = value;
            date.IsEnabled(!value);
            MarkDirty();
        });
        inspector_.Children().Append(daily);
        inspector_.Children().Append(date);
    } else if (entry->typeId == L"focus-timer") {
        auto duration = Number(text::duration, config::focusSettings(*entry).durationMinutes, 1, 180);
        duration.ValueChanged([this, entry](auto const&, NumberBoxValueChangedEventArgs const& args) { entry->settings[L"duration_minutes"] = IntegerValue(args.NewValue()); MarkDirty(); });
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
        MarkDirty();
        RefreshWidgets();
    });
    inspector_.Children().Append(remove);
}

void App::MoveWidget(int delta) {
    const auto destination = selected_ + delta;
    if (selected_ < 0 || destination < 0 || destination >= static_cast<int>(widgets_.size())) return;
    std::swap(widgets_[selected_], widgets_[destination]);
    selected_ = destination;
    MarkDirty();
    RefreshWidgets();
}

void App::InvalidateRegion() {
    configuration_.regionConfirmed = false;
    if (confirmed_) confirmed_.IsChecked(false);
    MarkDirty();
}

void App::ShowLayout() {
    auto page = MakePage(text::layout, text::layoutIntro);
    page.Spacing(14);
    preview_ = Canvas();
    preview_.Height(56);
    preview_.Background(SolidColorBrush(Windows::UI::Color{255, 116, 116, 116}));
    Automation::AutomationProperties::SetName(preview_, text::preview);
    preview_.SizeChanged([this](auto const&, auto const&) { UpdatePreview(); });
    page.Children().Append(preview_);
    previewText_ = Paragraph(L"");
    page.Children().Append(previewText_);
    std::string monitorError;
    const auto monitors = w11widget::windows::enumerateMonitors(monitorError);
    ComboBox monitor;
    monitor.Header(box_value(text::monitor));
    Automation::AutomationProperties::SetName(monitor, text::monitor);
    monitor.HorizontalAlignment(HorizontalAlignment::Stretch);
    std::vector<std::wstring> devices;
    int current = -1;
    for (const auto& value : monitors) {
        devices.push_back(value.device);
        const auto label = value.device + (value.primary ? std::wstring(L" · ") + text::primaryMonitor : L"");
        monitor.Items().Append(box_value(label));
        if (configuration_.monitor == value.device || (configuration_.monitor == L"primary" && value.primary)) current = static_cast<int>(devices.size()) - 1;
    }
    if (current < 0) {
        devices.push_back(configuration_.monitor);
        monitor.Items().Append(box_value(std::wstring(text::missingMonitor) + configuration_.monitor));
        current = static_cast<int>(devices.size()) - 1;
    }
    monitor.SelectedIndex(current);
    monitor.SelectionChanged([this, devices](IInspectable const& sender, auto const&) {
        const auto index = sender.as<ComboBox>().SelectedIndex();
        if (index < 0 || index >= static_cast<int>(devices.size())) return;
        configuration_.monitor = devices[index];
        InvalidateRegion();
        UpdatePreview();
    });
    Grid monitorRow;
    ColumnDefinition monitorColumn;
    monitorColumn.Width({1, GridUnitType::Star});
    ColumnDefinition refreshColumn;
    refreshColumn.Width({1, GridUnitType::Auto});
    monitorRow.ColumnDefinitions().Append(monitorColumn);
    monitorRow.ColumnDefinitions().Append(refreshColumn);
    monitor.Margin({0, 0, 12, 0});
    monitorRow.Children().Append(monitor);
    auto refresh = Action(text::refreshMonitors);
    refresh.VerticalAlignment(VerticalAlignment::Bottom);
    refresh.Click([this](auto const&, auto const&) { ShowLayout(); });
    Grid::SetColumn(refresh, 1);
    monitorRow.Children().Append(refresh);
    page.Children().Append(monitorRow);
    auto side = Choices(text::align, {text::left, text::right}, configuration_.side == config::PanelSide::Left ? 0 : 1);
    auto width = Number(text::width, configuration_.widthDip, 80, 640);
    auto offset = Number(text::offset, configuration_.offsetDip, 0, 20000);
    side.SelectionChanged([this](IInspectable const& sender, auto const&) {
        configuration_.side = sender.as<ComboBox>().SelectedIndex() == 0 ? config::PanelSide::Left : config::PanelSide::Right;
        InvalidateRegion(); UpdatePreview();
    });
    width.ValueChanged([this](auto const&, NumberBoxValueChangedEventArgs const& args) { configuration_.widthDip = IntegerValue(args.NewValue()); InvalidateRegion(); UpdatePreview(); });
    offset.ValueChanged([this](auto const&, NumberBoxValueChangedEventArgs const& args) { configuration_.offsetDip = IntegerValue(args.NewValue()); InvalidateRegion(); UpdatePreview(); });
    StackPanel presets;
    presets.Orientation(Orientation::Horizontal);
    presets.Spacing(8);
    for (int index = 0; index < 2; ++index) {
        auto button = Action(index == 0 ? text::presetLeft : text::presetRight);
        button.Click([side, width, offset, index](auto const&, auto const&) { side.SelectedIndex(index); width.Value(330); offset.Value(150); });
        presets.Children().Append(button);
    }
    page.Children().Append(presets);
    Grid geometry;
    for (int index = 0; index < 3; ++index) {
        ColumnDefinition column;
        column.Width({1, GridUnitType::Star});
        geometry.ColumnDefinitions().Append(column);
    }
    side.Margin({0, 0, 12, 0});
    width.Margin({0, 0, 12, 0});
    Grid::SetColumn(width, 1);
    Grid::SetColumn(offset, 2);
    geometry.Children().Append(side);
    geometry.Children().Append(width);
    geometry.Children().Append(offset);
    page.Children().Append(geometry);
    page.Children().Append(Paragraph(text::placementNote, 12));
    confirmed_ = CheckBox();
    confirmed_.Content(Paragraph(text::confirmRegion));
    Automation::AutomationProperties::SetName(confirmed_, text::confirmRegion);
    confirmed_.IsChecked(configuration_.regionConfirmed);
    confirmed_.Checked([this](auto const&, auto const&) {
        if (!previewSnapshot_) return;
        configuration_.regionConfirmed = true;
        configuration_.regionContext = Context(*previewSnapshot_);
        MarkDirty();
    });
    confirmed_.Unchecked([this](auto const&, auto const&) { configuration_.regionConfirmed = false; MarkDirty(); });
    page.Children().Append(confirmed_);
    UpdatePreview();
}

void App::UpdatePreview() {
    if (!preview_ || !previewText_) return;
    std::string error;
    previewSnapshot_ = w11widget::windows::readTaskbarForMonitor(configuration_.monitor, error);
    preview_.Children().Clear();
    if (configuration_.regionConfirmed && (!previewSnapshot_ || Context(*previewSnapshot_) != configuration_.regionContext)) InvalidateRegion();
    auto placement = previewSnapshot_ ? w11widget::calculatePlacement(previewSnapshot_->bounds, previewSnapshot_->dpi,
        previewSnapshot_->edge, previewSnapshot_->autoHide, Placement(configuration_)) : w11widget::PlacementResult{};
    if (confirmed_) confirmed_.IsEnabled(previewSnapshot_.has_value() && placement.ok);
    if (!previewSnapshot_ || !placement.ok) {
        previewText_.Text(std::wstring(text::previewUnavailable) + to_hstring(previewSnapshot_ ? placement.error : error).c_str());
        return;
    }
    Border candidate;
    candidate.CornerRadius({4, 4, 4, 4});
    candidate.BorderBrush(SolidColorBrush(Windows::UI::Color{255, 255, 255, 255}));
    candidate.BorderThickness({2, 2, 2, 2});
    candidate.Background(SolidColorBrush(Windows::UI::Color{255, 0, 120, 212}));
    const double scale = preview_.ActualWidth() / static_cast<double>(previewSnapshot_->bounds.width());
    candidate.Width(std::max(1.0, static_cast<double>(placement.bounds.width()) * scale));
    candidate.Height(36);
    Canvas::SetLeft(candidate, (static_cast<double>(placement.bounds.left) - previewSnapshot_->bounds.left) * scale);
    Canvas::SetTop(candidate, 10);
    preview_.Children().Append(candidate);
    previewText_.Text(std::wstring(text::previewReady) + L"\n" +
        std::to_wstring(placement.bounds.left) + L", " + std::to_wstring(placement.bounds.top) + L" · " +
        std::to_wstring(placement.bounds.width()) + L" × " + std::to_wstring(placement.bounds.height()) + L" px · " +
        std::to_wstring(previewSnapshot_->dpi) + L" DPI");
}

void App::ShowAppearance() {
    auto page = MakePage(text::appearance, text::appearanceIntro);
    auto theme = Choices(text::theme, {text::systemTheme, text::lightTheme, text::darkTheme},
        configuration_.theme == L"light" ? 1 : configuration_.theme == L"dark" ? 2 : 0);
    theme.SelectionChanged([this](IInspectable const& sender, auto const&) {
        const auto value = sender.as<ComboBox>().SelectedIndex();
        configuration_.theme = value == 1 ? L"light" : value == 2 ? L"dark" : L"system";
        MarkDirty(); ApplyTheme();
    });
    page.Children().Append(theme);
    auto opacity = Number(text::opacity, configuration_.opacity * 100, 25, 100);
    opacity.ValueChanged([this](auto const&, NumberBoxValueChangedEventArgs const& args) { configuration_.opacity = args.NewValue() / 100; MarkDirty(); });
    page.Children().Append(opacity);
    page.Children().Append(Paragraph(text::appearanceNote));
    if (store_) page.Children().Append(Paragraph(std::wstring(text::profile) + store_->filePath().wstring(), 12));
}

void App::ApplyTheme() {
    using namespace Microsoft::UI::Windowing;
    auto root = window_.Content().as<FrameworkElement>();
    root.RequestedTheme(configuration_.theme == L"light" ? ElementTheme::Light : configuration_.theme == L"dark" ? ElementTheme::Dark : ElementTheme::Default);
    if (AppWindowTitleBar::IsCustomizationSupported()) window_.AppWindow().TitleBar().PreferredTheme(
        configuration_.theme == L"light" ? TitleBarTheme::Light : configuration_.theme == L"dark" ? TitleBarTheme::Dark : TitleBarTheme::UseDefaultAppMode);
}

void App::StartTimedClose() {
    if (durationSeconds_ == 0) return;
    closeTimer_ = window_.DispatcherQueue().CreateTimer();
    closeTimer_.IsRepeating(false);
    closeTimer_.Interval(std::chrono::seconds(durationSeconds_));
    closeTimer_.Tick([this](auto const&, auto const&) { window_.Close(); });
    closeTimer_.Start();
}

} // namespace winrt::w11widget_native_settings::implementation
