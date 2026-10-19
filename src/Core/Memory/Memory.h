#pragma once

#include <array>
#include <cstdint>

namespace R2BOY::Core::Memory
{
    /**
     * Armazena WRAM, HRAM, VRAM e OAM; ROM e RAM do cartucho ficam em Cartridge.
     * Implementa o espelhamento da WRAM e os bancos CGB (VBK/SVBK).
     * Os bloqueios de DMA/PPU ficam no BUS. Boot ROM ainda não é emulada.
     * A inicialização em zero é determinística, não uma
     * reprodução dos valores indeterminados da RAM ao ligar o hardware.
     */
    class Memory
    {
    public:
        // Limpa as memórias voláteis e seleciona os bancos iniciais (VRAM 0, WRAM 1).
        // Em modo DMG, ignora escritas nos registradores de bancos exclusivos do CGB.
        void reset(bool cgbMode = false);

        // Endereços aceitos: 8000–9FFF, C000–FDFF, FE00–FE9F, FF80–FFFE,
        // e FF4F/FF70 em modo CGB. Outras leituras retornam 0xFF; escritas são ignoradas.
        std::uint8_t read(std::uint16_t address) const;
        void write(std::uint16_t address, std::uint8_t value);

        // Porta interna da PPU: banco explícito, independente de VBK e dos bloqueios da CPU.
        std::uint8_t readVRAM(std::uint16_t address, unsigned bank) const;

    private:
        std::array<std::uint8_t, 0x4000> vram_{};
        std::array<std::uint8_t, 0x8000> wram_{};
        std::array<std::uint8_t, 0xA0> oam_{};
        std::array<std::uint8_t, 0x7F> hram_{};
        bool cgbMode_ = false;
        std::uint8_t vramBank_ = 0;
        std::uint8_t wramBank_ = 1;
    };
}
