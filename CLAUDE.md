# Drum machine Teensy 4.1 : façon de travailler

Boîte à rythmes à échantillons (C++ portable dans `core/`, plateforme PC dans `platform/desktop/`, Teensy à venir). `README.md` donne la structure et les commandes de build et de test.

Ce fichier décrit le **flux de travail voulu**, pas un protocole à appliquer à la lettre. On peut en sortir quand la situation s'y prête ; le bon sens et le dialogue priment.

## Le flux
- **Une nouvelle fonctionnalité commence par la discussion, puis un plan.** On en parle, on écrit `plan_XXX.md` (décisions, découpage en tâches), les tâches vont dans `backlog.md`, et on code ensuite.
- **On implémente tâche par tâche**, en reparlant de la tâche avant de la commencer : c'est l'occasion d'en discuter les détails et de trancher ce qui ne l'est pas.
- **Discuter avant d'agir.** Une remarque, un doute, une idée ou une question de l'utilisateur en cours de route est d'abord un **sujet de discussion**, pas une instruction de modifier le code. Je réponds, je pose ma question si un point doit être tranché, et quand il y a un choix à faire, je donne mon avis. Avant de modifier quoi que ce soit, je dis ce que je m'apprête à faire et j'attends un accord clair. **Une réponse à ma question n'est pas une autorisation d'implémenter.** Dans le doute entre une question et un ordre, c'est une question.
- Les ordres précis et sans ambiguïté (« renomme X en Y », « supprime cette ligne ») s'exécutent directement.

## Le suivi
- **`backlog.md` suit l'avancement, et je le mets à jour avec le travail, dans le même tour** : une tâche qui passe à `doing` ou `done`, qui change, ou qu'on découvre. Le mode d'emploi est en tête du fichier.
- **Une tâche est `done` quand son critère « terminé quand » est vérifié** (tests, build), et je dis ce qui a été vérifié.
- **Les décisions vivent dans les plans** (`plan_*.md`, `plan_teensy.md` pour la Teensy), pas dans le backlog. Quand une décision change, le plan est mis à jour, pas seulement le code.

## Règles du projet
- Conversation, plans (`plan_*.md`), `backlog.md` et commentaires du code en français (c'est déjà la convention du dépôt). **Le code lui-même est en anglais** : identifiants, noms de fichiers, de tests, de champs, de commandes et messages de commit. Seuls les textes affichés à l'utilisateur de l'appareil suivent ce qui existe déjà. Ne pas expliquer plus que ce qui est demandé.
- Ne pas commiter : l'utilisateur le fait lui-même.
- Demander avant d'installer quoi que ce soit (par exemple `brew install platformio`).
- `core/` reste portable : pas d'allocation dynamique ni de STL dans le chemin audio, seulement `cstdint`, `cmath`, `cstring`, `atomic`, `cstdio`.
- Tout slider ImGui de l'émulateur utilise `wheel_slider.h`.
