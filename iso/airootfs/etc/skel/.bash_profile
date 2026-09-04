# Phase 3.2 (OmniOS.md §5.3.2). Boot straight into the compositor on tty1 and
# nowhere else, so ssh and the other ttys stay a plain shell.
if [ -z "$WAYLAND_DISPLAY" ] && [ "$XDG_VTNR" = "1" ]; then
    # omnios.nolauncher on the kernel command line drops to a console instead,
    # which is the only way to debug a launcher that crashes on start.
    if ! grep -qw omnios.nolauncher /proc/cmdline; then
        exec Hyprland
    fi
fi
