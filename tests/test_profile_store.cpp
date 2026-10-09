#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../platform/desktop/profile_store_file.h"

using namespace profile;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond, msg)                            \
    do {                                            \
        if (!(cond)) {                              \
            std::printf("FAIL: %s\n", msg);         \
            ++failures;                             \
        }                                           \
    } while (0)

static std::string read_all(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void write_all(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

static ProfileData make_profile(float bpm) {
    ProfileData p;
    p.add_sample("Kick1.wav");
    p.loop_count                = 1;
    p.loops[0].bpm              = bpm;
    p.loops[0].track_count      = 1;
    p.loops[0].tracks[0].pattern = 0x1111;
    return p;
}

static ProfileData work;

int main() {
    const std::string dir  = "test_profile_store_dir";
    const std::string path = dir + "/sub/default.json";
    fs::remove_all(dir);

    FileProfileStore store(path);

    // Pas encore de fichier : profil neuf, sans erreur.
    CHECK(store.load(work) == LoadResult::Missing && work.loop_count == 0 && work.sample_count == 0 &&
              std::strcmp(work.name, "Default") == 0,
          "fichier absent : profil neuf");

    // Sauvegarde : le dossier est créé, le fichier est un JSON lisible, pas de .tmp qui traîne.
    CHECK(store.save(make_profile(120.0f)), "sauvegarde dans un dossier à créer");
    CHECK(fs::exists(path) && !fs::exists(path + ".tmp") && !fs::exists(path + ".bak"), "fichier créé, ni .tmp ni .bak la première fois");
    CHECK(read_all(path).find("\"format\": \"dmp\"") != std::string::npos, "contenu en JSON");
    CHECK(store.load(work) == LoadResult::Loaded && work.loop_count == 1 && work.loops[0].bpm == 120.0f &&
              work.loops[0].tracks[0].pattern == 0x1111,
          "relecture");

    // Deuxième sauvegarde : l'ancienne version est gardée dans .bak.
    CHECK(store.save(make_profile(140.0f)), "seconde sauvegarde");
    CHECK(fs::exists(path + ".bak") && !fs::exists(path + ".tmp"), ".bak créé, pas de .tmp");
    CHECK(read_all(path + ".bak").find("120.0") != std::string::npos && read_all(path).find("140.0") != std::string::npos,
          ".bak = ancienne version, fichier = nouvelle");

    // Échec d'écriture : si le .tmp ne peut pas être créé, l'ancien fichier reste intact.
    {
        const std::string before = read_all(path);
        fs::create_directory(path + ".tmp");  // un dossier à la place du fichier temporaire
        CHECK(!store.save(make_profile(99.0f)), "écriture impossible : refus");
        CHECK(std::strlen(store.last_error()) > 0, "message d'erreur renseigné");
        CHECK(read_all(path) == before, "l'ancien fichier est intact après l'échec");
        fs::remove_all(path + ".tmp");
        CHECK(store.save(make_profile(99.0f)) && std::strlen(store.last_error()) == 0, "la sauvegarde suivante fonctionne et efface l'erreur");
    }

    // Un .tmp laissé par une panne n'est jamais lu comme un profil.
    {
        write_all(path + ".tmp", "n'importe quoi, écriture interrompue");
        CHECK(store.load(work) == LoadResult::Loaded && work.loops[0].bpm == 99.0f, "un .tmp orphelin est ignoré au chargement");
        CHECK(store.save(make_profile(100.0f)), "et il est remplacé à la sauvegarde suivante");
        CHECK(!fs::exists(path + ".tmp"), "plus de .tmp après la sauvegarde");
    }

    // Fichier corrompu : refusé avec un message, et mis de côté (.bak) avant d'être remplacé.
    {
        write_all(path, "{\"format\":\"dmp\",\"version\":1,\"loops\":[{\"bpm\":12");  // coupé
        CHECK(store.load(work) == LoadResult::Failed && std::strlen(store.last_error()) > 0, "fichier coupé : échec avec message");

        write_all(path, "ceci n'est pas du json");
        CHECK(store.load(work) == LoadResult::Failed, "texte quelconque : échec");
        write_all(path, "{\"format\":\"autre\",\"version\":1}");
        CHECK(store.load(work) == LoadResult::Failed, "autre format : échec");
        write_all(path, "{\"format\":\"dmp\",\"version\":9}");
        CHECK(store.load(work) == LoadResult::Failed, "version trop récente : échec");

        write_all(path, "contenu abîmé à conserver");
        CHECK(store.save(make_profile(111.0f)), "on peut sauvegarder par-dessus un fichier abîmé");
        CHECK(read_all(path + ".bak") == "contenu abîmé à conserver", "le fichier abîmé est conservé dans .bak");
        CHECK(store.load(work) == LoadResult::Loaded && work.loops[0].bpm == 111.0f, "le nouveau profil est lisible");
    }

    // Données invalides : refusées sans toucher au fichier.
    {
        const std::string before = read_all(path);
        ProfileData       bad    = make_profile(120.0f);
        bad.loops[0].tracks[0].sample = 9;  // hors de la table de samples
        CHECK(!store.save(bad) && read_all(path) == before, "profil incohérent : refusé, fichier intact");
    }

    // Chemin impossible (un fichier à la place du dossier).
    {
        write_all(dir + "/fichier", "x");
        FileProfileStore broken(dir + "/fichier/default.json");
        CHECK(!broken.save(make_profile(120.0f)), "dossier impossible à créer : refus");
        CHECK(std::strlen(broken.last_error()) > 0, "erreur renseignée");
    }

    fs::remove_all(dir);
    if (failures == 0) std::printf("OK\n");
    return failures ? 1 : 0;
}
