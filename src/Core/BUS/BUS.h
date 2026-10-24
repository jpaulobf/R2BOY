#pragma once

#include "Core/Cartridge/Cartridge.h"
#include "Core/Memory/Memory.h"
#include "Core/IO/Interrupts.h"
#include "Core/IO/Serial.h"
#include "Core/IO/Timer.h"
#include "Core/IO/DMA.h"
#include "Core/IO/Joypad.h"
#include "Core/IO/SpeedSwitch.h"
#include "Core/PPU/PPU.h"
#include "Core/APU/APU.h"
#include <cstdint>

namespace R2BOY::Core::BUS
{
    /**
     * Roteia ROM/MBC e RAM externa ao cartucho, RAM interna à Memory e IF/IE
     * ao controlador de interrupções, SB/SC à serial, FF04–FF07 ao timer e FF46
     * ao DMA, FF00 ao joypad, os registradores LCD à PPU e FF24–FF26 à APU.
     * Os componentes devem viver mais que o BUS. Os demais registradores
     * de periféricos e boot ROM serão conectados depois;
     * leituras ainda não implementadas retornam 0xFF e escritas são ignoradas.
     */
    class BUS
    {
    public:
        // Recebe os componentes pertencentes à Board, sem assumir sua posse.
        BUS(Cartridge::Cartridge &cartridge, Memory::Memory &memory, IO::Interrupts &interrupts,
            IO::Serial &serial, IO::Timer &timer, IO::DMA &dma, PPU::PPU &ppu, IO::Joypad &joypad,
            IO::SpeedSwitch &speedSwitch, APU::APU &apu);

        // Acessos da CPU, sujeitos aos bloqueios de DMA e PPU.
        std::uint8_t read(std::uint16_t address) const;

        // Acesso de escrita ao barramento.
        void write(std::uint16_t address, std::uint8_t value);

        // A CPU consulta os pedidos habilitados e reconhece apenas o de maior prioridade.
        std::uint8_t pendingInterrupts() const { return interrupts_.pending(); }

        // IE e IF são amostrados em momentos diferentes durante a entrada da IRQ.
        std::uint8_t enabledInterrupts() const { return interrupts_.readIE() & 0x1F; }

        // IF é amostrado durante a entrada da IRQ; retorna os pedidos ativos.
        std::uint8_t requestedInterrupts() const { return interrupts_.readIF() & 0x1F; }

        // Limpa em IF somente a fonte reconhecida pela CPU, identificada de 0 a 4.
        void acknowledgeInterrupt(unsigned bit) { interrupts_.acknowledge(bit); }

        // Evento interno de STOP: reseta DIV sem inventar um acesso extra da CPU.
        // Retorna true se STOP iniciou a troca de velocidade em vez de standby.
        bool stop()
        {
            const auto previousCounter = timer_.systemCounter();
            timer_.write(0xFF04, 0);
            serial_.clock(previousCounter, timer_.systemCounter(), interrupts_);
            return speedSwitch_.beginSwitch();
        }

        // Consulta se a CPU está no processo de troca de velocidade.
        bool switchingSpeed() const { return speedSwitch_.switching(); }

    private:
        friend class IO::DMA;
        // Roteia a leitura sem os bloqueios da CPU; mantém o bloqueio de VRAM pela PPU.
        std::uint8_t readDMA(std::uint16_t address) const;
        // Porta de escrita do DMA: atinge OAM sem passar pelos bloqueios da CPU.
        void writeOAM(std::uint16_t address, std::uint8_t value) { memory_.write(address, value); }
        Cartridge::Cartridge &cartridge_;
        Memory::Memory &memory_;
        IO::Interrupts &interrupts_;
        IO::Serial &serial_;
        IO::Timer &timer_;
        IO::DMA &dma_;
        PPU::PPU &ppu_;
        IO::Joypad &joypad_;
        IO::SpeedSwitch &speedSwitch_;
        APU::APU &apu_;
    };
}