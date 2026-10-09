#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../config.h"
#include "../core/sample_player.h"
#include <filesystem>

#include "../platform/desktop/sample_bank.h"
#include "../platform/desktop/sample_library.h"
#include "../platform/desktop/sample_loader.h"

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

static void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
static void put32(std::vector<uint8_t>& b, uint32_t v) {
    put16(b, static_cast<uint16_t>(v));
    put16(b, static_cast<uint16_t>(v >> 16));
}
static void tag(std::vector<uint8_t>& b, const char* t) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(t[i]));
}

// WAV minimal, avec un chunk LIST avant 'data' pour vérifier que le parcours des chunks fonctionne.
static bool write_test_wav(const char* path, uint16_t channels, uint32_t rate,
                           const std::vector<int16_t>& samples) {
    std::vector<uint8_t> b;
    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * 2);
    tag(b, "RIFF");
    put32(b, 4 + 24 + 12 + 8 + data_bytes);
    tag(b, "WAVE");
    tag(b, "fmt ");
    put32(b, 16);
    put16(b, 1);
    put16(b, channels);
    put32(b, rate);
    put32(b, rate * channels * 2);
    put16(b, static_cast<uint16_t>(channels * 2));
    put16(b, 16);
    tag(b, "LIST");
    put32(b, 4);
    tag(b, "INFO");
    tag(b, "data");
    put32(b, data_bytes);
    for (int16_t s : samples) put16(b, static_cast<uint16_t>(s));

    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fwrite(b.data(), 1, b.size(), f);
    std::fclose(f);
    return true;
}

int main() {
    // Rampe : data[k] = k * 1000 -> valeurs float k * 1000 / 32768.
    std::vector<int16_t> ramp = {0, 1000, 2000, 3000, 4000};
    auto expect = [&](float k) { return k * 1000.0f / 32768.0f; };

    SamplePlayer p;
    p.set_sample(ramp.data(), static_cast<uint32_t>(ramp.size()));

    float out[16];

    // Pitch 1.0 : restitue le sample tel quel, puis s'arrête.
    {
        for (float& v : out) v = 0.0f;
        p.set_pitch(1.0f);
        p.trigger(1.0f);
        p.render(out, 16);
        for (int k = 0; k < 5; ++k) CHECK(near(out[k], expect(static_cast<float>(k))), "pitch 1.0 identique");
        CHECK(out[5] == 0.0f, "silence après la fin");
        CHECK(!p.active(), "inactif après la fin");
    }

    // Pitch 0.5 : interpolation linéaire entre échantillons (x.5 au milieu).
    {
        for (float& v : out) v = 0.0f;
        p.set_pitch(0.5f);
        p.trigger(1.0f);
        p.render(out, 16);
        for (int i = 0; i < 8; ++i) CHECK(near(out[i], expect(0.5f * static_cast<float>(i))), "pitch 0.5 interpolé");
    }

    // Pitch 2.0 : un échantillon sur deux.
    {
        for (float& v : out) v = 0.0f;
        p.set_pitch(2.0f);
        p.trigger(1.0f);
        p.render(out, 16);
        CHECK(near(out[0], expect(0)) && near(out[1], expect(2)) && near(out[2], expect(4)), "pitch 2.0 saute un échantillon sur deux");
    }

    // Vélocité = gain.
    {
        for (float& v : out) v = 0.0f;
        p.set_pitch(1.0f);
        p.trigger(0.5f);
        p.render(out, 16);
        CHECK(near(out[2], 0.5f * expect(2)), "vélocité appliquée");
    }

    // Rendu en plusieurs morceaux == rendu en un seul.
    {
        float a[16] = {}, b[16] = {};
        p.set_pitch(0.7f);
        p.trigger(1.0f);
        p.render(a, 16);
        p.trigger(1.0f);
        p.render(b, 3);
        p.render(b + 3, 5);
        p.render(b + 8, 8);
        bool same = true;
        for (int i = 0; i < 16; ++i) same = same && (a[i] == b[i]);
        CHECK(same, "indépendant du découpage en blocs");
    }

    // Sans sample : trigger sans effet.
    {
        SamplePlayer empty;
        empty.trigger(1.0f);
        CHECK(!empty.active(), "pas de sample -> inactif");
    }

    // Chargeur WAV.
    {
        std::vector<int16_t> src = {1, -2, 300, -32768, 32767};
        const char*          path = "test_sample_mono.wav";
        std::vector<int16_t> loaded;
        std::string          err;

        CHECK(write_test_wav(path, 1, cfg::SAMPLE_RATE, src), "écriture du WAV de test");
        CHECK(load_wav_mono16(path, loaded, err), "chargement d'un WAV valide");
        CHECK(loaded == src, "données chargées identiques");

        write_test_wav(path, 2, cfg::SAMPLE_RATE, src);
        CHECK(!load_wav_mono16(path, loaded, err), "stéréo refusé");
        CHECK(err.find("afconvert") != std::string::npos, "message d'erreur avec la conversion");

        write_test_wav(path, 1, 48000, src);
        CHECK(!load_wav_mono16(path, loaded, err), "48 kHz refusé");

        CHECK(!load_wav_mono16("fichier_inexistant.wav", loaded, err), "fichier absent refusé");
        std::remove(path);
    }

    // SampleBank : possède les buffers, pointeurs stables, libération à la demande.
    {
        const char*          path = "test_sample_bank.wav";
        std::vector<int16_t> src  = {5, 6, 7, 8};
        write_test_wav(path, 1, cfg::SAMPLE_RATE, src);

        SampleBank     bank;
        const int16_t* a = nullptr;
        const int16_t* b = nullptr;
        uint32_t       la = 0, lb = 0;
        std::string    err;

        CHECK(bank.load(path, a, la, err) && la == 4 && a[2] == 7, "bank : chargement");
        CHECK(bank.load(path, b, lb, err) && a != b, "bank : deux chargements = deux buffers");
        CHECK(bank.count() == 2, "bank : deux buffers possédés");

        bank.release(a);
        CHECK(bank.count() == 1 && b[3] == 8, "bank : libération d'un buffer, l'autre reste valide");
        bank.release(a);  // pointeur déjà libéré : ignoré
        CHECK(bank.count() == 1, "bank : double libération ignorée");

        CHECK(!bank.load("fichier_inexistant.wav", a, la, err), "bank : fichier absent refusé");
        CHECK(bank.count() == 1, "bank : un échec n'ajoute rien");
        std::remove(path);
    }

    // DesktopSampleLibrary : ne liste que les WAV utilisables, triés sans tenir compte de la casse.
    {
        namespace fs = std::filesystem;
        const std::string dir = "test_sample_library_dir";
        fs::remove_all(dir);
        fs::create_directory(dir);

        const std::vector<int16_t> pcm = {10, 20, 30};
        write_test_wav((dir + "/snare.wav").c_str(), 1, cfg::SAMPLE_RATE, pcm);
        write_test_wav((dir + "/Kick.WAV").c_str(), 1, cfg::SAMPLE_RATE, pcm);
        write_test_wav((dir + "/stereo.wav").c_str(), 2, cfg::SAMPLE_RATE, pcm);      // mauvais format
        write_test_wav((dir + "/.cache.wav").c_str(), 1, cfg::SAMPLE_RATE, pcm);      // fichier caché
        std::FILE* f = std::fopen((dir + "/notes.txt").c_str(), "wb");               // pas un WAV
        std::fclose(f);

        SampleBank           bank;
        DesktopSampleLibrary lib(dir, bank);
        CHECK(lib.count() == 2, "bibliothèque : 2 samples utilisables sur 5 fichiers");
        CHECK(lib.count() == 2 && std::string(lib.name(0)) == "Kick.WAV" && std::string(lib.name(1)) == "snare.wav",
              "bibliothèque : tri alphabétique insensible à la casse");

        const int16_t* data = nullptr;
        uint32_t       len  = 0;
        CHECK(lib.load(1, data, len) && len == 3 && data[2] == 30, "bibliothèque : chargement dans la banque");
        CHECK(bank.count() == 1, "la banque possède le buffer");
        lib.discard(data);
        CHECK(bank.count() == 0, "discard libère le buffer");
        CHECK(!lib.load(5, data, len) && !lib.load(-1, data, len), "index invalide refusé");

        write_test_wav((dir + "/new.wav").c_str(), 1, cfg::SAMPLE_RATE, pcm);
        lib.refresh();
        CHECK(lib.count() == 3, "refresh : le nouveau fichier apparaît");

        SampleBank           bank2;
        DesktopSampleLibrary missing("dossier_inexistant", bank2);
        CHECK(missing.count() == 0, "dossier inexistant : liste vide, sans plantage");

        fs::remove_all(dir);
    }

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
