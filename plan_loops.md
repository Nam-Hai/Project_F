# Plan : sauvegarde des loops (profil)

Objectif : pouvoir sauvegarder les loops créées dans un **profil** (une collection de loops) et les rejouer rapidement. Le code de format est dans `core/` (portable, sans allocation) ; seul le stockage (fichier PC, carte SD Teensy) dépend de la plateforme.

## 1. Décisions

* **Profil = une collection de loops.** Pour l'instant **un seul profil**, « Default » : pas de changement ni de création de profil dans l'UI. La structure de fichier est prête pour en avoir plusieurs plus tard.
* **Loop = BPM + pistes.** Pour chaque piste : le sample (référencé par son **nom**, pas l'audio), le gain, le pitch et le pattern de 16 pas. Les samples restent dans `samples/` (PC) ou sur la carte SD.
* **Pas de nommage libre pour l'instant** : les loops sont des emplacements numérotés (« Loop 1, 2, 3… ») avec un résumé (BPM, noms des pistes). Le renommage viendra plus tard.
* **Changement de loop immédiat** (on remplace tout) dans la première version. Le changement à la fin de la mesure viendra plus tard.
* **Format JSON** (et non un format texte maison), lu avec **jsmn** : le découpage syntaxique est générique et écrit une seule fois ; ajouter une fonctionnalité plus tard = lire une clé de plus, sans toucher au parseur.
* **CRC repoussé** : la place est prévue dans le format, mais il n'est pas implémenté. Un fichier tronqué est déjà détecté par le JSON lui-même ; ce que le CRC détecterait en plus, ce sont des modifications silencieuses au milieu du fichier (voir §5).

## 2. Format du fichier de profil
JSON standard, produit par la machine, lisible et modifiable à la main (pas de commentaires).

```json
{
  "format": "dmp",
  "version": 1,
  "profile": "Default",
  "samples": [
    "Kick1.wav",
    "Snare1.wav"
  ],
  "loops": [
    {
      "bpm": 120.0,
      "tracks": [
        { "sample": 0, "gain": 1.0, "pitch": 1.0, "pattern": "x...x...x...x..." },
        { "sample": 1, "gain": 0.8, "pitch": 1.0, "pattern": "....x.......x..." }
      ]
    }
  ]
}
```

* **Table de samples** : le profil liste chaque sample une fois ; les pistes y renvoient par numéro. Un sample absent est détecté à un seul endroit.
* **Version** (`"version": 1`) : un fichier d'une version plus récente que le code est refusé avec un message clair.
* **Extensible** : les clés inconnues sont ignorées, à tous les niveaux. Les champs absents prennent une valeur par défaut. Les changements de structure (pas seulement des champs en plus) demandent une nouvelle version et un lecteur de migration.
* **Écriture sécurisée** (à faire avec le stockage) : écrire dans un fichier temporaire, puis le renommer, pour qu'une coupure de courant ne détruise pas le profil.
* **jsmn** (MIT, un seul en-tête, `third_party/jsmn/`) : tokenizer sans allocation, mode strict. Il ne vérifie pas les virgules ni les deux-points : notre couche de lecture contrôle donc la structure et borne tous ses accès. Profil plein (32 loops x 16 pistes) : environ 45 Ko de JSON et environ 115 Ko de RAM pendant la lecture (7168 jetons, dimensionnés pour le pire cas avec les listes de vélocité et de pitch par pas : voir `plan_step_params.md`).
* **Nombres** lus et écrits indépendamment de la langue du système (jamais de virgule décimale).
* **Limites** (tableaux fixes) : **12 loops** (réduit de 32 le 10/10/2026 pour économiser la mémoire : voir plan_record.md),  64 samples dans la table, 16 pistes par loop, noms de sample de 47 caractères, nom de profil de 23.

## 3. Interface (écran 128x64)

**Bandeau du haut** : le titre de la vue est en haut à **gauche**, coupé à 16 caractères s'il est trop long. En haut à **droite** : le BPM et l'état lecture/pause (`120 ▶`) dans toutes les vues. **Seule la vue Pistes** y ajoute le numéro de la loop courante et l'indicateur de modification : `L3* 120 ▶` (`*` si elle a des modifications non sauvegardées ; `L*` pour une loop jamais sauvegardée). Le titre de cette vue est court (« Pistes »), donc ça tient dans les 76 px restants. La vue d'ajout et de remplacement de sample s'appelle « Samples » dans les deux cas.

**Menu de la loop**
* Dans la vue **Pistes**, en montant tout en haut avec la roue, la **ligne de titre devient sélectionnable**. Le bouton **Menu** sur cette ligne ouvre le menu de la loop : « Charger une loop », « Sauvegarder la loop ».
* **Sauvegarder la loop** : écrase l'emplacement d'où elle vient (avec confirmation, comme la suppression de piste) ; une loop neuve prend le prochain emplacement libre. Profil plein (32 loops) : refus avec un message, il faut d'abord supprimer une loop.

**Vue Loops** (« Charger une loop »)
* La liste des loops du profil, avec un résumé (BPM, nombre de pistes). Valider charge la loop ; Back annule.
* **La dernière ligne est « + Nouvelle loop »** (même principe que « + Ajouter piste ») : elle vide la machine pour repartir de zéro.
* **Supprimer une loop** : le bouton **Menu** sur une loop de la liste ouvre un menu avec « Supprimer » (avec confirmation, comme pour les pistes).
* Charger ou créer une nouvelle loop alors que la loop courante est **modifiée** : confirmation « Quitter sans sauvegarder ? » (le premier Valider change le libellé, le second exécute ; bouger la roue ou Back annule).
* Un chargement remplace toutes les pistes. Si un sample est introuvable, la piste est sautée et l'écran l'indique (« 1 sample manquant »).

**Démarrage** : la machine démarre sur une **loop vide** (`L*`). `default_pattern.h` ne sert plus qu'à l'exécutable sans interface (`drum_machine`).

## 4. Étapes
1. **Modèle et format (`core/profile/`) : FAIT.** `ProfileData` / `LoopData` / `TrackData` (tableaux fixes, `add_sample` / `find_sample`), `ProfileCodec::parse` (jsmn strict + couche de lecture bornée, statuts `Ok / Truncated / InvalidJson / NotAProfile / TooNew / TooBig / BadField` avec messages en français) et `ProfileCodec::write` (JSON indenté, échappement des noms, refus des données invalides). Testé (`test_profile`) : aller-retour, écriture stable, profil vide, fichier écrit à la main (clés inconnues, valeurs par défaut), nombres, fichier coupé à de nombreux endroits, JSON invalide, mauvais format, version trop récente, champs invalides, limites des tableaux, profil plein, imbrication profonde, données invalides à l'écriture, et **4 000 fichiers altérés au hasard** sans aucun plantage. Vérifié aussi sous AddressSanitizer et UBSan.
2. **Moteur : FAIT.** `Command::SetTrackPattern` (les 16 pas d'une piste en un message, dans `length`) et `Command::ClearAll` (toutes les pistes : voix, sample rendu à l'UI, pattern, gain, pitch). Tests dans `test_live`.
3. **Stockage : FAIT.** Interface `ProfileStore` (`core/profile/profile_store.h` : `load` -> `Loaded / Missing / Failed`, `save`, `last_error`) et `FileProfileStore` (PC, `profiles/default.json`, créé à la première sauvegarde). Écriture sécurisée : `.tmp` écrit, ancienne version copiée en `.bak`, renommage. Testé (`test_profile_store`) : fichier absent, aller-retour, `.bak`, échec d'écriture sans toucher à l'ancien fichier, `.tmp` orphelin ignoré, fichier abîmé conservé en `.bak`, données incohérentes refusées. `ProfileData::compact_samples()` (la table de samples ne garde que ce que les loops utilisent) et `remove_loop()`.
4. **UI, modèle et chargement : FAIT.** `UiController` exporte son état en `LoopData` (nom de fichier, gain et pitch conservés par piste) et reconstitue une loop. Chargement : samples retrouvés par nom (`SampleLibrary::find`) et chargés tout de suite, pistes manquantes sautées avec message (« 1 sample manquant »), puis commandes envoyées par petits paquets dans `update()` (`ClearAll`, `SetBpm`, puis sample / gain / pitch / pattern par piste) ; les entrées sont ignorées pendant ce temps (une fraction de seconde). Indicateur de modification (`dirty`) : pas, BPM, ajout / remplacement / suppression de piste.
5. **UI, menus et vues : FAIT.** Ligne de titre sélectionnable (monter au-dessus de la première piste) : **Menu** = menu de la loop (Charger / Sauvegarder / Annuler), **Valider** = liste des loops directement. Vue **Loops** (résumé BPM et nombre de pistes, `>` devant la loop courante, « + Nouvelle loop » en dernière ligne, **Menu** sur une loop = Supprimer). Sauvegarde : confirmation « Ecraser ? » si la loop a déjà un emplacement ; refus si le profil est plein (« Profil plein ») ou si la table de samples est pleine (« Trop de samples »). Charger ou créer une loop alors que la courante est modifiée : « Quitter sans sauv. ? ». Si une écriture échoue, le profil de l'UI revient à son état d'avant. En-tête de la vue Pistes : `L3* 120 ▶`. Testé (`test_loops`, 100+ vérifications) et en intégration (`test_integration` : une loop construite avec l'UI, sauvegardée dans un vrai fichier, rechargée dans une machine neuve avec le vrai moteur : audio identique à l'échantillon près ; buffers libérés après « Nouvelle loop » ; sample disparu).
6. **Démarrage : FAIT.** L'émulateur démarre sur une loop vide, à l'arrêt, et lit `profiles/default.json` ; `default_pattern.h` ne sert plus qu'à `drum_machine` (sans interface).
7. **Plus tard** : plusieurs profils (changement et création), renommage des loops, changement de loop à la fin de la mesure, reprise automatique de la session, CRC, hash des samples, carte SD sur Teensy, repositionnement du potard BPM après un chargement.

## 5. Points ouverts
* **Potard BPM après un chargement** : le potard simulé garde sa position ; si la loop chargée a un autre BPM, toucher le potard fait sauter le tempo. Sur le vrai matériel, il faudra un « rattrapage » (le potard n'agit qu'une fois qu'il a recroisé la valeur courante).
* **Reprise de session / sauvegarde automatique** : pas prévue (la machine démarre vide). Sur Teensy, il faudra écrire à l'arrêt de la lecture ou après un délai pour ménager la carte SD.
* **CRC** : repoussé. Il détecterait une modification silencieuse du contenu (4 000 altérations aléatoires du fichier de test en laissent 174 valides, par exemple un chiffre changé). Il devra rester facultatif pour ne pas interdire l'édition à la main.
* **Changement de loop à la fin de la mesure**, **renommage**, **plusieurs profils** : voir l'étape 7.

## Échec d'écriture : pas de copie de sécurité
L'UI ne garde plus de copie du profil pour revenir en arrière (115 Ko de moins). Le profil en mémoire peut avoir une longueur d'avance sur le disque : si l'écriture échoue, la loop reste à son emplacement, `current_loop_` pointe dessus et l'indicateur garde son `*` ; un nouvel essai réécrit tout le profil (avec la confirmation « Ecraser ? » habituelle), sans doublon. La table de samples est nettoyée **avant** d'y ajouter les samples de la loop. Une suppression de loop dont l'écriture échoue retire la loop de la mémoire ; elle disparaît du disque à la prochaine sauvegarde réussie. Si on éteint sans réessayer, la modification est perdue.

**Mise à jour (10/10/2026) : le profil est maintenant binaire.** `FileProfileStore` lit et écrit le format binaire de `core/profile/binary_codec.h` (`profiles/default.dmp`) ; le JSON ne sert plus que de format d'échange lisible (`ProfileCodec`, côté PC, outil `profile_tool` à venir, B-012). Voir `plan_teensy.md` T7.

**Mise à jour (11/10/2026) : entrer dans la liste des loops quitte la loop courante.** Dès qu'on entre dans la vue Loops, le moteur est vidé (`ClearAll`) et l'écran aussi ; « Quitter sans sauv. ? » est demandé avant si la loop est modifiée ; il n'y a plus de retour depuis la liste (on choisit une loop ou « + Nouvelle loop »). Voir `plan_teensy.md` T6 (3b) et le backlog B-026.

**Mise à jour (11/10/2026) : une piste dont le sample manque est gardée.** Elle s'ouvre silencieuse, avec un « ? » dans la liste des pistes, garde son nom de sample d'origine (à la sauvegarde aussi) et reste modifiable ; « Changer le sample » lui donne un son. Une loop qui ne tient pas dans les 16 Mo des samples est listée mais son ouverture est refusée (« Memoire pleine »). Voir `plan_teensy.md` T6 et le backlog B-027 / B-028.
