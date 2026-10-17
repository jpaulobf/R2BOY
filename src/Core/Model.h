#pragma once

namespace R2BOY::Core
{
    // Modelo físico do console. Um CGB também pode executar cartuchos em modo DMG.
    enum class Model
    {
        DMG,
        CGB
    };

    // Modo de execução do jogo, separado do modelo físico para representar essa compatibilidade.
    enum class ExecutionMode
    {
        DMG,
        CGB
    };
}