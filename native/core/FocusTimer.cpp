#include "FocusTimer.h"
#include <algorithm>

namespace w11widget::native {
FocusTimer::FocusTimer(int minutes) { configure(minutes); }
bool FocusTimer::configure(int minutes) {
    if (minutes < 1 || minutes > 180) return false;
    if (durationMinutes_ != minutes) { durationMinutes_ = minutes; reset(); }
    return true;
}
void FocusTimer::start(std::int64_t now) {
    if (running()) return;
    if (remainingMilliseconds_ == 0) reset();
    startedAt_ = now;
    state_ = FocusState::Running;
}
void FocusTimer::update(std::int64_t now) {
    if (!running()) return;
    // Ignore a regressing supplied clock; the platform must pass a monotonic source.
    if (now < startedAt_) return;
    const auto elapsed = now - startedAt_;
    remainingMilliseconds_ = elapsed >= remainingMilliseconds_ ? 0 : remainingMilliseconds_ - elapsed;
    startedAt_ = now;
    if (remainingMilliseconds_ == 0) state_ = FocusState::Completed;
}
void FocusTimer::pause(std::int64_t now) {
    update(now);
    if (running()) state_ = FocusState::Paused;
}
void FocusTimer::reset() {
    remainingMilliseconds_ = static_cast<std::int64_t>(durationMinutes_) * 60 * 1000;
    state_ = FocusState::Ready;
}
int FocusTimer::remainingSeconds() const noexcept {
    return static_cast<int>((remainingMilliseconds_ + 999) / 1000);
}
}
