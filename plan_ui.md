# Plan UI : écran réel + émulateur de matériel

Objectif : une interface utilisable sur le Teensy (petit écran OLED 128x64 monochrome, boutons, roue, potard), développée d'abord sur PC avec un émulateur. Le design visuel sera retravaillé plus tard : le but du POC est de valider le fonctionnement général.

## 1. Les deux « UI »
| | UI réelle (« écran ») | UI simulée (« matériel ») |
|---|---|---|
| Rôle | Ce qui s'affiche sur l'OLED du Teensy | Faux panneau de contrôles (boutons, roue, potard) |
| Existe sur | PC **et** Teensy (même code) | PC uniquement, jetable |
| Code | `core/ui/` (portable, sans allocation) | `platform/desktop/` (ImGui + SDL2) |
| Produit / consomme | Consomme des `InputEvent`, dessine dans un `Canvas` | Produit des `InputEvent` |

Les entrées (roue, boutons, potard) font partie de l'UI simulée : sur Teensy, les vrais composants produiront les mêmes `InputEvent`.

## 2. Contrôles émulés
| Contrôle | Événement | Remarque |
|---|---|---|
| Roue | rotation (delta +/-) | défilement des listes |
| Bouton **Valider** | appui | entrer dans une vue, valider une action |
| Bouton **Back** | appui | remonter d'une vue |
| Bouton **Menu** | appui | ouvre le menu contextuel de la vue courante (menu de la piste dans Pistes et Pattern) |
| Bouton **Play/Stop** | appui | transport |
| Potard **BPM** | valeur 0..1 | tempo |
| 16 boutons de pas | appui sur le pas 0..15 | bascule le pas dans la vue Pattern |

Les boutons de pas ont une LED dans l'émulateur (état du pas, pas courant), mais **le premier hardware n'en aura pas** : l'écran doit donc rester le retour principal.

## 3. Vues de l'écran (128x64, police 5x7, 8 lignes de 21 caractères)
Toutes les vues ont une **ligne d'en-tête** : titre à gauche, indicateur en haut à droite avec le **BPM** et l'état **lecture / pause**.

Navigation :
```
 Pistes ──Valider sur une piste──► Pattern ──Back──► Pistes
   │  │                              │
   │  └─Menu sur une piste──┐        └─Menu──┐
   │                        ▼                ▼
   │                  Menu de la piste (Changer le sample / Supprimer / Annuler)
   │                   Back ou Annuler : retour à la vue d'où il a été ouvert (Pistes ou Pattern)
   │                   Changer le sample ─► Ajout (mode remplacement) ─► retour à la vue d'origine
   └──Valider sur « + Ajouter »──► Ajout ──Valider sur un sample──► Pattern (nouvelle piste vide)
                                     └─Back──► Pistes
```
Le bouton **Menu** est contextuel : sur une piste de la liste ou dans le pattern, il ouvre le menu de la piste ; sur « + Ajouter piste » et dans la vue Ajout, il ne fait rien (pour l'instant) ; rappuyé dans le menu, il le referme. Valider dans le pattern ne fait rien.

### Vue Pistes (écran d'accueil)
```
Pistes            120 >
> Kick1
  Snare1
  Hat1
  + Ajouter piste
```
* Une ligne par piste active, nommée d'après son sample, puis **« + Ajouter piste »** en dernière ligne.
* La roue déplace le curseur ; la liste défile si elle dépasse l'écran (pas de limite à 8 côté UI).
* Un petit indicateur par piste clignote quand elle joue.
* Valider : ouvre la vue Pattern de la piste, ou la vue Ajout sur la dernière ligne.

### Vue Pattern (piste sélectionnée)
```
Kick1             120 >
[#][ ][ ][ ][#][ ][ ][ ] ...   16 pas, 8 px chacun
 ^ pas courant
```
* 16 pas sur 128 px = 8 px par pas : la rangée s'aligne sur les 16 boutons physiques.
* Un appui sur le bouton de pas *i* : sélectionne le pas *i* (curseur) **et** bascule son état oui / non.
* Le pas en cours de lecture est repéré visuellement.
* Pour l'instant : uniquement « joué ou non » (vélocité et pitch viendront plus tard, ce sera le rôle du pas sélectionné).
* Valider : menu de la piste (Supprimer / Changer le sample / Annuler). *À confirmer.*
* Back : retour à la liste des pistes.

### Vue Ajout
```
Ajouter           120 >
> Kick1
  Kick2
  Snare1
  Hat1
```
* Liste des samples disponibles (dossier `samples/` sur PC, carte SD sur Teensy), défilement à la roue.
* Valider : crée une piste avec ce sample, pattern vide, et ouvre la vue Pattern.
* Écoute du sample en le survolant : *à confirmer* (utile pour choisir, peut gêner en défilant).
* Back : retour à la liste des pistes.

## 4. Architecture
```
 FakeHardwareView (ImGui) ──InputEvent──►  UiController  ◄── EngineState
 ScreenView (ImGui)       ◄─ Canvas ───   (core/ui, portable) ──Command──► Engine
```
* **`InputEvent`** : `Wheel(delta)`, `ButtonOk`, `ButtonBack`, `ButtonPlay`, `Step(i)`, `PotBpm(valeur)`.
* **`UiController`** (portable, tableaux de taille fixe, pas de `std::string` / `std::vector`) :
  * `handle(InputEvent)` : met à jour la vue courante et envoie des `Command` via une interface `CommandSink`.
  * `update(const EngineState&)` : lit le pas courant, les triggers par piste, le BPM.
  * `render(Canvas&)` : dessine la vue courante.
  * Possède sa **propre copie du modèle** (liste des pistes, nom du sample, pattern 16 bits par piste) : l'UI est la seule à modifier les patterns, donc pas besoin de lire l'état du thread audio.
* **`Canvas`** : framebuffer 128x64 en 1 bit, **même disposition mémoire que le SSD1306** (8 pages de 128 octets, 1024 octets), primitives `clear`, `set_pixel`, `hline`, `rect`, `text` et police bitmap 5x7. Sur Teensy : un pilote SSD1306 envoie ce buffer (I2C/SPI) dans `loop()`, pas dans le callback audio.
* **Vues PC** : `ScreenView` affiche le `Canvas` en texture agrandie, `FakeHardwareView` fournit les widgets ; elles ne se connaissent pas, un petit hôte (`emulator.cpp`) les assemble.
* **Dépendances PC** : SDL2 (`brew install sdl2`) et Dear ImGui (sources copiées dans `third_party/`, avec les backends SDL2). Ni l'un ni l'autre n'est jamais embarqué sur Teensy.

**Règle pour l'émulateur :** tout slider doit être pilotable à la molette de la souris par survol. Utiliser `SliderFloatWheel` / `SliderIntWheel` de `platform/desktop/wheel_slider.h`, jamais `ImGui::SliderFloat` / `SliderInt` directement.

## 5. Prérequis côté moteur (« live ») : FAIT
* `Command::SetTrackPitch`, `Command::SetTrackSample(piste, pointeur, longueur)` : le fichier est chargé dans le thread principal, le callback ne fait que brancher le pointeur sur le `SamplePlayer` de la piste (le moteur en possède un par piste). Une piste est « ajoutée » en lui donnant un sample, et l'UI choisit l'index libre.
* `Command::ClearTrack` : supprime la piste (voix, pattern, gain, pitch).
* **Propriété des buffers** (à la place du pool prévu) : un pointeur donné au moteur reste valide jusqu'à ce que le moteur le rende par `Engine::pop_released()` (remplacement ou suppression). Une seconde file SPSC audio -> UI porte ces pointeurs, sans compteur d'acquittement ni course possible. L'UI doit la vider régulièrement (64 entrées).
* **`SampleBank`** (PC, `platform/desktop/sample_bank.*`) : possède les buffers chargés ; `load()` puis, quand le moteur les rend, `release()`. Sur Teensy, l'équivalent sera un tampon en RAM2 / PSRAM.
* **`EngineState::triggers[piste]`** : compteur de trigs par piste (indicateur de jeu dans la liste).
* **`SampleLibrary`** (plateforme) : lister les `.wav` disponibles (disque sur PC, SD sur Teensy) : à faire avec la vue Ajout (étape 5).
* **`MAX_TRACKS`** : 16 côté moteur (constante de `config.h`, à augmenter si besoin). Le vrai plafond sur Teensy sera la RAM disponible pour les samples (PSRAM en option), pas le nombre de pistes.

## 6. Étapes
1. **Moteur live : FAIT.** `SetTrackSample`, `SetTrackPitch`, `ClearTrack`, restitution des buffers (`pop_released`), compteur de trigs par piste, `SampleBank`. Testé sans UI (`test_live`, `test_sample`). `main.cpp` charge maintenant la loop par ces mêmes commandes.
2. **`Canvas` + police 5x7 : FAIT** (`core/ui/canvas.*`, `core/ui/font5x7.h`). Buffer de 1024 octets au format SSD1306, primitives `clear`, `set_pixel`, `hline`, `vline`, `rect`, `invert_rect`, `text` (opaque, option inversée pour la ligne sélectionnée), `text_width` (alignement à droite pour l'indicateur BPM). Clipping aux bords. Testé par comparaison à un rendu ASCII de référence (`test_canvas`) et vérifié à l'oeil sur les 95 caractères.
2b. **Fenêtre d'aperçu de l'écran : FAIT** (`canvas_window`, Dear ImGui v1.92.9 copié dans `third_party/imgui/` + SDL2 installé via Homebrew). Affiche le `Canvas` agrandi (zoom réglable, pixels nets, couleur OLED) et un panneau pour taper 8 lignes de texte (texte, partie alignée à droite, case inversé). Premier jalon de l'émulateur (`ScreenView`) ; le panneau de contrôles simulés viendra à l'étape 4. Outil terminal `canvas_preview` aussi disponible (jetable). La cible `canvas_window` n'est compilée que si SDL2 est trouvé (`find_package(SDL2)`).
3. **`InputEvent` + `UiController` : FAIT** (`core/ui/input_event.h`, `core/ui/ui_controller.*`, interface `CommandSink` dans `core/command.h`, implémentée par `Engine`). Vues Pistes et Pattern, en-tête BPM + lecture/pause, potard BPM (60 à 200), Play/Stop, indicateur de jeu par piste (120 ms), pas en cours de lecture. `add_track()` sert à déclarer les pistes de la loop initiale. « Ajouter piste » ne fait rien pour l'instant (vue Ajout = étape 5). Testé sans fenêtre avec un faux moteur qui enregistre les commandes (`test_ui`). Version d'origine du texte de l'étape : vue Pistes, vue Pattern, en-tête BPM / lecture. Tests sans fenêtre : on envoie des événements, on vérifie les `Command` produites et l'image du `Canvas`.
4. **Émulateur ImGui + SDL2 : FAIT** (`./build/emulator`). Drum machine complète : moteur + audio + `UiController` + deux vues dans **deux fenêtres SDL séparées**, chacune avec son contexte ImGui (`ScreenView` : Canvas agrandi, zoom entier automatique, aucune entrée ; `FakeHardwareView` : roue = slider rotatif commandé à la molette de la souris ou aux flèches, Valider, Back, Play/Stop, potard BPM, 16 boutons de pas avec LED simulées, raccourcis clavier). La loop de `default_pattern.h` est chargée par `default_loop.*`, partagé avec `drum_machine`. Modification du pattern en direct possible. Texte d'origine de l'étape : `ScreenView`, `FakeHardwareView` (roue, Valider, Back, Play/Stop, potard BPM, 16 boutons avec LED). Première modification de la loop en direct.
5. **Vue Ajout + `SampleLibrary` : FAIT.** Interface `SampleLibrary` (`core/ui/sample_library.h` : `count`, `name`, `load`, `discard`) et implémentation PC `DesktopSampleLibrary` (liste les WAV du dossier `samples/` au bon format, triés sans tenir compte de la casse, ignore les autres en le signalant sur stderr). « + Ajouter piste » ouvre la vue Ajout (roue = parcourir, Valider = créer, Back = annuler) ; la piste prend le plus petit index moteur libre, le sample est chargé hors audio puis envoyé par `SetTrackSample`, et la vue Pattern de la nouvelle piste s'ouvre vide. Échec de chargement ou file pleine : message « Echec du chargement » pendant 2 s, buffer rendu à la bibliothèque. Testé (`test_ui`, `test_sample`). Texte d'origine : liste des samples, ajout d'une piste en direct.
6. **Menu piste + écoute : FAIT.** Le bouton **Menu** (depuis la liste des pistes ou depuis le pattern) ouvre le menu de la piste (Changer le sample / Supprimer la piste / Annuler ; Back ou Menu de nouveau = retour à la vue d'origine) : plus besoin d'entrer dans la vue Pattern. Suppression avec confirmation (« Confirmer ? », annulée si on bouge la roue ou fait Back) : `ClearTrack`, la liste se décale. « Changer le sample » rouvre la vue Ajout en mode remplacement (titre « Changer sample », pattern conservé, Back ou la validation reviennent à la vue d'origine). **Écoute** : dans la vue Ajout, le sample survolé est joué 150 ms après l'arrêt de la roue (jamais à l'ouverture, seul le dernier sample survolé est joué), coupée par Back ou Valider ; moteur : voix d'écoute hors pistes + `Command::PreviewSample` / `StopPreview`, buffers rendus par `pop_released`. Testé (`test_ui`, `test_live`). Texte d'origine : suppression, changement de sample ; écoute des samples (si confirmé).
7. **EN STANDBY : Teensy** (en attente du matériel). On passe d'abord par plusieurs tours de retouche de l'UI sur l'émulateur (voir « Tours de retouche » ci-dessous). Texte d'origine : lecture des vrais boutons (anti-rebond), roue, potard (filtrage), pilote SSD1306.

## 7. Points à confirmer
* Écoute d'un sample quand on le survole dans la vue Ajout ?
* Potard BPM : plage visée (par exemple 60 à 200) ; au démarrage, le BPM de `default_pattern.h` s'applique tant que le potard n'a pas bougé (évite un saut de tempo).

## 8. Tours de retouche de l'UI (en cours)
Les étapes 1 à 6 donnent une UI complète mais brute. On la retravaille en plusieurs tours, sur l'émulateur, avant de passer au Teensy (étape 7, en standby). Chaque tour : retours d'usage de l'utilisateur, puis modifications testées.

Pistes connues, pas encore traitées :
* Réécoute du sample sélectionné dans la vue Ajout (aujourd'hui : bouger la roue puis revenir dessus).
* Aucun repère de défilement dans les listes (flèches haut/bas quand il y a des éléments hors écran).
* Aucune aide à l'écran sur les contrôles (pas d'indication des boutons utiles dans chaque vue).
* Le cache des samples d'écoute, nécessaire sur Teensy (lecture SD plus lente).

### Tour 1
* **Fait : bouton Menu contextuel.** Le menu de la piste (changer le sample / supprimer) s'ouvre directement depuis la liste des pistes ; Valider dans le pattern n'ouvre plus de menu. Émulateur : bouton « Menu » et touche M.
* À remplir avec les autres retours.
