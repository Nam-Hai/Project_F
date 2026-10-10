# Plan : enveloppe ADSR par piste

Objectif : couper un sample **en douceur** (un Release court) au lieu de la coupure nette qui faisait **cliquer** les 808 quand une note en remplace une autre, et donner une enveloppe ADSR réglable par piste.

## 1. Décisions (validées le 10/10/2026)
* **Un ADSR par piste** (comme la quantification, la largeur et la balance) : tous les pas de la piste partagent la même enveloppe.
* **Pas de note-off pour l'instant** : on joue toujours le sample en entier. Le **Release** ne sert qu'à la **relance** : quand une nouvelle note arrive sur la piste, l'ancienne descend à zéro en R ms pendant que la nouvelle démarre. **À terme**, une mini « séquence MIDI » permettra de poser des notes et leur **longueur** sur le pattern : la fin d'une note déclenchera alors le Release (note-off). Le lecteur a déjà la fonction qui passe une lecture en Release (`start_release`) : il suffira de l'appeler à la fin de la longueur, sans refaire l'enveloppe.
* **Réglage** : les **touches noires 5, 6, 7 et 8** (index 4 à 7), tenues dans la vue Pattern, avec la roue (comme Quantifier, Wide et Balance sur les touches 2 à 4). Pendant le maintien, l'écran montre **la forme classique de l'ADSR** avec le segment réglé en gras et les quatre valeurs en bas (celle réglée en négatif).
* **Défauts** : A 0 ms, D 0 ms, S 100 %, **R 5 ms**. Le son d'une note seule est donc inchangé (niveau 1 constant), et la relance d'un 808 ne clique plus sans rien régler.

## 2. Moteur (`core/sample_player.*`, `core/engine.*`)
* **Enveloppe par note**, rampes **linéaires** : Attack de 0 à 1 ; Decay de 1 au niveau Sustain ; Sustain tenu jusqu'à la fin du sample ; Release du niveau atteint à 0 (durée R, quel que soit le niveau de départ). A, D et R en ms (0..2000, 0 = immédiat), S en % (0..100). Avec A = 0 la note démarre à plein niveau ; avec D = 0 (ou S = 100 %) le niveau tenu est atteint dès le premier échantillon.
* **Relance** : la note en cours **et déjà audible** est copiée dans une **queue** en Release pendant que la nouvelle démarre (au plus 4 queues par piste ; si elles sont toutes prises, la plus basse, donc la plus ancienne et presque éteinte, est reprise). Une relance avant le moindre échantillon produit (deux notes au même instant) ne crée pas de queue : pas de double volume. **Release = 0** : coupure nette, comme avant l'enveloppe.
* Une **queue garde le pan, le pitch et le gain de sa note** ; la largeur stéréo de la piste, elle, s'applique à tout en direct. `set_sample` (changement de sample) arrête aussi les queues (elles liraient l'ancien buffer, rendu au moteur).
* **Commandes** `SetTrackAttack`, `SetTrackDecay`, `SetTrackSustain`, `SetTrackRelease` (valeur arrondie et bornée, piste invalide ignorée) ; `ClearTrack` et `ClearAll` remettent les défauts. Constantes : `cfg::ENV_MS_MAX` (2000) et `cfg::ENV_DEFAULT_RELEASE_MS` (5).
* **Le kick synthétique** (en sommeil) n'a pas d'enveloppe.
* **Pourquoi le clic disparaît** : les 808 commencent à zéro. À la relance, la somme « ancienne note x niveau qui descend + nouvelle note qui part de 0 » est continue avec ce qui sonnait juste avant : plus de saut.

## 3. Profil
Quatre clés facultatives par piste, écrites seulement hors défaut (0, 0, 100, 5) : `attack`, `decay`, `sustain`, `release` (entiers ; 0..2000 ms, 0..100 %). Lecture stricte (`BadField` hors bornes, décimal, chaîne, `null`...), anciens fichiers : défauts, **un Release à 0 est écrit** (il diffère du défaut 5). Tableau de jetons : 5 120 -> **6 656** (16 clés par piste : 33 jetons, pire cas 6 471) ; profil plein tout rempli : 234 Ko de JSON (tampon d'écriture de 256 Ko). Mémoire : `TrackData` 300 octets, `SamplePlayer` 288 octets (x17 = 4,9 Ko, queues comprises), `ProfileCodec` 104 Ko.

## 4. Interface (`core/ui/`)
* Touches noires **5 à 8** : Attack, Decay, Sustain, Release. En-tête : `Atk`, `Dec`, `Sus`, `Rel`. Mêmes règles de maintien que les touches 2 à 4 (la dernière enfoncée règle, la précédente reprend la main au relâchement, Back ou Menu abandonnent, aucune action hors vue Pattern ; dans le mode Clavier / Rec elles jouent des notes).
* **La roue** : Attack, Decay et Release parcourent une liste, **un cran = la valeur suivante** (fine près de 0 : 0, 1, 2, 3, 5, 7, 10, 15, 20, 30, 40, 50, 70, 100, 150, 200, 300, 500, 700, 1000, 1500, 2000 ms) ; une valeur qui n'y est pas (venue d'un fichier) va d'abord à la plus proche dans le sens du mouvement. **Sustain** : 5 % par cran.
* **Visualiseur** : la forme (montée, descente, palier, descente finale) dessinée avec le nouveau tracé de segments du `Canvas` (`line`, Bresenham), largeurs **logarithmiques** (0 ms = 1 pixel, 2000 ms = 27 pixels), hauteur du palier = Sustain, axe du temps en pointillés, segment réglé en gras. Bas de l'écran : les quatre valeurs **avec leurs unités**, sur une seule rangée, la valeur réglée en négatif : `A12ms D.3s S70% R5ms`. Durées en texte court de 4 caractères au plus : `12ms` sous 100 ms, `.3s` / `.15s` de 100 à 999 ms (secondes sans zéro devant), `1s` / `1.5s` à partir de 1000 ms ; le pire cas (`A.15s D70ms S100% R1.5s`, 20 caractères, 2 pixels entre deux valeurs) tient en 126 pixels sur 128. La forme remplace la timeline pendant le maintien.
* Chargement (une commande par réglage non défaut, par petits paquets) et sauvegarde conservent tout.
* **Émulateur** : étiquettes `A`, `D`, `S`, `R` au-dessus des touches 5 à 8.

## 5. Tests
`test_envelope` (niveaux de chaque étage à l'échantillon près, Release depuis un niveau partiel, saut mesuré à la relance d'un 808 : **0,3 sans Release, moins de 0,03 avec**, queues, vol de queue, sample changé, pitch des queues, bornes, commandes, séquenceur de bout en bout), `test_envelope_ui` (touches, roue, valeurs hors liste, forme à l'écran, chargement et sauvegarde, vrai moteur), `test_profile`, `test_canvas` (segments).

## 6. Limites assumées et suite
* Pas de note-off tant qu'il n'y a pas de longueur de note : le Sustain tient jusqu'à la fin du sample. Un 808 se raccourcit donc avec **Decay et Sustain 0 %** (il s'éteint après D), ou avec un Release et des relances.
* Rampes linéaires seulement (pas de courbes exponentielles).
* Pas d'enveloppe par note ni de modulation par la vélocité.
* **Suite prévue** : la mini séquence de notes avec longueurs (piano roll) fera fonctionner le Release comme un vrai note-off ; le Gate par piste (longueur fixe) pourrait servir de première étape.
