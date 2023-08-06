/*

  In progress: this programs attemps to combine two sort of mapping I
  have already developed elsewhere separately:

  - 1 Map single keys like janus-key does (e.g.,

    CAPS -> ESC on tap, ALT on HOLD
    ESC -> CAPS on tap
    ENTER -> CTRL on hold )

  AND

  - 2 Map multiple combos to a single key or combo. (e.g,

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

  ###### ###### ###### ###### ###### ######

  Compile with:
  gcc -g `pkg-config --cflags libevdev` `pkg-config --libs libevdev x11` ./07.c `pkg-config --libs libevdev` -pthread -o 07

  ###### ###### ###### ###### ###### ######

  We could use a the same struct to map both single keys and
  combinations of two key:

    input     ---->    on tap           on hold
      |                  |                 |
   _______           ______________     _________
  |       |         |              |   |         |

  0, CAPS     ---->   0, ESC,               LALT
  0, ENTER    ---->   0, 0 (ENTER),         RCTRL
  RCTRL, ESC  ---->   0, RIGHT,             0
  0, ESC      ---->   0, CAPS,              0
  RALT, F     ---->   RCTRL, RIGHT,         0
  RCTRL, F    ---->   0, RIGHT,             0
  RCTRL, 0    ---->   RALT, 0,              0
  RMETA, 0    ---->  RALT, 0,               0

  According to the above, the keys appears in single maps (sm) and
  combination maps (cm) as follows:

  |-------+----------------+-----------+-----------|
  | KEY   | Single Key Map | Combo Map | Janus Key |
  |-------+----------------+-----------+-----------|
  | E     | n              | n         | n         |
  |-------+----------------+-----------+-----------|
  | CAPS  | y              | n         | y         |
  |-------+----------------+-----------+-----------|
  | ENTER | y              | n         | y         |
  |-------+----------------+-----------+-----------|
  | F     | n              | y         | n         |
  |-------+----------------+-----------+-----------|
  | ESC   | y              | y         | n         |
  |-------+----------------+-----------+-----------|

  |-------+----------------+-----------+-----------|
  | MOD   | Single Key Map | Combo Map | Janus key |
  |-------+----------------+-----------+-----------|
  | SHIFT | n              | n         | n         |
  |-------+----------------+-----------+-----------|
  | RMETA | y              | n         | n         |
  |-------+----------------+-----------+-----------|
  | RALT  | n              | y         | n         |
  |-------+----------------+-----------+-----------|
  | RCTRL | y              | y         | n         |
  |-------+----------------+-----------+-----------|

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

volatile int currently_focused_window;

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

mod_key mod_map[] = {
    // key                1st function     2nd function
    {  KEY_CAPSLOCK,      KEY_ESC,         KEY_LEFTALT     },
    {  KEY_ENTER,         0,               KEY_RIGHTALT    },
    {  KEY_RIGHTALT,      KEY_RIGHTCTRL },
    {  KEY_LEFTALT,       KEY_LEFTCTRL  },
    {  KEY_RIGHTCTRL,     KEY_RIGHTALT  },
    {  KEY_LEFTCTRL,      KEY_LEFTMETA  },
    {  KEY_LEFTMETA,      KEY_LEFTALT   },
    {  KEY_RIGHTMETA,     KEY_RIGHTALT  },
    {  KEY_COMPOSE,       KEY_RIGHTMETA },
    {  KEY_ESC,           KEY_CAPSLOCK  }
};

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

key_map default_window_map[] = {
  { 0,             KEY_CAPSLOCK, 0,             KEY_ESC,      KEY_LEFTALT   },
  { 0,             KEY_ENTER,    0,             0,            KEY_RIGHTCTRL },
  { KEY_RIGHTCTRL, KEY_ESC,      0,             KEY_RIGHT,    0             },
  { 0,             KEY_ESC,      0,             KEY_CAPSLOCK, 0             },
  { KEY_RIGHTALT,  KEY_F,        KEY_RIGHTCTRL, KEY_RIGHT,    0             },
  { KEY_RIGHTCTRL, 0,            KEY_RIGHTALT,  0,            0             },
  { KEY_RIGHTCTRL, KEY_F,        0,             KEY_RIGHT,    0             },
  { KEY_SYSRQ,     0,            KEY_RIGHTALT,  0,            0             },
};

typedef struct {
  char* class_name;
  unsigned int size;
  key_map maps[];
} window_map;

window_map default_map = {
  "Default",
  8,
  {
    { 0,             KEY_CAPSLOCK, 0,             KEY_ESC,      KEY_LEFTALT   },
    { 0,             KEY_ENTER,    0,             0,            KEY_RIGHTCTRL },
    { KEY_RIGHTCTRL, KEY_ESC,      0,             KEY_RIGHT,    0             },
    { 0,             KEY_ESC,      0,             KEY_CAPSLOCK, 0             },
    { KEY_RIGHTALT,  KEY_F,        KEY_RIGHTCTRL, KEY_RIGHT,    0             },
    { KEY_RIGHTCTRL, 0,            KEY_RIGHTALT,  0,            0             },
    { KEY_RIGHTCTRL, KEY_F,        0,             KEY_RIGHT,    0             },
    { KEY_SYSRQ,     0,            KEY_RIGHTALT,  0,            0             },
  }
};

window_map brave_map = {
  "Brave-browser",
  2,
  { // just some random stuff for tests
    { KEY_RIGHTALT,  KEY_F,        KEY_RIGHTCTRL, KEY_LEFT,    0             },
    { 0,             KEY_ESC,      0,             KEY_F,        0             },
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

int kb_state_of2(unsigned int k_code) {
  for (int i = 0; i < sizeof(keyboard2)/sizeof(keyboard_key_state2); i++) {
    if (keyboard2[i].code == k_code)
      return keyboard2[i].value;
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


// If `key` is in the mod_map, then return its index. Otherwise return
// -1.
static int is_in_mod_map(unsigned int key) {
    size_t length = sizeof(mod_map)/sizeof(mod_map[0]);
    for (int i = 0; i < length; i++) {
        if (mod_map[i].key == key)
            return i;
    }
    return -1;
};

// If `key` is a janus key, then return its index. Otherwise return
// -1.
static int is_janus(unsigned int key) {
    int i = is_in_mod_map(key);
    if (i >= 0)
        if (mod_map[i].secondary_function > 0)
            return i;
    return -1;
}

void handle_key_merge(struct input_event ev) {
  set_keyboard_state(ev);

  printf("handling %d, %d\n", ev.code, ev.value);

  map* map_of_key = get_active_map_of_key(ev);
  if (map_of_key)
    printf("map_of_key is truthy\n");

  map* map_of_mod = get_active_map_of_mod(ev);
  if (map_of_mod)
    printf("map_of_mod is truthy\n");

  if (is_janus(ev.code)) {

  } else {

  }

  if (map_of_key) {
    printf("we are in map_of_key block\n");
    if (ev.value == 1) {
      printf("we are in ev.value == 1 block\n");
      if (kb_state_of(map_of_key->mod_from) == 1) {
	send_key_ev_and_sync(uidev, map_of_key->mod_from, 0);

        // # = IN PROGRESS: considering cases with mod_to.
	if (map_of_key->mod_to)	  send_key_ev_and_sync(uidev, map_of_key->mod_to, 1);

        send_key_ev_and_sync(uidev, map_of_key->key_to, 1);
      } else if (kb_state_of(map_of_key->mod_from) == 2) {
        send_key_ev_and_sync(uidev, map_of_key->mod_from, 0);

        // # = IN PROGRESS: considering cases with mod_to.
	if (map_of_key->mod_to) {
	    send_key_ev_and_sync(uidev, map_of_key->mod_to, 1);
	}

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

        // # = IN PROGRESS: considering cases with mod_to.
	if (map_of_key->mod_to)
	  send_key_ev_and_sync(uidev, map_of_key->mod_to, 0);

        send_key_ev_and_sync(uidev, map_of_key->mod_from, 1);
      } else if (kb_state_of(map_of_key->mod_from) == 2) {
        send_key_ev_and_sync(uidev, map_of_key->key_to, 0);

        // # = IN PROGRESS: considering cases with mod_to.
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

	// # = IN PROGRESS: considering cases with mod_to.
	if (map_of_mod->mod_to)
	  send_key_ev_and_sync(uidev, map_of_mod->mod_to, 1);

        send_key_ev_and_sync(uidev, map_of_mod->key_to, 1);
      } else if (kb_state_of(map_of_mod->key_from) == 2) {
        printf("we are in kb_state_of(map_of_mod->key_from) == 2\n");
	send_key_ev_and_sync(uidev, map_of_mod->mod_from, 0);
        send_key_ev_and_sync(uidev, map_of_mod->key_from, 0);

	// # = IN PROGRESS: considering cases with mod_to.
	if (map_of_mod->mod_to)
	  send_key_ev_and_sync(uidev, map_of_mod->mod_to, 1);

        send_key_ev_and_sync(uidev, map_of_mod->key_to, 1);
      } else if (kb_state_of(map_of_mod->key_from) == 0) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
      }
    } else if (ev.value == 2) {
      printf("we are in ev.value == 2  block\n");
      if (kb_state_of(map_of_mod->key_from) == 1)  {
        printf("the alleged impossible is happening");
      } else if (kb_state_of(map_of_mod->key_from) == 2) {
        printf("the alleged impossible is happening");
      } else if (kb_state_of(map_of_mod->key_from) == 0) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
      }
    } else if (ev.value == 0) {
      printf("we are in ev.value == 0  block\n");
      if (kb_state_of(map_of_mod->key_from) == 1)  {
        send_key_ev_and_sync(uidev, ev.code, ev.value);

	// # = IN PROGRESS: considering cases with mod_to.
	if (map_of_mod->mod_to)
	  send_key_ev_and_sync(uidev, map_of_mod->mod_to, 0);

        send_key_ev_and_sync(uidev, map_of_mod->key_to, 0);
        send_key_ev_and_sync(uidev, map_of_mod->key_from, 1);
      } else if (kb_state_of(map_of_mod->key_from) == 2) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);

	// # = IN PROGRESS: considering cases with mod_to.
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

// If `key` is in a single key/mod map in maps2, then return index of
// map. Otherwise return -1.
static int is_key_in_single_map(int key) {
  size_t length = sizeof(default_window_map)/sizeof(default_window_map[0]);

  for (size_t i = 0; i< length; i++)
    if (default_window_map[i].key_from == key && default_window_map[i].mod_from == 0)
      return i;

  return -1;
}

static key_map* is_key_in_single_map2(int key) {
  int i = currently_focused_window;

  if (i == 0) { // only default map
    size_t length = window_maps[i]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[0]->maps[j].key_from == key && window_maps[0]->maps[j].mod_from == 0)
        return &window_maps[0]->maps[j];

  } else { // // non-default map + default map

    // First search in the specific map
    size_t length = window_maps[i]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[i]->maps[j].key_from == key && window_maps[i]->maps[j].mod_from == 0)
        return &window_maps[i]->maps[j];

    // If we haven't found it and returned, then let's look in the default map
    length = window_maps[i]->size;
    for (size_t j = 0; j < length; j++)
      if (window_maps[i]->maps[j].key_from == key && window_maps[i]->maps[j].mod_from == 0)
        return &window_maps[i]->maps[j];
  }

  return 0;
}

// If `mod` is in a single key/mod map in maps2, then return index of
// map. Otherwise return -1.
static int is_mod_in_single_map(int mod) {
  size_t length = sizeof(default_window_map)/sizeof(default_window_map[0]);

  for (size_t i = 0; i< length; i++)
    if (mod == default_window_map[i].mod_from && default_window_map[i].key_from == 0)
      return i;

  return -1;
}

// If `key` is in a combo map in maps2, then return index of
// map. Otherwise return -1.
static int is_key_in_combo_map(int key) {
  size_t length = sizeof(default_window_map)/sizeof(default_window_map[0]);

  for (size_t i = 0; i< length; i++)
    if (key == default_window_map[i].key_from && default_window_map[i].mod_from != 0)
      return i;

  return -1;
}

// If `mod` is in a combo map in maps2, then return index of
// map. Otherwise return -1.
static int is_mod_in_combo_map(int mod) {
  size_t length = sizeof(default_window_map)/sizeof(default_window_map[0]);

  for (size_t i = 0; i< length; i++)
    if (mod == default_window_map[i].mod_from && default_window_map[i].key_from != 0)
      return i;

  return -1;
}

// If any of the janus keys is down or held return the index of the
// first one of them in the mod_map. Otherwise, return -1.
static int some_jk_are_down_or_held() {
  size_t l = sizeof(default_window_map)/sizeof(default_window_map[0]);

  for (int i = 0; i < l; i++) {
    if (default_window_map[i].on_hold != 0) {// we found a map of a janus key

      // only one among the key and the mod is non-zero
      int non_zero = default_window_map[i].key_from ? default_window_map[i].key_from : default_window_map[i].mod_from;
      if (kb_state_of2(non_zero)) {
        return i;
      }

    }
  }

  return -1;
}

void handle_key2(struct input_event ev) {
  printf("((((( handling %d, %d )))))\n\n", ev.code, ev.value);

  // Update keyboard state
  set_keyboard_state2(ev);

  //print_keyboard2();

  printf("The currently focused window id is %d\n", currently_focused_window);

  int k_sm_i = is_key_in_single_map(ev.code);
  key_map* k_sm_i2 = is_key_in_single_map2(ev.code);

  int second_f = 0;
  if (k_sm_i && default_window_map[k_sm_i].on_hold) second_f = k_sm_i;
  int m_sm_i = is_mod_in_single_map(ev.code);
  int k_cm_i = is_key_in_combo_map(ev.code);
  int m_cm_i = is_mod_in_combo_map(ev.code);

  if (k_sm_i != -1) {
    printf("Is key in single key map.\n");
  }
  if (k_sm_i2 != 0) {
    printf("Is key in single key map. 2\n");
    printf("key to: %d\n", k_sm_i2->mod_to);
    printf("key to: %d\n", k_sm_i2->key_to);
  }
  if (default_window_map[k_sm_i].on_hold) {
    printf("Is janus key.\n");
  }
  if (m_sm_i != -1) {
    printf("Is mod in single key map.\n");
  }
  if (k_cm_i != -1) {
    printf("Is key in combo key map.\n");
  }
  if (m_cm_i != -1) {
    printf("Is mod in combo key map.\n");
  }

  if (k_sm_i == -1 &&
      m_sm_i == -1 &&
      k_cm_i == -1 &&
      m_cm_i == -1)
    {
      printf("'NORMAL' KEY\n");

      if (ev.value == 1 ) {
        if (some_jk_are_down_or_held() >= 0) {
          printf("last_input_was_special_combination = 1\n");
          printf("send down or held jks 2nd function 1\n");
          printf("send key 1\n");
        } else {
          printf("last_input_was_special_combination = 0\n");
          printf("send key 1\n");
        }
      } else if (ev.value == 2) { // same as in ev.code == 1
        if (some_jk_are_down_or_held() >= 0) {
          printf("last_input_was_special_combination = 1\n");
          printf("send down or held jks 2nd function 1\n");
          printf("send key 1\n");
        } else {
          printf("last_input_was_special_combination = 0\n");
          printf("send key 1\n");
        }
      } else { // ev.value == 0
          printf("send key 0\n");
      }
    }
}

void handle_key(struct input_event ev) {
  set_keyboard_state(ev);

  printf("handling %d, %d\n", ev.code, ev.value);

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

        // # = IN PROGRESS: considering cases with mod_to.
	if (map_of_key->mod_to)	  send_key_ev_and_sync(uidev, map_of_key->mod_to, 1);

        send_key_ev_and_sync(uidev, map_of_key->key_to, 1);
      } else if (kb_state_of(map_of_key->mod_from) == 2) {
        send_key_ev_and_sync(uidev, map_of_key->mod_from, 0);

        // # = IN PROGRESS: considering cases with mod_to.
	if (map_of_key->mod_to) {
	    send_key_ev_and_sync(uidev, map_of_key->mod_to, 1);
	}

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

        // # = IN PROGRESS: considering cases with mod_to.
	if (map_of_key->mod_to)
	  send_key_ev_and_sync(uidev, map_of_key->mod_to, 0);

        send_key_ev_and_sync(uidev, map_of_key->mod_from, 1);
      } else if (kb_state_of(map_of_key->mod_from) == 2) {
        send_key_ev_and_sync(uidev, map_of_key->key_to, 0);

        // # = IN PROGRESS: considering cases with mod_to.
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

	// # = IN PROGRESS: considering cases with mod_to.
	if (map_of_mod->mod_to)
	  send_key_ev_and_sync(uidev, map_of_mod->mod_to, 1);

        send_key_ev_and_sync(uidev, map_of_mod->key_to, 1);
      } else if (kb_state_of(map_of_mod->key_from) == 2) {
        printf("we are in kb_state_of(map_of_mod->key_from) == 2\n");
	send_key_ev_and_sync(uidev, map_of_mod->mod_from, 0);
        send_key_ev_and_sync(uidev, map_of_mod->key_from, 0);

	// # = IN PROGRESS: considering cases with mod_to.
	if (map_of_mod->mod_to)
	  send_key_ev_and_sync(uidev, map_of_mod->mod_to, 1);

        send_key_ev_and_sync(uidev, map_of_mod->key_to, 1);
      } else if (kb_state_of(map_of_mod->key_from) == 0) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
      }
    } else if (ev.value == 2) {
      printf("we are in ev.value == 2  block\n");
      if (kb_state_of(map_of_mod->key_from) == 1)  {
        printf("the alleged impossible is happening");
      } else if (kb_state_of(map_of_mod->key_from) == 2) {
        printf("the alleged impossible is happening");
      } else if (kb_state_of(map_of_mod->key_from) == 0) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);
      }
    } else if (ev.value == 0) {
      printf("we are in ev.value == 0  block\n");
      if (kb_state_of(map_of_mod->key_from) == 1)  {
        send_key_ev_and_sync(uidev, ev.code, ev.value);

	// # = IN PROGRESS: considering cases with mod_to.
	if (map_of_mod->mod_to)
	  send_key_ev_and_sync(uidev, map_of_mod->mod_to, 0);

        send_key_ev_and_sync(uidev, map_of_mod->key_to, 0);
        send_key_ev_and_sync(uidev, map_of_mod->key_from, 1);
      } else if (kb_state_of(map_of_mod->key_from) == 2) {
        send_key_ev_and_sync(uidev, ev.code, ev.value);

	// # = IN PROGRESS: considering cases with mod_to.
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

void set_currently_focused_window(char* name) {
  int currently_focused_window_next_value = 0;

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
        handle_key2(ev);
    }
  } while (rc == LIBEVDEV_READ_STATUS_SYNC || rc == LIBEVDEV_READ_STATUS_SUCCESS || rc == -EAGAIN);

  if (rc != LIBEVDEV_READ_STATUS_SUCCESS && rc != -EAGAIN)
    fprintf(stderr, "Failed to handle events: %s\n", strerror(-rc));

  rc = 0;
 out:
  libevdev_free(dev);

  return rc;
}
