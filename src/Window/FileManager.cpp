#include "FileManager.h"
#include "ConfigManager.h"
#include "Common/SafeText.h"
#include <imgui.h>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <cctype>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace R2BOY::Window
{
    namespace
    {
        std::string displayPath(const std::filesystem::path &path)
        {
            try
            {
                return path.u8string();
            }
            catch (const std::filesystem::filesystem_error &)
            {
                return "[Unsupported filename encoding]";
            }
        }
    }

    void FileManager::setRomOpenHandler(RomOpenHandler handler)
    {
        open = std::move(handler);
    }

    void FileManager::requestOpen()
    {
        recentFile.reset();
        requested = true;
    }

    void FileManager::requestOpen(const std::filesystem::path &file)
    {
        recentFile = file;
        requested = true;
    }

    void FileManager::drawRecentMenu()
    {
        const auto &roms = config.getRecentRoms();
        if (!ImGui::BeginMenu("Recent Files", !roms.empty()))
            return;
        int recentIndex = 0;
        for (const auto &rom : roms)
        {
            const auto label = Common::safeLabel(std::filesystem::u8path(rom).filename().u8string());
            ImGui::PushID(recentIndex++);
            if (ImGui::MenuItem(label.c_str()))
                requestOpen(std::filesystem::u8path(rom));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", rom.c_str());
            ImGui::PopID();
        }
        ImGui::EndMenu();
    }

    void FileManager::setDirectory(const std::filesystem::path &path)
    {
        // Converte antes de mudar a pasta: uma falha não deixa a navegação parcial.
        const auto encoded = path.u8string();
        directory = path;
        config.setLastRomPath(encoded);
    }

    FileManager::FileManager(ConfigManager &settings) : config(settings)
    {
        std::error_code ec;
        directory = std::filesystem::current_path(ec);
        if (ec)
            directory = ".";
        const auto saved = std::filesystem::u8path(config.getLastRomPath());
        if (!saved.empty() && std::filesystem::is_directory(saved, ec))
            directory = saved;
    }

    void FileManager::drawDriveSelector()
    {
#ifdef _WIN32
        const auto current = displayPath(directory.root_name());
        if (!ImGui::BeginCombo("Drive", current.empty() ? "Select a drive" : current.c_str()))
            return;

        // Consulta a lista ao abrir o combo, incluindo unidades removíveis e de rede mapeadas.
        const DWORD drives = GetLogicalDrives();
        if (drives == 0)
            ImGui::TextUnformatted("Unable to list drives.");
        for (unsigned int i = 0; i < 26; ++i)
        {
            if ((drives & (DWORD{1} << i)) == 0)
                continue;
            const char letter = static_cast<char>('A' + i);
            const char root[] = {letter, ':', '\\', '\0'};
            const bool selected = current.size() == 2 && current[1] == ':' &&
                                  std::toupper(static_cast<unsigned char>(current[0])) == letter;
            if (ImGui::Selectable(root, selected))
            {
                std::error_code ec;
                if (std::filesystem::is_directory(root, ec))
                {
                    setDirectory(root);
                    selectedFile.clear();
                    chooseArchive = false;
                    error.clear();
                }
                else
                    error = "Drive is unavailable or cannot be accessed.";
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
#endif
    }

    bool FileManager::load(const std::filesystem::path &file, const std::optional<Common::RomFile::Entry> &entry)
    {
        try
        {
            if (!open)
                throw std::runtime_error("ROM loading is unavailable.");
            const auto fullPath = std::filesystem::absolute(file).lexically_normal();
            open(fullPath, entry);
            config.addRomToList(fullPath.u8string());
            setDirectory(fullPath.parent_path());
            error.clear();
            return true;
        }
        catch (const std::exception &e)
        {
            error = e.what();
            return false;
        }
    }

    bool FileManager::select(const std::filesystem::path &file)
    {
        try
        {
            if (std::filesystem::is_directory(file))
            {
                setDirectory(file);
                selectedFile.clear();
                error.clear();
                return false;
            }
            if (Common::RomFile::isZip(file))
            {
                auto found = Common::RomFile::entries(file);
                // Um ZIP com uma única ROM dispensa a etapa de escolha do membro.
                if (found.size() == 1)
                    return load(file, found.front());
                archive = file;
                entries = std::move(found);
                chooseArchive = true;
                error.clear();
                return false;
            }
            return load(file);
        }
        catch (const std::exception &e)
        {
            error = e.what();
            return false;
        }
    }

    void FileManager::draw()
    {
        bool openRecent = false;
        // Adia OpenPopup até existir um frame ImGui, inclusive para pedidos via Ctrl+O.
        if (requested)
        {
            requested = false;
            chooseArchive = false;
            error.clear();
            openRecent = recentFile.has_value();
            ImGui::OpenPopup("Open ROM");
        }
        ImGui::SetNextWindowSize(ImVec2(570, 430), ImGuiCond_FirstUseEver);
        bool visible = true;
        if (!ImGui::BeginPopupModal("Open ROM", &visible))
            return;

        if (openRecent)
        {
            const auto file = std::move(*recentFile);
            recentFile.reset();
            if (select(file))
                ImGui::CloseCurrentPopup();
        }

        drawDriveSelector();
        ImGui::TextWrapped("%s", displayPath(chooseArchive ? archive : directory).c_str());
        ImGui::Separator();
        if (ImGui::Button("Up"))
        {
            try
            {
                if (chooseArchive)
                    chooseArchive = false;
                else
                {
                    const auto parent = directory.parent_path();
                    if (!parent.empty())
                        setDirectory(parent);
                }
                selectedFile.clear();
                error.clear();
            }
            catch (const std::filesystem::filesystem_error &)
            {
                error = "Unable to open directory: unsupported filename encoding.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        if (chooseArchive)
        {
            ImGui::BeginChild("ZIP entries", ImVec2(0, -45), true);
            for (const auto &entry : entries)
            {
                ImGui::PushID(static_cast<int>(entry.index));
                const auto label = Common::safeLabel(entry.name);
                if (ImGui::Selectable(label.c_str()) && load(archive, entry))
                    ImGui::CloseCurrentPopup();
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        else
        {
            ImGui::BeginChild("Files", ImVec2(0, -45), true);
            std::error_code ec;
            struct ListedFile
            {
                std::filesystem::directory_entry entry;
                std::string label;
            };
            std::vector<ListedFile> files;
            bool unsupportedNames = false;
            std::filesystem::directory_iterator it(directory, ec), end;
            for (; !ec && it != end; it.increment(ec))
            {
                try
                {
                    std::error_code status;
                    const bool isDirectory = it->is_directory(status);
                    if (isDirectory || (it->is_regular_file(status) &&
                                        (Common::RomFile::isRom(it->path()) || Common::RomFile::isZip(it->path()))))
                    {
                        const auto name = it->path().filename().u8string();
                        files.push_back({*it, (isDirectory ? "[DIR] " : "") + Common::safeLabel(name)});
                    }
                }
                catch (const std::filesystem::filesystem_error &)
                {
                    unsupportedNames = true;
                }
            }
            if (ec)
                ImGui::TextWrapped("Unable to list directory: %s", ec.message().c_str());
            if (unsupportedNames)
                ImGui::TextWrapped("Some entries have unsupported file names and were skipped.");
            std::sort(files.begin(), files.end(), [](const auto &a, const auto &b)
                      { return a.entry.path() < b.entry.path(); });
            int fileIndex = 0;
            for (const auto &file : files)
            {
                ImGui::PushID(fileIndex++);
                if (ImGui::Selectable(file.label.c_str(), selectedFile == file.entry.path()))
                {
                    selectedFile = file.entry.path();
                    if (select(file.entry.path()))
                        ImGui::CloseCurrentPopup();
                    ImGui::PopID();
                    break; // A navegação atualiza a lista no próximo frame.
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        if (!error.empty())
            ImGui::TextWrapped("%s", error.c_str());
        ImGui::EndPopup();
    }
}