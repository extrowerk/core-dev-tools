#ifndef NTO_CACHE_H
#define NTO_CACHE_H

/* Store memory from buf into cache.  */
void
nto_cache_store(unsigned addr, unsigned len, char *buf);

/* Fetch memory into buf.  Returns number of bytes successfully found.  */
int
nto_cache_fetch(unsigned addr, unsigned len, char *buf);

/* Set all memory in cache to invalid state.  */
void
nto_cache_invalidate();

/* Destroy cache and all memory associated with it.  */
void
nto_cache_destroy();

/* Minimum size to read. */
extern unsigned nto_cache_min;

#endif
