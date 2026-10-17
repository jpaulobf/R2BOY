#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace R2BOY::Core::Cartridge
{
    /**
     * Representa os metadados e os resultados de verificação de uma ROM.
     * Os campos do header ocupam os endereços 0x0100–0x014F da imagem.
     * Preserva os códigos originais e também calcula tamanhos em bytes e checksums.
     * As regras seguem https://gbdev.io/pandocs/The_Cartridge_Header.html.
     */
    struct Header
    {
        // Quantidade mínima de bytes para acessar até o último campo do header.
        static constexpr std::size_t MinimumSize = 0x150;

        /**
         * Interpreta o header e verifica o logo e os checksums.
         * Não valida o tamanho completo do cartucho; essa etapa cabe a Cartridge.
         * Divergências nas verificações são sinalizadas nos campos *Valid, sem lançar
         * exceção. O checksum global considera todos os bytes recebidos da ROM.
         */
        static Header parse(const std::vector<std::uint8_t> &rom);

        // Título sanitizado; compartilha espaço com fabricante e flag CGB.
        std::string title;

        // Código de quatro caracteres, quando identificado pela heurística do parser.
        std::string manufacturer;

        // Código novo de licença (0x0144–0x0145); aplicável quando oldLicensee é 0x33.
        std::string newLicensee;

        // Flag CGB (0x0143); usada para interpretar a compatibilidade GB/GBC.
        std::uint8_t cgbFlag = 0;

        // Flag SGB (0x0146); o valor 0x03 indica suporte ao Super Game Boy.
        std::uint8_t sgbFlag = 0;

        // Código do controlador e recursos do cartucho (0x0147), sem instanciar MBC.
        std::uint8_t cartridgeType = 0;

        // Código de tamanho da ROM (0x0148), convertido para bytes em romSize.
        std::uint8_t romSizeCode = 0;

        // Código de tamanho da RAM (0x0149), interpretado em ramSize.
        std::uint8_t ramSizeCode = 0;

        // Código de destino (0x014A); 0x00 representa Japão e 0x01, exterior.
        std::uint8_t destinationCode = 0;

        // Código antigo de licença (0x014B); 0x33 seleciona o código novo.
        std::uint8_t oldLicensee = 0;

        // Número de versão da ROM (0x014C).
        std::uint8_t version = 0;

        // Checksum de 8 bits armazenado em 0x014D, referente a 0x0134–0x014C.
        std::uint8_t headerChecksum = 0;

        // Checksum global armazenado em 0x014E–0x014F, com byte mais significativo primeiro.
        std::uint16_t globalChecksum = 0;

        // Tamanho declarado em bytes; zero indica código desconhecido.
        std::size_t romSize = 0;

        // Capacidade da RAM em bytes; zero pode indicar ausência ou código desconhecido.
        // Para MBC2, representa 512 células de quatro bits, não 512 bytes de oito bits.
        std::size_t ramSize = 0;

        // Resultado da comparação dos 48 bytes do logo em 0x0104–0x0133.
        bool logoValid = false;

        // Indica se o checksum calculado do header coincide com o armazenado.
        bool headerChecksumValid = false;

        // Indica se a soma da ROM, excluindo 0x014E e 0x014F, coincide com a armazenada.
        bool globalChecksumValid = false;

        // Descrição estática da compatibilidade; não depende da extensão do arquivo.
        const char *compatibility() const;

        // Nome estático do tipo de cartucho; códigos não reconhecidos têm descrição genérica.
        const char *typeName() const;
    };
}