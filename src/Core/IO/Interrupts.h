#pragma once

#include <cstdint>

namespace R2BOY::Core::IO
{
    /**
     * Mantém IF/IE e os pedidos de interrupção; IME pertence à CPU.
     * Os cinco bits representam VBlank, STAT, timer, serial e joypad, nessa prioridade.
     * request() permite conectar os periféricos sem expor os bytes internos.
     */
    class Interrupts
    {
    public:
        void reset(bool afterBoot = false)
        {
            enabled_ = 0;
            requested_ = afterBoot ? 1 : 0;
        }
        std::uint8_t readIF() const { return requested_ | 0xE0; }
        std::uint8_t readIE() const { return enabled_; }
        void writeIF(std::uint8_t value) { requested_ = value & 0x1F; }
        void writeIE(std::uint8_t value) { enabled_ = value; }
        std::uint8_t pending() const { return enabled_ & requested_ & 0x1F; }
        void request(unsigned bit)
        {
            if (bit < 5)
                requested_ |= static_cast<std::uint8_t>(1 << bit);
        }
        void acknowledge(unsigned bit)
        {
            if (bit < 5)
                requested_ &= static_cast<std::uint8_t>(~(1 << bit));
        }

    private:
        std::uint8_t enabled_ = 0;
        std::uint8_t requested_ = 0;
    };
}