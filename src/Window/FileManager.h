#pragma once

#include "System/RomFile.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace R2BOY::Window
{
    /**
     * Gerencia o seletor de ROMs da interface: navegação por pastas, seleção de
     * membros de ZIP e apresentação de erros. Mantém o estado entre frames.
     * A leitura dos arquivos usa System::RomFile; o carregamento do cartucho
     * é delegado ao callback configurado pela Engine.
     */
    class FileManager
    {
    public:
        // Callback síncrono com caminho e membro opcional do ZIP. Exceções
        // std::exception são capturadas e apresentadas no próprio seletor.
        using RomOpenHandler = std::function<void(const std::filesystem::path &, const std::optional<System::RomFile::Entry> &)>;

        // Inicia a navegação na pasta atual; não exige um contexto ImGui ativo.
        FileManager();

        // Configura o destino da seleção. Deve ser chamado antes de abrir uma ROM.
        void setRomOpenHandler(RomOpenHandler handler);

        // Agenda a abertura do seletor para a próxima chamada a draw().
        void requestOpen();

        // Desenha o seletor no frame ImGui atual, dentro da mesma janela a cada frame.
        void draw();

    private:
        // Exibe as unidades lógicas no Windows; não desenha controles em outras plataformas.
        void drawDriveSelector();

        // Executa o callback e captura falhas; true indica uma carga bem-sucedida.
        bool load(const std::filesystem::path &file, const std::optional<System::RomFile::Entry> &entry = std::nullopt);

        // Abre pastas, lista ROMs de ZIP ou solicita a carga de uma ROM simples.
        // Retorna true apenas quando o seletor pode fechar após uma carga.
        bool select(const std::filesystem::path &file);

        RomOpenHandler open;
        std::filesystem::path selectedFile;
        std::filesystem::path directory;
        std::filesystem::path archive;
        std::vector<System::RomFile::Entry> entries;
        std::string error;
        bool requested = false;
        bool chooseArchive = false;
    };
}
