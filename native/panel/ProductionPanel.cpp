#include "ProductionPanel.h"
#include "ShellIntegration.h"
#include "locales/tr.h"
#include "../core/Configuration.h"
#include "../core/FocusTimer.h"
#include "../core/PopupPlacement.h"
#include "../platform/SettingsStore.h"
#include "../platform/TimeZones.h"
#include "platform/windows/TaskbarLocator.h"

#include <windowsx.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <psapi.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>

namespace w11widget::native {
namespace {
constexpr wchar_t PanelClass[] = L"w11widget.NativePanel";
constexpr wchar_t PopupClass[] = L"w11widget.WidgetPopup";
constexpr UINT TrayMessage = WM_APP + 1, ShellChanged = WM_APP + 2;
constexpr UINT_PTR RefreshTimer = 1, StopTimer = 2, ShellRetryTimer = 3, PopupDismissTimer = 4;
constexpr int SettingsId = 100, OverflowId = 101, WidgetBase = 200;
constexpr int PopupText = 301, PopupAction = 302, PopupReset = 303, PopupEdit = 304, PopupClose = 305;
constexpr int MenuShow = 401, MenuHide = 402, MenuExit = 403;

template<class T> void release(T*& object) { if (object) { object->Release(); object = nullptr; } }
double instant()
{
    FILETIME file{}; GetSystemTimeAsFileTime(&file);
    ULARGE_INTEGER value{}; value.LowPart = file.dwLowDateTime; value.HighPart = file.dwHighDateTime;
    return static_cast<double>(value.QuadPart - 116444736000000000ULL) / 10000.0;
}
std::int64_t monotonic() { return static_cast<std::int64_t>(GetTickCount64()); }
std::wstring secondsText(int seconds)
{
    std::wostringstream text;
    text << std::setfill(L'0') << std::setw(2) << seconds / 60 << L':' << std::setw(2) << seconds % 60;
    return text.str();
}
std::wstring remainingText(std::int64_t seconds)
{
    const auto minutes = (std::max<std::int64_t>(0, seconds) + 59) / 60;
    std::wostringstream text;
    if (minutes >= 1440) text << minutes / 1440 << strings::Days;
    text << std::setfill(L'0') << std::setw(2) << minutes / 60 % 24 << L':' << std::setw(2) << minutes % 60;
    return text.str();
}
const wchar_t* focusState(FocusState state)
{
    switch (state) {
    case FocusState::Running: return strings::Running;
    case FocusState::Paused: return strings::Paused;
    case FocusState::Completed: return strings::Completed;
    default: return strings::Ready;
    }
}
RegionContext contextOf(const windows::TaskbarSnapshot& value)
{
    return {value.monitorDevice, value.monitorBounds.left, value.monitorBounds.top,
        value.monitorBounds.right, value.monitorBounds.bottom, value.bounds.left, value.bounds.top,
        value.bounds.right, value.bounds.bottom, static_cast<int>(value.dpi)};
}
D2D1_COLOR_F color(COLORREF value)
{
    return D2D1::ColorF(GetRValue(value) / 255.0f, GetGValue(value) / 255.0f, GetBValue(value) / 255.0f);
}
struct Readout { std::wstring label, value, details; bool valid = true; };
struct ProductionPanel;
LRESULT CALLBACK panelProcedure(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK popupProcedure(HWND, UINT, WPARAM, LPARAM);

struct ProductionPanel {
    Options options;
    SettingsStore store;
    SettingsRevision revision;
    PanelConfiguration configuration;
    ProfileInstance instance;
    TaskbarEvents taskbarEvents;
    HWND window = nullptr, popup = nullptr, previousFocus = nullptr;
    HWND settingsButton = nullptr, overflowButton = nullptr;
    std::vector<HWND> widgetButtons;
    std::vector<size_t> overflowWidgets;
    std::vector<Readout> readouts;
    std::map<std::wstring, FocusTimer> focusTimers;
    size_t popupIndex = 0;
    HANDLE settingsProcess = nullptr, changes = INVALID_HANDLE_VALUE;
    DWORD settingsPid = 0;
    UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    bool userVisible = true, placed = false, positioning = false, dark = false, highContrast = false;
    bool trayAdded = false, runtimeFailed = false, profileValid = true, suspended = false;
    float textScale = 1.0f;
    std::wstring placementIssue, lastWarning;
    ID2D1Factory* graphics = nullptr;
    ID2D1DCRenderTarget* canvas = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteFactory* textFactory = nullptr;
    IDWriteTextFormat* labelFont = nullptr;
    IDWriteTextFormat* valueFont = nullptr;
    HFONT popupFont = nullptr;
    winrt::Windows::UI::ViewManagement::UISettings uiSettings{nullptr};
    winrt::Windows::UI::ViewManagement::UISettings::TextScaleFactorChanged_revoker textScaleChanged;
    HBRUSH backgroundBrush = nullptr;
    D2D1_COLOR_F background{}, foreground{}, surface{}, muted{}, border{};
    COLORREF backgroundColor = RGB(243,243,243), textColor = RGB(26,26,26);
    unsigned paints = 0, refreshes = 0;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    double readyMilliseconds = 0;

    ProductionPanel(const Options& value, std::filesystem::path profile)
        : options(value), store(std::move(profile)), configuration(defaultConfiguration()) {}
    ~ProductionPanel()
    {
        textScaleChanged.revoke();
        if (popup) DestroyWindow(popup);
        if (trayAdded) { auto icon = trayData(); Shell_NotifyIconW(NIM_DELETE, &icon); }
        if (changes != INVALID_HANDLE_VALUE) FindCloseChangeNotification(changes);
        if (settingsProcess) CloseHandle(settingsProcess);
        if (popupFont) DeleteObject(popupFont);
        if (backgroundBrush) DeleteObject(backgroundBrush);
        release(brush); release(canvas); release(labelFont); release(valueFont);
        release(textFactory); release(graphics);
    }
    NOTIFYICONDATAW trayData() const
    {
        NOTIFYICONDATAW icon{sizeof(icon)};
        icon.hWnd = window; icon.uID = 1;
        icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        icon.uCallbackMessage = TrayMessage;
        icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(icon.szTip, strings::ProductTitle);
        return icon;
    }
    void addTray()
    {
        auto icon = trayData();
        trayAdded = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
        if (trayAdded) { icon.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &icon); }
    }
    void warn(const std::wstring& message)
    {
        if (message.empty() || message == lastWarning) return;
        lastWarning = message;
        auto icon = trayData(); icon.uFlags = NIF_INFO;
        wcsncpy_s(icon.szInfo, message.c_str(), _TRUNCATE);
        wcscpy_s(icon.szInfoTitle, strings::ProductTitle);
        icon.dwInfoFlags = NIIF_WARNING;
        Shell_NotifyIconW(NIM_MODIFY, &icon);
    }
    bool initializeGraphics()
    {
        HRESULT result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &graphics);
        if (SUCCEEDED(result)) result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&textFactory));
        if (SUCCEEDED(result)) result = textFactory->CreateTextFormat(L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            9.0f, L"tr-TR", &labelFont);
        if (SUCCEEDED(result)) result = textFactory->CreateTextFormat(L"Segoe UI Variable", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f, L"tr-TR", &valueFont);
        for (auto* font : {labelFont, valueFont}) if (font) {
            font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            font->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            IDWriteInlineObject* ellipsis = nullptr;
            if (SUCCEEDED(textFactory->CreateEllipsisTrimmingSign(font, &ellipsis))) {
                font->SetTrimming(&trimming, ellipsis); ellipsis->Release();
            }
        }
        return SUCCEEDED(result);
    }
    bool ensureCanvas()
    {
        if (canvas) return true;
        const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
        HRESULT result = graphics->CreateDCRenderTarget(&properties, &canvas);
        if (SUCCEEDED(result)) result = canvas->CreateSolidColorBrush(foreground, &brush);
        if (FAILED(result)) { release(brush); release(canvas); }
        return SUCCEEDED(result);
    }
    void updateTheme()
    {
        try { textScale = static_cast<float>((uiSettings ? uiSettings : winrt::Windows::UI::ViewManagement::UISettings()).TextScaleFactor()); }
        catch (const winrt::hresult_error&) { textScale = 1.0f; }
        DWORD light = 1, bytes = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
        HIGHCONTRASTW contrast{sizeof(contrast)};
        highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
            (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        dark = !highContrast && (configuration.theme == L"dark" ||
            (configuration.theme == L"system" && light == 0));
        backgroundColor = highContrast ? GetSysColor(COLOR_WINDOW) : dark ? RGB(32,32,32) : RGB(243,243,243);
        textColor = highContrast ? GetSysColor(COLOR_WINDOWTEXT) : dark ? RGB(245,245,245) : RGB(26,26,26);
        background = color(backgroundColor); foreground = color(textColor);
        surface = highContrast ? background : D2D1::ColorF(dark ? 0x2b2b2b : 0xffffff);
        muted = highContrast ? foreground : D2D1::ColorF(dark ? 0xc8c8c8 : 0x535353);
        border = highContrast ? foreground : D2D1::ColorF(dark ? 0x565656 : 0xcdcdcd);
        if (backgroundBrush) DeleteObject(backgroundBrush);
        backgroundBrush = CreateSolidBrush(backgroundColor);
        for (const HWND target : {window, popup}) if (target) {
            const BOOL enabled = dark;
            DwmSetWindowAttribute(target, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled));
            const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
            DwmSetWindowAttribute(target, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
            RedrawWindow(target, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        }
        if (window) SetLayeredWindowAttributes(window, 0,
            static_cast<BYTE>(highContrast ? 255 : std::clamp(configuration.opacity, .25, 1.) * 255), LWA_ALPHA);
    }
    void load(bool initial = false)
    {
        const auto result = store.load();
        if (!result.ok) {
            if (initial) { configuration.widgets.clear(); profileValid = false; }
            warn(strings::ProfileFailed); return;
        }
        if (!initial && result.revision == revision) return;
        revision = result.revision;
        configuration = result.settings;
        profileValid = true; lastWarning.clear();
        // Resolve undated legacy one-shot targets once for this session; only Apply writes them.
        for (auto& widget : configuration.widgets) if (widget.typeId == L"countdown") {
            auto settings = countdownSettings(widget);
            std::wstring error;
            if (!settings.dailyRepeat && settings.targetDate.empty() && normalizeCountdown(settings, instant(), error))
                widget.settings[L"target_date"] = settings.targetDate;
        }
        for (auto item = focusTimers.begin(); item != focusTimers.end();) {
            const auto match = std::find_if(configuration.widgets.begin(), configuration.widgets.end(),
                [&](const auto& widget) { return widget.instanceId == item->first && widget.typeId == L"focus-timer"; });
            if (match == configuration.widgets.end()) item = focusTimers.erase(item); else ++item;
        }
        for (const auto& widget : configuration.widgets) if (widget.typeId == L"focus-timer") {
            auto [timer, inserted] = focusTimers.try_emplace(widget.instanceId, focusSettings(widget).durationMinutes);
            if (!inserted) timer->second.configure(focusSettings(widget).durationMinutes);
            if (!widget.enabled || !widget.visible) timer->second.pause(monotonic());
        }
        if (window) { closePopup(false); rebuildButtons(); updateTheme(); place(); refresh(); }
    }
    void watch()
    {
        try {
            uiSettings = winrt::Windows::UI::ViewManagement::UISettings();
            textScaleChanged = uiSettings.TextScaleFactorChanged(winrt::auto_revoke,[target=window](const auto&,const auto&) {
                PostMessageW(target,WM_SETTINGCHANGE,0,0);
            });
        } catch (const winrt::hresult_error&) {}
        std::error_code error;
        std::filesystem::create_directories(store.filePath().parent_path(), error);
        if (!error) changes = FindFirstChangeNotificationW(store.filePath().parent_path().c_str(), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
        if (changes == INVALID_HANDLE_VALUE) warn(strings::WatchFailed);
    }
    HWND button(int id, const wchar_t* label, HWND parent)
    {
        return CreateWindowExW(0, L"BUTTON", label,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0,0,1,1, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    }
    void rebuildButtons()
    {
        for (auto handle : widgetButtons) if (handle) DestroyWindow(handle);
        widgetButtons.assign(configuration.widgets.size(), nullptr);
        for (size_t index = 0; index < configuration.widgets.size(); ++index) {
            const auto& widget = configuration.widgets[index];
            if (widget.enabled && widget.visible)
                widgetButtons[index] = button(WidgetBase + static_cast<int>(index), strings::Widgets, window);
        }
        resize();
    }
    void resize()
    {
        if (!window) return;
        RECT bounds{}; GetClientRect(window, &bounds);
        const int dpi = static_cast<int>(GetDpiForWindow(window));
        const auto px = [dpi](int dip) { return MulDiv(dip, dpi, 96); };
        const int height = std::max(1L, bounds.bottom);
        const int control = std::min(px(32), static_cast<int>(bounds.right));
        MoveWindow(settingsButton, std::max(0L,bounds.right - control), 0, control, height, TRUE);
        int x = px(2); overflowWidgets.clear();
        bool overflow = false;
        const int available = std::max(0L, bounds.right - control);
        for (size_t index = 0; index < widgetButtons.size(); ++index) {
            const HWND handle = widgetButtons[index]; if (!handle) continue;
            const int width = px(configuration.widgets[index].widthDip);
            bool later = false;
            for (size_t next = index + 1; next < widgetButtons.size(); ++next) later |= widgetButtons[next] != nullptr;
            if (overflow || x + width > available - (later ? px(28) : 0)) {
                overflow = true; overflowWidgets.push_back(index); ShowWindow(handle, SW_HIDE);
                if (auto timer = focusTimers.find(configuration.widgets[index].instanceId); timer != focusTimers.end())
                    timer->second.pause(monotonic());
            } else { MoveWindow(handle, x, 0, width, height, TRUE); ShowWindow(handle, SW_SHOWNOACTIVATE); x += width; }
        }
        ShowWindow(overflowButton, overflow ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (overflow) MoveWindow(overflowButton, std::max(0, available - px(28)), 0, px(28), height, TRUE);
        InvalidateRect(window, nullptr, TRUE);
    }
    void pauseAll()
    {
        for (auto& [id, timer] : focusTimers) timer.pause(monotonic());
        KillTimer(window, RefreshTimer);
    }
    void place()
    {
        if (!window || positioning) return;
        positioning = true;
        std::string probeError;
        const auto taskbar = windows::readTaskbarForMonitor(configuration.monitor, probeError);
        if (taskbar) KillTimer(window, ShellRetryTimer);
        const bool confirmed = taskbar && configuration.regionConfirmed && configuration.regionContext == contextOf(*taskbar);
        PlacementResult placement;
        if (taskbar) placement = calculatePlacement(taskbar->bounds, taskbar->dpi, taskbar->edge, taskbar->autoHide,
            {configuration.side == PanelSide::Left ? w11widget::PanelSide::Left : w11widget::PanelSide::Right,
             configuration.widthDip, 32, configuration.offsetDip, confirmed});
        const bool wasPlaced = placed;
        placed = taskbar && placement.ok;
        placementIssue = placed ? L"" : !configuration.regionConfirmed ? strings::PreviewHint :
            !confirmed ? strings::ChangedLayout : strings::UnavailableLayout;
        LONG_PTR style = WS_POPUP | WS_CLIPCHILDREN;
        if (!placed) style |= WS_CAPTION | WS_SYSMENU;
        SetWindowLongPtrW(window, GWL_STYLE, style);
        SetWindowTextW(window, placed ? strings::ProductTitle : strings::PreviewTitle);
        RECT target{};
        if (placed) {
            target = {placement.bounds.left, placement.bounds.top, placement.bounds.right, placement.bounds.bottom};
            taskbarEvents.observe(taskbar->hwnd, window, ShellChanged);
            KillTimer(window, ShellRetryTimer);
        } else {
            RECT work{};
            if (taskbar) work = {taskbar->workArea.left,taskbar->workArea.top,taskbar->workArea.right,taskbar->workArea.bottom};
            else SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            const UINT dpi = taskbar ? taskbar->dpi : GetDpiForWindow(window);
            target = {0,0,MulDiv(configuration.widthDip, dpi, 96),MulDiv(36,dpi,96)};
            AdjustWindowRectExForDpi(&target, static_cast<DWORD>(style), FALSE,
                static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi);
            const int width = static_cast<int>(std::min<LONG>(target.right-target.left, work.right-work.left));
            const int height = target.bottom-target.top;
            target = {work.left + std::max(0L,(work.right-work.left-width)/2),work.top+MulDiv(32,dpi,96),0,0};
            target.right = target.left+width; target.bottom=target.top+height;
            if (!taskbar) SetTimer(window, ShellRetryTimer, 5000, nullptr);
        }
        if (wasPlaced != placed) closePopup(false);
        UINT flags = SWP_NOACTIVATE | SWP_FRAMECHANGED;
        if (userVisible) flags |= SWP_SHOWWINDOW;
        SetWindowPos(window, HWND_TOPMOST, target.left,target.top,target.right-target.left,target.bottom-target.top,flags);
        if (!userVisible) ShowWindow(window,SW_HIDE);
        const BOOL enabled=TRUE;
        DwmSetWindowAttribute(window,DWMWA_EXCLUDED_FROM_PEEK,&enabled,sizeof(enabled));
        DwmSetWindowAttribute(window,DWMWA_TRANSITIONS_FORCEDISABLED,&enabled,sizeof(enabled));
        positioning=false;
        resize();
        if (!placementIssue.empty()) warn(placementIssue);
    }
    Readout read(const WidgetConfiguration& widget)
    {
        Readout result;
        if (widget.typeId == L"world-clock") {
            const auto settings = worldClockSettings(widget);
            const auto reading = readWorldClock(settings.timeZone, instant());
            const auto slash = settings.timeZone.find_last_of(L'/');
            result.label = settings.label.empty() ? settings.timeZone.substr(slash == std::wstring::npos ? 0 : slash+1) : settings.label;
            std::replace(result.label.begin(),result.label.end(),L'_',L' ');
            result.valid=reading.valid; result.value=reading.time;
            if (settings.showDate) result.label += L" " + reading.date.substr(5);
            result.details=reading.time+L"\r\n"+reading.date+L"\r\n\r\n"+settings.timeZone+L"\r\n"+reading.offset+L" "+reading.abbreviation;
        } else if (widget.typeId == L"countdown") {
            const auto settings=countdownSettings(widget); const auto reading=readCountdown(settings,instant());
            result.label=settings.label; result.valid=reading.valid;
            result.value=reading.completed ? strings::Completed : remainingText(reading.remainingSeconds);
            result.details=result.value+L"\r\n\r\n"+strings::Target+reading.targetDate+L" "+settings.targetTime+L"\r\n"+
                strings::Zone+settings.timeZone+L"\r\n"+(settings.dailyRepeat ? strings::Daily : strings::Once);
        } else if (widget.typeId == L"focus-timer") {
            auto& timer=focusTimers.at(widget.instanceId); timer.update(monotonic());
            result.label=focusSettings(widget).label; result.value=secondsText(timer.remainingSeconds());
            result.details=result.value+L"  "+focusState(timer.state())+L"\r\n\r\n"+strings::FocusHint;
        } else {
            result.label=stringValue(widget.settings,L"label",widget.typeId);
            result.value=strings::UnknownWidget; result.details=strings::UnknownHint;
        }
        if (!result.valid) { result.value=strings::ReadoutError; result.details=strings::ClockError; }
        return result;
    }
    void refresh()
    {
        ++refreshes;
        readouts.clear();
        for (size_t index=0;index<configuration.widgets.size();++index) {
            const auto& widget=configuration.widgets[index];
            const bool exposed = widget.enabled && widget.visible &&
                ((index < widgetButtons.size() && widgetButtons[index] && IsWindowVisible(widgetButtons[index])) ||
                 (popup && popupIndex == index));
            if (exposed) readouts.push_back(read(widget));
            else readouts.push_back({stringValue(widget.settings,L"label",widget.typeId),L"",L"",true});
            if (index<widgetButtons.size() && widgetButtons[index]) {
                const auto name=readouts.back().label+L", "+readouts.back().value;
                wchar_t old[512]{}; GetWindowTextW(widgetButtons[index],old,512);
                if (name != old) { SetWindowTextW(widgetButtons[index],name.c_str()); InvalidateRect(widgetButtons[index],nullptr,FALSE); }
            }
        }
        if (popup) updatePopup();
        schedule();
    }
    void schedule()
    {
        KillTimer(window,RefreshTimer);
        if (!userVisible || !IsWindowVisible(window) || suspended) return;
        bool running=false;
        for (const auto& [id,timer]:focusTimers) running |= timer.running();
        SYSTEMTIME now{};GetSystemTime(&now);
        SetTimer(window,RefreshTimer,running ? 1000 : millisecondsToNextMinute(now.wSecond*1000U+now.wMilliseconds),nullptr);
    }
    void drawButton(const DRAWITEMSTRUCT& item)
    {
        if (!ensureCanvas() || FAILED(canvas->BindDC(item.hDC,&item.rcItem))) { runtimeFailed=true; return; }
        const UINT dpi=GetDpiForWindow(item.hwndItem);
        canvas->SetDpi(static_cast<float>(dpi),static_cast<float>(dpi));
        const float width=(item.rcItem.right-item.rcItem.left)*96.0f/dpi;
        const float height=(item.rcItem.bottom-item.rcItem.top)*96.0f/dpi;
        canvas->BeginDraw(); canvas->Clear(background);
        const bool pressed=(item.itemState&ODS_SELECTED)!=0;
        brush->SetColor(pressed ? border : surface);
        const auto bounds=D2D1::RoundedRect(D2D1::RectF(2,2,width-2,height-2),4,4);
        canvas->FillRoundedRectangle(bounds,brush);
        if (highContrast || (item.itemState&ODS_FOCUS)!=0) {
            brush->SetColor(foreground); canvas->DrawRoundedRectangle(bounds,brush,1.0f);
        }
        const int index=static_cast<int>(item.CtlID)-WidgetBase;
        if (item.CtlID>=WidgetBase && index>=0 && static_cast<size_t>(index)<readouts.size() && item.hwndItem != GetDlgItem(popup,item.CtlID)) {
            const auto& reading=readouts[index];
            if (textScale <= 1.25f) {
                brush->SetColor(muted);
                canvas->DrawTextW(reading.label.c_str(),static_cast<UINT32>(reading.label.size()),labelFont,
                    D2D1::RectF(5,2,width-5,14),brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            brush->SetColor(foreground);
            IDWriteTextLayout* text = nullptr;
            const float top = textScale > 1.25f ? 2.0f : 12.0f;
            if (SUCCEEDED(textFactory->CreateTextLayout(reading.value.c_str(),static_cast<UINT32>(reading.value.size()),
                valueFont,std::max(1.0f,width-10),std::max(1.0f,height-top-2),&text))) {
                text->SetFontSize(12.0f*textScale,{0,static_cast<UINT32>(reading.value.size())});
                canvas->DrawTextLayout(D2D1::Point2F(5,top),text,brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
                text->Release();
            }
        } else {
            wchar_t caption[128]{};GetWindowTextW(item.hwndItem,caption,128);
            std::wstring text=item.CtlID==SettingsId ? L"⚙" : item.CtlID==OverflowId ? L"…" : caption;
            brush->SetColor(foreground);
            canvas->DrawTextW(text.c_str(),static_cast<UINT32>(text.size()),valueFont,
                D2D1::RectF(4,2,width-4,height-2),brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        const auto result=canvas->EndDraw();
        if (result==D2DERR_RECREATE_TARGET) { release(brush);release(canvas);InvalidateRect(item.hwndItem,nullptr,FALSE); }
        else if (FAILED(result)) runtimeFailed=true; else ++paints;
    }
    void show(bool visible)
    {
        userVisible=visible;
        if (!visible) { pauseAll();closePopup(false);ShowWindow(window,SW_HIDE); }
        else { place();refresh(); }
    }
    void openSettings(const std::wstring& widgetId={})
    {
        if (settingsProcess && WaitForSingleObject(settingsProcess,0)==WAIT_TIMEOUT) {
            struct Selection { DWORD pid; HWND selected = nullptr; long long area = 0; } selection{settingsPid};
            EnumWindows([](HWND candidate,LPARAM context)->BOOL {
                auto& selection=*reinterpret_cast<Selection*>(context);
                DWORD process=0;GetWindowThreadProcessId(candidate,&process);
                if(process!=selection.pid||!IsWindowVisible(candidate))return TRUE;
                DWORD cloaked=0;
                if(SUCCEEDED(DwmGetWindowAttribute(candidate,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked)return TRUE;
                RECT bounds{};
                if(GetWindowRect(candidate,&bounds)) {
                    const auto area=static_cast<long long>(bounds.right-bounds.left)*(bounds.bottom-bounds.top);
                    if(area>selection.area) {selection.selected=candidate;selection.area=area;}
                }
                return TRUE;
            },reinterpret_cast<LPARAM>(&selection));
            if(selection.selected) {
                if(!widgetId.empty()) {
                    COPYDATASTRUCT data{0x57313157,static_cast<DWORD>((widgetId.size()+1)*sizeof(wchar_t)),
                        const_cast<wchar_t*>(widgetId.c_str())};
                    DWORD_PTR response=0;
                    SendMessageTimeoutW(selection.selected,WM_COPYDATA,reinterpret_cast<WPARAM>(window),reinterpret_cast<LPARAM>(&data),
                        SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&response);
                }
                ShowWindow(selection.selected,SW_RESTORE);SetForegroundWindow(selection.selected);
            }
            return;
        }
        if (settingsProcess) { CloseHandle(settingsProcess);settingsProcess=nullptr; }
        std::wstring path(32768,L'\0');const DWORD count=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if (!count||count>=path.size()) return;path.resize(count);
        const auto executable=std::filesystem::path(path).parent_path()/L"w11widget_native_settings.exe";
        std::error_code error;
        if (!std::filesystem::is_regular_file(executable,error)) { warn(strings::SettingsUnavailable);return; }
        std::wstring command=quoteArgument(executable.wstring())+L" --settings-file "+quoteArgument(store.filePath().wstring());
        if (!widgetId.empty()) command+=L" --widget-id "+quoteArgument(widgetId);
        STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,
            executable.parent_path().c_str(),&startup,&process)) {
            warn(std::wstring(strings::SettingsStartFailed)+std::to_wstring(GetLastError()));return;
        }
        CloseHandle(process.hThread);settingsProcess=process.hProcess;settingsPid=process.dwProcessId;
    }
    void closePopup(bool restoreFocus)
    {
        if (!popup) return;
        const bool hadFocus=GetForegroundWindow()==popup;
        const HWND old=popup;popup=nullptr;DestroyWindow(old);
        if (popupIndex < configuration.widgets.size() &&
            (popupIndex >= widgetButtons.size() || !widgetButtons[popupIndex] || !IsWindowVisible(widgetButtons[popupIndex]))) {
            const auto timer=focusTimers.find(configuration.widgets[popupIndex].instanceId);
            if(timer!=focusTimers.end())timer->second.pause(monotonic());
        }
        if (restoreFocus&&hadFocus&&IsWindow(previousFocus))SetForegroundWindow(previousFocus);
        schedule();
    }
    void openPopup(size_t index)
    {
        if (index>=configuration.widgets.size())return;
        closePopup(false);previousFocus=GetForegroundWindow();popupIndex=index;
        const auto& widget=configuration.widgets[index];
        if (!widget.enabled || !widget.visible) return;
        if (!userVisible) show(true);
        if(index<readouts.size())readouts[index]=read(widget);
        const HWND anchor=index<widgetButtons.size()&&IsWindowVisible(widgetButtons[index]) ? widgetButtons[index] : window;
        RECT target{};GetWindowRect(anchor,&target);
        MONITORINFO monitor{sizeof(monitor)};
        if(!GetMonitorInfoW(MonitorFromRect(&target,MONITOR_DEFAULTTONEAREST),&monitor))return;
        const UINT dpi=GetDpiForWindow(window);
        const auto effectiveDpi=static_cast<UINT>(dpi*textScale);
        const auto placement=popupPlacement({target.left,target.top,target.right,target.bottom},
            {monitor.rcWork.left,monitor.rcWork.top,monitor.rcWork.right,monitor.rcWork.bottom},340,230,effectiveDpi);
        if(!placement)return;
        const int width=static_cast<int>(placement->width()),height=static_cast<int>(placement->height());
        const auto label=index<readouts.size()?readouts[index].label:widget.instanceId;
        popup=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,PopupClass,label.c_str(),WS_POPUP|WS_BORDER|WS_CLIPCHILDREN,
            placement->left,placement->top,width,height,window,nullptr,GetModuleHandleW(nullptr),this);
        if(!popup)return;
        const auto px=[effectiveDpi](int value){return MulDiv(value,effectiveDpi,96);};
        if(popupFont)DeleteObject(popupFont);
        NONCLIENTMETRICSW metrics{sizeof(metrics)};
        if(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0)) {
            metrics.lfMessageFont.lfHeight=-px(14);popupFont=CreateFontIndirectW(&metrics.lfMessageFont);
        }
        HWND text=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT,
            px(20),px(18),width-px(40),height-px(92),popup,reinterpret_cast<HMENU>(static_cast<INT_PTR>(PopupText)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(text,WM_SETFONT,reinterpret_cast<WPARAM>(popupFont),TRUE);
        int left=px(16);
        if(widget.typeId==L"focus-timer") {
            HWND action=button(PopupAction,strings::Start,popup);MoveWindow(action,left,height-px(66),px(100),px(30),TRUE);
            HWND reset=button(PopupReset,strings::Reset,popup);MoveWindow(reset,left+px(108),height-px(66),px(90),px(30),TRUE);
        }
        HWND edit=button(PopupEdit,strings::Edit,popup);MoveWindow(edit,left,height-px(34),px(100),px(28),TRUE);
        HWND close=button(PopupClose,strings::Close,popup);MoveWindow(close,width-px(106),height-px(34),px(90),px(28),TRUE);
        updateTheme();updatePopup();ShowWindow(popup,SW_SHOWNORMAL);SetForegroundWindow(popup);
        SetFocus(GetDlgItem(popup,widget.typeId==L"focus-timer"?PopupAction:PopupEdit));
    }
    void updatePopup()
    {
        if(!popup||popupIndex>=readouts.size())return;
        const auto& reading=readouts[popupIndex];
        const auto detail=reading.label+L"\r\n"+reading.details;
        SetDlgItemTextW(popup,PopupText,detail.c_str());
        const auto& widget=configuration.widgets[popupIndex];
        if(widget.typeId==L"focus-timer") {
            const auto& timer=focusTimers.at(widget.instanceId);
            SetDlgItemTextW(popup,PopupAction,timer.running()?strings::Pause:
                timer.state()==FocusState::Paused?strings::Resume:strings::Start);
        }
    }
    void menu(bool onlyOverflow=false)
    {
        HMENU menu=CreatePopupMenu();
        if(!onlyOverflow) {
            AppendMenuW(menu,MF_STRING,userVisible?MenuHide:MenuShow,userVisible?strings::HidePanel:strings::ShowPanel);
            AppendMenuW(menu,MF_STRING,SettingsId,strings::Settings);AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        }
        for(size_t index=0;index<configuration.widgets.size();++index) {
            if(!configuration.widgets[index].enabled||!configuration.widgets[index].visible)continue;
            if(onlyOverflow&&std::find(overflowWidgets.begin(),overflowWidgets.end(),index)==overflowWidgets.end())continue;
            readouts[index]=read(configuration.widgets[index]);
            const auto label=readouts[index].label+L" — "+readouts[index].value;
            AppendMenuW(menu,MF_STRING,WidgetBase+index,label.c_str());
        }
        if(!onlyOverflow) {AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,MenuExit,strings::Exit);}
        POINT point{};GetCursorPos(&point);SetForegroundWindow(window);
        const UINT selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);
        DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);
        if(selected==SettingsId)openSettings();
        else if(selected==MenuShow)show(true);
        else if(selected==MenuHide)show(false);
        else if(selected==MenuExit)DestroyWindow(window);
        else if(selected>=WidgetBase&&selected<WidgetBase+configuration.widgets.size())openPopup(selected-WidgetBase);
    }
    void writeMetrics()
    {
        if(options.metricsFile.empty())return;
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
        GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
        std::ofstream output(std::filesystem::path(options.metricsFile),std::ios::binary|std::ios::trunc);
        output<<"{\"product\":\"w11widget-native\",\"version\":\""<<W11WIDGET_VERSION
              <<"\",\"pid\":"<<GetCurrentProcessId()<<",\"widget_count\":"<<configuration.widgets.size()
              <<",\"ready_milliseconds\":"<<readyMilliseconds<<",\"private_bytes\":"<<memory.PrivateUsage
              <<",\"paint_count\":"<<paints<<",\"refresh_count\":"<<refreshes
              <<",\"profile_valid\":"<<(profileValid?"true":"false")
              <<",\"runtime_failed\":"<<(runtimeFailed?"true":"false")<<"}\n";
        if(!output)runtimeFailed=true;
    }
};

LRESULT CALLBACK popupProcedure(HWND window,UINT message,WPARAM value,LPARAM parameter)
{
    auto* self=reinterpret_cast<ProductionPanel*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        self=static_cast<ProductionPanel*>(reinterpret_cast<CREATESTRUCTW*>(parameter)->lpCreateParams);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
    }
    if(!self)return DefWindowProcW(window,message,value,parameter);
    switch(message) {
    case WM_COMMAND:
        if(HIWORD(value)!=BN_CLICKED)return 0;
        if(LOWORD(value)==PopupClose)self->closePopup(true);
        else if(LOWORD(value)==PopupEdit) {
            const auto id=self->configuration.widgets[self->popupIndex].instanceId;
            self->closePopup(false);self->openSettings(id);
        } else if(LOWORD(value)==PopupAction||LOWORD(value)==PopupReset) {
            auto& timer=self->focusTimers.at(self->configuration.widgets[self->popupIndex].instanceId);
            if(LOWORD(value)==PopupReset)timer.reset();
            else if(timer.running())timer.pause(monotonic());else timer.start(monotonic());
            self->refresh();
        }
        return 0;
    case WM_ACTIVATE:
        if(LOWORD(value)==WA_INACTIVE) {
            // Win32 UIA moves focus through child controls before invoking them.
            // Let that activation sequence settle before deciding the user left the flyout.
            SetTimer(window,PopupDismissTimer,150,nullptr);
        } else KillTimer(window,PopupDismissTimer);
        return 0;
    case WM_TIMER:
        if(value==PopupDismissTimer) {
            KillTimer(window,PopupDismissTimer);
            const HWND active=GetForegroundWindow();
            if(active!=window&&!IsChild(window,active))self->closePopup(false);
        }
        return 0;
    case WM_CLOSE:self->closePopup(true);return 0;
    case WM_KEYDOWN:if(value==VK_ESCAPE) {self->closePopup(true);return 0;}break;
    case WM_DRAWITEM:self->drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(parameter));return TRUE;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(value),self->textColor);
        SetBkColor(reinterpret_cast<HDC>(value),self->backgroundColor);
        return reinterpret_cast<LRESULT>(self->backgroundBrush);
    case WM_ERASEBKGND: {RECT bounds{};GetClientRect(window,&bounds);FillRect(reinterpret_cast<HDC>(value),&bounds,self->backgroundBrush);return 1;}
    case WM_DPICHANGED:self->closePopup(true);return 0;
    }
    return DefWindowProcW(window,message,value,parameter);
}
LRESULT CALLBACK panelProcedure(HWND window,UINT message,WPARAM value,LPARAM parameter)
{
    auto* self=reinterpret_cast<ProductionPanel*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        self=static_cast<ProductionPanel*>(reinterpret_cast<CREATESTRUCTW*>(parameter)->lpCreateParams);self->window=window;
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
    }
    if(!self)return DefWindowProcW(window,message,value,parameter);
    if(self->taskbarCreated&&message==self->taskbarCreated) {self->addTray();self->place();return 0;}
    switch(message) {
    case WM_CREATE:
        self->settingsButton=self->button(SettingsId,strings::Settings,window);
        self->overflowButton=self->button(OverflowId,strings::MoreWidgets,window);
        return self->settingsButton&&self->overflowButton?0:-1;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_COMMAND:
        if(HIWORD(value)==BN_CLICKED) {
            const int id=LOWORD(value);
            if(id==MenuExit)DestroyWindow(window);
            else if(id==MenuHide)self->show(false);
            else if(id==MenuShow)self->show(true);
            else if(id==SettingsId)self->openSettings();
            else if(id==OverflowId)self->menu(true);
            else if(id>=WidgetBase)self->openPopup(static_cast<size_t>(id-WidgetBase));
        }return 0;
    case WM_CONTEXTMENU:self->menu();return 0;
    case TrayMessage:
        if(LOWORD(parameter)==WM_CONTEXTMENU||LOWORD(parameter)==NIN_KEYSELECT)self->menu();
        else if(LOWORD(parameter)==NIN_SELECT)self->show(!self->userVisible);
        return 0;
    case ShellChanged: self->place();return 0;
    case WM_TIMER:
        if(value==RefreshTimer)self->refresh();
        else if(value==StopTimer)DestroyWindow(window);
        else if(value==ShellRetryTimer)self->place();
        return 0;
    case WM_DRAWITEM:self->drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(parameter));return TRUE;
    case WM_PAINT:{PAINTSTRUCT paint{};BeginPaint(window,&paint);FillRect(paint.hdc,&paint.rcPaint,self->backgroundBrush);EndPaint(window,&paint);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:self->resize();return 0;
    case WM_WINDOWPOSCHANGING:
        if(self->placed&&self->userVisible&&!self->positioning) {
            auto* position=reinterpret_cast<WINDOWPOS*>(parameter);
            position->flags&=~SWP_HIDEWINDOW;
            if((position->flags&SWP_NOZORDER)==0)position->hwndInsertAfter=HWND_TOPMOST;
        }break;
    case WM_SHOWWINDOW:
        if(!value) {self->pauseAll();self->closePopup(false);}else self->schedule();break;
    case WM_TIMECHANGE:self->refresh();return 0;
    case WM_POWERBROADCAST:
        if(value==PBT_APMSUSPEND) {self->suspended=true;self->pauseAll();self->closePopup(false);}
        if(value==PBT_APMRESUMEAUTOMATIC||value==PBT_APMRESUMESUSPEND) {self->suspended=false;self->place();self->refresh();}
        return TRUE;
    case WM_THEMECHANGED:self->updateTheme();return 0;
    case WM_SETTINGCHANGE:self->updateTheme();[[fallthrough]];
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
    case WM_DEVICECHANGE:self->closePopup(false);self->place();return 0;
    case WM_CLOSE:if(self->trayAdded)self->show(false);else DestroyWindow(window);return 0;
    case WM_DESTROY:
        self->closePopup(false);self->pauseAll();KillTimer(window,StopTimer);KillTimer(window,ShellRetryTimer);
        PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,value,parameter);
}
}

int runProductionPanel(HINSTANCE instance,const Options& options)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    const auto profile=options.settingsFile.empty()?defaultSettingsPath():std::filesystem::path(options.settingsFile);
    if(!profile.is_absolute()||profile.filename().empty())return 2;
    ProductionPanel self(options,profile);
    std::wstring error;
    const auto result=self.instance.start(profile,options.settings,error);
    if(result==ProfileInstance::Result::Existing)return 0;
    if(result==ProfileInstance::Result::Error) {MessageBoxW(nullptr,strings::InstanceFailed,strings::ProductTitle,MB_ICONERROR);return 1;}
    if(!self.initializeGraphics())return 1;
    self.load(true);self.updateTheme();
    for(const auto& [name,procedure]:{std::pair{PanelClass,panelProcedure},std::pair{PopupClass,popupProcedure}}) {
        WNDCLASSEXW type{sizeof(type)};type.lpfnWndProc=procedure;type.hInstance=instance;
        type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.lpszClassName=name;
        if(!RegisterClassExW(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 1;
    }
    if(!CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED,
        PanelClass,strings::ProductTitle,WS_POPUP|WS_CLIPCHILDREN,0,0,330,32,nullptr,nullptr,instance,&self))return 1;
    self.userVisible=!options.background;
    self.rebuildButtons();self.addTray();self.watch();self.load();self.updateTheme();self.place();self.refresh();
    self.instance.onActivation([&](bool settings){if(settings)self.openSettings();else self.show(true);});
    if(options.settings || !self.profileValid)self.openSettings();
    if(options.durationSeconds && !SetTimer(self.window,StopTimer,options.durationSeconds*1000U,nullptr)) {
        DestroyWindow(self.window);return 1;
    }
    self.readyMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-self.started).count();
    bool finished=false;
    while(!finished) {
        const DWORD count=self.changes==INVALID_HANDLE_VALUE?0:1;
        const auto wait=MsgWaitForMultipleObjectsEx(count,count?&self.changes:nullptr,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        if(wait==WAIT_FAILED) {self.runtimeFailed=true;break;}
        if(count&&wait==WAIT_OBJECT_0) {
            // Rearm before loading: a second atomic replace cannot disappear between read and rearm.
            if(!FindNextChangeNotification(self.changes)) {FindCloseChangeNotification(self.changes);self.changes=INVALID_HANDLE_VALUE;self.warn(strings::WatchFailed);}
            self.load();
        }
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_QUIT) {finished=true;break;}
            if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE&&self.popup) {self.closePopup(true);continue;}
            if(message.message==WM_KEYDOWN&&message.wParam==VK_F2) {self.openSettings();continue;}
            if(self.popup&&IsDialogMessageW(self.popup,&message))continue;
            if(!IsDialogMessageW(self.window,&message)) {TranslateMessage(&message);DispatchMessageW(&message);}
        }
    }
    self.writeMetrics();
    return self.runtimeFailed?1:0;
}
}
