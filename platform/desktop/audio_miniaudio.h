#pragma once

// Sortie audio PC (miniaudio). Joue le même rôle que le driver I2S + DMA sur Teensy :
// appelle `fn` par blocs d'au plus cfg::BLOCK échantillons stéréo planaires.
class DesktopAudio {
public:
    using RenderFn = void (*)(float* left, float* right, int frames, void* user);

    DesktopAudio();
    ~DesktopAudio();
    DesktopAudio(const DesktopAudio&)            = delete;
    DesktopAudio& operator=(const DesktopAudio&) = delete;

    bool start(RenderFn fn, void* user);
    void stop();

    struct Impl;  // détail d'implémentation, défini dans audio_miniaudio.cpp

private:
    Impl* impl_;
};
