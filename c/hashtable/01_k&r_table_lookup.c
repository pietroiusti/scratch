#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct nlist { /* table entry */
  struct nlist *next; /* next entry in chain */
  char *name; /* defined name */ /* gp: key */
  char *defn; /* replacement text */ /* gp: value */
};

# define HASHSIZE 101

static struct nlist *hashtab[HASHSIZE]; /* pointer table */

/* hash: form hash value for string s */
unsigned hash(char *s)
{
  unsigned hashval;

  for (hashval = 0; *s != '\0'; s++)
    hashval = *s + 31 * hashval;
  return hashval % HASHSIZE;
}

/* lookup: look for s in hashtab */
struct nlist *lookup (char *s)
{
  struct nlist *np;

  for (np = hashtab[hash(s)]; np != NULL; np = np->next)
    if (strcmp(s, np->name) == 0)
      return np; /* found */
  return NULL;   /* not found */
}

/* install: put (name, defn) in hashtab */
struct nlist *install(char *name, char *defn)
{
  struct nlist *np;
  unsigned hashval;

  if ((np = lookup(name)) == NULL) { /* not found */
    np = (struct nlist *) malloc(sizeof(*np));
    if (np == NULL || (np->name = strdup(name)) == NULL)
      return NULL;
    hashval = hash(name);
    np->next = hashtab[hashval];
    hashtab[hashval] = np;
  } else /* already  here */
    free((void *) np->defn); /* free previous defn */
  if ((np->defn = strdup(defn)) == NULL)
    return NULL;
  return np;
}

int main(void)
{
  printf("Installing 'giulio'...\n");
  install("giulio", "my name");

  printf("Looking up...\n");
  struct nlist *pointer = lookup("giulio");

  if (pointer) {
    printf("Found!\n");
    printf("Def: %s\n", pointer->defn);
  }

  return 0;
}
