/* A virtual mouse, made through uinput, for the guest's pointer, which QEMU's
 * monitor cannot move with usb-tablet attached (the boot-os skill has why).
 * The way to test anything mouse-only in the VM: the panel's corner mark,
 * a tooltip, a click on a tile's dots.
 *
 *   vm-mouse DX DY                       move by (DX, DY), quickly
 *   vm-mouse corner X Y [click|hover]    into the bottom-left corner, then X
 *                                        right and Y up one pixel at a time
 *                                        (no acceleration); click there, or
 *                                        stay six seconds for a tooltip
 *
 * Built and run in the guest:
 *   gcc -O2 -o /tmp/vm-mouse vm-mouse.c && sudo /tmp/vm-mouse corner 26 24 click
 * (26 24 is the OmniOS mark in the panel's corner at 1280x800.)
 */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdlib.h>
#include <string.h>
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

static void move(int dx, int dy) {
    emit(EV_REL, REL_X, dx);
    emit(EV_REL, REL_Y, dy);
    emit(EV_SYN, SYN_REPORT, 0);
}

int main(int argc, char** argv) {
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return 1;
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(fd, UI_SET_EVBIT, EV_REL);
    ioctl(fd, UI_SET_RELBIT, REL_X);
    ioctl(fd, UI_SET_RELBIT, REL_Y);
    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;
    strcpy(setup.name, "OmniOS test mouse");
    ioctl(fd, UI_DEV_SETUP, &setup);
    ioctl(fd, UI_DEV_CREATE);
    sleep(1);  // for the compositor to pick the device up

    if (argc > 3 && !strcmp(argv[1], "corner")) {
        for (int i = 0; i < 30; ++i) { move(-200, 200); usleep(10000); }
        usleep(200000);
        const int x = atoi(argv[2]), y = atoi(argv[3]);
        for (int i = 0; i < x || i < y; ++i) {
            move(i < x ? 1 : 0, i < y ? -1 : 0);
            usleep(15000);
        }
        usleep(300000);
        // "hover": stay connected a while, so a tooltip has time to show
        // (the device going away reads as the pointer leaving). Plasma showed
        // no tooltips over Game Mode's launcher for this mouse at all, its
        // own included; on the desktop they show.
        if (argc > 4 && !strcmp(argv[4], "hover")) sleep(6);
        if (argc > 4 && !strcmp(argv[4], "click")) {
            emit(EV_KEY, BTN_LEFT, 1);
            emit(EV_SYN, SYN_REPORT, 0);
            usleep(80000);
            emit(EV_KEY, BTN_LEFT, 0);
            emit(EV_SYN, SYN_REPORT, 0);
        }
    } else {
        const int dx = argc > 1 ? atoi(argv[1]) : 2000;
        const int dy = argc > 2 ? atoi(argv[2]) : -2000;
        for (int i = 0; i < 20; ++i) { move(dx / 20, dy / 20); usleep(20000); }
    }
    sleep(1);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
