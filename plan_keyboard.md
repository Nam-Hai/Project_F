# Plan : clavier de piano (24 + 2 touches)

Objectif : jouer l'échantillon d'une piste à des **hauteurs différentes**, sur un clavier de piano, avec un **retour sonore immédiat**, depuis une vue dédiée. **Sans enregistrement** : c'est un instrument pour entendre comment un sample sonne à chaque note. Cette fonctionnalité est utile seule, et c'est la base de l'enregistrement à la main (`plan_record.md`).

## 1. Disposition
**26 boutons, 24 jouables.** Les **16 boutons de pas actuels sont les touches blanches** ; **10 nouveaux boutons, placés au-dessus et décalés, sont les touches noires**.

```
   noires :   ▪  ▪     ▪  ▪  ▪     ▪  ▪     ▪  ▪  ▪
   blanches : C  D  E  F  G  A  B  C  D  E  F  G  A  B  ·  ·
              1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16
```
* **Deux octaves** : les **14 premières blanches** et les 10 noires font les 24 demi-tons.
* **Les blanches 15 et 16 sont désactivées** dans la vue Clavier (elles ne servent qu'à garder 16 blanches alignées avec les 16 pas) ; elles pourront servir à autre chose.
* **Le Do de la 8e blanche est la note de base** (pitch 0), il n'y a pas de pad séparé.

| Touche | Pitch (demi-tons) |
|---|---|
| blanches 1 à 14 : Do Ré Mi Fa Sol La Si Do Ré Mi Fa Sol La Si | -12 -10 -8 -7 -5 -3 -1 **0** +2 +4 +5 +7 +9 +11 |
| noires, de gauche à droite | -11 -9 -6 -4 -2 +1 +3 +6 +8 +10 |

**Tout tient dans la plage de pitch actuelle (±12)** : aucun changement de plage, pas de décalage d'octave.

## 2. Décisions
* **Vélocité fixe à 127** : les boutons n'ont pas de « force de frappe ». Elle se règle ensuite note par note avec le mode V ; des pads sensibles pourront l'apporter plus tard.
* **Une vue dédiée « Clavier »** : c'est elle qui transforme les boutons en clavier 24 + 2 (la future prise d'enregistrement s'y fait aussi : elle devient un état de cette vue, voir `plan_record.md`). **Back** en sort et revient à la vue d'origine. Aucun mode du bouton Mode : on n'y entre pas par erreur.
* **On y entre d'une seule façon : avec la première touche noire** (la plus à gauche, entre les deux premières blanches ; Do dièse, pitch -11), qui hors de la vue Clavier est **le bouton d'entrée dans le clavier**. Elle fonctionne depuis la vue **Pattern** (piste ouverte) et depuis la vue **Pistes** (curseur sur une piste, pas sur la ligne de titre ni sur « Ajouter piste »). L'appui ouvre la vue Clavier **et joue la note de cette touche** (Do dièse grave), pour ne pas perdre le geste. **Il n'y a pas de ligne « Clavier » dans le menu de la piste** (elle ferait doublon avec ce bouton).
* **Les 9 autres touches noires ne font rien hors de la vue Clavier**, et la première est ignorée dans les menus, la liste des samples, la liste des loops et le menu de loop. Dans la vue Clavier, les 10 noires jouent toutes leur note. Les blanches restent les boutons de pas partout ailleurs que dans la vue Clavier. Les 9 noires inutilisées et les deux blanches désactivées (15 et 16) pourront recevoir d'autres fonctions.
* Les touches jouent la piste **tout de suite**, au pitch de la touche, même lecture à l'arrêt, sans toucher au pattern ni au séquenceur.

## 3. Moteur (`core/`)
* Nouvelle commande **`Note(piste, vélocité, pitch)`** : fait sonner l'échantillon de la piste immédiatement, avec cette vélocité et ce pitch (rapport `2^(demi-tons/12)`, multiplié par le pitch de la piste, comme pour les notes des pas). Piste sans voix, piste invalide : ignorée. Le pitch est borné à ±12. Elle compte comme un déclenchement de la piste (indicateur de la liste).
* Le retour sonore est le même que celui d'un pas : `trigger_pitched`, sans nouvelle voix.

## 4. Interface (`core/ui/`)
* **Événements d'entrée** : les 14 premières blanches envoient l'événement de pas existant ; **un nouvel événement pour les 10 noires** (indice 0 à 9). Dans la vue Clavier, l'UI interprète les événements de pas comme des touches blanches (les pas 15 et 16 sont ignorés), partout ailleurs ils restent des pas. Hors de la vue Clavier, **seule la noire n° 1** a un effet : dans la vue Pistes (curseur sur une piste) ou Pattern, elle ouvre la vue Clavier et joue sa note ; les autres noires et la noire n° 1 dans les autres vues sont ignorées.
* **Table de correspondance** touche -> pitch, en un seul endroit (voir §1), testée.
* **Vue Clavier** :
  * Titre : le nom de la piste ; à droite, comme dans les autres vues, le BPM et l'état de lecture.
  * **Un clavier dessiné à l'échelle** : 14 blanches de 8 pixels de large (les deux dernières cases vides), 10 noires au-dessus, décalées. Le Do central est marqué.
  * Une touche frappée **s'inverse un instant** (environ 120 ms, comme l'indicateur de jeu des pistes) : les boutons n'envoient que l'appui, sans relâchement.
  * Back : sortie. Les autres boutons (Menu, Play, potard) gardent leur rôle habituel ; Play et le BPM fonctionnent donc pendant qu'on joue.

## 5. Émulateur
* Les 10 touches noires sont dessinées **au-dessus des 16 boutons de pas**, décalées comme sur un clavier, toujours visibles ; hors de la vue Clavier, un clic sur la **première** ouvre la vue Clavier (vue Pistes ou Pattern), les neuf autres sont inactives (grisées). En vue Clavier, les deux dernières blanches sont grisées.
* Clic sur une touche = appui. Les touches de l'ordinateur pour jouer au clavier (disposition type logiciel de musique) viendront plus tard.

## 6. Étapes
1. **Moteur : la commande `Note` : FAIT** (`test_note`, sanitizers aussi ; 4 casses volontaires du code sur 5 détectées, la cinquième est équivalente). `Command::Note` : `track`, `value` = vélocité (arrondie, bornée à 1..127), nouveau champ `pitch` (demi-tons, borné à ±12, 0 par défaut) ; joue la piste immédiatement par `trigger_pitched`, indépendamment du pattern et du séquenceur (qui ne bougent pas) ; le pitch de la note se multiplie avec celui de la piste et ne persiste pas (un pas joué ensuite retrouve sa vitesse normale) ; deux notes de suite relancent l'échantillon sans cumul ; piste sans voix ou invalide ignorée ; compte comme un déclenchement de la piste ; le rythme d'une autre piste n'est pas dérangé par les notes. Texte d'origine : niveau de sortie à la vélocité 127 ; le pitch change la vitesse de lecture comme pour un pas (même résultat qu'un pas au même pitch) ; combinaison avec le pitch de la piste ; bornes ; piste sans voix ou invalide ; pas d'effet sur le séquenceur ni sur le pattern ; compteur de déclenchements.
2. **UI : table touche -> pitch, événement des noires, vue Clavier, entrée par la première touche noire : FAIT** (`test_keyboard`, sanitizers aussi ; 9 casses volontaires du code sur 9 détectées). `core/ui/keyboard_layout.h` (table des pitchs, position des noires, Do central), `InputEvent::Black`, `View::Keyboard`. Entrée par la **première** noire depuis la vue Pattern ou la vue Pistes (curseur sur une piste) : elle ouvre la vue et joue sa note ; ignorée sur la ligne de titre, sur « Ajouter piste » et dans les autres vues ; les 9 autres noires ne font rien hors du clavier. Dans la vue : 14 blanches et 10 noires envoient `Note(piste moteur, 127, pitch)`, les blanches 15 et 16 sont inactives, roue / Valider / Menu / Mode sont ignorés, Play et le BPM fonctionnent, Back rend la main à la vue d'origine (le mode d'édition du pattern est conservé) ; le pattern et la loop ne sont pas modifiés. Dessin : clavier à l'échelle (blanches de 8 pixels, 10 noires de 5 x 20 pixels décalées), Do central marqué d'un point, touche jouée inversée 120 ms (blanche pleine, noire creuse, point en négatif), rangée du bas libre pour l'enregistrement. Vérifié avec le vrai moteur : chaque touche sonne exactement comme la `Note` directe de même pitch. Pas encore branché dans l'émulateur (étape 3). Texte d'origine : toutes les touches (24 jouables, 2 désactivées), commandes envoyées, entrée par la **première** noire depuis la vue Pattern et depuis la vue Pistes (piste sous le curseur seulement ; rien sur la ligne de titre ni sur « Ajouter piste »), la note de l'appui d'entrée est jouée, les neuf autres noires ignorées hors de la vue Clavier, la première ignorée dans les menus et les listes de samples et de loops, Back revient à la vue d'origine, rendu du clavier (nombre de pixels, touche inversée, marque du Do central), les boutons de pas se comportent comme avant ailleurs.
3. **Émulateur : les 10 touches noires, le dessin du clavier, les touches grisées : FAIT, À VÉRIFIER À L'ÉCRAN** (l'émulateur démarre sans erreur, mais je n'ai pas pu cliquer dedans : le dessin et le comportement des touches restent à essayer). Les 10 touches noires sont dessinées au-dessus des 16 boutons de pas, décalées entre deux blanches ; étiquette « Clavier » au-dessus de la première ; hors de la vue Clavier, seule la première est active, les 9 autres sont grisées ; en vue Clavier, les blanches deviennent des touches claires nommées (C D E F G A B...), les deux dernières grisées. Touches de l'ordinateur pour les noires : Z X C V B N , . / ' (les blanches restent 1-8 et Q W E R T Y U I). Fenêtre des contrôles agrandie. Texte d'origine : Essais d'écoute et retouches (taille des touches à l'écran, durée de l'inversion, ordre du menu).

## 7. Limites assumées
* Pas de vélocité à la frappe (toujours 127).
* Pas de son tenu : l'échantillon se joue en une fois, comme une percussion (ce n'est pas un synthé polyphonique).
* Une seule note à la fois par piste : une nouvelle touche relance l'échantillon (le lecteur n'a qu'une voix par piste).
* Qualité du pitch : le lecteur de samples interpole linéairement, donc les notes aiguës (au-dessus de +7 environ) sonnent un peu métalliques.
* Matériel : 10 boutons de plus à câbler (26 au total) ; sur Teensy, une matrice de boutons sera probablement nécessaire. Sans effet sur l'émulateur.

## 8. Décisions validées et points restants
**Validé** : disposition (14 + 2 blanches, 10 noires), Do central = note de base, plage de pitch inchangée (±12), vélocité fixe, entrée par la **première** touche noire seulement (pas de ligne de menu), l'appui d'entrée **joue la note** de cette touche (Do dièse grave), une seule vue « Clavier » (la prise d'enregistrement s'y fera plus tard).
**Reste à suivre à l'usage** : la découvrabilité du raccourci (étiquette « Clavier » sur la première noire dans l'émulateur, inscription près du bouton sur le matériel).
