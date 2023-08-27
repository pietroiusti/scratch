/*
  In progress: this programs attemps to combine two sort of mapping I
  have already developed elsewhere separately:

  - 1) Map single keys to other single keys (this function was
    performed by janus_key).

    CAPS -> ESC on tap
    ESC -> CAPS on tap

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
  performed by janus-key]

  ###### ###### ###### ###### ###### ######

  Compile with:
  gcc -g `pkg-config --cflags libevdev` `pkg-config --libs libevdev x11` ./07_b.c `pkg-config --libs libevdev` -pthread -o 07b

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

// 0 is the index of the default window map (in the window_maps array)
// which represents the set of those key_maps which are valid in any
// window, unless overruled by a specific window map.
volatile int currently_focused_window = 0;

// Modified key: key to which a primary and/or a secondary function
// has been assigned. (Those keys to which a secondary function has
// been assigned are called `janus keys`.)
typedef struct {
    unsigned int key;
    unsigned int primary_function;
    unsigned int secondary_function;
    unsigned int state;
    struct timespec last_time_down;
} mod_key;

// Delay in milliseconds.
unsigned int max_delay = 300; // If a key is held down for a time
			      // greater than max_delay, then, when
			      // released, it will not send its
			      // primary function

typedef struct {
  unsigned int mod_from;
  unsigned int key_from;
  unsigned int mod_to;
  unsigned int key_to;
  unsigned int on_hold; // on hold
} key_map;

typedef struct {
  char* class_name;
  unsigned int size;
  key_map key_maps[];
} window_map;

window_map default_map = {
  "Default",
  9,
  {
    { 0,             KEY_CAPSLOCK, 0,             KEY_ESC,      KEY_LEFTALT   },
    { 0,             KEY_ENTER,    0,             0,            KEY_RIGHTCTRL },
    { KEY_RIGHTCTRL, KEY_ESC,      0,             KEY_RIGHT,    0             },
    { 0,             KEY_ESC,      0,             KEY_CAPSLOCK, 0             },
    { 0,             KEY_W,        0,             KEY_1,        0             },
    { KEY_RIGHTALT,  KEY_F,        KEY_RIGHTCTRL, KEY_RIGHT,    0             },
    { KEY_RIGHTCTRL, 0,            KEY_RIGHTALT,  0,            0             },
    { KEY_RIGHTCTRL, KEY_F,        0,             KEY_RIGHT,    0             },
    { KEY_SYSRQ,     0,            KEY_RIGHTALT,  0,            0             },
  }
};

window_map brave_map = {
  "Brave-browser",
  3,
  { // Just some random stuff for tests
    { KEY_RIGHTALT,  KEY_F,        KEY_RIGHTCTRL, KEY_LEFT,    0             },
    { 0,             KEY_ESC,      0,             KEY_F,       0             },
    { 0,             KEY_ENTER,    0,             KEY_F,       0             },
  }
};

window_map* window_maps[] = {
  &default_map,
  &brave_map,
};

char* mapped_window_class_names[] = {
  "Default", // this should not be modified
  "Brave-browser",
};

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

// Take index of a map in maps and send mod_to + key_to of that map
static void send_output(const struct libevdev_uinput *uidev, int i) {
  if (maps[i].mod_to)
    send_key_ev_and_sync(uidev, maps[i].mod_to, 1);
}

typedef struct {
  unsigned int code;
  int value;
} keyboard_key_state;


// ## Represents the current state keyboard's keys state (new
// version).  A key can be in either 1, or 2, or 0 state (value). Now
// we also store the last time down (which gets registered on ev.value
// 1).
typedef struct {
  unsigned int code; // Code of the key
  int value;         // Status of key (either 1, or 2, or 0)
  struct timespec last_time_down; // Last time value was set to 1
} keyboard_key_state2;
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
};

void print_keyboard2() {
  for (int i = 0; i < sizeof(keyboard2)/sizeof(keyboard_key_state2); ++i) {
    printf("########################################\n");
    printf("Key code: %u\n", keyboard2[i].code);
    printf("Key value: %d\n", keyboard2[i].value);
    printf("Last time down: %lld.%09ld seconds\n", (long long)keyboard2[i].last_time_down.tv_sec, keyboard2[i].last_time_down.tv_nsec);
    printf("########################################\n");
    printf("\n");
  }
}

void set_keyboard_state2(struct input_event ev) {
  for (int i = 0; i < sizeof(keyboard2)/sizeof(keyboard_key_state2); i++) {
    if (keyboard2[i].code == ev.code) {
      keyboard2[i].value = ev.value;
      if (ev.value == 1)
        clock_gettime(CLOCK_MONOTONIC, &keyboard2[i].last_time_down);
    }
  }
}

int state_of(unsigned int k_code) {
  for (int i = 0; i < sizeof(keyboard2)/sizeof(keyboard_key_state2); i++) {
    if (keyboard2[i].code == k_code)
      return keyboard2[i].value;
  }
  return -1;
}

struct libevdev_uinput *uidev;

static key_map* is_key_in_single_map(int key) {
  int i = currently_focused_window;

  if (i == 0) { // only default map
    size_t length = window_maps[i]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[0]->key_maps[j].key_from == key && window_maps[0]->key_maps[j].mod_from == 0)
        return &window_maps[0]->key_maps[j];

  } else { // non-default map + default map

    // Non-default maps take precedence over the default map in this
    // case.

    // First search in the specific map
    size_t length = window_maps[i]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[i]->key_maps[j].key_from == key && window_maps[i]->key_maps[j].mod_from == 0)
        return &window_maps[i]->key_maps[j];

    // If we haven't found it and returned, then let's look in the default map
    length = window_maps[i]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[i]->key_maps[j].key_from == key && window_maps[i]->key_maps[j].mod_from == 0)
        return &window_maps[i]->key_maps[j];
  }

  return 0;
}

static key_map* is_mod_in_single_map(int mod) {
  int i = currently_focused_window;

  if (i == 0) { // only default map
    size_t length = window_maps[0]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[0]->key_maps[j].key_from == 0 && window_maps[0]->key_maps[j].mod_from == mod)
        return &window_maps[0]->key_maps[j];
  } else { // non-default map + default map

    // Non-default maps take precedence over the default map in this
    // case.

    // First search in the specific map
    size_t length = window_maps[i]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[i]->key_maps[j].key_from == 0 && window_maps[i]->key_maps[j].mod_from == mod)
        return &window_maps[i]->key_maps[j];

    // If we haven't found it and returned, then let's look in the default map
    length = window_maps[i]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[i]->key_maps[j].key_from == 0 && window_maps[i]->key_maps[j].mod_from == mod)
        return &window_maps[i]->key_maps[j];
  }

  return 0;
}

static int are_the_same_map(key_map* km1, key_map* km2) {
  return km1->key_from == km2->key_from
         &&
         km1->mod_from == km2->mod_from;
}

// Return pointer to combo key map which is uniquely active, if any,
// otherwise return 0. A combo key map is uniquely active if it's
// active and no other combo key map is active.
//
// What if there is an active key map in the specific window map and
// an also an active key map, a different one, in the default window
// map?
//
//     Suppose I keep down RCTRL and RALT and then I tap F. A RCTRL+F
//     non-default map and a RALT+F default map might exist. What to
//     do in this case? I think here we should be sending
//     RCTRL+RALT+F, so neither maps should be considered active; we
//     should return 0.
//
//         This is making me think: suppose, again, I keep down RCTRL
//         and RALT and then I tap F. Furthermore, suppose there is
//         only one relevant combo map --- say RCTRL+F. The latter map
//         should not be considered active; we should be sending
//         RCTRL+RALT+F, and we should NOT be sending
//         RALT+REMAPPED(RCTRL+F). [TODO]
//
// What if there is an active key map in the specific window map and
// an also an active key map, the same one, in the default window map?
//
//     The specific takes precedence over the default. For example, we
//     might have RALT+F in the default window map mapped to FOO and
//     in the brave map mapped to BAR. If we are in brave and hit
//     RALT+F, then the key map in the brave map should be considered
//     active.
//
// If there is only a specific map? Obvious.
//
// If there is only a default map? Obvious.
//
static key_map* is_key_in_uniquely_active_combo_map(int key) {
  int i = currently_focused_window;
  key_map* default_win_map_result = 0; // key_map in default window map
  key_map* non_default_win_map_result = 0; // key_map in non-default window map

  // Find relevant key map in non-default window map, if any.
  if (i != 0) {
    window_map* w_map = window_maps[i];
    size_t length = w_map->size;
    for (size_t j = 0; j < length; j++) {
      if (w_map->key_maps[j].key_from == key && w_map->key_maps[j].mod_from != 0) {

        if (state_of(w_map->key_maps[j].mod_from) != 0) {
          if (!non_default_win_map_result) {
            non_default_win_map_result = &w_map->key_maps[j];
          } else {
            return 0; // We found more than one active combo map in the non-default window map
          }
        }
      }
    }
  }

  if (!non_default_win_map_result) {
    // Find relevant key map in default window map, if any.
    window_map* default_w_map = window_maps[0];
    size_t length = default_w_map->size;
    for (size_t j = 0; j < length; j++) {
      if (default_w_map->key_maps[j].key_from == key && default_w_map->key_maps[j].mod_from != 0) {
        if (state_of(default_w_map->key_maps[j].mod_from)) {
          if (!default_win_map_result) {
            default_win_map_result = &default_w_map->key_maps[j];
          } else {
            return 0; // We found more than one active combo map in the default window map
          }
        }
      }
    }
  }

  if (non_default_win_map_result && default_win_map_result) {
    // if they are the same map, then return the non-default
    if(are_the_same_map(non_default_win_map_result, default_win_map_result))
      return non_default_win_map_result;
    else // if they are not the same map, then we considere neither active [TODO: explain]
      return 0;
  } else if (non_default_win_map_result) {
    return non_default_win_map_result;
  } else {
    return default_win_map_result;
  }
}

static key_map* is_key_in_combo_map(int key) {
  int i = currently_focused_window;

  if (i == 0) {
    size_t length = window_maps[0]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[0]->key_maps[j].key_from == key && window_maps[0]->key_maps[j].mod_from != 0)
        return &window_maps[0]->key_maps[j];
  } else {

    // Non-default maps take precedence over the default map in this
    // case.

    // First search in the specific map
    size_t length = window_maps[i]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[i]->key_maps[j].key_from == key && window_maps[i]->key_maps[j].mod_from != 0)
        return &window_maps[i]->key_maps[j];

    // If we haven't found it and returned, then let's look in the default map
    length = window_maps[i]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[i]->key_maps[j].key_from == key && window_maps[i]->key_maps[j].mod_from != 0)
        return &window_maps[i]->key_maps[j];
  }

  return 0;
}

static key_map* is_mod_in_combo_map(int mod) {
  int i = currently_focused_window;

  if (i == 0) {
    size_t length = window_maps[0]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[0]->key_maps[j].key_from != 0 && window_maps[0]->key_maps[j].mod_from == mod)
        return &window_maps[0]->key_maps[j];
  } else {

    // Non-default maps take precedence over the default map in this
    // case.

    // First search in the specific map
    size_t length = window_maps[i]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[i]->key_maps[j].key_from != 0 && window_maps[i]->key_maps[j].mod_from == mod)
        return &window_maps[i]->key_maps[j];

    // If we haven't found it and returned, then let's look in the default map
    length = window_maps[i]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[i]->key_maps[j].key_from != 0 && window_maps[i]->key_maps[j].mod_from == mod)
        return &window_maps[i]->key_maps[j];
  }

  return 0;
}

void handle_key(struct input_event ev) {
  printf("\n\n((((( handling %d, %d )))))\n\n", ev.code, ev.value);

  // Update keyboard state
  set_keyboard_state2(ev);

  //print_keyboard2();

  printf("The currently focused window id is %d\n", currently_focused_window);

  key_map* k_sm_i = is_key_in_single_map(ev.code);
  key_map* m_sm_i = is_mod_in_single_map(ev.code);
  key_map* k_cm_i = is_key_in_combo_map(ev.code);
  key_map* m_cm_i = is_mod_in_combo_map(ev.code);

  if (k_sm_i != 0) {
    printf("Is key in single key map.\n");
  }
  if (k_sm_i && k_sm_i->on_hold) {
    printf("Is janus key.\n");
  }
  if (m_sm_i != 0) {
    printf("Is mod in single key map.\n");
  }
  if (k_cm_i != 0) {
    printf("Is key in combo key map.\n");
    printf("%d\n", k_cm_i->key_to);
  }
  if (m_cm_i != 0) {
    printf("Is mod in combo key map.\n");
  }

  if (k_cm_i) { // ## NON-MOD KEY PRESS PRESENT IN ONE OR MORE COMBO MAP
    printf("We are in the k_cm_i block.\n");

    // TODO: test is_key_in_uniquely_active_combo_map
    if (is_key_in_uniquely_active_combo_map(ev.code)) {
      printf("Handling key of one or more combo maps one of which is currently uniquely active.\n");
      // If the mod_from of only one map is down/held (map is ``active''),
      //
      // Do what we do in 06 + change wrt primary function.
      //
      if (ev.value == 1) {
        printf("<{([*])}>===> send mod_from (0).\n");

        printf("<{([*])}>===> send mod_to (1), if any.\n");

        printf("<{([*])}>===> send key_to (1).\n");
      } else if (ev.value == 2) {

        printf("<{([*])}>===> send key_to (2).\n");

      } else if (ev.value == 0) {
        printf("<{([*])}>===> send key_to (0).\n");

        printf("<{([*])}>===> send mod_to (0), if any.\n");

        printf("<{([*])}>===> send mod_from (1).\n");
      }
    } else {
      // Else
      //
      // Treat the key as a MOD/NON-MOD NO-MAP KEY PRESS
      //
      printf("Handling key of one or more combo maps none of which is currently active.\n");
      printf("<{([*])}>===> SEND PRIMARY FUNCTION OF KEY RECEIVED WITH VALUE RECEIVED\n");
    }
  } else if (m_cm_i) { // ## MOD KEY PRESS PRESENT IN ONE OR MORE COMBO MAP
    printf("We are in the m_cm_i block.\n");

    // TODO: replace the 1
    if (1) {
      // If the key_from of only one map is down/held (map is
      // ``active''),
      //
      // Do what we do in 06 + change wrt primary function
      //
      if (ev.value == 1) {
        //send_key_ev_and_sync(uidev, map_of_key->mod_from, 0);
        printf("<{([*])}>===> send mod_from (0)\n");
        if (m_cm_i->mod_to) printf("<{([*])}>===> send mod_to (1)\n");
        printf("<{([*])}>===> send key_to (1)\n");
      } else if (ev.value == 2) {
        printf("<{([*])}>===> send key_to (2)\n");
      } else if (ev.value == 0) {
        printf("<{([*])}>===> send key_to (0)\n");
        if (m_cm_i->mod_to) printf("<{([*])}>===> send mod_to (0)\n");
        printf("<{([*])}>===> send mod_from (1)\n");
      }
    } else {
      // Else,
      //
      // Treat the key as a MOD/NON-MOD NO-MAP KEY PRESS
      //
      printf("Mod of one or more combo maps none of which is currently active.\n");
      printf("<{([*])}>===> SEND PRIMARY FUNCTION OF MOD RECEIVED WITH VALUE RECEIVED\n");
    }
  } else { // ## MOD/NON-MOD NO-MAP KEY PRESS

  }
}

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

int
main(int argc, char **argv)
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
