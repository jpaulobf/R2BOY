#pragma once

#include <cstdint>
#include <vector>

struct SDL_AudioStream;

namespace R2BOY::System
{
    /**
     * Saída de áudio da aplicação para o dispositivo físico (SDL3).
     * Recebe as amostras estéreo float geradas pela Core/APU e as enfileira num
     * SDL_AudioStream (seguro entre threads). Se não houver dispositivo, a
     * classe fica inativa e o emulador segue em silêncio.
     */
    class Audio
    {
    public:
        Audio() = default;
        ~Audio() { close(); }
        Audio(const Audio &) = delete;
        Audio &operator=(const Audio &) = delete;

        // Abre o dispositivo padrão; exige que SDL já esteja inicializado. Retorna false sem áudio.
        bool open();
        void close();
        bool active() const { return stream_ != nullptr; }
        std::uint32_t sampleRate() const { return sampleRate_; }
        void setEnabled(bool enabled);
        bool enabled() const { return enabled_; }

        // Ajusta o ganho da saída entre 0 e 100%, preservando o estado de mute.
        void setVolume(int percent);

        // Enfileira amostras L/R intercaladas. Descarta o excesso quando a fila passa de ~150 ms,
        // para que a latência não cresça se o emulador rodar mais rápido que o tempo real.
        void queue(const std::vector<float> &samples);

        // Descarta o áudio pendente (pausa, reset, avanço rápido).
        void clear();

    private:
        SDL_AudioStream *stream_ = nullptr;
        std::uint32_t sampleRate_ = 48000;
        bool enabled_ = true;
        int volume_ = 100;
    };
}