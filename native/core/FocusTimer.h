#pragma once

#include <cstdint>

namespace w11widget::native {
enum class FocusState { Ready, Running, Paused, Completed };
class FocusTimer {
public:
    explicit FocusTimer(int durationMinutes = 25);
    bool configure(int durationMinutes);
    void start(std::int64_t monotonicMilliseconds);
    void pause(std::int64_t monotonicMilliseconds);
    void reset();
    void update(std::int64_t monotonicMilliseconds);
    // Hiding/suspending uses pause(); resuming requires explicit start(). No scheduler is owned here.
    FocusState state() const noexcept { return state_; }
    bool running() const noexcept { return state_ == FocusState::Running; }
    int remainingSeconds() const noexcept;
    int durationMinutes() const noexcept { return durationMinutes_; }
private:
    int durationMinutes_ = 25;
    std::int64_t remainingMilliseconds_ = 25 * 60 * 1000;
    std::int64_t startedAt_ = 0;
    FocusState state_ = FocusState::Ready;
};
} // namespace w11widget::native
