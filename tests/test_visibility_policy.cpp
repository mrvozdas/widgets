#include "core/VisibilityPolicy.h"

#include <iostream>

namespace {

using w11widget::VisibilityDecision;
using w11widget::VisibilityInputs;
int failures = 0;

void expect(const VisibilityInputs& inputs, VisibilityDecision expected, const char* name) {
    if (w11widget::decideVisibility(inputs) != expected) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}

} // namespace

int main() {
    static_assert(noexcept(w11widget::decideVisibility(VisibilityInputs{})));
    expect({}, VisibilityDecision::Hidden, "startup never requests visibility");

    // Exhaust every environment combination. Initial approval and user intent
    // alone govern visibility; former automatic-hide gates must stay inert.
    for (unsigned flags = 0; flags < 32; ++flags) {
        VisibilityInputs inputs;
        inputs.geometryUnchanged = (flags & 1U) != 0;
        inputs.taskbarAvailable = (flags & 2U) != 0;
        inputs.autoHide = (flags & 4U) != 0;
        inputs.fullscreen = (flags & 8U) != 0;
        inputs.suspend = (flags & 16U) != 0;
        expect(inputs, VisibilityDecision::Hidden, "idle environment events never show the panel");
        inputs.approved = true;
        expect(inputs, VisibilityDecision::Hidden, "manual Hide dominates cached approval");
        inputs.requested = true;
        expect(inputs, VisibilityDecision::Show, "approved Show stays pinned through every environment state");
        inputs.approved = false;
        expect(inputs, VisibilityDecision::AwaitConfirmation, "initial Show still requires explicit approval");
    }

    VisibilityInputs pinned{true, true, true, true, false, false, false};
    pinned.fullscreen = true;
    pinned.autoHide = true;
    pinned.geometryUnchanged = false;
    pinned.taskbarAvailable = false;
    pinned.suspend = true;
    expect(pinned, VisibilityDecision::Show, "combined fullscreen shell DPI and power changes never self-hide");
    pinned.requested = false;
    expect(pinned, VisibilityDecision::Hidden, "manual Hide ends pinned visibility");
    pinned.fullscreen = false;
    pinned.autoHide = false;
    pinned.geometryUnchanged = true;
    pinned.taskbarAvailable = true;
    pinned.suspend = false;
    expect(pinned, VisibilityDecision::Hidden, "environment recovery cannot undo manual Hide");

    if (failures != 0) {
        std::cerr << failures << " visibility checks failed.\n";
        return 1;
    }
    std::cout << "VISIBILITY POLICY TEST: PASS\n";
    return 0;
}
