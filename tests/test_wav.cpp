// Chargeur WAV : PCM 16 bits et flottant 32 bits, mono et stéréo, cas limites, mélange en mono.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../config.h"
#include "../platform/desktop/sample_loader.h"

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

using Bytes = std::vector<uint8_t>;

static void put16(Bytes& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
static void put32(Bytes& b, uint32_t v) {
    put16(b, static_cast<uint16_t>(v));
    put16(b, static_cast<uint16_t>(v >> 16));
}
static void tag(Bytes& b, const char* t) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(t[i]));
}

struct Spec {
    uint16_t tag_id   = 1;
    uint16_t channels = 1;
    uint32_t rate     = cfg::SAMPLE_RATE;
    uint16_t bits     = 16;
    bool     extensible = false;
    bool     list_first = false;   // un chunk LIST (taille impaire) avant 'data'
    int      cut        = 0;       // octets retirés à la fin du fichier (fichier tronqué)
};

static void write_wav(const char* path, const Spec& s, const Bytes& data) {
    Bytes b;
    tag(b, "RIFF");
    put32(b, 0);
    tag(b, "WAVE");
    tag(b, "fmt ");
    if (s.extensible) {
        put32(b, 40);
        put16(b, 0xFFFE);
    } else {
        put32(b, 16);
        put16(b, s.tag_id);
    }
    put16(b, s.channels);
    put32(b, s.rate);
    put32(b, s.rate * s.channels * s.bits / 8);
    put16(b, static_cast<uint16_t>(s.channels * s.bits / 8));
    put16(b, s.bits);
    if (s.extensible) {
        put16(b, 22);
        put16(b, s.bits);
        put32(b, 3);
        put16(b, s.tag_id);  // début du GUID du sous-format
        for (int i = 0; i < 14; ++i) b.push_back(0);
    }
    if (s.list_first) {
        tag(b, "LIST");
        put32(b, 3);
        for (int i = 0; i < 3; ++i) b.push_back('x');
        b.push_back(0);  // octet de remplissage
    }
    tag(b, "data");
    put32(b, static_cast<uint32_t>(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    b.resize(b.size() - static_cast<std::size_t>(s.cut));

    std::FILE* f = std::fopen(path, "wb");
    std::fwrite(b.data(), 1, b.size(), f);
    std::fclose(f);
}

static Bytes pcm(const std::vector<int16_t>& v) {
    Bytes b;
    for (int16_t x : v) put16(b, static_cast<uint16_t>(x));
    return b;
}
static Bytes floats(const std::vector<float>& v) {
    Bytes b;
    for (float x : v) {
        uint32_t u;
        std::memcpy(&u, &x, 4);
        put32(b, u);
    }
    return b;
}

static const char* PATH = "test_wav.wav";

struct Result {
    bool                 ok = false;
    std::vector<int16_t> data;
    int                  channels = 0;
    std::string          error;
};

static Result load(const Spec& s, const Bytes& data) {
    write_wav(PATH, s, data);
    Result r;
    r.ok = load_wav(PATH, r.data, r.channels, r.error);
    return r;
}

int main() {
    // ----- PCM 16 bits -----
    {
        Spec s;
        Result r = load(s, pcm({1, -2, 300, -32768, 32767}));
        CHECK(r.ok && r.channels == 1 && r.data == std::vector<int16_t>({1, -2, 300, -32768, 32767}), "PCM 16 bits mono : données identiques");

        s.channels = 2;
        r = load(s, pcm({10, -10, 20, -20, 30, -30}));
        CHECK(r.ok && r.channels == 2 && r.data == std::vector<int16_t>({10, -10, 20, -20, 30, -30}), "PCM 16 bits stéréo : entrelacé G, D, G, D");

        s.list_first = true;
        r = load(s, pcm({1, 2, 3, 4}));
        CHECK(r.ok && r.data.size() == 4, "chunk LIST de taille impaire avant 'data' : sauté avec son octet de remplissage");

        s.list_first = false;
        r = load(s, pcm({}));
        CHECK(r.ok && r.data.empty() && r.channels == 2, "aucune donnée : chargé, vide");
    }

    // ----- Flottant 32 bits -----
    {
        Spec s;
        s.tag_id = 3;
        s.bits   = 32;
        s.channels = 2;
        const float nan = std::nanf("");
        Result r = load(s, floats({0.5f, -0.5f, 1.0f, -1.0f, 2.0f, -3.0f, nan, 0.0f, 0.25f, -0.25f}));
        CHECK(r.ok && r.channels == 2 && r.data.size() == 10, "flottant stéréo : 10 valeurs");
        CHECK(r.data.size() == 10 && r.data[0] == 16384 && r.data[1] == -16384, "0,5 -> 16384, -0,5 -> -16384 (arrondi)");
        CHECK(r.data.size() == 10 && r.data[2] == 32767 && r.data[3] == -32767, "1 -> 32767, -1 -> -32767");
        CHECK(r.data.size() == 10 && r.data[4] == 32767 && r.data[5] == -32768, "hors bornes : écrêté");
        CHECK(r.data.size() == 10 && r.data[6] == 0 && r.data[7] == 0, "valeur non numérique : 0");
        CHECK(r.data.size() == 10 && r.data[8] == 8192 && r.data[9] == -8192, "0,25 -> 8192");

        s.channels = 1;
        r = load(s, floats({0.5f, -1.0f}));
        CHECK(r.ok && r.channels == 1 && r.data == std::vector<int16_t>({16384, -32767}), "flottant mono");

        // Format « extensible » (celui des fichiers d'enregistreurs) avec sous-format flottant.
        s.extensible = true;
        s.channels   = 2;
        r = load(s, floats({0.5f, -0.5f}));
        CHECK(r.ok && r.channels == 2 && r.data == std::vector<int16_t>({16384, -16384}), "extensible, sous-format flottant");
        Spec p;
        p.extensible = true;
        r = load(p, pcm({7, 8}));
        CHECK(r.ok && r.data == std::vector<int16_t>({7, 8}), "extensible, sous-format PCM 16 bits");
    }

    // ----- Formats refusés -----
    {
        Spec s;
        s.channels = 3;
        Result r = load(s, pcm({1, 2, 3}));
        CHECK(!r.ok && r.data.empty() && r.error.find("canal") != std::string::npos, "3 canaux refusés");
        s.channels = 0;
        r = load(s, pcm({1}));
        CHECK(!r.ok, "0 canal refusé");
        s = Spec{};
        s.rate = 48000;
        r = load(s, pcm({1}));
        CHECK(!r.ok && r.error.find("48000") != std::string::npos && r.error.find("afconvert") != std::string::npos, "48 kHz refusé, message avec la conversion");
        s = Spec{};
        s.bits = 24;
        r = load(s, Bytes(6, 0));
        CHECK(!r.ok, "PCM 24 bits refusé");
        s.bits = 8;
        CHECK(!load(s, Bytes(6, 0)).ok, "PCM 8 bits refusé");
        s = Spec{};
        s.tag_id = 3;
        s.bits   = 64;
        CHECK(!load(s, Bytes(16, 0)).ok, "flottant 64 bits refusé");
        s.tag_id = 3;
        s.bits   = 16;
        CHECK(!load(s, Bytes(16, 0)).ok, "flottant déclaré en 16 bits refusé");
        s.tag_id = 2;  // ADPCM
        s.bits   = 4;
        CHECK(!load(s, Bytes(16, 0)).ok, "format compressé refusé");
        CHECK(!load_wav("fichier_inexistant.wav", r.data, r.channels, r.error), "fichier absent refusé");
        // Un refus ne laisse pas de données.
        write_wav(PATH, s, Bytes(4, 0));
        r.data.assign(5, 1);
        load_wav(PATH, r.data, r.channels, r.error);
        CHECK(r.data.empty(), "un refus vide la sortie");
    }

    // ----- Fichiers tronqués : seules les images entières sont gardées -----
    {
        Spec s;
        s.channels = 2;
        s.cut      = 2;  // 6 échantillons annoncés, 5 présents : 2 images entières
        Result r   = load(s, pcm({1, 2, 3, 4, 5, 6}));
        CHECK(r.ok && r.data == std::vector<int16_t>({1, 2, 3, 4}), "stéréo tronquée : l'image incomplète est abandonnée");
        s.cut = 1;  // un demi-échantillon en moins
        r     = load(s, pcm({1, 2, 3, 4, 5, 6}));
        CHECK(r.ok && r.data == std::vector<int16_t>({1, 2, 3, 4}), "tronqué au milieu d'un échantillon");
        s.tag_id = 3;
        s.bits   = 32;
        s.cut    = 3;
        r        = load(s, floats({0.5f, 0.5f, 0.5f, 0.5f}));
        CHECK(r.ok && r.data.size() == 2, "flottant stéréo tronqué : une image gardée");
        Spec m;
        m.cut = 1;
        r     = load(m, pcm({1, 2, 3}));
        CHECK(r.ok && r.data == std::vector<int16_t>({1, 2}), "mono tronqué");
    }

    // ----- Mélange en mono -----
    {
        std::vector<int16_t> v = {1, 2, -1, -2, 100, -100, 32767, 32767, -32768, -32768, 3, 4};
        downmix_to_mono(v, 2);
        CHECK(v == std::vector<int16_t>({2, -2, 0, 32767, -32768, 4}), "moyenne des deux canaux, arrondie, sans dépassement");
        std::vector<int16_t> m = {1, 2, 3};
        downmix_to_mono(m, 1);
        CHECK(m == std::vector<int16_t>({1, 2, 3}), "mono : inchangé");
        std::vector<int16_t> odd = {1, 3, 5};
        downmix_to_mono(odd, 2);
        CHECK(odd == std::vector<int16_t>({2}), "image incomplète abandonnée");
        std::vector<int16_t> empty;
        downmix_to_mono(empty, 2);
        CHECK(empty.empty(), "vide");

        Spec s;
        s.channels = 2;
        write_wav(PATH, s, pcm({1000, 3000, -1000, -3000}));
        std::vector<int16_t> out;
        std::string          err;
        CHECK(load_wav_mono(PATH, out, err) && out == std::vector<int16_t>({2000, -2000}), "load_wav_mono : stéréo mélangée");
        Spec f;
        f.tag_id = 3;
        f.bits   = 32;
        write_wav(PATH, f, floats({0.5f}));
        CHECK(load_wav_mono(PATH, out, err) && out == std::vector<int16_t>({16384}), "load_wav_mono : flottant mono");
    }

    std::remove(PATH);
    if (failures == 0) std::printf("OK : test_wav\n");
    return failures == 0 ? 0 : 1;
}
