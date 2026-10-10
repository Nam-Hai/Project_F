#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "../core/profile/profile_codec.h"

using namespace profile;

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Grands objets : statiques, comme sur Teensy (jamais sur la pile).
static ProfileCodec codec;
static ProfileData  parsed;
static ProfileData  source;
static char         buffer[96 * 1024];

static ProfileStatus parse(const std::string& text) { return codec.parse(text.data(), text.size(), parsed); }

// Profil minimal valide, avec un morceau de JSON à insérer dans une piste, une loop, ou à la racine.
static std::string doc(const std::string& root_extra = "", const std::string& loop_extra = "",
                       const std::string& track_extra = "") {
    return "{\"format\":\"dmp\",\"version\":1," + root_extra +
           "\"samples\":[\"a.wav\",\"b.wav\"],\"loops\":[{" + loop_extra +
           "\"bpm\":100,\"tracks\":[{" + track_extra + "\"sample\":1,\"pattern\":\"x...\"}]}]}";
}

int main() {
    // ----- Aller-retour : écriture puis lecture -----
    {
        source = ProfileData{};
        std::strcpy(source.name, "Techno");
        source.add_sample("Kick1.wav");
        source.add_sample("Snare1.wav");
        source.add_sample("HH \"open\".wav");  // guillemets dans le nom : échappés

        source.loop_count = 2;
        source.loops[0].bpm         = 120.0f;
        source.loops[0].track_count = 2;
        source.loops[0].tracks[0]   = {0, 1.0f, 1.0f, 0x1111};
        source.loops[0].tracks[1]   = {1, 0.8f, 0.5f, 0x0410};
        source.loops[1].bpm         = 133.5f;
        source.loops[1].track_count = 1;
        source.loops[1].tracks[0]   = {2, 0.125f, 2.0f, 0x8001};

        const int len = ProfileCodec::write(source, buffer, sizeof buffer);
        CHECK(len > 0 && static_cast<std::size_t>(len) == std::strlen(buffer), "écriture : longueur cohérente");
        CHECK(std::strstr(buffer, "\"version\": 1") && std::strstr(buffer, "\"pattern\": \"x...x...x...x...\""),
              "écriture : version et pattern lisibles");

        CHECK(codec.parse(buffer, static_cast<std::size_t>(len), parsed) == ProfileStatus::Ok, "relecture du JSON écrit");
        CHECK(std::strcmp(parsed.name, "Techno") == 0 && parsed.sample_count == 3 && parsed.loop_count == 2, "nom, comptes");
        CHECK(std::strcmp(parsed.samples[2], "HH \"open\".wav") == 0, "nom de sample avec guillemets");
        CHECK(near(parsed.loops[0].bpm, 120.0f) && parsed.loops[0].track_count == 2, "loop 1");
        CHECK(parsed.loops[0].tracks[1].sample == 1 && near(parsed.loops[0].tracks[1].gain, 0.8f) &&
                  near(parsed.loops[0].tracks[1].pitch, 0.5f) && parsed.loops[0].tracks[1].pattern == 0x0410,
              "piste 2 de la loop 1");
        CHECK(near(parsed.loops[1].bpm, 133.5f) && near(parsed.loops[1].tracks[0].gain, 0.125f) &&
                  parsed.loops[1].tracks[0].pattern == 0x8001,
              "loop 2 : décimales et pattern aux extrémités");

        // Écrire ce qu'on vient de lire redonne exactement le même texte.
        static char again[96 * 1024];
        CHECK(ProfileCodec::write(parsed, again, sizeof again) == len && std::strcmp(again, buffer) == 0,
              "écriture stable (lire puis écrire donne le même texte)");

        // Taille de buffer : juste assez fonctionne, un octet de moins échoue.
        CHECK(ProfileCodec::write(source, buffer, static_cast<std::size_t>(len) + 1) == len, "buffer exactement assez grand");
        CHECK(ProfileCodec::write(source, buffer, static_cast<std::size_t>(len)) == -1, "buffer trop petit : refusé");
        CHECK(ProfileCodec::write(source, buffer, 0) == -1, "buffer vide refusé");
    }

    // ----- Profil vide -----
    {
        source = ProfileData{};
        const int len = ProfileCodec::write(source, buffer, sizeof buffer);
        CHECK(len > 0 && codec.parse(buffer, static_cast<std::size_t>(len), parsed) == ProfileStatus::Ok, "profil vide : aller-retour");
        CHECK(parsed.sample_count == 0 && parsed.loop_count == 0 && std::strcmp(parsed.name, "Default") == 0, "profil vide : contenu");
    }

    // ----- Fichier écrit à la main : espaces, ordre des clés, clés inconnues, valeurs par défaut -----
    {
        const std::string text = R"(
        {
          "future_feature": { "nested": [1, 2, {"x": null}], "flag": true },
          "samples": [ "Kick1.wav" ],
          "version": 1,
          "format": "dmp",
          "loops": [
            { "note": "intro", "tracks": [ { "sample": 0, "extra": [1,2,3] } ] },
            { "bpm": 90, "tracks": [] }
          ]
        })";
        CHECK(parse(text) == ProfileStatus::Ok, "fichier manuscrit accepté");
        CHECK(parsed.loop_count == 2 && parsed.loops[0].track_count == 1 && parsed.loops[1].track_count == 0, "comptes");
        CHECK(near(parsed.loops[0].bpm, 120.0f) && near(parsed.loops[0].tracks[0].gain, 1.0f) &&
                  near(parsed.loops[0].tracks[0].pitch, 1.0f) && parsed.loops[0].tracks[0].pattern == 0,
              "champs absents : valeurs par défaut");
        CHECK(near(parsed.loops[1].bpm, 90.0f), "bpm entier");
        CHECK(std::strcmp(parsed.name, "Default") == 0, "nom de profil par défaut");

        CHECK(parse(doc("\"unknown\": [[[[1]]]],", "\"x\": {},", "\"y\": \"z\",")) == ProfileStatus::Ok,
              "clés inconnues ignorées à tous les niveaux");
    }

    // ----- Nombres -----
    {
        CHECK(parse(doc("", "", "\"gain\": 1e0, \"pitch\": -0.5,")) == ProfileStatus::Ok &&
                  near(parsed.loops[0].tracks[0].gain, 1.0f) && near(parsed.loops[0].tracks[0].pitch, -0.5f),
              "exposant et négatif");
        CHECK(parse(doc("", "", "\"gain\": 2.5E-1,")) == ProfileStatus::Ok && near(parsed.loops[0].tracks[0].gain, 0.25f), "exposant négatif");
        CHECK(parse(doc("", "", "\"gain\": \"fort\",")) == ProfileStatus::BadField, "gain en texte refusé");
        CHECK(parse(doc("", "", "\"gain\": 1.,")) != ProfileStatus::Ok, "« 1. » refusé");
        CHECK(parse(doc("", "", "\"gain\": 1,5,")) != ProfileStatus::Ok, "virgule décimale refusée (JSON invalide)");
        CHECK(parse(doc("", "", "\"gain\": true,")) == ProfileStatus::BadField, "booléen à la place d'un nombre refusé");
        CHECK(parse(doc("", "", "\"gain\": null,")) == ProfileStatus::BadField, "null à la place d'un nombre refusé");
    }

    // ----- Fichier coupé, invalide, ou qui n'est pas un profil -----
    {
        source = ProfileData{};
        source.add_sample("Kick1.wav");
        source.loop_count = 1;
        source.loops[0].track_count = 1;
        const int len = ProfileCodec::write(source, buffer, sizeof buffer);

        bool all_truncated = true;
        for (int cut = 1; cut < len - 2; cut += 7) {  // coupé à de nombreux endroits
            const ProfileStatus s = codec.parse(buffer, static_cast<std::size_t>(cut), parsed);
            all_truncated = all_truncated && s != ProfileStatus::Ok;
        }
        CHECK(all_truncated, "un fichier coupé n'est jamais accepté");
        CHECK(codec.parse(buffer, static_cast<std::size_t>(len) - 3, parsed) == ProfileStatus::Truncated, "fin manquante : « coupé »");

        CHECK(parse("") == ProfileStatus::InvalidJson && parse("   \n") == ProfileStatus::InvalidJson, "vide");
        CHECK(parse("{\"a\" 1}") != ProfileStatus::Ok, "deux-points manquant");
        CHECK(parse("{} {}") == ProfileStatus::InvalidJson, "texte en trop après la racine");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1} x") == ProfileStatus::InvalidJson, "déchets après la racine");
        CHECK(parse("[1,2,3]") == ProfileStatus::NotAProfile, "tableau à la racine");
        CHECK(parse("42") != ProfileStatus::Ok, "nombre à la racine");
        CHECK(parse("{}") == ProfileStatus::NotAProfile, "objet vide");
        CHECK(parse("{\"format\":\"autre\",\"version\":1}") == ProfileStatus::NotAProfile, "autre format");
        CHECK(parse("{\"format\":\"dmp\"}") == ProfileStatus::NotAProfile, "version absente");
        CHECK(parse("{\"format\":\"dmp\",\"version\":2}") == ProfileStatus::TooNew, "version trop récente");
        CHECK(parse("{\"format\":\"dmp\",\"version\":0}") == ProfileStatus::BadField, "version 0");
        CHECK(parse("{\"format\":\"dmp\",\"version\":\"1\"}") == ProfileStatus::BadField, "version en texte");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1.5}") == ProfileStatus::BadField, "version non entière");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1}") == ProfileStatus::Ok && parsed.loop_count == 0, "profil minimal valide");

        // Un contenu binaire avec un zéro au milieu ne plante pas.
        const char junk[] = {'{', '"', 'f', '\0', '"', '}', 0x01, 0x7f};
        CHECK(codec.parse(junk, sizeof junk, parsed) != ProfileStatus::Ok, "octets quelconques refusés sans plantage");
    }

    // ----- Champs invalides -----
    {
        // Clé en double : la première occurrence l'emporte (doc() place la clé en plus avant la normale).
        CHECK(parse(doc("", "", "\"sample\": 0,")) == ProfileStatus::Ok && parsed.loops[0].tracks[0].sample == 0,
              "clé en double : la première occurrence l'emporte");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[\"a\"],\"loops\":[{\"tracks\":[{\"sample\":1}]}]}") == ProfileStatus::BadField,
              "index de sample hors de la table");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[\"a\"],\"loops\":[{\"tracks\":[{\"sample\":-1}]}]}") == ProfileStatus::BadField,
              "index de sample négatif");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[\"a\"],\"loops\":[{\"tracks\":[{\"sample\":0.5}]}]}") == ProfileStatus::BadField,
              "index de sample non entier");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[\"a\"],\"loops\":[{\"tracks\":[{}]}]}") == ProfileStatus::BadField,
              "piste sans sample");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[\"a\"],\"loops\":[{\"tracks\":[3]}]}") == ProfileStatus::BadField,
              "piste qui n'est pas un objet");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"loops\":[5]}") == ProfileStatus::BadField, "loop qui n'est pas un objet");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":{}}") == ProfileStatus::BadField, "samples qui n'est pas un tableau");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[1]}") == ProfileStatus::BadField, "nom de sample qui n'est pas du texte");
        CHECK(parse(doc("", "", "\"pattern\": \"..x.\",")) == ProfileStatus::Ok && parsed.loops[0].tracks[0].pattern == 0x0004,
              "pattern en double : le premier l'emporte");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[\"a\"],\"loops\":[{\"tracks\":[{\"sample\":0,\"pattern\":\"x..?\"}]}]}") == ProfileStatus::BadField,
              "caractère inconnu dans un pattern");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[\"a\"],\"loops\":[{\"tracks\":[{\"sample\":0,\"pattern\":\"x...x...x...x...x\"}]}]}") == ProfileStatus::BadField,
              "pattern de 17 pas");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[\"a\"],\"loops\":[{\"tracks\":[{\"sample\":0,\"pattern\":\"X.X.\"}]}]}") == ProfileStatus::Ok &&
                  parsed.loops[0].tracks[0].pattern == 0x0005,
              "majuscules acceptées, pattern court");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[\"\\u00e9.wav\"]}") == ProfileStatus::BadField,
              "échappement \\u refusé");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[\"é.wav\"]}") == ProfileStatus::Ok && std::strcmp(parsed.samples[0], "é.wav") == 0,
              "UTF-8 brut accepté");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"profile\":\"nom_vraiment_beaucoup_trop_long_pour_un_profil\"}") == ProfileStatus::BadField,
              "nom de profil trop long");
        const std::string long_name(60, 'n');
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[\"" + long_name + "\"]}") == ProfileStatus::BadField, "nom de sample trop long");
    }

    // ----- Limites des tableaux fixes -----
    {
        std::string samples;
        for (int i = 0; i < MAX_SAMPLES + 1; ++i) samples += (i ? ",\"s\"" : "\"s\"");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[" + samples + "]}") == ProfileStatus::TooBig, "65 samples");

        std::string loops;
        for (int i = 0; i < MAX_LOOPS + 1; ++i) loops += (i ? ",{}" : "{}");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"loops\":[" + loops + "]}") == ProfileStatus::TooBig, "33 loops");

        std::string tracks;
        for (int i = 0; i < cfg::MAX_TRACKS + 1; ++i) tracks += (i ? ",{\"sample\":0}" : "{\"sample\":0}");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"samples\":[\"a\"],\"loops\":[{\"tracks\":[" + tracks + "]}]}") == ProfileStatus::TooBig,
              "17 pistes");

        std::string many = "[";
        for (int i = 0; i < ProfileCodec::MAX_TOKENS + 10; ++i) many += (i ? ",1" : "1");
        many += "]";
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"big\":" + many + "}") == ProfileStatus::TooBig, "trop de jetons");

        // Profil plein : MAX_LOOPS (12) loops de 16 pistes passent par le tableau de jetons.
        source = ProfileData{};
        source.add_sample("Kick1.wav");
        source.loop_count = MAX_LOOPS;
        for (int l = 0; l < MAX_LOOPS; ++l) {
            source.loops[l].track_count = cfg::MAX_TRACKS;
            for (int t = 0; t < cfg::MAX_TRACKS; ++t) source.loops[l].tracks[t] = {0, 0.75f, 1.25f, static_cast<uint16_t>(l * 100 + t)};
        }
        const int len = ProfileCodec::write(source, buffer, sizeof buffer);
        CHECK(len > 0 && codec.parse(buffer, static_cast<std::size_t>(len), parsed) == ProfileStatus::Ok &&
                  parsed.loop_count == MAX_LOOPS && parsed.loops[MAX_LOOPS - 1].tracks[15].pattern == static_cast<uint16_t>((MAX_LOOPS - 1) * 100 + 15),
              "profil plein (12 loops x 16 pistes) : aller-retour");
        std::printf("profil plein : %d octets de JSON\n", len);
    }

    // ----- Vélocité et pitch par pas -----
    {
        auto count = [](const char* text, const char* what) {
            int n = 0;
            for (const char* p = text; (p = std::strstr(p, what)) != nullptr; ++p) ++n;
            return n;
        };
        auto all_default = [](const TrackData& t) {
            for (int s = 0; s < cfg::MAX_STEPS; ++s)
                if (t.step_velocity[s] != cfg::VELOCITY_MAX || t.step_pitch[s] != 0) return false;
            return true;
        };

        source = ProfileData{};
        source.add_sample("a.wav");
        source.loop_count           = 1;
        source.loops[0].track_count = 3;
        source.loops[0].tracks[1].step_velocity[4]  = 90;
        source.loops[0].tracks[1].step_velocity[15] = 1;
        source.loops[0].tracks[2].step_pitch[0]     = -12;
        source.loops[0].tracks[2].step_pitch[3]     = 12;
        source.loops[0].tracks[2].step_pitch[7]     = -5;
        source.loops[0].tracks[2].step_velocity[2]  = 64;

        const int len = ProfileCodec::write(source, buffer, sizeof buffer);
        CHECK(len > 0, "écriture avec vélocités et pitchs");
        CHECK(count(buffer, "\"velocity\"") == 2 && count(buffer, "\"step_pitch\"") == 1,
              "les listes ne sont écrites que pour les pistes qui s'écartent des valeurs par défaut");
        CHECK(std::strstr(buffer, "\"velocity\": \"127,127,127,127,90,127,127,127,127,127,127,127,127,127,127,1\"") != nullptr,
              "liste de vélocités lisible, 16 valeurs");
        CHECK(std::strstr(buffer, "\"step_pitch\": \"-12,0,0,12,0,0,0,-5,0,0,0,0,0,0,0,0\"") != nullptr, "liste de pitchs lisible");

        CHECK(codec.parse(buffer, static_cast<std::size_t>(len), parsed) == ProfileStatus::Ok, "relecture");
        CHECK(all_default(parsed.loops[0].tracks[0]), "piste sans listes : valeurs par défaut");
        bool same = true;
        for (int t = 1; t < 3; ++t)
            for (int s = 0; s < cfg::MAX_STEPS; ++s)
                same = same && parsed.loops[0].tracks[t].step_velocity[s] == source.loops[0].tracks[t].step_velocity[s] &&
                       parsed.loops[0].tracks[t].step_pitch[s] == source.loops[0].tracks[t].step_pitch[s];
        CHECK(same, "vélocités et pitchs restitués pas par pas");

        static char again[96 * 1024];
        CHECK(ProfileCodec::write(parsed, again, sizeof again) == len && std::strcmp(again, buffer) == 0, "écriture stable");

        // Un ancien fichier (sans les listes) se charge avec les valeurs par défaut.
        CHECK(parse(doc()) == ProfileStatus::Ok && all_default(parsed.loops[0].tracks[0]), "ancien fichier : valeurs par défaut");

        // Écrit à la main : espaces, signe +, liste plus courte (le reste garde sa valeur par défaut), liste vide.
        CHECK(parse(doc("", "", "\"velocity\": \"100, 90 ,80\", \"step_pitch\": \"+3,-2\",")) == ProfileStatus::Ok &&
                  parsed.loops[0].tracks[0].step_velocity[0] == 100 && parsed.loops[0].tracks[0].step_velocity[1] == 90 &&
                  parsed.loops[0].tracks[0].step_velocity[2] == 80 && parsed.loops[0].tracks[0].step_velocity[3] == 127 &&
                  parsed.loops[0].tracks[0].step_pitch[0] == 3 && parsed.loops[0].tracks[0].step_pitch[1] == -2 &&
                  parsed.loops[0].tracks[0].step_pitch[2] == 0,
              "listes manuscrites : espaces, signe +, liste courte");
        CHECK(parse(doc("", "", "\"velocity\": \"\", \"step_pitch\": \"\",")) == ProfileStatus::Ok && all_default(parsed.loops[0].tracks[0]),
              "listes vides : valeurs par défaut");

        // Valeurs invalides : refusées, jamais corrigées en silence.
        const char* bad_velocity[] = {"0", "128", "1,2,,3", "1,2,", ",1", "a", "1.5", "1 2", "-", "+", "99999999999", "-5",
                                      "127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127"};
        bool all_refused = true;
        for (const char* v : bad_velocity)
            all_refused = all_refused && parse(doc("", "", std::string("\"velocity\": \"") + v + "\",")) == ProfileStatus::BadField;
        CHECK(all_refused, "vélocités invalides : BadField");
        const char* bad_pitch[] = {"13", "-13", "0,0,x", "1e1", "100000", "--1"};
        all_refused = true;
        for (const char* v : bad_pitch)
            all_refused = all_refused && parse(doc("", "", std::string("\"step_pitch\": \"") + v + "\",")) == ProfileStatus::BadField;
        CHECK(all_refused, "pitchs invalides : BadField");
        CHECK(parse(doc("", "", "\"velocity\": [127,127],")) == ProfileStatus::BadField, "tableau JSON à la place de la chaîne");
        CHECK(parse(doc("", "", "\"velocity\": 127,")) == ProfileStatus::BadField, "nombre à la place de la chaîne");
        CHECK(parse(doc("", "", "\"step_pitch\": null,")) == ProfileStatus::BadField, "null à la place de la chaîne");
        CHECK(parse(doc("", "", "\"velocity\": \"127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127\",")) == ProfileStatus::Ok,
              "16 valeurs : accepté");

        // Écriture de valeurs hors limites : refusée.
        source.loops[0].tracks[1].step_velocity[0] = 0;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : vélocité 0 refusée");
        source.loops[0].tracks[1].step_velocity[0] = 200;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : vélocité 200 refusée");
        source.loops[0].tracks[1].step_velocity[0] = 127;
        source.loops[0].tracks[2].step_pitch[0] = 13;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : pitch 13 refusé");

        // Les copies de loops et le nettoyage de la table de samples gardent les valeurs.
        source.loops[0].tracks[2].step_pitch[0] = -12;
        source.add_sample("b.wav");
        source.compact_samples();
        CHECK(source.loops[0].tracks[2].step_pitch[3] == 12 && source.loops[0].tracks[1].step_velocity[4] == 90, "compact_samples garde les valeurs par pas");
        source.loop_count = 2;
        source.loops[1] = source.loops[0];
        source.remove_loop(0);
        CHECK(source.loops[0].tracks[2].step_pitch[7] == -5, "remove_loop garde les valeurs par pas");
    }

    // ----- Profil plein, avec toutes les listes sur chaque piste : le tableau de jetons suffit -----
    {
        static char big[256 * 1024];
        source = ProfileData{};
        source.add_sample("Kick1.wav");
        source.loop_count = MAX_LOOPS;
        for (int l = 0; l < MAX_LOOPS; ++l) {
            source.loops[l].track_count = cfg::MAX_TRACKS;
            for (int t = 0; t < cfg::MAX_TRACKS; ++t) {
                TrackData& td = source.loops[l].tracks[t];
                td.gain       = 0.75f;
                td.pitch      = 1.25f;
                td.pattern    = static_cast<uint16_t>(l * 100 + t);
                for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                    td.step_velocity[s] = static_cast<uint8_t>(1 + (l + t + s * 7) % 127);
                    td.step_pitch[s]    = static_cast<int8_t>((l + t * 3 + s) % 25 - 12);
                }
            }
        }
        const int len = ProfileCodec::write(source, big, sizeof big);
        CHECK(len > 0, "profil plein avec toutes les listes : écriture");
        const ProfileStatus st = codec.parse(big, static_cast<std::size_t>(len), parsed);
        CHECK(st == ProfileStatus::Ok, "profil plein avec toutes les listes : relecture (jetons suffisants)");
        bool same = st == ProfileStatus::Ok && parsed.loop_count == MAX_LOOPS;
        for (int l = 0; same && l < MAX_LOOPS; ++l)
            for (int t = 0; same && t < cfg::MAX_TRACKS; ++t)
                for (int s = 0; s < cfg::MAX_STEPS; ++s)
                    same = same && parsed.loops[l].tracks[t].step_velocity[s] == source.loops[l].tracks[t].step_velocity[s] &&
                           parsed.loops[l].tracks[t].step_pitch[s] == source.loops[l].tracks[t].step_pitch[s];
        CHECK(same, "toutes les valeurs restituées");
        std::printf("profil plein avec toutes les listes : %d octets de JSON\n", len);
    }

    // ----- Micro-timing : décalages, notes supplémentaires, force de quantification -----
    {
        auto count = [](const char* text, const char* what) {
            int n = 0;
            for (const char* p = text; (p = std::strstr(p, what)) != nullptr; ++p) ++n;
            return n;
        };
        auto timing_default = [](const TrackData& t) {
            if (t.quantize != 0) return false;
            for (int s = 0; s < cfg::MAX_STEPS; ++s)
                if (t.step_offset[s] != 0 || t.extra_count[s] != 0) return false;
            return true;
        };

        source = ProfileData{};
        source.add_sample("a.wav");
        source.loop_count           = 1;
        source.loops[0].track_count = 3;
        TrackData& t1 = source.loops[0].tracks[1];
        t1.step_offset[0] = -64;
        t1.step_offset[4] = 32;
        t1.step_offset[15] = 63;
        t1.quantize = 30;
        TrackData& t2 = source.loops[0].tracks[2];
        t2.extra_count[4] = 3;
        t2.extra[4][0] = {-16, 100, 0};
        t2.extra[4][1] = {16, 80, 5};
        t2.extra[4][2] = {48, 60, -12};
        t2.extra_count[9] = 1;
        t2.extra[9][0] = {-1, 127, 0};

        const int len = ProfileCodec::write(source, buffer, sizeof buffer);
        CHECK(len > 0, "écriture avec micro-timing");
        CHECK(count(buffer, "\"step_offset\"") == 1 && count(buffer, "\"extra_hits\"") == 1 && count(buffer, "\"quantize\"") == 1,
              "les clés ne sont écrites que pour les pistes qui s'écartent des valeurs par défaut");
        CHECK(std::strstr(buffer, "\"step_offset\": \"-64,0,0,0,32,0,0,0,0,0,0,0,0,0,0,63\"") != nullptr, "liste des décalages lisible, 16 valeurs");
        CHECK(std::strstr(buffer, "\"extra_hits\": \"4:-16:100:0:0 4:16:80:5:0 4:48:60:-12:0 9:-1:127:0:0\"") != nullptr,
              "notes supplémentaires : pas:décalage:vélocité:pitch:pan");
        CHECK(std::strstr(buffer, "\"quantize\": 30") != nullptr, "force de quantification en nombre");

        CHECK(codec.parse(buffer, static_cast<std::size_t>(len), parsed) == ProfileStatus::Ok, "relecture");
        const TrackData& r1 = parsed.loops[0].tracks[1];
        const TrackData& r2 = parsed.loops[0].tracks[2];
        CHECK(timing_default(parsed.loops[0].tracks[0]), "piste sans micro-timing : valeurs par défaut");
        CHECK(r1.step_offset[0] == -64 && r1.step_offset[4] == 32 && r1.step_offset[15] == 63 && r1.step_offset[1] == 0 && r1.quantize == 30,
              "décalages et force de quantification restitués");
        CHECK(r2.extra_count[4] == 3 && r2.extra[4][0].offset == -16 && r2.extra[4][0].velocity == 100 && r2.extra[4][1].pitch == 5 &&
                  r2.extra[4][2].offset == 48 && r2.extra[4][2].pitch == -12 && r2.extra_count[9] == 1 && r2.extra[9][0].offset == -1 &&
                  r2.extra_count[0] == 0,
              "notes supplémentaires restituées dans leur ordre");

        static char again[96 * 1024];
        CHECK(ProfileCodec::write(parsed, again, sizeof again) == len && std::strcmp(again, buffer) == 0, "écriture stable");

        // Anciens fichiers et fichiers manuscrits.
        CHECK(parse(doc()) == ProfileStatus::Ok && timing_default(parsed.loops[0].tracks[0]), "ancien fichier : valeurs par défaut");
        CHECK(parse(doc("", "", "\"step_offset\": \" -5, 6 ,+7\", \"extra_hits\": \"  3:-4:50:2   3:4:60:-2  \", \"quantize\": 100,")) == ProfileStatus::Ok &&
                  parsed.loops[0].tracks[0].step_offset[0] == -5 && parsed.loops[0].tracks[0].step_offset[2] == 7 && parsed.loops[0].tracks[0].step_offset[3] == 0 &&
                  parsed.loops[0].tracks[0].extra_count[3] == 2 && parsed.loops[0].tracks[0].extra[3][1].velocity == 60 && parsed.loops[0].tracks[0].quantize == 100,
              "fichier manuscrit : espaces, signe +, listes courtes, force 100");
        CHECK(parse(doc("", "", "\"step_offset\": \"\", \"extra_hits\": \"\", \"quantize\": 0,")) == ProfileStatus::Ok &&
                  timing_default(parsed.loops[0].tracks[0]),
              "listes vides et force 0 : valeurs par défaut");

        // Valeurs invalides : refusées.
        const char* bad_offset[] = {"-65", "64", "1,2,,3", "a", "1.5", "1 2", "99999999999", "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"};
        bool all_refused = true;
        for (const char* v : bad_offset)
            all_refused = all_refused && parse(doc("", "", std::string("\"step_offset\": \"") + v + "\",")) == ProfileStatus::BadField;
        CHECK(all_refused, "décalages invalides : BadField");

        const char* bad_extra[] = {
            "4:-16:100",          "4:-16:100:0:",     "4:0:100:0:51",    "4:0:100:0:-51",  "4:0:100:0:1:2",  "4:0:100:0:x",  "4:0:100:0:1x",    "a:b:c:d",         "16:0:100:0",      "-1:0:100:0",     "4:-65:100:0",
            "4:64:100:0",         "4:0:0:0",          "4:0:128:0",       "4:0:100:13",      "4:0:100:-13",    "4:0:100:0x",
            "4::100:0",           "4:0:100:0;5:0:100:0", "4:0:100:0,5:0:100:0",
            "4:0:100:0 4:1:100:0 4:2:100:0 4:3:100:0",  // 4 notes dans le même pas : trop
        };
        all_refused = true;
        for (const char* v : bad_extra)
            all_refused = all_refused && parse(doc("", "", std::string("\"extra_hits\": \"") + v + "\",")) == ProfileStatus::BadField;
        CHECK(all_refused, "notes supplémentaires invalides : BadField");
        CHECK(parse(doc("", "", "\"extra_hits\": \"4:0:100:0 4:1:100:0 4:2:100:0\",")) == ProfileStatus::Ok && parsed.loops[0].tracks[0].extra_count[4] == 3,
              "3 notes supplémentaires dans un pas : accepté");

        for (const char* q : {"101", "-1", "1.5", "\"30\"", "null", "true"})
            CHECK(parse(doc("", "", std::string("\"quantize\": ") + q + ",")) == ProfileStatus::BadField, "quantize invalide : BadField");
        CHECK(parse(doc("", "", "\"step_offset\": [1,2],")) == ProfileStatus::BadField && parse(doc("", "", "\"extra_hits\": 5,")) == ProfileStatus::BadField &&
                  parse(doc("", "", "\"extra_hits\": null,")) == ProfileStatus::BadField,
              "type JSON incorrect : BadField");

        // Une chaîne de notes trop longue pour le tampon de lecture est refusée proprement.
        std::string too_long;
        for (int i = 0; i < 100; ++i) too_long += "1:0:100:0 ";
        CHECK(parse(doc("", "", "\"extra_hits\": \"" + too_long + "\",")) == ProfileStatus::BadField, "chaîne de notes beaucoup trop longue : BadField");

        // Écriture de valeurs hors limites : refusée.
        source.loops[0].tracks[1].step_offset[0] = -65;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : décalage -65 refusé");
        source.loops[0].tracks[1].step_offset[0] = -64;
        source.loops[0].tracks[1].quantize = 101;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : force 101 refusée");
        source.loops[0].tracks[1].quantize = 30;
        source.loops[0].tracks[2].extra_count[4] = 4;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : 4 notes supplémentaires refusées");
        source.loops[0].tracks[2].extra_count[4] = 3;
        source.loops[0].tracks[2].extra[4][1].velocity = 0;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : vélocité 0 d'une note supplémentaire refusée");
        source.loops[0].tracks[2].extra[4][1].velocity = 80;
        source.loops[0].tracks[2].extra[4][1].pitch    = 13;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : pitch 13 d'une note supplémentaire refusé");
        source.loops[0].tracks[2].extra[4][1].pitch    = 5;
        source.loops[0].tracks[2].extra[4][1].offset   = 64;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : décalage 64 d'une note supplémentaire refusé");
        source.loops[0].tracks[2].extra[4][1].offset   = -65;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : décalage -65 d'une note supplémentaire refusé");
        source.loops[0].tracks[2].extra[4][1].offset   = 16;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) > 0, "(valeurs remises dans les bornes : l'écriture repasse)");
    }

    // ----- Pan : « step_pan » et le cinquième champ des notes supplémentaires -----
    {
        auto count = [](const char* text, const char* what) {
            int n = 0;
            for (const char* p = text; (p = std::strstr(p, what)) != nullptr; ++p) ++n;
            return n;
        };
        source = ProfileData{};
        source.add_sample("a.wav");
        source.loop_count           = 1;
        source.loops[0].track_count = 3;
        TrackData& t1 = source.loops[0].tracks[1];
        t1.step_pan[0]  = -50;
        t1.step_pan[3]  = 25;
        t1.step_pan[15] = 50;
        TrackData& t2 = source.loops[0].tracks[2];
        t2.extra_count[4] = 2;
        t2.extra[4][0]    = {-16, 100, 0, -30};
        t2.extra[4][1]    = {16, 80, 5, 50};

        const int len = ProfileCodec::write(source, buffer, sizeof buffer);
        CHECK(len > 0, "écriture avec pan");
        CHECK(count(buffer, "\"step_pan\"") == 1, "« step_pan » seulement pour la piste qui s'écarte du centre");
        CHECK(std::strstr(buffer, "\"step_pan\": \"-50,0,0,25,0,0,0,0,0,0,0,0,0,0,0,50\"") != nullptr, "liste des pans lisible, 16 valeurs");
        CHECK(std::strstr(buffer, "\"extra_hits\": \"4:-16:100:0:-30 4:16:80:5:50\"") != nullptr, "pan des notes supplémentaires : cinquième champ");
        CHECK(codec.parse(buffer, static_cast<std::size_t>(len), parsed) == ProfileStatus::Ok, "relecture");
        const TrackData& r0 = parsed.loops[0].tracks[0];
        const TrackData& r1 = parsed.loops[0].tracks[1];
        const TrackData& r2 = parsed.loops[0].tracks[2];
        bool centered = true;
        for (int s = 0; s < cfg::MAX_STEPS; ++s) centered = centered && r0.step_pan[s] == 0 && r2.step_pan[s] == 0;
        CHECK(centered, "pistes sans pan : centre");
        CHECK(r1.step_pan[0] == -50 && r1.step_pan[3] == 25 && r1.step_pan[15] == 50 && r1.step_pan[1] == 0, "pans des pas restitués");
        CHECK(r2.extra_count[4] == 2 && r2.extra[4][0].pan == -30 && r2.extra[4][1].pan == 50 && r2.extra[4][0].offset == -16 && r2.extra[4][1].pitch == 5,
              "pans des notes supplémentaires restitués, autres champs intacts");
        static char again[96 * 1024];
        CHECK(ProfileCodec::write(parsed, again, sizeof again) == len && std::strcmp(again, buffer) == 0, "écriture stable");

        // Anciens fichiers : notes supplémentaires à quatre champs, pan 0 ; pas de « step_pan ».
        CHECK(parse(doc("", "", "\"extra_hits\": \"3:-4:50:2 3:4:60:-2\",")) == ProfileStatus::Ok && parsed.loops[0].tracks[0].extra_count[3] == 2 &&
                  parsed.loops[0].tracks[0].extra[3][0].pan == 0 && parsed.loops[0].tracks[0].extra[3][1].pan == 0 && parsed.loops[0].tracks[0].extra[3][1].velocity == 60,
              "ancien fichier (quatre champs) : pan 0");
        CHECK(parse(doc("", "", "\"extra_hits\": \"3:-4:50:2 3:4:60:-2:+7\",")) == ProfileStatus::Ok && parsed.loops[0].tracks[0].extra[3][0].pan == 0 &&
                  parsed.loops[0].tracks[0].extra[3][1].pan == 7,
              "quatre et cinq champs mélangés, signe +");
        CHECK(parse(doc()) == ProfileStatus::Ok && parsed.loops[0].tracks[0].step_pan[5] == 0, "ancien fichier sans step_pan : centre");
        CHECK(parse(doc("", "", "\"step_pan\": \" -5, 6 ,+7\",")) == ProfileStatus::Ok && parsed.loops[0].tracks[0].step_pan[0] == -5 &&
                  parsed.loops[0].tracks[0].step_pan[2] == 7 && parsed.loops[0].tracks[0].step_pan[3] == 0,
              "step_pan manuscrit : espaces, signe +, liste courte");
        CHECK(parse(doc("", "", "\"step_pan\": \"\",")) == ProfileStatus::Ok && parsed.loops[0].tracks[0].step_pan[0] == 0, "step_pan vide : centre");

        // Valeurs invalides.
        const char* bad_pan[] = {"-51", "51", "1,2,,3", "a", "1.5", "1 2", "99999999999", "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"};
        bool refused = true;
        for (const char* v : bad_pan) refused = refused && parse(doc("", "", std::string("\"step_pan\": \"") + v + "\",")) == ProfileStatus::BadField;
        CHECK(refused, "step_pan invalide : BadField");
        CHECK(parse(doc("", "", "\"step_pan\": [1,2],")) == ProfileStatus::BadField && parse(doc("", "", "\"step_pan\": 5,")) == ProfileStatus::BadField &&
                  parse(doc("", "", "\"step_pan\": null,")) == ProfileStatus::BadField,
              "step_pan : mauvais type JSON refusé");
        CHECK(parse(doc("", "", "\"step_pan\": \"-50,50\",")) == ProfileStatus::Ok, "bornes -50 et +50 acceptées");

        // Écriture de valeurs hors bornes refusée.
        source.loops[0].tracks[1].step_pan[2] = 51;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : pan 51 refusé");
        source.loops[0].tracks[1].step_pan[2] = -51;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : pan -51 refusé");
        source.loops[0].tracks[1].step_pan[2] = 0;
        source.loops[0].tracks[2].extra[4][1].pan = 51;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : pan 51 d'une note supplémentaire refusé");
        source.loops[0].tracks[2].extra[4][1].pan = -51;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : pan -51 d'une note supplémentaire refusé");
        source.loops[0].tracks[2].extra[4][1].pan = 0;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) > 0, "(valeurs remises dans les bornes : l'écriture repasse)");
    }

    // ----- Largeur et balance de la piste : clés « width » et « balance » -----
    {
        auto count = [](const char* text, const char* what) {
            int n = 0;
            for (const char* p = text; (p = std::strstr(p, what)) != nullptr; ++p) ++n;
            return n;
        };
        source = ProfileData{};
        source.add_sample("a.wav");
        source.loop_count           = 1;
        source.loops[0].track_count = 4;
        source.loops[0].tracks[1].width   = -100;
        source.loops[0].tracks[2].balance = 50;
        source.loops[0].tracks[3].width   = 7;
        source.loops[0].tracks[3].balance = -33;

        const int len = ProfileCodec::write(source, buffer, sizeof buffer);
        CHECK(len > 0, "écriture avec largeur et balance de piste");
        CHECK(count(buffer, "\"width\":") == 2 && count(buffer, "\"balance\":") == 2, "clés écrites seulement pour les pistes qui s'écartent du centre");
        CHECK(std::strstr(buffer, "\"width\": -100") != nullptr && std::strstr(buffer, "\"balance\": 50") != nullptr, "valeurs en nombres");
        CHECK(codec.parse(buffer, static_cast<std::size_t>(len), parsed) == ProfileStatus::Ok, "relecture");
        const LoopData& lp = parsed.loops[0];
        CHECK(lp.tracks[0].width == 0 && lp.tracks[0].balance == 0 && lp.tracks[1].width == -100 && lp.tracks[1].balance == 0 && lp.tracks[2].width == 0 &&
                  lp.tracks[2].balance == 50 && lp.tracks[3].width == 7 && lp.tracks[3].balance == -33,
              "largeur et balance de chaque piste restituées, sans se mélanger");
        static char again[96 * 1024];
        CHECK(ProfileCodec::write(parsed, again, sizeof again) == len && std::strcmp(again, buffer) == 0, "écriture stable");

        // Anciens fichiers : sans les clés, 0.
        CHECK(parse(doc()) == ProfileStatus::Ok && parsed.loops[0].tracks[0].width == 0 && parsed.loops[0].tracks[0].balance == 0, "ancien fichier : inchangé, centre");
        CHECK(parse(doc("", "", "\"width\": 0, \"balance\": 0,")) == ProfileStatus::Ok, "zéro explicite accepté");
        CHECK(parse(doc("", "", "\"width\": -100, \"balance\": 50,")) == ProfileStatus::Ok && parsed.loops[0].tracks[0].width == -100 &&
                  parsed.loops[0].tracks[0].balance == 50,
              "bornes acceptées");

        // Valeurs invalides (la largeur va de -100 à 100, la balance de -50 à 50).
        const char* bad_common[] = {"1.5", "\"5\"", "null", "true", "[1]", "{}", "1e3"};
        for (const char* v : bad_common) {
            CHECK(parse(doc("", "", std::string("\"width\": ") + v + ",")) == ProfileStatus::BadField, "largeur invalide : BadField");
            CHECK(parse(doc("", "", std::string("\"balance\": ") + v + ",")) == ProfileStatus::BadField, "balance invalide : BadField");
        }
        CHECK(parse(doc("", "", "\"width\": 101,")) == ProfileStatus::BadField && parse(doc("", "", "\"width\": -101,")) == ProfileStatus::BadField,
              "largeur hors de -100..100 : BadField");
        CHECK(parse(doc("", "", "\"width\": 100,")) == ProfileStatus::Ok && parsed.loops[0].tracks[0].width == 100, "largeur +100 acceptée");
        CHECK(parse(doc("", "", "\"balance\": 51,")) == ProfileStatus::BadField && parse(doc("", "", "\"balance\": -51,")) == ProfileStatus::BadField,
              "balance hors de -50..50 : BadField");
        // Écriture hors bornes refusée.
        source.loops[0].tracks[1].width = 101;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : largeur de piste 101 refusée");
        source.loops[0].tracks[1].width = -101;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : largeur de piste -101 refusée");
        source.loops[0].tracks[1].width   = -100;
        source.loops[0].tracks[2].balance = -51;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : balance de piste -51 refusée");
        source.loops[0].tracks[2].balance = 50;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) > 0, "(valeurs remises dans les bornes : l'écriture repasse)");
    }

    // ----- Enveloppe ADSR de la piste : « attack », « decay », « sustain », « release » -----
    {
        auto count = [](const char* text, const char* what) {
            int n = 0;
            for (const char* p = text; (p = std::strstr(p, what)) != nullptr; ++p) ++n;
            return n;
        };
        source = ProfileData{};
        source.add_sample("a.wav");
        source.loop_count           = 1;
        source.loops[0].track_count = 4;
        CHECK(source.loops[0].tracks[0].attack == 0 && source.loops[0].tracks[0].decay == 0 && source.loops[0].tracks[0].sustain == 100 &&
                  source.loops[0].tracks[0].release == cfg::ENV_DEFAULT_RELEASE_MS,
              "valeurs par défaut : A 0, D 0, S 100, R 5");
        source.loops[0].tracks[1].attack  = 12;
        source.loops[0].tracks[1].decay   = 300;
        source.loops[0].tracks[2].sustain = 70;
        source.loops[0].tracks[3].release = 2000;
        source.loops[0].tracks[3].attack  = 2000;
        source.loops[0].tracks[3].sustain = 0;

        const int len = ProfileCodec::write(source, buffer, sizeof buffer);
        CHECK(len > 0, "écriture avec enveloppes");
        CHECK(count(buffer, "\"attack\":") == 2 && count(buffer, "\"decay\":") == 1 && count(buffer, "\"sustain\":") == 2 && count(buffer, "\"release\":") == 1,
              "clés écrites seulement quand elles s'écartent de leur défaut");
        CHECK(std::strstr(buffer, "\"attack\": 12") != nullptr && std::strstr(buffer, "\"sustain\": 0") != nullptr && std::strstr(buffer, "\"release\": 2000") != nullptr,
              "valeurs en nombres");
        CHECK(codec.parse(buffer, static_cast<std::size_t>(len), parsed) == ProfileStatus::Ok, "relecture");
        const LoopData& lp = parsed.loops[0];
        CHECK(lp.tracks[0].attack == 0 && lp.tracks[0].decay == 0 && lp.tracks[0].sustain == 100 && lp.tracks[0].release == cfg::ENV_DEFAULT_RELEASE_MS,
              "piste sans enveloppe : défauts (dont Release 5)");
        CHECK(lp.tracks[1].attack == 12 && lp.tracks[1].decay == 300 && lp.tracks[1].sustain == 100 && lp.tracks[1].release == cfg::ENV_DEFAULT_RELEASE_MS,
              "attack et decay restitués, le reste par défaut");
        CHECK(lp.tracks[2].sustain == 70 && lp.tracks[2].attack == 0, "sustain restitué seul");
        CHECK(lp.tracks[3].release == 2000 && lp.tracks[3].attack == 2000 && lp.tracks[3].sustain == 0 && lp.tracks[3].decay == 0, "bornes 2000 et 0 restituées, sans se mélanger");
        static char again[96 * 1024];
        CHECK(ProfileCodec::write(parsed, again, sizeof again) == len && std::strcmp(again, buffer) == 0, "écriture stable");

        // Une valeur égale au défaut n'est pas écrite ; un Release à 0 (coupure nette) l'est.
        source.loops[0].tracks[1].attack = 0;
        source.loops[0].tracks[1].decay  = 0;
        source.loops[0].tracks[2].release = 0;
        const int len2 = ProfileCodec::write(source, buffer, sizeof buffer);
        CHECK(len2 > 0 && std::strstr(buffer, "\"release\": 0") != nullptr && count(buffer, "\"decay\":") == 0, "Release 0 écrit (différent du défaut 5), valeurs par défaut omises");
        CHECK(codec.parse(buffer, static_cast<std::size_t>(len2), parsed) == ProfileStatus::Ok && parsed.loops[0].tracks[2].release == 0, "Release 0 relu");

        // Anciens fichiers : défauts. Zéros et valeurs explicites acceptés.
        CHECK(parse(doc()) == ProfileStatus::Ok && parsed.loops[0].tracks[0].release == cfg::ENV_DEFAULT_RELEASE_MS && parsed.loops[0].tracks[0].sustain == 100 &&
                  parsed.loops[0].tracks[0].attack == 0,
              "ancien fichier : enveloppe par défaut");
        CHECK(parse(doc("", "", "\"attack\": 0, \"decay\": 0, \"sustain\": 100, \"release\": 5,")) == ProfileStatus::Ok && parsed.loops[0].tracks[0].release == 5,
              "valeurs par défaut explicites");
        CHECK(parse(doc("", "", "\"attack\": 2000, \"decay\": 2000, \"sustain\": 0, \"release\": 2000,")) == ProfileStatus::Ok &&
                  parsed.loops[0].tracks[0].decay == 2000 && parsed.loops[0].tracks[0].sustain == 0,
              "bornes acceptées");

        // Valeurs invalides.
        for (const char* key : {"attack", "decay", "sustain", "release"})
            for (const char* v : {"-1", "1.5", "\"5\"", "null", "true", "[1]", "{}", "1e9"})
                CHECK(parse(doc("", "", std::string("\"") + key + "\": " + v + ",")) == ProfileStatus::BadField, "enveloppe invalide : BadField");
        for (const char* key : {"attack", "decay", "release"})
            CHECK(parse(doc("", "", std::string("\"") + key + "\": 2001,")) == ProfileStatus::BadField, "temps au-dessus de 2000 ms : BadField");
        CHECK(parse(doc("", "", "\"sustain\": 101,")) == ProfileStatus::BadField, "sustain au-dessus de 100 : BadField");

        // Écriture hors bornes refusée.
        source.loops[0].tracks[1].attack = 2001;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : attack 2001 refusé");
        source.loops[0].tracks[1].attack  = 0;
        source.loops[0].tracks[1].decay   = 2001;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : decay 2001 refusé");
        source.loops[0].tracks[1].decay   = 0;
        source.loops[0].tracks[2].release = 2001;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : release 2001 refusé");
        source.loops[0].tracks[2].release = 5;
        source.loops[0].tracks[2].sustain = 101;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : sustain 101 refusé");
        source.loops[0].tracks[2].sustain = 70;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) > 0, "(valeurs remises dans les bornes : l'écriture repasse)");
    }

    // ----- Profil plein, tout rempli : 16 clés par piste, 3 notes supplémentaires dans chaque pas, pans extrêmes -----
    {
        static char big[300 * 1024];
        source = ProfileData{};
        source.add_sample("Kick1.wav");
        source.loop_count = MAX_LOOPS;
        for (int l = 0; l < MAX_LOOPS; ++l) {
            source.loops[l].track_count = cfg::MAX_TRACKS;
            for (int t = 0; t < cfg::MAX_TRACKS; ++t) {
                TrackData& td = source.loops[l].tracks[t];
                td.gain       = 0.75f;
                td.pattern    = static_cast<uint16_t>(l * 100 + t);
                td.quantize   = static_cast<uint8_t>((l + t) % 101);
                td.width      = static_cast<int8_t>(t % 2 ? -100 : 100);
                td.balance    = static_cast<int8_t>(l % 2 ? -50 : 50);
                td.attack     = static_cast<uint16_t>(1000 + (l + t) % 1001);  // quatre chiffres : le plus long à écrire
                td.decay      = static_cast<uint16_t>(1000 + (l * 3 + t) % 1001);
                td.sustain    = static_cast<uint8_t>(l % 2 ? 99 : 0);
                td.release    = static_cast<uint16_t>(2000 - (l + t) % 1000);
                for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                    td.step_velocity[s] = static_cast<uint8_t>(1 + (l + t + s * 7) % 127);
                    td.step_pitch[s]    = static_cast<int8_t>((l + t * 3 + s) % 25 - 12);
                    td.step_offset[s]   = static_cast<int8_t>((l * 5 + t * 3 + s * 11) % 128 - 64);
                    td.step_pan[s]      = static_cast<int8_t>(s % 2 ? -50 : 50);  // le plus long à écrire
                    td.extra_count[s]   = cfg::MAX_EXTRA_HITS;
                    for (int h = 0; h < cfg::MAX_EXTRA_HITS; ++h)
                        td.extra[s][h] = {static_cast<int8_t>((l + t + s + h * 17) % 128 - 64), static_cast<uint8_t>(1 + (l + s + h * 31) % 127),
                                          static_cast<int8_t>((t + s + h) % 25 - 12), static_cast<int8_t>(h % 2 ? 50 : -50)};
                }
            }
        }
        const int len = ProfileCodec::write(source, big, sizeof big);
        CHECK(len > 0, "profil plein, tout rempli : écriture");
        const ProfileStatus st = codec.parse(big, static_cast<std::size_t>(len), parsed);
        CHECK(st == ProfileStatus::Ok, "profil plein, tout rempli : relecture (jetons suffisants)");
        bool same = st == ProfileStatus::Ok && parsed.loop_count == MAX_LOOPS;
        for (int l = 0; same && l < MAX_LOOPS; ++l)
            for (int t = 0; same && t < cfg::MAX_TRACKS; ++t) {
                const TrackData& a = parsed.loops[l].tracks[t];
                const TrackData& b = source.loops[l].tracks[t];
                same = same && a.quantize == b.quantize && a.width == b.width && a.balance == b.balance && a.attack == b.attack && a.decay == b.decay &&
                       a.sustain == b.sustain && a.release == b.release;
                for (int s = 0; same && s < cfg::MAX_STEPS; ++s) {
                    same = same && a.step_offset[s] == b.step_offset[s] && a.step_velocity[s] == b.step_velocity[s] && a.step_pitch[s] == b.step_pitch[s] &&
                           a.step_pan[s] == b.step_pan[s] && a.extra_count[s] == b.extra_count[s];
                    for (int h = 0; same && h < a.extra_count[s]; ++h)
                        same = same && a.extra[s][h].offset == b.extra[s][h].offset && a.extra[s][h].velocity == b.extra[s][h].velocity &&
                               a.extra[s][h].pitch == b.extra[s][h].pitch && a.extra[s][h].pan == b.extra[s][h].pan;
                }
            }
        CHECK(same, "toutes les valeurs restituées");
        std::printf("profil plein, tout rempli : %d octets de JSON\n", len);
    }

    // ----- Imbrication profonde : refusée proprement, sans plantage -----
    {
        std::string deep;
        for (int i = 0; i < 200; ++i) deep += "[";
        for (int i = 0; i < 200; ++i) deep += "]";
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"deep\":" + deep + "}") == ProfileStatus::InvalidJson, "imbrication de 200 niveaux");
        CHECK(parse("{\"format\":\"dmp\",\"version\":1,\"a\":[[[[[1]]]]]}") == ProfileStatus::Ok, "imbrication raisonnable ignorée");
    }

    // ----- Robustesse : des fichiers abîmés ne font jamais planter le lecteur -----
    {
        source = ProfileData{};
        source.add_sample("Kick1.wav");
        source.add_sample("Snare1.wav");
        source.loop_count = 2;
        for (int l = 0; l < 2; ++l) {
            source.loops[l].track_count = 2;
            source.loops[l].tracks[0]   = {0, 1.0f, 1.0f, 0x1111};
            source.loops[l].tracks[1]   = {1, 0.8f, 1.5f, 0x0410};
            source.loops[l].tracks[0].step_velocity[3] = 70;
            source.loops[l].tracks[1].step_pitch[5]    = -7;
            source.loops[l].tracks[0].step_offset[6]   = -20;
            source.loops[l].tracks[0].quantize         = 40;
            source.loops[l].tracks[0].step_pan[7]      = -35;
            source.loops[l].tracks[0].width            = -20;
            source.loops[l].tracks[1].balance          = 15;
            source.loops[l].tracks[0].attack           = 7;
            source.loops[l].tracks[1].sustain          = 60;
            source.loops[l].tracks[1].release          = 120;
            source.loops[l].tracks[1].extra_count[2]   = 2;
            source.loops[l].tracks[1].extra[2][0]      = {-10, 90, 3, 20};
            source.loops[l].tracks[1].extra[2][1]      = {12, 70, -3, -45};
        }
        const int len = ProfileCodec::write(source, buffer, sizeof buffer);

        // Générateur pseudo-aléatoire déterministe : le test est reproductible.
        uint32_t rng = 12345;
        auto next    = [&rng]() { rng = rng * 1664525u + 1013904223u; return rng >> 8; };

        static char mutated[96 * 1024];
        int         accepted = 0;
        const int   rounds   = 4000;
        for (int r = 0; r < rounds; ++r) {
            std::memcpy(mutated, buffer, static_cast<std::size_t>(len));
            int mlen = len;

            const int edits = 1 + static_cast<int>(next() % 4);
            for (int e = 0; e < edits; ++e) {
                const int pos = static_cast<int>(next() % static_cast<uint32_t>(mlen));
                switch (next() % 4) {
                    case 0: mutated[pos] = static_cast<char>(next());              break;  // octet quelconque
                    case 1: mutated[pos] = "{}[],:\"x0.-"[next() % 11];            break;  // caractère de syntaxe
                    case 2: mlen = pos > 0 ? pos : 1;                              break;  // coupé
                    case 3: {                                                              // morceau supprimé
                        const int n = 1 + static_cast<int>(next() % 12);
                        if (pos + n < mlen) {
                            std::memmove(mutated + pos, mutated + pos + n, static_cast<std::size_t>(mlen - pos - n));
                            mlen -= n;
                        }
                        break;
                    }
                }
            }
            if (codec.parse(mutated, static_cast<std::size_t>(mlen), parsed) == ProfileStatus::Ok) {
                ++accepted;
                // Un profil accepté est toujours cohérent : jamais d'index de sample hors table.
                bool consistent = parsed.sample_count <= MAX_SAMPLES && parsed.loop_count <= MAX_LOOPS;
                for (int l = 0; l < parsed.loop_count; ++l)
                    for (int t = 0; t < parsed.loops[l].track_count; ++t) {
                        consistent = consistent && parsed.loops[l].tracks[t].sample < parsed.sample_count;
                        consistent = consistent && parsed.loops[l].tracks[t].quantize <= cfg::QUANTIZE_MAX &&
                                     parsed.loops[l].tracks[t].width >= cfg::WIDTH_MIN && parsed.loops[l].tracks[t].width <= cfg::WIDTH_MAX &&
                                     parsed.loops[l].tracks[t].balance >= cfg::PAN_MIN && parsed.loops[l].tracks[t].balance <= cfg::PAN_MAX &&
                                     parsed.loops[l].tracks[t].attack <= cfg::ENV_MS_MAX && parsed.loops[l].tracks[t].decay <= cfg::ENV_MS_MAX &&
                                     parsed.loops[l].tracks[t].release <= cfg::ENV_MS_MAX && parsed.loops[l].tracks[t].sustain <= 100;
                        for (int s = 0; s < cfg::MAX_STEPS; ++s) {
                            const TrackData& td = parsed.loops[l].tracks[t];
                            consistent = consistent && td.step_offset[s] >= cfg::OFFSET_MIN && td.step_offset[s] <= cfg::OFFSET_MAX &&
                                         td.extra_count[s] <= cfg::MAX_EXTRA_HITS;
                            for (int h = 0; h < td.extra_count[s] && h < cfg::MAX_EXTRA_HITS; ++h)
                                consistent = consistent && td.extra[s][h].offset >= cfg::OFFSET_MIN && td.extra[s][h].offset <= cfg::OFFSET_MAX &&
                                             td.extra[s][h].velocity >= cfg::VELOCITY_MIN && td.extra[s][h].velocity <= cfg::VELOCITY_MAX &&
                                             td.extra[s][h].pitch >= cfg::STEP_PITCH_MIN && td.extra[s][h].pitch <= cfg::STEP_PITCH_MAX &&
                                             td.extra[s][h].pan >= cfg::PAN_MIN && td.extra[s][h].pan <= cfg::PAN_MAX;
                        }
                        for (int s = 0; s < cfg::MAX_STEPS; ++s)
                            consistent = consistent && parsed.loops[l].tracks[t].step_velocity[s] >= cfg::VELOCITY_MIN &&
                                         parsed.loops[l].tracks[t].step_velocity[s] <= cfg::VELOCITY_MAX &&
                                         parsed.loops[l].tracks[t].step_pitch[s] >= cfg::STEP_PITCH_MIN &&
                                         parsed.loops[l].tracks[t].step_pitch[s] <= cfg::STEP_PITCH_MAX &&
                                         parsed.loops[l].tracks[t].step_pan[s] >= cfg::PAN_MIN && parsed.loops[l].tracks[t].step_pan[s] <= cfg::PAN_MAX;
                    }
                CHECK(consistent, "profil accepté après altération : données cohérentes");
            }
        }
        std::printf("robustesse : %d altérations, %d encore acceptées (contenu modifié mais valide)\n", rounds, accepted);
        CHECK(accepted < rounds, "certaines altérations sont refusées");
    }

    // ----- Écriture de données invalides -----
    {
        source = ProfileData{};
        source.add_sample("a.wav");
        source.loop_count           = 1;
        source.loops[0].track_count = 1;
        source.loops[0].tracks[0].sample = 3;  // hors de la table
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : index de sample hors table refusé");

        source.loops[0].tracks[0].sample = 0;
        source.loops[0].tracks[0].gain   = NAN;
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : NaN refusé");

        source.loops[0].tracks[0].gain = 1.0f;
        std::strcpy(source.samples[0], "mauvais\nnom.wav");
        CHECK(ProfileCodec::write(source, buffer, sizeof buffer) == -1, "écriture : caractère de contrôle dans un nom refusé");
    }

    // ----- Table de samples -----
    {
        ProfileData p;
        CHECK(p.add_sample("a.wav") == 0 && p.add_sample("b.wav") == 1 && p.add_sample("a.wav") == 0 && p.sample_count == 2,
              "add_sample : sans doublon");
        CHECK(p.find_sample("b.wav") == 1 && p.find_sample("c.wav") == -1, "find_sample");
        CHECK(p.add_sample(std::string(60, 'x').c_str()) == -1, "add_sample : nom trop long");
        for (int i = 2; i < MAX_SAMPLES; ++i) p.add_sample(("s" + std::to_string(i)).c_str());
        CHECK(p.add_sample("de_trop.wav") == -1, "add_sample : table pleine");
    }

    // ----- compact_samples / remove_loop -----
    {
        source = ProfileData{};
        source.add_sample("a.wav");  // 0 : utilisé seulement par la loop 0
        source.add_sample("b.wav");  // 1 : plus utilisé par personne
        source.add_sample("c.wav");  // 2 : utilisé par les deux loops
        source.add_sample("d.wav");  // 3 : utilisé seulement par la loop 1
        source.loop_count = 2;
        source.loops[0].track_count = 2;
        source.loops[0].tracks[0].sample = 0;
        source.loops[0].tracks[1].sample = 2;
        source.loops[1].track_count = 2;
        source.loops[1].tracks[0].sample = 2;
        source.loops[1].tracks[1].sample = 3;

        source.compact_samples();
        CHECK(source.sample_count == 3 && std::strcmp(source.samples[0], "a.wav") == 0 &&
                  std::strcmp(source.samples[1], "c.wav") == 0 && std::strcmp(source.samples[2], "d.wav") == 0,
              "compact : le sample inutilisé disparaît, l'ordre est conservé");
        CHECK(source.loops[0].tracks[0].sample == 0 && source.loops[0].tracks[1].sample == 1 &&
                  source.loops[1].tracks[0].sample == 1 && source.loops[1].tracks[1].sample == 2,
              "compact : les pistes sont renumérotées");

        source.remove_loop(0);
        CHECK(source.loop_count == 1 && source.loops[0].tracks[1].sample == 2, "remove_loop : les loops suivantes remontent");
        source.compact_samples();
        CHECK(source.sample_count == 2 && std::strcmp(source.samples[0], "c.wav") == 0 && std::strcmp(source.samples[1], "d.wav") == 0 &&
                  source.loops[0].tracks[0].sample == 0 && source.loops[0].tracks[1].sample == 1,
              "compact après suppression d'une loop");
        source.remove_loop(5);
        source.remove_loop(-1);
        CHECK(source.loop_count == 1, "remove_loop : index invalide ignoré");
        source.remove_loop(0);
        source.compact_samples();
        CHECK(source.loop_count == 0 && source.sample_count == 0, "plus aucune loop : table de samples vide");
    }

    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
