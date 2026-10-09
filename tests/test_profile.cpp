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

        // Profil plein : 32 loops de 16 pistes passent par le tableau de jetons.
        source = ProfileData{};
        source.add_sample("Kick1.wav");
        source.loop_count = MAX_LOOPS;
        for (int l = 0; l < MAX_LOOPS; ++l) {
            source.loops[l].track_count = cfg::MAX_TRACKS;
            for (int t = 0; t < cfg::MAX_TRACKS; ++t) source.loops[l].tracks[t] = {0, 0.75f, 1.25f, static_cast<uint16_t>(l * 100 + t)};
        }
        const int len = ProfileCodec::write(source, buffer, sizeof buffer);
        CHECK(len > 0 && codec.parse(buffer, static_cast<std::size_t>(len), parsed) == ProfileStatus::Ok &&
                  parsed.loop_count == MAX_LOOPS && parsed.loops[31].tracks[15].pattern == static_cast<uint16_t>(31 * 100 + 15),
              "profil plein (32 loops x 16 pistes) : aller-retour");
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
                        for (int s = 0; s < cfg::MAX_STEPS; ++s)
                            consistent = consistent && parsed.loops[l].tracks[t].step_velocity[s] >= cfg::VELOCITY_MIN &&
                                         parsed.loops[l].tracks[t].step_velocity[s] <= cfg::VELOCITY_MAX &&
                                         parsed.loops[l].tracks[t].step_pitch[s] >= cfg::STEP_PITCH_MIN &&
                                         parsed.loops[l].tracks[t].step_pitch[s] <= cfg::STEP_PITCH_MAX;
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
