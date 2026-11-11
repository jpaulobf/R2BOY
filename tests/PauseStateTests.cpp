#include "System/PauseState.h"
#include <stdexcept>

int main()
{
    R2BOY::System::PauseState state;
    const auto check = [](bool value)
    { if (!value) throw std::runtime_error("pause causes must remain independent"); };
    check(!state.paused());
    state.setManual(true);
    state.setMenu(true);
    state.setMinimized(true);
    state.setMenu(false);
    state.setMinimized(false);
    check(state.paused() && state.manual());
    state.setManual(false);
    check(!state.paused());
    state.setMenu(true);
    state.setManual(true);
    state.setManual(false);
    check(state.paused());
    state.setMinimized(true);
    state.setMenu(false);
    check(state.paused());
    state.setMinimized(false);
    check(!state.paused());
}