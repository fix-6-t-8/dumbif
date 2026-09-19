# dumbif — Conception des scènes : offsets et compilation

> **Statut : proposition de design, non implémentée.** Rédigé le 2026-08-31.
> Ce document décrit une cible, pas l'état du code. Rien n'est encore écrit dans `src/`.

## Principe : deux formats, pas un

| | Format auteur | Format runtime |
| --- | --- | --- |
| Fichier | `scenario.if` | `scenario.dat` |
| Nature | texte, **délimité** | binaire, **préfixé** |
| Lu par | l'auteur, l'indexeur | le moteur Z80 |
| Édition | à la main | jamais |

Un **indexeur** tournant sur PC compile le premier vers le second. Le moteur Z80
ne connaît que le second et ne fait que des `seek` et des `read` — aucun parsing
de texte, aucune recherche de label, aucune comparaison de chaînes pour naviguer.

## Format auteur

```
[label]
Texte de la scène. Les *commandes* sont entre astérisques ;
le texte d'une commande EST le label de sa destination.;
```

- `[label]` ouvre une scène, `;` la termine.
- `*mot*` marque une commande. Sa chaîne remplit trois rôles à la fois :
  mot mis en évidence à l'écran, mot saisi par le joueur, clé de destination.

## Format runtime

```
Offset       Contenu                        Type
-----------  -----------------------------  ------------------
+0           nb_scenes (N)                  uint8_t
+1           table des offsets              uint16_t × (N+1)
+1+2(N+1)    données des scènes, à la file  —
```

Les `uint16_t` sont écrits en **petit-boutiste** (Z80). Les offsets sont
**absolus** depuis le début du fichier : la valeur se passe telle quelle à
`seek(..., SEEK_SET)`.

### Accès à la scène `n`

```
1. seek(1 + n*2, SEEK_SET) ; read 4 octets  ->  offsets[n] et offsets[n+1]
2. longueur = offsets[n+1] - offsets[n]
3. seek(offsets[n], SEEK_SET) ; read longueur  ->  scène entière en RAM
4. parsing dans le buffer
```

**Deux `seek` et deux `read`, quelle que soit la taille du scénario.**

### Pourquoi cette table

- **Une seule colonne.** L'index de scène n'est pas stocké : il *est* la position
  dans la table. L'écrire serait redondant.
- **Entrées de 2 octets.** `n * 2` se calcule en une instruction (`add hl, hl`).
  Le Z80 n'a aucune instruction de multiplication ; une taille d'entrée en
  puissance de deux est un critère de conception, pas une coquetterie.
- **Sentinelle `N+1`.** La dernière entrée contient l'offset de fin des données.
  La longueur de chaque scène s'obtient par différence, sans jamais être stockée.
- **Étape 1 en une lecture.** Les deux offsets étant adjacents, 4 octets suffisent.
- **Scène lue d'un bloc.** Connaître la longueur d'avance permet un `read` unique,
  donc un parsing en RAM sur buffer contigu — pas de machine à états à cheval
  sur plusieurs lectures.

## Structure d'une scène

```
Offset   Contenu                              Type
-------  -----------------------------------  -----------------------
+0       longueur du texte                    uint16_t
+2       texte brut, astérisques conservées   —
+...     nb_commandes                         uint8_t
+...     index de destination                 uint8_t × nb_commandes
```

**Les libellés ne sont pas dupliqués.** Ils sont déjà dans le texte, entre
astérisques. La *k*-ième paire d'astérisques rencontrée correspond à
`destinations[k]`. Coût : **un octet par commande**.

Résolution d'un choix : balayage du buffer texte **en RAM** (quelques centaines
d'octets), puis lecture directe d'un `uint8_t`. Aucun accès disque, aucune
comparaison de label.

## Pourquoi préfixé plutôt que délimité

Le syscall impose d'annoncer la taille avant de lire :

```c
zos_err_t read(zos_dev_t dev, void* buf, uint16_t* size);
```

Un format préfixé fournit exactement cette information. Trois conséquences :

1. **Taille exacte connue avant lecture** — pas de bloc arbitraire, pas de report.
2. **Aucun caractère interdit** dans les données : `;` et `*` peuvent apparaître
   librement dans le texte compilé, donc aucun mécanisme d'échappement à écrire.
3. **Saut sans transfert** : `seek(+len, SEEK_CUR)` passe l'enregistrement sans
   lire un octet.

## Contrôles à la charge de l'indexeur (sur PC)

Toute erreur doit être signalée avec un numéro de ligne, sur le PC — jamais
découverte à l'exécution sur Z80.

- Commande pointant vers un label inexistant.
- Label dupliqué.
- Scène dépassant la taille du buffer du moteur (512 octets, **à confirmer**).
- Plus de 255 scènes (`uint8_t`) ou plus de 64 Ko de données (`uint16_t`).

## Limites du format

- 255 scènes, 64 Ko de données compilées.
- Une scène doit tenir dans le buffer du moteur.

## Points non tranchés

- Échappement de `;` et `*` dans le **format auteur** (le format runtime, lui,
  n'a pas de caractère réservé).
- Sensibilité à la casse et aux espaces multiples lors de la saisie.
- Modèle de saisie : le joueur tape le libellé, ou choisit un numéro.
- État de jeu (inventaire, drapeaux, liens conditionnels) : **non prévu** dans ce
  design. Son ajout modifierait le format d'une scène.
