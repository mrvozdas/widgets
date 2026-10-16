# Built-in Widget API 1

This document specifies the statically linked C++ widget contract used by
version 0.6.1, retaining API 1 introduced in 0.3.0. It applies only to modules
compiled into `w11widget`; dynamic
plugins and third-party binary ABI compatibility are not supported.

## Registration

Register a `WidgetDescriptor` and factory with `WidgetRegistry`. A type ID is
unique and must contain 1-64 lowercase ASCII letters, digits, `.`, `_`, or
`-`, beginning with a letter. The descriptor must declare
`WidgetApiVersion` (currently `1`) and a nonempty display name.

`qmlSource` must be an absolute, query-free `qrc:/.../*.qml` URL without `..`.
This restricts built-in views to compiled local resources. The registry rejects
duplicate type IDs, incompatible API versions, invalid descriptors, and empty
factories with an explicit error.

## Module lifecycle

Implement [`IWidget`](../src/core/IWidget.h) and return its view model from
`viewModel()`. All calls occur on the UI thread:

1. The host calls `validateSettings()` then `initialize()` once for an enabled
   configured instance.
2. It calls `updateSettings()` only after successful initialization when that
   instance's settings change.
3. A running, enabled, visible instance receives `activate()`. It receives
   `suspend()` when hidden or when the host stops.
4. `shutdown()` is called before removal, replacement, disabling, or host
   destruction. The host owns the module and destroys it after shutdown.

An implementation must return a useful `error` string on failure. It must not
perform network I/O, block the UI thread, or retain host objects after
`shutdown()`.

## Failure handling and settings

The host catches module and factory exceptions at the module boundary. A failed
instance becomes an error card without stopping other widgets or changing panel
placement/native visibility behavior. Unknown persisted `type_id` values are
kept as error entries so later versions can register them.

Persisted instances have an ID, type ID, enabled and visible flags, width in
the 48-320 DIP range, and a JSON settings object. At most 16 instances are
allowed. Widget-specific validation belongs in `validateSettings()`; the sample
module accepts only `label` and `accent` (`#RRGGBB`).

Each `world-clock` instance accepts `show_date` (boolean) and `time_zone` (one
valid IANA zone string), plus optional `label` (string, at most 40 UTF-16 code
units), and owns one clock view model. Trim leading/trailing label whitespace;
`displayName` returns the nonempty custom name or the zone's default name.
`WorldClockWidget::defaultLabelForTimeZone()` returns `UTC`, `New York` or `İstanbul`
for the initial zones; other IANA IDs use their last component with underscores
changed to spaces. Omitted or empty labels preserve compatibility with existing
settings and need no file write on load. The panel and detail tooltip use the
displayed name, while minute refreshes retain it.
The manager saves a clock label on `editingFinished`, including Enter or leaving
the field, preserving all other settings. Prefill its editor with the custom or
default name; an unchanged computed default must not introduce a custom label or
save. Clearing a custom name saves an empty label and restores the default in the
editor. Time-zone and date edits preserve custom labels; a zone change without a
custom label follows the new default. Adding a clock creates one
UTC instance. Its date, enabled and visible state, removal and order are independent
of other clocks. The host renders current configurations in array order for all
widget types. World Clock and Countdown use their measured QML implicit widths;
their persisted preferred width remains structurally validated but does not stretch
their content. Overflow scrolls within the fixed panel, while one shared settings
gear remains outside the row even when no clocks are present.

Settings loading migrates legacy schema-2 World Clock `time_zones` arrays, including
the former omitted-zone defaults, into independent instances. It preserves the
first ID, flags, date setting, zone order and previous Countdown-first displayed
order, and generates unique IDs for added instances. It must reject malformed
legacy settings or migration beyond the 16-instance limit with an explicit error.
Migration is in memory; only an explicit edit or reset may save the new shape.
