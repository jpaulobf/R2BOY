#pragma once

#include "Core/StateArchive.h"
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
     * No DMG, leituras no barramento ocupado veem o byte transferido; OAM
     * retorna FF. Escritas em conflito são ignoradas. No CGB, leituras em
     * conflito ainda usam FF; corrupção por revisão e conflitos com a PPU
     * não são emulados.
     * Referências: gbdev.io/pandocs/OAM_DMA_Transfer.html e os testes
     * acceptance/oam_dma_start.s e oam_dma_restart.s de Gekkio/mooneye-test-suite.
     */
    class DMA
    {
    public:
        // Modelo físico, inclusive quando o CGB executa um cartucho DMG.
        void reset(bool colorHardware = false);

        // Último valor escrito em FF46, inclusive durante o atraso de partida.
        std::uint8_t read() const { return register_; }

        // Agenda a cópia da página de 256 bytes indicada para os 160 bytes de OAM.
        void start(std::uint8_t page);

        // Avança um T-cycle; a cópia efetiva ocorre uma vez a cada quatro chamadas.
        void tick(BUS::BUS &bus);

        // Indica transferência efetiva; o atraso inicial isolado não ativa o bloqueio.
        bool active() const { return active_; }

        // Inclui a partida: rotinas de espera contam ciclos desde a escrita em FF46.
        bool inProgress() const { return active_ || startup_ != 0; }

        // Consulta conflitos da CPU conforme o modelo físico e o barramento de origem.
        bool blocksCPU(std::uint16_t address) const;

        // Valor observado em uma leitura que blocksCPU() identificou como conflito.
        std::uint8_t readCPUConflict(std::uint16_t address) const;

    private:
        friend class R2BOY::Core::StateArchive;
        void state(StateArchive &archive)
        {
            const bool expectedHardware = colorHardware_;
            archive(register_, sourcePage_, index_, startup_, phase_, active_, colorHardware_, data_);
            if (archive.reading() && (colorHardware_ != expectedHardware || index_ > 160 || startup_ > 8 ||
                                      phase_ > 4 || (active_ && phase_ == 0)))
                throw std::runtime_error("Invalid OAM DMA state.");
        }
        std::uint8_t register_ = 0xFF;
        std::uint8_t sourcePage_ = 0;
        unsigned index_ = 0;
        unsigned startup_ = 0;
        unsigned phase_ = 0;
        bool active_ = false;
        bool colorHardware_ = false;
        std::uint8_t data_ = 0xFF;
    };
}