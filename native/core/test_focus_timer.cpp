#include "FocusTimer.h"
#include <iostream>
#include <cstdlib>
using namespace w11widget::native;
int main() {
    int checks = 0;
    const auto check = [&](bool condition) { ++checks; if (!condition) { std::cerr << "FAIL focus check " << checks << '\n'; std::exit(1); } };
    FocusTimer timer(1); check(timer.state() == FocusState::Ready && timer.remainingSeconds() == 60);
    timer.update(100000); check(timer.remainingSeconds() == 60);
    timer.start(1000); timer.update(1501); check(timer.running() && timer.remainingSeconds() == 60);
    timer.update(2000); check(timer.remainingSeconds() == 59);
    timer.start(3000); timer.update(4000); check(timer.remainingSeconds() == 57);
    timer.pause(4500); check(timer.state() == FocusState::Paused && timer.remainingSeconds() == 57);
    timer.update(500000); check(timer.remainingSeconds() == 57);
    timer.start(500000); timer.update(500500); check(timer.remainingSeconds() == 56);
    timer.update(499000); check(timer.remainingSeconds() == 56);
    check(!timer.configure(0) && !timer.configure(181) && timer.running());
    check(timer.configure(1) && timer.running());
    check(timer.configure(2) && timer.state() == FocusState::Ready && timer.remainingSeconds() == 120);
    timer.start(0); timer.update(120000); check(timer.state() == FocusState::Completed && timer.remainingSeconds() == 0);
    timer.pause(130000); check(timer.state() == FocusState::Completed);
    timer.start(130000); check(timer.running() && timer.remainingSeconds() == 120);
    timer.reset(); check(timer.state() == FocusState::Ready && timer.remainingSeconds() == 120);
    timer.start(0); timer.pause(30000); timer.start(999000); timer.update(1089000); check(timer.state() == FocusState::Completed);
    std::cout << "Native focus timer: " << checks << " checks PASS\n";
}
