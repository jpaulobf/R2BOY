#include "Window/ConfigManager.h"
#include "Common/SafeText.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void persistence(const std::filesystem::path &file)
    {
        {
            std::ofstream initial(file, std::ios::binary);
            initial << "# comment\r\nf1=older.gb\r\nf2=older.gb\r\nf10=oldest.gb\r\n"
                       "last_rom_path=C:/ROMs\r\nfilter=nearest\r\ncustom=a=b\r\n";
        }
        {
            R2BOY::Window::ConfigManager config(file);
            check(config.getRecentRoms().size() == 2, "load must remove duplicates and CRLF endings");
            check(config.getRecentRoms().front() == "older.gb", "f1 must remain most recent");
            check(config.getLastRomPath() == "C:/ROMs", "load must restore directory");
            for (int i = 1; i <= 12; ++i)
                config.addRomToList("game" + std::to_string(i) + ".gb");
            config.addRomToList("game5.gb");
            config.addRomToList("");
            check(config.getRecentRoms().size() == 10, "history must be limited to ten unique entries");
            check(config.getRecentRoms().front() == "game5.gb", "reopening must move entry to front");
            check(config.getRecentRoms().back() == "game3.gb", "history must evict oldest entry");
            config.setLastRomPath("C:/Jogos/português");
        }
        {
            R2BOY::Window::ConfigManager config(file);
            check(config.getRecentRoms().size() == 10, "destructor must persist all recent files");
            check(config.getRecentRoms().front() == "game5.gb", "round trip must preserve ordering");
            check(config.getLastRomPath() == "C:/Jogos/português", "round trip must preserve UTF-8 directory");
            config.loadConfigFile();
            check(config.getRecentRoms().size() == 10, "reload must not append stale entries");
        }
        std::ifstream saved(file);
        const std::string content{std::istreambuf_iterator<char>(saved), {}};
        check(content.find("filter=nearest\n") != std::string::npos, "unknown keys starting with f must survive");
        check(content.find("custom=a=b\n") != std::string::npos, "values containing equals must survive");
    }
}

int main()
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("r2boy-config-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        std::filesystem::create_directories(directory);
        persistence(directory / "config.ini");
        check(R2BOY::Common::escapeTerminal("game\x1B[2J\n") == "game\\x1B[2J\\x0A",
              "terminal control bytes must be escaped");
        check(R2BOY::Common::escapeTerminal("text\n", true) == "text\n", "serial newlines must remain usable");
        check(R2BOY::Common::escapeTerminal("\xC2\x9B") == "\\xC2\\x9B", "UTF-8 C1 controls must be escaped");
        check(R2BOY::Common::safeLabel("Game.gb##Other.gb") == "Game.gb#\\x23Other.gb",
              "file names must not hide their suffix through ImGui identifiers");
        const auto invalid = directory / "invalid.ini";
        {
            std::ofstream out(invalid, std::ios::binary);
            out << "last_rom_path=";
            out.put(static_cast<char>(0xFF));
            out << "\nf1=";
            out.put(static_cast<char>(0xFF));
            out << '\n';
        }
        {
            R2BOY::Window::ConfigManager config(invalid);
            check(config.getLastRomPath().empty() && config.getRecentRoms().empty(), "invalid UTF-8 paths must be ignored");
        }
        const auto oversized = directory / "oversized.ini";
        {
            std::ofstream out(oversized, std::ios::binary);
            out << std::string(1024 * 1024 + 1, 'x');
        }
        {
            R2BOY::Window::ConfigManager config(oversized);
        }
        check(std::filesystem::file_size(oversized) == 1024 * 1024 + 1,
              "oversized configuration must remain intact after rejection");
        const auto protectedFile = directory / "protected.txt";
        const auto linkedConfig = directory / "linked.ini";
        {
            std::ofstream out(protectedFile);
            out << "protected=original\n";
        }
        std::filesystem::create_hard_link(protectedFile, linkedConfig);
        {
            R2BOY::Window::ConfigManager config(linkedConfig);
            config.setLastRomPath("folder");
        }
        std::ifstream protectedInput(protectedFile);
        const std::string protectedContents{std::istreambuf_iterator<char>(protectedInput), {}};
        check(protectedContents == "protected=original\n", "config save must not truncate a linked file");
        protectedInput.close();
        const auto fresh = directory / "nested" / "config.ini";
        {
            R2BOY::Window::ConfigManager config(fresh);
            check(config.getRecentRoms().empty(), "missing config must start empty");
            config.addRomToList("one.gb");
        }
        {
            R2BOY::Window::ConfigManager config(fresh);
            check(config.getRecentRoms().size() == 1, "save must create parent directories");
        }
        std::ifstream saved(fresh);
        const std::string content{std::istreambuf_iterator<char>(saved), {}};
        check(content.find("f2=\n") != std::string::npos && content.find("f10=\n") != std::string::npos,
              "save must include empty slots through f10");
        saved.close();
        std::filesystem::remove_all(directory);
        std::cout << "Config manager tests passed.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}