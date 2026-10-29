#include "Cartridge.h"
#include "Common/RomFile.h"

#include <stdexcept>
#include <utility>
#include <algorithm>

namespace R2BOY::Core::Cartridge
{
    void Cartridge::load(std::vector<std::uint8_t> rom, std::string source)
    {
        Header header = Header::parse(rom);

        if (rom.size() < 32768)
            throw std::runtime_error("ROM is smaller than the minimum 32 KiB cartridge.");

        if (header.romSize != 0 && rom.size() < header.romSize)
            throw std::runtime_error("ROM is truncated: smaller than the size declared in its header.");

        MBC::MBC controller(header.cartridgeType);
        using Kind = MBC::MBC::Kind;
        if (header.romSizeCode > 8)
            throw std::runtime_error("Unsupported ROM size code for bank mapping.");

        // Compilações MBC1M usam outra ligação dos bancos. Reconhece o logo de
        // um segundo jogo no banco 0x10 para evitar tratá-las como MBC1 padrão.
        constexpr std::size_t secondLogo = 16 * 16384 + 0x104;

        if (controller.kind() == Kind::MBC1 && header.logoValid && rom.size() >= secondLogo + 48 &&
            std::equal(rom.begin() + 0x104, rom.begin() + 0x134, rom.begin() + secondLogo))
            throw std::runtime_error("MBC1M multicart wiring is not supported yet.");

        if (controller.hasRam() && header.ramSizeCode > 5 && controller.kind() != Kind::MBC2)
            throw std::runtime_error("Unsupported RAM size code.");

        const auto ramSize = controller.hasRam() ? header.ramSize : 0;
        std::size_t maxRom = 2 * 1024 * 1024, maxRam = 32768;
        if (controller.kind() == Kind::ROMOnly)
        {
            maxRom = 32768;
            maxRam = 8192;
        }

        if (controller.kind() == Kind::MBC2)
        {
            maxRom = 256 * 1024;
            maxRam = 512;
        }

        if (controller.kind() == Kind::MBC5)
        {
            maxRom = 8 * 1024 * 1024;
            maxRam = header.cartridgeType >= 0x1C ? 65536 : 131072;
        }

        if (header.romSize > maxRom || ramSize > maxRam ||
            (controller.kind() == Kind::MBC1 && header.romSize > 512 * 1024 && ramSize > 8192))
            throw std::runtime_error("ROM/RAM sizes exceed this controller's supported wiring.");
        std::vector<std::uint8_t> ram(ramSize, controller.kind() == Kind::MBC2 ? 0x0F : 0xFF);

        // Só substitui o estado após validar a imagem, preservando a ROM anterior em caso de erro.
        auto identity = Common::RomFile::identity(rom);
        header_ = std::move(header);
        rom_ = std::move(rom);
        identity_ = std::move(identity);
        source_ = std::move(source);
        mbc_ = controller;
        ram_ = std::move(ram);
        ramDirty_ = false;
    }

    std::uint8_t Cartridge::read(std::uint16_t address) const
    {
        if (!loaded())
            return 0xFF;

        if (address < 0x8000)
        {
            // Restringe o banco à capacidade declarada, espelhando seleções acima dela.
            const auto banks = header_.romSize / 16384;
            return rom_[(mbc_.romBank(address) % banks) * 16384 + (address & 0x3FFF)];
        }

        if (address < 0xA000 || address >= 0xC000 || ram_.empty())
            return 0xFF;

        const auto bank = mbc_.ramBank();
        if (!bank)
            return 0xFF;

        // A janela A000–BFFF também espelha RAMs menores que 8 KiB, como a do MBC2.
        const auto value = ram_[(*bank * 8192 + (address - 0xA000)) % ram_.size()];

        return mbc_.kind() == MBC::MBC::Kind::MBC2 ? value | 0xF0 : value;
    }

    void Cartridge::write(std::uint16_t address, std::uint8_t value)
    {
        if (!loaded())
            return;

        if (address < 0x8000)
        {
            mbc_.write(address, value);
            return;
        }

        if (address < 0xA000 || address >= 0xC000 || ram_.empty())
            return;

        const auto bank = mbc_.ramBank();
        if (!bank)
            return;

        auto &cell = ram_[(*bank * 8192 + (address - 0xA000)) % ram_.size()];
        if (mbc_.kind() == MBC::MBC::Kind::MBC2)
            value &= 0x0F;

        if (cell != value)
        {
            cell = value;
            ramDirty_ = true;
        }
    }

    void Cartridge::restoreBatteryRam(std::vector<std::uint8_t> data)
    {
        if (!hasBattery() || data.size() != ram_.size())
            throw std::runtime_error("Save RAM size does not match the battery-backed cartridge.");

        if (mbc_.kind() == MBC::MBC::Kind::MBC2)
            for (auto &value : data)
                value &= 0x0F;

        ram_ = std::move(data);
        ramDirty_ = false;
    }
}