#include "core/VisibilityPolicy.h"

namespace w11widget {

VisibilityDecision decideVisibility(const VisibilityInputs& inputs) noexcept {
    if (!inputs.requested) {
        return VisibilityDecision::Hidden;
    }
    if (!inputs.approved) {
        return VisibilityDecision::AwaitConfirmation;
    }
    // Fullscreen, auto-hide, missing shell probes and DPI/power events never
    // cancel a successful explicit Show. Retain its observed placement until
    // the user requests Hide or approves another placement.
    return VisibilityDecision::Show;
}

} // namespace w11widget
