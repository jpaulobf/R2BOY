#pragma once

#include "Common/RomFile.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace R2BOY::Window
{
    class ConfigManager;
    /**
     * Gerencia o seletor de ROMs da interface: navegação por pastas, seleção de
     * membros de ZIP e apresentação de erros. Mantém o estado entre frames.
     * A leitura dos arquivos usa Common::RomFile; o carregamento do cartucho
     * é delegado ao callback configurado pela Engine.
     */
    class FileManager
    {
    public:
        // Callback síncrono com caminho e membro opcional do ZIP. Exceções
        // std::exception são capturadas e apresentadas no próprio seletor.
        using RomOpenHandler = std::function<void(const std::filesystem::path &, const std::optional<Common::RomFile::Entry> &)>;

        // Restaura o último diretório válido; não exige um contexto ImGui ativo.
        explicit FileManager(ConfigManager &config);

        // Configura o destino da seleção. Deve ser chamado antes de abrir uma ROM.
        void setRomOpenHandler(RomOpenHandler handler);

        // Agenda a abertura do seletor para a próxima chamada a draw().
        void requestOpen();

        // Agenda a abertura de um recente pelo mesmo fluxo de ROMs e ZIPs.
        void requestOpen(const std::filesystem::path &file);

        // Desenha o submenu File > Recent Files no frame atual.
        void drawRecentMenu();

        // Desenha o seletor no frame ImGui atual, dentro da mesma janela a cada frame.
        void draw();

    private:
        // Exibe as unidades lógicas no Windows; não desenha controles em outras plataformas.
        void drawDriveSelector();

        // Executa o callback e captura falhas; true indica uma carga bem-sucedida.
        bool load(const std::filesystem::path &file, const std::optional<Common::RomFile::Entry> &entry = std::nullopt);

        // Abre pastas, lista ROMs de ZIP ou solicita a carga de uma ROM simples.
        // Retorna true apenas quando o seletor pode fechar após uma carga.
        bool select(const std::filesystem::path &file);

        void setDirectory(const std::filesystem::path &path);

        ConfigManager &config;
        std::optional<std::filesystem::path> recentFile;
        RomOpenHandler open;
        std::filesystem::path selectedFile;
        std::filesystem::path directory;
        std::filesystem::path archive;
        std::vector<Common::RomFile::Entry> entries;
        std::string error;
        bool requested = false;
        bool chooseArchive = false;
    };
}
