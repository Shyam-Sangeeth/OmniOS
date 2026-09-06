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
./build/omnictl store                   # apps found on this machine
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
| [Apps.cpp](src/omnios/Apps.cpp) | 10 | The built-in app tiles, and which packages must not be removed. |
| [DesktopEntry.cpp](src/omnios/DesktopEntry.cpp) | 10 | Finds installed apps from their freedesktop desktop entries. |

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

OmniOS does not have a store of its own. It ships **GNOME Software** — the same
program Ubuntu ships, before Canonical renamed it — as a tile on the Apps tab.

Arch builds it without the PackageKit plugin, so its catalogue is **Flathub**
rather than the repositories, and that is the right way round for a console: a
Flatpak carries its own libraries, so installing one cannot drag the base system
into the partial upgrade that `pacman -Sy something` invites. `omnios-flathub`
adds the remote at boot, because a fresh Flatpak install has no remotes at all
and the store then opens onto an empty shelf with nothing to say about why.

The other half is knowing what is *on* the machine, and that is
[DesktopEntry.cpp](src/omnios/DesktopEntry.cpp): the Apps tab is built from
freedesktop desktop entries, so anything installed — from the store, from
pacman, by hand — becomes a tile with nothing added to a table in this
repository. Flatpak exports an entry per app into a directory already on the
search path, so a Flathub install needs no special case at all.

Flatpak's `Exec` needs one thing beyond the spec. Every exported entry looks
like this:

    /usr/bin/flatpak run ... --file-forwarding org.videolan.VLC @@u %U @@

The `@@u … @@` pair brackets the arguments that are file paths, for a launcher
that has files to hand in. Dropping `%U` and keeping the brackets leaves
`flatpak run … @@u @@`, and flatpak takes those as the files it was promised —
which is why an app installed from the store appeared as a tile, started, and
immediately went away again. The markers go with the field codes.

The cost of that is everything the image itself drags in would become a tile
too: settings dialogs from the file manager's dependencies, `avahi-discover`,
`bssh`, `cmake-gui`. So `build-iso.sh` records the desktop entries that ship
with the image — computed from the dependency closure of `packages.x86_64`, no
second pass over the built tree — and the launcher hides those. A missing list
means no filtering, which is the safe direction: a developer build shows
everything rather than nothing.

### One thing at a time

The launcher switches to the app workspace before it spawns anything, which
covers every window it opens itself. It does not cover the ones it does not
open — a game started from inside Steam, a second window from a running app, a
dialog. Those land on whatever workspace is current, which is the launcher's,
and Hyprland tiles them: the grid squeezed into half the screen with a game
beside it.

[HyprlandEvents.cpp](src/launcher/HyprlandEvents.cpp) listens on Hyprland's
event socket and the controller moves anything that is not the shell off the
launcher's workspace, then follows it. No polling and no window rules, which
matters because this Hyprland rejects the windowrule syntax outright.

Two things that make this fail silently, both of them found the hard way:
Hyprland reports a window address bare in its events and requires it prefixed
in its dispatchers, so handing it straight back matches nothing; and it does not
guarantee its own variables reach an `exec-once` child, so the socket is found
by searching as well as by `HYPRLAND_INSTANCE_SIGNATURE`. Both now say so in
the shell log rather than doing nothing quietly.

### The controller

A console you can only drive with a keyboard is not a console. The shell now
takes a gamepad, and it does it by becoming the keyboard rather than growing a
second set of navigation: each press is turned into the key the shell already
answers to and posted to whatever holds focus. One mapping table, and no
duplicate of the grid logic to drift out of step with the original.

| Button | Does |
|---|---|
| D-pad / left stick | move, with hold-to-repeat |
| A (south) | open |
| B (east) | back, close a menu |
| X (west) | that tile's menu |
| Y (north) | rescan |
| L1 / R1 | switch tab |
| Start | sleep, restart, shut down |
| Guide | back to the library, from anywhere |

SDL3, because Qt6 has no gamepad module — QtGamepad did not survive Qt5 — and
because SDL carries the mapping database that makes a DualSense and an Xbox pad
behave the same. It is optional at build time: without SDL3 the shell still
builds and is simply keyboard-only.

A DualSense needs nothing added over USB: `hid-playstation` is in the kernel,
SDL3 carries the mapping, and the udev rules that let SDL reach its hidraw node
— and with them rumble, the light bar and the touchpad — arrive with
`steam-devices`, which Steam already pulls in. Wireless is the half that needed
a stack: without bluez a PS5 pad cannot be paired at all, which is how most
people use one. "Pair a controller" in the system menu holds a scan open long
enough to walk to the console, then pairs, trusts and connects the first
gamepad it sees, so it comes back on its own next time.

SDL reads the input devices directly rather than through the compositor, which
is what makes the Guide button work from inside a running game. The same fact
means everything else has to be ignored while a game is running, or the grid
would be moving quietly underneath it.

Two things had to be true before a press could land. Qt routes a key only to
the item holding active focus, and there is none while the window is inactive —
which is the state after an app exits, or after the session has been away on
another virtual terminal — so the shell is asked to take focus first. And
`focusWindow()` is null in exactly those moments, so delivery falls back to the
launcher's only top-level window rather than dropping the press.

### The other corner

The top-right used to carry a permanent line of application state — "No games
in /home/omni/Games" — which repeated what the empty grid already said and was
otherwise nothing anyone needed. It now answers the two questions a console is
actually asked at a glance: am I online, and is the controller on.

The state text is not gone, it is transient. Some of it matters a great deal —
"the disk is full", "Steam could not start" — so it appears for a few seconds
when it changes and then gets out of the way.

[SystemStatus.cpp](src/launcher/SystemStatus.cpp) reads NetworkManager and bluez
through `nmcli` and `bluetoothctl` rather than binding to their D-Bus APIs: the
shell already shells out for everything else, and one process every eight
seconds does not justify a dependency on libnm and GDBus. The lists it produces
are shaped like MenuPanel's model, so the same panel that serves a tile's menu
serves these.

Clicking an indicator opens its panel, and both are in the system menu as well,
because a console often has no pointer and the keyboard and controller should
not be second-class. Wi-Fi lists what is in range with signal strength, and
connects to open networks and to ones this machine already knows. A new secured
network says plainly that it needs a password and that there is no keyboard flow
for one yet, which is better than a connection that fails without explanation.

### The mark, and turning the machine off

The top-left corner holds the OmniOS mark rather than the word "OmniOS", which
only told you what you were already looking at. Clicking it — or pressing
**F10**, because a console has to work with no pointer — opens Sleep, Restart
and Shut down.

The mark is drawn in [OmniLogo.qml](src/launcher/OmniLogo.qml) rather than
loaded from a file, and drawn to match the boot splash's mark rather than being
a second logo that happens to sit in the same product. The splash renders it at
760px; this one has to read at 28, so it is the same orbit ring and play
triangle with the wordmark and tagline dropped.

`powerAction()` takes the action through a fixed table rather than interpolating
the string it was handed, and goes through `systemctl` rather than sudo, so
logind still gets to run the inhibitors and anything holding a shutdown off is
respected.

### Sleep, and getting back

Sleep is only half a feature without a way back. Linux arms almost nothing as a
wakeup source — every USB device comes up with `power/wakeup` disabled — so a
suspended machine ignores the gamepad and the keyboard, and only the power
button brings it round. On a console under a television, with the box out of
reach, that is close to useless.

`99-omnios-wakeup.rules` arms USB devices *and* their host controllers. Both
halves are needed: a device armed behind a controller that is not armed still
cannot wake anything, because nothing is listening for the resume signal.

The power button behaves the way a console's does rather than the way a
server's does — a tap sleeps, a long press powers off — which also makes the
same button the way back.

`zz-omnios-resume` is what makes the shell reappear. Waking came back to a
stuck splash: Plymouth ships no sleep hook of its own, this profile masks
`plymouth-quit.service` because the boot handoff needs it, so on resume nothing
had the job of taking the splash down and `plymouthd` sat holding DRM master
with Hyprland unable to draw behind it. The hook takes it down and asks Hyprland
to switch its outputs back on.

### The tile menu

Every app tile carries three dots in its bottom-right corner (or press **M**):
open, check for update, update, uninstall. Entries that do not apply are left
out rather than greyed — a disabled "Install" on something already installed
only describes a state the tile already shows.

The one exception is uninstalling a system app, which stays visible and
disabled with the reason. That is a *rule* rather than a state, and dropping it
silently would leave someone wondering whether the tile was special or the menu
was broken.

The rule itself is a property of the **package**, not of the tile:

```cpp
bool packageIsProtected(std::string_view package);
```

The browser tile and the YouTube tile are two rows sharing one `chromium`
package, so "is this tile a system app?" is the wrong question — removing
chromium from either would break both.

Removal follows wherever the app came from: `flatpak uninstall` for a Flatpak,
and for a native app `pacman -Qoq` on its desktop file to find the owning
package first, because plenty of apps ship an entry from a package named
nothing like the binary.

`checkupdates` from `pacman-contrib` backs the update check on native packages.
It compares against a throwaway database instead of running `pacman -Sy`, which
on Arch would leave the system one partial upgrade away from a mismatched libc.

On a live image the root filesystem is a RAM overlay, so anything installed is
gone at the next boot — and there is only so much of it. The Apps tab says both,
in one sentence: how much room is left, and that it will not survive. When the
space runs low the same line turns red, because a full overlay is exactly what
makes apps stop starting, and with no notice the console simply looks broken.

That is not hypothetical. Opening Steam once fills a 2 GB overlay by itself, on
its first run, downloading its own client — after which nothing else starts and
nothing says why. `cow_spacesize` is now a percentage of RAM rather than a
fixed number, because the right size depends entirely on the machine, and an
app that dies within a couple of seconds while the disk is full is reported as
"could not start — the disk is full" rather than sent to a log that will not
mention it.

### Steam

Steam installs into `~/.local/share/Steam/steamapps`, which says "games" to
nobody, and games installed through it never appeared on the Games tab at all.
`omni-steam-library` symlinks that directory to `~/Games/steam` at session
start, so Steam keeps its own layout while living where the rest of the library
does.

A symlink rather than Steam's own library configuration: `libraryfolders.vdf`
is rewritten whenever Steam feels like it and "which library is the default" is
a preference it owns, whereas a symlink is a fact about the filesystem that
Steam simply follows. It refuses to touch an existing library that has anything
in it — moving somebody's games is not a script's decision — and the scanner
reads Steam's default locations too, so those games still get tiles.

[SteamLibrary.cpp](src/omnios/SteamLibrary.cpp) reads Steam's own
`appmanifest_<appid>.acf` records rather than guessing from directory names.
That is both more accurate — the real title, the real size — and the only way
to get the app id, because a Steam game is launched by id and not by path:

    steam steam://rungameid/440

Handing anyone the executable does not work. The client has to be running for
DRM, the overlay and cloud saves, so the route is through Steam even when the
binary is easy to find. For the same reason `gamemoderun` is not wrapped around
it: that would govern the client, while the game runs as a child of its own out
of reach. This is also the first detection source that is not a guess — the
platform told us — so it is recorded as `manifest` rather than `folder`.

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
