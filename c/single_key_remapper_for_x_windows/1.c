/**
   x windows key single key remapper.

   Giulio Pietroiusti. 2023.

   This program allows to remap single keys to other single keys when
   the name of currently focused X window matches one of some strings
   of our choice.

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


/**
   Compile with:
   gcc -pthread -g `pkg-config --cflags libevdev` `pkg-config --libs libevdev x11` ./1.c -o 1
*/

#include <linux/input-event-codes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include "libevdev/libevdev.h"
#include "libevdev/libevdev-uinput.h"
#include <errno.h>

typedef struct {
  unsigned key_from;
  unsigned key_to;
} key_map;

typedef struct {
  char *window_class_name;
  key_map *key_maps;
  unsigned key_maps_size;
} window_map;

window_map window_maps[] = {
  { "Chromium",
  (key_map[]){{ KEY_CAPSLOCK, KEY_ESC },
              { KEY_ESC, KEY_CAPSLOCK }},
  2 },
  { "Brave-browser",
  (key_map[]){{ KEY_LEFT, KEY_HOME },
              { KEY_UP,KEY_PAGEUP },
              { KEY_RIGHT, KEY_END },
              { KEY_DOWN, KEY_PAGEDOWN }},
  4 },
  { "Emacs",
  (key_map[]){{ KEY_LEFT, KEY_HOME },}}
};

// The value -1 is used to mean that the currently focused window has
// no window_map associated with it, that is, it behaves in the
// stadard way. Positive integers, instead, are indices of the
// window_maps array's elements.
volatile int currently_focused_window = -1;

void set_currently_focused_window(char *win_name) {
  int currently_focused_window_next_value = -1;
  for (size_t i = 0; i < sizeof(window_maps)/sizeof(window_maps[0]); i++) {
    char *n = window_maps[i].window_class_name;
    if (strcmp(win_name, n) == 0) {
      currently_focused_window_next_value = i;
      break;
    }
  }
  currently_focused_window = currently_focused_window_next_value;
  printf("currently_focused_window set to %d\n", currently_focused_window);
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

  unsigned char firstLoop = 1;

  // Get name of the focused window window when focus changes (and
  // when program starts)
  do {
    if (!firstLoop) {
      XNextEvent(display, &xevent);

      if (xevent.xproperty.atom != active_window_atom)
        continue;
    }

    if (firstLoop) {
      firstLoop = 0;
    }

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

    if (focused_window == 0) {
      continue;
    }

    char* window_name1;
    if (XFetchName(display, focused_window, &window_name1) != 0) {
      printf("The active window is: %s\n", window_name1);
      XFree(window_name1);
    }
    XClassHint class_hint;
    if (XGetClassHint(display, focused_window, &class_hint) == 0) {
      continue;
    }
    char *window_class = class_hint.res_class;
    char *window_name2 = class_hint.res_name;
    printf("res.class = %s\n", window_class);
    printf("res.name = %s\n", window_name2);
    printf("\n\n");

    set_currently_focused_window(window_class);

  } while (1);
}

void printConf(void) {
  for (size_t i = 0; i < sizeof(window_maps)/sizeof(window_maps[0]); i++) {
    printf("The %s map has %u key maps:\n", window_maps[i].window_class_name, window_maps[i].key_maps_size);

    for (size_t j = 0; j < window_maps[i].key_maps_size; j++) {
      printf("%i --> %i\n", window_maps[i].key_maps[j].key_from, window_maps[i].key_maps[j].key_to);
    }
  }
}

static void handle_ev_key(const struct libevdev_uinput *uidev, unsigned int code, int value) {
  printf("code: %d, value: %d\n", code, value);
}

int main(int argc, char **argv) {
  // Print stuff
  printf("Initializing...\n");
  printConf();

  // Start tracking windows
  pthread_t track_window_thread;
  int track_window_thread_return_value = pthread_create(&track_window_thread, NULL, track_window, NULL);

  struct libevdev *dev = NULL;
    const char *file;
    int fd;
    int rc = 1;

    if (argc < 2)
        goto out;

    usleep(100000); // let (KEY_ENTER), value 0 go through before

    file = argv[1];
    fd = open(file, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device\n");
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

    do {
        struct input_event ev;
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL|LIBEVDEV_READ_FLAG_BLOCKING, &ev);
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            printf("janus_key: dropped\n");
            while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
            }
            printf("janus_key: re-synced\n");
        } else if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (ev.type == EV_KEY) {
                handle_ev_key(uidev, ev.code, ev.value);
            }
        }
    } while (rc == LIBEVDEV_READ_STATUS_SYNC || rc == LIBEVDEV_READ_STATUS_SUCCESS || rc == -EAGAIN);
    
    if (rc != LIBEVDEV_READ_STATUS_SUCCESS && rc != -EAGAIN)
        fprintf(stderr, "Failed to handle events: %s\n", strerror(-rc));
    
    rc = 0;
out:
    libevdev_free(dev);
    
    return rc;

  return 0;
}
