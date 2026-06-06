# SoundMapping

Contrôle du volume **par application** sous Windows (comme SteelSeries Sonar),
avec à terme un **boîtier matériel** physique pour piloter les canaux.

POC en **C++ natif** : binaire léger, aucun runtime, empreinte minimale.

## Ce que fait le POC

- Détecte les applications qui jouent du son.
- Affiche une **table de mixage** : un slider de volume vertical par appli + case « Muet ».
- Modifie le volume **en temps réel**, indépendamment pour chaque appli.
- Bouton **Rafraîchir** pour re-scanner les applis.

## Stack

- **C++17** + **Win32** (GUI native, zéro dépendance externe).
- **Windows Core Audio** (WASAPI / COM) pour le volume par application.
- Windows 10/11.

## Prérequis

- **Visual Studio 2022** avec la charge de travail *« Développement Desktop en C++ »*
  (ou *Build Tools for Visual Studio* + *CMake*).

## Compiler

### Option A — CMake (recommandé)

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Binaire produit : `build\Release\SoundMapping.exe`

### Option B — cl.exe direct

Depuis l'invite **« x64 Native Tools Command Prompt for VS 2022 »** :

```bat
cl /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE ^
   src\main.cpp src\audio\AudioSessionManager.cpp ^
   src\gui\MixerWindow.cpp src\hardware\SerialController.cpp ^
   /link Ole32.lib Comctl32.lib /SUBSYSTEM:WINDOWS /OUT:SoundMapping.exe
```

## Architecture

```
SoundMapping/
├── CMakeLists.txt
└── src/
    ├── main.cpp                              # WinMain + CoInitialize
    ├── audio/
    │   ├── AudioSessionManager.{h,cpp}       # volume par appli (Core Audio)
    │   ├── EndpointEnumerator.{h,cpp}        # liste les sorties (id + nom)
    │   └── ProcessCapture.{h,cpp}            # capture le son d'1 appli (Win11)
    ├── dsp/
    │   ├── Biquad.h                          # filtre biquad (RBJ), pur C++
    │   └── ChannelEq.{h,cpp}                 # EQ basses/aigus par canal
    ├── routing/
    │   ├── AudioRouter.{h,cpp}               # routage loopback -> sortie
    │   └── AppAudioRouter.{h,cpp}            # capture appli -> EQ+volume -> sortie
    ├── gui/MixerWindow.{h,cpp}               # table de mixage (Win32)
    ├── hardware/SerialController.{h,cpp}     # futur boîtier (stub)
    └── tools/
        ├── router_test.cpp                   # test du routage (source -> sortie)
        ├── virtual_route.cpp                 # route le périph VIRTUEL -> sortie réelle
        └── app_eq.cpp                        # EQ par appli (capture par processus)
```

Idée directrice : **toute la logique audio est dans `audio/`**. La GUI et le
futur hardware ne sont que des « clients » de cette couche. Brancher le boîtier
ne touchera donc pas au coeur.

## Périphériques virtuels — étape 1 : moteur de routage

Objectif final : des **périphériques virtuels** vers lesquels certaines applis
envoient leur son, chacun avec ses réglages, tous mixés vers **une sortie réelle
sélectionnable**.

Créer un périphérique virtuel nécessite un **driver noyau signé** (voir la
roadmap). On commence donc par la partie réutilisable et sans driver : le
**moteur de routage** (`routing/AudioRouter`). Il capture le son d'une sortie en
*loopback*, applique un volume, et le rejoue sur une autre sortie — avec
rééchantillonnage automatique. Quand le périphérique virtuel existera, seule
l'« id source » changera, le moteur restera identique.

### Tester le moteur (`RouterTest`)

```powershell
cmake --build build --config Release --target RouterTest
.\build\Release\RouterTest.exe
```

L'outil liste tes sorties, demande une **source** (capturée en loopback) et une
**destination** (sortie réelle), puis route le son. Tape un volume `0-100` à la
volée, `q` pour quitter.

> Sans périphérique virtuel pour l'instant, teste avec de **vrais**
> périphériques : envoie une appli vers la sortie A (Paramètres Windows → Son),
> route A → B avec l'outil, et tu entendras le son aussi sur B. Le routage
> *exclusif* (son inaudible sur la source) viendra avec le driver virtuel.

## Périphériques virtuels — étape 2 : le périphérique virtuel

Créer un périphérique audio se fait au niveau **driver noyau**. Plutôt que de le
réécrire, on utilise le driver open-source **Virtual-Audio-Driver** (dérivé du
sample officiel SYSVAD de Microsoft, licence MIT), puis notre `AudioRouter`
renvoie son son vers une sortie réelle.

### Installer le driver virtuel

1. **Désactiver Secure Boot** (BIOS/UEFI) — sinon le *test-signing* ne s'active
   pas et le driver ne se chargera pas.
2. **Activer le test-signing** (PowerShell admin), puis redémarrer :
   ```powershell
   bcdedit /set testsigning on
   ```
   Un filigrane « Mode test » apparaît : c'est normal.
3. Télécharger la dernière *release* depuis
   [VirtualDrivers/Virtual-Audio-Driver](https://github.com/VirtualDrivers/Virtual-Audio-Driver/releases)
   et la décompresser.
4. **Device Manager** → menu *Action* → *Ajouter un matériel d'ancienne
   génération* → *Installer manuellement* → *Contrôleurs audio, vidéo et jeu* →
   *Disque fourni…* → sélectionner `VirtualAudioDriver.inf`.
5. Vérifier qu'une sortie **« Virtual Audio Driver »** apparaît dans les
   *Paramètres → Son*.

> Souci connu sur Windows 11 24H2 (périphérique absent, erreur de signature 52) :
> voir l'[issue #1](https://github.com/VirtualDrivers/Virtual-Audio-Driver/issues/1).

### Router le périphérique virtuel vers une sortie réelle

```powershell
cmake --build build --config Release --target VirtualRoute
.\build\Release\VirtualRoute.exe
```

L'outil détecte automatiquement le périphérique virtuel, te demande la **sortie
réelle** de destination, et lance le routage. Mets ensuite le périphérique
virtuel **par défaut** dans Windows : tout ton son y arrive, puis ressort sur la
sortie réelle choisie. Volume réglable en direct.

## Effets par application — EQ par appli (capture par processus)

Avec **toutes les apps sur un seul CABLE**, leurs sons sont déjà mélangés : on
ne peut pas leur appliquer des effets différents. Pour un EQ **par appli**, on
capture chaque application **séparément** grâce à l'API *process loopback* de
Windows 11 (`ProcessCapture`), on lui applique son propre EQ (`ChannelEq` :
basses + aigus), puis on rend le résultat sur la sortie choisie
(`AppAudioRouter`).

> Prérequis : **Windows 11** (ou Windows 10 build 20348+) pour la capture par
> processus.

### Tester (`AppEq`)

```powershell
cmake --build build --config Release --target AppEq
.\build\Release\AppEq.exe
```

Choisis une application (ex. Spotify) et une sortie, puis règle en direct :

```
b6     # +6 dB de basses
t-3    # -3 dB d'aigus
v80    # volume 80 %
q      # quitter
```

Astuce : mets la sortie de l'appli sur **CABLE** pour ne pas l'entendre en
direct — tu n'entends alors que la version traitée. Le moteur EQ est validé par
test (identité à 0 dB, boost basses/aigus ciblés, stabilité).

## Roadmap

1. **POC** — mixer logiciel fonctionnel. ✅
2. **Moteur de routage user-mode** (`AudioRouter`) — loopback → sortie réelle,
   avec volume. ✅ (testable via `RouterTest`)
3. **EQ par application** — capture par processus (`ProcessCapture`) + EQ
   (`ChannelEq`) + routeur per-app (`AppAudioRouter`), testable via `AppEq`. ✅
   Prochaine étape : mixer plusieurs apps en parallèle + GUI (un canal par appli).
4. **Périphérique virtuel** — driver open-source Virtual-Audio-Driver (SYSVAD,
   *test-signing* local) + auto-routage virtuel → sortie réelle (`VirtualRoute`). ✅
   Plus tard : compiler notre propre driver depuis les sources si besoin.
5. **Notifications de sessions** (`IAudioSessionNotification`) → mise à jour auto
   de la liste, sans bouton Rafraîchir.
6. **Icône systray** + lancement au démarrage. ⚠️ Une *app utilisateur* en
   arrière-plan, **pas** un *service Windows* (session 0) : un service ne voit
   pas les sessions audio de ta session interactive.
7. **Hardware** — Arduino + potentiomètres → lecture série → `SerialController`
   mappe les curseurs sur les volumes.
8. Persistance des réglages par application.
