#include "ConfigManager.h"
#include <fstream>
#include <iostream>
#include <utility>

namespace R2BOY::Window
{
    namespace
    {
        bool isRecentKey(const std::string &key)
        {
            return key == "f10" || (key.size() == 2 && key[0] == 'f' && key[1] >= '1' && key[1] <= '9');
        }
    }

    ConfigManager::ConfigManager(std::filesystem::path path) : configFilePath(std::move(path))
    {
        loadConfigFile();
    }

    ConfigManager::~ConfigManager()
    {
        saveConfigFile();
    }

    void ConfigManager::loadConfigFile()
    {
        configValues.clear();
        recentRoms.clear();
        lastRomPath.clear();
        std::ifstream file(configFilePath);
        std::string line;
        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;
            const auto delimiter = line.find('=');
            if (delimiter != std::string::npos)
                configValues[line.substr(0, delimiter)] = line.substr(delimiter + 1);
        }
        // Insere do mais antigo ao mais recente para também eliminar duplicatas.
        for (int i = 10; i >= 1; --i)
        {
            const auto found = configValues.find("f" + std::to_string(i));
            if (found != configValues.end())
                addRomToList(found->second);
        }
        const auto found = configValues.find("last_rom_path");
        if (found != configValues.end())
            lastRomPath = found->second;
    }

    void ConfigManager::saveConfigFile() const
    {
        std::error_code ec;
        if (configFilePath.has_parent_path())
            std::filesystem::create_directories(configFilePath.parent_path(), ec);
        if (ec)
        {
            std::cerr << "ConfigManager: Unable to create configuration directory: " << ec.message() << '\n';
            return;
        }
        std::ofstream file(configFilePath);
        file << "# Last Opened Files\n";
        auto rom = recentRoms.begin();
        for (int i = 1; i <= 10; ++i)
        {
            file << 'f' << i << '=';
            if (rom != recentRoms.end())
                file << *rom++;
            file << '\n';
        }
        file << "\n# Last ROM Directory\nlast_rom_path=" << lastRomPath << '\n';
        for (const auto &[key, value] : configValues)
            if (!isRecentKey(key) && key != "last_rom_path")
                file << key << '=' << value << '\n';
        file.flush();
        if (!file)
            std::cerr << "ConfigManager: Unable to save configuration.\n";
    }

    void ConfigManager::addRomToList(const std::string &romPath)
    {
        if (romPath.empty())
            return;
        // A cópia também permite receber uma referência a um item da própria lista.
        const auto path = romPath;
        recentRoms.remove(path);
        recentRoms.push_front(path);
        if (recentRoms.size() > 10)
            recentRoms.pop_back();
    }
}