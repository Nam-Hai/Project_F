# Fiche Technique Matérielle : Hardware & Interfaçage

## 1. Composants Principaux

### Microcontrôleur : PJRC Teensy 4.1
* **Processeur :** NXP i.MX RT1062 (ARM Cortex-M7 à $600\text{ MHz}$).
* **Unité Flottante (FPU) :** VFPv5 intégrée (calculs $float32$ en un seul cycle d'horloge).
* **Mémoire interne :** $1\text{ Mo}$ RAM ($512\text{ Ko}$ DTCM / $512\text{ Ko}$ OCRAM), $8\text{ Mo}$ Flash.
* **Périphériques clés utilisés :** Periphérique I2S/SAI1 (Serial Audio Interface), DMA (Direct Memory Access), Port SDIO (Carte MicroSD).

### Convertisseur Numérique-Analogique (DAC) : PCM5102A
* **Interface :** I2S (Inter-IC Sound) jusqu'à $384\text{ kHz} / 32\text{ bits}$.
* **Rapport Signal/Bruit (SNR) :** $112\text{ dB}$.
* **Niveau de sortie :** $2,1\text{ V}_{\text{RMS}}$ (niveau ligne standard).
* **Génération Master Clock (SCK) :** Horloge interne par PLL (aucun signal MCLK requis si configuré).

---

## 2. Cartographie des Broches (Pinout Diagram)

### Connexion Teensy 4.1 $\leftrightarrow$ DAC PCM5102A

| Signal I2S / DAC | Broche PCM5102A | Broche Teensy 4.1 | Description |
| :--- | :--- | :--- | :--- |
| **VIN / VCC** | VCC / 5V | **VIN (5V USB)** ou **3.3V** | Alimentation logique & analogique |
| **GND** | GND | **GND** | Masse commune |
| **LCK / LRCK** | LRCK / WCLK | **Pin 20 (LRCLK1)** | Horloge de sélection de canal (Word Select) |
| **BCK** | BCK / BCLK | **Pin 21 (BCLK1)** | Horloge de bits (Bit Clock) |
| **DIN** | DIN / DATA | **Pin 7 (OUT1A)** | Données audio numériques série |
| **SCK** | SCK / MCK | **GND** | Mis à la masse (active la PLL interne du PCM5102A) |
| **FLT** | FLT | **GND** | Filtre passe-bas (Normal Low Latency) |
| **DMP** | DEMP | **GND** | Désactive le De-emphasis |
| **XMT** | XSMT | **broche libre de la Teensy** (à choisir) | Mute logiciel : LOW au démarrage, HIGH ~100 ms après le démarrage de l'I2S (HIGH = actif, LOW = mute) ; voir `plan_teensy.md` T9. Pour un premier essai sans code, on peut le relier au 3.3V |
| **FMT** | FMT | **GND** | Format I2S standard |

---

## 3. Schéma de l'Étage de Sortie Analogique (Recommandé)

Afin d'éliminer le bruit de découpage haute fréquence ($>20\text{ kHz}$) et d'isoler la sortie Jack, le circuit d'interface analogique suivant est préconisé :

```
 Sortie DAC          Condensateur           Filtre RC           Amplificateur
 (PCM5102A)            de Liaison           Passe-Bas              Opérationnel
   OUTL/R               C_block              R_f = 1k           (ex: TL072 / NE5532)
     o-------------------||-------------------+-/\/\/-+-----------------+
                         10uF                 |       |                 |
                                             --- C_f  |     +-----+     |
                                             --- 2.2n |     |  -  |-----+-----> Sortie Jack
                                              |       +-----|     |           (Ligne/Casque)
                                             GND            |  +  |
                                                            +-----+
                                                               |
                                                              GND
```

### Spécifications de l'étage analogique :
* **Fréquence de coupure du filtre passe-bas :**
  $$f_c = \frac{1}{2\pi \cdot R_f \cdot C_f} = \frac{1}{2\pi \cdot 1000 \cdot 2,2 \times 10^{-9}} \approx 72,3\text{ kHz}$$
* **AOP recommandé :** Texas Instruments **TL072** (Audio classique) ou **NE5532** (faible bruit, forte capacité de courant pour casque).

---

## 4. Nomenclature des Composants (BOM - Bill of Materials)

| Composant | Référence / Valeur | Quantité | Rôle |
| :--- | :--- | :--- | :--- |
| Microcontrôleur | Teensy 4.1 (avec headers) | 1 | Processeur principal & moteur DSP |
| DAC I2S | Module Breakout PCM5102A | 1 | Conversion Numérique -> Analogique |
| Puce Mémoire **(retenue)** | PSRAM QSPI : carte vendue avec les puces soudées, **référence mise de côté : Protosupplies « Teensy 4.1 Fully Loaded », 16 Mo de PSRAM** (variante avec 256 Mo de flash NAND en option, voir plan_teensy.md T17) | 1 à 2 puces (8 à 16 Mo) | Mémoire vive pour les samples WAV, notamment **stéréo** (deux fois plus lourds que le mono) : la RAM2 (512 Ko) ne suffit pas pour des 808 longs. Pas de conflit avec la carte microSD (lecteur de l'autre côté de la carte, autre bus). |
| Potentiomètre | $10\text{ k}\Omega$ Logarithmique | 1 | Contrôle du volume général |
| Condensateurs | $10\ \mu\text{F}$ Electrolytique / $2,2\text{ nF}$ Céramique | 2 de chaque | Découplage DC et filtrage passe-bas |
| Résistances | $1\text{ k}\Omega$, $10\text{ k}\Omega$, $100\text{ k}\Omega$ | 4 à 8 | Atténuation et polarisation AOP |
| Connecteurs | Embase Jack $6.35\text{ mm}$ ou $3.5\text{ mm}$ Stéréo | 1 | Sortie audio physique |
| Carte MicroSD | Class 10 (16 Go ou 32 Go) | 1 | Stockage des kits de percussions WAV et du profil de loops |
| Écran | OLED monochrome 128x64 (SSD1306, I2C ou SPI) | 1 | Affichage de l'interface |
| Encodeur rotatif | Encodeur incrémental (« roue », avec ou sans poussoir) | 1 | Navigation dans les listes, réglage des valeurs |
| Boutons de pas / touches blanches | Boutons-poussoirs alignés | 16 | 16 pas du pattern ; touches blanches du clavier de piano |
| Boutons de touches noires | Boutons-poussoirs, placés au-dessus des blancs et décalés | 10 | Touches noires du clavier de piano (la **10e** passe en mode Clavier / Rec) ; dans la vue Pattern, les touches 1 à 4, **tenues**, règlent Gain, Quantifier, Wide (largeur stéréo) et Balance de la piste, et les touches 5 à 8 l'enveloppe ADSR de la piste (le firmware doit lire l'appui et le relâchement) |
| Boutons de commande | Boutons-poussoirs | 5 | Valider, Back, Menu, Mode, Play/Stop |
| Potentiomètre BPM | $10\text{ k}\Omega$ linéaire | 1 | Tempo |

---

## 5. Interface utilisateur : contrôles prévus

| Contrôle | Quantité | Rôle |
| :--- | :--- | :--- |
| Roue (encodeur rotatif) | 1 | Défilement des listes, réglage des valeurs |
| Boutons Valider, Back, Menu, Mode, Play/Stop | 5 | Navigation et transport |
| Potentiomètre BPM | 1 | Tempo |
| Boutons de pas | 16 | Pattern ; **touches blanches** du clavier (les 14 premières jouent, les 2 dernières sont inactives en mode clavier) |
| Boutons de touches noires | 10 | Clavier ; la **première** sert aussi à entrer dans le mode clavier |

* **Total : 31 boutons-poussoirs**, une roue, un potard, un écran OLED. Les boutons de pas n'ont **pas de LED** dans la première version (le retour passe par l'écran).
* **Disposition du clavier** : les 10 boutons noirs sont au-dessus des 16 boutons de pas, décalés entre deux blanches comme sur un piano (voir `plan_keyboard.md`).
* **Câblage** : 31 boutons, c'est assez pour envisager soit un câblage direct, soit une **matrice de boutons** ; le choix, avec le décompte des broches libres de la Teensy 4.1 (I2S, écran, encodeur, potard, SD déjà pris), est à faire lors de la conception.
* **Étiquettes** : des inscriptions près des touches noires de réglage (« Gain » : touche 1 ; « Quant », « Wide », « Bal » : touches 2 à 4 ; « A », « D », « S », « R » : touches 5 à 8) et de la 10e (« Rec ») sont utiles, puisque ces raccourcis n'ont pas de ligne de menu.
