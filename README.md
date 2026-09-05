# OmniOS core

The engine room of OmniOS: the code that decides *what a file is*, *what can run
it*, and *how to start it*. [OmniOS.md](OmniOS.md) is the full system design;
this is Phases 6, 7 and 9 of it, built as a plain C++20 library with no UI
toolkit dependency so it builds and tests anywhere.

The Phase 10 QML launcher will sit on top of `omnios_core` and call exactly the
same entry points `omnictl` does today.

## Build

Needs CMake 3.20+ and a C++20 compiler. Nothing else — no network access, no
third-party libraries.

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Try it

`OMNIOS_GAMES_DIR` and `OMNIOS_DATA_DIR` override `~/Games` and `~/.omnios`, so
you can point the whole system at a scratch tree:

```bash
export OMNIOS_GAMES_DIR=/tmp/demo/Games
export OMNIOS_DATA_DIR=/tmp/demo/.omnios

./build/omnictl init                    # create the folder tree
./build/omnictl scan                    # walk ~/Games, rebuild the library
./build/omnictl list                    # every tile the launcher would show
./build/omnictl info ps4.god-of-war     # metadata + the exact launch command
./build/omnictl launch ps4.god-of-war   # print the plan (--run to execute)
./build/omnictl detect some-file.pkg    # identify one file
./build/omnictl verify manifest.json    # validate an .opkg manifest
./build/omnictl engines                 # which layers are installed
./build/omnictl apps                    # built-in app tiles
./build/omnictl store                   # installable extras
```

`launch` is a dry run unless you pass `--run`, so the routing is inspectable on
a machine with no emulators installed.

## What's here

| File | Phase | Does |
|---|---|---|
| [Platform.cpp](src/omnios/Platform.cpp) | — | The platform registry. Every platform-specific fact lives in one table. |
| [Json.cpp](src/omnios/Json.cpp) | 6 | Small total JSON parser. Never throws; malformed input returns an error string. |
| [Manifest.cpp](src/omnios/Manifest.cpp) | 6 | `.opkg` manifest parsing and validation, split into blocking errors and warnings. |
| [Detector.cpp](src/omnios/Detector.cpp) | 9.1 | Magic bytes → platform, with extension and folder fallbacks. |
| [GameScanner.cpp](src/omnios/GameScanner.cpp) | 7 | Walks `~/Games/`, identifies titles, builds the library. |
| [GameLibrary.cpp](src/omnios/GameLibrary.cpp) | 7.5 | The tile model plus its `~/.omnios/library.json` cache. |
| [Router.cpp](src/omnios/Router.cpp) | 9.2–9.5 | Platform → engine → argv, with install hints when a layer is missing. |
| [Apps.cpp](src/omnios/Apps.cpp) | 10 | Built-in app tiles and the installable catalogue behind the Store tab. |

### Detection

Bytes decide wherever a format has a signature, because a renamed dump in the
wrong folder should still get a working tile. Each result records *how* it was
reached (`magic` / `extension` / `folder`), so a failed launch can say whether
OmniOS recognised the file or guessed from its location.

Two cases the design doc's table doesn't separate, and this does:

- **Linux vs PS4.** Both are x86-64 ELF. The `EI_OSABI` byte at offset 7 is what
  differs — the PS4 toolchain targets FreeBSD (`0x09`).
- **Wii vs GameCube.** Different magic at different offsets (`0x18` vs `0x1C`),
  and Wii is checked first so a Wii image isn't misread.

`~/Games/pc/` is shared by Linux and Windows, so it supplies no folder hint —
those two are always separated by their headers.

### Apps and the store

Two registries, both tables like every other one here.

`allApps()` is what ships: a video player, a file manager, a browser and a
YouTube tile. `appCatalog()` is what can be added — VLC, Firefox, Kodi and the
rest, all from the official repositories so nothing has to be compiled on a
console at first boot. Installing one from the Store tab makes it a tile on the
Apps tab; there is no separate list to keep in sync.

Every app tile carries a three-dot menu: open, check for update, update,
install, uninstall. Uninstall is the one that needs a guard, and the guard is a
property of the *package*, not of the tile:

```cpp
bool packageIsProtected(std::string_view package);
```

The browser tile and the YouTube tile are two rows sharing one `chromium`
package, so "is this tile a system app?" is the wrong question — removing
chromium from either tile would break both. `removeApp()` asks the right one and
refuses, and the menu shows the entry greyed out with the reason rather than
hiding it, so a protected tile still looks like it has a menu.

`checkupdates` from `pacman-contrib` backs the update check. It compares against
a throwaway database instead of running `pacman -Sy`, which on Arch would leave
the system one partial upgrade away from a mismatched libc.

On a live image the root filesystem is a RAM overlay, so anything installed is
gone at the next boot. The Store tab says so rather than letting a user find out
by rebooting; `Launcher.ephemeral` detects it from the mount type, so the notice
disappears by itself once OmniOS is installed to a disk.

### Routing

The engine table in [Router.cpp](src/omnios/Router.cpp) is data, deliberately.
The emulator landscape moves: **Ryujinx was discontinued in October 2024** and
**Citra was taken down in March 2024**, so the `ryujinx-bin` and `citra-bin`
packages named in OmniOS.md Phase 8 no longer build. Switch and 3DS route to the
maintained forks (Ryubing, Azahar) instead, and swapping either is a one-line
change to the table.

PS5 routes to shadPS4 as the closest available Orbis layer. That covers a subset
of PS5 titles today, not the platform.

### The library cache

`~/.omnios/library.json` is only ever a faster copy of what a scan produces — it
is written through a temp file and rename, version-stamped, and discarded rather
than trusted if it is corrupt or stale. A rescan preserves the two things it
cannot rederive: fetched cover art and an engine the user pinned by hand.

## Tests

68 tests, run by `ctest` or directly via `build/tests/omnios_tests`. The harness
is [tests/Test.h](tests/Test.h) — self-registering `TEST`/`CHECK`, so building
the ISO never needs to fetch a test framework.

## Not built yet

Phase 6's installer (extracting a `.opkg` into `~/Games/`, checksum
verification), Phase 7.4 cover art fetching, Phase 9.4's process monitor, and
the Phase 10 QML shell.
