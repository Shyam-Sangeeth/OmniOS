# OmniOS

A gaming operating system built on Arch Linux. It boots from a USB stick into a
Plasma desktop, with **Game Mode** — a PS5-style tile launcher made for a
controller — one menu away, and it runs games from every platform it can: Linux
natively, Windows through Steam and Proton, consoles through emulators. It can
install itself onto a disk.

[OmniOS.md](OmniOS.md) is the full system design, in thirteen phases. This
README is what exists.

> **Status:** early. Everything below has been built and exercised in QEMU.
> None of it has run on real hardware yet, and that is the next test.

## What you get

- **A boot menu** on the USB stick: *Try OmniOS*, *Install OmniOS*, *Try OmniOS
  in Game Mode*, and troubleshooting entries. It waits ten seconds, then tries.
- **Desktop Mode** — KDE Plasma, dark, with the OmniOS mark in the corner and an
  OmniOS loading screen. The application launcher has a *Game Mode* button next
  to Sleep, Restart and Shut Down.
- **Game Mode** — the tile launcher, open over the desktop with KDE's own panel
  still along the bottom: a Games tab built by scanning `~/Games` and Steam's
  library, an Apps tab, and full controller support. Switching modes is
  instant; *Switch to desktop* is in its menu.
- **An installer** that puts OmniOS on a disk of its own: a language and
  keyboard layout, an account (or none, for a controller-only console), a time
  zone, and a typed confirmation before
  it will erase anything that holds files or another operating system.
- **A sign-in screen**, on installs where "sign in automatically" is off.
- **A store** — Discover on the desktop, on Flathub. What it installs turns up
  in Game Mode's Apps tab too, next to Steam.

## Getting started

### Build the ISO

The ISO is built by `mkarchiso` in an Arch Linux container. On Windows with
Docker Desktop:

```powershell
.\scripts\build-iso-docker.ps1 -KeepCache
```

It lands in `out/omnios-0.1.0-x86_64.iso` (about 3 GB). `-KeepCache` keeps the
downloaded packages between builds. [docs/BUILDING.md](docs/BUILDING.md) has the
other routes (WSL2, a real Arch machine) and everything that goes wrong.

### Try it in a VM

```powershell
.\scripts\run-qemu.ps1                    # boot the ISO in a window
.\scripts\run-qemu.ps1 -Disk              # ...with a 40 GB disk to install onto
.\scripts\run-qemu.ps1 -FromDisk          # boot that disk, with no ISO attached
.\scripts\run-qemu.ps1 -FromDisk -Uefi    # the same through UEFI firmware
.\scripts\run-qemu.ps1 -BlankDisk         # start again from an empty disk
```

The test disk lives in `%LOCALAPPDATA%\OmniOS\vm`, not in the repository.
[.claude/skills/boot-os/SKILL.md](.claude/skills/boot-os/SKILL.md) is the
detailed guide to driving the VM — screenshots, keys, logs, and the ways it
misleads.

### Put it on a USB stick

Write the ISO with Rufus in **DD Image mode** (or `dd` on Linux), turn **Secure
Boot off** in the PC's firmware — OmniOS is not signed for it — and boot from the
stick using the firmware's boot menu.

### Install it

Choose *Install OmniOS* in the boot menu, or open it from the live desktop or
Game Mode's menu. It takes a **whole disk**: it does not resize another system's
partitions to fit beside it. Put OmniOS on its own disk and pick it from the
firmware's boot menu instead.

## Repository layout

| Path | What |
|---|---|
| [src/omnios/](src/omnios) | The core: detection, scanning, routing, apps. Plain C++20, no UI toolkit. |
| [src/omnictl/](src/omnictl) | `omnictl`, the command-line front end to the core. |
| [src/launcher/](src/launcher) | The Qt 6 / QML shell: the Game Mode launcher, the installer (`--install`) and the sign-in screen (`--greeter`), all one binary. |
| [iso/](iso) | The archiso profile: packages, boot menus, and the files laid over the image. |
| [iso/airootfs/usr/local/bin/](iso/airootfs/usr/local/bin) | The system scripts: `omni-install`, `omni-session-select`, `omni-greeter`, and the rest. |
| [scripts/](scripts) | Building the ISO, and running and driving it in QEMU. |
| [tests/](tests) | Tests for the core. |

## How it works

### One session, two modes, and no display manager

Both modes are one Plasma session — a lean set of Plasma rather than all of KDE.
Desktop Mode is Plasma as it is. Game Mode is the OmniOS launcher open over it,
maximised rather than full screen, so KDE's own panel stays along the bottom:
the same taskbar, tray, clock and notifications in both modes. Switching is
opening or closing one window. `omni-session-select game|desktop` does it, and
that is all *Switch to desktop* and the desktop's *Game Mode* button call;
whatever was running stays open across the switch.

Game Mode opens onto an empty taskbar. The desktop's pinned apps are saved and
cleared through Plasma's scripting interface, and put back when the desktop
returns; the launcher itself is kept off the taskbar by a small KWin script;
and the panel stops floating, so no wallpaper shows around it. What appears on
the taskbar in Game Mode is only what gets started from the library.

What opens while Game Mode is on — a game, an app from the Apps tab — opens
full screen, as on a console; dialogs stay ordinary, on top of their window.
Leaving Game Mode turns those windows back into ordinary desktop windows. The
same KWin script does both, and keeps the launcher off the taskbar.

Game Mode used to be a second session on Hyprland. Moving it into Plasma took
out a compositor restart on every switch, a second polkit agent, a second config
format (Hyprland's, which changes in 0.57) and a second stack to keep working
across sleep.

There is no login manager on the live image. tty1 signs in by itself, and
`.bash_profile` starts Plasma, and starts it again if it ends. A login that
begins in Game Mode — the boot menu's Game Mode entry, or Game Mode picked at
the sign-in screen — opens the launcher from a Plasma autostart entry. The loop
keeps everything the start-up already had to get right — handing the display
over from the boot splash, logging to the serial port — and it refuses to spin:
a session that ends within seconds three times running stops the loop with the
reason on screen.

The launcher's Apps tab hides what shipped with the image, including OmniOS's
own desktop entries: *Game Mode* and *Install OmniOS* are the desktop's doors
into what Game Mode already is.

### The boot menu

On the USB stick the menu shows for ten seconds: BIOS machines get a vesamenu in
the launcher's colours, UEFI machines systemd-boot's plain list. *Install
OmniOS* boots the same live desktop with the installer already open — closing it
leaves an ordinary live session, so choosing it commits to nothing. An installed
system boots straight through, with no menu.

### Installing

[omni-install](iso/airootfs/usr/local/bin/omni-install) does the work and the
installer screen only starts it and shows its progress, so exactly one place
decides which disks may be erased.

It installs the live system itself: the squashfs the machine booted from,
unpacked onto the disk, so what is installed is exactly what was tried. The disk
gets an EFI system partition and an ext4 root, and both boot loaders —
systemd-boot for UEFI, extlinux behind GPT's MBR stub for BIOS — so the same disk
starts in either kind of machine.

Before anything is offered, each disk is looked at, read-only and with journals
left alone. An operating system on it is named — Windows from its boot manager or
`Windows\System32`, a Linux system from its `os-release` (parsed, never
sourced) — and such disks are listed last and flagged. Erasing any disk that
holds anything takes `erase` typed out; `omni-install` itself refuses a disk with
an OS on it unless told which OS, so no caller can skip that.

The language and keyboard come first, before the account, because of the
keyboard. A layout picked there applies to the live session at once, so the
password typed on the next page is typed the way it will be at every sign-in
afterwards; chosen later, a German keyboard would type a US password with `y`
and `z` swapped, and the new system would never accept it. KWin takes the change
from `kxkbrc` through KConfig's change notification (`kwriteconfig6 --notify`);
the `/Layouts` `reloadConfig` D-Bus signal most guides give is no longer
listened to. Picking a language suggests a layout for it, but only a Latin one:
a Cyrillic or Arabic layout is installed after US English, since usernames have
to be typed in Latin letters, and Meta+Alt+K switches between them.

`omni-install --locale de_DE.UTF-8 --keyboard de` then generates the locale
beside en_US, writes `locale.conf`, the console keymap (translated through
systemd's `kbd-model-map`), X11's keyboard file where `localectl` and the
sign-in screen read it, and the account's `kxkbrc`, which is where Plasma does.
Qt names each language in itself, so the list is sorted as its readers would
look: Deutsch under D.

The account step creates the user by renaming the live `omni` account, so its
groups and permissions carry over. The password travels over stdin, never the
command line, and never reaches the install log. On the installed system ssh is
off and root is locked, because the live image's password is printed in this
repository. *Skip* keeps a passwordless `omni` account for a console with only a
controller.

With a password, changing the system asks for it, as on any other system:
`sudo` stops being passwordless, and so does Discover for system packages.
Game Mode asks in a box of its own when a tile's menu updates or removes a
system app, checking the password with sudo before anything runs. Flatpaks,
power and the rest never ask. Without a password there is nothing to type, so
that install keeps the live image's passwordless rules.

### Signing in

With *sign in automatically* off, tty1 shows an OmniOS sign-in screen instead of
a text prompt. greetd asks PAM whether the password is right; the screen itself
([GreeterWindow.qml](src/launcher/GreeterWindow.qml)) runs full screen under
cage as greetd's unprivileged user and never decides that for itself. It offers
Desktop or Game Mode, and Restart and Shut down. Signing out comes back to it;
switching between modes, which never ends the session, does not.

### The desktop's OmniOS look

Plasma's application launcher cannot be given a new power button by
configuration — those buttons come from a fixed list compiled into Plasma. So
[make-desktop-launcher.sh](scripts/make-desktop-launcher.sh) takes Kickoff's QML
at build time, for exactly the Plasma release being installed, adds *Game Mode*
and the OmniOS mark, and installs it as its own widget, which the default panel
uses instead. If anything about that fails, the build keeps Plasma's own
launcher: a KDE logo in the corner is fine, a desktop with no menu is not.

The screen shown while Plasma starts is the boot splash continued — the same
images, same size, same place — so from the power button to the desktop there is
one OmniOS screen rather than an OmniOS one followed by a KDE one.

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

OmniOS does not have a store of its own: the desktop has **Discover**, KDE's,
installing from **Flathub**. Game Mode's Apps tab has one built-in tile, Steam —
a browser, a file manager and the rest are a switch to the desktop away, where
they are Plasma's own — and whatever Discover installs appears beside it.

Flathub rather than the repositories is the right way round for a console: a
Flatpak carries its own libraries, so installing one cannot drag the base system
into the partial upgrade that `pacman -Sy something` invites. `omnios-flathub`
adds the remote at boot, because a fresh Flatpak install has no remotes at all
and the store then opens onto an empty shelf with nothing to say about why.

The other half is knowing what is *on* the machine, and that is
[DesktopEntry.cpp](src/omnios/DesktopEntry.cpp): the Apps tab is built from
freedesktop desktop entries, so anything installed — from the store, from
pacman, by hand — becomes a tile with nothing added to a table in this
repository.

Flatpak's `Exec` needs one thing beyond the spec. Every exported entry looks
like this:

    /usr/bin/flatpak run ... --file-forwarding org.videolan.VLC @@u %U @@

The `@@u … @@` pair brackets the arguments that are file paths, for a launcher
that has files to hand in. Dropping `%U` and keeping the brackets leaves
`flatpak run … @@u @@`, and flatpak takes those as the files it was promised —
which is why an app installed from the store appeared as a tile, started, and
immediately went away again. The markers go with the field codes.

The cost is that everything the image itself drags in would become a tile too:
settings dialogs from the file manager's dependencies, `avahi-discover`,
`cmake-gui`. So `build-iso.sh` records the desktop entries that ship with the
image — computed from the dependency closure of `packages.x86_64` — and the
launcher hides those. A missing list means no filtering, which is the safe
direction.

### Getting back to the library

What the launcher opens appears over it, and closing it brings the library
back. The controller's Guide button brings the library back from inside a
running game as well. A Wayland app cannot raise its own window — KWin treats
that as focus stealing and only flashes the taskbar entry — so
[omni-kwin-activate](iso/airootfs/usr/local/bin/omni-kwin-activate) asks KWin
to, by loading a three-line script through its D-Bus scripting interface.

### The controller

A console you can only drive with a keyboard is not a console. The shell takes a
gamepad by becoming the keyboard rather than growing a second set of
navigation: each press is turned into the key the shell already answers to and
posted to whatever holds focus. The installer and the sign-in screen take a
controller the same way.

| Xbox | PlayStation | Does |
|---|---|---|
| D-pad / left stick | D-pad / left stick | Move, with hold-to-repeat |
| A | ✕ | Open |
| B | ○ | Back, close a menu |
| X | □ | That tile's menu |
| Y | △ | Rescan |
| LB / RB | L1 / R1 | Switch tab |
| Menu | Options | The system menu |
| Xbox | PS | Back to the library, from anywhere |

SDL3, because Qt 6 has no gamepad module and SDL carries the mapping database
that makes a DualSense and an Xbox pad behave the same. CMake treats it as
optional, but `build-iso.sh` refuses to build an image whose launcher lacks it:
every image before 2026-09-27 shipped keyboard-only because the build container
had no SDL3, and nothing short of a real pad would have shown it.

A DualSense or DualShock 4 needs nothing added over USB: `hid-playstation` is in
the kernel and SDL3 carries the mapping. SDL's own PlayStation driver needs the
pad's hidraw node, which is root's by default, and Arch's `steam` package ships
no rule for it — so [60-omnios-controllers.rules](iso/airootfs/etc/udev/rules.d/60-omnios-controllers.rules)
hands it to whoever is signed in, and the launcher sets the light bar to OmniOS
purple when the pad connects. What the screen calls the buttons follows the pad
in hand: ✕ ○ □ △, L1 R1 and Options on a PlayStation pad; A B X Y, LB RB and Menu
on an Xbox one. Each is drawn as a badge in the line, as consoles show them:
the face buttons as a disc in the pad's own colours (a green A; PlayStation's
blue cross and pink square), the rest as a small pill
([PadGlyphs.cpp](src/launcher/PadGlyphs.cpp)). With the keyboard in use, the hints show
keycaps the same way (Enter, Esc, F5, the arrows). Wireless needs bluez, and *Pair a controller* in
the system menu holds a scan open long enough to walk to the console, then
pairs, trusts and connects the first gamepad it sees.

SDL reads the input devices directly rather than through the compositor, which
is what makes the Guide button work from inside a running game — and means every
other press has to be ignored while a game runs, or the grid would move
underneath it.

It also means KDE never sees the controller, and KDE's idle timer counts only
keyboards and mice: with nothing but a pad in use, the screen would dim, turn off
and the machine sleep in the middle of a game. So the launcher reports pad
activity to KDE (`org.freedesktop.ScreenSaver.SimulateUserActivity`), at most
every thirty seconds, including while a game runs.

### Typing with a controller

Game Mode asks for text in two places: the administrator password, and a new
Wi-Fi network's password. With a controller in hand each shows an on-screen
keyboard ([OnScreenKeyboard.qml](src/launcher/OnScreenKeyboard.qml)): the D-pad
moves, A types, X deletes, Y is a space, L1 Shift, R1 symbols, Start is done and
B cancels. With a keyboard in hand it stays out of the way.

The installer's boxes and the sign-in screen's password use the same keyboard
([FieldKeyboard.qml](src/launcher/FieldKeyboard.qml)), opened by A on a box: an
empty box says so while a controller is in use. Done keeps the text and moves
on to the next box, or signs in; B puts the box back as it was. So an account
with a name and a password can be made, and signed in to, with only a pad.

The launcher tells the two apart by marking every key it posts for the
controller with a scan code no keyboard sends; touching a real key or the mouse
clears the mark. A Wi-Fi password then goes to NetworkManager in a file only
this user can read, deleted as soon as `nmcli` has it — never on a command line,
where any process on the machine could read it. Network names, which are
whatever an access point broadcasts, are quoted before they reach a shell.

### TV

OmniOS TV (`omni-launcher-qml --tv`, the *TV* tile on the Apps tab and *TV* in
the desktop's application menu) plays free-to-air channels from
[iptv-org](https://github.com/iptv-org/iptv): a public list of publicly
available streams — news, public broadcasters and the like — published as M3U
playlists by country and by category. Nothing is hosted by OmniOS or by
iptv-org; a channel is a name, a logo and a stream address that TV plays.

One row across the top holds every filter: *Country*, *Category* and
*Language* — each a list with its own search, closest match first, where any
number of choices can be ticked — a search over channel names, and ★ for
favourites only. Within a filter any ticked choice will do; across filters all
must: India or Nepal, and News, and Hindi or Telugu. Each list counts what its
choices would leave given the others, and leaves out the ones that would leave
nothing. The open list is one overlay for the whole window, so a click on an
option can never reach a channel behind it, a click outside closes it, and only
one list is ever open.
The country starts as your own (from the system language, else the time zone).
iptv-org publishes its whole list grouped each of those three ways; the three
are merged by stream (`TvCatalog` in [Iptv.cpp](src/omnios/Iptv.cpp)), so each
channel carries all three. The channels fill the screen below as tiles with
their logos. A plays full screen; Y keeps a channel in favourites. While a
channel plays the controller is a remote: up and down are volume, LB and RB
change channel, and a channel that turns out to be dead is skipped (up to five
in a row), as a TV skips an empty one. B stops.

Many of the streams are offline on a given day or only answer inside their own
country — iptv-org marks the ones it knows are geo-blocked — so a channel that
will not play says so rather than leaving a black screen. The three lists (about
8 MB together) are cached for a day in `~/.cache/omnios/tv`, and cached lists
are shown, marked as such, when the network is down. [Iptv.cpp](src/omnios/Iptv.cpp) reads the playlists, and
only ever hands the player an http(s) address.

Every channel plays inside TV's own window ([TvPlayer.qml](src/launcher/TvPlayer.qml)),
under a bar of OmniOS's controls — *Back*, the
channel's name, *Audio* and *Subtitles* (the stream's
tracks, and *Off*; each says what there is even when there is one or none),
volume down, level (press to mute) and up, and ★ — shown on any key or mouse
movement and faded after a few seconds; eight with something in the bar
chosen, after which the keys go back to the picture, as on a TV. An Audio or
Subtitles list closes itself after eight seconds untouched. Up and down (or the wheel) are volume,
the pad's shoulders (PgUp/PgDn) change channel, M mutes, and
Enter goes into the bar, where left and right move between its controls. Left and
right on the picture do nothing: a stick held to one side repeats its
direction, and when they changed channel a nudge raced through the list; a
shoulder never repeats, and one press is one channel. Esc,
Backspace, B on a pad, a right-click or *Back* all leave. While a stream connects it says so; one that will
not play — an error, an end, or 25 seconds of nothing — shows a card with
*Next channel* and *Back to channels* instead of a black screen. One that has
played and then drops — a server that closes the connection at an ad break —
is reconnected up to three times first.

**The player is mpv**, as a library: [MpvItem](src/launcher/MpvItem.cpp) has
libmpv draw each frame with OpenGL into TV's own scene (its render API), so the
bar, the lists and the guide sit over the picture, and the window is OpenGL
whatever Qt would have chosen — with no GPU, Mesa's software OpenGL, which
plays 1080p in the VM. It was Qt Multimedia first, and three things moved it:
Qt's player cannot send a user agent or a referrer, which about one stream in
eight needs; its FFmpeg refuses an HLS segment whose address has no file
extension, which is every segment of amagi's channels (Samsung TV Plus, many of
them Indian — 9X Jhakaas, for one), with no setting to allow it; and it stopped
dead at a corrupt frame where mpv carries on. Those channels had been handed to
mpv as a separate full-screen program, without OmniOS's bar; now there is one
player, and every channel has the same controls. mpv's own keys, on-screen
controls and scripts are off; its warnings go to TV's log (the journal, when
the launcher opened it), and `OMNIOS_MPV_DEBUG=1` makes them verbose.

A channel's playlist is often a choice of qualities, and TV reads it first and
hands the player one: the tallest picture up to the screen's height and never
below 1080p, since the best copy is often the only one with the extra
languages (`pickHlsVariant` in [Iptv.cpp](src/omnios/Iptv.cpp); a playlist with
separate audio renditions is passed on whole), asked for with the channel's
user agent and referrer, as the player will ask. The audio list names each
language once, in its own script — हिन्दी, తెలుగు, বাংলা for Sony Yay — and
*Main audio* for a track with no language.

**What is on.** A tile shows the programme on now under the channel's name,
with a thin bar for how far into it; the player shows it too, with the bar
(*Now*, its times, how far in, a two-line description, and what is *Next*).
iptv-org hosts no guides — its epg project is a scraper for anyone to run, and
the community copies it once listed are gone — so the guides come from
[epgshare01](https://epgshare01.online/epgshare01/), which publishes XMLTV
files per country, days ahead, rebuilt daily. Its channel ids are not
iptv-org's, so channels are matched by name, reduced to what tells them apart
("Sony Yay (1080p)" and "SONY YAY!" are both `sonyyay`), and only within the
channel's own country, from the end of its iptv-org id (`.in`). Measured on
India that covers 432 of 739 channels; the rest are small local channels no
guide has. Guides are fetched for the countries ticked (your own when none
is) and the channel playing: India's are three files, 5 MB packed and 80 MB
unpacked, read on a worker thread in about 50 ms and then kept only for those
channels, from three hours back to a day and a half ahead. They are cached for
a day and read again every twelve hours. [Epg.cpp](src/omnios/Epg.cpp) does the
matching and reading; a channel no guide covers simply shows none.

Everything above works from a controller: the D-pad and A in the filters and
their lists, A on a search box for the on-screen keyboard, Y for favourites,
and in the player the D-pad and shoulders as listed, X to mute and B to leave.
Every program reading the pad sees every press, so TV acts on one only while
it is the window in front: after the Guide button brings the launcher back, TV
behind it stays still.

### Updates

[omni-update](iso/airootfs/usr/local/bin/omni-update) is the whole of it:
`check` lists what an update would change, without root and without changing
anything (pacman's side is `checkupdates`, which syncs a throwaway copy of the
databases); `apply` updates the signing keys, then everything with
`pacman -Syu`, then Flatpak apps, and says whether a restart is needed —
when the running kernel's modules have gone, it has been replaced.

Everything, always. Arch is released as one piece, and upgrading one package
without the rest (`pacman -Sy foo`) is a partial upgrade: the classic way to a
system whose programs no longer match their libraries. The Apps tab's per-app
*Update* used to do exactly that for pacman apps; it now updates the system.
Emulators are packages like any other, so this keeps them current too — there
is no second update channel for them.

Game Mode checks a minute after starting and every six hours, quietly — an
offline machine is not an error worth repeating — and shows *N updates
available* in the top bar. The system menu then offers *Update system*, which
asks once ("Not now" under the cursor), then asks for the password, and
narrates pacman's progress (*12 of 340 · mesa*) while it runs. After a kernel
update the menu offers *Restart to finish updating*. It refuses on the live
image, which runs from RAM, and while Discover holds pacman's lock.

### The system menu

KDE's panel under Game Mode has sound, network, Bluetooth and the clock for the
pointer. A controller cannot reach the panel, so the launcher's own system menu
(**F10**, or Start) has them too: sound opens volume, mute and the output list;
network lists Wi-Fi in range with signal strength; Bluetooth lists devices.

Messages that matter — "the disk is full", "Steam could not start" — appear in
the launcher's top-right corner for a few seconds when they happen, then get
out of the way.

[SystemStatus.cpp](src/launcher/SystemStatus.cpp) reads NetworkManager, bluez
and PipeWire through `nmcli`, `bluetoothctl` and `wpctl` rather than their D-Bus
APIs: the shell already shells out for everything else.

### Switching, and turning the machine off

**F10** (or Start on a controller) opens the launcher's system menu in its
top-left corner: sound, network, Bluetooth, pairing, *Switch to desktop*,
*Install OmniOS* on the live image, Sleep, Restart and Shut down. Power actions
go through a fixed table and `systemctl` rather than sudo, so logind still runs
its inhibitors.

The OmniOS start menu in Plasma's panel switches too. Its button beside Sleep
asks which mode is on each time the menu opens: *Game Mode* on the desktop,
*Desktop* in Game Mode. (Not "Switch to desktop": Kickoff folds its whole
button row into its Session menu once the row outgrows the menu, and an added
button vanishes when it does.)

The mark in the panel is drawn to match the boot splash's; the launcher's copy
is [OmniLogo.qml](src/launcher/OmniLogo.qml).

### Sleep, and getting back

Linux arms almost nothing as a wakeup source, so a suspended machine ignores the
gamepad and keyboard. `99-omnios-wakeup.rules` arms USB devices *and* their host
controllers — a device armed behind a controller that is not armed still cannot
wake anything. The power button behaves like a console's: a tap sleeps, a long
press powers off.

`zz-omnios-resume` brings the shell back. Plymouth ships no sleep hook, and this
profile masks its quit service for the boot handoff, so on resume nothing took
the splash down and it sat holding the display; the hook takes it down, and
KWin switches its outputs back on by itself.

### The tile menu

Every app tile carries three dots (or press **M**): open, check for update,
update, uninstall. Entries that do not apply are left out rather than greyed —
except uninstalling a system app, which stays visible and disabled with the
reason, because that is a rule rather than a state.

The rule belongs to the **package**, not the tile: two tiles can share one
package, and removing it from either would break both.

```cpp
bool packageIsProtected(std::string_view package);
```

Removal follows wherever the app came from — `flatpak uninstall`, or `pacman`
on the package that owns the desktop entry. `checkupdates` backs the update check
without running `pacman -Sy`, which on Arch leaves a system one partial upgrade
from a mismatched libc.

On the live image the root filesystem is a RAM overlay, so anything installed is
gone at the next boot, and there is only so much room. The Apps tab says both,
and turns red when space runs low, because a full overlay is exactly what makes
apps stop starting with no explanation. `cow_spacesize` is half the machine's
RAM rather than a fixed number, because opening Steam once fills 2 GB by itself.

### Steam

Steam installs into `~/.local/share/Steam/steamapps`, and games installed through
it never appeared on the Games tab. `omni-steam-library` symlinks that directory
to `~/Games/steam` before either session starts, so Steam keeps its own layout
while living where the rest of the library does. It will not move an existing
library that has anything in it.

[SteamLibrary.cpp](src/omnios/SteamLibrary.cpp) reads Steam's own
`appmanifest_<appid>.acf` records rather than guessing from directory names —
more accurate, and the only way to get the app id, because a Steam game is
launched by id:

    steam steam://rungameid/440

The client has to be running for DRM, the overlay and cloud saves, so the route
is through Steam even when the binary is easy to find.

That makes the `steam` process no measure of the game. With the client already
running it hands the id over and exits at once, which the launcher used to take
as the game ending — coming back over it as it started. With no client it
becomes the client and stays, so the game "ran" as long as Steam did, and
starting anything else terminated Steam. So a Steam game is followed through
the process Steam runs every game under: its reaper, whose command line reads
`reaper SteamLaunch AppId=<id> -- <game>`. Seen appearing and then gone is the
game ending, and only then does the library come back. If it never appears —
Steam updating, a first-run dialog — the launcher stays out of Steam's way and
stops waiting after ten minutes. Starting something else stops the game by
signalling the reaper and everything under it, never Steam itself.

While anything runs, the pad leaves the launcher alone, so its presses reach
the game. Once Guide has brought the launcher to the front it drives the
launcher again: nothing else is listening then.

From there, the system menu (Start, or F10) opens on *Resume* and *Quit* for
what is running, and so does the running game's own tile menu. Resume raises
the game's window through `omni-kwin-activate --pid`: a game's window class is
whatever its engine picked, but its processes are known — the launcher's child
and everything under it, or Steam's reaper and everything under that. Quit asks
first, with *Keep playing* under the cursor, then sends SIGTERM to the whole
tree and, three seconds on, SIGKILL to whatever ignored it — a hung game does.
Each process is noted with its start time as well as its id, so nothing that
has taken a freed id since is touched.

Not every manifest is a game. Steam installs Proton, the Steam Linux Runtimes
and its Windows redistributables the same way, and a first download writes its
manifest long before there is anything to play; none of those get a tile (the
`StateFlags` "fully installed" bit decides the second). Libraries on other
drives, listed in `libraryfolders.vdf`, are read too. Covers are the ones Steam
already caches for its own library view, under `appcache/librarycache` — the
tall capsule, or the wide header when that is all there is.

A Steam tile has a menu (M, or □/X on a pad): Play, and three things only Steam
can do, handed to it through its own links — *Open in Steam*
(`steam://nav/games/details/<id>`, the game's library page, where Properties
is), *Verify game files* (`steam://validate/<id>`) and *Uninstall*
(`steam://uninstall/<id>`, which Steam confirms itself). The detail screen adds
when it was last played, from the manifest's `LastPlayed`.

Games are installed in Steam's window, not through the launcher, so the Games
tab watches for the result: when the launcher comes back to the front, and every
ten seconds while it is there, it compares the Steam games it would show — ids,
titles, covers — with the last scan, and rescans on a difference. Comparing
what a tile shows rather than file times matters: Steam rewrites a manifest
every few seconds during a download, and a rescan resets the grid.

### Routing

The engine table in [Router.cpp](src/omnios/Router.cpp) is data, deliberately.
The emulator landscape moves: **Ryujinx was discontinued in October 2024** and
**Citra was taken down in March 2024**, so Switch and 3DS route to the maintained
forks (Ryubing, Azahar) instead, and swapping either is a one-line change.

PS5 routes to shadPS4 as the closest available Orbis layer. That covers a subset
of PS5 titles today, not the platform.

### The library cache

`~/.omnios/library.json` is only ever a faster copy of what a scan produces: it
is written through a temp file and rename, version-stamped, and discarded rather
than trusted if it is corrupt or stale. A rescan keeps the two things it cannot
rederive — fetched cover art and an engine the user pinned by hand.

## The core, on its own

`omnios_core` and `omnictl` build and test anywhere, with CMake 3.20+ and a
C++20 compiler and nothing else — no network access, no third-party libraries.
The shell under `src/launcher` needs Qt 6 and is skipped where there is none, so
a build on Windows compiles the core and the tests but not the launcher; the ISO
build compiles the launcher.

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

`OMNIOS_GAMES_DIR` and `OMNIOS_DATA_DIR` override `~/Games` and `~/.omnios`, so
the whole thing can be pointed at a scratch tree:

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

`launch` is a dry run unless given `--run`, so the routing can be inspected on a
machine with no emulators installed.

| File | Phase | Does |
|---|---|---|
| [Platform.cpp](src/omnios/Platform.cpp) | — | The platform registry. Every platform-specific fact lives in one table. |
| [Paths.cpp](src/omnios/Paths.cpp) | — | Where games and data live, and the overrides for both. |
| [Json.cpp](src/omnios/Json.cpp) | 6 | Small total JSON parser. Never throws; malformed input returns an error string. |
| [Manifest.cpp](src/omnios/Manifest.cpp) | 6 | `.opkg` manifest parsing and validation, split into blocking errors and warnings. |
| [Detector.cpp](src/omnios/Detector.cpp) | 9.1 | Magic bytes → platform, with extension and folder fallbacks. |
| [GameScanner.cpp](src/omnios/GameScanner.cpp) | 7 | Walks `~/Games/`, identifies titles, builds the library. |
| [SteamLibrary.cpp](src/omnios/SteamLibrary.cpp) | 7 | Steam's installed games, from its own manifests. |
| [GameLibrary.cpp](src/omnios/GameLibrary.cpp) | 7.5 | The tile model plus its `~/.omnios/library.json` cache. |
| [Router.cpp](src/omnios/Router.cpp) | 9.2–9.5 | Platform → engine → argv, with install hints when a layer is missing. |
| [Apps.cpp](src/omnios/Apps.cpp) | 10 | The built-in app tiles, and which packages must not be removed. |
| [DesktopEntry.cpp](src/omnios/DesktopEntry.cpp) | 10 | Installed apps from their freedesktop desktop entries. |

## Tests

95 tests, run by `ctest` or directly as `build/tests/omnios_tests`. The harness
is [tests/Test.h](tests/Test.h) — self-registering `TEST`/`CHECK`, so building
the ISO never needs to fetch a test framework. They cover the core; the shell,
the installer and the sign-in screen are tested by booting them, as described in
the boot-os skill.

## Not done yet

- **Real hardware.** Everything so far has run in QEMU only.
- **Phase 6's `.opkg` installer** — extracting a package into `~/Games` with
  checksum verification — and **Phase 7.4 cover art**.
- **Sleep on real hardware** is untested. In QEMU the system resumes, but the
  VM's own faults — its watchdog, its virtual GPU, its ACPI timer under WHPX —
  get in the way; Desktop Mode's sleep works there with the workarounds in the
  boot-os skill, and Game Mode's cannot be tested in the VM at all.
