#pragma once

#include <array>
#include <cstdint>

namespace R2BOY::Common
{
    // Dados de diagnóstico copiados entre instruções; não representam leituras da CPU.
    struct TileSnapshot
    {
        static constexpr unsigned TileCount = 384;
        static constexpr unsigned TileBytes = TileCount * 16;
        bool loaded = false;
        bool color = false;
        std::array<std::array<std::uint8_t, TileBytes>, 2> banks{};
        // Cores ARGB já convertidas pela PPU: BG/OBJ, paleta, índice de cor.
        std::array<std::array<std::array<std::uint32_t, 4>, 8>, 2> palettes{};
    };
}
