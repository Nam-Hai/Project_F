// Étape 1 : sinus 440 Hz via le callback audio, pour valider la chaîne de sortie PC.
#include <cstdio>

#include "../../config.h"
#include "../../core/dsp_utils.h"
#include "audio_miniaudio.h"

struct Tone {
    float phase = 0.0f;
};

static void render_tone(float* left, float* right, int frames, void* user) {
    auto* tone = static_cast<Tone*>(user);
    const float inc = dsp::TWO_PI * 440.0f / static_cast<float>(cfg::SAMPLE_RATE);

    for (int i = 0; i < frames; ++i) {
        left[i] = right[i] = 0.2f * std::sin(tone->phase);
        tone->phase += inc;
        if (tone->phase >= dsp::TWO_PI) tone->phase -= dsp::TWO_PI;
    }
}

int main() {
    Tone         tone;
    DesktopAudio audio;
    if (!audio.start(render_tone, &tone)) return 1;

    std::printf("Sinus 440 Hz en cours. Entrée pour quitter.\n");
    std::getchar();
    return 0;
}
