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
    (
        sleep 30
        echo "===== OMNIOS SESSION LOG ====="
        cat "$OMNI_LOG" 2>/dev/null
        echo "===== PROCESSES ON TTY1 ====="
        ps -t tty1 -o pid,stat,cmd --no-headers 2>/dev/null
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
