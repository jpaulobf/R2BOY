#pragma once

#include "Header.h"

namespace R2BOY::Core::Cartridge
{
    /**
     * @brief Mantém a imagem da ROM e os metadados do cartucho carregado.
     *
     * Recebe os bytes já lidos pela camada System, sem depender de arquivos, SDL ou
     * ImGui. A ROM fica disponível apenas para consulta após o carregamento.
     * Controladores MBC e RAM persistente ainda não estão implementados.
     */
    class Cartridge
    {
    public:

        /**
         * Valida a imagem e substitui o cartucho atual.
         * bytes correspondentes ao tamanho conhecido declarado no header.
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

        // Origem informada na carga; vazia enquanto não houver cartucho.
        const std::string &source() const { return source_; }

    private:
        // As referências retornadas pelos acessores refletem a próxima carga.
        Header header_;
        std::vector<std::uint8_t> rom_;
        std::string source_;
    };
}