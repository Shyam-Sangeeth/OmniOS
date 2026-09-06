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

**Blank screen, no launcher.** Read `/tmp/omnios-shell.log` first; a QML error
puts the shell into its terminal fallback rather than showing nothing.

## Checking the result honestly

A screenshot proves what is on screen and nothing else. When a change touches
what the tiles *do* — a menu action, a launch, an install — drive it and look,
rather than reporting the feature from the fact that it built. If something is
only verified as far as "the UI renders it", say so.
