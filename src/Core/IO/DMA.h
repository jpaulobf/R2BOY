#pragma once

#include <cstdint>

namespace R2BOY::Core::BUS
{
    class BUS;
}

namespace R2BOY::Core::IO
{
    /**
     * Copia 160 bytes, um por M-cycle, após o atraso de partida. Reiniciar
     * preserva a transferência anterior até a nova partida.
     * Conflitos usam leituras 0xFF e escritas ignoradas; valores de barramento
     * aberto/corrupção por revisão do hardware e conflitos com a PPU não são emulados.
     * Referências: gbdev.io/pandocs/OAM_DMA_Transfer.html e os testes
     * acceptance/oam_dma_start.s e oam_dma_restart.s de Gekkio/mooneye-test-suite.
     */
    class DMA
    {
    public:
        // Modelo físico, inclusive quando o CGB executa um cartucho DMG.
        void reset(bool colorHardware = false);
        std::uint8_t read() const { return register_; }
        void start(std::uint8_t page);
        void tick(BUS::BUS &bus);
        bool active() const { return active_; }
        bool blocksCPU(std::uint16_t address) const;

    private:
        std::uint8_t register_ = 0xFF;
        std::uint8_t sourcePage_ = 0;
        unsigned index_ = 0;
        unsigned startup_ = 0;
        unsigned phase_ = 0;
        bool active_ = false;
        bool colorHardware_ = false;
    };
}