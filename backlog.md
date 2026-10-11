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
- **B-025** `SampleLibrary` à chemins relatifs (priorité 3)

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
- statut: parked · priorité: 1 · propriétaire: claude · étiquettes: phase-0, samples, core, en-attente-de-la-carte
- dépend de: B-020
- références: plan_teensy.md#T6 (point 3c) ; B-029
- terminé quand: (à redéfinir après la décision de B-029) un bloc de mémoire pour les samples d'une loop dans `core/`, remis à zéro à l'ouverture d'une loop, refus propre quand il est plein, tests (dont débordement).
- **mise en attente (11/10/2026, décision de l'utilisateur)** : le chantier est reporté jusqu'à ce que la Teensy soit disponible, parce que le choix de structure dépend de mesures sur la carte (coût CPU d'une lecture de sample par blocs dans `SamplePlayer`, vitesse de lecture de la carte SD, temps de copie en PSRAM). L'émulateur fonctionne sans arène (`std::vector`) et rien ne bloque.
- options discutées le 11/10/2026 : (1) **arène** (blocs rangés à la suite, remise à zéro à chaque ouverture de loop ; remplacer un sample laisse un trou ; quand la mémoire est pleine à cause des trous, compactage en **rechargeant depuis la carte** : `ClearAll`, `reset()`, rechargement des pistes depuis le modèle de l'interface ; silence de la durée d'un chargement, demandé à l'utilisateur (« Memoire pleine, compacter ? ») ; récupère le dernier bloc alloué en reculant le curseur) ; (2) **blocs chaînés** (idée de l'utilisateur, famille FAT / LFS : blocs de taille fixe, chaque sample est une chaîne de blocs, plus aucun trou ni compactage ; coût : `SamplePlayer` doit lire par tronçons avec interpolation entre deux blocs, avec un coût CPU à mesurer ; `SampleReader` écrit déjà bloc par bloc sans changement) ; (3) tas à blocs libres (réduit les trous sans les supprimer, écarté) ; (4) emplacements fixes de 1 Mo par piste (écarté : un 808 fait environ 3 Mo). Avec des samples d'environ 3 Mo, deux remplacements de gros samples saturent 16 Mo, donc le problème des trous est réel.
- découpage envisagé si l'arène est retenue : (a) la classe `SampleArena` seule dans `core/` (allouer, remettre à zéro, capacité, alignement sur 4 octets, « plein » proprement, annuler la dernière allocation) ; (b) confirmation du moteur après `ClearAll` (B-023 ; la file `released_` pourrait suffire comme signal) ; (c) branchement de l'émulateur (arène de 16 Mo, `reset()` à l'entrée dans la vue Loops une fois le moteur confirmé) ; (d) l'édition : ajout et remplacement d'un sample dans une loop ouverte, trous, compactage sur demande ; (e) partage des samples identiques et zone d'écoute séparée (B-024) ; (f) le côté Teensy (bloc de PSRAM), dans B-107.

### B-023 Confirmation du moteur après `ClearAll`
- statut: parked · priorité: 1 · propriétaire: claude · étiquettes: phase-0, samples, core, en-attente-de-la-carte
- dépend de: B-022
- références: plan_teensy.md#T6 ; core/engine.*
- terminé quand: un compteur dans `EngineState` confirme que le moteur a traité `ClearAll` avant la remise à zéro de l'arène ; test où une lecture en cours ne lit jamais de mémoire réécrite.
- mise en attente avec B-022 (11/10/2026) : la structure mémoire des samples est rouverte (voir B-022 et B-029).


### B-024 Partage des samples identiques et zone d'écoute
- statut: parked · priorité: 3 · propriétaire: claude · étiquettes: phase-0, samples, en-attente-de-la-carte
- dépend de: B-022
- références: plan_teensy.md#T6
- terminé quand: deux pistes qui utilisent le même fichier n'occupent la mémoire qu'une fois ; la prévisualisation utilise une zone séparée et tronquée.
- mise en attente avec B-022 (11/10/2026) : le partage des samples identiques et la zone d'écoute dépendent de la structure mémoire retenue (voir B-029).


### B-025 `SampleLibrary` à chemins relatifs
- statut: todo · priorité: 3 · propriétaire: claude · étiquettes: phase-0, samples
- dépend de: —
- références: plan_teensy.md#T6, #T10
- terminé quand: la bibliothèque référence les samples par chemin relatif à la racine (dossiers plus tard) ; noms de 47 octets au plus.

### B-026 Vue Loops : vider la machine à l'entrée, pas de retour, confirmation d'abandon
- statut: done · priorité: 1 · propriétaire: claude · étiquettes: phase-0, samples, ui, core
- dépend de: —
- références: plan_teensy.md#T6 (point 3b) ; plan_loops.md ; core/ui/ui_controller.cpp (`open_loops`, `on_loops_ok`, `on_back`, `new_loop`)
- terminé quand: décisions de l'utilisateur (11/10/2026) : (1) dès qu'on entre dans la vue « changer de loop », l'interface envoie `ClearAll` (le son s'arrête, les anciens samples sont rendus par le moteur) et remet son modèle à vide ; (2) **pas de retour** depuis cette vue (Retour ne fait rien) : on y choisit une loop ou « + Nouvelle loop » ; (3) si la loop courante est modifiée et non sauvegardée, « Quitter sans sauv. ? » est demandé **avant** d'entrer dans la vue, donc avant de vider. Tests d'interface mis à jour (`test_loops` et autres). Détails d'interface à valider avec l'utilisateur avant de coder.
- vérifié (11/10/2026) : `UiController::enter_loops` (appelée par la ligne de titre et par « Charger une loop » du menu de la loop) : si la loop est modifiée et non sauvegardée, premier appui = « Quitter sans sauv. ? » (en bas de l'écran pour la ligne de titre, sur la ligne du menu pour « Charger une loop »), second appui = entrer ; n'importe quel autre geste annule la question ; entrer envoie `ClearAll` (le son s'arrête, le moteur rend ses samples), vide le modèle de l'écran, et si la file de commandes est pleine on reste où l'on est (message « Echec de l'action », la question sera reposée). **Retour ne fait rien dans la liste des loops.** Le curseur de la liste démarre sur la loop qu'on vient de quitter (un seul appui pour la rouvrir ; ajout de ma part, à ta discrétion). « + Nouvelle loop » ne renvoie plus de commande (la machine est déjà vide) ; la confirmation à la validation d'une loop et le code de suppression de la « loop courante » (devenus inutiles) sont retirés. Tests : `test_loops` réécrit sur ces points (entrée, question depuis les deux entrées, annulations, échec de file, chargements après entrée, suppression, affichage), `test_step_edit` et `test_keyboard` ajustés. ASan et UBSan propres. Mutations : 29 changements du nouveau code, 10 survivants au premier tour dont 6 corrigés par de nouveaux tests ou de la simplification ; restent 4 équivalents (réinitialisation défensive du modèle, borne défensive du curseur, deux conditions redondantes). Les 28 tests passent ; compilation Teensy sans avertissement.


### B-027 Vérifier une loop avec les en-têtes avant de la charger
- statut: done · priorité: 2 · propriétaire: claude · étiquettes: phase-0, samples, ui, core
- dépend de: B-026
- références: plan_teensy.md#T6 ; core/wav/wav_header.h ; core/ui/sample_library.h
- terminé quand: décisions de l'utilisateur (11/10/2026) : avant de charger une loop, l'interface contrôle chaque sample par son en-tête seule (existe, format accepté, nombre d'images) et en déduit la taille totale (images × canaux × 2 octets, par piste) ; **si la loop ne tient pas dans la mémoire des samples, elle est refusée** (« Memoire pleine », rien n'est chargé, on reste dans la liste des loops : on évite l'état instable) ; la mémoire des samples vaut **16 Mo sur l'émulateur comme sur la Teensy** (une constante de `config.h`, pour unifier les comportements ; la vraie arène de la Teensy sera un peu plus petite : zone d'écoute, à revoir avec B-022). `SampleLibrary` est étendue : taille d'un sample sans le charger, capacité totale. Les samples manquants sont comptés et annoncés avant le chargement ; ce qu'on en fait est l'objet de B-028. Pas de limite appliquée à l'ajout d'une piste en cours d'édition tant que l'arène n'est pas branchée (B-022).
- vérifié (11/10/2026) : `cfg::SAMPLE_MEMORY_BYTES` = 16 Mo dans `config.h` ; `SampleLibrary` étendue (`info(index, images, canaux)` lue dans l'en-tête sans charger, `memory_bytes()` ; par défaut « inconnu » et « pas de limite », donc les autres bibliothèques ne changent pas) ; `DesktopSampleLibrary` l'implémente avec `probe_wav` et la constante ; `UiController::load_loop` calcule la taille totale en 64 bits (images × canaux × 2 octets, par piste, un même fichier sur deux pistes compté deux fois, en-tête illisible ou sample absent non comptés) **avant** toute lecture de son : si elle dépasse la mémoire, « Memoire pleine », rien n'est chargé ni envoyé, la liste des loops reste ouverte et la loop reste listée. Tests : `test_loops` (taille exacte acceptée, un octet de plus refusé, une plus petite loop de la même liste passe, ressayer quand la place revient, sans limite, 8 Gio sans dépassement de 32 bits, en-tête illisible, sample absent, doublon, unités, affichage de la liste avec le message) ; `test_integration` avec de vrais fichiers de 16 Mo pile (chargé) et de 16 Mo plus une image (refusé, aucun fichier chargé), lecture des en-têtes sans rien charger. ASan et UBSan propres. Mutations : 16 sur 16 tuées. Les 28 tests passent ; compilation Teensy sans avertissement. Les samples manquants gardent le comportement d'aujourd'hui (pistes sautées et annonce) en attendant B-028.


### B-028 Pistes au sample manquant : conservées et remplaçables
- statut: done · priorité: 2 · propriétaire: claude · étiquettes: phase-0, samples, ui, core
- dépend de: B-027
- références: plan_teensy.md#T6 ; core/ui/ui_controller.cpp (`load_loop`, `build_loop`, `confirm_replace`) ; plan_loops.md ; core/engine.* (piste sans sample)
- terminé quand: décisions de l'utilisateur (11/10/2026) : (1) une loop dont des samples manquent (ou ne sont pas valides) **s'ouvre quand même** et ses pistes sont **gardées** au lieu d'être sautées ; (2) une piste sans sample **se comporte comme les autres** : sa timeline s'affiche normalement, on peut y appliquer **toutes les transformations** (gain, enveloppe, notes, vélocité, pitch, décalages, largeur, balance...) comme si le sample existait ; elle reste **silencieuse** tant qu'elle n'a pas de sample ; (3) on **affiche le nom du sample manquant** (celui du profil) et on le **garde à la sauvegarde** tant qu'il n'a pas été changé : si le fichier revient sur la carte (redémarrage de la machine, carte rechargée), la loop refonctionne à la réouverture ; (4) « Changer le sample » donne enfin un son. Le moteur doit accepter une piste sans sample (à vérifier). (5) **un indicateur « ? »** s'affiche pour signaler le problème de sample (décision de l'utilisateur le 11/10/2026) ; emplacement proposé : à droite de la ligne de la piste, à la place du petit carré de jeu (une piste sans sample ne joue pas, donc il n'y a pas de conflit).
- vérifié (11/10/2026) : `UiController::load_loop` garde toutes les pistes : celle dont le sample est introuvable, illisible ou au mauvais format est chargée sans son (`Track::missing`), avec le nom du sample du profil, son pattern et tous ses réglages ; ses commandes partent comme celles des autres (`SetTrackSample` sans données, ignorée par le moteur : il n'y a rien à jouer, et le moteur n'allume pas l'indicateur de jeu) ; seule exception, un index de sample hors de la table du profil (que les codecs ne produisent pas) écarte la piste. Un « ? » s'affiche à droite de la ligne dans la liste des pistes, à la place du carré de jeu (inversé quand la ligne est sélectionnée). « Changer le sample » donne un son et retire le « ? » ; la sauvegarde garde le nom d'origine tant que rien n'est remplacé (profil relu : `hat.wav` conservé) ; si le fichier revient, la loop se recharge normalement à la réouverture. Moteur vérifié de bout en bout (`test_integration`) : la piste manquante est silencieuse, la piste remplacée joue à son pas, la loop rechargée avec le fichier revenu est identique à l'originale. Tests : `test_loops` (pistes gardées, commandes, nom, « ? » affiché et inversé, modifications d'un pas / vélocité / gain envoyées à la bonne piste du moteur, sauvegarde, remplacement, fichier revenu, nombre de canaux transmis, index hors table), `test_integration` mis à jour. ASan et UBSan propres. Mutations : 15 changements du nouveau code, 4 survivants au premier tour corrigés par de nouveaux tests : 15 sur 15 tuées. Les 28 tests passent ; compilation Teensy sans avertissement. Pas de « ? » dans l'en-tête de la vue Pattern (seulement dans la liste des pistes).


### B-029 Choisir la structure mémoire des samples (après mesures sur la carte)
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-1, samples, audio, matériel
- dépend de: B-104, B-107
- références: plan_teensy.md#T6 (point 3c) ; B-022, B-023, B-024
- terminé quand: les trois mesures sont faites sur la carte (coût CPU d'une lecture de sample par blocs comparé à la lecture contiguë ; vitesse de lecture de la carte SD en Mo/s ; temps de copie de 12 Mo en PSRAM) et la décision est écrite dans `plan_teensy.md` T6 : arène avec compactage par rechargement, ou blocs chaînés (ou autre). B-022, B-023 et B-024 sont alors réécrites et rendues prêtes selon ce choix.

### B-030 Fonction « afficher ce Canvas »
- statut: done · priorité: 2 · propriétaire: claude · étiquettes: phase-0, ui
- dépend de: —
- références: plan_teensy.md#T4 ; platform/desktop/emulator.cpp
- terminé quand: l'affichage d'un `Canvas` 128×64 est isolé derrière une fonction de plateforme (celle de l'émulateur d'abord), prête à recevoir le pilote SSD1309.
- **supprimée / absorbée (11/10/2026, décision de l'utilisateur)** : pas de travail à faire ici. L'émulateur a déjà sa fonction d'affichage (`ScreenView::draw`) et le `Canvas` a déjà le format du SSD1306 (`data()` donne les 1 024 octets prêts à partir). L'interface `Display` (« afficher ce Canvas ») sera dessinée avec le pilote SSD1309 sous les yeux, dans **B-101** ; la détection de changement du `Canvas` (ne renvoyer l'image que si elle a changé, T4) est notée dans B-102.


### B-040 Protocole d'événements de la télécommande
- statut: parked · priorité: 2 · propriétaire: claude · étiquettes: phase-0, ui, core, en-attente-de-la-carte
- dépend de: —
- références: plan_teensy.md#T16
- terminé quand: format d'événement (type, index, valeur) dans `core/`, codage et décodage testés ; choix du transport (série ou MIDI USB) noté dans `plan_teensy.md`.
- **mise en attente (11/10/2026, décision de l'utilisateur)** : reportée jusqu'à ce que la Teensy soit disponible : le protocole est simple et ce sont les essais sur la carte (comportement du port série USB, type d'USB) qui le confirmeront.
- **proposition discutée, non tranchée** : (1) transport **USB série** (flux bidirectionnel, port `/dev/cu.usbmodem…` ouvert en POSIX côté Mac, cohabite avec l'audio USB dans « Série + MIDI + Audio » de T14) plutôt que MIDI USB (messages de 3 octets, CoreMIDI, conventions musicales sans rapport) ; (2) format en **lignes de texte**, une par événement (`wheel 1`, `ok`, `back`, `menu`, `mode`, `play`, `step 5`, `black 3`, `blackup 3`, `pot 0.4375`), pour pouvoir taper un événement à la main dans un terminal série et lire ce qui circule, USB garantissant déjà l'intégrité des octets ; (3) seulement des événements de l'ordinateur vers la Teensy au départ, le retour de l'image de l'écran (1 024 octets par changement) étant une tâche ultérieure ; (4) dans `core/` : une fonction qui écrit un `InputEvent` en ligne, un lecteur d'octets au fil de l'eau qui reconstitue les lignes et rend des événements ; tests : tous les types, lignes coupées en morceaux, `\r\n` et `\n`, lignes inconnues ou trop longues ignorées sans bloquer le flux, valeurs hors limites, lignes vides, bruit aléatoire.


### B-041 Émulateur : option « envoyer à la Teensy »
- statut: parked · priorité: 2 · propriétaire: claude · étiquettes: phase-0, ui, en-attente-de-la-carte
- dépend de: B-040
- références: plan_teensy.md#T16
- terminé quand: l'émulateur peut envoyer ses événements par le transport choisi (testé en boucle locale tant qu'il n'y a pas de carte).
- mise en attente avec B-040 (11/10/2026).


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
- statut: parked · priorité: 1 · propriétaire: claude · étiquettes: phase-0, sd, profil, core, en-attente-de-la-carte
- dépend de: B-010
- références: plan_teensy.md#T10
- terminé quand: séquence d'écriture `.tmp` puis `.bak` puis principal, reprise au démarrage sur le fichier dont le CRC est valide, écrite dans `core/` et testée sur le Mac avec des fichiers tronqués à chaque étape.
- **mise en attente (11/10/2026, décision de l'utilisateur)** : reporté jusqu'à ce que la Teensy soit disponible. Raison : l'interface de fichiers que la logique utiliserait (ouvrir, lire, écrire, renommer, supprimer, tester l'existence) est à ajuster sur la vraie bibliothèque de la carte SD ; en attendant, `FileProfileStore` du Mac écrit déjà par « .tmp » puis renommage avec copie en « .bak » (mais sa lecture ne regarde que le fichier principal).
- **proposition discutée, non tranchée** (à reprendre avec la carte) : (1) mettre la logique dans `core/` derrière une petite interface de fichiers, branchée sur `std::filesystem` au Mac et sur la carte SD sur la Teensy ; (2) test central : un faux système de fichiers en mémoire qui simule une **coupure après chaque opération** de l'écriture, puis relance la lecture, qui doit toujours rendre un profil complet (ancien ou nouveau) ; (3) ordre de lecture : T10 prévoyait principal, `.tmp`, `.bak` ; un `.tmp` complet et valide encore présent est toujours **plus récent** que le principal (coupure entre l'écriture du `.tmp` et le renommage), donc l'ordre **`.tmp`, principal, `.bak`** est préférable ; (4) la lecture signale « restauré » (message d'interface dans B-061) et ne réécrit pas le principal pendant le chargement (la carte est peut-être défaillante).


### B-061 Messages et action « Recharger la carte »
- statut: parked · priorité: 2 · propriétaire: claude · étiquettes: phase-0, sd, ui, en-attente-de-la-carte
- dépend de: B-060
- références: plan_teensy.md#T10
- terminé quand: « Pas de carte » et « Echec sauvegarde » s'affichent dans les cas prévus ; une ligne « Recharger la carte » dans le menu de la loop relance l'initialisation ; tests d'interface.
- mise en attente avec B-060 (11/10/2026) : elle dépend du résultat de lecture de B-060 (« restauré ») et de la détection d'une carte absente, qui se règle avec la carte.


### B-070 Journal d'erreurs sur la carte
- statut: parked · priorité: 3 · propriétaire: claude · étiquettes: phase-0, sd, core, en-attente-de-la-carte
- dépend de: —
- références: plan_teensy.md#T14
- terminé quand: file d'événements en mémoire, écriture de `log.txt` par la boucle principale (jamais depuis l'audio), 64 Ko au plus avec `log.old`, événements listés dans T14 ; tests sur le Mac avec un faux stockage.
- mise en attente (11/10/2026, décision de l'utilisateur) : reporté jusqu'à ce que la Teensy soit disponible (le journal s'écrit sur la carte SD).


### B-080 Série de scénarios de charge
- statut: parked · priorité: 2 · propriétaire: claude · étiquettes: phase-0, outillage, en-attente-de-la-carte
- dépend de: —
- références: plan_teensy.md#T13, #T1 ; tools/bench_engine.cpp
- terminé quand: `bench_engine` exécute des scénarios de charge croissante (8 pistes stéréo à 40 lectures, 16 pistes à 40, 16 pistes à 80, avec et sans pitch, avec et sans queues de release) et affiche la moyenne et le pire bloc de chacun ; le même code est prêt à tourner sur la carte.

## Phase 1 : mise en route sur la carte (une inconnue à la fois)
- idée ajoutée (11/10/2026) : une variante optionnelle du banc où `SamplePlayer` lit un sample réparti par blocs, pour estimer le surcoût d'une lecture par blocs (relatif, sur le Mac), en préparation de B-029.
- mise en attente (11/10/2026, décision de l'utilisateur) : reportée jusqu'à ce que la Teensy soit disponible (la comparaison Mac / carte se fait avec la carte).


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
- ajouté (11/10/2026, suite à B-030) : concevoir à ce moment l'interface commune « afficher ce `Canvas` » (`Display::show`), avec le pilote sous les yeux (image entière ou page par page, initialisation, contraste), puis l'implémenter dans l'émulateur et dans le pilote.

### B-102 `UiController` sur l'écran
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-1, ui
- dépend de: B-101
- références: plan_teensy.md#T4
- terminé quand: le `Canvas` rendu par `UiController` s'affiche à l'écran, identique à l'émulateur.
- ajouté (11/10/2026, suite à B-030) : ne renvoyer l'image à l'écran que si le `Canvas` a changé (T4) : petite classe de détection de changement dans `core/`, testable sur le Mac.

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

### B-107 Carte SD : samples en PSRAM (première version simple)
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-1, sd, samples
- dépend de: B-021, B-100
- références: plan_teensy.md#T6, #T10 ; B-029
- terminé quand: une loop dont les samples sont sur la carte SD est chargée dans la PSRAM (avec `wav::SampleReader`) et jouée, avec une première structure **très simple** (les samples rangés à la suite dans un bloc de PSRAM, remis à zéro à l'ouverture d'une loop) en attendant la décision de B-029 ; le temps de chargement et la vitesse de lecture de la carte (Mo/s) sont relevés pour B-029.

### B-108 Profil binaire sur la carte SD
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-1, sd, profil
- dépend de: B-060, B-107
- références: plan_teensy.md#T10, #T7
- terminé quand: sauvegarde et chargement du profil sur la carte, y compris la reprise après une coupure simulée pendant l'écriture.
- note (11/10/2026) : B-060 est en attente ; elle redevient prête quand la carte est là, et se fait alors avec B-108.


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
