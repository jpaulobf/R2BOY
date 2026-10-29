#pragma once

#include "Core/StateArchive.h"
#include "Header.h"
#include "MBC/MBC.h"

namespace R2BOY::Core::Cartridge
{
    /**
     * @brief Mantém a imagem da ROM e os metadados do cartucho carregado.
     *
     * Recebe os bytes já lidos pela camada System, sem depender de arquivos, SDL ou
     * ImGui. A ROM fica disponível apenas para consulta após o carregamento.
     * O MBC seleciona bancos; a RAM é alocada conforme tipo e tamanho do header.
     * A persistência em disco cabe a System/SaveFile, mantendo o Core sem I/O.
     */
    class Cartridge
    {
    public:
        /**
         * Valida imagem, tipo de controlador e capacidades antes de substituir o
         * cartucho atual. Rejeita ROM truncada, controlador/tamanhos não suportados
         * e combinações incompatíveis de ROM/RAM; aloca RAM apenas quando o tipo
         * de cartucho a fornece. Inicializa RAM comum em 0xFF e células MBC2 em 0x0F.
         * Em caso de falha na validação, preserva o cartucho anterior. Divergências
         * de logo e checksums são registradas no Header e não impedem a carga.
         */
        void load(std::vector<std::uint8_t> rom, std::string source);

        // Indica se uma ROM foi carregada com sucesso.
        bool loaded() const { return !rom_.empty(); }

        // Metadados da ROM; consulte loaded() antes de interpretá-los.
        const Header &header() const { return header_; }

        // Bytes originais, sem cópia; o vetor permanece vazio antes da primeira carga.
        const std::vector<std::uint8_t> &rom() const { return rom_; }
        const std::string &identity() const { return identity_; }

        // Origem informada na carga; vazia enquanto não houver cartucho.
        const std::string &source() const { return source_; }

        // Acessos nas faixas 0000–7FFF (ROM/MBC) e A000–BFFF (RAM do cartucho).
        // Leituras sem cartucho/RAM ou fora dessas faixas retornam 0xFF.
        std::uint8_t read(std::uint16_t address) const;

        // Escritas na faixa de ROM configuram o MBC; na RAM habilitada, atualizam os bytes.
        void write(std::uint16_t address, std::uint8_t value);

        // Reinicia bancos e habilitação da RAM, preservando seus bytes.
        void resetBanks() { mbc_.reset(); }

        // Consulta bancos e recursos do controlador, sem permitir reconfiguração externa.
        const MBC::MBC &controller() const { return mbc_; }

        // MBC2 usa um byte por célula de quatro bits, inclusive no arquivo .sav.
        const std::vector<std::uint8_t> &ram() const { return ram_; }

        // Exige cartucho carregado e um tipo de controlador com bateria.
        bool hasBattery() const { return loaded() && mbc_.hasBattery(); }

        // Apenas mudanças efetivas nos bytes habilitados marcam a RAM como modificada.
        bool ramDirty() const { return ramDirty_; }

        // Confirma a persistência dos bytes atuais; deve ser chamado após salvar com sucesso.
        void markRamSaved() { ramDirty_ = false; }

        // Exige bateria e tamanho exato; valida antes de alterar o conteúdo.
        void restoreBatteryRam(std::vector<std::uint8_t> data);

    private:
        friend class R2BOY::Core::StateArchive;
        void state(StateArchive &archive)
        {
            archive(mbc_, ram_);
            if (archive.reading())
                ramDirty_ = true;
        }
        // As referências retornadas pelos acessores refletem a próxima carga.
        Header header_;
        std::vector<std::uint8_t> rom_;
        std::string identity_;
        std::string source_;
        MBC::MBC mbc_;
        std::vector<std::uint8_t> ram_;
        bool ramDirty_ = false;
    };
}