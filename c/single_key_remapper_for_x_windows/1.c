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

#include <linux/input-event-codes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  unsigned key_from;
  unsigned key_to;
} key_map;

typedef struct {
  char* window_class_name;
  key_map* key_maps;
  unsigned key_maps_size;
} window_map;

window_map window_maps[] = {
  { "Chromium",
  (key_map[]){{ KEY_CAPSLOCK, KEY_ESC },
              { KEY_ESC, KEY_CAPSLOCK }},
  2
  },
  { "Brave-browser",
  (key_map[]){{ KEY_LEFT, KEY_HOME },
              { KEY_UP,KEY_PAGEUP },
              { KEY_RIGHT, KEY_END },
              { KEY_DOWN, KEY_PAGEDOWN }},
  4 }
};

int main(void) {
  for (size_t i = 0; i < sizeof(window_maps)/sizeof(window_maps[0]); i++) {
    printf("The %s map has %u key maps:\n", window_maps[i].window_class_name, window_maps[i].key_maps_size);

    for (size_t j = 0; j < window_maps[i].key_maps_size; j++) {
      printf("%i --> %i\n", window_maps[i].key_maps[j].key_from, window_maps[i].key_maps[j].key_to);
    }
  }

  printf("Done. :)\n");

  return 0;
}
