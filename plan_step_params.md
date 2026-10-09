# Plan : vélocité et pitch par pas

Objectif : pouvoir régler, pour **chaque pas joué** d'une piste, sa **vélocité** (volume de la note) et son **pitch** (hauteur), en plus de « joué ou non ». Ces réglages sont sauvegardés avec les loops.

## 1. Décisions

* **Données par pas** : joué ou non (existant), **vélocité** de 1 à 127 (défaut **127**, donc les loops existantes ne changent pas de volume), **pitch** en demi-tons de -12 à +12 (défaut **0**). Les valeurs d'un pas sont conservées même quand le pas est éteint.
* **Vélocité -> volume** : gain = vélocité / 127 (courbe linéaire pour commencer). Il se multiplie avec le gain de la piste.
* **Pitch -> vitesse de lecture** : rapport `2^(demi-tons / 12)` ; il se multiplie avec le pitch de la piste. Il s'applique à chaque déclenchement (une note remplace la précédente de la piste).
* **Interaction par modes**, avec un bouton **Mode** qui fait tourner **Edit -> Velocity -> Pitch -> Edit**, uniquement dans la vue Pattern (ignoré ailleurs). On arrive toujours en mode **Edit**.

| Mode | Bouton de pas | Roue | Boîtes |
|---|---|---|---|
| **Edit** | bascule le pas (comme aujourd'hui) | rien | pleine = joué, contour = silence |
| **Velocity** | sélectionne le pas (s'il est joué) | règle sa vélocité (4 points par cran) | remplissage = vélocité |
| **Pitch** | sélectionne le pas (s'il est joué) | règle son pitch (1 demi-ton par cran) | ligne centrale = 0 ; remplissage vers le haut (+) ou vers le bas (-) |

* **Pas éteints** en Velocity et Pitch : leur boîte reste vide et appuyer dessus ne fait rien (pour allumer un pas, on repasse en Edit).
* **Le pas sélectionné reste le même quand on change de mode.**
* **Écoute** : à chaque cran de roue dans Velocity ou Pitch, la note du pas sélectionné est jouée (à la vélocité et au pitch en cours), pour entendre le réglage. Limitée à une note toutes les 60 ms environ.

## 2. Affichage (écran 128x64, 1 bit : pas de transparence)
Le « pas alpha » devient un **remplissage en hauteur** de la boîte de 7 pixels (de 0 à 7 pixels), du bas vers le haut.
* **Velocity** : l'intérieur de la boîte (5 lignes) se remplit du bas vers le haut : `1 + ((vélocité - 1) x 4 + 63) / 126` lignes, soit de 1 à 5 ; vélocité 127 = boîte entièrement pleine, comme en Edit. *(Changement par rapport à la première idée « 0 à 7 pixels » : avec le contour, une vélocité de 1 aurait eu la même apparence qu'un pas éteint.)*
* **Pitch** : le contour avec une **ligne centrale** (pitch 0) ; un pitch positif remplit vers le haut, un pitch négatif vers le bas, de 0 à 2 lignes en plus (`(|pitch| x 2 + 6) / 12`). Un pas joué se distingue donc toujours d'un pas éteint. Les valeurs de -2 à +2 se confondent visuellement avec 0 : le texte donne la valeur exacte.
* **Bas de l'écran** : en Edit, « Pas 5 : joue » comme avant. **En Velocity et Pitch, une jauge horizontale pour le pas sélectionné** (s'il est joué), sous les boîtes, avec **sa valeur exacte à droite** : contour de 100 x 7 pixels.
  * **Velocity** : remplie depuis la gauche, de 1 pixel (vélocité 1) à toute la jauge (127). Valeur écrite : `64`, `127`.
  * **Pitch** : remplie depuis le **centre**, vers la droite pour un pitch positif, vers la gauche pour un pitch négatif, 4 pixels par demi-ton (48 pixels pour ±12) ; un trait central qui dépasse d'un pixel en haut et en bas marque le 0. Valeur écrite : `+3`, `-12`, `0`.
  * **Aucune jauge** tant qu'aucun pas joué n'est sélectionné (pas d'invitation), ni quand le pas sélectionné a été éteint depuis.
  * Cela remplace le texte « Pas 5  Vel 90 » (supprimé à la demande de l'utilisateur) : la valeur exacte redevient lisible, et les pitchs de -2 à +2 se distinguent de 0.
* **Le mode est indiqué en haut à droite par une seule lettre, à gauche du BPM : `E` (Edit), `V` (Velocity) ou `P` (Pitch)**, lettre simple dans les trois cas (pas d'inversion). La dernière ligne de l'écran est libre. Le titre de la vue Pattern (nom de la piste) est coupé à 13 caractères pour laisser la place à la lettre.
* **Niveaux de remplissage des boîtes** : 5 en vélocité, 3 en pitch (coarse, pour voir d'un coup d'œil la phrase entière) ; la jauge du pas sélectionné donne la précision.

## 3. Moteur (`core/`)
* `Pattern` du séquenceur : en plus de `on[piste][pas]`, `velocity[piste][pas]` (1..127, défaut 127) et `pitch[piste][pas]` (-12..12, défaut 0). `ClearTrack` et `ClearAll` les remettent par défaut.
* Au déclenchement d'un pas, le séquenceur appelle `trigger(piste, vélocité, pitch)` : la voix applique le gain `vélocité/127` et le pitch du pas multiplié par celui de la piste. `Voice` reçoit une surcharge `trigger(vélocité, rapport_de_pitch)` (le synthé en sommeil l'ignore).
* Nouvelles commandes : `SetStepVelocity(piste, pas, valeur)`, `SetStepPitch(piste, pas, demi-tons)` et `AuditionStep(piste, pas)` (joue la note du pas avec ses valeurs actuelles, sans toucher au séquenceur). Les valeurs hors limites sont bornées.
* Aucune allocation, tableaux fixes : 16 pistes x 16 pas x 2 octets = 512 octets de plus.

## 4. Sauvegarde (format JSON, version 1 inchangée)
Deux clés facultatives par piste, **écrites seulement si une valeur s'écarte de la valeur par défaut** :
```json
{ "sample": 0, "gain": 1.0, "pitch": 1.0, "pattern": "x...x...x...x...",
  "velocity": "127,127,127,127,90,127,127,127,127,127,127,127,127,127,127,127",
  "step_pitch": "0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,-2" }
```
* **Listes de nombres dans une chaîne** plutôt que des tableaux JSON : un tableau de 16 valeurs coûte 17 jetons à jsmn ; avec 16 pistes x 32 loops, le tableau de 6144 jetons (environ 100 Ko) ne suffirait plus et un profil plein ne se chargerait plus. Une chaîne coûte 1 jeton et reste lisible. Elle est lue par un petit lecteur d'entiers séparés par des virgules (borné, avec contrôle des limites).
* **Compatibilité** : les anciens fichiers se chargent tels quels (valeurs par défaut) ; un ancien programme qui lit un nouveau fichier ignore les clés qu'il ne connaît pas (il perdrait seulement ces réglages à sa prochaine sauvegarde).
* Valeur invalide (hors limites, texte) : statut `BadField`, comme pour les autres champs. Liste plus courte que 16 : les pas manquants prennent la valeur par défaut.
* **Mémoire** : `TrackData` passe de 12 à environ 44 octets ; le profil complet (32 loops x 16 pistes) passe d'environ 9 Ko à 28 Ko, l'`UiController` (profil + copie de sécurité) à 58 Ko, et le tableau de jetons du lecteur à 115 Ko. À déclarer en statique sur Teensy (RAM2).

## 5. UI (`core/ui/`)
* **`InputEvent::Mode`**.
* **Modèle** : chaque piste de l'UI garde `velocity[16]` et `pitch[16]`. L'export vers `LoopData` et le chargement d'une loop les prennent en compte. Le chargement envoie, après les 4 commandes de base d'une piste, une commande par valeur non défaut ; le nombre de commandes devient variable, mais le chargement par paquets le gère déjà.
* **Comportement** :
  * Entrer dans la vue Pattern : mode Edit.
  * Mode : passe au mode suivant (vue Pattern seulement).
  * Pas en Velocity ou Pitch : sélectionne le pas s'il est joué, sinon rien.
  * Roue en Velocity ou Pitch, avec un pas sélectionné : règle la valeur (bornée), envoie la commande, marque la loop comme modifiée, joue la note d'essai.
  * Roue en Edit : rien (comme aujourd'hui).
* **Rendu** : boîtes, texte et indication du mode, comme décrit au §2.
* **Émulateur** : bouton « Mode » et touche Tab.

## 6. Étapes
1. **Moteur et séquenceur : FAIT** (`test_step_params`, sous sanitizers aussi). `Pattern` avec `velocity[piste][pas]` (1..127, défaut 127) et `pitch[piste][pas]` (-12..12, défaut 0), `reset_track` ; `Voice::trigger_pitched(vélocité, rapport)` (les voix sans pitch l'ignorent) ; `SamplePlayer` : pitch effectif = piste x note, borné à 0,05 - 8, recalculé si le pitch de la piste change pendant la note ; commandes `SetStepVelocity`, `SetStepPitch` (arrondies et bornées) et `AuditionStep` (joue la note d'un pas, même éteint, sans toucher au séquenceur) ; `Engine::play_step` ; bornes dans `config.h` (`VELOCITY_*`, `STEP_PITCH_*`) et `dsp::semitone_ratio`. Texte d'origine : Tests : la vélocité change le niveau, le pitch change la vitesse de lecture (position dans le sample), combinaison avec le gain et le pitch de la piste, bornes, valeurs conservées quand le pas est éteint, remise à zéro par `ClearTrack` / `ClearAll`, précision d'échantillon du séquenceur inchangée.
2. **Profil et format : FAIT** (`test_profile`, sanitizers aussi). `TrackData` : `step_velocity[16]` (défaut 127) et `step_pitch[16]` (défaut 0), toujours un agrégat (`{sample, gain, pitch, pattern}` fonctionne encore). Clés `velocity` et `step_pitch` : listes d'entiers séparés par des virgules dans une chaîne (espaces et signe `+` acceptés à la lecture), écrites seulement si une valeur n'est pas celle par défaut, 16 valeurs à l'écriture ; plus courtes ou vides à la lecture : le reste garde la valeur par défaut. Valeur hors limites, texte mal formé, plus de 16 valeurs, type JSON incorrect : `BadField` (jamais corrigé en silence) ; à l'écriture, des valeurs hors limites sont refusées. **Le tableau de jetons passe de 6144 à 7168 (environ 115 Ko)** : le pire cas (32 loops de 16 pistes avec les deux listes) demande 6 891 jetons ; un profil plein de cette sorte pèse 110 Ko de JSON et se relit entièrement (testé). Tailles mesurées : `TrackData` 48 octets, `ProfileData` 28 Ko, `UiController` 58 Ko, `ProfileCodec` 115 Ko. Fichiers abîmés au hasard : l'invariant « vélocités et pitchs toujours dans les bornes » tient sur 4 000 altérations. Texte d'origine : lecture et écriture des deux listes, valeurs par défaut non écrites. Tests : aller-retour, anciens fichiers, listes trop courtes ou trop longues, valeurs hors limites, texte invalide, profil plein avec toutes les listes (le tableau de jetons ne doit pas déborder), nouvelle série de fichiers abîmés au hasard, sanitizers.
3. **Modèle de l'UI : FAIT** (`test_loops`, `test_integration`). Chaque piste de l'UI garde `step_velocity[16]` et `step_pitch[16]` (accesseurs `step_velocity(piste, pas)` et `step_pitch(piste, pas)`) ; `add_track` accepte ces deux tableaux (bornés, ou valeurs par défaut s'ils sont nuls). Export vers `LoopData` et chargement : après les 4 commandes de base d'une piste, une commande par réglage non défaut (`SetStepVelocity` pour les vélocités dans l'ordre des pas, puis `SetStepPitch`) ; le nombre de commandes est variable, la piste sans réglage n'en envoie que 4, et le chargement par paquets de 5 envoie exactement les mêmes commandes dans le même ordre. Une piste sautée (sample manquant) ne décale pas les réglages des suivantes. Test de bout en bout : un profil JSON écrit à la main (vélocités, pitchs, gain) chargé par l'UI produit exactement le même audio qu'un moteur réglé directement, avant et après une resauvegarde. Texte d'origine :
4. **Modes, interaction et affichage : FAIT** (`test_step_edit` ; 7 mutations du code sur 8 détectées par les tests, la huitième est équivalente). `InputEvent::Mode` ; `UiController::EditMode` (Edit, Velocity, Pitch) : Mode fait tourner les modes dans la vue Pattern seulement, le pattern s'ouvre toujours en Edit, le mode survit à un aller-retour par le menu ; en Velocity et Pitch un bouton de pas sélectionne le pas s'il est joué (un pas éteint est ignoré), la roue règle la valeur du pas sélectionné (vélocité : 4 points par cran, pitch : 1 demi-ton), borné, avec envoi de `SetStepVelocity` / `SetStepPitch` seulement si la valeur change, loop marquée modifiée, et file de commandes pleine sans effet sur le modèle ; la roue ne fait rien en Edit, sans sélection, ou si le pas sélectionné a été éteint depuis. Rendu des trois modes et textes (voir §2). Émulateur : bouton « Mode » et touche Tab. Texte d'origine :
5. **Écoute : FAIT avec l'étape 4** (`test_step_edit`). Chaque réglage envoie `AuditionStep` juste après la commande de valeur ; cadence limitée à une note toutes les 60 ms, et si un réglage tombe pendant ce délai, la note est retenue et part dans `update()` : on entend toujours la **valeur finale** ; elle est abandonnée si on change de mode ou de vue avant. Texte d'origine :
6. **Essais sur l'émulateur** puis retouches (lisibilité des niveaux, voie de barres si besoin).

## 7. Limites assumées
* Pitch uniquement par demi-tons, de -12 à +12 (le lecteur de samples interpole linéairement : monter beaucoup d'octaves ajoute de l'aliasing).
* Vélocité linéaire (pas de courbe perceptuelle pour l'instant).
* Pas de durée de note, de probabilité ni de modification de plusieurs pas à la fois.
* Le gain et le pitch **de la piste** ne sont toujours pas modifiables depuis l'UI.
