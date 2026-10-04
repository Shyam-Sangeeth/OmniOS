# Building and booting the OmniOS ISO

The profile in [iso/](../iso/) is complete and syntax-checked. Turning it into
a bootable `.iso` requires an Arch Linux userspace, because `mkarchiso`
pacstraps a real system and needs loop devices and root. **It cannot run on
Windows**, and neither can the ISO build inside plain WSL without an Arch
distribution.

Pick whichever route matches what you already have.

## Route A — Docker Desktop (verified: this is how the shipped ISO was built)

Docker Desktop on Windows runs its engine inside a Linux VM, so it needs
**WSL2 or Hyper-V** — enabling either is a Windows optional feature and takes a
reboot. Docker alone is not enough on a machine that has neither. Check first:

```powershell
wsl --status            # "Default Version: 2" means WSL2 is live
Get-Service vmcompute   # Running means Hyper-V is live
```

Nothing works until those features are enabled *and the machine has rebooted* —
a pending servicing operation reports as "not installed" and Docker's engine
returns 500 with no VM behind it.

The Arch image is ~800 MB and the build pulls a few GB of packages.

```powershell
winget install --id Docker.DockerDesktop
```

Then, from the repo root:

```powershell
docker run --rm --privileged `
  -v "${PWD}:/repo" -w /repo `
  archlinux:latest `
  bash -c "pacman -Syu --noconfirm archiso cmake ninja gcc git && ./scripts/build-iso.sh"
```

`--privileged` is required: `mkarchiso` mounts loop devices, which a default
container cannot do.

In practice use the wrapper rather than that raw command. It mounts a Docker
volume for the work tree, which is not optional: `mkarchiso` builds a real root
filesystem, and a Windows bind mount cannot carry Unix ownership, setuid bits
or device nodes — pacstrap dies partway through if you try. It also refreshes
`archlinux-keyring` first, since a stale keyring in the base image makes every
package fail signature verification for no obvious reason.

```powershell
.\scripts\build-iso-docker.ps1 -KeepCache
```

`-KeepCache` persists pacman's package cache in a named volume, so a second
build does not re-download several GB.

**Disk space.** Docker Desktop keeps everything in one virtual disk
(`%LOCALAPPDATA%\Docker\wsl\disk\docker_data.vhdx`) that grows with every
build and container and never shrinks by itself: it reached 26 GB holding
10 GB. `.\scripts\compact-docker.ps1` gives the freed space back to Windows
(an administrator prompt; nothing inside Docker is deleted). Of the images,
only `archlinux:latest` (builds) and `debian:stable` (build-openbios.sh) are
used; the `omnios-pacman-cache` volume is the cache above.

The ISO lands in `out/`. Boot it from Windows:

```powershell
winget install --id SoftwareFreedomConservancy.QEMU
.\scripts\run-qemu.ps1
```

## Route B — WSL2 with Arch

Needs a reboot to install WSL, and gives you a persistent Linux environment
that is useful beyond this one build.

```powershell
wsl --install --no-distribution   # reboot when it asks
wsl --install archlinux
```

Then inside the Arch shell:

```bash
sudo pacman -Syu --noconfirm archiso cmake ninja gcc pkgconf qt6-base qt6-declarative qt6-tools sdl3 mpv layer-shell-qt
cd /mnt/c/Users/shyam/OneDrive/Documents/Projects/OmniOS
sudo ./scripts/build-iso.sh
```

WSL2 runs a real kernel with loop device support, so `mkarchiso` works. QEMU
can then run either inside WSL (`./scripts/run-qemu.sh`, using WSLg for the
window) or on Windows against the same file (`.\scripts\run-qemu.ps1`).

## Route C — an actual Arch machine

```bash
sudo pacman -S archiso qemu-full edk2-ovmf cmake ninja gcc pkgconf qt6-base qt6-declarative qt6-tools sdl3 mpv layer-shell-qt
sudo ./scripts/build-iso.sh
./scripts/run-qemu.sh --uefi
```

This is the only route where the VM gets KVM and OVMF without extra setup, so
it is much faster than the other two.

---

## What the build script does

`scripts/build-iso.sh` does three things before calling `mkarchiso`, all of
them consequences of the profile being edited on Windows:

1. **Compiles `omnictl`** and installs it into `iso/airootfs/usr/local/bin/`,
   so the image ships the actual compatibility engine rather than a stub.
2. **Creates the systemd `.wants` symlinks** listed in `iso/services.enable`.
   Git on Windows checks symlinks out as plain text files, so they cannot be
   committed; the list is text and the links are made on the Linux host.
3. **Stages `packages.aur.txt`** at `/usr/share/omnios/`, where
   `omni-first-boot` reads it.

## What a QEMU boot does and does not prove

A VM boot validates the half of the system that is about *being an OS*:

- the `linux-zen` kernel boots and the archiso initramfs finds the squashfs
- systemd-boot (UEFI) and syslinux (BIOS) entries are correct
- the sysctl drop-in, zram device and cpupower governor apply
- autologin reaches tty1, Hyprland starts, and `omni-launcher` runs
- `omnictl scan` finds games on an attached disk and routes them correctly

It cannot validate the half that is about *running games*. There is no GPU
passthrough, so Vulkan falls back to software rendering (`llvmpipe`): Proton
and every emulator will either crawl or refuse to start. That is expected and
is exactly why OmniOS.md Phase 13.4 is "test on real hardware" — the VM proves
the plumbing, hardware proves the performance.

## The boot handoff, and why it is fragile

Boot passes the display through three owners: plymouth (from the initramfs),
then Hyprland, then the launcher inside it. Two rules make that work, and both
were learned by breaking them:

1. **plymouth must be gone, not merely asked to go.** `plymouth quit` returns
   when the request is sent. `plymouth --ping` then reports the daemon gone
   while it is still alive holding DRM master. Poll for the `plymouthd`
   *process*. Until that was right the compositor could not allocate a single
   framebuffer — `DRM_IOCTL_MODE_CREATE_DUMB failed: Permission denied`, 482
   times — and every display mode was refused as a downstream symptom.
2. **Do not pass `--retain-splash`.** Retaining the splash means plymouth keeps
   the display instead of handing it back, which is the same failure.

The symptom of getting either wrong is a compositor that starts, stays running,
logs nothing alarming, and never draws. `ps` shows Hyprland and Xwayland alive;
the screen shows the console underneath. Nothing fails, so nothing points at
the cause. Read `/tmp/omnios-session.log` over the serial port
(`run-qemu.ps1 -SerialLog`) rather than inferring from screenshots.

A VM also needs a display device with a render node. `-Vga std` (bochs-drm)
exposes `card0` but no `renderD128`, so aquamarine reports "Can't create
renderer, no matching devices found" and nothing renders at all; it is useful
only for capturing the splash, because QEMU's `screendump` can read its scanout
at every stage of boot where virtio's cannot.

## The Windows build does not compile the launcher

The root CMakeLists only adds `src/launcher` when it finds Qt6, and a Windows
checkout normally has none. So `cmake --build build` on Windows builds
`omnios_core`, `omnictl` and the tests — and skips every line of
`LauncherController.cpp`, `main.cpp` and the QML.

A green local build therefore says nothing at all about the shell. A missing
terminating quote in `LauncherController.cpp` passed on Windows, tests and all,
and only surfaced inside the ISO build several minutes later.

Compile-check launcher changes in a container before building an image:

```bash
docker run --rm -v "//c/path/to/OmniOS:/src:ro" archlinux:latest bash -c '
  pacman -Sy --noconfirm --needed cmake ninja gcc pkgconf qt6-base qt6-declarative qt6-svg sdl3 zlib mpv layer-shell-qt >/dev/null 2>&1
  mkdir /work && tar -C /src --exclude=./out --exclude=./vm --exclude=./testgames --exclude=./build -cf - . | tar -C /work -xf - && cd /work
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
  cmake --build build 2>&1 | grep -Ev "^\[" | head -30'
```

It takes about a minute against a warm image and it is the only thing that
proves the shell still compiles. The source is copied without `out/` (the
ISO), `vm/` (the test disk) and `testgames/`: about 19 MB rather than several
GB, all of it read through the slow Windows bind mount.

## Two ways a build "fails" without failing

Both of these cost real time before they were understood, and neither is a
problem with the profile.

- **`docker.exe : error: command failed to execute correctly`, while the
  container is still running.** PowerShell turns anything a native command
  writes to stderr into a terminating `NativeCommandError` when the command is
  in a pipeline. `pacman-key` writes `There is no secret key available to sign
  with` during the keyring refresh, which is harmless. So
  `.\scripts\build-iso-docker.ps1 | Out-File log.txt` reports a failed build
  while `mkarchiso` carries on quite happily in the container. Run the script
  without a pipeline, or check `docker ps` before believing the error.
- **The Docker daemon is gone after you booted a VM.** `wsl --shutdown` is the
  fix for a wedged WHPX (below) and it also stops Docker Desktop's backend, so
  the next build fails at the daemon check. Start Docker Desktop again and wait
  for `docker info` to answer.

## Running the VM on Windows

`run-qemu.ps1` passes `kernel-irqchip=off,hpet=off`. Both are required and they
fix different things:

- **`kernel-irqchip=off`** — without it the VM never leaves SeaBIOS under WHPX:
  it starts, burns under two CPU-seconds and sits paused.
- **`hpet=off`** — with the emulated HPET present, the guest kernel's IO-APIC
  timer check fails and it panics during early boot:

      Kernel panic - not syncing: IO-APIC + timer doesn't work!

  The check is timing-sensitive, so the panic is intermittent. That makes it
  look like a flaky hypervisor rather than a guest that died on the first
  screenful, especially since the serial log then holds only the ISOLINUX
  banner and `screendump` appears to do nothing.

`-cpu max` is not usable here either. It asks WHPX for every feature the host
advertises, and reading the resulting vCPU's xsave state then fails outright:

    qemu-system-x86_64.exe: failed to get xsave state: No error

It dies *after* ISOLINUX has drawn, so it reads like a broken image rather than
a hypervisor limit. `run-qemu.ps1` defaults to `Skylake-Client` for that reason;
`-Cpu max,-hypervisor` restores the old behaviour on a host where it works. If
it fails anyway, `wsl --shutdown` frees the hypervisor — a running WSL2 backend
can wedge WHPX on its own.

Boot with `-Headless` for anything you intend to check. The SDL window feeds the
host's pointer and keyboard straight into the guest, so a VM that has one can
come up on the wrong tab, with an app already open, or having chosen something
out of a menu — the guest cursor sitting anywhere other than dead centre is the
tell.

Keyboard input over the monitor works (`sendkey tab`, `sendkey m`, `sendkey
ret`). `mouse_move` does **not** reach the guest with `usb-tablet` attached,
though `mouse_button` clicks wherever the pointer already sits — so a monitor
"click" lands in a place you did not choose. Drive the UI with the keyboard.

If a VM looks stuck, screendump it before theorising: `(Get-Process
qemu-system-x86_64).CPU` frozen near five seconds almost always means the guest
panicked, and the panic text is on screen.

`run-qemu.ps1 -Detach -Fresh` boots without holding the terminal and
`vm-console.ps1` drives the result; `.claude/skills/boot-os/SKILL.md` is the
short version of everything below.

The QEMU monitor also serves exactly one client and leaves the socket in
CloseWait afterwards, so a second connection is refused for the life of the VM.
Do all monitor work — every screendump, every sendkey — over one held
connection.

## The launcher goes deaf after a VT switch

Switching to a text console with Ctrl+Alt+F2 and back with `chvt 1` leaves the
launcher visible but receiving no keys at all. `hyprctl activewindow` still
reports `omni-launcher` as focused and `acceptsInput: 1`, and even Hyprland's
own Super binding does not fire — which points at the input devices not being
reattached to the seat on the way back rather than at anything in the shell.

It matters for diagnosis, because reading a log on tty2 costs the session its
keyboard until the VM is rebooted. Grab what you need in one visit.

## Known gaps in the profile

Honest list of what has not been verified, because it cannot be without a
build:

- **Proton is not detected.** It shows as "not installed" on a booted image
  even though `steam` is present, and that is correct: Proton is not a binary
  on `PATH`. Steam fetches it into `compatibilitytools.d` and runs it through
  its own runtime, so both detecting and launching it have to go via Steam.
- **AUR package names are unverified.** The ISO build never sees them — pacman
  has no AUR support — so `pcsx2-git`, `shadps4-bin`, `ryubing` and the rest
  are only proven when `omni-first-boot` runs on a live system.
- **The installer has only run in a VM.** *Install OmniOS* (Phase 13.2) is on
  the boot menu, the live desktop and Game Mode's menu, and has installed and
  booted on QEMU's BIOS and UEFI firmware. Registering OmniOS with a real UEFI
  machine's boot menu has not run anywhere yet: every test install started from
  a BIOS-booted stick, so the disk was found through the fallback path instead.
- **No GPU in a VM.** Mesa falls back to `llvmpipe`, which is enough for the
  compositor and the launcher but not for any game. Phase 13.4 still means
  real hardware.
- **Branding is partial.** The Plymouth splash, Plasma's loading screen and the
  BIOS boot menu are in OmniOS colours; systemd-boot's UEFI menu has no colours
  to set. Launcher sound effects are not done, and the Hyprland config still uses
  the .conf format that Hyprland 0.57 removes.
