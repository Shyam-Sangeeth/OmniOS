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

# A VM has no real GPU. wlroots defaults to a GL renderer that needs one, so
# without a hardware device Hyprland exits before drawing anything. pixman is
# the software renderer, and hardware cursors have to go with it.
if ! [ -e /dev/dri/renderD128 ]; then
    export WLR_RENDERER=pixman
    export WLR_NO_HARDWARE_CURSORS=1
    export LIBGL_ALWAYS_SOFTWARE=1
fi

# /tmp, not /var/log: the live user is unprivileged, and a log the session
# cannot write is a log that does not exist when it is needed most.
readonly OMNI_LOG=/tmp/omnios-session.log

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
fi

# start-hyprland is the supported entry point and ships in the hyprland
# package. Launching the bare Hyprland binary makes it warn, on screen and on
# every boot, that this is a debugging-only path — it does not set up the
# session (dbus, systemd user units, environment) the way the wrapper does.
if command -v start-hyprland >/dev/null 2>&1; then
    start-hyprland >"$OMNI_LOG" 2>&1
else
    Hyprland >"$OMNI_LOG" 2>&1
fi
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
