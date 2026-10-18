#pragma once

#include "Core/Cartridge/Cartridge.h"
#include "Core/Memory/Memory.h"
#include <cstdint>

namespace R2BOY::Core::BUS
{
    /**
     * Roteia ROM/MBC e RAM externa ao cartucho, e RAM interna à Memory.
     * Os componentes referenciados devem viver mais que o BUS. Os registradores
     * de periféricos, boot ROM e efeitos de temporização serão conectados depois;
     * leituras ainda não implementadas retornam 0xFF e escritas são ignoradas.
     */
    class BUS
    {
    public:
        // Construtor que recebe referências para o cartucho e a memória.
        BUS(Cartridge::Cartridge &cartridge, Memory::Memory &memory);

        // Acesso de leitura ao barramento.
        std::uint8_t read(std::uint16_t address) const;

        // Acesso de escrita ao barramento.
        void write(std::uint16_t address, std::uint8_t value);

    private:
        Cartridge::Cartridge &cartridge_;
        Memory::Memory &memory_;
    };
}
