#pragma once

#include "Core/StateArchive.h"
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

        // Estado padrão de ROM simples, sem RAM nem bateria, antes da carga do cartucho.
        MBC() = default;

        // Decodifica o tipo do header; lança runtime_error para tipos não suportados.
        explicit MBC(std::uint8_t cartridgeType);

        // Reinicia registradores; não altera a RAM pertencente ao cartucho.
        void reset();

        // Interpreta escritas em 0000–7FFF como comandos de bancos, RAM e rumble.
        void write(std::uint16_t address, std::uint8_t value);

        // Banco de 16 KiB para 0000–7FFF, antes de aplicar as linhas de endereço disponíveis.
        std::size_t romBank(std::uint16_t address) const;

        // Banco de 8 KiB ou nullopt se a RAM estiver desabilitada/seleção inválida.
        std::optional<std::size_t> ramBank() const;

        // Família do controlador decodificada a partir do tipo do cartucho.
        Kind kind() const { return kind_; }

        // Recurso de RAM declarado pelo tipo, independente de estar habilitada agora.
        bool hasRam() const { return hasRam_; }

        // Informa se o tipo de cartucho possui bateria para persistência da RAM.
        bool hasBattery() const { return battery_; }

        // Estado lógico do motor MBC5; não aciona dispositivos físicos da plataforma.
        bool rumbleActive() const { return rumbleActive_; }

    private:
        friend class R2BOY::Core::StateArchive;
        void state(StateArchive &archive)
        {
            // Propriedades fixas vêm da ROM carregada, nunca do arquivo de estado.
            const auto expectedKind = kind_;
            const bool expectedRam = hasRam_, expectedBattery = battery_, expectedRumble = rumble_;
            archive(kind_, hasRam_, battery_, rumble_, rumbleActive_, ramEnabled_, romBank_);
            archive(secondaryBank_, advancedMode_);
            if (archive.reading() && (kind_ != expectedKind || hasRam_ != expectedRam ||
                                      battery_ != expectedBattery || rumble_ != expectedRumble ||
                                      (rumbleActive_ && !rumble_)))
                throw std::runtime_error("Invalid cartridge controller state.");
        }
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