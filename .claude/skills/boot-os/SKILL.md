---
name: boot-os
description: Boot the OmniOS ISO in QEMU on Windows and drive it — press keys, take screenshots, read logs from a TTY. Use when asked to boot, run, launch, show, or test the OS, or to check something on the running console.
---

# Booting OmniOS

The ISO boots straight into a fullscreen shell with no ssh and no desktop
behind it, and a foreground QEMU holds the terminal. So everything after the
boot goes through QEMU's monitor socket, and the two scripts below are the whole
interface.

## 1. Make sure there is an ISO

```bash
ls -la out/*.iso
```

Nothing there, or older than your changes? Build first — see
`docs/BUILDING.md`. The build compiles the launcher **before** mkarchiso, so an
ISO only contains the source as of when the build started.

If you changed the launcher, compile it in a container first. `cmake --build
build` on Windows does **not** build `src/launcher` — the root CMakeLists skips
it without Qt6 — so a green local build and passing tests say nothing about
whether the shell still compiles. `docs/BUILDING.md` has the one-liner.

## 2. Boot it

```powershell
./scripts/run-qemu.ps1 -Detach -Fresh -SerialLog "$env:TEMP\omnios-serial.log" -MonitorPort 4444
```

- `-Detach` returns instead of blocking, and prints the pid.
- `-Headless` gives no window. **Use it whenever you are verifying rather than
  watching** — see below.
- `-SshPort` forwards a host port to the guest's ssh (default 12222, log in as
  `omni` / `omnios`). Note 2222 does not work: Windows reserves port ranges for
  Hyper-V and QEMU then refuses the whole netdev with "Could not set up host
  forwarding rule", with nothing listening to explain why.
- `-Fresh` kills any running QEMU and clears the hypervisor. **This stops Docker
  Desktop**, so start it again before the next build and wait for `docker info`
  to answer.

Then wait for the session rather than guessing at a delay. The login script
prints a diagnostic block ending in `END =====` once the compositor is up:

```bash
until grep -qi "END =====" "/c/Users/<you>/AppData/Local/Temp/omnios-serial.log"; do sleep 5; done
```

Give it another ~10 s after that for the launcher to draw.

## 3. Drive it

**The ISO waits ten seconds at a boot menu** (Try / Install / Game Mode /
troubleshooting) before starting "Try OmniOS", on BIOS and UEFI alike. Every
boot is ten seconds slower than it looks; send `<KEY:ret>` straight after
`run-qemu.ps1 -Detach` returns to skip it, or `<KEY:down>` then `<KEY:ret>` for
Install, which opens the installer on the desktop by itself.

**A normal boot lands in Desktop Mode (Plasma), not the tile launcher.** To
reach Game Mode, open KRunner and run the Game Mode entry; to come back, pick
"Switch to desktop" from the launcher's F10 menu:

```powershell
./scripts/vm-console.ps1 -Shot game.png -Script '<KEY:alt-f2>', '<WAIT:4>', 'Game Mode', '<WAIT:4>', '<ENTER>', '<WAIT:40>'
```

A switch takes 30-40 s under software rendering. `/tmp/omnios-session.log` is
the log of whichever session started last.

One script, one monitor connection:

```powershell
# switch to the Apps tab and open the focused tile's menu
./scripts/vm-console.ps1 -Shot apps.png -Script '<KEY:tab>', '<WAIT:2>', '<KEY:m>'
```

Then read the PNG. Script entries are literal text to type, or `<ENTER>`,
`<KEY:name>` (any QEMU key name), `<WAIT:n>`.

Reading a log from inside the guest:

```powershell
./scripts/vm-console.ps1 -Shot log.png -Script `
  '<KEY:ctrl-alt-f2>', '<WAIT:3>', 'root', '<ENTER>', '<WAIT:2>', 'omnios', '<ENTER>', '<WAIT:3>', `
  'clear', '<ENTER>', 'tail -20 /tmp/omnios-pkg.log', '<ENTER>', '<WAIT:3>'
```

Useful logs: `/tmp/omnios-shell.log` (the launcher's own stderr, where a QML
error would appear), `/tmp/omnios-app.log` (whatever the launcher last
started), `/tmp/omnios-pkg.log` (package operations).

**In Desktop Mode, prefer KRunner to a TTY.** KRunner runs a shell command
typed into it, so a Konsole window shows the output without the VT switch that
breaks the keyboard (below). The `sleep` keeps the window open long enough to
screenshot:

```powershell
./scripts/vm-console.ps1 -Shot out.png -Script '<KEY:alt-f2>', '<WAIT:4>', `
  'konsole -e sh -c "tail -20 /tmp/omnios-session.log; sleep 300"', '<WAIT:2>', '<ENTER>', '<WAIT:15>'
```

## Testing the installer

`-Disk` attaches a 40 GB qcow2 at `%LOCALAPPDATA%\OmniOS\vm\omnios-test.qcow2`,
created when missing and kept between boots; `-BlankDisk` starts it empty. It
is outside the repo because the repo is in OneDrive. It is attached with
discard, so a reinstall reuses the space the last one freed: two installs in a
row measured 8.76 then 8.84 GB, where without discard the file had crept to
16.5 GB and was heading for 40. It never shrinks, though; `-BlankDisk` is how
to get the space back. After an install, `-FromDisk`
boots that disk with **no ISO attached** — the only honest test that the disk
starts on its own. Add `-Uefi` to boot it through OVMF instead of SeaBIOS; the
installer puts both boot loaders on the disk, so check both.

```powershell
./scripts/run-qemu.ps1 -Detach -Fresh -Headless -Disk     -SerialLog ... -MonitorPort 4444   # live ISO + disk
./scripts/run-qemu.ps1 -Detach -Fresh -Headless -FromDisk -SerialLog ... -MonitorPort 4444   # installed, BIOS
./scripts/run-qemu.ps1 -Detach -Fresh -Headless -FromDisk -Uefi ...                          # installed, UEFI
```

**Iterate on a script without rebuilding the ISO.** A rebuild is 10-30
minutes. Serve the working copy from the host instead — QEMU's user network
reaches the host's loopback as 10.0.2.2 — and run it inside the booted ISO:

```bash
python -m http.server 8765 --bind 127.0.0.1    # in the directory holding the script
```
then, in the guest (through KRunner, see above):
`konsole -e sh -c "curl -sf http://10.0.2.2:8765/t.sh | sh; sleep 3000"`.
That is how omni-install was proven before it went into an image. It only
works for scripts; anything compiled into the launcher needs the rebuild.

**The sign-in screen only exists on an install with auto sign-in off.** Quickest
way there: from the live session, as root,
`echo PASSWORD | omni-install --disk /dev/vda --erase /dev/vda --user NAME --password-stdin --no-autologin`
(add `--erase-os OmniOS` if the disk already holds an install), then power off
and boot `-FromDisk`. The screen is `omni-greeter` under greetd; its output is
in `journalctl -u greetd`. Signing out of the desktop there must come back to
it, and switching modes must not.

**Power off from inside the guest before `-FromDisk`**, so the disk is clean:
KRunner, `systemctl poweroff`. vm-console will then fail with "connection was
forcibly closed" — that is QEMU exiting, i.e. success.

## What will waste your time if you do not know it

**The SDL window is a live input device wired to the host, so use `-Headless`
to verify anything.** It takes the host pointer position the moment it opens,
and stray clicks and keys land in the guest as though someone had typed them.
That produced a launcher which had opened Files by itself, one sitting on the
wrong tab, and one that had picked Sleep out of the power menu — none of which
the shell can do unaided, and each of which looked like a real bug for as long
as it took to notice the guest cursor was somewhere it had never been put. The
giveaway is the cursor: with no input it sits dead centre. `screendump` works
perfectly well with no window, so nothing is lost.

**The monitor accepts one connection for the life of the VM.** It leaves the
socket in CloseWait afterwards, so a second connect is refused. Put everything
into one `vm-console.ps1` call rather than several.

**`mouse_move` does not reach the guest** with `usb-tablet` attached, though
`mouse_button` clicks wherever the pointer already happens to sit — so a
monitor "click" lands somewhere you did not choose and looks like the UI
ignoring you. Drive the UI with the keyboard.

It is not a units mistake, which is the obvious second guess: the tablet takes
absolute coordinates on a 0-32767 axis rather than pixels, and it ignores
correctly scaled ones just the same. Clicking the top-right status icons was
tried this way and changed nothing on screen. **So anything mouse-only cannot be
verified from here.** Reach it another way — every one of those icons has a
duplicate in the F10 menu that runs the same function — and say which one you
actually exercised.

**Two ways of asking "is it running?" answer "no" when it is.** From the TTY,
`pgrep -a gnome-control-center` matches nothing — pgrep warns that a pattern
over 15 characters cannot match a process name, and the warning is easy to read
past. Use `pgrep -af`. And `hyprctl` run through `su omni` prints nothing at all
without `HYPRLAND_INSTANCE_SIGNATURE`, which is not an error you will notice:

```bash
sig=$(ls /run/user/1000/hypr | head -1)
su omni -c "XDG_RUNTIME_DIR=/run/user/1000 HYPRLAND_INSTANCE_SIGNATURE=$sig hyprctl clients"
```

Both of those said an app had failed to start while it was in fact running
fullscreen on workspace 2 — which is where the launcher puts everything, so it
is never on the screenshot you take of workspace 1.

**A VT switch costs the session its keyboard.** After `ctrl-alt-f2` and back,
the launcher is visible but receives nothing; `hyprctl` still reports it focused
and even Hyprland's own Super binding does not fire. Reboot to recover, so get
everything you need from the TTY in one visit — and never switch away before
the UI checks you actually came for.

**PowerShell turns a native command's stderr into a terminating error when it is
in a pipeline.** `./scripts/build-iso-docker.ps1 | Out-File log.txt` reports a
failed build while the container carries on happily. Check `docker ps` before
believing it. The same trap makes `run-qemu.ps1 | Select-Object -First 20` look
like QEMU refusing to start.

**Hung at the ISOLINUX banner, ~7 CPU-seconds, and `screendump` writes
nothing?** That is the hypervisor, not the guest: a panicked guest still
screenshots. `wsl --shutdown` on its own does not clear it — Docker Desktop's
processes keep holding it after the WSL backend is down, which is why `-Fresh`
stops those too and then waits 20 seconds. Three boots in a row wedged before
that was understood.

**A wedged VM that *does* screenshot is a guest panic.** Look at it before
theorising — the text is on screen. `-cpu max` is the other one: it
dies with `failed to get xsave state` *after* ISOLINUX has drawn, which reads
like a broken image. `run-qemu.ps1` defaults to `Skylake-Client` for that
reason.

**A QML file named like a context property hides it.** Inside `Installer.qml`,
`Installer.refresh()` resolved to the file's own type, not to the C++ object
registered as "Installer" — the installer opened on its failure page with a
blank reason. The journal said it plainly ("is not a function", "Unable to
assign [undefined]"). Read `journalctl --user -b | grep qrc:` before theorising
about a QML screen that behaves impossibly.

**Blank screen, no launcher.** Read `/tmp/omnios-shell.log` first; a QML error
puts the shell into its terminal fallback rather than showing nothing.

**Keystrokes can arrive late, so look before you press Enter on the power
menu.** Five Downs sent with a second between them showed the highlight one row
short in the screenshot. The fifth landed afterwards, so one more Down went to
Sleep and Enter suspended the machine. Screenshot the highlighted row, then
send Enter in a separate call.

**"Kernel panic - not syncing: IO-APIC + timer doesn't work!" at 0.008 s is the
VM, not the image.** The timer check it fails is timing-sensitive under WHPX
with kernel-irqchip=off; hpet=off in run-qemu.ps1 made it rarer, not gone. On
2026-09-26 it hit three boots out of five, with or without keys pressed at the
menu, the host idle and nothing holding the hypervisor. Do not "fix" it with
no_timer_check in the ISO's boot entries: on real hardware that check also
chooses a working timer route. Boot again. A panicked screen is 720x400 text
mode, while a healthy boot is at the 1280x800 splash 25 s in, so a loop can
tell them apart from the screendump's header alone:

```powershell
foreach ($try in 1..4) {
  ./scripts/run-qemu.ps1 -Detach -Fresh -Headless -Disk -SerialLog ... -MonitorPort 4444 | Out-Null
  Start-Sleep 25
  ./scripts/vm-console.ps1 -Shot "$env:TEMP\probe.ppm" -Script '<WAIT:0>' -SettleSeconds 0 | Out-Null
  $dims = ([Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes("$env:TEMP\probe.ppm")[0..20]) -split "`n")[1]
  if ($dims -ne '720 400') { break }
}
```

**Sleep does not come back in the VM.** On 2026-09-25, `system_wakeup` after a
suspend cold-booted to ISOLINUX and wedged there, so `-Fresh` was the only way
out. Resume had worked when sleep was built; the sound card was added to
run-qemu.ps1 after that and is the first suspect, but it has not been checked.
Until it is, do not choose Sleep in a VM you still need.

## Checking the result honestly

A screenshot proves what is on screen and nothing else. When a change touches
what the tiles *do* — a menu action, a launch, an install — drive it and look,
rather than reporting the feature from the fact that it built. If something is
only verified as far as "the UI renders it", say so.
