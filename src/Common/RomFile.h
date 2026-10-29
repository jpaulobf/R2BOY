#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace R2BOY::Common
{
    /**
     * Lê arquivos de ROM e membros de ZIP para a camada de emulação.
     * Concentra o acesso ao sistema de arquivos fora do Core. Os membros de ZIP são
     * descomprimidos em memória, sem criar arquivos no disco. Limites atuais:
     * 8 MiB por ROM e 128 MiB por arquivo ZIP. O header é interpretado por Cartridge.
     * Erros de leitura e de formato são reportados por exceções std::runtime_error.
     */
    class RomFile
    {
    public:
        // Identifica um membro no diretório do ZIP; o índice não é a posição na lista filtrada.
        struct Entry
        {
            unsigned int index;
            std::string name;
        };

        // Verifica somente a extensão .gb/.gbc, sem diferenciar maiúsculas e minúsculas.
        static bool isRom(const std::filesystem::path &path);

        // Verifica somente a extensão .zip, sem abrir nem validar o arquivo.
        static bool isZip(const std::filesystem::path &path);

        /**
         * Lista os membros .gb/.gbc, inclusive em subpastas, na ordem do ZIP.
         * A listagem não garante que o conteúdo de cada membro possa ser extraído.
         */
        static std::vector<Entry> entries(const std::filesystem::path &path);

        // Lê uma ROM .gb/.gbc diretamente; rejeita arquivos vazios ou acima de 8 MiB.
        static std::vector<std::uint8_t> read(const std::filesystem::path &path);

        /**
         * Extrai o membro pelo índice obtido em entries(), verificando seu CRC.
         * Use o mesmo ZIP, sem modificá-lo entre a listagem e a extração.
         * Rejeita membros criptografados, não suportados ou com tamanho fora dos limites.
         */
        static std::vector<std::uint8_t> read(const std::filesystem::path &path, unsigned int entry);

        // Identifica os bytes da ROM por CRC32, em oito dígitos hexadecimais maiúsculos.
        // Para ZIPs, recebe o conteúdo extraído, garantindo o mesmo hash da ROM simples.
        static std::string hash(const std::vector<std::uint8_t> &rom);

        // Identidade SHA-256 para associar saves à imagem exata da ROM.
        static std::string identity(const std::vector<std::uint8_t> &rom);
    };
}