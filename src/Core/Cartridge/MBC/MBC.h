#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace R2BOY::Core::Cartridge::MBC
{
    /**
     * Mantém os registradores de bancos, sem possuir os bytes de ROM/RAM.
     * Cartridge aplica os bancos às memórias. Suporta ROM simples, MBC1 padrão,
     * MBC2, MBC3 sem RTC e MBC5. Regras: Pan Docs, capítulos sobre cada MBC.
     */
    class MBC
    {
    public:
        enum class Kind
        {
            ROMOnly,
            MBC1,
            MBC2,
            MBC3,
            MBC5
        };
        MBC() = default;

        // Decodifica o tipo do header; lança runtime_error para tipos não suportados.
        explicit MBC(std::uint8_t cartridgeType);

        // Reinicia registradores; não altera a RAM pertencente ao cartucho.
        void reset();
        void write(std::uint16_t address, std::uint8_t value);

        // Banco de 16 KiB para 0000–7FFF, antes de aplicar as linhas de endereço disponíveis.
        std::size_t romBank(std::uint16_t address) const;

        // Banco de 8 KiB ou nullopt se a RAM estiver desabilitada/seleção inválida.
        std::optional<std::size_t> ramBank() const;
        Kind kind() const { return kind_; }
        bool hasRam() const { return hasRam_; }
        bool hasBattery() const { return battery_; }
        bool rumbleActive() const { return rumbleActive_; }

    private:
        Kind kind_ = Kind::ROMOnly;
        bool hasRam_ = false;
        bool battery_ = false;
        bool rumble_ = false;
        bool rumbleActive_ = false;
        bool ramEnabled_ = false;
        std::uint16_t romBank_ = 1;
        std::uint8_t secondaryBank_ = 0;
        bool advancedMode_ = false;
    };
}