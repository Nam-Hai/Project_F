# Feuille de Route Technique & Architecture Logicielle

## 1. Architecture Logicielle Globale

L'architecture est découpée en trois couches strictes afin de garantir le respect des contraintes temps réel.

```
       +-------------------------------------------------------+
       |               COUCHE APPLICATIVE (UI & SD)            |
       |  - Gestion des boutons / encodeurs / écran OLED       |
       |  - Lecteur de fichiers WAV sur carte SD -> RAM        |
       +-------------------------------------------------------+
                                  |
                                  v  (Écriture dans structures de contrôle)
       +-------------------------------------------------------+
       |               COUCHE SÉQUENCEUR (TIMERS)              |
       |  - IntervalTimer @ BPM dynamique                      |
       |  - Déclenchement des trigs (Kick, Snare, Hats, Sample) |
       +-------------------------------------------------------+
                                  |
                                  v  (Appels de fonctions Trigger)
       +-------------------------------------------------------+
       |               COUCHE MOTEUR AUDIO (DSP)               |
       |  - I2S + DMA Callback (Double Buffer)                 |
       |  - Oscillateurs, Enveloppes, Filtres, Interpolation   |
       |  - Mixeur Float32 + Soft-Clipping (tanh)              |
       +-------------------------------------------------------+
```

---

## 2. Modélisation Mathématique des Synthétiseurs Percussifs

### A. Synthétiseur de Kick (Grosse Caisse)
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

---

### B. Synthétiseur de Snare (Caisse Claire)
La caisse claire est la combinaison du timbre du fût (oscillateur sinusoïdal) et du timbre du timbre métallique sous le fût (bruit blanc filtré).

$$y_{\text{snare}}[n] = w_{\text{body}} \cdot (A_{\text{body}}[n] \cdot \sin(\theta[n])) + w_{\text{noise}} \cdot (A_{\text{noise}}[n] \cdot \text{Filter}_{\text{BP}}(\text{Noise}[n]))$$

* **Bruit blanc :** Générateur congruentiel linéaire rapide :
  $$\text{Noise}[n] = 2.0 \cdot \left( \frac{\text{rand()}}{\text{RAND\_MAX}} \right) - 1.0$$
* **Filtre Passe-Bande (IIR 1er ordre) :**
  $$y_f[n] = b_0 \cdot x[n] - a_1 \cdot y_f[n-1]$$

---

### C. Lecteur de Samples avec Interpolation Liéneaire
Pour modifier le pitch d'un sample stocké dans un tableau $S[k]$ sans aliasing excessif :

1. **Accumulateur de Position Flottante $P[n]$ :**
   $$P[n] = P[n-1] + \text{pitch\_ratio}$$
   $\text{pitch\_ratio} = 1,0$ (vitesse normale), $0,5$ (une octave plus bas), $2,0$ (une octave plus haut).

2. **Interpolation entre deux échantillons :**
   Soit $i = \lfloor P[n] \rfloor$ l'indice entier et $\alpha = P[n] - i$ la partie fractionnaire :
   $$y_{\text{sample}}[n] = S[i] + \alpha \cdot (S[i+1] - S[i])$$

---

### D. Mixage & Saturation Douce (Soft Clipping)
La somme des $M$ voies produit une valeur $X[n] = \sum_{m=1}^{M} y_m[n]$.
Pour éviter le clipping numérique tout en ajoutant une chaleur harmonique :

$$Y[n] = \tanh(X[n] \cdot G_{\text{master}})$$

---

## 3. Structure des Fichiers du Code (Projet C++)

```
drum_machine/
├── drum_machine.ino         # Point d'entrée principal (setup, loop)
├── config.h                 # Paramètres globaux (Sample Rate, Buffer Size, Pins)
├── audio/
│   ├── audio_driver.h/.cpp  # Configuration I2S, DMA, interruptions hardware
│   ├── dsp_utils.h          # Fonctions tanh, générateurs d'ondes, enveloppes
│   ├── voice.h              # Interface abstraite des voies percussives
│   ├── synth_kick.h/.cpp    # Module synthèse de Kick
│   ├── synth_snare.h/.cpp   # Module synthèse de Snare
│   ├── synth_hihat.h/.cpp   # Module synthèse de Hi-Hat (bruit HPF)
│   ├── sample_player.h/.cpp # Lecteur de samples avec interpolation
│   └── mixer.h/.cpp         # Sommation, gain master, soft clipping
├── engine/
│   ├── sequencer.h/.cpp     # Horloge 16 pas, gestion du BPM et des patterns
│   └── sample_loader.h/.cpp # Parsing d'en-tête WAV et chargement SD -> RAM
└── ui/
    ├── controls.h/.cpp      # Lecture des boutons et potentiomètres
    └── display.h/.cpp       # Affichage OLED (SSD1306/ST7789)
```

---

## 4. Feuille de Route d'Implémentation Pas à Pas

### Phase 1 : "Hello World Audio" (Semaine 1)
* [ ] Interfacer la Teensy 4.1 et le module PCM5102A sur breadboard.
* [ ] Configurer les registres I2S et le DMA sous Teensyduino.
* [ ] Écrire le callback audio minimal générant une onde sinusoïdale pure à $440\text{ Hz}$ sans coupure ni bruit d'horloge.

### Phase 2 : Moteur de Synthèse Percussive (Semaines 2 & 3)
* [ ] Coder la classe `SynthKick` (oscillateur sinus + pitch bend exponentiel + enveloppe).
* [ ] Coder la classe `SynthSnare` (mixage sinus + bruit blanc filtré).
* [ ] Coder la classe `SynthHiHat` (bruit blanc + filtre passe-haut $f_c \approx 7\text{ kHz}$).
* [ ] Implémenter la fonction `Trigger()` déclenchant les enveloppes.

### Phase 3 : Lecteur de Samples WAV en RAM (Semaine 4)
* [ ] Écrire un parser léger pour en-têtes WAV ($44,1\text{ kHz}$, $16\text{ bits}$ mono).
* [ ] Charger un fichier `.wav` depuis la carte MicroSD vers un tableau en RAM2 / PSRAM au démarrage.
* [ ] Coder le moteur de lecture avec phase flottante et interpolation linéaire.

### Phase 4 : Mixeur & Traitement Dynamique (Semaine 5)
* [ ] Implémenter le mixeur $N$ voies en $float32$.
* [ ] Ajouter la fonction de saturation douce $\tanh(x)$ en sortie de bus.
* [ ] Valider la charge CPU globale du callback audio (doit rester $< 30\%$ du temps processeur disponible).

### Phase 5 : Séquenceur & Horloge Temps Réel (Semaine 6)
* [ ] Configurer un `IntervalTimer` pour générer les *ticks* de séquenceur réglables en BPM ($60$ à $200\text{ BPM}$).
* [ ] Implémenter une structure de pattern 16 pas pour 4 pistes simultanées.
* [ ] Relier le séquenceur aux triggers des synthétiseurs et du lecteur de sample.

### Phase 6 : Interface Utilisateur & Finitions (Semaines 7+)
* [ ] Ajouter des boutons-poussoirs pour le déclenchement direct et le pas-à-pas.
* [ ] Connecter un écran OLED (SPI/I2C) pour afficher le BPM et le kit actif.
* [ ] Finaliser l'étage de sortie analogique sur PCB ou carte à trous.