/*
  Map Right Ctrl + F to Right.
  compile with:
  gcc -g `pkg-config --cflags libevdev` ./03_ctrl_f_mapped_to_right.c `pkg-config --libs libevdev` -o 03

  The template I've used is from
  https://gitlab.freedesktop.org/libevdev/libevdev/blob/master/tools/libevdev-events.c
  That code has the following header:

  SPDX-License-Identifier: MIT
  Copyright © 2013 Red Hat, Inc.


  The license of the code I'm adding is MIT too.





  Some elaboration:

  - Keyboard event values:
    - 1 = gone down
    - 2 = held down
    - 0 = gone up

  - State of the keyboard ≠ State of the userspace.
    - Example:...

  - Conditionals:
    - 1) f ≠ 0 at keyboard level    ---> rctrl must be 0 at userspace level

      (if f ≠ 0 at kb level, then it either ``sends'' f, or sends
      right --- when rctrl is down.

      If it sends f, then rctrl must be currently 0 at userspace
      level, otherwise we would be sending right. If it sends right,
      then rctrl must be currently 0 at userspace level, because we
      want to send right, not rctrl+right.)


    - 2) rctrl ≠ 0 at keyboard level ---> f must be 0 at userspace level

      (rctrl can either send rctrl or nothing.

      If it sends rctrl, then f must be currently 0 at userspace
      level, otherwise we would not be send right, which is what we
      are supposed to send when f and rctrl are both down at keyboard
      level. If it sends nothing (while being down at kb level), then
      f must be currently 0 at userspace level, because otherwise
      rctrl should send rctrl.)

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

typedef struct {
    unsigned int mod_from;
    unsigned int key_from;
    unsigned int mod_to;
    unsigned int key_to;
} map;

map maps[] = {
    //        from -----------> to
    //         ^                ^
    // ________|_________  _____|________
    // |mod           key| | mod   key  |
    { KEY_RIGHTCTRL, KEY_F, 0, KEY_RIGHT },
    { KEY_RIGHTCTRL, KEY_B, 0, KEY_LEFT },
};

typedef struct {
    unsigned int code;
    int value;
} keyboard_key_state;

keyboard_key_state keyboard[] = {
    { KEY_RIGHTCTRL, 0 },
    { KEY_F, 1 },
    { KEY_B, 1 },
};

/* (partial) state of the keyboard */
int f_1 = 0;
int f_2 = 0;
int f_0 = 1;
int rctrl_1 = 0;
int rctrl_2 = 0;
int rctrl_0 = 1;

struct libevdev_uinput *uidev;

map* is_mapped_key(struct input_event ev) {
    for (int i = 0; sizeof(maps)/sizeof(map); i++) {
        if (maps[i].key_from == ev.code)
            return &maps[i];
    }
    return 0;
}

map* is_mapped_mod(struct input_event ev) {
    for (int i = 0; sizeof(maps)/sizeof(map); i++) {
        if (maps[i].mod_from == ev.code)
            return &maps[i];
    }
    return 0;
}

void set_keyboard_state(struct input_event ev) {
    for (int i = 0; i < sizeof(keyboard)/sizeof(keyboard_key_state); i++) {
        if (keyboard[i].code == ev.code)
            keyboard[i].value = ev.value;
    }
}

void handle_key2(struct input_event ev) {
    set_keyboard_state(ev);

    map* mapped_key = is_mapped_key(ev);
    map* mapped_mod = is_mapped_mod(ev);

    if (mapped_key) {
        if (ev.value == 1) {
            if (mapped_key->mod_from == 1) {
                // Not considering when a key is mapped more than once
                // and
                // not considering cases with mod_to.
                send_key_ev_and_sync(uidev, mapped_key->mod_from, 0);
                send_key_ev_and_sync(uidev, mapped_key->key_to, 1);
            } else if (mapped_key->mod_from == 2) {
                send_key_ev_and_sync(uidev, mapped_key->mod_from, 0);
                send_key_ev_and_sync(uidev, mapped_key->key_to, 1);
            } else if (mapped_key->mod_from == 0) {
                send_key_ev_and_sync(uidev, ev.code, ev.value);
            }
        } else if (ev.value == 2) {
            if (mapped_key->mod_from == 1) {
                send_key_ev_and_sync(uidev, mapped_key->key_to, 2);
            } else if (mapped_key->mod_from == 2) {
                send_key_ev_and_sync(uidev, mapped_key->key_to, 2);
            } else if (mapped_key->mod_from == 0) {
                send_key_ev_and_sync(uidev, ev.code, ev.value);
            }
        } else if (ev.value == 0) {
            if (mapped_key->mod_from == 1) {
                send_key_ev_and_sync(uidev, mapped_key->key_to, 0);
                send_key_ev_and_sync(uidev, mapped_key->mod_from, 1);
            } else if (mapped_key->mod_from == 2) {
                send_key_ev_and_sync(uidev, mapped_key->key_to, 0);
                send_key_ev_and_sync(uidev, mapped_key->mod_from, 1);
            } else if (mapped_key->mod_from == 0) {
                send_key_ev_and_sync(uidev, ev.code, ev.value);
            }
        }
    } else if (mapped_mod) {
        if (ev.value == 1) {
            if (mapped_mod->key_from == 1)  {
                send_key_ev_and_sync(uidev, mapped_mod->mod_from, 0);
                send_key_ev_and_sync(uidev, mapped_mod->key_from, 0);
                send_key_ev_and_sync(uidev, mapped_mod->key_to, 1);
            } else if (mapped_mod->key_from == 2) {
                send_key_ev_and_sync(uidev, mapped_mod->mod_from, 0);
                send_key_ev_and_sync(uidev, mapped_mod->key_from, 0);
                send_key_ev_and_sync(uidev, mapped_mod->key_to, 1);
            } else if (mapped_mod->key_from == 0) {
                send_key_ev_and_sync(uidev, ev.code, ev.value);
            }
        } else if (ev.value == 2) {
            if (mapped_mod->key_from == 1)  {
                printf("the alleged impossible is happening");
            } else if (mapped_mod->key_from == 2) {
                printf("the alleged impossible is happening");
            } else if (mapped_mod->key_from == 0) {
                send_key_ev_and_sync(uidev, ev.code, ev.value);
            }
        } else if (ev.value == 0) {
            if (mapped_mod->key_from == 1)  {
                send_key_ev_and_sync(uidev, ev.code, ev.value);
                send_key_ev_and_sync(uidev, mapped_mod->key_to, 0);
                send_key_ev_and_sync(uidev, mapped_mod->key_from, 1);
            } else if (mapped_mod->key_from == 2) {
                send_key_ev_and_sync(uidev, ev.code, ev.value);
                send_key_ev_and_sync(uidev, mapped_mod->key_to, 0);
                send_key_ev_and_sync(uidev, mapped_mod->key_from, 1);
            } else if (mapped_mod->key_from == 0) {
                send_key_ev_and_sync(uidev, ev.code, ev.value);                
            }
        }        
    } else {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
    }
}

void handle_key(struct input_event ev) {
    if (ev.code == KEY_F) {
        if (ev.value == 1) { // receiving f1
            f_1 = 1; f_2 = 0; f_0 = 0; // set keyboard state

            if (rctrl_1) {
                send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 0); // fake ctrl0
                send_key_ev_and_sync(uidev, KEY_RIGHT, 1);
            } else if (rctrl_2) {
                send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 0); // fake ctrl0
                send_key_ev_and_sync(uidev, KEY_RIGHT, 1);
            } else if (rctrl_0) {
                send_key_ev_and_sync(uidev, ev.code, ev.value); // send original f1
            }

        } else if (ev.value == 2) { // receiving f2
            f_2 = 1; f_1 = 0; f_0 = 0; // set keyboard state

            if (rctrl_1) {
                send_key_ev_and_sync(uidev, KEY_RIGHT, 2); // send right2
            } else if (rctrl_2) {
                send_key_ev_and_sync(uidev, KEY_RIGHT, 2); // send right2
            } else if (rctrl_0) {
                send_key_ev_and_sync(uidev, ev.code, ev.value); // send original f2
            }

        } else if (ev.value == 0) { // receiving f0
            f_0 = 1; f_1 = 0; f_2 = 0; // set keyboard state

            if (rctrl_1) {
                send_key_ev_and_sync(uidev, KEY_RIGHT, 0); // send right0
                send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 1); // restore ctrl (we might wanna save the actual old value) // when we will have more maps...
            } else if (rctrl_2) {
                send_key_ev_and_sync(uidev, KEY_RIGHT, 0); // send right0
                send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 1); // restore ctrl (we might wanna save the actual old value) // when we will have more maps...
            } else if (rctrl_0) {
                send_key_ev_and_sync(uidev, ev.code, ev.value); // send original f0
            }

        }
    } else {
        if (ev.code == KEY_RIGHTCTRL) {
            if (ev.value == 1) { // receiving ctrl1
                rctrl_1 = 1; rctrl_2 = 0; rctrl_0 = 0; // set keyboard state

                if (f_1) {
                    printf("receiving ctrl1 (in context f1)\n");
                    send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 0); // fake ctrl0
                    send_key_ev_and_sync(uidev, KEY_F, 0);
                    send_key_ev_and_sync(uidev, KEY_RIGHT, 1); // send right1
                } else if (f_2) {
                    printf("receiving ctrl1 (in context f2)\n");
                    send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 0); // fake ctrl0
                    send_key_ev_and_sync(uidev, KEY_F, 0);
                    send_key_ev_and_sync(uidev, KEY_RIGHT, 1); // send right1
                } else if (f_0) {
                    printf("receiving ctrl1 (in context f0)\n");
                    send_key_ev_and_sync(uidev, ev.code, ev.value); // send original ctrl1
                }

            } else if (ev.value == 2) { // receiving ctrl2
                rctrl_2 = 1; rctrl_1 = 0; rctrl_0 = 0; // set keyboard state

                if (f_1) {
                    if (!rctrl_0) { // might be impossible...
                        //send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 0); // fake ctrl0
                        printf("the alleged impossible is happening");
                    }
                } else if (f_2) {
                    if (!rctrl_0) { // might be impossible...
                        //send_key_ev_and_sync(uidev, KEY_RIGHTCTRL, 0); // fake ctrl0
                        printf("the alleged impossible is happening");
                    }
                } else if (f_0) {
                    send_key_ev_and_sync(uidev, ev.code, ev.value); // send original ctrl2
                }

            } else if (ev.value == 0) { // receiving ctrl0
                rctrl_0 = 1; rctrl_1 = 0; rctrl_2 = 0; // set keyboard state

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
}

int
main(int argc, char **argv)
{
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

    printf("mod_from: %d\n", maps[0].mod_from);
    printf("key_from: %d\n", maps[0].key_from);
    printf("mod_to: %d\n", maps[0].mod_to);
    printf("key_to: %d\n", maps[0].key_to);

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
                handle_key2(ev);
            } else {
                ;
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
