# Phase 3.2 (OmniOS.md §5.3.2). Boot straight into the compositor on tty1 and
# nowhere else, so ssh and the other ttys stay a plain shell.
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

# No render node means no GPU: some virtual display devices (bochs/stdvga)
# expose /dev/dri/card0 for modesetting but no /dev/dri/renderD128. Fall back
# to Mesa's software GL so the compositor still has an OpenGL implementation.
#
# WLR_RENDERER=pixman is deliberately NOT set here. Hyprland does not support
# the pixman renderer — it needs GLES — and setting it produces a compositor
# that starts, stays running and never puts anything on screen. That cost four
# rebuilds to find, because nothing fails and nothing is logged.
if ! [ -e /dev/dri/renderD128 ]; then
    export LIBGL_ALWAYS_SOFTWARE=1
    export WLR_NO_HARDWARE_CURSORS=1
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
if [ -w /dev/ttyS0 ]; then
    (
        sleep 30
        echo "===== OMNIOS SESSION LOG ====="
        cat "$OMNI_LOG" 2>/dev/null
        echo "===== PROCESSES ON TTY1 ====="
        ps -t tty1 -o pid,stat,cmd --no-headers 2>/dev/null
        echo "===== DRM DEVICES ====="
        ls -l /dev/dri 2>/dev/null
        echo "===== END ====="
    ) >/dev/ttyS0 2>&1 &
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
    plymouth quit --retain-splash >/dev/null 2>&1 || true

    # 'plymouth quit' returns as soon as the request is sent, not when the
    # daemon has exited and dropped DRM master. Starting the compositor into
    # that gap produced the worst possible outcome: Hyprland came up healthy
    # with no output device at all — process running, Xwayland running, screen
    # still showing the console underneath. A compositor that fails is
    # debuggable; one that runs invisibly is not.
    #
    # Wait for it to actually go, but never longer than five seconds: a stuck
    # plymouth must not stop the console from booting.
    waited=0
    while plymouth --ping >/dev/null 2>&1 && [ "$waited" -lt 50 ]; do
        sleep 0.1
        waited=$((waited + 1))
    done
fi

# Hyprland is invoked directly, on purpose.
#
# start-hyprland is the upstream-recommended entry point and silences the
# "started without start-hyprland" warning, but it hands off to a session
# manager and returns immediately. From a tty autologin that means this script
# finishes, the login shell exits, getty respawns, and the boot never reaches
# the launcher — which is exactly what it did. Verified by isolation: the
# failure was identical under both stdvga and virtio-vga, so the display device
# was not involved.
#
# The warning is cosmetic. A console that never starts is not.
Hyprland >"$OMNI_LOG" 2>&1
status=$?

if [ $status -ne 0 ]; then
    clear
    echo "OmniOS: the compositor exited with status $status."
    echo
    echo "last 20 lines of $OMNI_LOG:"
    echo "---------------------------------------------------------------"
    tail -n 20 "$OMNI_LOG" 2>/dev/null || echo "(no log was written)"
    echo "---------------------------------------------------------------"
    echo
    echo "You are at a shell. 'omnictl list' works without the compositor."
fi
