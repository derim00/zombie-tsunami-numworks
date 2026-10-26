# Zombie Tsunami pour NumWorks

Réécriture complète en C99 (API EADK, application `.nwa`).
Le zip fourni (`ZombieTsunami-Vita`) est un *loader* PS Vita qui exécute le binaire Android :
il ne contient pas le code du jeu. Le gameplay a donc été refait à partir des captures et de la
boucle de jeu d'origine.

## Compiler / installer

```
make build     # produit output/app.nwa
make run       # installe sur la calculatrice branchée en USB (nwlink)
```
Prérequis : `arm-none-eabi-gcc` et Node.js (pour `npx nwlink`), comme pour l'exemple officiel
`epsilon-sample-app-c`.

## Contrôles

| Touche | Action |
|---|---|
| OK / EXE / HAUT | Sauter (maintenir = saut plus haut) |
| HAUT / BAS | Naviguer dans les menus (menu principal, pause, fin de partie) |
| OK / EXE | Valider un bouton |
| BACK | En jeu : pause. Dans le menu principal : quitter |

## Règles

- La horde court seule. Touchez un humain pour le convertir (+1 zombie, +10 points).
- Trou : les zombies au sol qui passent dessus tombent. Tonneaux, caisses, pointes : chaque
  zombie qui les percute meurt. Les zombies sautent en vague, comme dans le jeu d'origine.
- Voiture : le chiffre dans la bulle est le nombre de zombies requis. Vert = votre horde suffit,
  la voiture se retourne (+25 et un zombie). Rouge = sautez par-dessus.
- Plus la horde est grosse, plus une erreur coûte cher (les zombies cachés disparaissent
  proportionnellement). La vitesse augmente avec la distance.
- Humains sur plateformes et pièces en l'air : il faut sauter pour les attraper.
- Fin de partie quand il ne reste plus aucun zombie. Le record est conservé tant que
  l'application reste ouverte (l'API EADK n'offre pas de stockage persistant).

## Technique

- Menu principal (Jouer / Missions / Records), pause, écran de fin avec étagère de cerveaux, 3 missions de session.
- Décor qui passe du jour à la nuit selon la distance, lampadaires, fissures, zombies variés (peau, chapeaux).
- Rendu par bandes de 24 lignes (buffer de 15 Ko) : pas de framebuffer complet, pas de scintillement.
- 30 images/s à pas fixe, aucun float, aucun malloc, aucune dépendance libc ; tout l'état est
  initialisé explicitement au démarrage.
- Empreinte : ~13 Ko de code, ~17 Ko de RAM statique.
- `tests/host_test.c` : simulateur de l'API EADK sur PC (ASAN/UBSAN) avec bot, fuzz et captures.
