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
| Bouton **Play/Stop** | appui | transport |
| Potard **BPM** | valeur 0..1 | tempo |
| 16 boutons de pas | appui sur le pas 0..15 | bascule le pas dans la vue Pattern |

Les boutons de pas ont une LED dans l'émulateur (état du pas, pas courant), mais **le premier hardware n'en aura pas** : l'écran doit donc rester le retour principal.

## 3. Vues de l'écran (128x64, police 5x7, 8 lignes de 21 caractères)
Toutes les vues ont une **ligne d'en-tête** : titre à gauche, indicateur en haut à droite avec le **BPM** et l'état **lecture / pause**.

Navigation :
```
 Pistes ──Valider sur une piste──► Pattern ──Back──► Pistes
   │                                  └─Valider─► Menu piste (Supprimer / Changer le sample / Annuler)
   └──Valider sur « + Ajouter »──► Ajout ──Valider sur un sample──► Pattern (nouvelle piste vide)
                                     └─Back──► Pistes
```

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

## 5. Prérequis côté moteur (« live »)
* `Command::SetTrackPitch` (réservé pour plus tard).
* `Command::SetTrackSample(piste, pointeur, longueur)` : le fichier est chargé dans le thread principal, le callback ne fait que changer le pointeur.
* **Pool de buffers de samples** : on charge dans un buffer libre, on envoie la commande, on ne réutilise l'ancien qu'après acquittement du moteur (compteur dans `EngineState`).
* **Ajout / suppression de piste** : attribution d'un index de piste libre, pattern vidé à la suppression.
* **`SampleLibrary`** (plateforme) : lister et charger les `.wav` (disque sur PC, SD sur Teensy).
* **`EngineState`** : compteur de triggers par piste (indicateur de jeu dans la liste).
* **`MAX_TRACKS`** : 16 côté moteur (constante de `config.h`, à augmenter si besoin). Le vrai plafond sur Teensy sera la RAM disponible pour les samples (PSRAM en option), pas le nombre de pistes.

## 6. Étapes
1. **Moteur live :** `SetTrackSample`, pool de buffers, acquittement, compteur de triggers par piste, ajout / suppression de piste. Tests sans UI.
2. **`Canvas` + police 5x7 :** primitives et test par comparaison à une image de référence (dump ASCII ou `.pbm`).
3. **`InputEvent` + `UiController` :** vue Pistes, vue Pattern, en-tête BPM / lecture. Tests sans fenêtre : on envoie des événements, on vérifie les `Command` produites et l'image du `Canvas`.
4. **Émulateur ImGui + SDL2 :** `ScreenView`, `FakeHardwareView` (roue, Valider, Back, Play/Stop, potard BPM, 16 boutons avec LED). Première modification de la loop en direct.
5. **Vue Ajout + `SampleLibrary` :** liste des samples, ajout d'une piste en direct.
6. **Menu piste :** suppression, changement de sample ; écoute des samples (si confirmé).
7. **Teensy :** lecture des vrais boutons (anti-rebond), roue, potard (filtrage), pilote SSD1306.

## 7. Points à confirmer
* Menu de la piste ouvert par Valider dans la vue Pattern (Supprimer / Changer le sample), ou autre accès ?
* Écoute d'un sample quand on le survole dans la vue Ajout ?
* Potard BPM : plage visée (par exemple 60 à 200) ; au démarrage, le BPM de `default_pattern.h` s'applique tant que le potard n'a pas bougé (évite un saut de tempo).
