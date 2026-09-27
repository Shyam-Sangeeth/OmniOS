# OmniOS
> "Play everything."

A custom gaming OS based on Arch Linux. Runs every game from every platform — natively where possible, emulated where not. Two modes on one system: a Plasma desktop by default, and Game Mode — a PS5-style tile launcher made for a controller — one button away.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Build Order](#2-build-order)
3. [Phase 1 — Base System](#3-phase-1--base-system)
4. [Phase 2 — Hardware Support](#4-phase-2--hardware-support)
5. [Phase 3 — Display System](#5-phase-3--display-system)
6. [Phase 4 — Core Services](#6-phase-4--core-services)
7. [Phase 5 — File System & Storage](#7-phase-5--file-system--storage)
8. [Phase 6 — Package Format & Installer](#8-phase-6--package-format--installer)
9. [Phase 7 — Game Library & Scanner](#9-phase-7--game-library--scanner)
10. [Phase 8 — Compatibility Layers](#10-phase-8--compatibility-layers)
11. [Phase 9 — Compatibility Engine](#11-phase-9--compatibility-engine)
12. [Phase 10 — Launcher UI](#12-phase-10--launcher-ui)
13. [Phase 11 — Branding & Polish](#13-phase-11--branding--polish)
14. [Phase 12 — System Updates](#14-phase-12--system-updates)
15. [Phase 13 — Distribution](#15-phase-13--distribution)
16. [Layer Architecture](#16-layer-architecture)
17. [UI Design](#17-ui-design)
18. [File Manager & Install Flow](#18-file-manager--install-flow)
19. [.opkg Package Format](#19-opkg-package-format)
20. [Quick Reference](#20-quick-reference)
21. [Resources](#21-resources)

---

## 1. Overview

```
OmniOS
  = Arch Linux base (minimal install)
  + linux-zen kernel       (low-latency gaming patches)
  + Desktop Mode           (KDE Plasma on Wayland — the default session)
  + Game Mode              (PS5-style tile launcher, in the same Plasma session)
  + Steam + Proton + DXVK  (Windows games at near-native speed)
  + All emulators           (PS4, Switch, PS3, PS2, GameCube...)
  + Disk installer + sign-in screen
  + Custom branding
```

### Two modes

OmniOS is one system with two faces, and either one is a button away from the other.

| | Desktop Mode | Game Mode |
|---|---|---|
| For | Keyboard and mouse at a desk | A controller on the sofa |
| Session | KDE Plasma (Wayland), a lean set of it | The same Plasma session, with the OmniOS launcher open |
| Shows | A normal desktop, OmniOS-branded | The game library as tiles, filling the screen above Plasma's panel |
| Store | Discover (Flathub) | None of its own — Discover, a switch away; what it installs appears on the Apps tab |
| Switch | *Game Mode* button in the application launcher | *Switch to desktop* in the system menu |

Desktop Mode is the default, because a new user meets a desktop they already know, and a PC that is also a desk machine needs one. Game Mode is still the whole console experience from Phase 10: it can be chosen at boot, at sign-in, or from the desktop, and a controller-only install can live in it.

Both modes are one Plasma session, sharing one user, one home folder, one game library, one set of installed apps and one taskbar — KDE's own panel stays along the bottom in Game Mode. Switching modes opens or closes the launcher; nothing restarts, and whatever is running stays running.

### Execution Strategy

| Platform | Method | Performance |
|---|---|---|
| Linux native | Direct exec | 100% |
| Windows games | Proton (API translation) | 90–100% |
| PS4 / PS5 | Shadps4 (Orbis API layer) | 85–95% |
| Xbox One | Proton (NT layer) | 85–95% |
| Android games | Waydroid container + JIT | 75–85% |
| Switch | Ryujinx (ARM64 JIT) | 70–85% |
| PS3 | RPCS3 (full emulation) | 60–90% |
| PS2 | PCSX2 (full emulation) | 95%+ |
| GameCube / Wii | Dolphin (full emulation) | 95%+ |
| PS1 / retro | DuckStation / RetroArch | 99%+ |

### Prerequisites

- PC with at least 8 GB RAM, 50 GB free disk
- USB drive (8 GB+) for Arch install
- AMD GPU recommended (best open-source Vulkan support)
- Nvidia works but needs extra driver steps

---

## 2. Build Order

```
Phase 1   Base System          ← start here
Phase 2   Hardware Support
Phase 3   Display System
Phase 4   Core Services
Phase 5   File System
Phase 6   Package Format
Phase 7   Game Library
Phase 8   Compatibility Layers
Phase 9   Compatibility Engine
Phase 10  Launcher UI
Phase 11  Branding
Phase 12  Updates
Phase 13  Distribution
```

Each phase is independently testable before moving to the next.

---

## 3. Phase 1 — Base System

**Goal:** A bootable minimal Linux system. No GUI yet.

### 1.1 Download Arch ISO

```
https://archlinux.org/download/
```

Flash to USB:
```bash
dd if=archlinux-*.iso of=/dev/sdX bs=4M status=progress
# or use Balena Etcher (GUI tool)
```

### 1.2 Boot and install

```bash
archinstall
```

When asked:
- **Profile** → Minimal (no desktop environment)
- **Kernel** → linux-zen
- **Audio** → PipeWire
- **Filesystem** → ext4 (or btrfs for snapshots)
- **Hostname** → omnios
- **User** → create a user with sudo

### 1.3 Post-install base packages

```bash
sudo pacman -Syu
sudo pacman -S git base-devel curl wget vim

# AUR helper
git clone https://aur.archlinux.org/yay.git
cd yay && makepkg -si
```

### 1.4 Set up auto-login

Edit `/etc/systemd/system/getty@tty1.service.d/autologin.conf`:
```ini
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin yourusername --noclear %I $TERM
```

Auto-login is the default, and the live USB always uses it. An installed system can turn it off in the installer, and then shows the OmniOS sign-in screen instead (see [Phase 13](#15-phase-13--distribution)).

---

## 4. Phase 2 — Hardware Support

**Goal:** All hardware recognized and working from command line.

### 2.1 GPU drivers

**AMD (recommended):**
```bash
sudo pacman -S mesa lib32-mesa vulkan-radeon lib32-vulkan-radeon
```

**Intel:**
```bash
sudo pacman -S mesa lib32-mesa vulkan-intel lib32-vulkan-intel
```

**Nvidia:**
```bash
sudo pacman -S nvidia nvidia-utils lib32-nvidia-utils
```

### 2.2 Audio — PipeWire

Already installed if PipeWire was chosen in archinstall. Verify:
```bash
pactl info | grep "Server Name"
# Should show: PulseAudio (on PipeWire)
```

### 2.3 Networking
```bash
sudo pacman -S networkmanager
sudo systemctl enable --now NetworkManager
```

### 2.4 USB auto-mount
```bash
sudo pacman -S udisks2
```
Plasma's device notifier mounts drives through udisks2 in both modes, so no separate automounter is needed.

### 2.5 Input devices
```bash
sudo pacman -S libinput
```

---

## 5. Phase 3 — Display System

**Goal:** A blank Wayland desktop that can display windows.

### 3.1 Install Plasma

Both modes run in Plasma — a lean set, not the whole of KDE:
```bash
sudo pacman -S plasma-desktop plasma-nm plasma-pa bluedevil powerdevil kscreen                systemsettings xdg-desktop-portal-kde konsole dolphin discover                polkit-kde-agent xorg-xwayland
```

Game Mode used to run in its own session on Hyprland. It moved into Plasma so that switching modes is opening a window rather than restarting the display, and so there is one compositor, one polkit agent and one taskbar to keep working.

### 3.2 Auto-start on login

There is no display manager. `~/.bash_profile` on tty1 starts Plasma, and starts it again if it ends:
```bash
if [ -z "$WAYLAND_DISPLAY" ] && [ "$XDG_VTNR" = "1" ]; then
    while true; do
        case "$(cat "$XDG_RUNTIME_DIR/omnios-mode" 2>/dev/null || echo desktop)" in
            game | desktop) startplasma-wayland ;;
            console)        break ;;
        esac
    done
fi
```

- The first mode comes from the boot menu (`omnios.mode=game|desktop|install` on the kernel command line) or the sign-in screen, defaulting to desktop.
- A login that starts in Game Mode opens the launcher from a Plasma autostart entry (`/etc/xdg/autostart/omnios-game-mode.desktop`).
- `omni-session-select game|desktop` opens or closes the launcher; `console` logs out, and the loop stops.
- The loop stops, with the reason on screen, if a session ends within seconds three times running.

### 3.3 Game Mode's window

The launcher opens maximised and frameless, not full screen, so Plasma's panel stays visible beneath it. Games and apps it starts open over it; closing them brings the library back. A Wayland app cannot raise its own window, so the controller's Guide button brings the library forward through KWin's scripting interface (`omni-kwin-activate`).

---

## 6. Phase 4 — Core Services

**Goal:** System tuned for gaming before any game is installed.

### 4.1 GameMode
```bash
sudo pacman -S gamemode lib32-gamemode
sudo systemctl enable --now gamemoded
```

### 4.2 MangoHud
```bash
sudo pacman -S mangohud lib32-mangohud
```

### 4.3 Kernel tweaks

`/etc/sysctl.d/99-omnios.conf`:
```ini
vm.swappiness=10
vm.max_map_count=2147483642
net.core.netdev_max_backlog=16384
```

Apply: `sudo sysctl --system`

### 4.4 CPU performance governor
```bash
sudo pacman -S cpupower
sudo systemctl enable --now cpupower
# Edit /etc/default/cpupower: governor='performance'
```

### 4.5 Zram
```bash
yay -S zram-generator
```

`/etc/systemd/zram-generator.conf`:
```ini
[zram0]
zram-size = ram / 2
```

### 4.6 Optional: CachyOS kernel (even more gaming patches)
```bash
yay -S linux-cachyos linux-cachyos-headers
# Includes BORE scheduler + Futex2
```

---

## 7. Phase 5 — File System & Storage

**Goal:** User can plug in a USB and browse/copy game files.

### 5.1 Games folder structure

```
~/Games/
  ├── pc/          ← Linux + Windows .exe games
  ├── ps5/         ← PS5 PKG files
  ├── ps4/         ← PS4 PKG / SELF files
  ├── ps3/         ← PS3 PKG or ISO
  ├── ps2/         ← PS2 ISO / BIN+CUE
  ├── ps1/         ← PS1 BIN/CUE
  ├── switch/      ← NSP / XCI + keys/ folder
  ├── gamecube/    ← GCM / ISO
  ├── wii/         ← ISO
  ├── 3ds/         ← 3DS / CIA
  ├── gba/         ← GBA / GB / GBC
  ├── android/     ← APK files
  └── retro/       ← SNES, NES, Sega, etc.
```

Create them:
```bash
mkdir -p ~/Games/{pc,ps5,ps4,ps3,ps2,ps1,switch,gamecube,wii,3ds,gba,android,retro}
```

### 5.2 File manager

Built as a full-screen Qt6 overlay accessible from the launcher top bar.

```
OmniOS Files
├── Sidebar: Home / Games / Downloads / USB drives / Network
└── Main panel: file grid with Install / Copy buttons
```

### 5.3 Network file transfer

Simple HTTP server on port 8080 — phone or PC opens `omnios.local:8080` in browser and uploads files directly.

---

## 8. Phase 6 — Package Format & Installer

**Goal:** User can double-click a .opkg and game installs correctly.

### The .opkg format

OmniOS's native game package — a ZIP with a defined structure:

```
game-title.opkg
  ├── manifest.json    ← title, platform, executable, checksum
  ├── cover.jpg        ← tile artwork
  ├── icon.png         ← small icon
  ├── screenshots/     ← optional
  └── game/            ← actual game files
```

### manifest.json

```json
{
  "omni_version": "1.0",
  "id": "com.publisher.godofwar",
  "title": "God of War",
  "version": "1.0.0",
  "platform": "ps4",
  "developer": "Santa Monica Studio",
  "publisher": "Sony",
  "description": "Kratos and Atreus journey through Norse mythology.",
  "executable": "game/eboot.bin",
  "install_size_mb": 45000,
  "checksum": {
    "algorithm": "sha256",
    "value": "a3f5c2..."
  },
  "compatibility": {
    "tier": "api_layer",
    "engine": "shadps4",
    "notes": "Runs at ~90% native speed"
  },
  "tags": ["action", "adventure", "singleplayer"]
}
```

### Platform values

| Value | Platform | Installs to |
|---|---|---|
| `linux` | Native Linux | ~/Games/pc/ |
| `windows` | Windows | ~/Games/pc/ |
| `ps5` | PS5 | ~/Games/ps5/ |
| `ps4` | PS4 | ~/Games/ps4/ |
| `ps3` | PS3 | ~/Games/ps3/ |
| `ps2` | PS2 | ~/Games/ps2/ |
| `ps1` | PS1 | ~/Games/ps1/ |
| `switch` | Switch | ~/Games/switch/ |
| `gamecube` | GameCube | ~/Games/gamecube/ |
| `wii` | Wii | ~/Games/wii/ |
| `3ds` | 3DS | ~/Games/3ds/ |
| `gba` | GBA | ~/Games/gba/ |
| `android` | Android | ~/Games/android/ |
| `retro` | Retro | ~/Games/retro/ |

### Install flow

```
User opens .opkg
    → OmniOS reads manifest.json
    → Shows confirmation screen (cover art, title, size, destination)
    → User confirms
    → Extracts game/ → ~/Games/<platform>/<title>/
    → Copies cover.jpg + icon.png → ~/.omnios/library/<title>/
    → Tile appears in launcher
```

### Fallback formats (non-.opkg)

| File | Action |
|---|---|
| `.zip` / `.rar` / `.7z` | Extract → scan contents → detect platform → install |
| `.pkg` | PS4/PS5 → extract to correct folder |
| `.exe` (setup) | Windows installer → run via Wine |
| `.nsp` / `.xci` | Switch → move to ~/Games/switch/ |
| `.iso` / `.bin` | Detect platform → move to folder |
| `.apk` | Android → Waydroid install |

### Creating a .opkg manually

```bash
mkdir my-game/game
cp game-files/* my-game/game/
cp cover.jpg my-game/
# write manifest.json
zip -r my-game.opkg my-game/
```

### Future file extensions (same zip + manifest structure)

| Extension | Purpose |
|---|---|
| `.opkg` | Game package |
| `.osave` | Save file transfer |
| `.otheme` | Launcher theme |
| `.omod` | Game mod |

---

## 9. Phase 7 — Game Library & Scanner

**Goal:** OmniOS has a complete list of all installed games with artwork.

```
7.1  Game scanner — watches ~/Games/ subfolders (inotify)
7.2  Platform detector — reads file headers to confirm platform
7.3  Metadata reader — reads manifest.json if .opkg, else basic info
7.4  Cover art — bundled in .opkg, or fetched from SteamGridDB API
7.5  Library store — in-memory + cached to ~/.omnios/library/
7.6  Auto-update — new file added = tile appears instantly
```

---

## 10. Phase 8 — Compatibility Layers

**Goal:** Every platform has a working execution layer.

### Install all layers

```bash
# Steam + Proton (Windows games)
# Enable multilib in /etc/pacman.conf first
sudo pacman -S steam

# DXVK + VKD3D (for non-Steam Windows games)
yay -S dxvk-bin vkd3d-proton-bin

# Heroic Launcher (Epic Games + GOG)
yay -S heroic-games-launcher-bin

# PS4 / PS5
yay -S shadps4-bin

# Nintendo Switch
yay -S ryujinx-bin

# PS3
yay -S rpcs3-bin

# PS2
sudo pacman -S pcsx2

# GameCube + Wii
sudo pacman -S dolphin-emu

# PS1
yay -S duckstation-bin

# 3DS
yay -S citra-bin

# Retro (SNES, NES, GBA, Sega...)
sudo pacman -S retroarch
yay -S retroarch-assets-xmb retroarch-assets-ozone

# Android
yay -S waydroid
sudo systemctl enable --now waydroid-container
waydroid init
```

### In Steam settings
- Settings → Compatibility → Enable Steam Play for all titles → Proton Experimental

---

## 11. Phase 9 — Compatibility Engine

**Goal:** Select any game → correct layer launches it automatically.

```
9.1  Binary detector  — reads magic bytes + file extension
9.2  Platform router  — maps platform → correct layer
9.3  Launch manager   — starts layer with correct arguments
9.4  Process monitor  — detects when game exits → shows launcher
9.5  Error handler    — layer missing? shows friendly install prompt
```

### Binary detection (magic bytes)

| Magic bytes | Platform |
|---|---|
| `7F 45 4C 46` (ELF) | Linux or PS4 (check ELF OS/ABI field) |
| `4D 5A` (MZ) | Windows |
| `7F 43 4E 54` | PS4 PKG |
| `4F 15 3D 1D` | PS4/PS5 SELF |
| `7F 50 4B 47` | PS3 PKG |
| `50 46 53 30` (PFS0) | Switch NSP |
| `48 46 53 30` (HFS0) | Switch XCI |
| `00 D1 5E A5` | GameCube / Wii disc |

---

## 12. Phase 10 — Launcher UI

**Goal:** Complete, usable launcher. Feels like PS5.

This is Game Mode. The desktop is Desktop Mode (see [Two modes](#two-modes)); the launcher's system menu has *Switch to desktop*, and the desktop's application launcher has a *Game Mode* button beside Sleep, Restart and Shut Down.

### Design philosophy

- Once in Game Mode, the tiles fill the screen above Plasma's panel — no desktop icons, no windows to manage
- Can be the whole system: boot straight into it (the boot menu's Game Mode entry, or Game Mode picked at sign-in)
- Every game is a tile with cover art + platform badge
- One thing in focus at a time
- Background art shifts to match focused tile (blurred + dark)
- Works equally with keyboard, mouse, or controller

### Screen layout

```
┌─────────────────────────────────────────────────────────────┐
│  GAMES   APPS                                               │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   ╔═══════════╗  ┌─────────┐  ┌─────────┐  ┌─────────┐    │
│   ║  focused  ║  │         │  │         │  │         │    │
│   ║   tile    ║  │  tile   │  │  tile   │  │  tile   │    │
│   ║  [PS4]   ║  └─────────┘  └─────────┘  └─────────┘    │
│   ╚═══════════╝                                             │
│                                                             │
│  ── All Games ──────────────────────────────────────────   │
│  ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐         │
│  │    │ │    │ │    │ │    │ │    │ │    │ │    │         │
│  └────┘ └────┘ └────┘ └────┘ └────┘ └────┘ └────┘         │
│                                                             │
│ ╭─────────────────────────────────────────────────────────╮ │
│ │ ◉  ⚙ 🛍 📁 🌐  [Running game]        🔊 🖧 ᛒ   2:47 PM │ │
│ ╰─────────────────────────────────────────────────────────╯ │
└─────────────────────────────────────────────────────────────┘
  Background = blurred art of focused game, darkened
  Taskbar = Plasma's own panel: Game Mode runs in the same session as the
            desktop, so the taskbar, tray and clock are KDE's
```

### Game detail screen

```
┌─────────────────────────────────────────────────────────┐
│                   [Game Hero Art]                        │
│                                                         │
│   God of War                              [PS4]         │
│   ─────────────────────────────────────────────         │
│   Execution: PS4 API Layer  (~90% native speed)         │
│                                                         │
│   [ PLAY ]     [ Settings ]     [ Back ]                │
└─────────────────────────────────────────────────────────┘
```

### In-game overlay (home button)

```
┌──────────────────────────┐
│  God of War              │
│  ─────────────────────   │
│  FPS: 60   GPU: 72%      │
│  VRAM: 4.2 GB            │
│                          │
│  [Resume]  [Quit]        │
│  [Screenshot]            │
└──────────────────────────┘
```

### Navigation

| Input | Action |
|---|---|
| Arrow keys / D-pad | Move between tiles |
| Enter / A button | Open game detail |
| Escape / B button | Go back |
| Home button | In-game overlay |
| Tab | Jump between rows |

### Visual design

```
Colors:
  Background       #0A0A12
  Card background  #16161F
  Focused border   #FFFFFF (white glow)
  Accent           #6C63FF (purple)
  Text primary     #F0F0F5
  Text secondary   #8888AA

Platform badge colors:
  PC               #3A8FFF
  PS4/PS5          #0070D1
  Switch           #E4000F
  Xbox             #107C10
  Emulated         #888888

Tile sizes:
  Recent row    220 × 240 px  (focused: 240 × 260 px)
  All games     160 × 180 px

Animations:
  Tile focus    scale 1.0 → 1.06, 150ms ease-out
  Screen slide  300ms ease-out
  Background    400ms crossfade
```

### Technology

| Part | Technology |
|---|---|
| Compositor | KWin (Plasma), shared with Desktop Mode |
| Launcher UI | Qt6 + QML |
| Game scanner | C++ backend |
| Cover art | SteamGridDB API |
| Font | Inter or Geist |

### Launcher project structure

```
omnios-launcher/
├── CMakeLists.txt
├── main.cpp
├── core/
│   ├── GameScanner.h/cpp
│   ├── GameLibrary.h/cpp
│   └── Launcher.h/cpp
├── qml/
│   ├── Main.qml
│   └── components/
│       ├── GameTile.qml
│       ├── GameRow.qml
│       ├── TopBar.qml
│       ├── GameDetail.qml
│       ├── InGameOverlay.qml
│       └── Settings.qml
└── assets/
    ├── fonts/
    └── icons/
        ├── pc.svg
        ├── ps4.svg
        ├── switch.svg
        └── xbox.svg
```

---

## 13. Phase 11 — Branding & Polish

**Goal:** Boots and feels like a product, not a modded distro.

From the power button to the desktop it should be one OmniOS screen, never a Linux one followed by a KDE one:

| Stage | What shows |
|---|---|
| Boot menu (USB) | BIOS: syslinux vesamenu in the launcher's colours. UEFI: systemd-boot's plain list. |
| Boot menu (installed) | None — boots straight through. |
| Boot splash | Plymouth, OmniOS theme (the mark + a progress bar). |
| Sign-in | The OmniOS sign-in screen, only when auto-login is off. |
| Desktop loading | A KSplash theme that continues the Plymouth splash: same images, size and place. |
| Desktop | Breeze Dark, with the OmniOS mark in place of the KDE logo on the application launcher. |

OmniOS does not use GRUB: the USB uses syslinux (BIOS) and systemd-boot (UEFI), and installs use systemd-boot plus extlinux, so one disk boots on either kind of machine.

Plasma's application launcher cannot be given a new power button by configuration, so the build forks Kickoff for the exact Plasma release it installs, adds the *Game Mode* button and the OmniOS mark, and uses that on the default panel. If the fork fails, the build keeps Plasma's own launcher.

Checklist:
- [x] Boot splash (Plymouth — OmniOS logo)
- [x] Boot menu theme (syslinux vesamenu; systemd-boot on UEFI)
- [x] Auto-login by default; an OmniOS sign-in screen when it is off
- [x] Desktop: OmniOS launcher icon, loading screen, dark theme
- [ ] Custom fonts bundled
- [ ] Sound effects (tile focus, launch, notification)
- [ ] Launcher theme system (.otheme packages)

---

## 14. Phase 12 — System Updates

**Goal:** System stays up to date without touching a terminal.

```
12.1  System updater (pacman backend, triggered from launcher settings)
12.2  Emulator auto-update (check GitHub releases for new versions)
12.3  Update notification in launcher top bar
12.4  One-click update from settings screen
```

**As built:** `omni-update check|apply`, behind Game Mode's system menu (there is
no settings screen; the menu is where settings live). A full `pacman -Syu` after
the keyring, then Flatpak apps — never one package on its own, which on Arch is
a partial upgrade. The top bar says *N updates available* after a background
check (a minute in, then every six hours); *Update system* confirms, asks for
the password, and shows progress; a replaced kernel adds *Restart to finish
updating*. 12.2 needs no channel of its own: the emulators are pacman or
Flathub packages, so the system update covers them.

---

## 15. Phase 13 — Distribution

**Goal:** Anyone can download an ISO and install OmniOS.

```
13.1  Build custom Arch ISO with all phases included (archiso tool)
13.2  Installer (boots ISO → installs OmniOS, typed confirmation before erasing)
13.3  First-run setup (language, keyboard, account and time zone are in the installer; GPU driver, theme)
13.4  Test on real hardware
13.5  Release OmniOS v0.1
```

### 13.1 The USB boot menu

| Entry | Does |
|---|---|
| Try OmniOS | Live Desktop Mode (the default, after 10 seconds) |
| Install OmniOS | Live desktop with the installer already open — closing it leaves an ordinary live session |
| Try OmniOS in Game Mode | Live Game Mode |
| Safe graphics (BIOS only), Console only | Troubleshooting |
| Reboot, Power off (BIOS only) | — |

### 13.2 Installer

`omni-install` does the work; the installer screen (`omni-launcher-qml --install`, from the boot menu, the live desktop or Game Mode's menu) only starts it and shows its progress, so exactly one place decides which disks may be erased.

```
Choose disk → Language and keyboard → Account → Time zone → Confirm → Installing → Done
```

- **Whole disk only.** It installs beside nothing and resizes nothing: GPT, a 1 GiB EFI system partition, ext4 for the rest. OmniOS goes on its own disk, picked from the firmware's boot menu.
- **What is installed is what was tried:** the live squashfs, unpacked onto the disk, then configured for that machine.
- **Both boot loaders** — systemd-boot for UEFI, extlinux for BIOS — so the disk starts in either kind of machine.
- **Disk safety.** Every disk is inspected read-only before it is offered. An OS on it is named (Windows, or a Linux system from its `os-release`) and such disks are listed last and flagged. Erasing a disk that holds anything takes `erase` typed out, and `omni-install` itself refuses a disk with an OS on it unless told which OS.
- **Language and keyboard.** Any UTF-8 locale glibc can generate, and any xkeyboard-config layout. The layout applies to the live session as soon as it is picked, so the password typed next matches the installed system's layout. Non-Latin layouts are installed after US English, for typing usernames.
- **Account.** Name, username, password, hostname, and *sign in automatically*. The account is the live `omni` user renamed, so groups carry over. The password travels over stdin, never argv, and never reaches the log. *Skip* keeps a passwordless `omni` account for a controller-only console.
- **Hardening.** On the installed system ssh is off and root is locked, because the live image's password is public.

### 13.3 Sign-in screen

With *sign in automatically* off, tty1 runs greetd instead of auto-login, and greetd shows the OmniOS sign-in screen (`omni-launcher-qml --greeter`, full screen under the `cage` kiosk compositor, as greetd's unprivileged user). PAM decides whether the password is right; the screen never does.

- Pick the user, type the password, pick **Desktop** or **Game Mode**
- Restart and Shut down
- Controller works the same as in the launcher
- Signing out returns to it; switching modes does not

---

## 16. Layer Architecture

### Full stack overview

```
USER picks a game in the launcher (Game Mode) or opens it from the desktop
        │
LAUNCHER SHELL
  Game Mode:    OmniOS game library UI, over Plasma
  Desktop Mode: KDE Plasma (Wayland)
        │
COMPATIBILITY ENGINE
  Detector → Router → Layer Selector
  (Native > API Layer > JIT > Emulator)
        │
    ┌───┴──────────────┬──────────────┐
    ▼                  ▼              ▼
TIER 0             TIER 1         TIER 2 & 3
NATIVE             API LAYER      JIT / EMULATOR
```

### Tier 0 — Native (Linux)

```
Linux ELF → kernel exec() → CPU (x86-64 native) → Mesa → GPU
Performance: 100%
```

### Tier 1A — Windows API Layer

```
Windows .exe
  → Wine/Proton PE loader
  → NT syscalls → Linux syscalls
  → DirectX 9/10/11 → DXVK → Vulkan
  → DirectX 12     → VKD3D → Vulkan
CPU: x86-64 native  |  Performance: 90–100%
```

### Tier 1B — PS4 / PS5 API Layer

```
PS4 SELF / PKG
  → Shadps4
  → Orbis (FreeBSD) syscalls → Linux syscalls
  → GNM / GNMX graphics → Vulkan
CPU: x86-64 native (PS4 uses same AMD arch)  |  Performance: 85–95%
```

### Tier 1C — Xbox API Layer

```
Xbox UWP app
  → Proton (NT layer)
  → NT syscalls → Linux syscalls
  → DirectX 12 → VKD3D → Vulkan
CPU: x86-64 native  |  Performance: 80–90%
```

### Tier 2A — Switch JIT

```
Switch NSP / XCI
  → Ryujinx
  → ARM64 JIT → x86-64 (recompiled at runtime)
  → Horizon OS syscalls → Linux
  → NVN graphics → Vulkan
Performance: 70–85%
```

### Tier 2B — Android (Waydroid)

```
Android APK
  → Waydroid LXC container
  → ARM64 → x86-64 via libhoudini / FEX-Emu
Performance: 75–85%
```

### Tier 3 — Full Emulation

```
PS3  → RPCS3     (Cell/PowerPC recompiler)
PS2  → PCSX2     (MIPS recompiler)
GC   → Dolphin   (PowerPC JIT)
PS1  → DuckStation (MIPS dynarec)
Retro → RetroArch
Performance: 40–99%
```

### Unified graphics backend

```
DirectX 9/10/11 → DXVK    ─┐
DirectX 12      → VKD3D   ─┤
GNM (PS4)       → Shadps4 ─┤──▶ Vulkan ──▶ Mesa ──▶ GPU
NVN (Switch)    → Ryujinx ─┤
RSX (PS3)       → RPCS3   ─┘

Mesa drivers:  AMD → RADV  |  Intel → ANV  |  Nvidia → NVK
```

### Audio — PipeWire

```
ALSA / PulseAudio / JACK / raw PCM
  → PipeWire (3–5ms latency)
  → sound card
```

### Input — libinput + SDL2

```
Controller / Keyboard / Mouse
  → libinput → SDL2 / evdev
  → each layer maps to its own input format
Controller remapping: unified in OmniOS settings
```

### Kernel features (linux-zen)

| Feature | Benefit |
|---|---|
| BORE Scheduler | Games get CPU priority |
| Futex2 | 40% faster Wine/Proton sync |
| fsync | Faster Windows game multithreading |
| CONFIG_HZ=1000 | 1ms timer precision |
| io_uring | Fast async disk I/O |
| Huge pages | Faster game memory allocation |

### Complete launch example — PS4 game

```
1. User clicks God of War in launcher
2. Compatibility Engine: reads PKG → Platform: PS4
3. Router: Tier 1B → Shadps4
4. Shadps4: unpacks PKG, loads x86-64 binary (native CPU)
5. Orbis syscalls → Linux syscalls
6. GNM graphics → Vulkan → Mesa RADV → AMD GPU
7. Audio: sceAudio → PipeWire → speakers
8. Input: DualSense → libinput → SDL2 → Shadps4
9. Game runs at ~90% native PS4 performance
```

---

## 17. UI Design

### File manager

```
┌─────────────────────────────────────────────────────────────┐
│  OmniOS Files                                    ✕ Close    │
├──────────────┬──────────────────────────────────────────────┤
│  📁 Home     │   USB Drive (32 GB)                         │
│  📁 Games    │   ─────────────────────────────────────     │
│  📁 Downloads│   📁 PS4 Games/                             │
│  💿 USB      │      📦 god-of-war.pkg          18.4 GB     │
│  🌐 Network  │   📁 Switch/                                │
│              │      🗜  zelda-totk.zip          16.0 GB    │
│              │                                             │
│              │   [ Install Selected ]   [ Copy to Games ]  │
└──────────────┴──────────────────────────────────────────────┘
```

Accessible from: launcher top bar → folder icon
Opens as full-screen overlay (same dark theme)
Escape / B to close and return to launcher

---

## 18. File Manager & Install Flow

### From .opkg

```
Double-click .opkg
  → reads manifest.json
  → shows install screen (cover, title, size)
  → user confirms
  → extracts to ~/Games/<platform>/
  → tile appears in launcher
```

### From zip / archive

```
Select zip → Install Game
  → extract to temp folder
  → scan contents for platform clues
  → move to correct ~/Games/<platform>/
  → tile appears in launcher
```

### From USB

```
Plug in USB
  → notification: "USB detected — Open in Files?"
  → browse in file manager
  → select files → Copy & Install
  → files go to correct folder
  → tile appears in launcher
```

### From network

```
On phone/PC: open browser → omnios.local:8080
  → upload game files
  → land in ~/Downloads/
  → notification: "New file — Install?"
  → same flow as zip
```

### Uninstall

```
Right-click tile in launcher
  → Uninstall
  → shows size to be freed
  → confirm
  → deletes ~/Games/<platform>/<title>/
  → tile removed from launcher
```

---

## 19. .opkg Package Format

A standard ZIP renamed to `.opkg` with a defined internal structure.

OmniOS reads the manifest — no guessing, cover art bundled, checksum verified.

Plain `.zip` still works as a fallback — OmniOS just has to scan the contents.

See [Phase 6](#8-phase-6--package-format--installer) for full details.

---

## 20. Quick Reference

### Launch commands (manual)

```bash
# Windows game
PROTON_USE_WINED3D=1 proton run game.exe

# PS4 game
shadps4 /path/to/game.pkg

# Switch game
Ryujinx /path/to/game.nsp

# PS3 game
rpcs3 /path/to/game.pkg

# PS2 game
pcsx2 /path/to/game.iso

# GameCube / Wii
dolphin-emu /path/to/game.iso

# Android game
waydroid app install game.apk

# Any game with GameMode + MangoHud
gamemoderun mangohud <launcher> <game>
```

### Layer summary

| Layer | Component | Technology |
|---|---|---|
| Shell | Launcher + compositor | Qt6/QML on KDE Plasma (KWin) |
| Compat engine | Detector + Router | Custom C++ |
| Windows | NT API layer | Wine / Proton + DXVK |
| PS4/PS5 | Orbis API layer | Shadps4 |
| Switch | ARM64 JIT | Ryujinx |
| Android | ARM container | Waydroid + FEX |
| PS3 | Full emulation | RPCS3 |
| PS2 / PS1 | Full emulation | PCSX2 / DuckStation |
| GameCube / Wii | Full emulation | Dolphin |
| Retro | Full emulation | RetroArch |
| Graphics | Unified Vulkan | Mesa (RADV/ANV/NVK) |
| Audio | Low-latency | PipeWire |
| Input | Unified | libinput + SDL2 |
| Kernel | Gaming-tuned | linux-zen + BORE |

---

## 21. Resources

| Resource | URL |
|---|---|
| Arch Linux | archlinux.org |
| Arch Wiki | wiki.archlinux.org |
| KWin scripting | develop.kde.org/docs/plasma/kwin |
| ProtonDB | protondb.com |
| Shadps4 | github.com/shadps4-emu/shadPS4 |
| Ryujinx | ryujinx.org |
| RPCS3 | rpcs3.net |
| PCSX2 | pcsx2.net |
| Dolphin | dolphin-emu.org |
| DuckStation | github.com/stenzek/duckstation |
| Waydroid | waydroid.xyz |
| linux-zen | github.com/zen-kernel/zen-kernel |
| CachyOS kernel | github.com/CachyOS/linux-cachyos |
| SteamGridDB (cover art) | steamgriddb.com |
| MangoHud | github.com/flightlessmango/MangoHud |
