#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lru_cache.h"

void
usage (const char *prog)
{
  const char *fmt =
  "Usage: %s KEY VALUE ...\n";

  printf (fmt, prog);
  exit (EXIT_FAILURE);
}

int
main (int argc, char *argv[])
{
  char *prog, *ptr;
  int cnt, err, max;
  lru_cache_t *cache;

  if ((prog = strrchr (argv[0], '/')))
    prog++;
  else
    prog = argv[0];

  if (argc < 2)
    usage (prog);

  max = argc - 1;
  if (! max || (max % 2))
    usage (prog);
  max = max / 2;

  printf ("%s: got %d key value pairs\n", prog, max);

  max = max < LRU_CACHE_MIN_VALUES ? LRU_CACHE_MIN_VALUES : max;

  if (! (cache = lru_cache_create (max, 0, &err))) {
    fprintf (stderr, "%s: lru_cache_create: %s\n", prog, strerror (err));
    exit (EXIT_FAILURE);
  }

  for (cnt = 1; cnt < argc; cnt += 2) {
    if (cache->last)
      fprintf (stderr, "%s: key: %s, most recently used\n", prog, cache->last->key);
    if (lru_cache_set (cache, argv[cnt], argv[cnt + 1], LRU_CACHE_FLAG_DONT_FREE, &err) < 0) {
      fprintf (stderr, "%s: lru_cache_set: %s: %s\n",
        prog, argv[cnt], strerror (err));
      exit (EXIT_FAILURE);
    }
    printf ("%s: set: %s > %s\n", prog, argv[cnt], argv[cnt + 1]);
  }

  //sleep (5);

  for (cnt = 1; cnt < argc; cnt += 2) {
    if (! (ptr = lru_cache_get (cache, argv[cnt], 0, &err)) && err) {
      fprintf (stderr, "%s: lru_cache_get: %s: %s\n",
        prog, argv[cnt], strerror (err));
      exit (EXIT_FAILURE);
    }
    if (ptr)
      printf ("%s: get: %s < %s\n", prog, argv[cnt], ptr);
    else
      printf ("%s: get: %s, unavailable\n", prog, argv[cnt]);
  }

  for (cnt = (argc - 2); cnt > 0; cnt -= 2) {
    if (! (ptr = lru_cache_get (cache, argv[cnt], 0, &err)) && err) {
      fprintf (stderr, "%s: lru_cache_get: %s: %s\n",
        prog, argv[cnt], strerror (err));
      exit (EXIT_FAILURE);
    }
    if (ptr) {
      if (lru_cache_unset (cache, argv[cnt], 0, &err) < 0) {
        fprintf (stderr, "%s: lru_cache_unset: %s: %s\n",
          prog, argv[cnt], strerror (err));
        exit (EXIT_FAILURE);
      }
      printf ("%s: unset: %s\n", prog, argv[cnt]);
      if (cache->last)
        fprintf (stderr, "%s: key: %s, most recently used\n", prog, cache->last->key);
    }
  }

  if (lru_cache_destroy (cache, LRU_CACHE_FLAG_DONT_FREE, &err) < 0) {
    fprintf (stderr, "%s: lru_cache_destroy: %s\n", prog, strerror (err));
    exit (EXIT_FAILURE);
  }

  return EXIT_SUCCESS;
}
