# World Clock

The built-in `world-clock` module renders one time zone per independent widget
and supports individual panel names. It reads the local system clock with
`QDateTime` and converts it using Qt's bundled `QTimeZone` data.

## Zones and refresh

The initial configuration contains Countdown, then separate clocks for `UTC`,
`America/New_York` and `Europe/Istanbul`. **Saat ekle** in **Widget'ları düzenle…**
creates one new UTC clock. Select a clock, enter or choose an IANA time zone such
as `Asia/Tokyo` or `Europe/London`, then select **Uygula**. Typing or leaving the
field does not save. An invalid zone shows an error and retains the saved setting.

Each clock has its own enabled, visible and date settings and can be reordered or
removed independently. Current configurations render widgets in list order,
including Countdown. The module shows the clock's default or custom name and full
24-hour `HH:mm` time. It refreshes at the next minute boundary while active.
Daylight-saving changes come from `QTimeZone`, rather than a fixed UTC offset.

## Clock names

Select a clock in **Widget'ları düzenle…**. **Etiket** is prefilled with its editable
default name: `UTC`, `New York` or `İstanbul` for the initial zones. Other defaults
use the last IANA component with underscores changed to spaces, such as
`Buenos Aires` for `America/Argentina/Buenos_Aires`. Edit the text and press Enter
or leave the field to save a custom name. Names can contain up to 40 characters;
leading and trailing whitespace is trimmed. Clearing the field restores its
default name in both the editor and panel.

Renaming affects only the selected clock. Its time zone, date setting and other
clocks remain unchanged. The saved name survives minute refreshes and reopening
settings; later time-zone or date edits retain it. Without a custom name, changing
the zone follows that zone's default name. Defaults are computed on demand;
loading settings or leaving an unchanged default name does not save a label.
The detail tooltip includes the displayed name alongside the clock's zone details.

## Interaction and width

Each clock uses its measured text width, with a gap equal to two spaces between
its displayed name and time. Extra row width stays blank. The persisted default
`width_dip` is 72, but clock layout follows its content and the manager disables
the preferred-width field. **Tarihi göster** adds the local `MM-dd` date only to the
selected clock and grows its content width accordingly.

When widgets exceed the available width, the row scrolls inside the configured
panel. No clock text is truncated to force all widgets into that width. One shared
gear stays available outside the scrolling row, including when all clocks are
hidden, disabled or removed. Clicking a clock opens its date, time, UTC offset and
abbreviation in a transient detail tooltip without changing panel placement.

## Persistence and migration

Each clock stores `show_date` (a JSON boolean), `time_zone` (one valid IANA zone
string) and optional `label` (a string up to 40 UTF-16 code units). Empty or omitted
labels use the computed default name, so existing clock settings remain valid
without rewriting their bytes on load.
Widget flags and list position belong to that individual instance.

Legacy schema-2 `time_zones` arrays expand into separate clocks in memory. A legacy
clock without either zone setting expands to the previous UTC/New York/Istanbul
defaults. The first clock keeps the original instance ID; additional clocks receive
unique IDs, preserving zone order and the original date/enabled/visible settings.
Legacy layout migration retains the previous displayed order, with Countdown
instances first. New configurations follow their explicit widget order.

Loading does not rewrite the settings file. A later explicit edit or reset saves
the independent representation. Invalid legacy settings or expansion beyond the
16-widget limit produce an explicit error and leave the original file bytes intact.
