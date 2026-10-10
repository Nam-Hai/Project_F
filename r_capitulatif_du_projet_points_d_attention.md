# Récapitulatif du Projet : Drum Machine DIY (Teensy 4.1)

> **Mise à jour.** Ce document est le cahier des charges de départ. Le périmètre a évolué : **drum machine à base de samples** (la synthèse percussive est en sommeil), avec une interface complète (écran OLED, roue, boutons, 16 pas, clavier de piano). L'état actuel et les fonctionnalités prévues sont dans `plan_poc_desktop.md`, `plan_ui.md`, `plan_loops.md`, `plan_step_params.md`, `plan_keyboard.md` et `plan_record.md`.

## 1. Vision et Philosophie du Projet
Le projet consiste à concevoir une **drum machine hybride** en C++ bas niveau sur microcontrôleur **Teensy 4.1** (ARM Cortex-M7 @ $600\text{ MHz}$).

L'objectif est d'implémenter le moteur audio **totalement "from scratch"** (sans dépendre de la *Teensy Audio Library* pour le traitement du signal) afin de maîtriser l'intégralité de la chaîne DSP :
* **Synthèse percussive numérique :** Génération d'ondes (sinus, bruit blanc/rose), enveloppes $AD/AR$, pitch bend exponentiel (Kick, Snare, Hats).
* **Lecteur de samples (WAV) :** Lecture depuis la RAM avec gestion du pitch via interpolation linéaire.
* **Mixage & saturation :** Mixage multi-voies flottant 32 bits (`float`), contrôle de dynamique et *soft-clipping* ($\tanh$).
* **Séquenceur pas-à-pas :** Grille 16 pas pilotée par un timer temps réel à faible jitter.

---

## 2. Points d'Attention Majeurs (Hardware & Software)

### A. Temps Réel Strict et Gestion du Callback Audio
L'audio à $44,1\text{ kHz}$ impose un échantillon toutes les $22,67\ \mu\text{s}$. En travaillant par blocs de $N = 64$ échantillons, le CPU dispose exactement de :
$$t_{\text{block}} = 64 \times 22,67\ \mu\text{s} \approx 1,45\text{ ms}$$
pour calculer l'ensemble des voies audio.

* **Piège classique :** Effectuer une opération bloquante dans l'interruption audio (ex: `SD.open()`, `delay()`, `Serial.print()`, ou allocation dynamique de mémoire `malloc`/`new`).
* **Règle absolue :** La fonction `AudioCallback` ne doit exécuter que des calculs mathématiques purs pré-alloués en mémoire RAM static/global. Tout accès au stockage ou à l'I/O lente doit être réalisé dans la boucle principale (`loop()`) de manière asynchrone.

### B. Topologie Mémoire sur Teensy 4.1
La Teensy 4.1 possède une architecture mémoire spécifique qu'il faut exploiter correctement :
1. **RAM1 (DTCM - 512 Ko) :** Mémoire la plus rapide ($600\text{ MHz}$). Réservée au code critique, aux piles d'exécution et aux buffers de travail du callback audio.
2. **RAM2 (OCRAM - 512 Ko) :** Idéale pour stocker les structures du séquenceur, les tables d'onde et les petits samples système.
3. **EXTMEM (PSRAM optionnelle) :** Possibilité de souder une ou deux puces PSRAM QSPI de $8\text{ Mo}$ sous la carte (jusqu'à $16\text{ Mo}$). Indispensable si tu souhaites charger plusieurs mégaoctets de samples WAV en mémoire vive.

### C. Qualité du Signal Analogique & Bruit Numérique
Un microcontrôleur cadencé à $600\text{ MHz}$ génère un bruit haute fréquence important sur les lignes d'alimentation numérique.
* **Séparation des masses :** Bien séparer la masse numérique ($DGND$) de la masse analogique ($AGND$) du DAC PCM5102A.
* **Filtrage d'alimentation :** Alimenter la section analogique du DAC à travers un filtre $LC$ ou $RC$ passe-bas (ex: $10\ \Omega + 100\ \mu\text{F}$) pour éliminer l'ondulation de la ligne $3,3\text{V}$.
* **Suppression de la composante continue :** Les sorties audio des DACs présentent souvent une tension continue $V_{DC}$. Un condensateur de liaison (DC-block) de $10\ \mu\text{F}$ en série est obligatoire avant tout étage d'amplification ou sortie jack.

### D. Headroom et Marge de Dynamique (Clipping)
Lorsque $M$ voies audio (ex: 8 percussions) jouent simultanément à plein niveau ($1,0$), leur somme peut atteindre $8,0$, ce qui provoque un écrêtage numérique brutal (*hard clipping*) lors de la conversion vers le DAC $16$ ou $24$ bits.
* **Solution :** Travailler tout le traitement interne en virgule flottante (`float`), appliquer un atténuateur de mixage (ex: $1/M$ ou gain variable par voie), puis passer le signal dans une fonction de *soft-clipping* type $y = \tanh(x)$ avant la conversion en entiers signés pour le DAC.

---

## 3. Matériel Minimal pour Démarrer
1. **Teensy 4.1** (avec pins).
2. **Module DAC I2S PCM5102A** (Breakout board classique).
3. **Breadboard + fils de qualité.**
4. **Potentiomètre $10\text{ k}\Omega$** (volume) + **Embase Jack 3.5 mm ou 6.35 mm**.
5. **Câble Micro-USB** (données + alimentation).