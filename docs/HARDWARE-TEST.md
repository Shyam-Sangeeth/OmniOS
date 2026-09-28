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
- [ ] A PS5 pad over USB: hints show ✕ ○ □ △ as round badges (L1, R1, Options as pills), and the **light bar turns purple**. An Xbox pad shows coloured A B X Y discs.
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
- [ ] Install a small game in Steam and go back to the launcher: its tile appears on the Games tab by itself (within ~10 s) with Steam's cover art, and no Proton or runtime tiles appear. Opening it starts the game, and the launcher stays out of the way until you quit the game; then the library comes back by itself.
- [ ] While it runs, press Guide, then Start: *Resume* goes back into the game, and *Quit* (it asks first) ends it and brings the library back.
- [ ] On that game's tile, press M (□/X on a pad): *Open in Steam* shows its page in Steam, and its detail screen says when it was last played.
- [ ] Leave Game Mode open for a minute with the network up: the top bar says how many updates there are (or nothing, if none). Start → *Update system* → *Update now* → your password: it shows progress and ends with "The system is up to date" (or "Restart to finish" after a kernel update).
- [ ] Open TV from the Apps tab: it opens on your country's channels. Narrow them with Category and Language in the top row (each list searchable). Play one (A): it plays inside TV, with a bar (Back, Audio, Subtitles, volume, ★) on mouse movement or any key. Up/down is volume, LB/RB change channel (left/right and the stick do nothing), B/Esc/right-click leaves. Check the sound comes through at the level shown. A dead channel is skipped when surfing, and says it is unavailable when picked directly.
- [ ] TV with only a controller: D-pad to Country, A opens it, A on its search box types with the on-screen keyboard, A ticks, B closes. Play a channel; A goes into the bar, open Audio and leave it: the list closes and the bar fades by itself, then one B goes back to the channels. Press the Guide button while TV is open, then Y in the launcher: back in TV, no channel became a favourite.
- [ ] TV's player on a real GPU: channels start within a few seconds and play smoothly at 1080p (the VM uses software OpenGL). On Sony Yay, Audio lists its languages (हिन्दी, తెలుగు, ...); pick one and the sound changes without the picture stopping. 9X Jhakaas plays inside TV with the bar, not in a separate window.
- [ ] While a game runs, notifications stay away: plug in a USB stick or change the volume from the keyboard, and nothing pops up over the game. After quitting, a notification shows as usual. If one stays in the corner of the library, the system menu (Options/Start) offers *Clear notifications*, and it goes.
- [ ] A PS1 game with no BIOS of your own in `~/Games/bios`: it plays on the free OpenBIOS (`openbios.bin` appears there). Put your own PS1 BIOS in `~/Games/bios` and the next start uses it.
- [ ] An N64 game (`.z64`) in `~/Games/retro`: it plays past its first screen. In the VM the N64 core crashed at that point, under software rendering.
- [ ] A retro game you own (e.g. a `.nes` or `.gba` file) in `~/Games/retro` or `~/Games/gba`: press Y to rescan, it gets a tile, Play starts it full screen in RetroArch, Start + Select opens RetroArch's menu, and Guide then *Quit* returns to the library. The same for a GameCube/Wii game in `~/Games/gamecube` or `~/Games/wii` with Dolphin.
- [ ] A PS1 or PS2 game you own in `~/Games/ps1` / `~/Games/ps2`, with your console's BIOS in `~/Games/bios`: the page offers *Install DuckStation* / *Install PCSX2*; after it installs, Play starts the game full screen with no setup wizard, the pad works in the game, Select + Start opens the emulator's pause menu, and Guide then *Quit* returns to the library. Without the BIOS the page says so instead.
- [ ] A PS3 game folder you own in `~/Games/ps3`, with `PS3UPDAT.PUP` from Sony in `~/Games/bios`: *Install RPCS3*, then *Install PS3 system software* (answer Yes with a keyboard), RPCS3 closes by itself and the button becomes Play; the game starts full screen. Guide then *Quit* returns to the library.
- [ ] A Switch game with your `prod.keys` in `~/Games/bios` (and Switch firmware installed in Ryubing), a PS4 game folder, and a 3DS game: each offers its emulator's install, then plays full screen, and Guide then *Quit* returns to the library.
- [ ] In the PS3, Switch (from its second start) and 3DS games, the pad you pressed Play with controls the game: ✕/A bottom, D-pad and both sticks, shoulders and triggers. Try once with a DualSense over USB and once over Bluetooth, since SDL may see it differently from the VM's virtual pad.
- [ ] TV's guide: after a few seconds, popular channels (in India: Sony SAB, Colors, Aaj Tak) show the programme on now under their names, with a bar for how far in. Play one and press any key: the bottom shows Now (times in your clock's format), the description and Next, and they match what the channel is showing. A small local channel shows nothing, which is expected.
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
| Installs, uninstalls and updates | `/tmp/omnios-pkg.log` |

A photo of the screen is enough for anything visual.
