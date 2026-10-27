#include "Window/ConfigManager.h"
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
