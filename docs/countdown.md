# Countdown

The built-in `countdown` module is a local target-time widget. Its default settings
are **Geri sayım**, `18:00`, `Europe/Istanbul` and daily repeat enabled.

## Settings

| Key | Type | Meaning |
|---|---|---|
| `label` | string | Nonblank name, at most 40 characters |
| `target_time` | string | Required 24-hour `HH:mm` target time |
| `daily_repeat` | boolean | Repeats at the target time every day |
| `time_zone` | string | Required available IANA time-zone ID |
| `target_date` | string | `yyyy-MM-dd` one-shot target date; empty for daily targets |

The click tool window validates all fields before it saves. It is transient to the
panel and uses the tool-window flag, so it does not become a separate taskbar
application. With daily repeat disabled,
an empty date is resolved from the chosen time and zone: a passed time becomes
tomorrow. The resolved date is written to the persisted widget settings.

## Status and refresh

The compact view renders `⏳ HH:mm`. At the target, daily Countdown shows
**Tamamlandı** for one minute, then calculates the next day. A one-shot Countdown
remains completed after its stored target date.

The module uses `QDateTime`, `QTimeZone` and a single-shot minute-aligned `QTimer`.
The panel refreshes active widgets after its own `WM_TIMECHANGE` or resume message;
this affects no external process or window. No network, file, account, telemetry
or extra permission is used.

## Width

The preferred width and fixed display width are 72 DIP. The host scales
visible widget slots to fit the configured panel width before allowing
horizontal scrolling. Countdown reduces to time-only at its compact threshold;
neither behavior changes native panel geometry.
