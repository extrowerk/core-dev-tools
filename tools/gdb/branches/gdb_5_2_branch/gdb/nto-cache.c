/* Basic memory cache.  Memory regions stored in sorted linked list.
   Timestamps kept on regions so when coalesceing regions, we know whether
   to overwrite or abandon memory.  */

//#define TEST

#ifndef TEST
#include "defs.h"
#include "gdb_string.h"
#include "gdbcmd.h"
#else
#include <stdio.h>
#include <malloc.h>
#define xfree free
#define xmalloc malloc
#define xrealloc realloc
#endif

#include "nto-cache.h"

struct memregion{
	struct memregion *next;
	unsigned addr; /* Remote address.  */
	unsigned len; /* Length in cache.  */
	unsigned size; /* Size of data.  */
	char *data;
};

unsigned nto_cache_min = 32;

static struct memregion *head;
static struct memregion *empties;

/* Grab an unused region or create a new one if none available.  */
static struct memregion *
get_free_region()
{
	struct memregion *tmp;
	if(empties){
		tmp = empties;
		empties = tmp->next;
		tmp->next = NULL;
		tmp->addr = 0;
		tmp->len = 0;
		memset(tmp->data, 0, tmp->size);
	}
	else {
		tmp = xmalloc(sizeof(struct memregion));
		memset(tmp, 0, sizeof(struct memregion));
	}
	return tmp;
}

static void
coalesce()
{
	struct memregion *region = head, *tmp;
#define OVERLAPPING(r1, r2) ((r1)->addr <= (r2)->addr && (r2)->addr <= (r1)->addr + (r1)->len)
#define CONTAINED_IN(r1, r2) ((r2)->addr + (r2)->len <= (r1)->addr + (r1)->len)
#define DIFFERENCE(r1, r2) ((r2)->addr + (r2)->len - ((r1)->addr + (r1)->len))
#define DEST_ADDR(r1, r2) ((r1)->data + ((r2)->addr - (r1)->addr))
	while(region->next){
		/* Regions are kept in sorted order.  */
		if(CONTAINED_IN(region, region->next)){
			tmp = region->next;
			region->next = tmp->next;
			memcpy(DEST_ADDR(region, tmp), tmp->data, tmp->len);
			tmp->next = empties;
			empties = tmp;
			if(!region->next)
				break;
			continue;
		}
	
		/* We have overlapping regions so enlarge and catenate.  */
		if(OVERLAPPING(region, region->next)){
			unsigned diff = DIFFERENCE(region, region->next);
			tmp = region->next;
			if(region->size < region->len + diff){
				region->size = region->len + diff;
				region->data = xrealloc(region->data, region->size);
			}
			memcpy(DEST_ADDR(region,tmp), tmp->data, tmp->len);
			region->next = tmp->next;
			region->len += diff;
			tmp->next = empties;
			empties = tmp;
			if(!region->next)
				break;
			continue;
		}
		region = region->next;
	}
#undef OVERLAPPING
#undef CONTAINED_IN
#undef DIFFERENCE
#undef DEST_ADDR
}

void
nto_cache_store(unsigned addr, unsigned len, char *buf)
{
#define INSERT_HERE(r, addr) ((!(r)->next) || ((r)->addr <= (addr) && (r)->next->addr > (addr)))
	struct memregion *region = head, *tmp;
	
	if(!nto_cache_min || len == 0)
		return;
	
	while(region){
		if(INSERT_HERE(region, addr))
			break;
		region = region->next;
	}
	
	tmp = get_free_region();
	tmp->next = region->next;
	region->next = tmp;

	tmp->addr = addr;
	tmp->len = len;
	if(tmp->size < len){
		tmp->data = xrealloc(tmp->data, len);
		tmp->size = len;
	}
	memcpy(tmp->data, buf, len);
	coalesce();
#undef INSERT_HERE
}

int
nto_cache_fetch(unsigned addr, unsigned len, char *buf)
{
#define CONTAINED_IN(r, addr) ((r)->addr <= (addr) && (addr) < (r)->addr + (r)->len)
#define SRC_ADDR(r, addr) ((r)->data + addr - (r)->addr)
#define LEN(r, addr, len) ((addr) + (len) >= (r)->addr + (r)->len ? \
		(r)->addr + (r)->len - (addr): (len))
	struct memregion *region = head;
	int retval = 0;

	if(!nto_cache_min)
		return 0;

	while(region){
		if(CONTAINED_IN(region, addr)){
			retval = LEN(region, addr, len);
			memcpy(buf, SRC_ADDR(region, addr), retval);
			break;
		}
		region = region->next;
	}
	return retval;
#undef CONTAINED_IN
#undef SRC_ADDR
#undef LEN
}

void
nto_cache_invalidate()
{
	struct memregion *region = head->next, *tmp;

	if(!nto_cache_min)
		return;
	
	while(region){
		tmp = region;
		region = region->next;
		tmp->next = empties;
		empties = tmp;
	}
	head->next = NULL;
}

static void
cache_destroy(struct memregion *region)
{
	struct memregion *tmp;

	while(region){
		tmp = region;
		region = region->next;
		xfree(tmp->data);
		xfree(tmp);
	}
}

void
nto_cache_destroy()
{
	if(!nto_cache_min)
		return;
	
	cache_destroy(head->next);
	head->next = NULL;
	cache_destroy(empties);
	empties = NULL;
}

void
_initialize_nto_cache()
{
	head = xmalloc(sizeof(struct memregion));
	memset(head, 0, sizeof(struct memregion));
#ifndef TEST
	add_show_from_set(add_set_cmd("nto-remote-cache", no_class,
				  var_zinteger, (char *)&nto_cache_min,
				  "Set size of minimum memory read from remote Neutrino target. \
				  Zero disables cacheing.\n", &setlist),
				  &showlist);
#endif
}

#ifdef TEST

void
test_fetch(unsigned addr, unsigned len)
{
	static char buf[1024];
	int ret = nto_cache_fetch(addr, len, buf), i;

	printf("%d bytes at 0x%x: ", ret, addr);
	for(i = 0 ; i < ret ; i++)
		putc(buf[i],stdout);
	putc('\n', stdout);
}

void
test_store(unsigned addr, unsigned len, char *buf)
{
	nto_cache_store(addr, len, buf);
}

void
dump()
{
	struct memregion *region = head;

	printf("\nHead:\n");
	while(region){
		int i;
		printf("%d bytes at 0x%x, ", region->len, region->addr);
		printf("buffer: %d bytes, data:", region->size);
		for(i = 0 ; i < region->size ; i++)
			putc(*(region->data + i), stdout);
		putc('\n',stdout);
		region = region->next;
	}
	printf("\nEmpties:\n");
	region = empties;
	while(region){
		printf("buffer: %d bytes\n", region->size);
		region = region->next;
	}
	printf("\n");
}

char *buf0 = "123456789ABCDEFG123456789ABCDEFG";
char *buf1 = "abcdabcdabcdabcdabcdabcdabcdabcd";
char *buf2 = "efghefghefghefghefghefghefghefgh";
char *buf3 = "ijklijklijklijklijklijklijklijkl";
char *buf4 = "mnopmnopmnopmnopmnopmnopmnopmnop";
char *buf5 = "qrstqrstqrstqrstqrstqrstqrstqrst";
char *buf6 = "uvwxuvwxuvwxuvwxuvwxuvwxuvwxuvwx";
char *buf7 = "uvwxuvwx";

int
main()
{
	_initialize_nto_cache();
	test_store(0x1000, strlen(buf0), buf0);
	test_store(0x1030, strlen(buf1), buf1);
	test_store(0x10a0, strlen(buf2), buf2);
	/* Coalesce adjacent regions.  */
	test_store(0x1018, strlen(buf3), buf3);
	test_store(0x1100, strlen(buf4), buf4);
	test_store(0x1130, strlen(buf5), buf5);
	test_store(0x1150, strlen(buf6), buf6);
	/* Add region that fits inside another.  */
	test_store(0x1160, strlen(buf7), buf7);
	/* Add region that overlaps another.  */
	test_store(0x116c, strlen(buf7), buf7);

	test_fetch(0x1000, 32);
	test_fetch(0x1010, 32);
	test_fetch(0x1020, 64);
	test_fetch(0x1150, 64);

	dump();
	nto_cache_invalidate();
	
	/* Do it all over again.  */
	test_store(0x1000, strlen(buf0), buf0);
	test_store(0x1040, strlen(buf1), buf1);
	test_store(0x10a0, strlen(buf2), buf2);
	/* Coalesce adjacent regions.  */
	test_store(0x1020, strlen(buf3), buf3);
	/* Don't add quite so many this time.  */
	test_store(0x1150, strlen(buf6), buf6);
	/* Add region that fits inside another.  */
	test_store(0x1160, strlen(buf7), buf7);
	/* Add region that overlaps another.  */
	test_store(0x116c, strlen(buf7), buf7);

	test_fetch(0x1000, 32);
	test_fetch(0x1010, 32);
	test_fetch(0x1020, 64);
	test_fetch(0x1150, 64);
	
	dump();
	nto_cache_destroy();

	return 0;
}
#endif /* TEST */
