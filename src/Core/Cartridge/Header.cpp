#include "Header.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace R2BOY::Core::Cartridge
{
    namespace
    {
        // O header contém bytes brutos: substitui caracteres não imprimíveis e remove
        // espaços finais antes de expor os textos à interface.
        std::string text(const std::vector<std::uint8_t> &rom, std::size_t begin, std::size_t end)
        {
            std::string result;
            for (auto i = begin; i < end && rom[i] != 0; ++i)
                result += rom[i] >= 32 && rom[i] <= 126 ? static_cast<char>(rom[i]) : '?';
            while (!result.empty() && result.back() == ' ')
                result.pop_back();
            return result;
        }
    }

    Header Header::parse(const std::vector<std::uint8_t> &rom)
    {
        if (rom.size() < MinimumSize)
            throw std::runtime_error("ROM is too small to contain a cartridge header.");

        Header h;
        h.cgbFlag = rom[0x143];
        const bool color = (h.cgbFlag & 0x80) != 0;

        // Em cartuchos CGB, parte do título pode conter o código do fabricante.
        // Como não há um marcador exclusivo para esse campo, usa quatro caracteres
        // alfanuméricos como heurística; um título longo pode ser ambíguo.
        const bool manufacturer = color && std::all_of(rom.begin() + 0x13F, rom.begin() + 0x143,
                                                       [](std::uint8_t c)
                                                       { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); });

        h.title = text(rom, 0x134, manufacturer ? 0x13F : (color ? 0x143 : 0x144));

        if (manufacturer)
            h.manufacturer = text(rom, 0x13F, 0x143);

        h.newLicensee = text(rom, 0x144, 0x146);
        h.sgbFlag = rom[0x146];
        h.cartridgeType = rom[0x147];
        h.romSizeCode = rom[0x148];
        h.ramSizeCode = rom[0x149];
        h.destinationCode = rom[0x14A];
        h.oldLicensee = rom[0x14B];
        h.version = rom[0x14C];
        h.headerChecksum = rom[0x14D];
        h.globalChecksum = static_cast<std::uint16_t>((rom[0x14E] << 8) | rom[0x14F]);

        // Os códigos comuns dobram a capacidade a partir de 32 KiB. Os códigos
        // legados 0x52–0x54 representam quantidades específicas de bancos de 16 KiB.
        if (h.romSizeCode <= 8)
            h.romSize = std::size_t{32768} << h.romSizeCode;
        else if (h.romSizeCode >= 0x52 && h.romSizeCode <= 0x54)
        {
            constexpr std::size_t banks[] = {72, 80, 96};
            h.romSize = banks[h.romSizeCode - 0x52] * 16384;
        }

        constexpr std::size_t ramSizes[] = {0, 2048, 8192, 32768, 131072, 65536};
        if (h.ramSizeCode < std::size(ramSizes))
            h.ramSize = ramSizes[h.ramSizeCode];

        // O MBC2 possui 512 células de quatro bits, independentemente do código de RAM.
        if (h.cartridgeType == 0x05 || h.cartridgeType == 0x06)
            h.ramSize = 512;

        constexpr std::array<std::uint8_t, 48> logo = {
            0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
            0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
            0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
            0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E};

        h.logoValid = std::equal(logo.begin(), logo.end(), rom.begin() + 0x104);

        // A conversão para 8 bits mantém o resultado módulo 256 a cada subtração.
        std::uint8_t checksum = 0;
        for (std::size_t i = 0x134; i <= 0x14C; ++i)
            checksum = static_cast<std::uint8_t>(checksum - rom[i] - 1);
        h.headerChecksumValid = checksum == h.headerChecksum;

        // O checksum global soma a imagem inteira, exceto os dois bytes que o armazenam.
        std::uint16_t sum = 0;
        for (std::size_t i = 0; i < rom.size(); ++i)
            if (i != 0x14E && i != 0x14F)
                sum = static_cast<std::uint16_t>(sum + rom[i]);
        h.globalChecksumValid = sum == h.globalChecksum;

        return h;
    }

    const char *Header::compatibility() const
    {
        if (cgbFlag == 0xC0)
            return "Game Boy Color only";
        if ((cgbFlag & 0x80) != 0)
            return "Game Boy / Game Boy Color";
        return "Game Boy";
    }

    const char *Header::typeName() const
    {
        switch (cartridgeType)
        {
        case 0x00:
            return "ROM ONLY";
        case 0x01:
            return "MBC1";
        case 0x02:
            return "MBC1+RAM";
        case 0x03:
            return "MBC1+RAM+BATTERY";
        case 0x05:
            return "MBC2";
        case 0x06:
            return "MBC2+BATTERY";
        case 0x08:
            return "ROM+RAM";
        case 0x09:
            return "ROM+RAM+BATTERY";
        case 0x0B:
            return "MMM01";
        case 0x0C:
            return "MMM01+RAM";
        case 0x0D:
            return "MMM01+RAM+BATTERY";
        case 0x0F:
            return "MBC3+TIMER+BATTERY";
        case 0x10:
            return "MBC3+TIMER+RAM+BATTERY";
        case 0x11:
            return "MBC3";
        case 0x12:
            return "MBC3+RAM";
        case 0x13:
            return "MBC3+RAM+BATTERY";
        case 0x19:
            return "MBC5";
        case 0x1A:
            return "MBC5+RAM";
        case 0x1B:
            return "MBC5+RAM+BATTERY";
        case 0x1C:
            return "MBC5+RUMBLE";
        case 0x1D:
            return "MBC5+RUMBLE+RAM";
        case 0x1E:
            return "MBC5+RUMBLE+RAM+BATTERY";
        case 0x20:
            return "MBC6";
        case 0x22:
            return "MBC7+SENSOR+RUMBLE+RAM+BATTERY";
        case 0xFC:
            return "POCKET CAMERA";
        case 0xFD:
            return "BANDAI TAMA5";
        case 0xFE:
            return "HuC3";
        case 0xFF:
            return "HuC1+RAM+BATTERY";
        default:
            return "Unknown cartridge type";
        }
    }
}