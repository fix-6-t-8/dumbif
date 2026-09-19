# dumbif — CLAUDE.md

## Objectif du projet

Moteur **très simple** de **fiction interactive**, **en mode texte uniquement**, écrit
en **C**, tournant sur **Zeal 8-bit OS** (CPU Z80).

- `dumbif` est le **successeur** de `~/projects/zeal/zeal-if-compact/zeal-if-compact`,
  mais il est **réécrit intégralement from scratch**. L'ancien code n'est **pas** à
  réutiliser ni à porter ; il ne sert que de référence d'intention si besoin.
- État actuel (2026-09-19) : **premier jalon fonctionnel**, testé OK par l'utilisateur.
  `src/main.c` reçoit le chemin du `.dat` en argument, vérifie sa longueur, l'ouvre
  (`open`, `O_RDONLY`), appelle `read10(fd)` puis ferme le fichier.
  `read10()` (`src/scene.c`, prototype dans `src/scene.h`) lit et affiche les
  10 premiers octets du fichier. C'est du code **câblé de test** : dans le découpage
  validé, la lecture du `.dat` (`read`/`seek`) relève de `story.c`, pas de `scene.c`.
  `story.c/.h`, `input.c/.h` et `dumbif.h` existent mais sont **vides**.
- Le format de scénario, le parser, le moteur et la boucle de jeu restent **à définir
  avec l'utilisateur** — ne rien présupposer. Documents de travail à la racine :
  `scene_design.md` (format runtime), `file-management.md` (gestion des fichiers).
- Cible de test : **émulateur natif** Zeal (voir « Exécuter / tester »).

## Repères de l'écosystème (chemins locaux)

| Élément | Chemin |
| --- | --- |
| Zeal 8-bit OS (référence lecture) | `~/projects/zeal/Zeal-8-bit-OS` |
| Zeal 8-bit OS (copie utilisée par le conteneur ZDE) | `~/projects/zeal/zeal-dev-environment/home/Zeal-8-bit-OS` |
| ZDE (environnement de build) | `~/projects/zeal/zeal-dev-environment` (script `zde`, déjà dans le `PATH`) |
| Émulateur natif — sources | `~/projects/zeal/Zeal-NativeEmulator` |
| Émulateur natif — binaire compilé | `~/projects/zeal/Zeal-NativeEmulator/build/zeal-native` |
| ROM par défaut | `~/.zeal8bit/roms/default.img` → `Zeal-NativeEmulator/build/roms/os_with_romdisk.img` |
| Headers C du noyau (API) | `Zeal-8-bit-OS/kernel_headers/sdcc/include/` |
| Exemples C officiels | `Zeal-8-bit-OS/kernel_headers/examples/sdcc/` |

Les deux copies de `Zeal-8-bit-OS` sont au même commit (`9bd3296`) et leurs
`kernel_headers/` sont identiques (vérifié). Lire l'une ou l'autre revient au même.

> Attention : il n'existe **pas** de `~/projects/zeal-projects/Zeal-Native-Emulator`.
> Le bon chemin est `~/projects/zeal/Zeal-NativeEmulator`.

## Construire

Le projet est un projet **CMake + SDCC**, généré depuis le template ZDE `zgdk`.
`CMakeLists.txt:22` fait `include($ENV{ZOS_PATH}/cmake/zos_init.cmake)`.

**Chaque `.c` doit être listé dans `add_executable`** (`CMakeLists.txt:32-35`,
actuellement `src/main.c` et `src/scene.c`, séparés par des espaces/retours à la
ligne, jamais par des virgules). Un `.c` absent n'est pas compilé et le lien échoue
(`?ASlink-Warning-Undefined Global '_<fonction>'`). Le `make` reconfigure CMake
tout seul quand `CMakeLists.txt` change.

Le build de référence se fait **dans le conteneur ZDE** (confirmé par
`build/CMakeCache.txt` : `CMAKE_HOME_DIRECTORY=/src`,
`CMAKE_TOOLCHAIN_FILE=/home/zeal8bit/Zeal-8-bit-OS/cmake/sdcc_toolchain.cmake`) :

```sh
cd ~/projects/zeal-projects/dumbif
zde cmake            # configure build/ si besoin, puis compile
```

- **Ne pas utiliser `zde make`** : le projet n'a pas de `Makefile` (le README du
  template est trompeur sur ce point).
- Pour compiler sur l'hôte sans conteneur, il faut d'abord `eval "$(zde activate)"` :
  `ZOS_PATH` est **vide** dans un shell nu, et le build échoue sans lui.
- Sorties dans `bin/` (`CMAKE_RUNTIME_OUTPUT_DIRECTORY`) : `dumbif.ihx` (Intel Hex),
  `dumbif.bin` (binaire brut, **c'est le format accepté par le noyau**), `dumbif.map`.
  Le `.bin` est produit par `zos_add_outputs()` (`Zeal-8-bit-OS/cmake/zos_init.cmake:35`).

Toolchain (`Zeal-8-bit-OS/cmake/sdcc_toolchain.cmake:28-40`) : `sdcc -mz80`,
link à **`0x4000`** (`-b _HEADER=0x4000`), crt0 `kernel_headers/sdcc/bin/zos_crt0.rel`.

## Exécuter / tester

L'option `-u/--uprog` **remplace le programme `init` du romdisk** de la ROM par notre
binaire ; l'offset est déduit automatiquement de la position de l'OS dans le flash
(`Zeal-NativeEmulator/hw/flash.c:295-329`). Le programme démarre donc au boot.

```sh
# GUI
~/projects/zeal/Zeal-NativeEmulator/build/zeal-native \
    -u ~/projects/zeal-projects/dumbif/bin/dumbif.bin

# Sans fenêtre (utile pour une vérification automatisée : sortie UART -> stdout)
~/projects/zeal/Zeal-NativeEmulator/build/zeal-native -n \
    -u ~/projects/zeal-projects/dumbif/bin/dumbif.bin
```

Options utiles (`zeal-native --help`, vérifié sur le binaire local) :
`-r/--rom`, `-u/--uprog <file>[,<addr hex>]`, `-t/--tf <img>`, `-H/--hostfs <path>`,
`-m/--map <file>` (symboles pour le debugger), `-g/--debug`, `-b/--brk <addr|sym>`,
`-n/--headless [tstates]`, `-q/--no-reset`, `-v` (répétable).

- Sans `-r`, la ROM prise est `~/.zeal8bit/roms/default.img` (`hw/flash.c:412-425`).
- `-r` **peut réécrire l'image ROM** en fin d'exécution si le flash a été modifié
  (`hw/flash.c:467+`). Sans `-r`, aucune écriture. **Ne jamais pointer `-r` sur
  `default.img` sans raison explicite.**
- `zde emu` lance le **Web** emulator (service conteneurisé), pas l'émulateur natif.
  Ce n'est pas la cible de test de ce projet.
- **HostFS** (`-H`) est marqué **expérimental** et vaut `default n` dans le Kconfig
  (`Zeal-8-bit-OS/target/zeal8bit/Kconfig:76-79`), **mais la ROM que nous utilisons
  l'active** : `CONFIG_ENABLE_EMULATION_HOSTFS=y`
  (`Zeal-NativeEmulator/build/roms/os.conf:27`). Le dossier hôte est alors monté sur
  **`H:`** (`Zeal-8-bit-OS/target/zeal8bit/romdisk.asm:31`), en lecture **et écriture**
  (`Zeal-NativeEmulator/hw/hostfs.c:344-346,622,761-774`). N'existe **que** sous
  émulateur : revérifier la config de toute autre ROM avant de s'appuyer dessus.

## Contraintes de la plateforme (à respecter dans tout design)

Source : `Zeal-8-bit-OS/README.md` et `kernel_headers/sdcc/include/`.

- **Mono-thread**, pas d'ordonnanceur. Le programme a tout le CPU.
- Programme utilisateur linké à `0x4000`, **48 Ko maximum** (pages 1 à 3).
- **Aucun buffer passé à un syscall ne doit traverser une frontière de page
  virtuelle de 16 Ko.** Contrainte formelle du noyau.
- `FILENAME_LEN_MAX = 16`, `PATH_MAX = 128` (`zos_vfs.h:35,40`).
- Disques nommés par lettre (`A:` … `Z:`). FS réellement implémentés : **`rawtable`**
  (lecture seule, **sans répertoires** — `kernel/fs/rawtable.asm:5-9,494-498`) et
  **ZealFS** (lecture/écriture, avec répertoires ; notre ROM est en **v2**,
  `os.conf:54-56`), plus **HostFS** sous émulateur. **FAT16 n'est pas implémenté** :
  la valeur existe dans l'énumération (`include/vfs_h.asm:16`) mais tous les points
  d'entrée renvoient `ERR_NOT_SUPPORTED` (`kernel/disks.asm:1381-1392`).
- Pas de MMU côté programme sauf usage explicite de `map`/`palloc`/`pfree`.
- Cible 8 bits : privilégier `uint8_t`/`uint16_t`, éviter `int32_t`, **pas de flottants**,
  éviter l'allocation dynamique et la récursion profonde (pile Z80 réduite).

### Supports de masse et lettres de disque

Chaque driver teste son support au démarrage et ne monte sa lettre que s'il répond.
« Driver dans la ROM » ≠ « disque monté ».

| Lettre | Support | FS | Écriture | Driver dans notre ROM | Alimenté sous émulateur par |
| --- | --- | --- | --- | --- | --- |
| `A:` | romdisk (en ROM) | rawtable | **non** | oui (`os.conf:60`) | la ROM elle-même (`-r`, ou `default.img`) |
| `B:` | EEPROM I²C | ZealFS | oui | oui (compilé sans condition) | `-e/--eeprom` (AT24C512, 64 Ko) |
| `C:` | CompactFlash | rawtable | **non** | oui (`os.conf:18`) | `-C/--cf` |
| `T:` | carte TF / microSD | ZealFS | oui | oui (`os.conf:17`) | `-t/--tf` |
| `H:` | dossier du PC hôte | HostFS | oui | oui (`os.conf:27`) | `-H/--hostfs` |

Lettres fixées en dur dans les drivers : `romdisk.asm:31,37-43` (disque par défaut =
`A:`, `include/disks_h.asm:12`), `eeprom.asm:16`, `compactflash.asm:16`, `tf.asm:18`.

- **Seuls `B:`, `T:` et `H:` sont inscriptibles** et acceptent des répertoires. Rien ne
  peut être écrit ni rangé dans un dossier sur `A:` ni `C:`.
- **`B:` exige une image ZealFS**, pas des données brutes : au boot le noyau lit les deux
  premiers octets de l'EEPROM (I²C `0x50`) et exige `'Z'` + le numéro de version ZealFS,
  sinon « EEPROM not formatted » et pas de montage (`target/zeal8bit/eeprom.asm:36-52`).
  La version doit être celle compilée dans le noyau — **v2** pour notre ROM.
- L'option **`-C/--cf` existe mais n'est pas listée dans `--help`**
  (`Zeal-NativeEmulator/utils/config.c:151,192`).
- Images ZealFS fabriquées sur PC avec le driver FUSE `ZealFS` (`zeal-dev-environment/
  home/ZealFS`, binaire `build/zealfs` — musl + libfuse3, **à exécuter dans le
  conteneur ZDE**) : il crée l'image, la monte comme un dossier, on y copie les fichiers,
  puis `umount` écrit tout dans l'image.

### API noyau disponible (C / SDCC)

Headers : `zos_sys.h`, `zos_vfs.h`, `zos_video.h`, `zos_keyboard.h`, `zos_time.h`,
`zos_serial.h`, `zos_mouse.h`, `zos_errors.h`.

- E/S fichiers : `open`, `read`, `write`, `seek`, `close`, `stat`, `dstat`,
  `opendir`, `readdir`, `mkdir`, `chdir`, `curdir`, `rm`, `mount`, `dup`, `swap`.
- Devices standards : `DEV_STDOUT = 0`, `DEV_STDIN = 1`.
- **Saisie clavier** : `ioctl` sur `DEV_STDIN`, commande `KB_CMD_SET_MODE`
  (`zos_keyboard.h`). Modes : `KB_MODE_RAW`, `KB_MODE_COOKED` (bufferisé, flush sur
  `\n` — le mode naturel pour une saisie de commande en fiction interactive),
  `KB_MODE_HALFCOOKED`. Lecture bloquante via `KB_READ_BLOCK`.
- Convention d'appel imposée : `__sdcccall(1)`, SDCC **4.2+** requis (`zos_vfs.h:16-19`).

## Conventions de travail sur ce projet

- **Mode texte strictement** : pas de sprites, tilemaps, palettes ni mode graphique
  de la carte vidéo. Le template `zgdk` en apporte (`assets/`), c'est **hors scope**.
- **KISS avant tout.** C'est un moteur « nano » : le critère de choix est la
  simplicité et la robustesse, pas la généricité. Toute abstraction, couche de
  configuration ou machinerie « au cas où » est un défaut à signaler et retirer.
- **Ne rien affirmer sur le comportement de l'OS, de l'émulateur ou du projet sans
  l'avoir lu dans le code**, et citer les références `fichier:ligne`.
- **Design d'abord, code ensuite** : présenter l'analyse et la recommandation, attendre
  la validation de l'utilisateur avant d'écrire ou de modifier des fichiers.
- Réponses et échanges **en français** ; identifiants, commentaires de code et
  messages de commit selon la convention que l'utilisateur fixera.
- **Versionnage** (depuis le 2026-09-19) : dépôt git, branche `main`, remote `origin`
  en **HTTPS** `https://github.com/fix-6-t-8/dumbif.git` (la clé SSH locale n'est pas
  enregistrée sur GitHub ; les identifiants passent par `credential.helper store`).
  `main` suit `origin/main`. `.gitignore` exclut notamment `bin/`, `build/`,
  `.claude/memory/` et `.claude/settings.local.json`. Commits et push : faits par
  l'utilisateur.

## Arborescence

```
dumbif/
├── CMakeLists.txt      # CMake + SDCC, cible `dumbif` (sources : main.c, scene.c)
├── src/
│   ├── main.c          # argument -> open -> read10() -> close
│   ├── scene.c/.h      # read10() : lit et affiche 10 octets (test câblé)
│   ├── story.c/.h      # vides
│   ├── input.c/.h      # vides
│   └── dumbif.h        # vide
├── story/              # dumb.if, dumb.dat (scénario de test)
├── bin/                # sorties de build : .bin / .ihx / .map (ignoré par git)
├── build/              # répertoire CMake, généré dans le conteneur ZDE (ignoré)
├── scene_design.md     # format runtime des scènes
├── file-management.md  # synthèse gestion des fichiers sous Zeal
├── CHANGELOG           # 1.0.0 - 2026-08-23 : création du projet
├── README.md           # texte du template, à réécrire
├── dumbif.txt          # page de manuel (roff), texte du template
├── LICENSE
└── .gitignore
```

## Prochaine étape

Aucune prochaine étape n'a été définie lors de la session du 2026-09-19. Seule
remarque en suspens : la lecture du `.dat` (`read10`) devra quitter `scene.c` pour
`story.c`, conformément au découpage validé.
