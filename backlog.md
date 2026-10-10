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
*(à jour au 10/10/2026)*
- **B-001** Commander le matériel (utilisateur, priorité 1)
- **B-010** Codec binaire du profil (priorité 1)
- **B-020** Lecture d'en-tête WAV seule (priorité 1)
- **B-030** Fonction « afficher ce Canvas » isolée (priorité 2)
- **B-040** Protocole d'événements de la télécommande (priorité 2)
- **B-050** Installer PlatformIO (utilisateur doit confirmer, priorité 2)
- **B-070** Journal d'erreurs sur la carte (priorité 3)
- **B-080** Série de scénarios de charge (priorité 2)

---

## Matériel

### B-001 Commander le matériel
- statut: todo · priorité: 1 · propriétaire: utilisateur · étiquettes: phase-1, matériel
- dépend de: —
- références: plan_teensy.md#T17, #T9, #T4, #T19 ; fiche_technique_mat_rielle_composants.md
- terminé quand: tout est reçu. À commander : Teensy 4.1 avec PSRAM (avis : DEV-26-P, 16 Mo sans flash, 48,95 $ ; la variante avec flash est un bonus que rien n'utilise) · module PCM5102A avec jack · écran Waveshare 2.42" SSD1309 SPI · carte microSD 16 à 32 Go (FAT32) · câbles, plaque d'essai · alimentation USB de qualité. Pas de boutons ni de potentiomètres pour l'instant.

## Phase 0 : sans matériel, sur le Mac

### B-010 Codec binaire du profil
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-0, profil, core
- dépend de: —
- références: plan_teensy.md#T7, #T14 ; core/profile/
- terminé quand: `BinaryCodec` dans `core/` (enregistrements balisés, valeurs par défaut absentes, étiquettes inconnues sautées, **octet de version** en en-tête, CRC32), interface de flux `ByteSource` / `ByteSink`, tests (aller-retour, fichier tronqué, CRC faux, fuzz, pire cas de taille).

### B-011 Émulateur : stockage du profil en binaire
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-0, profil
- dépend de: B-010
- références: plan_teensy.md#T7 ; platform/desktop/profile_store_file.*
- terminé quand: l'émulateur lit et écrit le binaire ; le JSON n'est plus utilisé par le programme.

### B-012 Outil `profile_tool` (binaire ↔ JSON)
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-0, profil, outillage
- dépend de: B-010
- références: plan_teensy.md#T7 ; tools/
- terminé quand: `profile_tool` convertit un profil binaire en JSON lisible et inversement, avec test d'aller-retour.

### B-020 Lecture d'en-tête WAV seule
- statut: todo · priorité: 1 · propriétaire: claude · étiquettes: phase-0, samples, core
- dépend de: —
- références: plan_teensy.md#T6 ; platform/desktop/sample_loader.*
- terminé quand: une fonction lit seulement l'en-tête (format, canaux, fréquence, nombre d'échantillons) sans charger les données ; testée sur les formats déjà gérés.

### B-021 Lecteur WAV par morceaux
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-0, samples, core
- dépend de: B-020
- références: plan_teensy.md#T6
- terminé quand: interface commune de lecture par morceaux (sans allocation), implémentée sur le Mac, avec test de résultat identique au chargement complet.

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
- statut: todo · priorité: 2 · propriétaire: utilisateur · étiquettes: phase-0, outillage
- dépend de: —
- références: plan_teensy.md#T11
- terminé quand: `pio --version` répond. Confirmation de l'utilisateur avant `brew install platformio` ; le premier téléchargement (plateforme Teensy, Teensyduino 1.62, compilateur ARM) va dans `~/.platformio`.

### B-051 `platformio.ini` et compilation de `core/` pour le Cortex-M7
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-0, outillage, core
- dépend de: B-050
- références: plan_teensy.md#T11
- terminé quand: carte `teensy41`, `build_src_filter` sur `core/` et `platform/teensy/`, options `-std=gnu++17 -fno-exceptions -fno-rtti -O2 -Wdouble-promotion` ; un programme minimal qui inclut `core/` compile sans erreur ; surprises de portabilité corrigées.

### B-052 Remplacer le `%f` du gain en dB
- statut: done · priorité: 3 · propriétaire: claude · étiquettes: phase-0, core
- dépend de: —
- références: plan_teensy.md#T11 ; core/ui/ui_controller.cpp
- terminé quand: l'affichage du gain en dB n'utilise plus de format flottant, avec le même rendu ; tests d'interface inchangés.
- vérifié: `snprintf("%+ddB", lround(db))` dans `core/ui/ui_controller.cpp` ; plus aucun format flottant dans `core/` hors `profile_codec` (JSON, PC seulement) ; build propre, 24 tests sur 24 passent. Prolongement hors backlog (demande de l'utilisateur) : pas du gain à 0,5 dB, affichage `+0.5dB`.

### B-053 Taille du code et de la mémoire sur la Teensy
- statut: todo · priorité: 2 · propriétaire: claude · étiquettes: phase-0, outillage
- dépend de: B-051
- références: plan_teensy.md#T11, #T6
- terminé quand: la sortie de l'éditeur de liens est relevée (code, RAM1, RAM2, PSRAM) et comparée aux 61 Ko du profil, aux 68 Ko de l'interface et aux 19 Ko du moteur.

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
- dépend de: B-001, B-051
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
