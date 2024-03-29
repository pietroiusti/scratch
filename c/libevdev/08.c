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
    - (l/r)alt-b -> ctrl+left .)

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
#include <stdint.h>
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

// Keyboard key states lookup table.
//
// Index n holds the value (1, 2 or 0) of the key whose code is n in
// /usr/include/linux/input-event-codes.h
//
// I'm including up to 248. Should be enough.
int keyboard[249];

// 0 is the index of the default window map (in the window_maps array)
// which represents the set of those key_maps which are valid in any
// window, unless overruled by a specific window map.
volatile unsigned int currently_focused_window = 0;

struct libevdev_uinput *uidev;

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

key_map **selected_key_maps;
unsigned int selected_key_maps_size;
unsigned int key_maps_of_default_window_map_are_set = 0;

window_map default_map = {
  "Default",
  12,
  {
    //mod_from       key_from      mod_to         key_to
    { 0,             KEY_CAPSLOCK, 0,             KEY_ESC,       },
    { 0,             KEY_ENTER,    0,             KEY_ESC,       },
    { KEY_RIGHTCTRL, KEY_ESC,      0,             KEY_RIGHT,     },
    { 0,             KEY_ESC,      0,             KEY_CAPSLOCK,  },
    { 0,             KEY_W,        0,             KEY_1,         },
    { KEY_RIGHTALT,  KEY_F,        KEY_RIGHTCTRL, KEY_RIGHT,     },
    { KEY_RIGHTCTRL, 0,            KEY_RIGHTALT,  0,             },
    { 0,             KEY_L,        KEY_RIGHTCTRL, 0              },
    { KEY_LEFTCTRL,  0,            KEY_RIGHTALT,  0,             },
    { KEY_RIGHTCTRL, KEY_F,        0,             KEY_RIGHT,     },
    //{ KEY_SYSRQ,     0,            KEY_RIGHTALT,  0,             },
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

window_map foo_map = {
  "this-is-just-for-testing",
  6,
  { // Just some random stuff for tests
    { KEY_RIGHTALT,  KEY_F,        KEY_RIGHTCTRL, KEY_LEFT, },
    { KEY_RIGHTCTRL, KEY_G,        0,             KEY_ESC,  },
    { 0,             KEY_ESC,      0,             KEY_F,    },
    { 0,             KEY_ENTER,    0,             KEY_F,    },
    { 0,             KEY_A,        0,             KEY_RIGHTCTRL, },
    { 0,             KEY_Q,        0,             KEY_F,         },
  }
};

// Represents the current state of the physical keyboard's keys. A key
// can be in either 1, or 2, or 0 state (value).
/* typedef struct { */
/*   unsigned int code; // Code of the key */
/*   int value;         // Status of key (either 1, or 2, or 0) */
/*   /\* struct timespec last_time_down; // Last time value was set to 11 *\/ */
/* } keyboard_key_state2; */

// Represents the current state of the physical keyboard's keys. A key
// can be in either 1, or 2, or 0 state (value).
//
// IMPORTANT: each key used in the config must also appear in this
// array.
/* keyboard_key_state2 keyboard2[] = { */
/*   { KEY_LEFTCTRL, 0 }, // 29 */
/*   { KEY_RIGHTCTRL, 0 }, // 97 */
/*   { KEY_LEFTALT, 0 }, // 56 */
/*   { KEY_RIGHTALT, 0 }, // 100 */
/*   { KEY_CAPSLOCK, 0 }, // 58 */
/*   { KEY_ENTER, 0 }, // 28 */
/*   { KEY_RIGHT, 0 }, // 106 */
/*   { KEY_SYSRQ, 0 }, // 99 */
/*   { KEY_ESC, 0 }, // 1 */
/*   { KEY_P, 0 }, // 25 */
/*   { KEY_F, 0 }, // 33 */
/*   { KEY_B, 0 }, // 48 */
/*   { KEY_N, 0 }, // 49 */
/*   { KEY_V, 0 }, // 47 */
/*   { KEY_A, 0 }, // 30 */
/*   { KEY_E, 0 }, // 18 */
/*   { KEY_W, 0 }, // 17 */
/*   { KEY_G, 0 }, // 34 */
/*   { KEY_Q, 0 }, // 16 */
/*   { KEY_L, 0}, // 38 */
/* }; */

window_map* window_maps[] = {
  &default_map,
  &brave_map,
  &foo_map,
};

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
  keyboard[ev.code] = ev.value;
}

unsigned is_physically_down(int code) {
  // 1 and 2 means down, 0 means up. so we can just return that value.
  return keyboard[code];
}

/* void set_keyboard2_state(struct input_event ev) { */
/*   for (int i = 0; i < sizeof(keyboard2)/sizeof(keyboard_key_state2); i++) { */
/*     if (keyboard2[i].code == ev.code) { */
/*       keyboard2[i].value = ev.value; */
/*       /\* if (ev.value == 1) *\/ */
/*       /\*   clock_gettime(CLOCK_MONOTONIC, &keyboard2[i].last_time_down); *\/ */
/*     } */
/*   } */
/* } */

static void set_selected_key_maps() {
  unsigned int i = currently_focused_window;

  if (!key_maps_of_default_window_map_are_set) { // we want to do this only once
      for (size_t j = 0; j < window_maps[0]->size; j++) {
        selected_key_maps[j] = &window_maps[0]->key_maps[j];
      }
      key_maps_of_default_window_map_are_set = 1;
  }

  if (i != 0) {
    for (size_t j = 0; j < window_maps[i]->size; j++) {
      selected_key_maps[window_maps[0]->size+j] = &window_maps[i]->key_maps[j];
    }
  }

  selected_key_maps_size = i == 0
                           ? window_maps[0]->size
                           : window_maps[0]->size + window_maps[i]->size;
}

// Return primary function of code
//
// loop backwards
unsigned first_fun(unsigned code) {
  for (size_t i = selected_key_maps_size-1; i != SIZE_MAX; i--) {
    if ((selected_key_maps[i]->key_from == code
         &&
         !selected_key_maps[i]->mod_from)
        ||
        (selected_key_maps[i]->mod_from == code
         &&
         !selected_key_maps[i]->key_from))
      {
        if (selected_key_maps[i]->key_to) {
          return selected_key_maps[i]->key_to;
        } else if (selected_key_maps[i]->mod_to) {
          return selected_key_maps[i]->mod_to;
        } else {
          fprintf(stderr, "first_fun: Error. There is neither a key_to nor a mod_to\n");
        }
      }
  }

  return code;
};

unsigned is_logically_down_first(unsigned code) {
  // A key is considered logically down, if there is a single key
  // mapped to it which is physically down.

  // I'm gonna assume, at least for now, that one can define a single
  // map in four ways.

  // It makes sense to bind a single key to another single key only in
  // one key_map per window_map. Such a responsibility is on the user.
  //
  // Given so there can be max 2 relevant key_maps if currently_focused_window != 0
  // and there can be max 1 relevant key_map if currently_focused_window == 0.
  //
  // If there are two key_maps, then the one in the non-default window
  // map takes priority.
  //
  // So given the way selected_key_maps are arranged (default window
  // map first) we can just loop over them and the last match, if any,
  // is the right one.

  key_map* found = 0;

  for (size_t i = 0; i < selected_key_maps_size; i++) {
    if (selected_key_maps[i]->key_to == code
        && selected_key_maps[i]->key_from
        && !selected_key_maps[i]->mod_from)
      found = selected_key_maps[i];

    if (selected_key_maps[i]->key_to == code
        && !selected_key_maps[i]->key_from
        && selected_key_maps[i]->mod_from)
      found = selected_key_maps[i];

    if (selected_key_maps[i]->mod_to == code
        && !selected_key_maps[i]->key_from
        && selected_key_maps[i]->mod_from)
      found = selected_key_maps[i];

    if (selected_key_maps[i]->mod_to == code
        && selected_key_maps[i]->key_from
        && !selected_key_maps[i]->mod_from)
      found = selected_key_maps[i];
  }

  if (found) {
    unsigned k = 0;
    if ((k = found->key_from)) return is_physically_down(k);
    else if ((k = found->mod_from)) return is_physically_down(k);
    else {printf("Error: is_logically_down_second\n"); return 0;}
  } else {
    return is_physically_down(code);
  }
}

unsigned is_logically_down_2(unsigned code) {
  // I think: a code is logically down if there is at least one key
  // whose first fun is code is physically down.


  if (first_fun(code) == code) {
    if (is_physically_down(code)) {
      return code;
    }
  }

  for (size_t i = 0; i < selected_key_maps_size; i++) {
    if (selected_key_maps[i]->key_to == code
        && selected_key_maps[i]->key_from
        && !selected_key_maps[i]->mod_from
        && is_physically_down(selected_key_maps[i]->key_from))
      return selected_key_maps[i]->key_from;

    if (selected_key_maps[i]->key_to == code
        && !selected_key_maps[i]->key_from
        && selected_key_maps[i]->mod_from
        && is_physically_down(selected_key_maps[i]->mod_from))
      return selected_key_maps[i]->mod_from;

    if (selected_key_maps[i]->mod_to == code
        && !selected_key_maps[i]->key_from
        && selected_key_maps[i]->mod_from
        && is_physically_down(selected_key_maps[i]->mod_from))
      return selected_key_maps[i]->mod_from;

    if (selected_key_maps[i]->mod_to == code
        && selected_key_maps[i]->key_from
        && !selected_key_maps[i]->mod_from
        && selected_key_maps[i]->key_from)
      return selected_key_maps[i]->key_from;
  }

  return 0;
}

unsigned is_in_array(unsigned *arr, unsigned size, unsigned code) {
  for (size_t i = 0; i < size; i++)
    if (arr[i] == code)
      return 1;
  return 0;
}


// 3rd sketch
// looping backward seems the right thing to do
// TODO: test with non-default window map
unsigned is_logically_down(unsigned code) {

  if (first_fun(code) == code) {
    if (is_physically_down(code)) {
      return code;
    }
  }

  for (size_t i = selected_key_maps_size-1; i != SIZE_MAX; i--) {
    if (selected_key_maps[i]->key_to == code
        && selected_key_maps[i]->key_from
        && !selected_key_maps[i]->mod_from
        && is_physically_down(selected_key_maps[i]->key_from))
      return selected_key_maps[i]->key_from;

    if (selected_key_maps[i]->key_to == code
        && !selected_key_maps[i]->key_from
        && selected_key_maps[i]->mod_from
        && is_physically_down(selected_key_maps[i]->mod_from))
      return selected_key_maps[i]->mod_from;

    if (selected_key_maps[i]->mod_to == code
        && !selected_key_maps[i]->key_from
        && selected_key_maps[i]->mod_from
        && is_physically_down(selected_key_maps[i]->mod_from))
      return selected_key_maps[i]->mod_from;

    if (selected_key_maps[i]->mod_to == code
        && selected_key_maps[i]->key_from
        && !selected_key_maps[i]->mod_from
        && is_physically_down(selected_key_maps[i]->key_from))
      return selected_key_maps[i]->key_from;
  }

  return 0;
}

unsigned is_key_from_in_more_than_one_selected_keymap_with_mod_from_logically_down(unsigned code) {
  key_map* lastFound;
  unsigned found = 0;

  for (size_t i = 0; i < selected_key_maps_size; i++) {
    if (selected_key_maps[i]->key_from == code
        && selected_key_maps[i]->mod_from
        && is_logically_down(selected_key_maps[i]->mod_from)) {
      if (found != 0) {
        if (lastFound->mod_from != selected_key_maps[i]->mod_from) {
          found++;
        }
      } else {
        found++;
      }
      lastFound = selected_key_maps[i];
    }
  }

  if (found > 1)
    return 1;
  else
    return 0;
}

// nokild: no-other-key-is-logically-down (besides first fun of
// mod_from and first fun of key_from)
unsigned nokild(unsigned mod_from, unsigned key_from) {

  for (size_t i = 0; i < 249; i++) {
    if (keyboard[i]) {
      if (first_fun(i) != mod_from
          && first_fun(i) != key_from) {
        return 0;
      }
    }
  }

  // At the moment if there are more than one key down which bound to
  // key_from, this function return true. We might want to change
  // that, even though it doesn't seem necessary... when do you do
  // that?!

  return 1;
}

// First sketch. here just for historical/development reasons.
// Return (pointer to) uniquely active map where key is key_from, if
// any; otherwise 0.
static key_map* is_key_in_uniquely_active_combo_map_sketch(unsigned code) {
  // (given comments in 07_b.c and more thought) logic:

  // +-------------------------------------------------------------------------------------------------------------------------------------+
  // |1st fun of key is key_from in more than one key map in default window map where mod_from is != 0 and logically down --> return 0     |
  // |                                                                                                                                     |
  // |                                                                                                                                     |
  // |1st fun of key is key_from in more than one key map in non-default window map where mod_from is != 0 and logically down --> return 0 |
  // |                                                                                                                                     |
  // |                                                                                                                                     |
  // |1st fun of key is key_from in one key map in default window map where mod_from is != 0 and logically down             |              |
  // |and                                                                                                                   |--> return 0  |
  // |1st fun of key is key_from in a different key map in non-default window map where mod_from is != 0 and logically down |              |
  // +-------------------------------------------------------------------------------------------------------------------------------------+
    // these first three rules boils down to :
    // if 1st fun key is key_from in more than one key map where mod_from is != 0 and logically down --> return 0.
    if (is_key_from_in_more_than_one_selected_keymap_with_mod_from_logically_down(code))
      printf("is_key_from_in_more_than_one_selected_keymap_with_mod_from_logically_down");

  // 1st fun key is key_from in one key map in default window map where mod_from != 0 and logically is down          |
  // and                                                                                                             |--> return non-default key map if nokild*
  // 1st fun key is key_from in the same key map in non-default window map where mod_from is != 0 and logically down |


  // key is key_from in one key map in default window map where mod_from is != 0 and logically down          |
  // and                                                                                                     |--> return that default map if nokild*
  // key is not key_from in one key map in non-default window map where mod_from is != 0 and logically down  |


  // key is not key_from in one key map in default window map where mod_from is != 0 and logically down  |
  // and                                                                                                 |--> return that non-default map if nokild*
  // key is key_from in one key map in non-default window map where mod_from is != 0 and logically down  |


  // *nokild: no other key** is /logically/ down (besides key_from and mod_from)

  // ** def(`no other key`) = ...?
  // It's fine if there are only two keys down: `key` and a key which is logically mod_from.

  return 0;
}

// Return (pointer to) ``uniquely active map'' where key is key_from,
// if any; otherwise 0.
static key_map* is_key_in_uniquely_active_combo_map(unsigned code) {
  // if 1st fun of key is key_from in one key map where mod_from is
  // !=0 and logically down (which can be both in the default window
  // map and in the non-default window map) and nokild,
  //
  // then return that map (giving priority to key map in non-default
  // window map, if any)
  //
  // otherwise, return 0;


  // let's loop backwards so we just take the first match if any
  // (because the non-default window map, whose key maps have
  // precedence, comes later, if present)
  for(size_t i = selected_key_maps_size-1; i != SIZE_MAX; i--) {

    if (selected_key_maps[i]->key_from == first_fun(code)) {

      if (selected_key_maps[i]->mod_from) {

        if (is_logically_down(selected_key_maps[i]->mod_from)) {

          if (nokild(selected_key_maps[i]->mod_from, code)) {
            return selected_key_maps[i];
          } else {
            return 0; // if we are here there can't be any other
            // relevant combo map, so return 0. (we are only dealing with
            // combo maps of two keys for now)
          }

        }

      }

    }

  }

  return 0;
}

// analogously to is_key_in_uniquely_active_combo_map
static key_map* is_mod_in_uniquely_active_combo_map(unsigned code) {

  for (size_t i = selected_key_maps_size-1; i != SIZE_MAX; i--) {

    if (selected_key_maps[i]->mod_from == first_fun(code)) {

      if (selected_key_maps[i]->key_from) {

        if (is_logically_down(selected_key_maps[i]->key_from)) {

          if (nokild(code, selected_key_maps[i]->key_from)) {
            return selected_key_maps[i];
          } else {
            return 0;
          }

        }

      }

    }

  }

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

void handle_key(struct input_event ev) {
  printf("%i (%i)\n", ev.code, ev.value);


  // Update keyboard state
  set_keyboard_state(ev);

  // Update keyboard2 state
  // set_keyboard2_state(ev);

  set_selected_key_maps();
  // Should the setting of the key_maps be performed by the track_window fun?




  printf("Primary fun: %d\n", first_fun(ev.code));




  /* if (is_physically_down(KEY_RIGHTCTRL)) { */
  /*   printf("KEY_RIGHTCTRL is physically down!\n"); */
  /* } else { */
  /*   printf("KEY_RIGHTCTRL is NOT physically down!\n"); */
  /* } */
  /* if (is_logically_down(KEY_RIGHTCTRL)) { */
  /*   printf("KEY_RIGHTCTRL is logically down!\n"); */
  /* } else { */
  /*   printf("KEY_RIGHTCTRL is NOT logically down!\n"); */
  /* } */

  if (is_physically_down(KEY_RIGHTALT)) {
    printf("KEY_RIGHTALT is physically down!\n");
  } else {
    printf("KEY_RIGHTALT is NOT physically down!\n");
  }
  if (is_logically_down(KEY_RIGHTALT)) {
    printf("KEY_RIGHTALT is logically down!\n");
  } else {
    printf("KEY_RIGHTALT is NOT logically down!\n");
  }
  if (nokild(KEY_RIGHTALT, KEY_F)) {
    printf("nokild(KEY_RIGHTALT, KEY_F)\n");
  } else {
    printf("NOT nokild(KEY_RIGHTALT, KEY_F)\n");
  }
  /* printf("There are %d selected key_maps\n", selected_key_maps_size); */
  /* for (size_t i = 0; i < selected_key_maps_size; i++) */
  /*   printf("KEY MAP: mod_from %d, key_from %d, mod_to %d, key_to %d\n", */
  /*          selected_key_maps[i]->mod_from, */
  /*          selected_key_maps[i]->key_from, */
  /*          selected_key_maps[i]->mod_to, */
  /*          selected_key_maps[i]->key_to); */



  // ######
  key_map* uniquely_active_combo_map_of_key = is_key_in_uniquely_active_combo_map(ev.code);
  if (uniquely_active_combo_map_of_key) {
    printf("IS_KEY_IN_UNIQUELY_ACTIVE_COMBO_MAP\n");
    if (ev.value == 1) {

      if (is_logically_down(uniquely_active_combo_map_of_key->mod_from)) { // mod_from 1|2
        if (uniquely_active_combo_map_of_key->mod_to) {
          send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_key->mod_to, 1);
        }
        send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_key->key_to, 1);
      }

    } else if (ev.value == 2) {

      if (is_logically_down(uniquely_active_combo_map_of_key->mod_from)) { // mod_from 1|2
        send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_key->key_to, 1);
      }

    } else {

      if (is_logically_down(uniquely_active_combo_map_of_key->mod_from)) { // mod_from 1|2
        send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_key->key_to, 0);
        if (uniquely_active_combo_map_of_key->mod_to) {
          send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_key->mod_to, 0);
        }
        send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_key->mod_from, 0);
      }

    }
  }

  // ######
  key_map* uniquely_active_combo_map_of_mod = is_mod_in_uniquely_active_combo_map(ev.code);
  if (uniquely_active_combo_map_of_mod) {
    printf("IS_MOD_IN_UNIQUELY_ACTIVE_COMBO_MAP\n");
    if (ev.value == 1) {

      if (is_logically_down(uniquely_active_combo_map_of_mod->key_from)) { // key_from 1|2
        send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_mod->mod_from, 0);
        send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_mod->key_from, 0);
        if (uniquely_active_combo_map_of_mod->mod_to) {
          send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_mod->mod_to, 0);
        }
        send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_mod->key_to, 1);
      }

    } else if (ev.value == 2) {

      printf("The alleged impossible is happening\n");

    } else {

      if (is_logically_down(uniquely_active_combo_map_of_mod->key_from)) { // key_from 1|2
        send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_mod->mod_from, 0);
        if (uniquely_active_combo_map_of_mod->mod_to) {
          send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_mod->mod_to, 0);
        }
        send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_mod->key_to, 0);
        send_key_ev_and_sync(uidev, uniquely_active_combo_map_of_mod->key_from, 1);
      }

    }
  }

  // ######
  // key/mod of non-uniquely-active map
  send_key_ev_and_sync(uidev, first_fun(ev.code), ev.value);
}



unsigned int compute_max_size_of_selected_key_maps() {
  unsigned int number_of_all_window_maps = sizeof(window_maps) / sizeof(window_maps[0]);

  if (number_of_all_window_maps == 1) // there is only the default window map
    return window_maps[0]->size;

  unsigned int size_of_biggest_non_default_w_map = 0;

  for (size_t i = 1; i < number_of_all_window_maps; i++) {
    if (window_maps[i]->size > size_of_biggest_non_default_w_map) {
      size_of_biggest_non_default_w_map = window_maps[i]->size;
    }
  }

  return window_maps[0]->size + size_of_biggest_non_default_w_map;
}

int main(int argc, char **argv)
{
  // Set initial keyboard state
  memset(keyboard, 0, sizeof(keyboard));

  // Start tracking windows
  pthread_t xthread;
  int thread_return_value;
  thread_return_value = pthread_create(&xthread, NULL, track_window, NULL);

  // Allocate space for holding active key_maps (those key_maps which
  // are in place given the currently selected window)
  unsigned int max_size_of_selected_key_maps = compute_max_size_of_selected_key_maps();
  printf("size_of_selected_key_maps: %d\n", max_size_of_selected_key_maps);
  selected_key_maps = malloc(max_size_of_selected_key_maps * sizeof(key_map*));

  // Do libevdev stuff and call handle key at each key event
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
