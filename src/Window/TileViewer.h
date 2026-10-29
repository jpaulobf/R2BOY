#pragma once

#include "Common/TileSnapshot.h"
#include <functional>
#include <memory>
#include <array>

union SDL_Event;
struct SDL_Window;

namespace R2BOY::Window
{
    // Janela SDL independente, com contexto ImGui próprio e dados somente de leitura.
    class TileViewer
    {
    public:
        using SnapshotProvider = std::function<Common::TileSnapshot()>;
        // Grade 16 x 24; saída ARGB com 128 x 192 pixels.
        using Pixels = std::array<std::uint32_t, 128 * 192>;
        // Seleções fora dos bancos/tipos/paletas disponíveis retornam uma imagem vazia.
        static Pixels decode(const Common::TileSnapshot &snapshot, unsigned bank, unsigned object, unsigned palette);
        TileViewer();
        ~TileViewer();
        bool open();
        void close();
        bool isOpen() const;
        // Alinha as bordas superiores e deixa um pixel entre as janelas.
        void snapTo(SDL_Window *mainWindow);
        // Retorna true quando o evento pertence à janela do visualizador.
        bool processEvent(const SDL_Event &event);
        bool render(const SnapshotProvider &provider);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
