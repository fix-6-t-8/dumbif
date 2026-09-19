# Gestion des fichiers sur Zeal 8-bit OS

Synthèse des bonnes pratiques d'E/S fichier pour `dumbif`. Toutes les références
`fichier:ligne` pointent vers `~/projects/zeal/Zeal-8-bit-OS/`.

---

## 1. Il n'y a pas de `fopen`

`fopen`, `FILE`, `fclose`, `fread`, `fprintf` **n'existent pas** :

- Absents du `stdio.h` de SDCC 4.4.0 (`/opt/sdcc/share/sdcc/include/stdio.h`) —
  la partie « flux » de `<stdio.h>` (ISO C 7.21.5) n'y est pas, faute d'OS.
- Absents de `z80.lib` (`grep -c "_fopen"` → 0).

L'accès fichier passe par le **VFS du noyau**, avec une API bas niveau à la POSIX.
Documentation de référence : les commentaires Doxygen de
`kernel_headers/sdcc/include/zos_vfs.h`. Vue d'ensemble : `docs/system-calls.md`.

```c
#include <zos_vfs.h>
#include <zos_errors.h>
```

| Fonction | Signature | Ligne |
| --- | --- | --- |
| `open` | `zos_dev_t open(const char* name, uint8_t flags)` | `zos_vfs.h:173` |
| `read` | `zos_err_t read(zos_dev_t dev, void* buf, uint16_t* size)` | `zos_vfs.h:141` |
| `write` | `zos_err_t write(zos_dev_t dev, const void* buf, uint16_t* size)` | `zos_vfs.h:155` |
| `seek` | `zos_err_t seek(zos_dev_t dev, int32_t* offset, zos_whence_t whence)` | `zos_vfs.h:234` |
| `close` | `zos_err_t close(zos_dev_t dev)` | `zos_vfs.h:185` |

---

## 2. `open` : flags et convention d'erreur

Les flags sont des **bits ORables**, pas une chaîne de mode (`zos_vfs.h:55-63`) :

| Flag | Valeur | Note |
| --- | --- | --- |
| `O_RDONLY` | 0 | lecture seule |
| `O_WRONLY` | 1 | écriture seule |
| `O_RDWR` | 2 | curseur partagé ; l'écriture écrase |
| `O_TRUNC` | 4 | taille remise à 0 avant toute opération |
| `O_APPEND` | 8 | curseur en fin avant chaque écriture |
| `O_CREAT` | 16 | crée le fichier s'il n'existe pas |
| `O_NONBLOCK` | 32 | drivers uniquement, pas les fichiers |

**Le retour n'est pas un pointeur** mais un `int8_t` (`zos_dev_t`, `zos_vfs.h:74`).
Convention documentée en `zos_vfs.h:169-171` : numéro de device positif en cas de
succès, **opposé du code d'erreur** en cas d'échec. On teste donc le signe.

```c
zos_dev_t fd = open("A:/story.dat", O_RDONLY);
if (fd < 0) {
    zos_err_t err = (zos_err_t)(uint8_t)(-fd);   /* ERR_NO_SUCH_ENTRY, etc. */
    /* ... */
}
```

Le double cast `(uint8_t)(-fd)` n'est pas superflu : `fd` est un `int8_t`, promu en
`int` avant la négation ; le cast évite toute surprise d'extension de signe.

Codes utiles (`zos_errors.h`) : `ERR_NO_SUCH_ENTRY` (4), `ERR_READ_ONLY` (18),
`ERR_INVALID_PATH` (11), `ERR_PATH_TOO_LONG` (14), `ERR_ALREADY_OPENED` (16).

### Chemins et limites

Trois formes acceptées (`zos_vfs.h:159-164`) : relatif (`file.dat`), absolu au
disque courant (`/path/file.dat`), absolu système (`A:/path/file.dat`).
Un nom préfixé par `#` désigne un **driver**, pas un fichier (`#GPIO`).

- `FILENAME_LEN_MAX = 16`, `PATH_MAX = 128` (`zos_vfs.h:35,40`).
- Écriture possible **uniquement** sur `B:`, `T:` et `H:`. `A:` (romdisk) et `C:`
  sont en lecture seule → `O_CREAT`/`O_WRONLY` y renvoient `ERR_READ_ONLY`.
- Fermer dès que possible : le nombre de devices ouverts est limité par le noyau
  (`zos_vfs.h:178-180`).

---

## 3. La contrainte de page de 16 Ko — le piège n°1

Formalisée en `zos_vfs.h:118-129` : **aucun buffer passé à un syscall ne doit
franchir une frontière de page virtuelle de 16 Ko**, ni dépasser la taille d'une
page. Un buffer commençant à `0x7F00` ne peut pas faire plus de 256 octets, sinon
il déborde sur la page qui commence à `0x8000`.

On ne contrôle pas où le linker place les buffers. La parade robuste, reprise de
`zeal-ed` (`src/platform/zeal/ed_zeal_io.c`), tient en six instructions :

```c
static uint16_t page_amount(const void* buffer, uint16_t requested)
{
    uint16_t address = (uint16_t) buffer;
    uint16_t remain  = (uint16_t) (0x4000u - (address & 0x3fffu));
    return requested < remain ? requested : remain;
}
```

`address & 0x3fff` = position dans la page ; `0x4000 - ça` = octets restants avant
la frontière ; on plafonne la demande à ce reste.

La troncature est **invisible pour la logique métier** : `read` renvoie le nombre
réel d'octets lus, donc la boucle de lecture fait simplement un tour de plus.
Préférer cette approche à toute tentative d'aligner les buffers à la main.

---

## 4. Lecture : le motif canonique

```c
zos_err_t err;
uint16_t  amount;

for (;;) {
    amount = sizeof(buffer);              /* RÉINITIALISÉ à chaque tour */
    amount = page_amount(buffer, amount); /* contrainte 16 Ko */

    err = read(fd, buffer, &amount);
    if (err != ERR_SUCCESS)
        break;                            /* erreur d'E/S */
    if (amount == 0)
        break;                            /* fin de fichier */

    /* traiter buffer[0 .. amount-1] */
}
```

Deux pièges, tous deux liés au paramètre `size` :

1. **`size` est un paramètre entrée/sortie** (`zos_vfs.h:138-139`) : on y met la
   taille du buffer, `read` l'écrase avec le nombre d'octets réellement lus.
   L'oublier fait rétrécir la lecture à chaque itération jusqu'à zéro.
2. **Il n'y a pas de `feof`.** La fin de fichier, c'est `size == 0` au retour ;
   la valeur `zos_err_t` ne signale que les erreurs.

---

## 5. Écriture : `write` n'écrit pas forcément tout

Il faut boucler jusqu'à épuisement, avec une garde anti-boucle-infinie
(motif de `ed_io_write_all`, `zeal-ed/src/core/ed_file.c`) :

```c
static uint8_t write_all(zos_dev_t fd, const uint8_t* data, uint16_t length)
{
    uint16_t done = 0;

    while (done < length) {
        uint16_t amount = page_amount(&data[done], (uint16_t)(length - done));

        if (write(fd, &data[done], &amount) != ERR_SUCCESS || amount == 0)
            return 0;                     /* échec — amount==0 évite l'infini */
        done = (uint16_t)(done + amount);
    }
    return 1;
}
```

La garde `amount == 0` est indispensable : un device qui accepte zéro octet sans
signaler d'erreur ferait tourner la boucle indéfiniment.

---

## 6. Buffers : `static`, pas sur la pile

`zeal-ed` déclare ses buffers en variables locales (`uint8_t chunk[256]` dans
`ed_file_load`). Ça passe chez eux, mais la pile Z80 est réduite.

Pour `dumbif`, préférer `static` (section `_BSS`) :

```c
static uint8_t io_buffer[256];
```

- taille connue à la compilation et **visible dans la map** ;
- pas de concurrence avec la profondeur d'appel ;
- coût identique en octets, mais réservé une fois pour toutes.

---

## 7. `stdout` / `stdin` sont déjà ouverts

Pas besoin d'`open` (`zos_vfs.h:24-30`) : `DEV_STDOUT = 0`, `DEV_STDIN = 1`.
Ils s'utilisent directement avec `write`/`read`.

C'est la voie pour écrire du texte **sans payer `printf`** : dès qu'un seul
`printf` avec conversion (`%d`, `%s`) subsiste dans le programme, `printf_large`
et `vprintf` sont linkés, soit **~2,9 Ko** (mesuré : 3448 octets avec `printf`
contre 542 avec `puts` seul, sur les 48 Ko disponibles). L'arbitrage est global,
pas ligne par ligne.

Saisie clavier, mode naturel pour lire une commande (`zos_keyboard.h`) :

```c
ioctl(DEV_STDIN, KB_CMD_SET_MODE, (void*)(KB_READ_BLOCK | KB_MODE_COOKED));
```

Bufferisé, bloquant, flush sur `\n`. Note : il n'existe pas de vidage portable de
l'entrée sur Zeal — attention aux `\n` parasites d'une séquence `\r\n`.

---

## 8. Ne pas reprendre l'abstraction de `zeal-ed`

`zeal-ed` passe par une table de pointeurs de fonction (`ed_io_t` dans
`src/core/ed_file.h`) : le cœur appelle `io->read(...)`, et `ed_zeal_io.c` fournit
l'implémentation Zeal.

La raison est visible dans son arborescence (`tests/run-native-tests.sh`) :
pouvoir substituer une implémentation POSIX et **tester sur PC hors Z80**.
Bénéfice réel pour un éditeur de plusieurs milliers de lignes.

Pour `dumbif`, c'est du poids mort — 5 pointeurs en RAM, un appel indirect
(`__sdcc_call_hl`) par lecture, une couche à traverser pour se relire.
**Appeler `open()` directement.** L'indirection s'ajoutera le jour où des tests
hôtes seront réellement nécessaires, pas avant.

---

## Sources

Toutes les affirmations de ce document ont été vérifiées dans le code ou par
compilation réelle, à la date du 2026-09-06.

### Sources primaires — Zeal 8-bit OS

Dépôt local : `~/projects/zeal/Zeal-8-bit-OS` (commit `9bd3296`). Copie identique
utilisée par le conteneur ZDE : `~/projects/zeal/zeal-dev-environment/home/Zeal-8-bit-OS`.

| Fichier | Ce qui en a été tiré |
| --- | --- |
| `kernel_headers/sdcc/include/zos_vfs.h` | signatures `open`/`read`/`write`/`seek`/`close` (`:141,155,173,185,234`), flags `O_*` (`:55-63`), `zos_dev_t` (`:74`), `DEV_STDOUT`/`DEV_STDIN` (`:24-30`), `FILENAME_LEN_MAX`/`PATH_MAX` (`:35,40`), contrainte de page 16 Ko (`:118-129`), convention d'erreur d'`open` (`:169-171`), formes de chemin (`:159-164`), limite de devices ouverts (`:178-180`) |
| `kernel_headers/sdcc/include/zos_errors.h` | énumération `zos_err_t` (`:9-35`) |
| `kernel_headers/sdcc/include/zos_sys.h` | `exit(uint8_t)` (`:66`), `CALL_CONV` = `__sdcccall(1)` (`:16`) |
| `kernel_headers/sdcc/include/zos_keyboard.h` | `KB_CMD_SET_MODE`, `KB_READ_BLOCK`, `KB_MODE_COOKED` |
| `kernel_headers/sdcc/src/zeal8bitos.asm` | `_putchar` bufferisé et conditions de flush (`:624-676`), `_fflush_stdout` (`:684-697`) |
| `kernel_headers/sdcc/src/zos_crt0.asm` | flush puis `exit` sur le retour de `main` (`:48-56`), passage de `argc`/`argv` (`:28-47`) |
| `kernel_headers/sdcc/lib/README.md` | `z80.lib` = version SDCC patchée (`_CODE` → `_TEXT`) |
| `cmake/sdcc_toolchain.cmake` | chemin d'inclusion (`:28`), lien `-l z80` (`:39`), crt0 (`:31`) |
| `docs/system-calls.md` | vue d'ensemble par numéro de syscall (`open` en `:58`) |
| `README.md` | table des syscalls (`:391-477`), renvoi aux headers comme doc de référence (`:494-496`) |

Constat annexe : `kernel_headers/examples/` (sdcc, gnu-as, z88dk) ne contient
**aucun** appel à `open()` — il n'y a pas d'exemple officiel d'accès fichier.

### Sources primaires — SDCC

Version : **SDCC 4.4.0 #14620 (Linux)**, dans le conteneur ZDE.

| Fichier | Ce qui en a été tiré |
| --- | --- |
| `/opt/sdcc/share/sdcc/include/stdio.h` | `puts` est non variadique (`:79`), `printf` variadique (`:75`) ; aucune occurrence de `fopen`/`FILE`/`fclose`/`fread` |
| `/opt/sdcc/share/sdcc/include/stdlib.h` | ni `exit`, ni `abort`, ni `EXIT_SUCCESS`/`EXIT_FAILURE` — la section « process control » (ISO C 7.22.4) est absente |
| `kernel_headers/sdcc/lib/z80.lib` | module `puts` : dépend de `_putchar` (`S _putchar Ref000000`) et se termine par `ld hl,#0x0A ; jp _putchar` — c'est lui qui émet le `\n` |

### Source secondaire — zeal-ed

Dépôt : <https://github.com/zoul0813/zeal-ed> (branche `main`)

| Fichier | Rôle |
| --- | --- |
| [`src/platform/zeal/ed_zeal_io.c`](https://github.com/zoul0813/zeal-ed/blob/main/src/platform/zeal/ed_zeal_io.c) | **la vraie source utile** : `page_amount`, `zopen`/`zread`/`zwrite`/`zclose`, `ed_zeal_console_init` |
| [`src/core/ed_file.c`](https://github.com/zoul0813/zeal-ed/blob/main/src/core/ed_file.c) | `ed_io_write_all` (boucle d'écriture), `ed_file_load` (boucle de lecture) |
| [`src/core/ed_file.h`](https://github.com/zoul0813/zeal-ed/blob/main/src/core/ed_file.h) | l'abstraction `ed_io_t` — celle qu'on ne reprend pas (§8) |
| `tests/run-native-tests.sh`, `tests/test_main.c` | justifient l'abstraction : tests hôtes hors Z80 |

Statut : code tiers, non affilié au projet Zeal officiel. Repris comme
**illustration d'un motif**, pas comme référence normative — les règles
elles-mêmes viennent des headers du noyau ci-dessus.

### Mesures faites en séance

Compilation et link reproduits dans le conteneur ZDE (`sdcc -mz80` +
`sdldz80` + `sdobjcopy`, mêmes options que `sdcc_toolchain.cmake`) :

| Programme de test | Taille `.bin` | Modules de `z80.lib` tirés |
| --- | --- | --- |
| `printf` avec `%d`/`%s` | **3448 octets** | `printf_large`, `vprintf`, `strlen`, `__sdcc_call_hl`, `puts` |
| même logique, `puts` seul | **542 octets** | `puts` |

D'où le chiffre de ~2,9 Ko cité au §7.
