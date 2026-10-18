#pragma once

#include "Core/Cartridge/Cartridge.h"
#include <filesystem>

namespace R2BOY::System
{
    /**
     * Persiste a RAM de cartuchos com bateria, sem incluir a RAM interna do console.
     * Cada ROM usa <CRC32>.sav na pasta configurada; ROM simples e ZIP compartilham
     * o save dos mesmos bytes. MBC2 usa 512 bytes, um por célula de quatro bits.
     * Erros de I/O ou tamanho incompatível lançam exceção, preservando a RAM atual.
     */
    class SaveFile
    {
    public:
        explicit SaveFile(std::filesystem::path directory);

        std::filesystem::path pathFor(const Core::Cartridge::Cartridge &cartridge) const;

        // Retorna false se não houver bateria, RAM ou arquivo de save.
        bool load(Core::Cartridge::Cartridge &cartridge) const;
        
        // Só grava RAM modificada com bateria. Usa arquivo temporário e substituição
        // no mesmo diretório; só limpa ramDirty após confirmar a gravação.
        bool save(Core::Cartridge::Cartridge &cartridge) const;

    private:
        std::filesystem::path directory_;
    };
}
