#pragma once

namespace w11widget {

struct VisibilityInputs {
    bool requested = false;
    bool approved = false;
    bool geometryUnchanged = false;
    bool taskbarAvailable = false;
    bool autoHide = false;
    bool fullscreen = false;
    bool suspend = false;
};

enum class VisibilityDecision {
    Hidden,
    Show,
    AwaitConfirmation
};

// Approval represents the last explicit, validated Show request. Once approved,
// environment changes do not override the user's request to keep the panel up.
// The controller validates actual geometry before accepting the initial Show.
[[nodiscard]] VisibilityDecision decideVisibility(const VisibilityInputs& inputs) noexcept;

} // namespace w11widget
