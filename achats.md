# Achats : panier et prix

État au 10/10/2026. **Décision : option B, Teensy 4.1 nue + puces PSRAM soudées soi-même, tout commandé sur Amazon pour grouper la commande.** Les prix viennent de pages et d'extraits de recherche consultés ce jour-là ; **ils bougent, à revérifier sur la fiche avant de payer**. Ce qui n'a pas pu être lu est marqué *(prix non lu)*. Tâches du backlog : `B-001` (commande) et `B-002` (soudure). Décisions techniques : `plan_teensy.md`.

Périmètre : pas de boutons ni de potentiomètres pour l'instant (l'interface reste sur l'ordinateur, T16 / T3).

## Le panier (Amazon)

| # | Article | Choix proposé | Prix | Fiche |
|---|---|---|---|---|
| 1 | **Teensy 4.1** | version **avec connecteurs soudés** (par le vendeur) : se pose sur une plaque d'essai ; la puce Ethernet n'est pas utilisée mais ne gêne pas | ~46 € | [Amazon.fr B08CTM3279](https://www.amazon.fr/Teensy-4-1-Avec-des-%C3%89pingles/dp/B08CTM3279) (Luxbulk France, expédié par Amazon) |
| 2 | **Deux puces PSRAM 8 Mo** (= 16 Mo) | pack de 5 puces 8 Mo : 2 pour la carte, 3 de rechange en cas de ratage de soudure | *(prix non lu)* | [Amazon.de B08CTMSXL5](https://www.amazon.de/-/en/PSRAM-Chips-Pack-of-5/dp/B08CTMSXL5) (livré en France) |
| 3 | **Module DAC PCM5102A avec jack 3,5 mm soudé** | Aihasd GY-PCM5102 (jack soudé) ou DollaTek PCM5102A | 8 à 10 € | [Aihasd 9,93 €](https://www.amazon.fr/Aihasd-Interface-GY-PCM5102-Raspberry-num%C3%A9rique/dp/B075P33R8T) · [DollaTek 7,99 €](https://www.amazon.fr/DollaTek-Convertisseur-num%C3%A9rique-analogique-num%C3%A9rique-Raspberry/dp/B07PMGGMJF) |
| 4 | **Écran Waveshare 2,42" OLED 128×64, SSD1309, SPI** | version blanche | **21,99 €** | [Amazon.fr B0CL6WMHJN](https://www.amazon.fr/Waveshare-Resolution-Communication-Compatible-Raspberry/dp/B0CL6WMHJN) |
| 5 | **Carte microSD 32 Go** (FAT32) | SanDisk Ultra 32 Go | ~13,50 € | [Amazon.fr B012VKWY7I](https://www.amazon.fr/M%C3%A9moire-microSDHC-SanDisk-Adaptateur-Vitesse/dp/B012VKWY7I) |
| 6 | **Plaque d'essai + câbles Dupont** | kit Miuzei (3 plaques de 830 points + câbles) ou ELEGOO 120 câbles seuls | 14,99 € / 8,99 € | [Miuzei](https://www.amazon.fr/-/en/Miuzei-Breadboard-Dupont-Jumper-Cable/dp/B0GK9Q2SQM) · [ELEGOO](https://www.amazon.fr/Elegoo-Breadboard-Femelle-Longueur-Arduino/dp/B01JD5WCG2) |
| 7 | **Matériel de soudure CMS** (si tu n'en as pas) | flux en gel, étain fin (0,3 à 0,5 mm), tresse à dessouder, pince fine, loupe ; idéalement station à air chaud | *(non recherché)* | voir « La soudure » plus bas |
| 8 | **Câble micro-USB données** | à vérifier si tu en as un | ~5 € | |
| 9 | **Alimentation USB 5 V** | à vérifier si tu en as une (ou l'ordinateur) | | |
| 10 | **Câble jack ↔ enceinte / table** | à confirmer | | |

**Total approximatif des articles 1 à 6 : environ 110 à 125 €** (la Teensy ~46 €, les puces *(prix non lu)*, le DAC ~9 €, l'écran ~22 €, la carte ~13,50 €, plaque et câbles ~9 à 15 €), hors matériel de soudure. Amazon est plus cher que les boutiques spécialisées (Tinytronics vend la même carte 36,50 €, Pi Hut une puce 16 Mo à 8,60 £), mais tu groupes la commande.

## Ce qu'il faut savoir avant de commander

- **Les puces PSRAM.** Sur Amazon, je n'ai trouvé aucune puce de 16 Mo ; il faut donc **deux puces de 8 Mo** (une sur chaque emplacement de la carte). Le pack de 5 est sur **Amazon.de** : un avis signale une puce morte à l'arrivée, d'où l'intérêt des pièces de rechange. La puce seule sur Amazon.es est indisponible. Autre solution sur Amazon.fr : le [« Developer Pack »](https://www.amazon.fr/Teensy-Sans-%C3%89pingles-Developer-Pack/dp/B08P4BZPFH) (Teensy 4.1 avec Ethernet, sans connecteurs, **2 puces PSRAM + kit Ethernet**, *prix non lu*) ; il faudrait souder les connecteurs toi-même, mais tout vient d'une seule fiche.
- **Les vendeurs tiers.** Les fiches Amazon de la Teensy sont remplies de données génériques fausses (« 8 Go de RAM », « 1 To de stockage ») et une annonce « PJRC » à 57,95 € a une description qui ne correspond pas à la carte. Prends une annonce **expédiée par Amazon**, vérifie à la réception que la carte porte bien les marquages Teensy, et garde la facture.
- **L'écran** : le même module fait SPI et I2C, et le choix se fait par des résistances ou des soudures au dos ; **à régler en SPI 4 fils** avant de câbler (voir le [wiki Waveshare](https://www.waveshare.com/wiki/2.42inch_OLED_Module)). Les fiches Amazon.fr disent « SPI / I2C ».
- **Le DAC** : vérifier à la réception que le jack est présent et la position des petits cavaliers du module (FLT, DEMP, XSMT, FMT ; voir `fiche_technique_mat_rielle_composants.md`). Les modules « pHAT » sans jack ne conviennent pas. XSMT sera relié à une broche de la Teensy (T9).
- **La carte microSD** : FAT32, 32 Go au plus (T10).

## La soudure (tâche `B-002`, pour toi)
- **Quoi** : deux puces SOIC-8 sur les petites pastilles **au dos de la Teensy 4.1, sous le lecteur SD**, entre les broches 31-32 et 33-34. **Ne pas utiliser les grandes pastilles.** Une seule puce, sur les petites pastilles, est déjà détectée ; la seconde double la mémoire.
- **Risque** : la soudure CMS est fine ; une erreur peut abîmer la carte (c'est pourquoi on prend des puces de rechange, et pourquoi la carte entière coûte ~46 €).
- **Teensyduino** : les puces de 8 Mo sont reconnues depuis longtemps ; PlatformIO embarque la version 1.62 (T11).
- **Vérification** : un petit programme de test de la mémoire (celui de PJRC pour la PSRAM) lancé après la soudure, avant de monter le reste. Cela fait partie de `B-100`.

## Comparaison avec les autres voies (pour mémoire)
| | Prix de base | Livraison | Soudure |
|---|---|---|---|
| **B sur Amazon (choisie)** | ~46 € + puces | Amazon, un seul colis ou deux (Amazon.de) | oui |
| B en boutiques spécialisées | ~36,50 € + ~9 € | plusieurs commandes, ports non vérifiés | oui |
| A. Protosupplies « Fully Loaded » (DEV-26-P) | 48,95 $ | États-Unis seulement : transitaire (myus.com), taxes à l'arrivée | non |

## Autres sources examinées et écartées (10/10/2026)
- **[SparkFun](https://www.sparkfun.com/teensy-4-1.html)** : Teensy 4.1 authentique avec Ethernet, 31,50 $, sans connecteurs ; frais de gestion 2 $ + port + TVA et dédouanement à l'arrivée (politique citée sur un forum, non confirmée). Au total plus cher que l'Amazon à ~46 €.
- **Revendeurs européens de la carte SparkFun** : [RS Components](https://benl.rs-online.com/web/p/microcontroller-development-tools/2836911) ~70 € TTC, [Electrokit](https://www.electrokit.com/produkt/teensy-4-1/) ~33 € TTC (livraison vers la France non vérifiée), [The Pi Hut](https://thepihut.com/collections/all/products/teensy-4-1) 30,30 £ TTC (douane possible). Sans connecteurs.
- **Décision de l'utilisateur (10/10/2026)** : **Amazon**, puisque le prix final est équivalent et que la commande est groupée.

## À vérifier juste avant de payer
- Le prix du pack de 5 puces PSRAM (Amazon.de) et du Developer Pack, non lus.
- Que l'annonce de la Teensy est bien « expédiée par Amazon » et neuve.
- Le prix actuel de chaque fiche (les extraits datent du moment de la recherche).
- Ce que tu as déjà : câble micro-USB, alimentation, câble jack, fer à souder et accessoires de soudure CMS.
