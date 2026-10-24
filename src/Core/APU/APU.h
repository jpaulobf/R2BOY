#pragma once

#include "Core/StateArchive.h"
#include <cstdint>

namespace R2BOY::Core::APU
{
    /**
     * Controle global de áudio (NR50/NR51 e alimentação por NR52).
     * Os canais, seus indicadores de atividade e a geração de amostras ainda
     * não estão implementados. A saída física cabe a System/Audio.
     */
    class APU
    {
    public:
        void reset();
        std::uint8_t read(std::uint16_t address) const;
        void write(std::uint16_t address, std::uint8_t value);

    private:
        friend class R2BOY::Core::StateArchive;
        void state(StateArchive &archive) { archive(powered_, volume_, panning_); }
        bool powered_ = true;
        std::uint8_t volume_ = 0x77;
        std::uint8_t panning_ = 0xF3;
    };
}
