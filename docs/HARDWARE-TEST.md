# Hardware test checklist

What to check when OmniOS goes onto a real machine. Everything below has passed
in QEMU; these are the parts a VM cannot prove.

**Before you start:** write `out/omnios-0.1.0-x86_64.iso` with Rufus in **DD
mode**, and turn Secure Boot off. If you can, test on a machine that boots in
UEFI mode, since the VM tests were mostly BIOS.

## 1. Live USB

- [ ] The boot menu appears (Try / Install / Game Mode) and starts "Try" on its own after 10 s.
- [ ] The splash screen leads into the desktop with the OmniOS mark, and there's no black gap.
- [ ] The Game Mode boot entry lands in Game Mode with an empty taskbar.
- [ ] Wired network works, and sound plays through the right output.

## 2. Controller

- [ ] An Xbox pad over USB moves around Game Mode; A opens a tile and B goes back.
- [ ] A PS5 pad over USB: hints show ✕ ○ □ △, and the **light bar turns purple**.
- [ ] Pair a pad over Bluetooth and repeat.
- [ ] The Guide/PS button brings the launcher back in front of a running app.
- [ ] Leave the pad as the only thing in use for more than 10 minutes: the screen must not dim or sleep.

## 3. Wi-Fi from Game Mode

- [ ] Join a WPA2 network by typing its password on the on-screen keyboard.
- [ ] Type a wrong password: you should see an error, and no saved network left behind.

## 4. Install (erases the target disk)

- [ ] On "Language and keyboard", pick your language and keyboard layout. If your keyboard isn't US, type a few letters in the account page's name box: they should match the keys you press.
- [ ] Install a real name and password using **only the controller**, with "Sign in automatically" **off**.
- [ ] Try the erase confirmation, where you type "erase", with the pad.
- [ ] Take the USB stick out and reboot: the disk boots on its own. Check UEFI, and BIOS/CSM if the board has it.

## 5. Installed system

- [ ] The sign-in screen appears. Sign in with the pad's keyboard; a wrong password leaves you in the box.
- [ ] Choose Game Mode at sign-in and it starts in Game Mode.
- [ ] Signing out returns to the sign-in screen; switching modes does not.
- [ ] Open Steam from the Apps tab: it opens full screen, with no taskbar.
- [ ] Install a small game in Steam and go back to the launcher: its tile appears on the Games tab by itself (within ~10 s) with Steam's cover art, and no Proton or runtime tiles appear. Opening it starts the game.
- [ ] Discover can install an app.
- [ ] Last, since it removes Steam: Uninstall Steam from its menu on the Apps tab. It asks for your password first.
- [ ] Sleep and wake from the desktop and from Game Mode: the screen comes back, and the pad and Wi-Fi still work.

## If something fails

Run these from Konsole on the desktop:

| What failed | Collect |
|---|---|
| Game Mode or the launcher | `journalctl --user -b \| grep -a omni-launcher` |
| The sign-in screen | `journalctl -u greetd -b` |
| Sleep that didn't come back | `journalctl -b -1 \| tail -100`, after rebooting |
| Installs and uninstalls | `/tmp/omnios-pkg.log` |

A photo of the screen is enough for anything visual.
