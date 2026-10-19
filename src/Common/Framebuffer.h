#pragma once

#include "Constants.h"
#include <array>
#include <cstdint>

namespace R2BOY::Common
{
    // Pixels em ordem de linhas, no formato 0xAARRGGBB, na resolução nativa.
    using Framebuffer = std::array<std::uint32_t, Constants::ScreenWidth * Constants::ScreenHeight>;
}
