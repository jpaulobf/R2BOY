#pragma once

#include <cstdint>

namespace R2BOY::Common
{
    // Tipos de largura fixa para representar valores e registradores do hardware.
    // Os prefixos u e s indicam, respectivamente, valores sem sinal e com sinal.
    using u8 = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;
    using s8 = std::int8_t;
    using s16 = std::int16_t;
}
