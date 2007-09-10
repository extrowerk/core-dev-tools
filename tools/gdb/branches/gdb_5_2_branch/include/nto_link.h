#ifndef __LINK_H_INCLUDED
#define __LINK_H_INCLUDED

#include <elf/common.h>
#include <elf/external.h>

typedef unsigned int _uintptr;
typedef Elf32_External_Dyn Elf32_Dyn;

typedef struct link_map	Link_map;
struct link_map {
	_uintptr		l_addr;		/* base address */
	char			*l_name;	/* full soname of lib */
	Elf32_Dyn		*l_ld;		/* _DYNAMIC in lib */
	Link_map		*l_next;
	Link_map		*l_prev;
	char			*l_refname;	/* matching soname of lib */
};

typedef enum {
	RT_CONSISTENT,				/* link_maps are consistent */
	RT_ADD,						/* Adding to link_map */
	RT_DELETE					/* Removeing a link_map */
} r_state_e;

typedef enum {
	RD_FL_NONE =	0,
	RD_FL_DBG =		(1<<1)		/* process may be being debugged */
} rd_flags_e;

typedef enum {
	RD_NONE = 0,
	RD_PREINIT,					/* Before .init() */
	RD_POSTINIT,				/* After .init() */
	RD_DLACTIVITY				/* dlopen() or dlclose() occured */
} rd_event_e;

#define	R_DEBUG_VERSION	2

struct r_debug {
	int				r_version;	/* R_DEBUG_VERSION */
	Link_map		*r_map;		/* Global link_map */
	_uintptr		r_brk;		/* void (*r_brk)(void) */
	r_state_e		r_state;	/* RT_* */
	_uintptr		r_ldbase;	/* ldqnx.so.1 base address */
	Link_map		*r_ldsomap;	/* ldqnx.so.1 link map */
	rd_event_e		r_rdevent;	/* RD_* */
	rd_flags_e		r_flags;	/* RD_FL_* */
};

#endif
