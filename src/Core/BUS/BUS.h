#pragma once

#include "Core/Cartridge/Cartridge.h"
#include "Core/Memory/Memory.h"
#include "Core/IO/Interrupts.h"
#include "Core/IO/Serial.h"
#include "Core/IO/Timer.h"
#include <cstdint>

namespace R2BOY::Core::BUS
{
    /**
     * Roteia ROM/MBC e RAM externa ao cartucho, RAM interna à Memory e IF/IE
     * ao controlador de interrupções, SB/SC à serial e FF04–FF07 ao timer. Os componentes devem viver
     * mais que o BUS. Os demais registradores de periféricos, boot ROM e temporização
     * serão conectados depois;
     * leituras ainda não implementadas retornam 0xFF e escritas são ignoradas.
     */
    class BUS
    {
    public:
        // Recebe os componentes pertencentes à Board, sem assumir sua posse.
        BUS(Cartridge::Cartridge &cartridge, Memory::Memory &memory, IO::Interrupts &interrupts,
            IO::Serial &serial, IO::Timer &timer);

        // Acesso de leitura ao barramento.
        std::uint8_t read(std::uint16_t address) const;

        // Acesso de escrita ao barramento.
        void write(std::uint16_t address, std::uint8_t value);
        // A CPU consulta os pedidos habilitados e reconhece apenas o de maior prioridade.
        std::uint8_t pendingInterrupts() const { return interrupts_.pending(); }
        void acknowledgeInterrupt(unsigned bit) { interrupts_.acknowledge(bit); }
        // Evento interno de STOP: reseta DIV sem inventar um acesso extra da CPU.
        void stopDivider() { timer_.write(0xFF04, 0); }

    private:
        Cartridge::Cartridge &cartridge_;
        Memory::Memory &memory_;
        IO::Interrupts &interrupts_;
        IO::Serial &serial_;
        IO::Timer &timer_;
    };
}