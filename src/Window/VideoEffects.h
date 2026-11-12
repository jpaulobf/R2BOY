#pragma once
#include "Common/Framebuffer.h"
#include "DisplayMenu.h"

namespace R2BOY::Window
{
    // Retorna o quadro original ou um buffer processado, sem alterar a origem.
    const Common::Framebuffer &prepareVideoFrame(const Common::Framebuffer &framebuffer, bool gameLoaded,
                                                 bool dmgMode, const DisplaySettings &display, const PalettePreset &preset, Common::Framebuffer &output);
}