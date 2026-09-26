/* A virtual Xbox 360 pad, made through uinput, for testing the launcher's
 * controller handling in a VM — which has no controller, and QEMU cannot
 * emulate one. SDL sees it exactly as it sees a real pad on USB, so this tests
 * the whole path: SDL, the button mapping, the on-screen keyboard.
 *
 * Inside the guest (gcc is on the image):
 *
 *   curl -sf http://10.0.2.2:8765/vm-gamepad.c -o /tmp/vpad.c
 *   gcc -O2 -o /tmp/vpad /tmp/vpad.c
 *   sudo setsid -f /tmp/vpad /tmp/pad          # an Xbox 360 pad
 *   sudo setsid -f /tmp/vpad /tmp/pad ps5      # a DualSense instead
 *   echo a > /tmp/pad            # tap A (✕ on the DualSense); one per line
 *
 * Buttons, named by Xbox position: a b x y l1 r1 start back guide up down left
 * right; "quit" removes the device. See .claude/skills/boot-os/SKILL.md.
 *
 * The DualSense is made to look like what the kernel's hid-playstation driver
 * reports for a real one over USB: Sony's ids and name, sticks on 0-255, and
 * the square and triangle buttons as BTN_WEST and BTN_NORTH (xpad, below, has
 * them the other way round). There is no hidraw node behind it, so SDL drives
 * it through evdev — the light bar cannot be tested this way.
 */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fd;

static void emit(int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    write(fd, &ev, sizeof ev);
}

static void sync_now(void) { emit(EV_SYN, SYN_REPORT, 0); }

static void tap_key(int code) {
    emit(EV_KEY, code, 1); sync_now(); usleep(60000);
    emit(EV_KEY, code, 0); sync_now(); usleep(60000);
}

static void tap_hat(int axis, int value) {
    emit(EV_ABS, axis, value); sync_now(); usleep(60000);
    emit(EV_ABS, axis, 0); sync_now(); usleep(60000);
}

int main(int argc, char **argv) {
    const char *fifo = argc > 1 ? argv[1] : "/tmp/pad";
    const int ps5 = argc > 2 && !strcmp(argv[2], "ps5");
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("uinput"); return 1; }

    int keys[] = {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR,
                  BTN_TL2, BTN_TR2, BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; ++i) ioctl(fd, UI_SET_KEYBIT, keys[i]);

    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    int axes[] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ, ABS_HAT0X, ABS_HAT0Y};
    for (unsigned i = 0; i < sizeof axes / sizeof axes[0]; ++i) {
        struct uinput_abs_setup abs;
        memset(&abs, 0, sizeof abs);
        abs.code = axes[i];
        if (axes[i] == ABS_HAT0X || axes[i] == ABS_HAT0Y) { abs.absinfo.minimum = -1; abs.absinfo.maximum = 1; }
        else if (axes[i] == ABS_Z || axes[i] == ABS_RZ) { abs.absinfo.minimum = 0; abs.absinfo.maximum = 255; }
        else if (ps5) { abs.absinfo.minimum = 0; abs.absinfo.maximum = 255; abs.absinfo.value = 128; }
        else { abs.absinfo.minimum = -32768; abs.absinfo.maximum = 32767; abs.absinfo.fuzz = 16; abs.absinfo.flat = 128; }
        ioctl(fd, UI_SET_ABSBIT, axes[i]);
        ioctl(fd, UI_ABS_SETUP, &abs);
    }

    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_USB;
    if (ps5) {
        setup.id.vendor = 0x054c;
        setup.id.product = 0x0ce6;
        setup.id.version = 0x8111;
        strcpy(setup.name, "Sony Interactive Entertainment DualSense Wireless Controller");
    } else {
        setup.id.vendor = 0x045e;
        setup.id.product = 0x028e;
        setup.id.version = 0x0114;
        strcpy(setup.name, "Microsoft X-Box 360 pad");
    }
    ioctl(fd, UI_DEV_SETUP, &setup);
    ioctl(fd, UI_DEV_CREATE);
    fprintf(stderr, "vpad: created\n");

    unlink(fifo);
    mkfifo(fifo, 0666);
    chmod(fifo, 0666);
    char line[64];
    for (;;) {
        FILE *in = fopen(fifo, "r");
        if (!in) return 1;
        while (fgets(line, sizeof line, in)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strcmp(line, "a")) tap_key(BTN_SOUTH);
            else if (!strcmp(line, "b")) tap_key(BTN_EAST);
            /* xpad sends X as BTN_X and Y as BTN_Y, which input.h defines as
             * BTN_NORTH and BTN_WEST respectively; hid-playstation sends
             * square and triangle as BTN_WEST and BTN_NORTH. */
            else if (!strcmp(line, "x")) tap_key(ps5 ? BTN_WEST : BTN_X);
            else if (!strcmp(line, "y")) tap_key(ps5 ? BTN_NORTH : BTN_Y);
            else if (!strcmp(line, "l1")) tap_key(BTN_TL);
            else if (!strcmp(line, "r1")) tap_key(BTN_TR);
            else if (!strcmp(line, "start")) tap_key(BTN_START);
            else if (!strcmp(line, "back")) tap_key(BTN_SELECT);
            else if (!strcmp(line, "guide")) tap_key(BTN_MODE);
            else if (!strcmp(line, "up")) tap_hat(ABS_HAT0Y, -1);
            else if (!strcmp(line, "down")) tap_hat(ABS_HAT0Y, 1);
            else if (!strcmp(line, "left")) tap_hat(ABS_HAT0X, -1);
            else if (!strcmp(line, "right")) tap_hat(ABS_HAT0X, 1);
            else if (!strcmp(line, "quit")) { ioctl(fd, UI_DEV_DESTROY); return 0; }
            usleep(250000);
        }
        fclose(in);
    }
}
