# Plan : préparer le passage sur Teensy 4.1

Le POC est terminé côté fonctionnalités. Avant d'écrire du code Teensy, on **examine chaque point séparément** : ce qu'on sait, ce qu'on veut, et ce qu'on décide. Rien n'est tranché ici : chaque sujet a un état (`à discuter`, `décidé`, `à mesurer`) et on le met à jour au fur et à mesure.

**Méthode.** Un sujet à la fois, dans l'ordre ci-dessous. Pour chacun : (1) rappel des faits vérifiés, (2) options, (3) décision (ou mesure à faire pour décider), (4) conséquences sur le code et sur les autres sujets. Une fois tous les sujets passés, on en tire le plan d'implémentation (P0, P1, P2 en fin de document).

## Faits vérifiés (revue du 10/10/2026)
* `core/` est portable : pas de tas, pas de `std::vector` / `std::string`, pas de fichiers ni de threads ; inclus : `cstdint`, `cmath`, `cstring`, `cstddef`, `atomic`, `cstdio` (un seul `snprintf`, avec `%f`, pour l'affichage du gain en dB). Compilé avec `-fno-exceptions -fno-rtti`.
* L'heure est injectée (`update(now_ms)`) ; stockage, bibliothèque de samples et destination des commandes sont des interfaces (`ProfileStore`, `SampleLibrary`, `CommandSink`).
* Pas de chaîne de compilation ARM sur cette machine (`arm-none-eabi-g++`, PlatformIO, arduino-cli absents).
* **Banc d'essai du pire cas sur le Mac** : 16 pistes stéréo de 5 s, tous les pas allumés, 200 BPM, Release de 2 s (queues pleines), Wide 50, Attack et Decay actifs, décalages. Résultat : **69 µs par bloc de 128 échantillons en moyenne, 221 µs au pire, budget 2 903 µs**. Une Teensy à 600 MHz (sans NEON) est très probablement 15 à 30 fois plus lente que ce cœur : extrapolation grossière, **à confirmer sur la carte**.
* Tailles (PC) : `Engine` 19 Ko, `SamplePlayer` 288 octets x17, `UiController` 68 Ko (contient `ProfileData` 61 Ko), `ProfileCodec` 106 Ko, tampon JSON jusqu'à 256 Ko.

## Les sujets (dans l'ordre proposé)

### T1. Budget CPU du moteur — `à mesurer`
* **Constat** : le pire cas mesuré sur Mac prend 2,4 % (moyenne) à 7,6 % (pire bloc) du budget d'un bloc ; l'extrapolation Teensy est de 35 à 70 % en moyenne et dépasse le budget au pire.
* **À décider** : quelle polyphonie veut-on garantir (16 pistes, combien de queues, stéréo partout) ? Quelle marge de CPU garde-t-on pour l'écran, la SD, les entrées ?
* **Options** : limiter les queues à 2 ; sortir le `switch` de l'enveloppe de la boucle par échantillon (traiter par segments) ; remplacer `tanh` par une approximation ; mode de qualité réduite (mono, sans interpolation) ; compteur de cycles DWT pour mesurer sur la carte ; un banc d'essai permanent dans les tests.
* **Pour trancher** : mesurer sur la vraie carte dès que l'audio sort (P1). En attendant, ne pas optimiser à l'aveugle.

### T2. Entrées : horodatage et chaîne d'interruptions — `en attente, basse priorité` (10/10/2026)
* **Constat (inchangé)** : les `InputEvent` n'ont pas d'horodatage ; une frappe est datée au début du bloc de rendu suivant son arrivée dans le moteur. Tout retard variable (image de l'émulateur à 16 ms, `loop()` occupé sur la Teensy, transit USB de la télécommande) se retrouve dans le groove enregistré ; la compensation de latence ne corrige que le retard fixe.
* **Décision de l'utilisateur** : c'est un problème fin, **pas une priorité absolue**, car il faut d'abord **vérifier que tout fonctionne**. **On n'implémente rien maintenant** (ni le champ « moment de la frappe » dans `Command::Note`, ni la file d'entrées séparée) ; on y reviendra quand on s'intéressera en détail à la précision de l'enregistrement.
* **Pour la mise en route** : les frappes sont datées à leur arrivée dans le moteur, comme aujourd'hui. La précision de l'enregistrement par la télécommande de l'ordinateur sera limitée (USB et image de l'émulateur) ; ce n'est pas bloquant pour valider l'audio, l'écran, la carte SD et l'interface.
* **Pistes déjà identifiées (pour plus tard)** : horloge commune = compteur d'échantillons joués par le moteur ; champ « moment de la frappe » dans `Command::Note`, le moteur retirant le temps écoulé de la position calculée ; lecture des boutons dans un timer (décidé en T3), file d'entrées à part pour garder un seul producteur sur la file des commandes ; priorités des interruptions (la mise à jour audio est une interruption de faible priorité) ; côté PC, utiliser l'horodatage des événements SDL plutôt que la lecture à chaque image ; conversion d'horloge pour la télécommande.

### T3. Câblage des boutons — `décidé` (10/10/2026)
* **Constat** : 31 boutons (16 pas, 10 touches noires, 5 commandes), plus encodeur, potard, écran, I2S. Appui **et** relâchement nécessaires (touches noires tenues), appuis simultanés nécessaires (touche tenue + roue + bouton de pas, accords en mode Clavier). La Teensy 4.1 a environ 42 broches de bord et 7 plots sous la carte (numéros à vérifier sur la carte de brochage de PJRC) : le câblage direct (31 broches + 11 à 13 pour le reste) prendrait presque tout, sans place pour des LED.
* **Réponses de l'utilisateur** : des **LED plus tard, probablement** (une par pas) ; montage **carte perforée / fils directs d'abord, puis circuit imprimé sur mesure** ; complexité en composants : **peu importe, le plus robuste**.
* **Décision : 4 registres à décalage 74HC165 en guirlande** (32 entrées, 3 broches : chargement `PL`, horloge `CP`, sortie `Q7`).
  * Les 32 entrées sont **lues en même temps** (chargement parallèle) : pas de ghosting, accords exacts.
  * Boutons entre l'entrée et la masse, **résistances de rappel de 10 kΩ au 3,3 V** (réseaux SIP de 8 résistances, un par puce) ; alimentation des puces en **3,3 V** (niveaux logiques sûrs pour la Teensy) ; 100 nF de découplage par puce. Pas d'anti-rebond matériel : il est fait par le logiciel.
  * **Table de correspondance** bouton logique -> (puce, bit) à un seul endroit du code Teensy : le câblage sur la carte perforée comme sur le futur circuit imprimé peut suivre ce qui est le plus simple à router, sans rien changer d'autre. L'émulateur n'est pas touché (il produit les mêmes `InputEvent`).
  * **Lecture dans un timer** à 1 à 2 kHz (32 bits en environ 10 à 40 µs), **anti-rebond « première arête immédiate puis verrou de 5 ms »** (latence minimale à l'appui), événements d'**appui et de relâchement horodatés** poussés dans une file séparée (voir T2).
  * **Boutons sur une carte à part** possible : 5 fils vers la Teensy (3 signaux, 3,3 V, masse), au lieu de 31.
* **L'encodeur reste en direct** sur 2 broches (interruptions ou compteur matériel) : un encodeur en quadrature est trop rapide pour un balayage à 1 kHz. Son bouton poussoir éventuel peut prendre la 32e entrée du registre.
* **LED (plus tard)** : réserver 3 broches pour une guirlande de 74HC595 (16 LED, éteintes / allumées) ; pour de la luminosité réglable, un pilote type TLC5947 (même nombre de broches) ou des LED adressables (1 broche). Choix à faire quand on y est ; le budget de broches ne l'empêche pas.
* **Budget de broches après décision** : I2S 3 à 4, écran 2 à 5 (T4), encodeur 2 à 3, potard 1, boutons 3, réserve LED 3 : environ 15 à 20 sur une quarantaine disponibles.
* **Restent à valider au banc** : le mode SPI (ou lecture par bits) du 74HC165, la fréquence de lecture, la durée réelle du verrou anti-rebond, la longueur de câble maximale.
* **Conséquences** : un module `platform/teensy/buttons` (lecture, table, anti-rebond, horodatage) ; rien à changer dans `core/` pour ce sujet (les `InputEvent` existent déjà, y compris `BlackUp`) ; l'horodatage relève de T2.

### T4. Écran — `décidé` (10/10/2026) : Waveshare 2,42 pouces SSD1309 en SPI, pilote maison, image complète envoyée à chaque fois
* **Constat** : `Canvas` est déjà au format SSD1306 (1 024 octets en pages de 8 pixels). Il suffit d'envoyer ce tampon à l'écran ; seul le transport change.
* **SPI (module à 7 broches : GND, VCC, SCK / D0, MOSI / D1, RES, DC, CS)** : jusqu'à 8 à 10 MHz, soit environ **1 ms** pour une image complète ; transfert par DMA possible sur la Teensy 4 (le processeur n'attend pas). 5 broches de signal sur la Teensy (MOSI 11, SCK 13, CS 10 par exemple, plus DC et RES quelconques). Plus de fils à câbler.
* **I2C (module à 4 broches : GND, VCC, SCL, SDA)** : 400 kHz, soit environ **23 ms** par image complète avec la bibliothèque `Wire` (bloquante) ; 2 broches seulement (18 et 19) ; le bus peut être partagé avec d'autres puces (adresse 0x3C ou 0x3D).
* **Pièges d'achat** : (1) les écrans de **1,3 pouce** utilisent souvent un **SH1106** (et non un SSD1306) : même principe mais autre initialisation et décalage de 2 colonnes ; vérifier la puce avant d'acheter ; (2) les écrans bicolores (jaune en haut, bleu en bas) ont une bande jaune fixe de 16 lignes : sans gêne pour l'en-tête, mais à connaître ; (3) alimentation 3,3 V ou 5 V selon le module (régulateur embarqué), niveaux logiques 3,3 V.
* **Impact de la lenteur** : avec les boutons scannés dans un timer (T3), un `loop()` bloqué 23 ms ne fausse plus l'horodatage des entrées, mais il retarde leur traitement et rend l'interface moins vive ; en SPI avec DMA le problème disparaît presque.
* **Recommandation provisoire** : **SPI à 7 broches** (un seul pilote ; le transport I2C peut rester en option pour la mise en route). Rafraîchir seulement si l'image a changé.
* **Référence mise de côté (10/10/2026)** : **Waveshare 2.42 pouces OLED, 128 x 64, blanc, SPI / I2C** ([Amazon B0CL6WMHJN](https://www.amazon.fr/dp/B0CL6WMHJN), [wiki Waveshare](https://www.waveshare.com/wiki/2.42inch_OLED_Module)). D'après le wiki : puce **SSD1309** (et non SSD1306 : proche, mais initialisation légèrement différente), zone active 55 x 27,5 mm (grande, très lisible), alimentation **3,3 V ou 5 V**, **SPI 4 fils par défaut** (I2C possible en déplaçant deux résistances 0 Ω au dos du module, adresse 0x3C ou 0x3D selon DC), **7 broches : VCC, GND, DIN (MOSI / SDA), CLK (SCLK / SCL), CS, DC, RES**. Le prix, les dimensions de la carte et le niveau logique ne figurent pas dans ce que j'ai pu lire : à vérifier sur la fiche avant de commander. Les bibliothèques U8g2 gèrent des SSD1309 128 x 64 ; le `Canvas` (1 024 octets en pages) s'envoie tel quel après l'initialisation propre au SSD1309, à reprendre de l'exemple Waveshare (Arduino, dossier `OLED_2in42`).
* **Décision** : (1) **SPI** (mode par défaut du module) ; (2) **pilote maison minimal** (« pilote » = driver : le petit morceau de code qui initialise la puce et lui envoie le tampon) : la séquence d'initialisation du SSD1309 (reprise de l'exemple Waveshare), une fonction qui envoie les 1 024 octets du `Canvas` page par page, et le pilotage des broches CS, DC et RES. Pas de U8g2 : il apporterait son propre tampon et ses polices pour rien, puisque `Canvas` dessine déjà tout ; (3) **on envoie l'image complète à chaque fois, en mode bloquant** (environ 1 ms en SPI à 8 MHz) : aucune optimisation graphique pour l'instant. Pistes pour plus tard, seulement si on en a besoin : envoi par DMA, envoi des seules pages modifiées, limite de fréquence de rafraîchissement.
* **Reste à vérifier au banc** : la séquence d'initialisation exacte, la fréquence SPI supportée par le module, le contraste.

### T5. Réglage du BPM — `en attente` (10/10/2026)
* **Décision de l'utilisateur** : un potard (ou un encodeur) **uniquement pour le BPM est excessif** : le tempo ne se change pas souvent. **Pas de contrôle physique dédié pour l'instant**, et comme l'ordinateur sert de télécommande (T16), il n'y aura de toute façon pas de slider physique au début. Le sujet est **mis en attente**.
* **En attendant** : le BPM se règle depuis l'ordinateur (le curseur de l'émulateur envoie `InputEvent::PotBpm`, déjà géré par `UiController`). Aucun changement de code. Pas de potard à commander.
* **Ce qu'on avait établi (utile plus tard)** : un potard à butées lu par l'ADC pose trois problèmes sur la vraie carte (bruit qui enverrait des `SetBpm` en continu et marquerait la loop modifiée ; position physique qui ne correspond plus au BPM après le chargement d'une loop ; il faut une « reprise »). Une idée de l'utilisateur les évite : un réglage **relatif** (« on ne lit que les variations » : à 120, un cran à gauche donne 119), ce qui demande un encodeur sans butée.
* **Pistes pour plus tard, sans matériel en plus** : tenir une **touche noire libre** (la 9e, index 8) et tourner la **roue** de navigation, comme pour le gain ou la largeur ; ou une vue ou une ligne de menu dédiée. Dans ce cas, `InputEvent::PotBpm` serait remplacé par un événement relatif `BpmDelta(crans)` (BPM borné à 60..200, `SetBpm` seulement si la valeur change).
* **Rien à décider maintenant.**

### T6. Samples : bibliothèque, lecture, mémoire — `décidé` (10/10/2026)
* **Constat** : `DesktopSampleLibrary::refresh` charge chaque WAV **en entier** pour le valider (impossible sur une carte de 10 Go) ; `load_wav` lit tout le fichier en mémoire (1,8 Mo pour un 808 flottant) puis le convertit ; un fichier utilisé par deux pistes est chargé deux fois ; ouvrir une loop charge les nouveaux samples avant de libérer les anciens (pic doublé) ; la PSRAM se fragmente sans arène ; l'écoute (aperçu) recharge un sample à chaque arrêt de la roue.
* **Décisions de l'utilisateur** :
  * **Aucune limite** sur le nombre de samples sur la carte.
  * **Des dossiers** pour ranger les samples seront utiles, mais en **basse priorité** : on commence avec une liste à plat.
  * Conséquence à prévoir dès maintenant : la bibliothèque ne doit **pas** supposer qu'elle peut garder tous les noms de la carte en RAM. Elle doit pouvoir **parcourir une arborescence** : lister **un dossier à la fois** (noms triés, dossiers d'abord), entrer dans un dossier avec Valider, remonter avec Back, ne lire que les en-têtes. Les samples d'une loop sont référencés par leur **chemin relatif** (`kicks/Kick1.wav`), séparé par `/` : la longueur maximale d'un nom dans le profil (47 aujourd'hui) devra augmenter (par exemple 95). L'interface `SampleLibrary` (`count()`, `name(i)`, `find()`, `load()`) sera pensée dès le départ avec des chemins, pour que la version à plat d'aujourd'hui devienne une arborescence sans rien refaire.
* **Sous-questions** :
  1. *Lister* — **décidé** : lecture d'en-tête seule, un dossier à la fois (voir plus haut).
  2. *Lire* — **décidé** : lecteur WAV **par morceaux** (4 Ko lus, convertis flottant -> 16 bits à la volée et écrits directement à leur place définitive ; le fichier n'est jamais entier en mémoire). Interface commune PC / Teensy ; seule la lecture du fichier diffère (`fopen` ou carte SD).
  3. *Ranger en mémoire* — **décidé : arène à zone unique** (10/10/2026). Une arène en PSRAM (curseur d'allocation remis à zéro à l'ouverture d'une loop, pas de libération individuelle, donc pas de fragmentation), écrite dans `core/` pour être testée sur le Mac, avec la mémoire fournie par la plateforme. **Un court silence au changement de loop est accepté** : l'utilisateur ne prévoit pas de passer d'une loop à l'autre en direct, en jouant. Toute la mémoire reste donc disponible pour une seule loop. Déroulement prévu : l'interface envoie `ClearAll` (la loop se tait), **attend que le moteur confirme** qu'il a rendu tous les tampons (compteur dans `EngineState`), remet le curseur de l'arène à zéro, charge les samples de la nouvelle loop, puis envoie les commandes. Si l'arène est pleine : message « Memoire pleine » et la loop n'est pas chargée. Un sample ajouté ou remplacé pendant l'édition s'alloue à la suite ; un sample remplacé laisse un trou jusqu'au prochain chargement de loop (acceptable). Les deux moitiés alternées restent possibles plus tard si le besoin de changer de loop en direct apparaît.
  4. *Partager* — **décidé** : les samples identiques sont partagés entre pistes (table « chemin -> emplacement » le temps de l'ouverture d'une loop ; facile avec une arène).
  5. *Écoute* — **décidé** : zone séparée et réutilisée, échantillons tronqués (par exemple 2 secondes).
  6. *Fichiers refusés* — **décidé** : la Teensy **ne convertit jamais à la volée** (ni rééchantillonnage ni conversion de profondeur). Un fichier au mauvais format est simplement absent de la liste. Un **outil de conversion sur PC** (script autour d'`afconvert`) est une idée **de très basse priorité** : l'utilisateur peut corriger ses fichiers lui-même en regardant ce qu'il ajoute.
* **Conséquences** : `SampleLibrary`, `SampleBank`, `load_wav`, la vue « Samples » de l'interface (navigation dans les dossiers), le plan mémoire, `ProfileData` (taille des noms de samples).

### T7. Profil sur la carte : format et flux — `à discuter`
* **Constat** : JSON jusqu'à 234 Ko (12 loops pleines), jetons 106 Ko, tampon de texte 256 Ko ; écriture sûre (`.tmp` puis renommage, `.bak`).
* **À décider** : garder JSON tel quel (avec écriture et lecture par flux), ou un format binaire compact sur la carte et JSON seulement sur PC ? Taille réaliste d'un profil (12 loops de 5 à 6 pistes : environ 20 Ko) : dimensionner pour le pire cas ou pour le cas réel ? Que fait-on si le profil est plus gros que la mémoire disponible ?
* **Conséquences** : `ProfileCodec` (interface de flux), `ProfileStore`.

### T8. Fréquence d'échantillonnage — `à discuter`
* **Constat** : `cfg::SAMPLE_RATE = 44100` ; la bibliothèque audio de la Teensy tourne à 44 117,6 Hz. Sans correction : tempo et hauteurs 0,04 % trop rapides.
* **À décider** : constante par plateforme (44 117,647 sur Teensy) ou on laisse tel quel ? Impact sur les fichiers à 44 100 Hz et sur la compensation de latence.

### T9. Audio matériel : sortie jack, démarrage, bruit — `sortie décidée : module PCM5102A` (10/10/2026) ; démarrage et bruit à discuter
* **Besoin de l'utilisateur** : une **sortie jack analogique**. La Teensy 4.1 n'a pas de DAC : il faut un convertisseur externe.
* **Options pour la sortie** :
  1. **Module PCM5102A** (déjà prévu dans la fiche : I2S sur les broches 21, 20 et 7, SCK à la masse, XSMT au 3,3 V, FLT / DEMP / FMT à la masse) : sortie ligne de 2,1 V efficaces, 112 dB de rapport signal / bruit. Beaucoup de modules ont déjà une prise jack 3,5 mm. Pour une enceinte amplifiée, une table de mixage ou une interface audio, c'est suffisant tel quel. Pour un casque, l'étage décrit dans la fiche (filtre RC puis TL072 ou NE5532, condensateur de liaison 10 µF) donne plus de courant.
  2. **Audio Shield de PJRC** (puce SGTL5000, avec amplificateur de casque) : utilise les mêmes broches I2S que ci-dessus et demande une configuration I2C ; plus simple pour un casque, mais prend des broches et fait doublon avec le lecteur SD intégré. Le code audio (`AudioOutputI2S`) serait le même.
  3. **USB audio** (Teensy vu comme carte son par l'ordinateur) : **aucun matériel de plus**, pratique pour la toute première mise en route (le son sort par l'ordinateur) ; type USB « Serial + MIDI + Audio » dans Teensyduino (à vérifier). Fonctionne en même temps que la télécommande série ou MIDI de T16. Pas une solution finale (horloge asservie à l'USB).
* **Décision de l'utilisateur** : **module PCM5102A** pour la sortie analogique en jack. Branchement (rappel) : BCK -> broche 21, LCK -> broche 20, DIN -> broche 7, SCK -> GND, VIN -> 3,3 V ou 5 V selon le module, GND -> GND ; ponts du module : FLT sur L, DEMP sur L, XSMT sur H (sinon muet), FMT sur L. À vérifier sur la sérigraphie du module reçu. Un étage de sortie avec amplificateur (fiche, section 3) reste possible plus tard pour un casque.
* **Recommandation provisoire** : commencer par l'option 3 pour l'étape (3) des tests, commander le module PCM5102A avec son jack en parallèle pour passer à la sortie analogique ensuite (le code reste le même ; seule la sortie change).
* **À décider aussi** : broche de mute (XSMT) pour éviter le « plop » au démarrage ; ordre d'initialisation (DAC, audio, écran, SD) ; niveau de sortie et `MASTER_GAIN` ; qualité de l'alimentation pour le bruit (déjà dans la fiche).

### T10. Carte SD : erreurs et noms — `à discuter`
* **À décider** : comportement si la carte est absente, abîmée ou retirée en cours de route ; noms FAT (accents, casse, 47 caractères) ; hot-swap ; durée de vie en écriture (profil réécrit à chaque sauvegarde) ; sauvegarde du profil précédent.

### T11. Chaîne de compilation et portabilité — `à discuter`
* **À décider** : environnement (Teensyduino + Arduino IDE, PlatformIO, ou `arm-none-eabi-gcc` + CMake) ; compiler `core/` pour `-mcpu=cortex-m7` dès maintenant, avec `-Wdouble-promotion` ; remplacer le `%f` de `snprintf` ; vérifier `std::atomic<float>` sans verrou ; objets statiques en RAM2 / PSRAM (constructeurs) ; garder les tests PC comme référence.

### T12. Émulateur « mode contraintes » — `à discuter`
* **À décider** : blocages de `loop()` simulés (20 ms), gigue et retard des entrées, jauge de charge CPU, limites de mémoire. Objectif : trouver les problèmes de T1 et T2 sur PC avant la carte.

### T13. Qualité du moteur — `à discuter`
* **À décider** : niveaux de qualité (interpolation linéaire / sans, stéréo / mono, queues 2 / 4) et comment ils se règlent ; limiter la polyphonie en dernier recours (que fait-on si le budget CPU est dépassé : couper une voix, baisser la qualité, afficher un avertissement ?).

### T14. Divers — `à discuter`
* Redémarrage et watchdog, migration du format de profil (version), USB (MIDI, série de debug), mise à jour du firmware, comportement au premier démarrage (carte vierge), journal d'erreurs.

### T15. Clavier MIDI et clavier d'ordinateur comme entrées — `à discuter`
* **Idée de l'utilisateur** (10/10/2026) : jouer les 24 (+2) touches avec un clavier MIDI, ou avec le clavier de l'ordinateur dans un premier temps.
* **Ce qui existe** : l'émulateur reçoit déjà le clavier de l'ordinateur (touches 1-8 et Q à I = les 16 boutons de pas / touches blanches, Z X C V B N , . / ' = les 10 touches noires). La vélocité de la frappe est fixe à 127 (`KEYBOARD_VELOCITY`), et il n'y a pas d'événement de relâchement pour les notes (seulement pour les touches noires de réglage).
* **Ce qu'un clavier MIDI apporterait** : une **vraie vélocité** (déjà stockée par note : `Hit::velocity`, `Command::Note::value`) ; le **note-off**, donc la **longueur des notes** (lien direct avec la future « mini séquence MIDI » et avec le Release de l'enveloppe, `plan_envelope.md`) ; plus de 24 touches (49 ou 61), avec une transposition / un décalage d'octave pour rester dans la plage de pitch -12..+11 (Do central = MIDI 60 = pitch 0) ; sur PC comme sur Teensy.
* **Comment** (à étudier) :
  * `InputEvent::NoteOn(pitch, vélocité)` et `NoteOff(pitch)` dans `core/ui`, traités en mode Clavier / Rec ; les touches physiques ou du clavier d'ordinateur produisent les mêmes événements (vélocité 127) ;
  * PC (émulateur) : MIDI par CoreMIDI ou une petite bibliothèque (RtMidi) ; clavier d'ordinateur : disposition « piano » (A S D F ... pour les blanches, W E T Y U pour les noires) à comparer à la disposition actuelle ;
  * Teensy : **USB hôte** (bibliothèque USBHost_t36 : un clavier MIDI ou un clavier d'ordinateur se branche directement sur le connecteur 5 broches, alimentation 5 V à prévoir), **USB appareil** (la Teensy apparaît comme appareil MIDI pour un ordinateur ou une station audio), ou **MIDI DIN** (optocoupleur 6N138 + UART à 31 250 bauds) ;
  * tout passe par la même file d'événements **horodatés** que les boutons (T2).
* **À décider** : vélocité réelle oui / non (et courbe) ; quel(s) transport(s) MIDI ; comment gérer une plage de plus de 2 octaves ; ajouter le MIDI maintenant dans l'émulateur ou plus tard.

### T16. Piloter la Teensy depuis l'ordinateur pendant la mise en route — `décidé dans son principe` (10/10/2026)
* **Décision de l'utilisateur** : dans un premier temps, **commander seulement la carte Teensy (et l'écran OLED)**, pas les boutons ni le potard : le but est de **vérifier que le logiciel fonctionne**, en testant le moins de choses à la fois. **L'ordinateur sert de télécommande** : l'émulateur envoie les `InputEvent` à la Teensy par USB ; la Teensy fait tourner le moteur et `UiController`, et **dessine le `Canvas` sur l'écran OLED**. Une **sortie audio analogique en jack** est souhaitée.
* **Intérêt** : tester l'audio, la charge CPU (T1), la carte SD (T6, T7) et l'écran (T4) sans câbler un seul bouton ; tester le pipeline d'horodatage (T2) ; le matériel de saisie (T3) ne bloque plus le P1.
* **Reste à décider** : protocole (type, index, valeur, horodatage ; texte lisible ou binaire ; série USB ou MIDI USB), option « envoyer à la Teensy » dans l'émulateur, retour d'état éventuel (la Teensy peut renvoyer l'image de l'écran à l'émulateur), comment l'émulateur sait quels samples et quel profil sont sur la carte SD de la Teensy.

### Mise en route minimale (liste proposée, à valider)
| Élément | Rôle | Remarque |
|---|---|---|
| **Teensy 4.1** | tout le logiciel | idéalement la variante **avec PSRAM déjà soudée** (8 Mo) : pas de soudure sous la carte ; à vérifier chez PJRC |
| **Carte microSD** 16 à 32 Go (FAT32) | samples et profil | le lecteur est sur la Teensy |
| **Écran OLED SSD1306 128x64** | affichage du `Canvas` | SPI (7 broches) de préférence, I2C (4 broches) possible : voir T4 |
| **Module DAC PCM5102A** avec prise jack 3,5 mm | sortie analogique | peu cher, à commander dès le départ (voir T9) |
| Câbles dupont / plaque d'essai, câble USB | liaisons et alimentation | |
**Étapes de test, une seule inconnue à la fois** : (1) clignotement et série ; (2) écran seul : motif de test, puis `UiController` qui dessine le `Canvas` ; (3) audio : le séquenceur joue un pattern fixe avec quelques samples en mémoire, mesure du CPU ; (4) télécommande USB depuis l'émulateur ; (5) carte SD : samples et profil ; (6) ensuite seulement les boutons (T3), le potard (T5), le MIDI (T15).

## Ordre de discussion proposé
1. **T3 (câblage)**, **T4 (écran)**, **T5 (potard)** : ce sont des choix de matériel qui contraignent le reste et qu'il vaut mieux figer tôt.
2. ~~T2 (entrées et horodatage)~~ : **en attente**, basse priorité (voir T2).
3. **T6 (samples)**, **T7 (profil)** : les deux gros postes mémoire et disque.
4. **T1 (CPU)**, **T13 (qualité)** : ne se tranchent qu'après mesure ; on prépare les mesures.
5. **T15 (MIDI et clavier d'ordinateur)** et **T16 (pilotage depuis l'ordinateur)** : partagent la file d'événements horodatés de T2, et T16 conditionne l'ordre de la mise en route (P1).
6. **T8, T9, T10, T11, T12, T14** : plus courts.

## Du plan d'examen au plan d'implémentation (à remplir après les décisions)
* **P0 (sans matériel)** : compilation ARM, horodatage des entrées, constante de fréquence par plateforme, lecteur WAV et lecture d'en-tête, hystérésis du potard, banc d'essai CPU dans les tests, émulateur avec contraintes.
* **P1 (mise en route)** : audio par `AudioStream` avec un pattern fixe, mesure du CPU, puis SD, écran, entrées.
* **P2** : intégration et essais.
