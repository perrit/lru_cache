#ifndef LRU_CACHE_H_INCLUDED
#define LRU_CACHE_H_INCLUDED

#ifdef _POSIX_THREADS
#define LRU_CACHE_PTHREADS
#endif

/* system includes */
#ifdef LRU_CACHE_PTHREADS
#include <pthread.h>
#endif
#include <stdint.h>
#include <sys/types.h>

#define LRU_CACHE_MIN_VALUES (128)
#define LRU_CACHE_DEFAULT_MAX_VALUES (4096)
#define LRU_CACHE_MIN_SIZE (1024 * 1024) /* 1KB */
#define LRU_CACHE_DEFAULT_MAX_SIZE (0)

#define LRU_CACHE_FLAG_NONE (0)
#define LRU_CACHE_FLAG_DONT_FREE (1<<1)
#define LRU_CACHE_FLAG_DONT_LOCK (1<<2)

typedef struct _lru_cache lru_cache_t;
typedef struct _lru_cache_entry lru_cache_entry_t;

typedef size_t(*lru_cache_size_func)(void *);
typedef int(*lru_cache_compare_func)(void *, void *);
typedef void(*lru_cache_destroy_func)(void *);

struct _lru_cache_entry {
  time_t mtime; /* last modification time */
  char *key; /* copy of original key */
  void *value; /* pointer to value */
  lru_cache_entry_t *cc_prev; /* previous entry in collision chain */
  lru_cache_entry_t *cc_next; /* next entry in collision chain */
  lru_cache_entry_t *prev; /* previous entry in lru cache */
  lru_cache_entry_t *next; /* next entry in lru cache */
};

struct _lru_cache {
  time_t mtime; /* last modification time, used to invalidate cache entries */
  int ttl; /* time to live in seconds */
  uint32_t seed;
  size_t max_size;
  size_t size_cnt;
  unsigned int max_values;
  unsigned int value_cnt;

  lru_cache_entry_t **values;
  /* first and last pointers are used for least recently used logic */
  lru_cache_entry_t *first;
  lru_cache_entry_t *last;

#ifdef LRU_CACHE_PTHREADS
  pthread_rwlock_t lock;
#endif

  lru_cache_size_func size_func;
  lru_cache_compare_func compare_func;
  lru_cache_destroy_func destroy_func;
};

lru_cache_t *lru_cache_create (unsigned int, unsigned int, int *);
int lru_cache_empty (lru_cache_t *, int, int *);
int lru_cache_destroy (lru_cache_t *, int, int *);

/* cache can be locked externally, this is usefull when you need to be sure
   that the data will be valid throughout your function */
int lru_cache_rdlock (lru_cache_t *, int *);
int lru_cache_wrlock (lru_cache_t *, int *);
int lru_cache_unlock (lru_cache_t *, int *);

void *lru_cache_get (lru_cache_t *, const char *, int, int *);
int lru_cache_set (lru_cache_t *, const char *, void *, int, int *);
int lru_caceh_unset (lru_cache_t *, const char *, int, int *);

#endif
