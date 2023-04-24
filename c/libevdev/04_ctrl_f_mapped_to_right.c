// Map C-f to Right.

// The template I've used is from
// https://gitlab.freedesktop.org/libevdev/libevdev/blob/master/tools/libevdev-events.c
// has the following header:
//
/* SPDX-License-Identifier: MIT
 * Copyright © 2013 Red Hat, Inc.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include "libevdev/libevdev-uinput.h"
#include "libevdev/libevdev.h"

static void
print_abs_bits(struct libevdev *dev, int axis)
{
    const struct input_absinfo *abs;

    if (!libevdev_has_event_code(dev, EV_ABS, axis))
        return;

    abs = libevdev_get_abs_info(dev, axis);

    printf("	Value	%6d\n", abs->value);
    printf("	Min	%6d\n", abs->minimum);
    printf("	Max	%6d\n", abs->maximum);
    if (abs->fuzz)
        printf("	Fuzz	%6d\n", abs->fuzz);
    if (abs->flat)
        printf("	Flat	%6d\n", abs->flat);
    if (abs->resolution)
        printf("	Resolution	%6d\n", abs->resolution);
}

static void
print_code_bits(struct libevdev *dev, unsigned int type, unsigned int max)
{
    unsigned int i;
    for (i = 0; i <= max; i++) {
        if (!libevdev_has_event_code(dev, type, i))
            continue;

        printf("    Event code %i (%s)\n", i, libevdev_event_code_get_name(type, i));
        if (type == EV_ABS)
            print_abs_bits(dev, i);
    }
}

static void
print_bits(struct libevdev *dev)
{
    unsigned int i;
    printf("Supported events:\n");

    for (i = 0; i <= EV_MAX; i++) {
        if (libevdev_has_event_type(dev, i))
            printf("  Event type %d (%s)\n", i, libevdev_event_type_get_name(i));
        switch(i) {
        case EV_KEY:
            print_code_bits(dev, EV_KEY, KEY_MAX);
            break;
        case EV_REL:
            print_code_bits(dev, EV_REL, REL_MAX);
            break;
        case EV_ABS:
            print_code_bits(dev, EV_ABS, ABS_MAX);
            break;
        case EV_LED:
            print_code_bits(dev, EV_LED, LED_MAX);
            break;
        }
    }
}

static void
print_props(struct libevdev *dev)
{
    unsigned int i;
    printf("Properties:\n");

    for (i = 0; i <= INPUT_PROP_MAX; i++) {
        if (libevdev_has_property(dev, i))
            printf("  Property type %d (%s)\n", i,
                   libevdev_property_get_name(i));
    }
}

static int
print_event(struct input_event *ev)
{
    if (ev->type == EV_SYN)
        printf("Event: time %ld.%06ld, ++++++++++++++++++++ %s +++++++++++++++\n",
               ev->input_event_sec,
               ev->input_event_usec,
               libevdev_event_type_get_name(ev->type));
    else
        printf("Event: time %ld.%06ld, type %d (%s), code %d (%s), value %d\n",
               ev->input_event_sec,
               ev->input_event_usec,
               ev->type,
               libevdev_event_type_get_name(ev->type),
               ev->code,
               libevdev_event_code_get_name(ev->type, ev->code),
               ev->value);
    return 0;
}

static int
print_sync_event(struct input_event *ev)
{
    printf("SYNC: ");
    print_event(ev);
    return 0;
}

static void send_key_ev_and_sync(const struct libevdev_uinput *uidev, unsigned int code, int value)
{
    int err;

    err = libevdev_uinput_write_event(uidev, EV_KEY, code, value);
    if (err != 0) {
        perror("Error in writing EV_KEY event\n");
        exit(err);
    }
    err = libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);
    if (err != 0) {
        perror("Error in writing EV_SYN, SYN_REPORT, 0.\n");
        exit(err);
    }

    printf("Sending %u %u\n", code, value);
}

int
main(int argc, char **argv)
{
    int f_1 = 0;
    int f_2 = 0;
    int f_0 = 1;

    int ctrl_1 = 0;
    int ctrl_2 = 0;
    int ctrl_0 = 1;
    int ctrl_temp_0;

    struct libevdev *dev = NULL;
    const char *file;
    int fd;
    int rc = 1;

    if (argc < 2)
        goto out;

   usleep(200000); // let (KEY_ENTER), value 0 go through before
                    // grabbing the device

    file = argv[1];
    fd = open(file, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device");
        goto out;
    }

    rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        fprintf(stderr, "Failed to init libevdev (%s)\n", strerror(-rc));
        goto out;
    }

    int err;
    int uifd;
    struct libevdev_uinput *uidev;

    uifd = open("/dev/uinput", O_RDWR);
    if (uifd < 0) {
        printf("uifd < 0 (Do you have the right privileges?)\n");
        return -errno;
    }

    err = libevdev_uinput_create_from_device(dev, uifd, &uidev);
    if (err != 0)
        return err;

    int grab = libevdev_grab(dev, LIBEVDEV_GRAB);
    if (grab < 0) {
        printf("grab < 0\n");
        return -errno;
    }

    printf("Input device ID: bus %#x vendor %#x product %#x\n",
           libevdev_get_id_bustype(dev),
           libevdev_get_id_vendor(dev),
           libevdev_get_id_product(dev));
    printf("Evdev version: %x\n", libevdev_get_driver_version(dev));
    printf("Input device name: \"%s\"\n", libevdev_get_name(dev));
    printf("Phys location: %s\n", libevdev_get_phys(dev));
    printf("Uniq identifier: %s\n", libevdev_get_uniq(dev));
    print_bits(dev);
    print_props(dev);

    do {
        struct input_event ev;
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL|LIBEVDEV_READ_FLAG_BLOCKING, &ev);
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            printf("::::::::::::::::::::: dropped ::::::::::::::::::::::\n");
            while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                print_sync_event(&ev);
                rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
            }
            printf("::::::::::::::::::::: re-synced ::::::::::::::::::::::\n");
        } else if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (ev.type == EV_KEY) {
                //printf("#### ev.type == EV_KEY\n ####");

                if (ev.code == KEY_F) {
                    if (ev.value == 1) { // receiving f1

                        f_1 = 1; f_2 = 0; f_0 = 0; // set state

                        if (ctrl_1) {
                            send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 0); // fake ctrl0
                            ctrl_temp_0 = 1;
                            send_key_ev_and_sync(uidev, KEY_RIGHT, 1);
                        } else if (ctrl_2) {
                            send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 0); // fake ctrl0
                            ctrl_temp_0 = 1;
                            send_key_ev_and_sync(uidev, KEY_RIGHT, 1);
                        } else if (ctrl_0) {
                            send_key_ev_and_sync(uidev, ev.code, ev.value); // send original f1
                        }

                    } else if (ev.value == 2) { // receiving f2
                        f_2 = 1; f_1 = 0; f_0 = 0; // set state

                        if (ctrl_1) {
                            send_key_ev_and_sync(uidev, KEY_RIGHT, 2); // send right2
                        } else if (ctrl_2) {
                            send_key_ev_and_sync(uidev, KEY_RIGHT, 2); // send right2
                        } else if (ctrl_0) {
                            send_key_ev_and_sync(uidev, ev.code, ev.value); // send original f2
                        }

                    } else if (ev.value == 0) { // receiving f0
                        f_0 = 1; f_1 = 0; f_2 = 0;

                        if (ctrl_1) { // if (ctrl_temp_0)
                            send_key_ev_and_sync(uidev, KEY_RIGHT, 0); // send right0
                            ctrl_temp_0 = 0;
                            send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 1); // restore ctrl (we might wanna save the actual old value) // when we will have more maps...
                        } else if (ctrl_2) { // if (ctrl_temp_0)
                            send_key_ev_and_sync(uidev, KEY_RIGHT, 0); // send right0
                            ctrl_temp_0 = 0;
                            send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 1); // restore ctrl (we might wanna save the actual old value) // when we will have more maps...
                        } else if (ctrl_0) {
                            send_key_ev_and_sync(uidev, ev.code, ev.value); // send original f0
                        }

                    }
                } else {
                    if (ev.code == KEY_RIGHTCTRL) {
                        if (ev.value == 1) { // receiving ctrl1
                            ctrl_1 = 1; ctrl_2 = 0; ctrl_0 = 0;

                            if (f_1) {
                                send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 0); // fake ctrl0
                                ctrl_temp_0 = 1;
                                send_key_ev_and_sync(uidev, KEY_RIGHT, 1); // send right1
                            } else if (f_2) {
                                send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 0); // fake ctrl0
                                ctrl_temp_0 = 1;
                                send_key_ev_and_sync(uidev, KEY_RIGHT, 1); // send right1
                            } else if (f_0) {
                                send_key_ev_and_sync(uidev, ev.code, ev.value); // send original ctrl1
                            }

                        } else if (ev.value == 2) { // receiving ctrl2  <<<================================== buggy
                            ctrl_2 = 1; ctrl_1 = 0; ctrl_0 = 0;

                            if (f_1) {
                                if (!ctrl_0) { // might be impossible...
                                    //send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 0); // fake ctrl0
                                    //ctrl_temp_0 = 1;
                                }
                            } else if (f_2) {
                                if (!ctrl_0) { // might be impossible...
                                    //send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 0); // fake ctrl0
                                    //ctrl_temp_0 = 1;
                                }
                            } else if (f_0) {
                                send_key_ev_and_sync(uidev, ev.code, ev.value); // send original ctrl2
                            }

                        } else if (ev.value == 0) { // receiving ctrl0
                            ctrl_0 = 1; ctrl_1 = 0; ctrl_2 = 0;

                            if (f_1) {
                                printf("receiving ctrl0 (in context f_1)\n");
                                send_key_ev_and_sync(uidev, ev.code, ev.value); // send original ctrl0
                                // we know that f was acting as right
                                send_key_ev_and_sync(uidev, KEY_RIGHT, 0);
                                send_key_ev_and_sync(uidev, KEY_F, 1); // make f acting as a f again
                            } else if (f_2) {
                                printf("receiving ctrl0 (in context f_2)\n");
                                send_key_ev_and_sync(uidev, ev.code, ev.value); // send original ctrl0
                                // we know that f was acting as right
                                send_key_ev_and_sync(uidev, KEY_RIGHT, 0);
                                send_key_ev_and_sync(uidev, KEY_F, 1); // make f acting as a f again
                            } else if (f_0) {
                                printf("receiving ctrl0 (in context f_0)\n");
                                send_key_ev_and_sync(uidev, ev.code, ev.value); // send original ctrl0
                            }

                        }
                    } else { // receving a key other than f or ctrl
                        send_key_ev_and_sync(uidev, ev.code, ev.value); // send original key
                    }
                }
            } else {
                //printf("#### ev.type != EV_KEY\n ####");
                //print_event(&ev);
            }
        }
    } while (rc == LIBEVDEV_READ_STATUS_SYNC || rc == LIBEVDEV_READ_STATUS_SUCCESS || rc == -EAGAIN);

    if (rc != LIBEVDEV_READ_STATUS_SUCCESS && rc != -EAGAIN)
        fprintf(stderr, "Failed to handle events: %s\n", strerror(-rc));

    rc = 0;
out:
    libevdev_free(dev);

    return rc;
}
