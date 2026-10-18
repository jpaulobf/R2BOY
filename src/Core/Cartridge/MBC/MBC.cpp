#include "MBC.h"

#include <stdexcept>

namespace R2BOY::Core::Cartridge::MBC
{
    MBC::MBC(std::uint8_t type)
    {
        switch (type)
        {
        case 0x00:
        case 0x08:
        case 0x09:
            kind_ = Kind::ROMOnly;
            hasRam_ = type != 0;
            battery_ = type == 0x09;
            break;
        case 0x01:
        case 0x02:
        case 0x03:
            kind_ = Kind::MBC1;
            hasRam_ = type != 0x01;
            battery_ = type == 0x03;
            break;
        case 0x05:
        case 0x06:
            kind_ = Kind::MBC2;
            hasRam_ = true;
            battery_ = type == 0x06;
            break;
        case 0x0F:
        case 0x10:
            throw std::runtime_error("MBC3 cartridges with RTC are not supported yet.");
        case 0x11:
        case 0x12:
        case 0x13:
            kind_ = Kind::MBC3;
            hasRam_ = type != 0x11;
            battery_ = type == 0x13;
            break;
        case 0x19:
        case 0x1A:
        case 0x1B:
        case 0x1C:
        case 0x1D:
        case 0x1E:
            kind_ = Kind::MBC5;
            hasRam_ = type != 0x19 && type != 0x1C;
            battery_ = type == 0x1B || type == 0x1E;
            rumble_ = type >= 0x1C;
            break;
        default:
            throw std::runtime_error("Unsupported cartridge controller.");
        }
        reset();
    }

    void MBC::reset()
    {
        ramEnabled_ = kind_ == Kind::ROMOnly;
        romBank_ = 1;
        secondaryBank_ = 0;
        advancedMode_ = false;
        rumbleActive_ = false;
    }

    void MBC::write(std::uint16_t address, std::uint8_t value)
    {
        if (address >= 0x8000 || kind_ == Kind::ROMOnly)
            return;
        // No MBC2, A8 distingue habilitação de RAM da seleção de ROM em toda a faixa.
        if (kind_ == Kind::MBC2)
        {
            if (address >= 0x4000)
                return;
                
            if ((address & 0x0100) == 0)
                ramEnabled_ = (value & 0x0F) == 0x0A;
            else
                romBank_ = (value & 0x0F) == 0 ? 1 : value & 0x0F;
            return;
        }
        if (address < 0x2000)
            ramEnabled_ = (value & 0x0F) == 0x0A;
        else if (address < 0x4000)
        {
            if (kind_ == Kind::MBC5)
            {
                if (address < 0x3000)
                    romBank_ = (romBank_ & 0x100) | value;
                else
                    romBank_ = (romBank_ & 0xFF) | ((value & 1) << 8);
            }
            else
            {
                romBank_ = value & (kind_ == Kind::MBC1 ? 0x1F : 0x7F);
                // Corrige 0 -> 1 antes de ignorar linhas de endereço ausentes.
                if (romBank_ == 0)
                    romBank_ = 1;
            }
        }
        else if (address < 0x6000)
        {
            if (kind_ == Kind::MBC1)
                secondaryBank_ = value & 3;
            else if (kind_ == Kind::MBC3)
                secondaryBank_ = value;
            else
            {
                secondaryBank_ = value & (rumble_ ? 7 : 15);
                rumbleActive_ = rumble_ && (value & 8) != 0;
            }
        }
        else if (kind_ == Kind::MBC1)
            advancedMode_ = (value & 1) != 0;
    }

    std::size_t MBC::romBank(std::uint16_t address) const
    {
        if (kind_ == Kind::ROMOnly)
            return address < 0x4000 ? 0 : 1;

        if (kind_ == Kind::MBC1)
        {
            const auto upper = static_cast<std::size_t>(secondaryBank_) << 5;
            return address < 0x4000 ? (advancedMode_ ? upper : 0) : upper | romBank_;
        }

        return address < 0x4000 ? 0 : romBank_;
    }

    std::optional<std::size_t> MBC::ramBank() const
    {
        if (!hasRam_ || !ramEnabled_)
            return std::nullopt;

        if (kind_ == Kind::MBC3 && secondaryBank_ > 3)
            return std::nullopt;

        if (kind_ == Kind::ROMOnly || kind_ == Kind::MBC2)
            return 0;

        if (kind_ == Kind::MBC1 && !advancedMode_)
            return 0;

        return secondaryBank_;
    }
}
