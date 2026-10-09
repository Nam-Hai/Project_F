#include "sample_loader.h"

#include <cstdio>
#include <cstring>

#include "../../config.h"

static uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static const char* CONVERT_HINT =
    "\nConversion (macOS) : afconvert -f WAVE -d LEI16@44100 -c 1 entree.wav sortie.wav";

bool load_wav_mono16(const char* path, std::vector<int16_t>& out, std::string& error) {
    out.clear();

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
            if (tag != 1 || bits != 16 || channels != 1 ||
                rate != static_cast<uint32_t>(cfg::SAMPLE_RATE)) {
                char msg[256];
                std::snprintf(msg, sizeof msg,
                              "format non supporté (%s, %u bits, %u canal(aux), %u Hz) ; "
                              "attendu : PCM, 16 bits, 1 canal, %d Hz%s",
                              tag == 1 ? "PCM" : "non-PCM", bits, channels, rate, cfg::SAMPLE_RATE,
                              CONVERT_HINT);
                error = msg;
                return false;
            }
            const std::size_t bytes = (size < avail) ? size : avail;  // tolère un fichier tronqué
            const std::size_t count = bytes / 2;
            out.resize(count);
            for (std::size_t i = 0; i < count; ++i)
                out[i] = static_cast<int16_t>(rd16(file.data() + body + 2 * i));
            return true;
        }

        pos = body + size + (size & 1);  // les chunks sont alignés sur 2 octets
    }

    error = have_fmt ? "chunk 'data' absent" : "chunk 'fmt ' absent";
    return false;
}
