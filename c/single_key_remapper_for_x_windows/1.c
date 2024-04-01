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
  (key_map[]){{ KEY_LEFT, KEY_HOME }},
  2 },
};

// Variable that store the index of currently active window map (the
// map associated with the currently focused window).
//
// The value -1 means that the currently focused window has no
// window_map associated with it, so all keys behave in the standard
// way.
//
// This variable is shared by two threads. So, we use the `volatile`
// keyword to tell the compiler that he has to write/read memory.
//
// Moreover, given that the variable holds an integer, we know that
// read/write operations on it are atomic.
volatile int currently_focused_window = -1;

void set_currently_focused_window(char *win_name) {
  // Create local variable instead to hold next value of
  // currently_focused_window, instead of directly assigning the value
  // twice. Write the global variable `currently_focused_window` only
  // once. (We want to prevent the reader thread from reading the
  // temporary -1)
  int new_currently_focused_window = -1;
  for (size_t i = 0; i < sizeof(window_maps)/sizeof(window_maps[0]); i++) {
    char *name = window_maps[i].window_class_name;
    if (strcmp(win_name, name) == 0) {
      new_currently_focused_window = i;
      break;
    }
  }
  currently_focused_window = new_currently_focused_window;
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
  //printf("code: %d, value: %d\n", code, value);
  //printf("%d\n", currently_focused_window);

  // TODO: change key code on the fly if key is mapped

  // We must make copy of currently_focused_window because we are
  // reading its value multiple times and there is another thread that
  // can change its value.
  int currently_focused_window_copy = currently_focused_window;

  if (currently_focused_window_copy != -1) {
    window_map wm = window_maps[currently_focused_window_copy];
    printf("%d\n", wm.key_maps_size);
    size_t foundI = -1;
    for (size_t i = 0; i < wm.key_maps_size; i++) {
      key_map km = wm.key_maps[i];
      if (km.key_from == code) {
        foundI = i;
        break;
      }
    }

    if (foundI != -1) {
      printf("\n\nMAPPED KEY!\n");
      printf("key map key from: %u\n", window_maps[currently_focused_window_copy].key_maps[foundI].key_from);
      printf("code handled: %u\n", code);
    } else {
      printf("NORMAL KEY!\n");
    }
  }

  // just send key through if key is not mapped (for now just send through all keys)
  int err;
  //printf("Sending %u %u\n", code, value);
  err = libevdev_uinput_write_event(uidev, EV_KEY, code, value);
  if (err != 0) {
    perror("Error in writing EV_SYN, SYN_REPORT, 0.\n");
    exit(err);
  }
  err = libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);
  if (err != 0) {
    perror("Error in writing EV_SYN, SYN_REPORT, 0.\n");
    exit(err);
  }
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
      while (rc == LIBEVDEV_READ_STATUS_SYNC) {
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
      }
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
