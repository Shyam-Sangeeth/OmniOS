# Phase 3.2 (OmniOS.md §5.3.2). Boot straight into a session on tty1 and
# nowhere else, so ssh and the other ttys stay a plain shell.
#
# There are two sessions, Desktop Mode (Plasma) and Game Mode (Hyprland and the
# tile launcher), and this file is what switches between them: there is no
# display manager. The loop at the bottom starts whichever one is chosen and,
# when it ends, starts whichever one is chosen next. omni-session-select makes
# the choice.
#
# The compositor is never exec'd blindly. A console OS that fails to start its
# shell must say why: an `exec Hyprland` that dies leaves a black screen with a
# cursor and no way to diagnose it, which is exactly what the first QEMU boot
# produced. Output is logged and a failure drops to a shell with the reason on
# screen.

if [ -n "$WAYLAND_DISPLAY" ] || [ "$XDG_VTNR" != "1" ]; then
    return 2>/dev/null || true
fi

# omnios.nolauncher on the kernel command line drops to a console instead,
# which is the only way to debug a launcher that crashes on start.
if grep -qw omnios.nolauncher /proc/cmdline 2>/dev/null; then
    echo "omnios.nolauncher set — not starting the compositor."
    return 2>/dev/null || true
fi

# Hyprland, like every wlroots compositor, exits rather than run with
# superuser privileges. Reaching here as root means the autologin user is
# misconfigured; say that plainly instead of failing inside the compositor.
if [ "$(id -u)" = "0" ]; then
    echo "OmniOS: refusing to start the compositor as root."
    echo "The live user is 'omni' — check the getty autologin drop-in."
    return 2>/dev/null || true
fi

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
[ -d "$XDG_RUNTIME_DIR" ] || { mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"; }

# Virtual GPUs need coaxing. Read the DRM driver rather than guessing from
# device names, so this keys off what is actually bound.
# Match anywhere in the resolved driver path, not on its basename. card0's
# "device" is the PCI parent, so virtio-vga resolves to .../drivers/virtio-pci
# rather than virtio_gpu — an exact-name match silently never fires, which is
# exactly what happened on the first attempt at this.
omni_drm_driver=""
if [ -e /sys/class/drm/card0/device/driver ]; then
    omni_drm_driver=$(readlink -f /sys/class/drm/card0/device/driver)
fi

case "$omni_drm_driver" in
    *virtio* | *bochs* | *vmwgfx* | *qxl* | *vboxvideo* | *cirrus*)
        # AQ_NO_MODIFIERS is the important one. Without it aquamarine
        # negotiates DRM format modifiers that these drivers cannot satisfy
        # under software rendering, and every mode is refused:
        #
        #   ERR: Monitor Virtual-1: REJECTED available mode 1280x800@74.99Hz!
        #   ERR: Monitor Virtual-1: REJECTED preferred mode!!!
        #
        # The compositor then runs perfectly with nothing ever committed to
        # the screen — no error, no exit, no clue.
        export AQ_NO_MODIFIERS=1
        export WLR_NO_HARDWARE_CURSORS=1
        ;;
esac

# No render node at all means no GL device to bind: bochs/stdvga exposes
# card0 for modesetting but no renderD128, and aquamarine then reports
# "Can't create renderer, no matching devices found". Software GL does not
# rescue that — the device has to exist — so this only helps drivers that do
# expose one.
#
# WLR_RENDERER=pixman is deliberately NOT set. Hyprland has no pixman
# renderer; setting it yields a compositor that runs and never draws.
if ! [ -e /dev/dri/renderD128 ]; then
    export LIBGL_ALWAYS_SOFTWARE=1
fi

# /tmp, not /var/log: the live user is unprivileged, and a log the session
# cannot write is a log that does not exist when it is needed most.
readonly OMNI_LOG=/tmp/omnios-session.log

# Mirror the session to the serial port a little after the compositor should
# have come up. A compositor that runs without output produces no error and no
# exit, so there is nothing for the failure handler below to catch — the only
# way to see it is to read the log while it is still running. Serial reaches
# the host with no keyboard involved, which matters because driving the guest
# keyboard through QEMU's monitor proved unreliable past a couple of commands.
#
# Harmless where there is no serial port: the test guards it.
if [ -e /dev/ttyS0 ]; then
    if [ -w /dev/ttyS0 ]; then
        omni_serial() { cat >/dev/ttyS0; }
    else
        omni_serial() { sudo -n tee /dev/ttyS0 >/dev/null; }
    fi
    # Stream whatever the launcher starts to the serial port as it happens.
    # The one-shot dump below fires thirty seconds in, long before anyone has
    # opened an app, so it can never catch a program that dies on launch —
    # which is exactly the failure worth seeing.
    : >/tmp/omnios-app.log 2>/dev/null || true
    ( tail -n +1 -F /tmp/omnios-app.log 2>/dev/null | sed -u 's/^/[app] /' ) | omni_serial &

    (
        sleep 30
        echo "===== OMNIOS SESSION LOG ====="
        cat "$OMNI_LOG" 2>/dev/null
        echo "===== PROCESSES ON TTY1 ====="
        ps -t tty1 -o pid,stat,cmd --no-headers 2>/dev/null
        echo "===== LAST APP OUTPUT ====="
        tail -n 25 /tmp/omnios-app.log 2>/dev/null || echo "  (nothing launched yet)"
        echo "===== DISPLAY HOLDERS ====="
        echo "plymouthd: $(pgrep -a plymouthd 2>/dev/null || echo none)"
        echo "card0 held by:"
        sudo -n fuser -v /dev/dri/card0 2>&1 | head -8 || echo "  (fuser unavailable)"
        echo "logind session:"
        loginctl session-status 2>/dev/null | head -8 || echo "  (none)"
        echo "===== GRAPHICS ENVIRONMENT ====="
        echo "drm driver path: ${omni_drm_driver:-none}"
        echo "AQ_NO_MODIFIERS=${AQ_NO_MODIFIERS:-unset}"
        echo "LIBGL_ALWAYS_SOFTWARE=${LIBGL_ALWAYS_SOFTWARE:-unset}"
        echo "WLR_NO_HARDWARE_CURSORS=${WLR_NO_HARDWARE_CURSORS:-unset}"
        echo "===== DRM DEVICES ====="
        ls -l /dev/dri 2>/dev/null
        echo "===== END ====="
    ) 2>&1 | omni_serial &
fi

# Hand the display over before starting the compositor. plymouth holds DRM
# master for as long as it runs, and a wlroots compositor cannot take the
# device from it — Hyprland simply blocks. Holding the splash until the
# launcher had drawn therefore deadlocked one step later than the last fix:
# boot reached autologin and stopped there.
#
# --retain-splash leaves the last frame painted on the screen while plymouth
# exits, so the display stays covered across the handoff and the compositor
# draws over a splash rather than over black.
if command -v plymouth >/dev/null 2>&1; then
    # NOT --retain-splash. Retaining the splash means plymouth keeps the
    # display rather than handing it back, so the compositor never becomes DRM
    # master and every framebuffer allocation is refused:
    #
    #   KMS: DRM_IOCTL_MODE_CREATE_DUMB failed: Permission denied
    #   GBM: Failed to allocate a GBM buffer: bo null
    #   Monitor Virtual-1: REJECTED available mode 1280x800@74.99Hz!
    #
    # The rejected modes were only the symptom. A brief black frame during the
    # handoff is the price of the compositor actually getting the device.
    plymouth quit >/dev/null 2>&1 || true

    # 'plymouth quit' returns as soon as the request is sent, not when the
    # daemon has exited and dropped DRM master. Starting the compositor into
    # that gap produced the worst possible outcome: Hyprland came up healthy
    # with no output device at all — process running, Xwayland running, screen
    # still showing the console underneath. A compositor that fails is
    # debuggable; one that runs invisibly is not.
    #
    # Wait for it to actually go, but never longer than five seconds: a stuck
    # plymouth must not stop the console from booting.
    # Wait on the process, not on 'plymouth --ping'. The ping talks to a socket
    # and can report the daemon gone while it is still alive holding DRM
    # master, which leaves the compositor unable to allocate any framebuffer:
    # DRM_IOCTL_MODE_CREATE_DUMB fails with Permission denied and every mode is
    # then rejected.
    waited=0
    while pgrep -x plymouthd >/dev/null 2>&1 && [ "$waited" -lt 100 ]; do
        sleep 0.1
        waited=$((waited + 1))
    done

    # Last resort. A splash daemon that will not leave must not be allowed to
    # keep the console from starting.
    if pgrep -x plymouthd >/dev/null 2>&1; then
        sudo -n pkill -x plymouthd >/dev/null 2>&1 || true
        sleep 0.5
    fi
fi

# Which session a boot starts in. omnios.mode= on the kernel command line wins —
# that is how the "Game Mode" and "Install OmniOS" boot entries work — and
# otherwise it is the desktop.
#
# "install" is not a session of its own: it is the desktop with the installer
# opened on it (see the loop). Only the ISO's menu offers it; on an installed
# system it would have nothing to install from.
omni_boot_mode() {
    # The sign-in screen's Desktop / Game Mode choice, when there was one.
    case "${OMNIOS_START_MODE:-}" in
        game | desktop) echo "$OMNIOS_START_MODE"; return ;;
    esac
    local arg
    for arg in $(cat /proc/cmdline 2>/dev/null); do
        case "$arg" in
            omnios.mode=game) echo game; return ;;
            omnios.mode=desktop) echo desktop; return ;;
            omnios.mode=install) echo install; return ;;
        esac
    done
    echo desktop
}

# The choice, as omni-session-select records it. It lives in the runtime
# directory, so it lasts for this login and no longer: a boot starts where the
# boot entry says, not wherever someone last was.
readonly OMNI_MODE_FILE="$XDG_RUNTIME_DIR/omnios-mode"
[ -s "$OMNI_MODE_FILE" ] || omni_boot_mode >"$OMNI_MODE_FILE"

# Plasma's own launcher for a session started from a TTY. It reuses the
# systemd user bus when there is one and starts a private bus only when there
# is not; plain dbus-run-session is the fallback for a Plasma that predates it.
omni_start_desktop() {
    if [ -x /usr/lib/plasma-dbus-run-session-if-needed ]; then
        /usr/lib/plasma-dbus-run-session-if-needed /usr/bin/startplasma-wayland
    else
        dbus-run-session startplasma-wayland
    fi
}

# Point Steam's library at ~/Games/steam before anything can open Steam — from
# either session, so it happens here rather than in one of them. Never fatal:
# a machine that cannot arrange its folders must still start.
if command -v omni-steam-library >/dev/null 2>&1; then
    omni-steam-library >>/tmp/omnios-shell.log 2>&1 || true
fi

# Plasma opens whatever is in ~/.config/autostart when it starts, which is how
# the installer comes up by itself. It is taken away again as soon as that
# first desktop session ends, so switching to Game Mode and back, or logging
# out, does not open it a second time.
readonly OMNI_INSTALL_AUTOSTART="$HOME/.config/autostart/omnios-install-now.desktop"
omni_install_armed=0

omni_quick_exits=0
while true; do
    omni_mode="$(head -n 1 "$OMNI_MODE_FILE" 2>/dev/null)"

    case "$omni_mode" in
        install)
            if [ -d /run/archiso ]; then
                mkdir -p "$(dirname "$OMNI_INSTALL_AUTOSTART")"
                cat > "$OMNI_INSTALL_AUTOSTART" <<'AUTOSTART'
[Desktop Entry]
Type=Application
Name=Install OmniOS
Exec=omni-launcher-qml --install
Icon=omnios
OnlyShowIn=KDE;
AUTOSTART
                omni_install_armed=1
            fi
            echo desktop >"$OMNI_MODE_FILE"
            continue
            ;;
        console)
            # Asked for by name, so no error screen. The choice goes back to
            # the boot default first: otherwise 'exit' would log in, read
            # "console" again and land right back here, and the only way out
            # would be a reboot.
            omni_boot_mode >"$OMNI_MODE_FILE"
            clear
            echo "OmniOS: left the graphical session."
            echo "Type 'exit' to start $(cat "$OMNI_MODE_FILE") mode again."
            break
            ;;
        desktop)
            if ! command -v startplasma-wayland >/dev/null 2>&1; then
                # An image built without Plasma must still boot to something.
                echo "Desktop Mode is not installed on this image; starting Game Mode." >"$OMNI_LOG"
                echo game >"$OMNI_MODE_FILE"
                continue
            fi
            ;;
        game) ;;
        *)
            omni_boot_mode >"$OMNI_MODE_FILE"
            continue
            ;;
    esac

    omni_started=$SECONDS

    # Hyprland is invoked directly, on purpose.
    #
    # start-hyprland is the upstream-recommended entry point and silences the
    # "started without start-hyprland" warning, but it hands off to a session
    # manager and returns immediately. From a tty autologin that means this
    # loop sees the session end the instant it began, which is exactly what
    # happened before the loop existed: the login shell exited, getty respawned,
    # and the boot never reached the launcher. Verified by isolation: the
    # failure was identical under both stdvga and virtio-vga, so the display
    # device was not involved.
    #
    # The warning is cosmetic. A console that never starts is not.
    if [ "$omni_mode" = game ]; then
        Hyprland >"$OMNI_LOG" 2>&1
    else
        omni_start_desktop >"$OMNI_LOG" 2>&1
    fi
    status=$?
    omni_lasted=$((SECONDS - omni_started))

    if [ "$omni_install_armed" = 1 ]; then
        rm -f "$OMNI_INSTALL_AUTOSTART"
        omni_install_armed=0
    fi

    if [ $status -ne 0 ]; then
        clear
        echo "OmniOS: the $omni_mode session exited with status $status."
        echo
        echo "last 20 lines of $OMNI_LOG:"
        echo "---------------------------------------------------------------"
        tail -n 20 "$OMNI_LOG" 2>/dev/null || echo "(no log was written)"
        echo "---------------------------------------------------------------"
        echo

        # A desktop that will not start should not cost you the machine: Game
        # Mode is a different compositor and very likely still works. The
        # delay is so the reason above can be read, and Ctrl-C keeps you at
        # this shell instead.
        if [ "$omni_mode" = desktop ]; then
            echo "Starting Game Mode in 10 seconds. Press Ctrl-C to stay at a shell."
            sleep 10
            echo game >"$OMNI_MODE_FILE"
            continue
        fi

        echo "You are at a shell. 'omnictl list' works without the compositor."
        break
    fi

    # Signed in at the sign-in screen, and signed out rather than switched:
    # the mode is the one this session started in, so nobody asked for the
    # other. Leaving the login shell ends the session, and greetd shows the
    # sign-in screen again — where a machine that asks for a password has to
    # go back to, not straight into a fresh session for whoever is sitting
    # there next.
    if [ "${OMNIOS_GREETER:-}" = 1 ] && [ "$(head -n 1 "$OMNI_MODE_FILE" 2>/dev/null)" = "$omni_mode" ]; then
        exit 0
    fi

    # A clean exit is someone switching mode or logging out, and the loop
    # carries on. Three in a row that each lasted seconds are a session that
    # cannot start and exits politely about it — restarting that for ever would
    # be a black screen that flickers, with the reason never shown.
    if [ $omni_lasted -lt 10 ]; then
        omni_quick_exits=$((omni_quick_exits + 1))
    else
        omni_quick_exits=0
    fi
    if [ $omni_quick_exits -ge 3 ]; then
        clear
        echo "OmniOS: the $omni_mode session keeps ending as soon as it starts."
        echo
        echo "last 20 lines of $OMNI_LOG:"
        echo "---------------------------------------------------------------"
        tail -n 20 "$OMNI_LOG" 2>/dev/null || echo "(no log was written)"
        echo "---------------------------------------------------------------"
        echo
        echo "You are at a shell. Type 'exit' to try again."
        break
    fi
done
