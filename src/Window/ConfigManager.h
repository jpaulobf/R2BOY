#pragma once

#include <filesystem>
#include <list>
#include <map>
#include <string>

namespace R2BOY::Window
{
    // Persiste o diretório do seletor e as dez ROMs mais recentes em um INI simples.
    class ConfigManager
    {
    public:
        explicit ConfigManager(std::filesystem::path path = "resources/config.ini");
        ~ConfigManager();

        void loadConfigFile();
        void saveConfigFile() const;
        void addRomToList(const std::string &romPath);
        const std::list<std::string> &getRecentRoms() const { return recentRoms; }
        const std::string &getLastRomPath() const { return lastRomPath; }
        void setLastRomPath(const std::string &path) { lastRomPath = path; }

    private:
        std::filesystem::path configFilePath;
        std::list<std::string> recentRoms;
        std::string lastRomPath;
        std::map<std::string, std::string> configValues;
        bool canSave = true;
    };
}