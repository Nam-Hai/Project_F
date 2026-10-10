#include "sample_loader.h"

#include <cstdio>
#include <cstring>

#include "../../config.h"
#include "../../core/wav/wav_reader.h"
#include "stdio_file.h"

static const char* CONVERT_HINT =
    "\nConversion (macOS) : afconvert -f WAVE -d LEI16@44100 entree.wav sortie.wav";

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

// Message shown for a file that read_header refused.
static std::string wav_error_message(wav::Status status, const wav::Info& info) {
    switch (status) {
        case wav::Status::NotWav:        return "ce n'est pas un fichier WAV (RIFF/WAVE absent)";
        case wav::Status::NoFmt:         return "chunk 'fmt ' absent";
        case wav::Status::DataBeforeFmt: return "chunk 'data' trouvé avant 'fmt '";
        case wav::Status::NoData:        return "chunk 'data' absent";
        case wav::Status::Unsupported: {
            char msg[256];
            std::snprintf(msg, sizeof msg,
                          "format non supporté (%s, %u bits, %u canal(aux), %u Hz) ; "
                          "attendu : PCM 16 bits ou flottant 32 bits, 1 ou 2 canaux, %d Hz%s",
                          info.tag == 1 ? "PCM" : info.tag == 3 ? "flottant" : "autre", info.bits, info.channels, info.sample_rate,
                          cfg::SAMPLE_RATE, CONVERT_HINT);
            return msg;
        }
        case wav::Status::Ok: break;
    }
    return "erreur";
}

bool probe_wav(const char* path, wav::Info& info, std::string& error) {
    StdioFile file(path);
    if (!file.is_open()) {
        info  = wav::Info{};
        error = std::string("impossible d'ouvrir ") + path;
        return false;
    }
    const wav::Status status = wav::read_header(file, info);
    if (status != wav::Status::Ok) {
        error = wav_error_message(status, info);
        return false;
    }
    return true;
}

bool load_wav(const char* path, std::vector<int16_t>& out, int& channels_out, std::string& error) {
    out.clear();
    channels_out = 1;

    StdioFile file(path);
    if (!file.is_open()) {
        error = std::string("impossible d'ouvrir ") + path;
        return false;
    }
    wav::Info         info;
    const wav::Status status = wav::read_header(file, info);
    if (status != wav::Status::Ok) {
        error = wav_error_message(status, info);
        return false;
    }

    // The sound data is read and converted by the shared reader (4 KB at a time): the file never sits in memory as a whole.
    out.resize(static_cast<std::size_t>(info.frames) * info.channels);
    wav::SampleReader reader(file, info);
    const uint32_t    frames = reader.read(out.data(), info.frames);
    if (frames < info.frames) out.resize(static_cast<std::size_t>(frames) * info.channels);  // the file shrank under us: keep what was read
    channels_out = info.channels;
    return true;
}
