#pragma once

#include "Core/Board.h"
#include "Audio.h"
#include "Input.h"

namespace R2BOY::System
{
    /**
     * Coordena o ciclo da aplicação e a integração entre a plataforma e o Core.
     * Possui a Board e os adaptadores de entrada/áudio. Configura a abertura de ROMs,
     * processa eventos e solicita a renderização à Window. A execução do hardware
     * em tempo real ainda será integrada ao ciclo.
     */
    class Engine
    {
    public:
        // Executa até o encerramento; retorna 0 ao sair normalmente ou 1 em falha de janela/renderização.
        int run();

    private:
        // Hardware CGB executa tanto ROMs DMG quanto jogos exclusivos de Game Boy Color.
        Core::Board board_{Core::Model::CGB};
        Input input_;
        Audio audio_;
    };
}