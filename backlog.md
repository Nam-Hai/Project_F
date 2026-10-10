# Backlog : adaptation à la Teensy 4.1

Suivi des tâches. **Les décisions (pourquoi, comment, ce qu'on a écarté) sont dans `plan_teensy.md`** ; ce fichier dit seulement **quoi faire et où on en est**, avec des liens vers les décisions. Ne pas recopier les décisions ici.

## Mode d'emploi
- **Une tâche = une section `### B-nnn titre`**, d'une taille d'environ une session de travail, avec un critère de fin vérifiable.
- **Identifiants stables** (`B-nnn`), jamais renumérotés : déplacer une tâche d'une phase à l'autre ne change que ses étiquettes.
- **Champs** :
  - `statut` : `todo` · `doing` · `blocked` · `parked` (mis de côté volontairement) · `done`
  - `priorité` : 1 (haute), 2 (normale), 3 (basse)
  - `propriétaire` : `claude` ou `utilisateur` (commander, souder, écouter)
  - `étiquettes` : `phase-0` (sans matériel, sur le Mac), `phase-1` (mise en route sur la carte), `phase-2` (intégration), plus des étiquettes de thème (`profil`, `samples`, `ui`, `audio`, `sd`, `outillage`, `matériel`, `core`)
  - `dépend de` : identifiants qui doivent être `done` avant de commencer
  - `références` : sections de `plan_teensy.md` (T…) ou fichiers du dépôt
  - `terminé quand` : le critère de fin
- **Une tâche est « prête »** quand son statut est `todo` et que toutes ses dépendances sont `done`.
- **Mise à jour** : à chaque changement de statut, mettre à jour la section « Prêt maintenant » ci-dessous. Les tâches terminées restent ici jusqu'à ce que le fichier devienne lourd, puis partent dans `backlog_archive.md`.
- **Phases** (anciennement « P0, P1, P2 » de `plan_teensy.md`) : étiquettes `phase-0`, `phase-1`, `phase-2`. Les priorités sont des chiffres pour ne pas confondre les deux.

## Prêt maintenant
*(à jour au 11/10/2026)*
- **B-001** Commander le matériel (utilisateur, priorité 1)
- **B-060** Reprise de la carte SD (`.tmp` / `.bak` + CRC) (priorité 1)
- **B-022** Arène de samples (`SampleArena`) (priorité 1)
- **B-025** `SampleLibrary` à chemins relatifs (priorité 3)
- **B-030** Fonction « afficher ce Canvas » isolée (priorité 2)
- **B-040** Protocole d'événements de la télécommande (priorité 2)
- **B-070** Journal d'erreurs sur la carte (priorité 3)
- **B-080** Série de scénarios de charge (priorité 2)

---

## Matériel

### B-001 Commander le matériel
- statut: todo · priorité: 1 · propriétaire: utilisateur · étiquettes: phase-1, matériel
- dépend de: —
- références: achats.md (panier détaillé, liens et prix) ; plan_teensy.md#T17, #T9, #T4, #T19 ; fiche_technique_mat_rielle_composants.md
- terminé quand: tout est commandé sur Amazon (option B décidée le 10/10/2026 : Teensy 4.1 nue avec connecteurs + deux puces PSRAM 8 Mo soudées soi-même, module PCM5102A avec jack, écran Waveshare 2,42" SSD1309 SPI, microSD 32 Go, plaque d'essai et câbles Dupont, matériel de soudure CMS si besoin) puis reçu et contrôlé. Pas de boutons ni de potentiomètres pour l'instant. Reste à lire les prix du pack de puces PSRAM (Amazon.de) avant de payer.

### B-002 Souder les puces PSRAM sur la Teensy
- statut: todo · priorité: 1 · propriétaire: utilisateur · étiquettes: phase-1, matériel
- dépend de: B-001
- références: achats.md (section « La soudure ») ; plan_teensy.md#T18
- terminé quand: deux puces de 8 Mo sont soudées sur les petites pastilles au dos de la carte (sous le lecteur SD, entre les broches 31-32 et 33-34), et le test de mémoire de PJRC voit 16 Mo ; sinon, une puce de rechange est essayée.

## Phase 0 : sans matériel, sur le Mac

### B-010 Codec binaire du profil
- statut: done · priorité: 1 · propriétaire: claude · étiquettes: phase-0, profil, core
- dépend de: —
- références: plan_teensy.md#T7, #T14 ; core/profile/
- terminé quand: `BinaryCodec` dans `core/` (enregistrements balisés, valeurs par défaut absentes, étiquettes inconnues sautées, **octet de version** en en-tête, CRC32), interface de flux `ByteSource` / `ByteSink`, tests (aller-retour, fichier tronqué, CRC faux, fuzz, pire cas de taille).
- décisions (discussion du 10/10/2026) : flux plat d'enregistrements `[étiquette][longueur][contenu]` sans imbrication (une étiquette `LOOP` ouvre une loop, `TRACK` une piste, les champs suivants s'appliquent à la piste courante) ; marque `DMPB` + octet de version + petit-boutiste ; champs absents = défaut ; étiquette inconnue sautée ; CRC32 dans l'enregistrement de fin ; interfaces `ByteSource` / `ByteSink` ; nouvelle valeur `ProfileStatus::BadChecksum`.
- vérifié (10/10/2026) : `core/profile/binary_codec.{h,cpp}` (lecture, écriture, `valid`), `byte_stream.h` (`ByteSource`, `ByteSink`, `MemorySource`, `MemorySink`), `crc32.h` (CRC-32 à table de 16 entrées), `profile_status.{h,cpp}` (le statut d'erreur et `describe` sortent du codec JSON ; nouvelle valeur `BadChecksum`). Nouveau test `test_profile_binary` : valeurs de CRC connues et référence bit à bit, aller-retour (dont les extrémités des plages), valeurs par défaut non écrites (profil vide : 20 octets), pire cas 12 loops × 16 pistes pleines : **87 387 octets** (234 Ko en JSON), lecture par morceaux de 1 à 1000 octets, compatibilité JSON → binaire → JSON, erreurs d'en-tête, CRC, données après la fin, compatibilité future (étiquettes inconnues sautées, enregistrement plus long accepté, fin plus longue acceptée), ordre des enregistrements, limites (64 samples, 12 loops, 16 pistes, noms), chaque plage champ par champ (limite acceptée, limite + 1 refusée), refus à l'écriture, sink qui échoue, **toute coupure du fichier refusée et signalée comme tronquée**, **tout octet modifié refusé**, 20 000 entrées aléatoires. ASan et UBSan propres. Tests de mutation : 161 mutations du codec et du CRC, 157 tuées ; les 4 restantes sont équivalentes (`+ 0` sans effet ; mise à jour du CRC après sa lecture ; garde de nom non terminé dont l'absence ne se voit que par une lecture hors tableau ; réinitialisation de piste redondante, retirée du code). Compilation pour la Teensy sans avertissement ; le codec pèse 13 Ko de code, aucune variable statique. Les 25 tests passent.


### B-011 Émulateur : stockage du profil en binaire
- statut: done · priorité: 1 · propriétaire: claude · étiquettes: phase-0, profil
- dépend de: B-010
- références: plan_teensy.md#T7 ; platform/desktop/profile_store_file.*
- terminé quand: l'émulateur lit et écrit le binaire ; le JSON n'est plus utilisé par le programme.
- vérifié (10/10/2026) : `FileProfileStore` (`platform/desktop/profile_store_file.*`) lit et écrit par flux avec `BinaryCodec` (plus de tampon de 256 Ko ni de tableau de jetons de 100 Ko) ; même écriture sûre (`.tmp`, `.bak`, renommage) ; l'émulateur utilise `profiles/default.dmp` (converti une fois depuis `default.json` : 269 octets au lieu de 1 231, relu avec succès). `test_profile_store` réécrit pour le binaire (fichier coupé, octet abîmé, texte quelconque, JSON refusé, fichier vide, version trop récente, `.bak`, profil invalide, pire cas de 12 loops × 16 pistes) ; `test_integration` convertit son profil JSON écrit à la main en binaire. Les 25 tests passent. `profiles/default.json` est gardé en attendant `profile_tool` (B-012). Pas de conversion automatique à la lecture : un ancien fichier JSON n'est plus lu.


### B-012 Outil `profile_tool` (binaire ↔ JSON)
- statut: done · priorité: 2 · propriétaire: claude · étiquettes: phase-0, profil, outillage
- dépend de: B-010
- références: plan_teensy.md#T7 ; tools/
- terminé quand: `profile_tool` convertit un profil binaire en JSON lisible et inversement, avec test d'aller-retour.
- vérifié (10/10/2026) : `tools/profile_tool` (`bin2json`, `json2bin`, `check`) construit sur `tools/profile_tool.{h,cpp}` (testable) et `profile_tool_main.cpp` ; adaptateurs fichier partagés dans `platform/desktop/file_stream.h` (utilisés aussi par `FileProfileStore`). Écriture via « .tmp » puis renommage : un échec ne laisse jamais un fichier de sortie abîmé. Codes de sortie 0 / 1 (échec) / 2 (usage). Test `test_profile_tool` : `profiles/default.json` aller-retour JSON → binaire → JSON → binaire à l'identique, pire cas (12 loops × 16 pistes pleines : JSON 207 Ko, binaire 87 Ko), usage, fichiers absents, JSON invalide, nombre hors flottant (`bpm` 1e39) refusé avec message clair, sortie dans un dossier inexistant ou sur un dossier, fichier binaire coupé / octet abîmé / JSON donné comme binaire / vide. ASan et UBSan propres. Mutations : 23 tuées sur 27 ; les 4 restantes sont équivalentes ou non testables (échec d'écriture disque plein, échec JSON impossible pour un profil binaire valide, écriture déjà filtrée par `valid`). Les 26 tests passent. `profiles/default.json` peut maintenant être supprimé : `profile_tool bin2json profiles/default.dmp` le régénère.


### B-020 Lecture d'en-tête WAV seule
- statut: done · priorité: 1 · propriétaire: claude · étiquettes: phase-0, samples, core
- dépend de: —
- références: plan_teensy.md#T6 ; platform/desktop/sample_loader.*
- terminé quand: une fonction lit seulement l'en-tête (format, canaux, fréquence, nombre d'échantillons) sans charger les données ; testée sur les formats déjà gérés.
- décisions (discussion du 11/10/2026) : analyse de l'en-tête dans `core/wav/` (portable, sans allocation) par accès à une position donnée (`FileReader::read_at` + `size`), mêmes règles que `load_wav` d'aujourd'hui ; `load_wav` réécrit par-dessus (comportement identique, lecture des données par morceaux de 4 Ko) ; `DesktopSampleLibrary::refresh` ne lit plus que l'en-tête.
- vérifié (11/10/2026) : `core/wav/wav_header.{h,cpp}` (`wav::read_header`, interface `FileReader` : `size()` + `read_at(position, tampon, taille)`, `MemoryFile` pour les tests ; 720 octets de code sur la Teensy, aucune variable statique) ; mêmes règles qu'avant (RIFF/WAVE, PCM 16 bits ou flottant 32 bits, extensible, 1 ou 2 canaux, 44 100 Hz, blocs sautés avec leur octet de remplissage, fichier tronqué toléré) avec en plus au plus 64 blocs parcourus et aucun dépassement de 32 bits. `platform/desktop/stdio_file.h` (`StdioFile`). `load_wav` réécrit par-dessus : même comportement (les 29 vérifications de `test_wav` passent sans modification), les données sont lues et converties par morceaux de 4 Ko au lieu de charger tout le fichier. Nouvelle fonction `probe_wav` ; `DesktopSampleLibrary::refresh` ne lit plus que l'en-tête de chaque fichier (un WAV sans aucune image reste ignoré). Nouveau test `test_wav_header` : tous les formats acceptés et refusés, champs lus à leur place, blocs intercalés, tailles fausses (tronqué, 0xFFFFFFFF, 4 Go, bloc de 16 Mo), limite de 64 blocs, **un en-tête de 2 Mo de données se lit avec moins de 100 octets, un bloc LIST de 500 Ko est sauté sans être lu**, erreurs d'entrée-sortie, tous les préfixes de fichiers valides, 30 000 en-têtes abîmés. ASan et UBSan propres. Mutations : 52 tuées sur 54 ; les 2 restantes sont équivalentes (lecture d'un 12e octet non initialisé qui ne peut pas valoir « E », et coupure exactement à 15 octets qui donne le même résultat par un autre chemin). Compilation Teensy sans avertissement. Les 27 tests passent.


### B-021 Lecteur WAV par morceaux
- statut: done · priorité: 2 · propriétaire: claude · étiquettes: phase-0, samples, core
- dépend de: B-020
- références: plan_teensy.md#T6
- terminé quand: interface commune de lecture par morceaux (sans allocation), implémentée sur le Mac, avec test de résultat identique au chargement complet.
- décisions (discussion du 11/10/2026) : `wav::SampleReader` dans `core/wav/` : un lecteur qu'on appelle morceau par morceau (`read(dst, max_images)`, il garde son avancement), lecture de 4 Ko à la fois, conversion à la volée en 16 bits entrelacé, écriture dans une zone fournie par l'appelant ; la conversion flottant → 16 bits quitte `platform/desktop/` pour `core/` ; `load_wav` du Mac s'appuie dessus.
- vérifié (11/10/2026) : `core/wav/wav_reader.{h,cpp}` : `wav::SampleReader` (`read(dst, max_images)`, `frames_left()`, `failed()`) lit le fichier 4 Ko à la fois (tampon de 4 Ko sur la pile, aucune allocation), convertit à la volée en 16 bits entrelacé et écrit dans la zone fournie ; `wav::float_bits_to_int16` (arrondi, écrêté, non-nombre → 0) sort de `platform/desktop/sample_loader.cpp` pour aller dans `core/`. `load_wav` du Mac s'appuie dessus (un seul code pour les deux plateformes) ; les 29 vérifications de `test_wav` passent sans modification. Compilation Teensy sans avertissement (1,3 Ko de code pour les deux fichiers de `core/wav/`). Nouveau test `test_wav_reader` : conversion flottant → 16 bits (valeurs clés, arrondi des deux côtés, écrêtage, infinis, non-nombres, dénormalisé, 54 000 valeurs contre une référence, monotone), PCM identique, flottant mono et stéréo, lecture par morceaux de 1 à 9 999 images identique à la lecture d'un coup (4 formats, données décalées par un bloc LIST), **aucune requête au fichier au-delà de 4 Ko et chaque octet de données demandé une seule fois**, lecture partielle (les 2 premières secondes pour l'écoute), fichier qui rétrécit en cours de lecture (seules les images entières avant la coupure sont livrées, puis échec), lecteur qui reste en échec même si le fichier répond de nouveau, en-tête refusé ou format inconnu, 5 secondes stéréo flottant. ASan et UBSan propres. Mutations : 43 changements, 37 tuées ; les 6 restantes sont équivalentes (`>=` ou `>` à la valeur exacte d'une borne, garde sur NaN dont le résultat est 0 par hasard sur cette plateforme, gardes redondantes avec la boucle, `+ 0`) ; deux gardes redondantes ont été retirées du code. Les 28 tests passent.


### B-022 Arène de samples (`SampleArena`)
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-0, samples, core
- dépend de: B-020
- références: plan_teensy.md#T6
- terminé quand: bloc de mémoire unique dans `core/`, curseur remis à zéro à l'ouverture d'une loop, refus propre quand il est plein, tests (dont débordement).

### B-023 Confirmation du moteur après `ClearAll`
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-0, samples, core
- dépend de: B-022
- références: plan_teensy.md#T6 ; core/engine.*
- terminé quand: un compteur dans `EngineState` confirme que le moteur a traité `ClearAll` avant la remise à zéro de l'arène ; test où une lecture en cours ne lit jamais de mémoire réécrite.

### B-024 Partage des samples identiques et zone d'écoute
- statut: todo · priorité: 3 · propriétaire: claude · étiquettes: phase-0, samples
- dépend de: B-022
- références: plan_teensy.md#T6
- terminé quand: deux pistes qui utilisent le même fichier n'occupent la mémoire qu'une fois ; la prévisualisation utilise une zone séparée et tronquée.

### B-025 `SampleLibrary` à chemins relatifs
- statut: todo · priorité: 3 · propriétaire: claude · étiquettes: phase-0, samples
- dépend de: —
- références: plan_teensy.md#T6, #T10
- terminé quand: la bibliothèque référence les samples par chemin relatif à la racine (dossiers plus tard) ; noms de 47 octets au plus.

### B-030 Fonction « afficher ce Canvas »
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-0, ui
- dépend de: —
- références: plan_teensy.md#T4 ; platform/desktop/emulator.cpp
- terminé quand: l'affichage d'un `Canvas` 128×64 est isolé derrière une fonction de plateforme (celle de l'émulateur d'abord), prête à recevoir le pilote SSD1309.

### B-040 Protocole d'événements de la télécommande
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-0, ui, core
- dépend de: —
- références: plan_teensy.md#T16
- terminé quand: format d'événement (type, index, valeur) dans `core/`, codage et décodage testés ; choix du transport (série ou MIDI USB) noté dans `plan_teensy.md`.

### B-041 Émulateur : option « envoyer à la Teensy »
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-0, ui
- dépend de: B-040
- références: plan_teensy.md#T16
- terminé quand: l'émulateur peut envoyer ses événements par le transport choisi (testé en boucle locale tant qu'il n'y a pas de carte).

### B-050 Installer PlatformIO
- statut: done · priorité: 2 · propriétaire: utilisateur (confirmé le 10/10/2026) · étiquettes: phase-0, outillage
- dépend de: —
- références: plan_teensy.md#T11
- terminé quand: `pio --version` répond. Confirmation de l'utilisateur avant `brew install platformio` ; le premier téléchargement (plateforme Teensy, Teensyduino 1.62, compilateur ARM) va dans `~/.platformio`.
- vérifié: `brew install platformio` fait, `pio --version` répond « PlatformIO Core, version 6.2.0 ». Homebrew a nettoyé au passage d'anciennes versions de paquets et son cache (readline, sqlite, xz, zstd, vcpkg...) ; le build du projet et les 24 tests passent toujours.

### B-051 `platformio.ini` et compilation de `core/` pour le Cortex-M7
- statut: done · priorité: 2 · propriétaire: claude · étiquettes: phase-0, outillage, core
- dépend de: B-050
- références: plan_teensy.md#T11
- terminé quand: carte `teensy41`, `build_src_filter` sur `core/` et `platform/teensy/`, options `-std=gnu++17 -fno-exceptions -fno-rtti -O2 -Wdouble-promotion` ; un programme minimal qui inclut `core/` compile sans erreur ; surprises de portabilité corrigées.
- vérifié: `pio run` réussit pour `teensy41` (plateforme teensy 6.0.0, framework-arduinoteensy 1.162.0 = Teensyduino 1.62, compilateur ARM GCC 15.2). `platformio.ini` à la racine, `platform/teensy/main.cpp` minimal qui tire `Engine`, `UiController` et `Canvas` dans l'édition de liens (le codec JSON `profile_codec.cpp` est exclu : PC seulement). Une surprise de portabilité corrigée : `dsp::PI` et `dsp::TWO_PI` heurtaient les macros `PI` et `TWO_PI` d'Arduino, renommées `PI_F` et `TWO_PI_F` (`core/dsp_utils.h`, `core/synth_kick.cpp`, `platform/desktop/tone_test.cpp`) ; build du Mac et 24 tests toujours verts. Avertissements restants dans `core/` : 1 conversion float vers double (`sequencer.h:73`, voulue) et 5 avertissements de troncature de `snprintf` dans `ui_controller.cpp`. **Tous traités ensuite (10/10/2026)** : `static_cast<double>(bpm_)` dans `sequencer.h`, et valeurs bornées par `clamp_int` avant les `snprintf` (aucun changement d'affichage pour les valeurs réelles). `pio run` sur un build propre des sources du projet : 0 avertissement ; build du Mac et 24 tests toujours verts.

### B-052 Remplacer le `%f` du gain en dB
- statut: done · priorité: 3 · propriétaire: claude · étiquettes: phase-0, core
- dépend de: —
- références: plan_teensy.md#T11 ; core/ui/ui_controller.cpp
- terminé quand: l'affichage du gain en dB n'utilise plus de format flottant, avec le même rendu ; tests d'interface inchangés.
- vérifié: `snprintf("%+ddB", lround(db))` dans `core/ui/ui_controller.cpp` ; plus aucun format flottant dans `core/` hors `profile_codec` (JSON, PC seulement) ; build propre, 24 tests sur 24 passent. Prolongement hors backlog (demande de l'utilisateur) : pas du gain à 0,5 dB, affichage `+0.5dB`.

### B-053 Taille du code et de la mémoire sur la Teensy
- statut: done · priorité: 2 · propriétaire: claude · étiquettes: phase-0, outillage
- dépend de: B-051
- références: plan_teensy.md#T11, #T6
- terminé quand: la sortie de l'éditeur de liens est relevée (code, RAM1, RAM2, PSRAM) et comparée aux 61 Ko du profil, aux 68 Ko de l'interface et aux 19 Ko du moteur.
- vérifié: mesures de `teensy_size` pour le programme minimal (moteur + interface, sans matériel) : FLASH code 62 Ko, données 11 Ko ; RAM1 variables 100 Ko (moteur ~19 Ko + interface ~68 Ko avec le profil + tampons + Arduino), code en ITCM 59 Ko, 359 Ko libres pour les variables locales ; RAM2 12 Ko utilisés, **512 Ko libres pour l'arène de samples** (en plus des 16 Mo de PSRAM). Le moteur et l'interface tiennent largement.

### B-060 Reprise de la carte SD (`.tmp` / `.bak` + CRC)
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-0, sd, profil, core
- dépend de: B-010
- références: plan_teensy.md#T10
- terminé quand: séquence d'écriture `.tmp` puis `.bak` puis principal, reprise au démarrage sur le fichier dont le CRC est valide, écrite dans `core/` et testée sur le Mac avec des fichiers tronqués à chaque étape.

### B-061 Messages et action « Recharger la carte »
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-0, sd, ui
- dépend de: B-060
- références: plan_teensy.md#T10
- terminé quand: « Pas de carte » et « Echec sauvegarde » s'affichent dans les cas prévus ; une ligne « Recharger la carte » dans le menu de la loop relance l'initialisation ; tests d'interface.

### B-070 Journal d'erreurs sur la carte
- statut: todo · priorité: 3 · propriétaire: claude · étiquettes: phase-0, sd, core
- dépend de: —
- références: plan_teensy.md#T14
- terminé quand: file d'événements en mémoire, écriture de `log.txt` par la boucle principale (jamais depuis l'audio), 64 Ko au plus avec `log.old`, événements listés dans T14 ; tests sur le Mac avec un faux stockage.

### B-080 Série de scénarios de charge
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-0, outillage
- dépend de: —
- références: plan_teensy.md#T13, #T1 ; tools/bench_engine.cpp
- terminé quand: `bench_engine` exécute des scénarios de charge croissante (8 pistes stéréo à 40 lectures, 16 pistes à 40, 16 pistes à 80, avec et sans pitch, avec et sans queues de release) et affiche la moyenne et le pire bloc de chacun ; le même code est prêt à tourner sur la carte.

## Phase 1 : mise en route sur la carte (une inconnue à la fois)

### B-100 Clignotement et liaison série
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-1, matériel, outillage
- dépend de: B-001, B-002, B-051
- références: plan_teensy.md#T11, #T14
- terminé quand: un programme PlatformIO est téléversé, la LED clignote, un message arrive sur la série de l'ordinateur.

### B-101 Écran seul : motif de test
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-1, matériel, ui
- dépend de: B-100
- références: plan_teensy.md#T4
- terminé quand: le pilote SSD1309 maison (SPI) affiche un motif de test sur l'écran.

### B-102 `UiController` sur l'écran
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-1, ui
- dépend de: B-101, B-030
- références: plan_teensy.md#T4
- terminé quand: le `Canvas` rendu par `UiController` s'affiche à l'écran, identique à l'émulateur.

### B-103 Audio par USB, pattern fixe
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-1, audio
- dépend de: B-100
- références: plan_teensy.md#T9, #T14
- terminé quand: le moteur joue un pattern fixe avec quelques samples en mémoire, entendu par la sortie audio USB de l'ordinateur.

### B-104 Mesure du CPU
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-1, audio, outillage
- dépend de: B-103, B-080
- références: plan_teensy.md#T1, #T13
- terminé quand: la série de scénarios est mesurée sur la carte (compteur de cycles), les résultats sont notés dans `plan_teensy.md`, et on sait à quel scénario le budget de 2,9 ms est dépassé.

### B-105 Sortie analogique : PCM5102A, XSMT, ordre d'initialisation
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-1, audio, matériel
- dépend de: B-103
- références: plan_teensy.md#T9
- terminé quand: le son sort par le jack du module sans plop au démarrage (XSMT sur une broche, tenue basse puis passée haute ~100 ms après l'I2S) ; ordre d'initialisation de T9 respecté ; gain de sortie réglé à l'écoute. Le bruit éventuel est noté, pas encore traité.

### B-106 Télécommande USB depuis l'émulateur
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-1, ui
- dépend de: B-041, B-102, B-100
- références: plan_teensy.md#T16
- terminé quand: l'émulateur commande la Teensy, qui met à jour son écran et son son.

### B-107 Carte SD : samples dans l'arène en PSRAM
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-1, sd, samples
- dépend de: B-021, B-022, B-023, B-100
- références: plan_teensy.md#T6, #T10
- terminé quand: une loop dont les samples sont sur la carte SD est chargée dans la PSRAM et jouée ; le temps de chargement est relevé.

### B-108 Profil binaire sur la carte SD
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-1, sd, profil
- dépend de: B-060, B-107
- références: plan_teensy.md#T10, #T7
- terminé quand: sauvegarde et chargement du profil sur la carte, y compris la reprise après une coupure simulée pendant l'écriture.

### B-109 Réévaluer le choix de la carte
- statut: todo · priorité: 2 · propriétaire: utilisateur · étiquettes: phase-1, matériel
- dépend de: B-104
- références: plan_teensy.md#T1, #T18
- terminé quand: décision notée : garder la Teensy 4.1, ou changer si le budget CPU est dépassé même après les optimisations de T13.

## Phase 2 : intégration et essais

### B-200 Intégration complète sur la carte
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-2
- dépend de: B-106, B-108
- références: plan_teensy.md
- terminé quand: une session complète (charger une loop, éditer, enregistrer, sauvegarder, recharger) fonctionne sur la carte pilotée depuis l'ordinateur.

### B-201 Régler la compensation de latence
- statut: todo · priorité: 2 · propriétaire: utilisateur · étiquettes: phase-2, audio
- dépend de: B-200
- références: plan_teensy.md#T2 ; plan_record.md
- terminé quand: la valeur par défaut de la compensation est réglée à l'écoute sur la carte.

### B-202 Charge sur de vraies loops
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-2, audio
- dépend de: B-200, B-104
- références: plan_teensy.md#T1
- terminé quand: la charge est mesurée sur tes vraies loops et comparée aux scénarios.

## Mis de côté (`parked`) : à reprendre si le besoin apparaît

### B-300 Boutons (74HC165)
- statut: parked · priorité: 3 · propriétaire: claude · étiquettes: matériel, ui
- dépend de: B-200
- références: plan_teensy.md#T3
- terminé quand: conception déjà faite (4 puces en guirlande, table bouton → puce / bit, balayage 1–2 kHz, anti-rebond) ; à réaliser quand l'interface sur la carte devient prioritaire.

### B-301 Horodatage des entrées
- statut: parked · priorité: 3 · propriétaire: claude · étiquettes: ui, audio
- dépend de: B-300
- références: plan_teensy.md#T2
- terminé quand: à définir quand le sujet est repris.

### B-302 Émulateur « mode contraintes »
- statut: parked · priorité: 3 · propriétaire: claude · étiquettes: ui, outillage
- dépend de: —
- références: plan_teensy.md#T12
- terminé quand: version minimale = retard et gigue de 0 à 15 ms sur les entrées de l'émulateur, pour voir la tolérance de l'enregistrement.

### B-303 BPM sur la carte
- statut: parked · priorité: 3 · propriétaire: claude · étiquettes: ui
- dépend de: B-300
- références: plan_teensy.md#T5
- terminé quand: à définir (le BPM se règle par la télécommande en attendant).

### B-304 Fréquence d'échantillonnage réelle
- statut: parked · priorité: 3 · propriétaire: claude · étiquettes: audio, core
- dépend de: B-104
- références: plan_teensy.md#T8
- terminé quand: deux constantes `FILE_RATE` / `DEVICE_RATE`, correction de hauteur dans le lecteur, fréquence mesurée sur la carte.

### B-305 Niveaux de qualité du moteur
- statut: parked · priorité: 3 · propriétaire: claude · étiquettes: audio, core
- dépend de: B-104
- références: plan_teensy.md#T13
- terminé quand: seulement si la mesure de B-104 montre un dépassement ; leviers dans l'ordre noté en T13.

### B-306 MIDI
- statut: parked · priorité: 3 · propriétaire: claude · étiquettes: ui
- dépend de: —
- références: plan_teensy.md#T15
- terminé quand: à définir quand le sujet est repris.

### B-307 Flash 256 Mo de la carte
- statut: parked · priorité: 3 · propriétaire: utilisateur · étiquettes: matériel
- dépend de: —
- références: plan_teensy.md#T17
- terminé quand: bonus éventuel si la carte achetée en a une ; rien n'en dépend.

### B-308 Boîtier et alimentation
- statut: parked · priorité: 3 · propriétaire: utilisateur · étiquettes: matériel
- dépend de: B-200
- références: plan_teensy.md#T19
- terminé quand: interrupteur général, prise USB accessible, qualité de l'alimentation (bruit) : à décider.

### B-309 Passer les textes de l'écran en anglais
- statut: parked · priorité: 3 · propriétaire: claude · étiquettes: ui, core
- dépend de: —
- références: CLAUDE.md ; core/ui/ui_controller.cpp ; tests/ (tests d'interface et tests de rendu de référence)
- terminé quand: tous les textes dessinés dans le `Canvas` (menus, messages d'erreur comme « Pas de carte » / « Echec sauvegarde », libellés des réglages) sont en anglais, sans accents si la police 5x7 ne les gère pas ; les tests d'interface et les rendus de référence sont mis à jour. À faire à terme, pas avant la mise en route sur la carte. Les nouveaux textes d'écran restent en français d'ici là, pour rester cohérents.
