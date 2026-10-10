#include "sample_loader.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "../../config.h"

static uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static const char* CONVERT_HINT =
    "\nConversion (macOS) : afconvert -f WAVE -d LEI16@44100 entree.wav sortie.wav";

// Un flottant 32 bits (-1..1) en 16 bits : arrondi, écrêté aux bornes ; une valeur non numérique donne 0.
static int16_t float_to_int16(uint32_t bits) {
    float v;
    std::memcpy(&v, &bits, sizeof v);
    if (!(v == v)) return 0;
    const float scaled = v * 32767.0f;
    if (scaled >= 32767.0f) return 32767;
    if (scaled <= -32768.0f) return -32768;
    return static_cast<int16_t>(std::lround(scaled));
}

void downmix_to_mono(std::vector<int16_t>& samples, int channels) {
    if (channels <= 1) return;
    const std::size_t frames = samples.size() / static_cast<std::size_t>(channels);
    for (std::size_t i = 0; i < frames; ++i) {
        int sum = 0;
        for (int c = 0; c < channels; ++c) sum += samples[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)];
        // Moyenne arrondie au plus proche (les demi-valeurs s'éloignent de zéro).
        samples[i] = static_cast<int16_t>((sum + (sum < 0 ? -(channels / 2) : channels / 2)) / channels);
    }
    samples.resize(frames);
}

bool load_wav_mono(const char* path, std::vector<int16_t>& out, std::string& error) {
    int channels = 1;
    if (!load_wav(path, out, channels, error)) return false;
    downmix_to_mono(out, channels);
    return true;
}

bool load_wav(const char* path, std::vector<int16_t>& out, int& channels_out, std::string& error) {
    out.clear();
    channels_out = 1;

    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        error = std::string("impossible d'ouvrir ") + path;
        return false;
    }
    std::vector<uint8_t> file;
    uint8_t              buf[4096];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) file.insert(file.end(), buf, buf + n);
    std::fclose(f);

    if (file.size() < 12 || std::memcmp(file.data(), "RIFF", 4) != 0 ||
        std::memcmp(file.data() + 8, "WAVE", 4) != 0) {
        error = "ce n'est pas un fichier WAV (RIFF/WAVE absent)";
        return false;
    }

    bool     have_fmt = false;
    uint16_t tag = 0, channels = 0, bits = 0;
    uint32_t rate = 0;

    // Les chunks ne sont pas à position fixe (LIST, etc.) : on les parcourt.
    std::size_t pos = 12;
    while (pos + 8 <= file.size()) {
        const uint8_t* id   = file.data() + pos;
        const uint32_t size = rd32(file.data() + pos + 4);
        const std::size_t body = pos + 8;
        const std::size_t avail = file.size() - body;

        if (std::memcmp(id, "fmt ", 4) == 0 && size >= 16 && avail >= 16) {
            tag      = rd16(file.data() + body);
            channels = rd16(file.data() + body + 2);
            rate     = rd32(file.data() + body + 4);
            bits     = rd16(file.data() + body + 14);
            // WAVE_FORMAT_EXTENSIBLE : le vrai format est dans le sous-format.
            if (tag == 0xFFFE && size >= 26 && avail >= 26) tag = rd16(file.data() + body + 24);
            have_fmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            if (!have_fmt) {
                error = "chunk 'data' trouvé avant 'fmt '";
                return false;
            }
            const bool pcm16 = tag == 1 && bits == 16;
            const bool f32   = tag == 3 && bits == 32;
            if ((!pcm16 && !f32) || (channels != 1 && channels != 2) ||
                rate != static_cast<uint32_t>(cfg::SAMPLE_RATE)) {
                char msg[256];
                std::snprintf(msg, sizeof msg,
                              "format non supporté (%s, %u bits, %u canal(aux), %u Hz) ; "
                              "attendu : PCM 16 bits ou flottant 32 bits, 1 ou 2 canaux, %d Hz%s",
                              tag == 1 ? "PCM" : tag == 3 ? "flottant" : "autre", bits, channels, rate,
                              cfg::SAMPLE_RATE, CONVERT_HINT);
                error = msg;
                return false;
            }
            const std::size_t width  = pcm16 ? 2 : 4;
            const std::size_t bytes  = (size < avail) ? size : avail;  // tolère un fichier tronqué
            const std::size_t count  = bytes / (width * channels) * channels;  // images entières seulement
            out.resize(count);
            const uint8_t* src = file.data() + body;
            for (std::size_t i = 0; i < count; ++i)
                out[i] = pcm16 ? static_cast<int16_t>(rd16(src + 2 * i)) : float_to_int16(rd32(src + 4 * i));
            channels_out = channels;
            return true;
        }

        pos = body + size + (size & 1);  // les chunks sont alignés sur 2 octets
    }

    error = have_fmt ? "chunk 'data' absent" : "chunk 'fmt ' absent";
    return false;
}
