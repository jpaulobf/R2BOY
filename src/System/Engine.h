#pragma once

#include "Core/Board.h"
#include "Audio.h"
#include "Input.h"

namespace R2BOY::System
{
    /**
     * Coordena o ciclo da aplicação e a integração entre a plataforma e o Core.
     * Possui a Board e os adaptadores de entrada/áudio. Configura a abertura de ROMs,
     * processa eventos, avança o hardware pelo tempo decorrido e entrega à Window
     * o último framebuffer completo publicado pela PPU.
     */
    class Engine
    {
    public:
        // Executa até o encerramento; retorna 0 ao sair normalmente ou 1 em falha de janela/renderização.
        // Falhas ao localizar a pasta base ou ao salvar no encerramento também retornam 1.
        int run();

    private:
        // Hardware CGB executa tanto ROMs DMG quanto jogos exclusivos de Game Boy Color.
        Core::Board board_{Core::Model::CGB};
        Input input_;
        Audio audio_;
        bool vsyncEnabled_ = true;
        bool fastForwardEnabled_ = true;
        bool uncappedSpeed_ = false;
        bool paused_ = false;
    };
}
