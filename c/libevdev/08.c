/*
  In progress: this programs attemps to combine two sort of mapping I
  have already developed elsewhere separately:

  - 1) Map single keys to other single keys (this function was
    performed by janus_key). (E.g.,

    CAPS -> ESC on tap
    ESC -> CAPS on tap .)

  AND

  - 2) Map multiple combos to a single key or combo. (this function
    was performed by 06.c) (e.g,

    - (l/r)ctrl+f -> right
    - (l/r)ctrl+b -> left
    - (l/r)ctrl+p -> up
    - (l/r)ctrl+n -> down

    - (l/r)ctrl+e -> end
    - (l/r)ctrl+a -> home

    - (l/r)ctl-v -> pagedown
    - (l/r)alt-v -> pageup

    - (l/r)alt+f -> ctrl+right
    - (l/r)alt-b -> ctrol+left .)

  MOREOVER, this program allows for different mappings depending on
  which X windows is currently focused.

  [After 1) and 2) we will need add the final functionality:
  attributing certain keys a secondary function on hold. Originally
  performed by janus-key.]

  ###### ###### ###### ###### ###### ######

  Compile with:
  gcc -g `pkg-config --cflags libevdev` `pkg-config --libs libevdev x11` ./08.c `pkg-config --libs libevdev` -pthread -o 08

  ###### ###### ###### ###### ###### ######
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <stddef.h> // ??
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "libevdev/libevdev-uinput.h"
#include "libevdev/libevdev.h"
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <pthread.h>

typedef struct {
  unsigned int mod_from;
  unsigned int key_from;
  unsigned int mod_to;
  unsigned int key_to;
} key_map;

typedef struct {
  char* class_name;
  unsigned int size;
  key_map key_maps[];
} window_map;

window_map default_map = {
  "Default",
  11,
  {
    //mod_from       key_from      mod_to         key_to
    { 0,             KEY_CAPSLOCK, 0,             KEY_ESC,       },
    { 0,             KEY_ENTER,    0,             0,             },
    { KEY_RIGHTCTRL, KEY_ESC,      0,             KEY_RIGHT,     },
    { 0,             KEY_ESC,      0,             KEY_CAPSLOCK,  },
    { 0,             KEY_W,        0,             KEY_1,         },
    { KEY_RIGHTALT,  KEY_F,        KEY_RIGHTCTRL, KEY_RIGHT,     },
    { KEY_RIGHTCTRL, 0,            KEY_RIGHTALT,  0,             },
    { KEY_RIGHTCTRL, KEY_F,        0,             KEY_RIGHT,     },
    { KEY_SYSRQ,     0,            KEY_RIGHTALT,  0,             },
    { 0,             KEY_A,        0,             KEY_RIGHTCTRL, },
    { 0,             KEY_Q,        0,             KEY_F,         },
  }
};

window_map brave_map = {
  "Brave-browser",
  4,
  { // Just some random stuff for tests
    { KEY_RIGHTALT,  KEY_F,        KEY_RIGHTCTRL, KEY_LEFT, },
    { KEY_RIGHTCTRL, KEY_G,        0,             KEY_ESC,  },
    { 0,             KEY_ESC,      0,             KEY_F,    },
    { 0,             KEY_ENTER,    0,             KEY_F,    },
  }
};

// Represents the current state of the physical keyboard's keys. A key
// can be in either 1, or 2, or 0 state (value).
typedef struct {
  unsigned int code; // Code of the key
  int value;         // Status of key (either 1, or 2, or 0)
  /* struct timespec last_time_down; // Last time value was set to 11 */
} keyboard_key_state2;

// Represents the current state of the physical keyboard's keys. A key
// can be in either 1, or 2, or 0 state (value).
//
// IMPORTANT: each key used in the config must also appear in this
// array.
keyboard_key_state2 keyboard2[] = {
  { KEY_LEFTCTRL, 0 }, // 29
  { KEY_RIGHTCTRL, 0 }, // 97
  { KEY_LEFTALT, 0 }, // 56
  { KEY_RIGHTALT, 0 }, // 100
  { KEY_CAPSLOCK, 0 }, // 58
  { KEY_ENTER, 0 }, // 28
  { KEY_RIGHT, 0 }, // 106
  { KEY_SYSRQ, 0 }, // 99
  { KEY_ESC, 0 }, // 1
  { KEY_P, 0 }, // 25
  { KEY_F, 0 }, // 33
  { KEY_B, 0 }, // 48
  { KEY_N, 0 }, // 49
  { KEY_V, 0 }, // 47
  { KEY_A, 0 }, // 30
  { KEY_E, 0 }, // 18
  { KEY_W, 0 }, // 17
  { KEY_G, 0 }, // 34
  { KEY_Q, 0 }, // 16
};

window_map* window_maps[] = {
  &default_map,
  &brave_map,
};

// 0 is the index of the default window map (in the window_maps array)
// which represents the set of those key_maps which are valid in any
// window, unless overruled by a specific window map.
volatile int currently_focused_window = 0;

struct libevdev_uinput *uidev;

void set_currently_focused_window(char* name) {
  int currently_focused_window_next_value = 0;

  // Start from second element, given that the first is the default
  // map which always applies.
  for (int i = 1; i < sizeof(window_maps)/sizeof(window_map*); i++) {
    if (strcmp(name, window_maps[i]->class_name) == 0) {
      currently_focused_window_next_value = i;
      break;
    }
  }

  currently_focused_window = currently_focused_window_next_value;
  printf("currently_focused_window set to %d\n", currently_focused_window_next_value);
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

  // Get name of the focused window window at startup
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
  if (focused_window) {
    char* window_name1;
    if (XFetchName(display, focused_window, &window_name1) != 0) {
      printf("The active window is: %s\n", window_name1);
      XFree(window_name1);
    }
    XClassHint class_hint;
    if (XGetClassHint(display, focused_window, &class_hint)) {
      char *window_class = class_hint.res_class;
      char *window_name2 = class_hint.res_name;
      printf("res.class = %s\n", window_class);
      printf("res.name = %s\n", window_name2);
      printf("\n\n");

      set_currently_focused_window(window_class);
    }
  }

  // Get name of the focused window window when focus changes
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

    set_currently_focused_window(window_class);
  }
}

void set_keyboard_state(struct input_event ev) {
  for (int i = 0; i < sizeof(keyboard2)/sizeof(keyboard_key_state2); i++) {
    if (keyboard2[i].code == ev.code) {
      keyboard2[i].value = ev.value;
      /* if (ev.value == 1) */
      /*   clock_gettime(CLOCK_MONOTONIC, &keyboard2[i].last_time_down); */
    }
  }
}

void handle_key(struct input_event ev) {
  printf("%i (%i)\n", ev.code, ev.value);

  // Update keyboard state
  set_keyboard_state(ev);
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
  // (Probably better: We could just send KEY_ENTER 0 instead)

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