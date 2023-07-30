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
  gcc -g `pkg-config --cflags libevdev` ./07.c `pkg-config --libs libevdev` -o 07

  ###### ###### ###### ###### ###### ######

  We could use a the same struct to map both single keys and
  combinations of two key:

    input     ---->    on tap           on hold
      |                  |                 |
   _______           ______________     _________   
  |       |         |              |   |         |

  0, CAPS     ---->   0, ESC,               ALT
  0, ENTER    ---->   0, 0 (ENTER),         CTRL
  CTRL, ESC   ---->   0, RIGHT,             0
  0, ESC      ---->   0, CAPS,              0
  CTRL, F     ---->   0, RIGHT,             0
  CTRL, 0     ---->   ALT, 0,               0
  META, 0     ---->   ALT, 0,               0

  According to the above, the keys appears in single maps (sm) and
  combination maps (cm) as follows:

  |-------------+----------------+-----------|
  | KEY         | Single Key Map | Combo Map |
  |-------------+----------------+-----------|
  | E           | n              | n         |
  |-------------+----------------+-----------|
  | CAPS, ENTER | y              | n         |
  |-------------+----------------+-----------|
  | F           | n              | y         |
  |-------------+----------------+-----------|
  | ESC         | y              | y         |
  |-------------+----------------+-----------|

  |-------+----------------+-----------|
  | KEY   | Single Key Map | Combo Map |
  |-------+----------------+-----------|
  | SHIFT | n              | n         |
  |-------+----------------+-----------|
  | META  | y              | n         |
  |-------+----------------+-----------|
  | ALT   | n              | y         |
  |-------+----------------+-----------|
  | CTRL  | y              | y         |
  |-------+----------------+-----------|

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
    {  KEY_CAPSLOCK,      KEY_ESC,         KEY_LEFTALT     }, // Change both CAPS' primary and secondary function
    {  KEY_ENTER,         0,               KEY_RIGHTALT    }, // Do not change ENTER's primary function
    {  KEY_ESC,           KEY_CAPSLOCK  }  // Only change ESC's primary function
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
