// Intégration complète, sans fenêtre : vrai moteur, vraie bibliothèque de samples (dossier), vrai
// fichier de profil. Une loop est construite avec l'UI, sauvegardée, puis rechargée dans une machine
// neuve : l'audio produit doit être identique.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "../config.h"
#include "../core/engine.h"
#include "../core/ui/ui_controller.h"
#include "../platform/desktop/profile_store_file.h"
#include "../platform/desktop/sample_bank.h"
#include "../platform/desktop/sample_library.h"

namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

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

// WAV mono 16 bits 44,1 kHz.
static void write_wav(const std::string& path, const std::vector<int16_t>& pcm) {
    std::vector<uint8_t> b;
    const uint32_t       bytes = static_cast<uint32_t>(pcm.size() * 2);
    tag(b, "RIFF"); put32(b, 36 + bytes); tag(b, "WAVE");
    tag(b, "fmt "); put32(b, 16); put16(b, 1); put16(b, 1);
    put32(b, cfg::SAMPLE_RATE); put32(b, cfg::SAMPLE_RATE * 2); put16(b, 2); put16(b, 16);
    tag(b, "data"); put32(b, bytes);
    for (int16_t s : pcm) put16(b, static_cast<uint16_t>(s));
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

// Une machine complète : moteur + samples + profil + UI.
struct Machine {
    Engine               engine;
    SampleBank           bank;
    DesktopSampleLibrary library;
    FileProfileStore     store;
    UiController         ui;
    uint32_t             now = 0;

    Machine(const std::string& samples, const std::string& profile_path)
        : library(samples, bank), store(profile_path), ui(engine, &library, &store) {
        ui.load_profile();
    }

    // Fait tourner le moteur un instant : il applique les commandes et rend les buffers inutiles.
    void settle() {
        float l[cfg::BLOCK], r[cfg::BLOCK];
        engine.render(l, r, cfg::BLOCK);
        for (const int16_t* p; engine.pop_released(p);) bank.release(p);
        now += 20;
        ui.update(engine.state(), now);
    }
    void act(std::initializer_list<InputEvent> events) {
        for (const InputEvent& e : events) {
            ui.handle(e);
            settle();
        }
    }
    std::vector<float> play(int frames) {
        engine.push({Command::Play, 0, 0, 0.0f});
        std::vector<float> out(static_cast<std::size_t>(frames)), unused(static_cast<std::size_t>(frames));
        engine.render(out.data(), unused.data(), frames);
        return out;
    }
};

int main() {
    const std::string dir     = "test_integration_dir";
    const std::string samples = dir + "/samples";
    const std::string profile = dir + "/profiles/default.json";
    const int         frames  = 130000;  // un peu plus d'une mesure à 90 BPM
    fs::remove_all(dir);
    fs::create_directories(samples);

    // Deux samples distincts : « click » (fort, court) et « hat » (faible, un peu plus long).
    write_wav(samples + "/click.wav", {30000, 12000, 4000, 0});
    write_wav(samples + "/hat.wav", {9000, -9000, 6000, -6000, 3000, 0});

    std::vector<float> reference;
    {
        auto m = std::unique_ptr<Machine>(new Machine(samples, profile));
        CHECK(m->library.count() == 2 && m->ui.profile().loop_count == 0, "machine neuve : 2 samples, profil vide");

        // Piste 1 : click aux pas 1 et 5 ; piste 2 : hat au pas 3 ; BPM 90.
        m->act({InputEvent::ok(), InputEvent::ok()});  // « + Ajouter piste », puis le premier sample (click)
        CHECK(m->ui.track_count() == 1 && m->ui.view() == UiController::View::Pattern, "piste click créée, son pattern est ouvert");
        m->act({InputEvent::step(0), InputEvent::step(4), InputEvent::back()});
        m->act({InputEvent::wheel(1), InputEvent::ok(), InputEvent::wheel(1), InputEvent::ok()});  // « + Ajouter », hat
        CHECK(m->ui.track_count() == 2, "piste hat créée");
        m->act({InputEvent::step(2), InputEvent::back()});
        m->act({InputEvent::pot_bpm(30.0f / 140.0f)});
        CHECK(m->ui.bpm() == 90, "BPM 90 au potard");
        CHECK(m->ui.dirty() && m->ui.current_loop() == -1, "loop modifiée, jamais sauvegardée");

        // Sauvegarde par le menu de la loop (ligne de titre).
        m->act({InputEvent::wheel(-5), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok()});
        CHECK(m->ui.current_loop() == 0 && !m->ui.dirty(), "sauvegardée : loop 1");
        CHECK(fs::exists(profile), "le fichier de profil existe");

        reference = m->play(frames);
        float peak = 0.0f;
        for (float v : reference) peak = std::fmax(peak, std::fabs(v));
        CHECK(peak > 0.3f && reference[0] != 0.0f, "la loop sonne (un trig au premier échantillon)");
        // Pas = 44100 * 60 / 90 / 4 = 7350 échantillons : le hat est au pas 2, soit à l'échantillon 14700.
        CHECK(reference[14700] != 0.0f && reference[14699] == 0.0f, "le hat tombe à l'échantillon 14700 exactement");
    }

    // Machine neuve : même dossier de samples, même fichier de profil.
    {
        auto m = std::unique_ptr<Machine>(new Machine(samples, profile));
        CHECK(m->ui.profile().loop_count == 1 && m->ui.track_count() == 0 && m->ui.current_loop() == -1, "démarre vide, avec 1 loop dans le profil");

        m->act({InputEvent::wheel(-5), InputEvent::ok(), InputEvent::ok()});  // titre, liste des loops, loop 1
        CHECK(!m->ui.loading() && m->ui.track_count() == 2 && m->ui.current_loop() == 0 && !m->ui.dirty(), "loop chargée");
        CHECK(std::strcmp(m->ui.track_name(0), "click") == 0 && std::strcmp(m->ui.track_name(1), "hat") == 0 &&
                  m->ui.pattern_bits(0) == 0x0011 && m->ui.pattern_bits(1) == 0x0004 && m->ui.bpm() == 90,
              "pistes, patterns et BPM restitués");
        CHECK(m->bank.count() == 2, "deux samples chargés en mémoire");

        const std::vector<float> again = m->play(frames);
        CHECK(again == reference, "l'audio de la loop rechargée est identique à celui de la loop d'origine");

        // « + Nouvelle loop » : le moteur rend ses buffers, la banque se vide.
        m->act({InputEvent::wheel(-5), InputEvent::ok(), InputEvent::wheel(1), InputEvent::ok()});
        CHECK(m->ui.track_count() == 0 && m->ui.current_loop() == -1, "machine vidée");
        CHECK(m->bank.count() == 0, "les buffers sont rendus par le moteur puis libérés");
    }

    // Un sample qui a disparu : la piste est sautée, le reste joue.
    fs::remove(samples + "/hat.wav");
    {
        auto m = std::unique_ptr<Machine>(new Machine(samples, profile));
        CHECK(m->library.count() == 1, "un seul sample disponible");
        m->act({InputEvent::wheel(-5), InputEvent::ok(), InputEvent::ok()});
        CHECK(m->ui.track_count() == 1 && std::strcmp(m->ui.track_name(0), "click") == 0, "seule la piste click est restituée");
        CHECK(std::strcmp(m->ui.notice(), " 1 sample manquant") == 0, "message : 1 sample manquant");

        const std::vector<float> partial = m->play(frames);
        CHECK(partial[0] == reference[0] && partial[14700] == 0.0f, "le click joue, le hat est absent");

        // Resauvegarder retire le hat de la loop : le profil ne garde que ce qui existe.
        m->act({InputEvent::wheel(-5), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok(), InputEvent::ok()});
        CHECK(m->ui.profile().sample_count == 1 && m->ui.profile().loops[0].track_count == 1, "loop resauvegardée sans la piste manquante");
    }

    // Le .bak garde la version précédente du profil.
    CHECK(fs::exists(profile + ".bak") && !fs::exists(profile + ".tmp"), "ancienne version dans .bak, pas de .tmp");

    // Vélocité et pitch par pas, de bout en bout : fichier JSON écrit à la main -> UI -> moteur.
    {
        write_wav(samples + "/hat.wav", {9000, -9000, 6000, -6000, 3000, 0});  // le hat est de retour
        const std::string custom = dir + "/profiles/custom.json";
        {
            std::ofstream f(custom, std::ios::binary | std::ios::trunc);
            f << R"({"format":"dmp","version":1,"samples":["click.wav","hat.wav"],"loops":[{"bpm":120,"tracks":[
                {"sample":0,"pattern":"x...x...","velocity":"127,127,127,127,64,127,127,127,127,127,127,127,127,127,127,127","step_pitch":"7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"},
                {"sample":1,"gain":0.5,"pattern":"..x.....x.......","velocity":"127,127,40,127,127,127,127,127,90,127,127,127,127,127,127,127","step_pitch":"0,0,-12,0,0,0,0,0,12,0,0,0,0,0,0,0"}]}]})";
        }

        auto m = std::unique_ptr<Machine>(new Machine(samples, custom));
        CHECK(m->ui.profile().loop_count == 1, "profil manuscrit lu");
        m->act({InputEvent::wheel(-5), InputEvent::ok(), InputEvent::ok()});
        CHECK(m->ui.track_count() == 2 && m->ui.step_velocity(0, 4) == 64 && m->ui.step_pitch(0, 0) == 7 && m->ui.step_pitch(1, 2) == -12,
              "chargé : réglages par pas dans le modèle");
        const std::vector<float> from_file = m->play(frames);

        // Référence : un moteur configuré directement, sans fichier ni UI.
        auto build_reference = [&](bool with_extras) {
            Engine               ref;
            SampleBank           bank;
            DesktopSampleLibrary lib(samples, bank);  // trié : click.wav = 0, hat.wav = 1
            const int16_t*       d0 = nullptr;
            const int16_t*       d1 = nullptr;
            uint32_t             n0 = 0, n1 = 0;
            lib.load(0, d0, n0);
            lib.load(1, d1, n1);

            auto cmd = [](Command::Type type, int track, int step, float value) {
                Command c;
                c.type  = type;
                c.track = static_cast<uint8_t>(track);
                c.step  = static_cast<uint8_t>(step);
                c.value = value;
                return c;
            };
            Command s0 = cmd(Command::SetTrackSample, 0, 0, 0.0f);
            s0.data = d0;
            s0.length = n0;
            Command s1 = cmd(Command::SetTrackSample, 1, 0, 0.0f);
            s1.data = d1;
            s1.length = n1;
            ref.push(s0);
            ref.push(s1);
            ref.push(cmd(Command::SetTrackGain, 1, 0, 0.5f));
            Command p0 = cmd(Command::SetTrackPattern, 0, 0, 0.0f);
            p0.length  = 0x0011;
            Command p1 = cmd(Command::SetTrackPattern, 1, 0, 0.0f);
            p1.length  = 0x0104;
            ref.push(p0);
            ref.push(p1);
            if (with_extras) {
                ref.push(cmd(Command::SetStepVelocity, 0, 4, 64.0f));
                ref.push(cmd(Command::SetStepPitch, 0, 0, 7.0f));
                ref.push(cmd(Command::SetStepVelocity, 1, 2, 40.0f));
                ref.push(cmd(Command::SetStepVelocity, 1, 8, 90.0f));
                ref.push(cmd(Command::SetStepPitch, 1, 2, -12.0f));
                ref.push(cmd(Command::SetStepPitch, 1, 8, 12.0f));
            }
            ref.push(cmd(Command::Play, 0, 0, 0.0f));
            std::vector<float> out(static_cast<std::size_t>(frames)), unused(static_cast<std::size_t>(frames));
            ref.render(out.data(), unused.data(), frames);
            return out;
        };
        CHECK(from_file == build_reference(true), "l'audio chargé depuis le fichier = celui d'un moteur réglé directement");
        CHECK(from_file != build_reference(false), "les vélocités et pitchs changent bien le son");

        // Sauvegarde puis relecture dans une machine neuve : même audio.
        m->act({InputEvent::wheel(-5), InputEvent::menu(), InputEvent::wheel(1), InputEvent::ok(), InputEvent::ok()});
        CHECK(!m->ui.dirty() && fs::exists(custom + ".bak"), "resauvegardé (la version précédente est dans .bak)");
        m.reset();
        auto m2 = std::unique_ptr<Machine>(new Machine(samples, custom));
        m2->act({InputEvent::wheel(-5), InputEvent::ok(), InputEvent::ok()});
        CHECK(m2->play(frames) == from_file, "après sauvegarde et relecture : audio identique");
    }

    fs::remove_all(dir);
    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
