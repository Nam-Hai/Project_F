# Feuille de Route Technique & Architecture Logicielle

> **Périmètre actuel : drum machine à base de samples.** La synthèse percussive (kick, snare, hi-hat synthétiques) est **mise en sommeil** : le code du kick existe (`core/synth_kick.*`) mais on ne s'en occupe plus pour l'instant. Voir `plan_poc_desktop.md` pour l'état d'avancement du POC PC.

## 1. Architecture Logicielle Globale

L'architecture est découpée en couches strictes afin de garantir le respect des contraintes temps réel. Le `core/` est portable (PC et Teensy) ; seules les couches « plateforme » changent.

```
       +-------------------------------------------------------+
       |               COUCHE APPLICATIVE (UI & SD)            |
       |  - Entrées : boutons / encodeurs / potards            |
       |  - Écran : vue réelle (sélection de samples, etc.)    |
       |  - Chargement des WAV (SD sur Teensy, disque sur PC)  |
       +-------------------------------------------------------+
                                  |
                                  v  (Commandes via file SPSC)
       +-------------------------------------------------------+
       |               COUCHE SÉQUENCEUR (HORLOGE)             |
       |  - Horloge de pas au compteur d'échantillons          |
       |  - Déclenchement des trigs (une piste = un sample)    |
       +-------------------------------------------------------+
                                  |
                                  v  (Appels de fonctions Trigger)
       +-------------------------------------------------------+
       |               COUCHE MOTEUR AUDIO (DSP)               |
       |  - I2S + DMA (Teensy) / miniaudio (PC)                |
       |  - Lecteur de samples, pitch, interpolation           |
       |  - Mixeur Float32 + Soft-Clipping (tanh)              |
       +-------------------------------------------------------+
```

**Choix d'architecture actés :**
* Séquenceur piloté par le **compteur d'échantillons**, pas par un `IntervalTimer` (pas de jitter, pas de dérive, testable hors ligne). Le timer reste utile sur Teensy pour la MIDI clock et les tâches UI.
* Sortie I2S sur Teensy via **`AudioOutputI2S`** de la Teensy Audio Library (pilote seulement ; le DSP reste maison). Conséquences : blocs de **128** échantillons, sortie `int16` stéréo, 44,1 kHz.
* Communication UI -> moteur par **file de commandes lock-free** ; état du moteur relu par l'UI via `EngineState`.

---

## 2. Modélisation Mathématique

### A. Synthèse percussive (EN SOMMEIL)
Les formules ci-dessous restent valables mais ne sont plus au programme. Seul le kick est implémenté (`SynthKick`).

#### Synthétiseur de Kick (Grosse Caisse)
Un kick analogique/numérique se compose d'un oscillateur sinusoïdal dont la fréquence décroît de manière exponentielle, modulé par une enveloppe d'amplitude exponentielle.

1. **Fréquence instantanée $f(t)$ :**
   $$f(t) = f_{\text{target}} + (f_{\text{start}} - f_{\text{target}}) \cdot e^{-t / \tau_{\text{pitch}}}$$
   * $f_{\text{start}} \approx 150\text{ Hz} \text{ à } 200\text{ Hz}$, $f_{\text{target}} \approx 45\text{ Hz} \text{ à } 55\text{ Hz}$.

2. **Incrément de Phase $\Delta \theta[n]$ :**
   $$\Delta \theta[n] = \frac{2\pi \cdot f[n]}{f_s}$$
   $$\theta[n] = (\theta[n-1] + \Delta \theta[n]) \bmod 2\pi$$

3. **Enveloppe d'Amplitude $A[n]$ :**
   $$A[n] = A[n-1] \cdot \alpha_{\text{decay}} \quad \text{avec } \alpha_{\text{decay}} = e^{-\frac{1}{f_s \cdot \tau_{\text{amp}}}}$$

4. **Échantillon de Sortie :**
   $$y_{\text{kick}}[n] = A[n] \cdot \sin(\theta[n])$$

#### Synthétiseur de Snare (Caisse Claire) : non implémenté
La caisse claire est la combinaison du timbre du fût (oscillateur sinusoïdal) et du timbre métallique sous le fût (bruit blanc filtré).

$$y_{\text{snare}}[n] = w_{\text{body}} \cdot (A_{\text{body}}[n] \cdot \sin(\theta[n])) + w_{\text{noise}} \cdot (A_{\text{noise}}[n] \cdot \text{Filter}_{\text{BP}}(\text{Noise}[n]))$$

* **Bruit blanc :** générateur congruentiel linéaire (ou xorshift) maison, sans `rand()` dans le callback.
* **Filtre passe-bande :** un IIR du 1er ordre ne peut pas être passe-bande ; utiliser un biquad ou deux filtres du 1er ordre en cascade.

---

### B. Lecteur de Samples avec Interpolation Linéaire (IMPLÉMENTÉ)
Pour modifier le pitch d'un sample stocké dans un tableau $S[k]$ sans aliasing excessif :

1. **Accumulateur de Position Flottante $P[n]$ :**
   $$P[n] = P[n-1] + \text{pitch\_ratio}$$
   $\text{pitch\_ratio} = 1,0$ (vitesse normale), $0,5$ (une octave plus bas), $2,0$ (une octave plus haut).

2. **Interpolation entre deux échantillons :**
   Soit $i = \lfloor P[n] \rfloor$ l'indice entier et $\alpha = P[n] - i$ la partie fractionnaire :
   $$y_{\text{sample}}[n] = S[i] + \alpha \cdot (S[i+1] - S[i])$$

   L'implémentation sépare l'index entier et la fraction pour ne pas perdre de précision sur les longs samples.

---

### C. Mixage & Saturation Douce (Soft Clipping) (IMPLÉMENTÉ)
La somme des $M$ voies produit une valeur $X[n] = \sum_{m=1}^{M} y_m[n]$.
Pour éviter le clipping numérique tout en ajoutant une chaleur harmonique :

$$Y[n] = \tanh(X[n] \cdot G_{\text{master}})$$

---

## 3. Structure des Fichiers du Code (Projet C++)

Structure réelle du POC PC (détails dans `plan_poc_desktop.md`) :

```
Project_F/
├── CMakeLists.txt
├── config.h                    # SAMPLE_RATE, BLOCK (128), MAX_TRACKS, MAX_STEPS
├── core/                       # PORTABLE (PC + Teensy), aucun include OS
│   ├── engine.h/.cpp           # render(), commandes, séquenceur + voix + mixeur
│   ├── sequencer.h/.cpp        # horloge au compteur d'échantillons, grille de pas
│   ├── command.h               # Command + SpscQueue
│   ├── engine_state.h          # état lu par l'UI
│   ├── voice.h                 # interface des voies
│   ├── sample_player.h/.cpp    # lecteur de samples (pitch, interpolation)
│   ├── mixer.h/.cpp            # somme, gain master, soft clipping
│   ├── dsp_utils.h
│   ├── synth_kick.h/.cpp       # EN SOMMEIL
│   └── ui/                     # À FAIRE : logique d'UI portable (voir §4, phase 6)
├── platform/
│   ├── desktop/                # PC : miniaudio, chargeur WAV, config de loop, émulateur d'UI
│   └── teensy/                 # à venir : AudioStream + AudioOutputI2S, SD, écran, boutons
├── samples/                    # WAV mono 16 bits 44,1 kHz
├── tests/
└── third_party/miniaudio.h
```

---

## 4. Feuille de Route d'Implémentation Pas à Pas

### Phase 1 : Sortie audio (POC PC) : FAIT
* [x] Sinus 440 Hz via le callback audio sur PC (`tone_test`, miniaudio). *Écoute à valider.*
* [ ] Interfacer la Teensy 4.1 et le module PCM5102A sur breadboard.
* [ ] Brancher `engine.render()` sur `AudioOutputI2S` (classe `AudioStream`, conversion `float` -> `int16`), sans coupure ni bruit d'horloge.

### Phase 2 : Synthèse percussive : EN SOMMEIL
* [x] `SynthKick` (sinus + pitch bend exponentiel + enveloppe), avec test.
* [ ] `SynthSnare`, `SynthHiHat` : hors périmètre pour l'instant.

### Phase 3 : Lecteur de Samples WAV (priorité actuelle)
* [x] Parser WAV (PCM 16 bits ou flottant 32 bits, mono ou stéréo, 44,1 kHz ; refus explicite des autres formats).
* [x] Chargement d'un `.wav` depuis le disque vers un tableau en RAM (PC).
* [x] Moteur de lecture avec phase flottante et interpolation linéaire, pitch par piste.
* [ ] Chargement depuis la carte microSD vers la **PSRAM** (retenue : voir le récapitulatif) ou la RAM2 pour les petits samples, à l'ouverture d'une loop (Teensy) : allocation des tampons de samples à un seul endroit, détection de la PSRAM au démarrage.
* [ ] **Mémoire des samples : sujet à traiter (au plus tard avec le portage Teensy).** Aujourd'hui, ouvrir une loop charge les nouveaux samples **avant** de libérer les anciens (`ClearAll` -> `pop_released` -> `SampleBank::release`) : le pic mémoire est le double d'une loop, et un fichier utilisé par deux pistes est chargé deux fois. Sur Teensy, des allocations et libérations répétées de gros tampons en PSRAM la fragmentent. Pistes : (1) **partager** les tampons identiques entre pistes (un chargement par fichier, comptage de références) : faisable dès maintenant sur PC ; (2) une **arène** en PSRAM (curseur d'allocation remis à zéro à l'ouverture d'une loop, une fois les anciens tampons rendus par le moteur) : plus de fragmentation, pic d'une seule loop, au prix d'un court silence (0,1 à 0,5 s) pendant la lecture de la SD et en vérifiant l'existence des fichiers avant de vider ; l'interface `SampleLibrary` ne change pas. Voir aussi le **tampon du texte JSON** (jusqu'à 216 Ko au pire cas) à écrire / lire par morceaux directement sur la SD, et l'avertissement de `plan_record.md`.
* [ ] Sélection du sample d'une piste depuis l'UI (liste des fichiers disponibles).
* [x] Samples stéréo et flottants lus tels quels (plan_stereo.md, S1 et S2). * [ ] Autre fréquence d'échantillonnage (rééchantillonnage) : hors périmètre pour l'instant.

### Phase 4 : Mixeur & Traitement Dynamique
* [x] Mixeur $N$ voies en $float32$ avec gain par piste.
* [x] Saturation douce $\tanh(x)$ en sortie de bus.
* [ ] Valider le rendu à l'écoute avec de vrais samples (headroom quand plusieurs pistes tombent ensemble).
* [ ] Valider la charge CPU globale du callback audio sur Teensy (doit rester $< 30\%$ du temps disponible).

### Phase 5 : Séquenceur & Horloge Temps Réel : FAIT (PC)
* [x] Horloge de pas au compteur d'échantillons, tempo réglable en BPM ($60$ à $200\text{ BPM}$ et au-delà, borné à $20$-$300$), trigs à l'échantillon près, sans dérive.
* [x] Pattern de 16 pas, 16 pistes.
* [x] Séquenceur relié aux voix (samples) par `Engine::trigger`.
* [x] File de commandes (`SetBpm`, `SetStep`, `ToggleStep`, `SetTrackGain`, `Play`, `Stop`, `Trigger`).
* [ ] Swing, vélocité par pas, longueur de pattern variable.

### Phase 6 : Interface Utilisateur (deux vues) : FAIT sur PC
L'UI est séparée en **logique portable** (`core/ui/`) et **deux vues** sur PC :

* **Vue « écran »** : l'interface qui s'affichera réellement sur le petit écran du Teensy (pistes, pattern, samples, loops, menus). Elle dessine dans une abstraction de framebuffer (`Canvas`, OLED 128x64 monochrome) ; sur Teensy, un pilote SSD1306 enverra le même buffer.
* **Vue « matériel simulé »** (PC uniquement) : un faux panneau avec boutons, roue et potard qui émule le hardware en attendant de le recevoir. Il produit des `InputEvent`.

Flux : `InputEvent` (bouton, encodeur, potard) -> `UiController` -> `Command` -> moteur ; `EngineState` -> `UiController` -> `Canvas`.

* [x] `InputEvent` (roue, Valider, Back, Menu, Mode, Play, pas, potard BPM) et `UiController` portable (pistes, pattern, samples, loops, menus, modes Edit / Velocity / Pitch).
* [x] `Canvas` 128x64 et émulateur PC (Dear ImGui + SDL2). Détails : `plan_ui.md`.
* [x] Sauvegarde des loops dans un profil (format JSON) : `plan_loops.md`.
* [x] Vélocité et pitch par pas : `plan_step_params.md`.
* [ ] Côté Teensy : lecture des boutons / encodeur / potard (anti-rebond), pilote d'écran, mêmes `InputEvent`.
* [ ] Finaliser l'étage de sortie analogique sur PCB ou carte à trous.

### Phase 7 : Clavier de piano (24 + 2 touches) : PLANIFIÉ
Jouer l'échantillon d'une piste à des hauteurs différentes sur un clavier (16 touches blanches = les 16 boutons de pas, 10 touches noires en plus), avec retour sonore immédiat. Entrée par la première touche noire. Voir `plan_keyboard.md`.

### Phase 8 : Jeu « humain » et enregistrement à la main : PLANIFIÉ
* **Partie A, micro-timing** : décalage des notes par rapport à la grille (±50 % d'un pas), roulements (4 notes par pas), force de quantification par piste, mode T. Voir `plan_record.md`.
* **Partie B, enregistrement** : pré-compte, frappes datées par le moteur avec compensation de latence, prise dans la vue Clavier. Dépend des phases 7 et 8A.
