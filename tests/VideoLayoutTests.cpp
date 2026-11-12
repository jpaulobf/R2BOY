#include "Window/VideoLayout.h"
#include <cmath>
#include <stdexcept>

static void check(bool value)
{
    if (!value)
        throw std::runtime_error("Incorrect video layout");
}

int main()
{
    const auto aspect = R2Window::videoLayout(1920, 1080, 256, 192, false);
    check(aspect.x == 240 && aspect.y == 0 && aspect.width == 1440 && aspect.height == 1080);
    const auto stretched = R2Window::videoLayout(1920, 1080, 160, 144, true);
    check(stretched.x == 0 && stretched.y == 0 && stretched.width == 1920 && stretched.height == 1080);
    const auto tall = R2Window::videoLayout(900, 1200, 160, 144, false);
    check(tall.x == 0 && std::abs(tall.y - 195) < 0.01f && tall.width == 900 && tall.height == 810);
    const auto nes = R2Window::videoLayoutInteger(1920, 1080, 8.0 / 7.0, false);
    check(nes.x == 343 && nes.y == 0 && nes.width == 1234 && nes.height == 1080);
    const auto cropped = R2Window::videoLayoutInteger(1920, 1080, 60.0 / 49.0, false);
    check(cropped.x == 299 && cropped.y == 0 && cropped.width == 1322 && cropped.height == 1080);
    check(R2Window::videoLayout(0, 1080, 256, 192, false).width == 0);
}