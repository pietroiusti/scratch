/**
   x windows key combinations remapper.

   Giulio Pietroiusti. 2023.

   This program remaps certain key combinations to either other key
   combination (e.g., alt+f -> ctrl-right) or to a single key (e.g.,
   ctrl+f -> right), only if the currently focused X window matches one
   of the strings in the mapped_windows array.


   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/*

  Remappings:

  - (l/r)ctrl+f -> right
  - (l/r)ctrl+b -> left
  - (l/r)ctrl+p -> up
  - (l/r)ctrl+n -> down

  - (l/r)ctrl+e -> end
  - (l/r)ctrl+a -> home

  - (l/r)ctl-v -> pagedown
  - (l/r)alt-v -> pageup

  - (l/r)alt+f -> ctrl+right
  - (l/r)alt-b -> ctrol+left

  Compile with:
  gcc ./01.c `pkg-config --cflags libevdev` `pkg-config --libs libevdev x11` -pthread -o 01

 */

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include "libevdev/libevdev-uinput.h"
#include "libevdev/libevdev.h"

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

char* mapped_windows[] = {
  "brave-browser",
};

// -1 if not mapped, otherwise index of mapped window
volatile int currently_focused_window;

map maps[] = {
  //        from -----------> to
  //         ^                ^
  // ________|_________  _____|________
  // |mod           key| | mod   key  |

  // C-f, C-b, C-p, C-n
  { KEY_RIGHTCTRL, KEY_F, 0, KEY_RIGHT }, { KEY_LEFTCTRL, KEY_F, 0, KEY_RIGHT },
  { KEY_RIGHTCTRL, KEY_B, 0, KEY_LEFT }, { KEY_LEFTCTRL, KEY_B, 0, KEY_LEFT },
  { KEY_RIGHTCTRL, KEY_P, 0, KEY_UP }, { KEY_LEFTCTRL, KEY_P, 0, KEY_UP },
  { KEY_RIGHTCTRL, KEY_N, 0, KEY_DOWN }, { KEY_LEFTCTRL, KEY_N, 0, KEY_DOWN },

  // C-a, C-e
  { KEY_RIGHTCTRL, KEY_A, 0, KEY_HOME }, { KEY_LEFTCTRL, KEY_A, 0, KEY_HOME },
  { KEY_RIGHTCTRL, KEY_E, 0, KEY_END }, { KEY_LEFTCTRL, KEY_E, 0, KEY_END },

  // M-f, M-b
  { KEY_RIGHTALT, KEY_F, KEY_RIGHTCTRL, KEY_RIGHT }, { KEY_LEFTALT, KEY_F, KEY_LEFTCTRL, KEY_RIGHT },
  { KEY_RIGHTALT, KEY_B, KEY_RIGHTCTRL, KEY_LEFT }, { KEY_LEFTALT, KEY_B, KEY_LEFTCTRL, KEY_LEFT },

  // M-v, C-v
  { KEY_RIGHTALT, KEY_V, 0, KEY_PAGEUP }, { KEY_LEFTALT, KEY_V, 0, KEY_PAGEUP },
  { KEY_RIGHTCTRL, KEY_V, 0, KEY_PAGEDOWN }, { KEY_LEFTCTRL, KEY_V, 0, KEY_PAGEDOWN },
  // TODO:
  // C-w
  // M-w
  // C-y
  // C-d
  // M-d
  // C-k
  // C-space
  // C-s
  // C-r
  // C-g
  // Escaping map [I usually bind it to C-q]
};

// Take index of a map in maps and send mod_to + key_to of that map
static void send_output(const struct libevdev_uinput *uidev, int i) {
  if (maps[i].mod_to)
    send_key_ev_and_sync(uidev, maps[i].mod_to, 1);
}

typedef struct {
  unsigned int code;
  int value;
} keyboard_key_state;

keyboard_key_state keyboard[] = {
  { KEY_LEFTCTRL, 0 }, // 29
  { KEY_RIGHTCTRL, 0 }, // 97
  { KEY_LEFTALT, 0 }, // 56
  { KEY_RIGHTALT, 0 },
  { KEY_P, 0 }, // 25
  { KEY_F, 0 }, // 33
  { KEY_B, 0 }, // 48
  { KEY_N, 0 }, // 49
  { KEY_V, 0 },
  { KEY_A, 0 },
  { KEY_E, 0 },
};
// KEY_RIGHT     106
// KEY_LEFT      105

void set_keyboard_state(struct input_event ev) {
  for (int i = 0; i < sizeof(keyboard)/sizeof(keyboard_key_state); i++) {
    if (keyboard[i].code == ev.code)
      keyboard[i].value = ev.value;
  }
}

int kb_state_of(unsigned int k_code) {
  for (int i = 0; i < sizeof(keyboard)/sizeof(keyboard_key_state); i++) {
    if (keyboard[i].code == k_code)
      return keyboard[i].value;
  }
  return -1;
}

struct libevdev_uinput *uidev;

// return active map of key if any, otherwise 0.
map *get_active_map_of_key(struct input_event ev) {
  int number_of_active_maps = 0;
  int index = 0;

  for (int i = 0; i < sizeof(maps)/sizeof(map); i++) {
    if (maps[i].key_from == ev.code) {
      if (kb_state_of(maps[i].mod_from) != 0) {
        number_of_active_maps++;
        index = i;
      }
    }
  }
  printf("Map index: %d\n", index);
  if (number_of_active_maps == 1)
    return &maps[index];
  else
    return 0;
}

// return active map of mod if any, otherwise 0.
map* get_active_map_of_mod(struct input_event ev) {
  int number_of_active_maps = 0;
  int index = 0;

  for (int i = 0; i < sizeof(maps)/sizeof(map); i++) {
    if (maps[i].mod_from == ev.code) {
      if (kb_state_of(maps[i].key_from) != 0) {
        number_of_active_maps++;
        index = i;
      }
    }
  }

  if (number_of_active_maps == 1)
    return &maps[index];
  else
    return 0;
}

void handle_key(struct input_event ev) {
  set_keyboard_state(ev);

  printf("handling %d, %d\n", ev.code, ev.value);

  int currently_focused_window_copy = currently_focused_window;

  if (currently_focused_window_copy == -1) {
    printf("we should not use combo maps\n");
    send_key_ev_and_sync(uidev, ev.code, ev.value);
    return;
  }

  map* map_of_key = get_active_map_of_key(ev);
  if (map_of_key)
    printf("map_of_key is truthy\n");

  map* map_of_mod = get_active_map_of_mod(ev);
  if (map_of_mod)
    printf("map_of_mod is truthy\n");

  if (map_of_key) {
    printf("we are in map_of_key block\n");
    if (ev.value == 1) {
      printf("we are in ev.value == 1 block\n");
      if (kb_state_of(map_of_key->mod_from) == 1) {
	send_key_ev_and_sync(uidev, map_of_key->mod_from, 0);
	if (map_of_key->mod_to)
	  send_key_ev_and_sync(uidev, map_of_key->mod_to, 1);
        send_key_ev_and_sync(uidev, map_of_key->key_to, 1);
      } else if (kb_state_of(map_of_key->mod_from) == 2) {
        send_key_ev_and_sync(uidev, map_of_key->mod_from, 0);
	if (map_of_key->mod_to)
	  send_key_ev_and_sync(uidev, map_of_key->mod_to, 1);
        send_key_ev_and_sync(uidev, map_of_key->key_to, 1);
      } else if (kb_state_of(map_of_key->mod_from) == 0) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
      }
    } else if (ev.value == 2) {
      if (kb_state_of(map_of_key->mod_from) == 1) {
        send_key_ev_and_sync(uidev, map_of_key->key_to, 2);
      } else if (kb_state_of(map_of_key->mod_from) == 2) {
        send_key_ev_and_sync(uidev, map_of_key->key_to, 2);
      } else if (kb_state_of(map_of_key->mod_from) == 0) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
      }
    } else if (ev.value == 0) {
      if (kb_state_of(map_of_key->mod_from) == 1) {
        send_key_ev_and_sync(uidev, map_of_key->key_to, 0);
	if (map_of_key->mod_to)
	  send_key_ev_and_sync(uidev, map_of_key->mod_to, 0);
        send_key_ev_and_sync(uidev, map_of_key->mod_from, 1);
      } else if (kb_state_of(map_of_key->mod_from) == 2) {
        send_key_ev_and_sync(uidev, map_of_key->key_to, 0);
	if (map_of_key->mod_to)
	  send_key_ev_and_sync(uidev, map_of_key->mod_to, 0);
        send_key_ev_and_sync(uidev, map_of_key->mod_from, 1);
      } else if (kb_state_of(map_of_key->mod_from) == 0) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
      }
    }
  } else if (map_of_mod) {
    printf("we are in map_of_mod block\n");
    if (ev.value == 1) {
      printf("we are in ev.value == 1  block\n");
      if (kb_state_of(map_of_mod->key_from) == 1)  {
	printf("we are in kb_state_of(map_of_mod->key_from) == 1\n");
        send_key_ev_and_sync(uidev, map_of_mod->mod_from, 0);
        send_key_ev_and_sync(uidev, map_of_mod->key_from, 0);
	if (map_of_mod->mod_to)
	  send_key_ev_and_sync(uidev, map_of_mod->mod_to, 1);
        send_key_ev_and_sync(uidev, map_of_mod->key_to, 1);
      } else if (kb_state_of(map_of_mod->key_from) == 2) {
        printf("we are in kb_state_of(map_of_mod->key_from) == 2\n");
	send_key_ev_and_sync(uidev, map_of_mod->mod_from, 0);
        send_key_ev_and_sync(uidev, map_of_mod->key_from, 0);
	if (map_of_mod->mod_to)
	  send_key_ev_and_sync(uidev, map_of_mod->mod_to, 1);
        send_key_ev_and_sync(uidev, map_of_mod->key_to, 1);
      } else if (kb_state_of(map_of_mod->key_from) == 0) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
      }
    } else if (ev.value == 2) {
      printf("we are in ev.value == 2  block\n");
      if (kb_state_of(map_of_mod->key_from) == 1)  {
        printf("What should we be doing here?");
      } else if (kb_state_of(map_of_mod->key_from) == 2) {
        printf("What should we be doing here?");
      } else if (kb_state_of(map_of_mod->key_from) == 0) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
      }
    } else if (ev.value == 0) {
      printf("we are in ev.value == 0  block\n");
      if (kb_state_of(map_of_mod->key_from) == 1)  {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
	if (map_of_mod->mod_to)
	  send_key_ev_and_sync(uidev, map_of_mod->mod_to, 0);
        send_key_ev_and_sync(uidev, map_of_mod->key_to, 0);
        send_key_ev_and_sync(uidev, map_of_mod->key_from, 1);
      } else if (kb_state_of(map_of_mod->key_from) == 2) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
	if (map_of_mod->mod_to)
	  send_key_ev_and_sync(uidev, map_of_mod->mod_to, 0);
        send_key_ev_and_sync(uidev, map_of_mod->key_to, 0);
        send_key_ev_and_sync(uidev, map_of_mod->key_from, 1);
      } else if (kb_state_of(map_of_mod->key_from) == 0) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
      }
    }
  } else {
    send_key_ev_and_sync(uidev, ev.code, ev.value);
  }
}

void *track_window() {
  Display* display;
  XEvent xevent;
  char *display_name = getenv("DISPLAY");
  display = XOpenDisplay(display_name);
  if (display == NULL) {
    printf("display null\n");
    exit(1);
  }
  Window root_window = DefaultRootWindow(display);
  Atom active_window_atom = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
  XSelectInput(display, root_window, PropertyChangeMask);
  // return values
  Atom type_return;
  int format_return;
  unsigned long nitems_return;
  unsigned long bytes_left;
  unsigned char *data;

  while (1) {
    XNextEvent(display, &xevent);

    if (xevent.xproperty.atom != active_window_atom)
      continue;

    XGetWindowProperty(display,
                       root_window,
                       active_window_atom,
                       0,
                       1,
                       False,
                       XA_WINDOW,
                       &type_return,   //should be XA_WINDOW
                       &format_return, //should be 32
                       &nitems_return, //should be 1 (zero if there is no such window)
                       &bytes_left,    //should be 0 (i'm not sure but should be atomic read)
                       &data           //should be non-null
		       );

    Window focused_window = *(Window *)data;

    if (focused_window == 0)
      continue;

    char* window_name1;
    if (XFetchName(display, focused_window, &window_name1) != 0) {
      printf("The active window is: %s\n", window_name1);
      XFree(window_name1);
    }
    XClassHint class_hint;
    if (XGetClassHint(display, focused_window, &class_hint) == 0)
      continue;
    char *window_class = class_hint.res_class;
    char *window_name2 = class_hint.res_name;
    printf("res.class = %s\n", window_class);
    printf("res.name = %s\n", window_name2);
    printf("\n\n");

    int currently_focused_window_next_value = -1;
    for (int i = 0; i < sizeof(mapped_windows)/sizeof(char*); i++) {
      printf("comparing: [%s] with [%s]\n", mapped_windows[i], window_name2);
      if (strcmp(mapped_windows[i], window_name2) == 0)
	currently_focused_window_next_value = i;
    }
    currently_focused_window = currently_focused_window_next_value;
  }
}

int main(int argc, char **argv)
{
  pthread_t xthread;
  int thread_return_value;
  thread_return_value = pthread_create(&xthread, NULL, track_window, NULL);

  struct libevdev *dev = NULL;
  const char *file;
  int fd;
  int rc = 1;

  if (argc < 2)
    goto out;

  usleep(200000); // let (KEY_ENTER), value 0 go through before

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

  do {
    struct input_event ev;
    rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL|LIBEVDEV_READ_FLAG_BLOCKING, &ev);
    if (rc == LIBEVDEV_READ_STATUS_SYNC) {
      printf("Dropped\n");
      while (rc == LIBEVDEV_READ_STATUS_SYNC) {
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
      }
      printf("Re-synced\n");
    } else if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
      if (ev.type == EV_KEY)
        handle_key(ev);
    }
  } while (rc == LIBEVDEV_READ_STATUS_SYNC || rc == LIBEVDEV_READ_STATUS_SUCCESS || rc == -EAGAIN);

  if (rc != LIBEVDEV_READ_STATUS_SUCCESS && rc != -EAGAIN)
    fprintf(stderr, "Failed to handle events: %s\n", strerror(-rc));

  rc = 0;
 out:
  libevdev_free(dev);

  return rc;
}
