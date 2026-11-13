#include "Window/KeyboardConfig.h"
#include "Window/ConfigManager.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
static void check(bool value)
{
    if (!value)
        throw std::runtime_error("keyboard config failed");
}
// Verifica padrões, persistência, valores inválidos e falhas de gravação.
int main()
{
    const auto dir = fs::temp_directory_path() / ("r2boy-keys-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    try
    {
        const auto path = dir / "config.ini";
        {
            R2BOY::Window::ConfigManager config(path);
            R2BOY::Window::KeyboardConfig keys(config);
            auto mapping = keys.load();
            check(mapping == R2BOY::System::defaultKeyboardMapping());
            mapping[0] = SDLK_Q;
            mapping[4] = 0;
            config.setValue("unrelated", "preserved");
            check(keys.save(mapping));
        }
        {
            R2BOY::Window::ConfigManager config(path);
            R2BOY::Window::KeyboardConfig keys(config);
            check(keys.load()[0] == SDLK_Q && keys.load()[4] == 0);
            check(config.getValue("unrelated") == "preserved");
            config.setValue("keyboard_1_0", "bad");
            check(keys.load()[0] == SDLK_W);
        }
        const auto blocked = dir / "blocked";
        {
            std::ofstream out(blocked);
            out << "x";
        }
        {
            R2BOY::Window::ConfigManager config(blocked / "config.ini");
            R2BOY::Window::KeyboardConfig keys(config);
            auto mapping = keys.load();
            mapping[0] = SDLK_Q;
            check(!keys.save(mapping));
            check(keys.load()[0] == SDLK_W);
        }
        fs::remove_all(dir);
    }
    catch (...)
    {
        fs::remove_all(dir);
        throw;
    }
}