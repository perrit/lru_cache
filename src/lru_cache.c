/* system includes */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "lru_cache.h"

#define LRU_CACHE_SET_ERRNO(ptr,err) \
  do {                               \
    if (ptr)                         \
      *ptr = err;                    \
  } while (0);

#ifdef LRU_CACHE_PTHREADS
#define LRU_CACHE_RDLOCK(cache,flags) \
  ((flags & LRU_CACHE_FLAG_DONT_LOCK) ? 0 : pthread_rwlock_rdlock (&cache->lock))

#define LRU_CACHE_WRLOCK(cache,flags) \
  ((flags & LRU_CACHE_FLAG_DONT_LOCK) ? 0 : pthread_rwlock_wrlock (&cache->lock))

#define LRU_CACHE_UNLOCK(cache,flags) \
  ((flags & LRU_CACHE_FLAG_DONT_LOCK) ? 0 : pthread_rwlock_unlock (&cache->lock))
#else
#define LRU_CACHE_RDLOCK(cache,flags) (0)
#define LRU_CACHE_WRLOCK(cache,flags) (0)
#define LRU_CACHE_UNLOCK(cache,flags) (0)
#endif

#ifdef NDEBUG
#define LRU_CACHE_DEBUG(fmt, ...)
#else
#include <stdio.h>
#define LRU_CACHE_DEBUG(fmt, ...) \
  fprintf (stderr, fmt, ## __VA_ARGS__);
#endif

uint32_t rotl32 (uint32_t, int8_t);
uint32_t getblock (const uint32_t *, int);
uint32_t fmix (uint32_t);
static void MurMurhash3_x86_32 (const void *, int, uint32_t, void *);


/* MurmurHash3 by Austin Appleby, http://code.google.com/p/smhasher/ */

uint32_t rotl32 ( uint32_t x, int8_t r )
{
  return (x << r) | (x >> (32 - r));
}

#define	ROTL32(x,y)	rotl32(x,y)

//-----------------------------------------------------------------------------
// Block read - if your platform needs to do endian-swapping or can only
// handle aligned reads, do the conversion here

uint32_t getblock ( const uint32_t * p, int i )
{
  return p[i];
}

//-----------------------------------------------------------------------------
// Finalization mix - force all bits of a hash block to avalanche

uint32_t fmix ( uint32_t h )
{
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;

  return h;
}

//-----------------------------------------------------------------------------

static void
MurMurhash3_x86_32 (const void *key, int len, uint32_t seed, void *out)
{
  int i;
  const uint8_t * data = (const uint8_t*)key;
  const int nblocks = len / 4;

  uint32_t h1 = seed;

  uint32_t c1 = 0xcc9e2d51;
  uint32_t c2 = 0x1b873593;

  //----------
  // body

  const uint32_t * blocks = (const uint32_t *)(data + nblocks*4);

  for(i = -nblocks; i; i++)
  {
    uint32_t k1 = getblock(blocks,i);

    k1 *= c1;
    k1 = ROTL32(k1,15);
    k1 *= c2;
    
    h1 ^= k1;
    h1 = ROTL32(h1,13); 
    h1 = h1*5+0xe6546b64;
  }

  //----------
  // tail

  const uint8_t * tail = (const uint8_t*)(data + nblocks*4);

  uint32_t k1 = 0;

  switch(len & 3)
  {
  case 3: k1 ^= tail[2] << 16;
  case 2: k1 ^= tail[1] << 8;
  case 1: k1 ^= tail[0];
          k1 *= c1; k1 = ROTL32(k1,15); k1 *= c2; h1 ^= k1;
  };

  //----------
  // finalization

  h1 ^= len;

  h1 = fmix(h1);

  *(uint32_t*)out = h1;
}

/* /MurmurHash3 */

static uint32_t
lru_cache_hash (lru_cache_t *cache, const char *key)
{
  int len;
  uint32_t hash;

  assert (cache);
  assert (key);

  len = strlen (key);
  MurMurhash3_x86_32 (key, len, cache->seed, &hash);
  hash = hash % cache->max_values;

  LRU_CACHE_DEBUG ("%s: key: %s, hash: %lu\n", __func__, key, hash);

  return hash;
}

lru_cache_t *
lru_cache_create (unsigned int max_values, unsigned int max_size, int *err)
{
  lru_cache_t *cache;

  cache = NULL;

  if (max_values) {
    if (max_values < LRU_CACHE_MIN_VALUES) {
      LRU_CACHE_SET_ERRNO (err, EINVAL);
      goto error;
    }
  } else {
    max_values = LRU_CACHE_DEFAULT_MAX_VALUES;
  }

  if (max_size) {
    if (max_size < LRU_CACHE_MIN_SIZE) {
      LRU_CACHE_SET_ERRNO (err, EINVAL);
      goto error;
    }
  } else {
    max_size = LRU_CACHE_DEFAULT_MAX_SIZE;
  }

  if (! (cache = calloc (1, sizeof (lru_cache_t)))) {
    LRU_CACHE_SET_ERRNO (err, errno);
    goto error;
  }

  cache->mtime = time (NULL);
  cache->seed = cache->mtime;
  cache->max_values = max_values;
  cache->max_size = max_size;

  if (! (cache->values = calloc (max_values, sizeof (lru_cache_entry_t)))) {
    LRU_CACHE_SET_ERRNO (err, errno);
    goto error;
  }

#ifdef LRU_CACHE_PTHREADS
  LRU_CACHE_DEBUG ("%s: posix threads available\n", __func__);
  if (pthread_rwlock_init (&cache->lock, NULL) != 0) {
    LRU_CACHE_SET_ERRNO (err, errno);
    goto error;
  }
#endif

  return cache;
error:
  /* clean up */
  if (cache) {
    if (cache->values)
      free (cache->values);
    free (cache);
  }
  return NULL;
}

#define LRU_CACHE_SET_OPT(opt)                       \
  int ret;                                           \
                                                     \
  assert (cache);                                    \
                                                     \
  if ((ret = LRU_CACHE_WRLOCK (cache,flags)) != 0) { \
    LRU_CACHE_SET_ERRNO (err,ret);                   \
    return -1;                                       \
  }                                                  \
                                                     \
  cache->opt = opt;                                  \
                                                     \
  if ((ret = LRU_CACHE_UNLOCK (cache,flags)) != 0) { \
    LRU_CACHE_SET_ERRNO (err,ret);                   \
    return -1;                                       \
  }                                                  \
                                                     \
  return 0;

int
lru_cache_set_size_func (lru_cache_t *cache,
  lru_cache_size_func size_func, int flags, int *err)
{
  LRU_CACHE_SET_OPT (size_func);
}

int
lru_cache_set_compare_func (lru_cache_t *cache,
  lru_cache_compare_func compare_func, int flags, int *err)
{
  LRU_CACHE_SET_OPT (compare_func);
}

int
lru_cache_set_destroy_func (lru_cache_t *cache,
  lru_cache_destroy_func destroy_func, int flags, int *err)
{
  LRU_CACHE_SET_OPT (destroy_func);
}

int
lru_cache_set_ttl (lru_cache_t *cache, int ttl, int flags, int *err)
{
  LRU_CACHE_SET_OPT (ttl);
}

/* update mtime member of lru_cache_t object, which will effectively invalidate
   all older cache entries */
int
lru_cache_empty (lru_cache_t *cache, int flags, int *err)
{
  time_t mtime = time (NULL);
  LRU_CACHE_SET_OPT (mtime);
}

int
lru_cache_destroy (lru_cache_t *cache, int flags, int *err)
{
  int ret;
  lru_cache_entry_t *entry, *chain, *next;
  unsigned int cnt;

  if (! (flags & LRU_CACHE_FLAG_DONT_FREE))
    assert (cache->destroy_func);

  if (cache) {
    if (cache->values) {
      for (cnt = 0;
           cnt < cache->max_values && cache->value_cnt > 0;
           cnt++)
      {
        chain = cache->values[cnt];

        for (entry = chain; entry; entry = next) {
          LRU_CACHE_DEBUG ("%s: key: %s, hash: %s\n",
            __func__, entry->key, cnt);
          next = entry->next;
          if (entry->value && ! (flags & LRU_CACHE_FLAG_DONT_FREE))
            cache->destroy_func (entry->value);
          if (entry->key)
            free (entry->key);
          free (entry);
          cache->value_cnt--;
        }

        cache->values[cnt] = NULL;
      }
      free (cache->values);
    }
#ifdef LRU_CACHE_PTHREADS
    if ((ret = pthread_rwlock_destroy (&cache->lock)) != 0) {
      LRU_CACHE_SET_ERRNO (err, ret);
      return -1;
    }
#endif
    free (cache);
  }

  return 0;
}

int
lru_cache_rdlock (lru_cache_t *cache, int *err)
{
#ifdef LRU_CACHE_PTHREADS
  int ret;

  assert (cache);

  if ((ret = pthread_rwlock_rdlock (&cache->lock)) != 0) {
    LRU_CACHE_SET_ERRNO (err, ret);
    return -1;
  }
#endif
  return 0;
}

int
lru_cache_wrlock (lru_cache_t *cache, int *err)
{
#ifdef LRU_CACHE_PTHREADS
  int ret;

  assert (cache);

  if ((ret = pthread_rwlock_wrlock (&cache->lock)) != 0) {
    LRU_CACHE_SET_ERRNO (err, ret);
    return -1;
  }
#endif
  return 0;
}

int
lru_cache_unlock (lru_cache_t *cache, int *err)
{
#ifdef LRU_CACHE_PTHREADS
  int ret;

  assert (cache);

  if ((ret = pthread_rwlock_unlock (&cache->lock)) != 0) {
    LRU_CACHE_SET_ERRNO (err, ret);
    return -1;
  }
#endif
  return 0;
}

void *
lru_cache_get (lru_cache_t *cache, const char *key, int flags,
  int *err)
{
  int ret;
  lru_cache_entry_t *chain, *entry;
  time_t now;
  uint32_t hash;

  assert (cache);
  assert (key);

  if ((ret = LRU_CACHE_RDLOCK (cache, flags)) != 0) {
    LRU_CACHE_SET_ERRNO (err, ret);
    return NULL;
  }

  hash = lru_cache_hash (cache, key);
  chain = cache->values[hash];
  entry = NULL;
  if (chain) {
    now = time (NULL);
    for (entry = chain; entry; entry = entry->cc_next) {
      LRU_CACHE_DEBUG ("%s: key: %s, hash: %lu, mtime: %u, ttl: %u\n",
        __func__, entry->key, hash, entry->mtime, cache->ttl);
      if (strcmp (entry->key, key) == 0) {
        /* only return cache entry if it is valid */
        if (entry->mtime < cache->mtime || (entry->mtime + cache->ttl) < now)
          entry = NULL;
        break;
      }
    }
  }

  if ((ret = LRU_CACHE_UNLOCK (cache, flags)) != 0) {
    LRU_CACHE_SET_ERRNO (err, ret);
    return NULL;
  }

  if (entry)
    return entry->value;

  return NULL;
}

int
lru_cache_set (lru_cache_t *cache, const char *key, void *value, int flags,
  int *err)
{
  int res, ret;
  int prop_flags; /* propagation flags */
  lru_cache_entry_t *chain, *cur, *entry, *next, *prev;
  size_t resize, size;
  time_t now;
  uint32_t hash;

  assert (cache);
  assert (key);
  assert (value);

  if ((ret = LRU_CACHE_WRLOCK (cache, flags)) != 0) {
    LRU_CACHE_SET_ERRNO (err, ret);
    return -1;
  }

  res = 0;
  prop_flags = flags | LRU_CACHE_FLAG_DONT_LOCK;

  hash = lru_cache_hash (cache, key);
  chain = cache->values[hash];
  entry = NULL;
  if (chain) {
    for (; chain; ) {
      LRU_CACHE_DEBUG ("%s: key: %s, hash: %lu, exists\n",
        __func__, chain->key, hash);
      if (strcmp (chain->key, key) == 0)
        entry = chain;
      chain = chain->cc_next;
    }
  } else {
    LRU_CACHE_DEBUG ("%s: key: %s, hash: %lu, does not exist\n",
      __func__, key, hash);
  }

  /* remove unwanted hash table entries so we stay within bounds */
  if (cache->max_size) {
    if (entry) {
      resize = cache->size_func (value);
      size = cache->size_func (entry->value);
      if (resize < size)
        resize = 0;
    } else {
      resize  = sizeof (lru_cache_entry_t);
      resize += strlen (key);
      resize += cache->size_func (value);
    }

    LRU_CACHE_DEBUG ("%s: max_size: %lu, size_cnt: %lu, resize: %lu\n",
      __func__, cache->max_size, cache->size_cnt, resize);

    for (cur = cache->first;
         cur && (cache->size_cnt + resize) > cache->max_size;
         cur = cur->next)
    {
      if (entry != cur && lru_cache_unset (cur, cur->key, prop_flags, err) < 0)
        goto error;
    }
  }

  if (! entry && cache->value_cnt >= cache->max_values) {
    for (cur = cache->first;
         cur && cache->value_cnt >= cache->max_values;
         cur = cur->next)
    {
      if (lru_cache_unset (cur, cur->key, prop_flags, err) < 0)
        goto error;
    }
  }

  /* update or insert hash table entry */
  if (entry) {
    if (! (flags & LRU_CACHE_FLAG_DONT_FREE))
      cache->destroy_func (entry->value);
    if (entry != cache->last) {
      prev = entry->prev;
      next = entry->next;
      next->prev = entry->prev;
      if (entry == cache->first)
        cache->first = next;
      else
        prev->next = next;
    }
    entry->value = value;
  } else {
    if (! (entry = calloc (1, sizeof (lru_cache_entry_t)))) {
      LRU_CACHE_SET_ERRNO (err, errno);
      goto error;
    }
    entry->mtime = time (NULL);
    if (! (entry->key = strdup (key))) {
      LRU_CACHE_SET_ERRNO (err, errno);
      goto error;
    }
    if (cache->last) {
      prev = cache->last;
      prev->next = entry;
      entry->prev = prev;
      cache->last = entry;
    } else {
      cache->first = entry;
      cache->last = entry;
    }
    entry->value = value;

    if (chain) {
      chain->next = entry;
      entry->prev = chain;
    } else {
      cache->values[hash] = entry;
    }
  }

unlock:
  if ((ret = LRU_CACHE_UNLOCK (cache, flags)) != 0) {
    if (! res)
      LRU_CACHE_SET_ERRNO (err, ret);
    return -1;
  }

  return res;
error:
  res = -1;
  goto unlock;
}

int
lru_cache_unset (lru_cache_t *cache, const char *key, int flags,
  int *err)
{
  int len;
  int res, ret;
  lru_cache_entry_t *chain, *entry, *next, *prev;
  uint32_t hash;

  assert (cache);
  assert (key);

  res = 0;

  if ((ret = LRU_CACHE_WRLOCK (cache, flags)) < 0) {
    LRU_CACHE_SET_ERRNO (err, ret);
    return -1;
  }

  len = strlen (key);
  hash = lru_cache_hash (cache, key);
  chain = cache->values[hash];
  if (chain) {
    for (entry = chain;
         entry && strcmp (entry->key, key) != 0;
         entry = entry->next)
      ;
    /* entry must exist */
    if (! entry) {
      LRU_CACHE_SET_ERRNO (err, EINVAL);
      goto error;
    }

    /* update collision chain pointers */
    prev = entry->cc_prev;
    next = entry->cc_next;
    if (entry == chain)
      chain = next;
    if (prev)
      prev->cc_next = next;
    if (next)
      next->cc_prev = prev;

    cache->values[hash] = chain;

    /* update cache value pointers */
    prev = entry->prev;
    next = entry->next;
    if (prev)
      prev->next = next;
    else if (entry == cache->first)
      cache->first = next;
    if (next)
      next->prev = prev;
    else if (entry == cache->last)
      cache->last = prev;
  } else {
    /* entry must exist */
    LRU_CACHE_SET_ERRNO (err, EINVAL);
    goto error;
  }

unlock:
  if ((ret = LRU_CACHE_UNLOCK (cache, flags)) < 0) {
    if (! res)
      LRU_CACHE_SET_ERRNO (err, ret);
    return -1;
  }

  return res;
error:
  res = -1;
  goto unlock;
}
