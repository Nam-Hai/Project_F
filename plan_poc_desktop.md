# Plan POC Desktop : Architecture & Marche à suivre

Objectif : un petit exécutable PC qui joue en boucle un kick suivant un pattern initial défini par des constantes dans le code, avec une architecture réutilisable telle quelle sur Teensy 4.1. La vraie source de commandes sera l'UI : sur Teensy l'interface physique, sur PC une petite UI logicielle de simulation (en attendant le hardware).

## Principe d'architecture
Le cœur du projet est une **bibliothèque portable** qui ne connaît ni l'OS ni le matériel. Elle expose un seul point d'entrée audio, `render(float* out, int frames)`. Autour, de fines couches de plateforme (PC aujourd'hui, Teensy plus tard) l'appellent et lui envoient des commandes.

```
        SOURCES DE COMMANDES                    PLATEFORME AUDIO
  ┌────────────────────────────┐          ┌──────────────────────────┐
  │ DefaultPattern (PC, POC)   │          │ desktop: miniaudio       │
  │ SimUi (PC, UI simulée)     │          │ teensy : I2S + DMA       │
  │ HardwareUi (Teensy, futur) │          └────────────┬─────────────┘
  └─────────────┬──────────────┘                       │ callback(buf, N)
                │ Command (queue SPSC sans lock)       │
                ▼                                      ▼
        ┌───────────────────────── core (portable) ─────────────────┐
        │ Engine::render() → Sequencer → Voices → Mixer → softclip  │
        └──────────────────────────────┬─────────────────────────────┘
                                       │ EngineState (pas courant, BPM, play/stop)
                                       ▼
                              lu par l'UI (affichage, LEDs)
```

## Décisions clés
1. **Séquenceur piloté par le compteur d'échantillons**, pas par un timer.
   - À chaque bloc, il calcule à quel échantillon tombe le prochain pas (position fractionnaire pour éviter toute dérive).
   - Résultat identique sur PC et Teensy, sans jitter, sans dérive par rapport à l'horloge du DAC, testable hors ligne.
   - Un trig doit tomber à l'échantillon exact dans un bloc de 128 : le rendu est découpé autour du trig, ou un offset est passé à la voix.
   - Remplace l'`IntervalTimer` de la feuille de route. Le timer reste utile sur Teensy pour la MIDI clock / synchro externe et les tâches UI.
2. **Le core ignore d'où viennent les commandes.**
   - Pattern initial, UI simulée et UI matérielle produisent les mêmes `Command`.
   - Le core ne change pas quand on passe du PC au Teensy.
3. **Communication thread principal -> thread audio par une file SPSC.**
   - Commandes fines pensées pour l'UI : `ToggleStep(track, step)`, `SetStep`, `SetBpm`, `SetTrackGain`, `Play/Stop`, `Trigger(track)`.
   - Même modèle que sur Teensy (`loop()` alimente le callback d'interruption).
   - Les commandes sont prises en compte au prochain bloc (latence max 2,9 ms).
4. **`EngineState` en retour vers l'UI** (pas courant, BPM, play/stop), mis à jour par le thread audio et lu par l'UI (champs atomiques ou seconde file).
5. **Pas d'allocation dans `render()`.**
   - Voix, buffers et patterns sont des tableaux de taille fixe (`config.h` : `MAX_TRACKS`, `MAX_STEPS`, `BLOCK`).
   - Pas d'exceptions ni de RTTI, STL minimal dans le core, pour rester compatible Teensy.
6. **Samples chargés avant le démarrage audio** (hors callback). Le POC fait un kick synthétique, mais `Voice` est une interface commune pour que le sample player s'y branche ensuite.
7. **Audio PC : miniaudio** (un seul header, à copier dans `third_party/`). Modèle callback comme le futur driver I2S. Alternative : PortAudio (plus lourd).
8. **Build : CMake** pour le PC. Le core reste compilable sous PlatformIO/Teensy (`library.json` plus tard).
9. **Conventions de la Teensy Audio Library respectées dès le POC PC** (on utilisera son driver `AudioOutputI2S` pour la sortie I2S, notre DSP reste maison) :
   - Blocs de **128 échantillons** (`AUDIO_BLOCK_SAMPLES`), soit un budget de 2,9 ms par bloc à 44,1 kHz. Le callback miniaudio reçoit des tailles variables, donc un petit adaptateur découpe en blocs de 128.
   - `Engine::render` produit du **`float` stéréo**. La conversion en `int16` (attendu par `AudioOutputI2S`) est faite par la couche plateforme, sur PC comme sur Teensy.
   - **Adaptation Teensy prévue :** une classe héritant d'`AudioStream` appelle `engine.render()` dans `update()`, convertit en `int16` et transmet les deux blocs (gauche/droite) à `AudioOutputI2S`. La file de commandes ne change pas (`loop()` écrit, `update()` lit).
10. **Pas de fichier de pattern (ni JSON ni texte).** Le pattern initial est un fichier de constantes éditable (`default_pattern.h`), envoyé au moteur par la file de commandes au démarrage. Changer le pattern = recompiler (quelques secondes), jusqu'à l'arrivée de l'UI de simulation.

## Arborescence proposée
```
drum_machine/
├── CMakeLists.txt
├── config.h                    # constantes (SR, BLOCK, MAX_*)
├── core/                       # PORTABLE, aucun include OS
│   ├── engine.h/.cpp           # render(), possède séquenceur/voix/mixeur
│   ├── sequencer.h/.cpp        # horloge en échantillons, Pattern, pas courant
│   ├── command.h               # struct Command + SpscQueue<Command,N>
│   ├── engine_state.h          # état lu par l'UI
│   ├── voice.h                 # interface: trigger(vel), render(out,n)
│   ├── synth_kick.h/.cpp
│   ├── sample_player.h/.cpp    # lecteur de samples (pitch, interpolation linéaire)
│   ├── mixer.h/.cpp            # somme + gain + tanh
│   └── dsp_utils.h
├── platform/
│   ├── desktop/
│   │   ├── main.cpp            # charge la loop, lance l'audio (exécutable drum_machine)
│   │   ├── audio_miniaudio.*   # callback -> engine.render()
│   │   ├── default_pattern.h   # LOOP INITIALE : BPM, samples, pitchs, patterns (à éditer)
│   │   ├── sample_loader.*     # WAV -> RAM
│   │   ├── wav_writer.*        # rendu hors ligne en WAV
│   │   ├── tone_test.cpp       # sinus 440 Hz (test de la sortie audio)
│   │   ├── render_wav.cpp      # kick en boucle -> kick_test.wav
│   │   ├── sample_demo.cpp     # un sample à pitch 1.0 / 0.5 / 2.0 -> sample_test.wav
│   │   └── sim_ui.*            # UI de simulation (étape 7, pas encore fait)
│   └── teensy/                 # vide pour l'instant (audio_i2s, ui...)
├── samples/                    # dépose tes .wav ici (mono, 16 bits, 44,1 kHz)
├── tests/                      # test_kick, test_sample, test_sequencer (ctest)
└── third_party/miniaudio.h
```

## Loop initiale (`platform/desktop/default_pattern.h`)
Une ligne par piste (16 max) : sample, pitch, gain, pattern de 16 pas.
```cpp
constexpr float DEFAULT_BPM = 120.0f;

constexpr TrackSetup DEFAULT_TRACKS[] = {
    //  sample          pitch  gain   pattern (16 pas)
    {"synth:kick",      1.0f,  1.0f,  "x...x...x...x..."},
    // {"snare.wav",    1.0f,  0.8f,  "....x.......x..."},
};
```
- `sample` : `"synth:kick"` (kick synthétisé) ou le nom d'un `.wav` du dossier `samples/`.
- Pattern : `x` = trig, `.` = silence, complété par du silence s'il est plus court que 16 pas.
- `main.cpp` charge les samples puis envoie tout au moteur en `Command` (`SetBpm`, `SetStep`, `SetTrackGain`, `Play`). Même chemin que l'UI.
- Modifier puis recompiler : `cmake --build build && ./build/drum_machine`.

## État actuel
Le POC PC est terminé et largement dépassé. Ce qui est fait :
* **Moteur portable** (`core/`) : lecteur de samples (pitch par interpolation linéaire), mixeur `float` avec soft-clipping, séquenceur à l'horloge d'échantillons (trigs à l'échantillon près), file de commandes sans verrou, état du moteur lu par l'UI, vélocité et pitch par pas. Synthé de kick conservé mais **en sommeil**.
* **Sortie audio PC** (miniaudio) et chargement de samples WAV (PCM 16 bits, mono, 44,1 kHz).
* **Interface complète sur l'émulateur** (Dear ImGui + SDL2, deux fenêtres : écran OLED et panneau de contrôles simulés) : voir `plan_ui.md`.
* **Sauvegarde des loops** (profil JSON) : voir `plan_loops.md`.
* **Vélocité et pitch par pas** (modes Edit, Velocity, Pitch) : voir `plan_step_params.md`.

Planifié, pas encore commencé :
* **Clavier de piano 24 + 2 touches** : `plan_keyboard.md`.
* **Micro-timing, roulements et enregistrement à la main** : `plan_record.md`.
* **Adaptation Teensy** : classe `AudioStream` + `AudioOutputI2S`, chargeur SD, lecture des vrais boutons / encodeur / potard, pilote d'écran SSD1306 (en attente du matériel).

## Limites actuelles
* Pas de swing ; pas de décalage fin des notes (tout est sur la grille de 16 pas) : voir `plan_record.md`.
* Un seul profil, pas de renommage des loops, pas de reprise automatique de session : voir `plan_loops.md`.
* Formats de samples : aucune conversion automatique (stéréo, autre fréquence ou autre profondeur refusés).
* Bus de pré-écoute sur une seconde sortie audio : reporté (voir `plan_ui.md`).

## Points ouverts
* Valider à l'écoute le son de la loop et la latence de la machine de développement (utile pour la compensation de latence de l'enregistrement).
* Adaptation Teensy, en attente du matériel.
