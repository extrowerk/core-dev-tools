#ifndef dsmsgs_h_included
#define dsmsgs_h_included

//
// These are the protocol versioning numbers
// Update them with changes that introduce potential 
// compatibility issues
//
#define PDEBUG_PROTOVER_MAJOR				0x00000000
#define PDEBUG_PROTOVER_MINOR				0x00000003


#ifdef __QNX__
#include <inttypes.h>
#else
#include <nto_inttypes.h>
#endif
#include <stddef.h>
#include "pdbgerr.h"

#if defined(__WATCOMC__)
typedef struct { uint32_t hi; uint32_t lo; } uint64_t;
typedef struct { int32_t hi; int32_t lo; } int64_t;
#endif

// We are moving away from the channel commands - only the RESET
// and NAK are required.  The DEBUG and TEXT channels are now part
// of the DShdr and TShdr structs, 4th byte.  GP June 1 1999.
// They are still supported, but not required.
//
// A packet containg a single byte is a set channel command. 
//     IE:  7e xx chksum 7e
// 
// After a set channel all following packets are in the format
// for the specified channel. Currently three channels are defined.
// The raw channel has no structure. The other channels are all framed.
// The contents of each channel is defined by structures below. 
//
// 0 - Reset channel. Used when either end starts.
//
// 1 - Debug channel with the structure which follows below.
//     Uses DS (Debug Services) prefix.
//
// 2 - Text channel with the structure which follows below.
//     Uses TS (Text Services) prefix.
//
// 0xff - Negative acknowledgment of a packet transmission.
//


#define SET_CHANNEL_RESET				0
#define SET_CHANNEL_DEBUG				1
#define SET_CHANNEL_TEXT				2
#define SET_CHANNEL_NAK					0xff


///////////////////////////////////////////////////////////////////////////////
// Debug channel Messages:   DS - Debug services
//

//
// Defines and structures for the debug channel.
//
#define DS_DATA_MAX_SIZE				1024
#define DS_DATA_RCV_SIZE(msg, total)	((total) - (sizeof(*(msg)) - DS_DATA_MAX_SIZE))
#define DS_MSG_OKSTATUS_FLAG			0x20000000
#define DS_MSG_OKDATA_FLAG				0x40000000
#define DS_MSG_NO_RESPONSE				0x80000000

#define QNXNTO_NSIG			57	// from signals.h NSIG

//
// Common message header. It must be 32 or 64 bit aligned.
// The top bit of cmd is 1 for BIG endian data format.
//
#define DSHDR_MSG_BIG_ENDIAN	0x80
struct DShdr {
	uint8_t		cmd;
	uint8_t		subcmd;
	uint8_t		mid;
	uint8_t		channel;
} ;

//
// Command types
//
enum {
	DStMsg_connect,		//  0  0x0
	DStMsg_disconnect,	//  1  0x1
	DStMsg_select,		//  2  0x2
	DStMsg_mapinfo,		//  3  0x3
	DStMsg_load,		//  4  0x4
	DStMsg_attach,		//  5  0x5
	DStMsg_detach,		//  6  0x6
	DStMsg_kill,		//  7  0x7
	DStMsg_stop,		//  8  0x8
	DStMsg_memrd,		//  9  0x9
	DStMsg_memwr,		// 10  0xA
	DStMsg_regrd,		// 11  0xB
	DStMsg_regwr,		// 12  0xC
	DStMsg_run,		// 13  0xD
	DStMsg_brk,		// 14  0xE
	DStMsg_fileopen,	// 15  0xF
	DStMsg_filerd,		// 16  0x10
	DStMsg_filewr,		// 17  0x11
	DStMsg_fileclose,	// 18  0x12
	DStMsg_pidlist,		// 19  0x13
	DStMsg_cwd,         	// 20  0x14
	DStMsg_env,		// 21  0x15
	DStMsg_base_address,	// 22  0x16
	DStMsg_protover,	// 23  0x17
	DStMsg_handlesig,	// 24  0x18
	DStMsg_cpuinfo,		// 25  0x19
	// Room for new codes here
	DSrMsg_err=32,		// 32  0x20
	DSrMsg_ok,		// 33  0x21
	DSrMsg_okstatus,	// 34  0x22
	DSrMsg_okdata,		// 35  0x23
	// Room for new codes here
	DShMsg_notify=64	// 64  0x40
} ;


//
// Subcommand types
//
enum {
	DSMSG_LOAD_DEBUG,
	DSMSG_LOAD_RUN,
	DSMSG_LOAD_RUN_PERSIST,
	DSMSG_LOAD_INHERIT_ENV=0x80
} ;

enum {
	DSMSG_ENV_CLEARARGV,
	DSMSG_ENV_ADDARG,
	DSMSG_ENV_CLEARENV,
	DSMSG_ENV_SETENV,
	DSMSG_ENV_SETENV_MORE
} ;

enum {
	DSMSG_STOP_PID,
	DSMSG_STOP_PIDS
} ;

enum {
	DSMSG_SELECT_SET,
	DSMSG_SELECT_QUERY
} ;

enum {
	DSMSG_KILL_PIDTID,
	DSMSG_KILL_PID,
	DSMSG_KILL_PIDS
} ;

enum {
	DSMSG_MEM_VIRTUAL,
	DSMSG_MEM_PHYSICAL,
	DSMSG_MEM_IO,
	DSMSG_MEM_BASEREL
} ;

enum {
	DSMSG_REG_GENERAL,
	DSMSG_REG_FLOAT,
	DSMSG_REG_SYSTEM,
	DSMSG_REG_ALT
} ;

enum {
	DSMSG_RUN,
	DSMSG_RUN_COUNT,
	DSMSG_RUN_RANGE,
} ;

enum {
	DSMSG_PIDLIST_BEGIN,
	DSMSG_PIDLIST_NEXT,
	DSMSG_PIDLIST_SPECIFIC,
	DSMSG_PIDLIST_SPECIFIC_TID,	// *_TID - send starting tid for the request,
} ;					// and the response will have total to be sent

enum {
	DSMSG_CWD_QUERY,
	DSMSG_CWD_SET,
};

enum {
	DSMSG_MAPINFO_BEGIN =    0x01,
	DSMSG_MAPINFO_NEXT =     0x02,
	DSMSG_MAPINFO_SPECIFIC = 0x04,
	DSMSG_MAPINFO_ELF =      0x80,
};

enum {
	DSMSG_PROTOVER_MINOR = 0x000000FF, // bit field (status & DSMSG_PROTOVER_MAJOR)
	DSMSG_PROTOVER_MAJOR = 0x0000FF00, // bit field (status & DSMSG_PROTOVER_MINOR)
};

enum {
	DSMSG_BRK_EXEC =	0x0001,	// execution breakpoint
	DSMSG_BRK_RD =		0x0002,	// read access (fail if not supported)
	DSMSG_BRK_WR =		0x0004,	// write access (fail if not supported)
	DSMSG_BRK_RW =		0x0006,	// read or write access (fail if not supported)
	DSMSG_BRK_MODIFY =	0x0008,	// memory modified
	DSMSG_BRK_RDM =		0x000a,	// read access if suported otherwise modified
	DSMSG_BRK_WRM =		0x000c,	// write access if suported otherwise modified
	DSMSG_BRK_RWM =		0x000e,	// read or write access if suported otherwise modified
	DSMSG_BRK_HW =		0x0010,	// only use hardware debugging (i.e. no singlestep)
} ;

enum {
	DSMSG_NOTIFY_PIDLOAD,	/*  0 */
	DSMSG_NOTIFY_TIDLOAD,	/*  1 */
	DSMSG_NOTIFY_DLLLOAD,	/*  2 */
	DSMSG_NOTIFY_PIDUNLOAD,	/*  3 */
	DSMSG_NOTIFY_TIDUNLOAD,	/*  4 */
	DSMSG_NOTIFY_DLLUNLOAD,	/*  5 */
	DSMSG_NOTIFY_BRK,	/*  6 */
	DSMSG_NOTIFY_STEP,	/*  7 */
	DSMSG_NOTIFY_SIGEV,	/*  8 */
	DSMSG_NOTIFY_STOPPED	/*  9 */
} ;



//
// Messages sent to the target. DStMsg_* (t - for target messages)
//


//
// Connect to the agent running on the target
//
typedef struct {
	struct DShdr	hdr;
	uint8_t			major;
	uint8_t			minor;
	uint8_t			spare[2];
} DStMsg_connect_t;


//
// Disconnect from the agent running on the target
//
typedef struct {
	struct DShdr	hdr;
} DStMsg_disconnect_t;


//
// Select a pid, tid for subsequent messages or query their validity
//
typedef struct {
	struct DShdr	hdr;
	int32_t			pid;
	int32_t			tid;
} DStMsg_select_t;


//
// Return information on what is at the specified virtual address.
// If nothing is there we return info on the next thing in the address.
//
typedef struct {
	struct DShdr	hdr;
	int32_t			pid;
	int32_t			addr;
} DStMsg_mapinfo_t;


//
// Load a new process into memory for a filesystem
//
typedef struct {
	struct DShdr	hdr;
	int32_t			argc;
	int32_t			envc;
	char			cmdline[DS_DATA_MAX_SIZE];
} DStMsg_load_t;


//
// Attach to an already running process
//
typedef struct {
	struct DShdr	hdr;
	int32_t			pid;
} DStMsg_attach_t;


//
// Detach from a running process which was attached to or loaded
//
typedef struct {
	struct DShdr	hdr;
	int32_t			pid;
} DStMsg_detach_t;


//
// Set a signal on a process
//
typedef struct {
	struct DShdr	hdr;
	int32_t			signo;
} DStMsg_kill_t;


//
// Stop one or more processes/threads
//
typedef struct {
	struct DShdr	hdr;
} DStMsg_stop_t;


//
// Memory read request
//
typedef struct {
	struct DShdr	hdr;
	uint32_t		spare0;
	uint64_t		addr;
	uint16_t		size;
} DStMsg_memrd_t;


//
// Memory write request
//
typedef struct {
	struct DShdr	hdr;
	uint32_t		spare0;
	uint64_t		addr;
	uint8_t			data[DS_DATA_MAX_SIZE];
} DStMsg_memwr_t;


//
// Register read request
//
typedef struct {
	struct DShdr	hdr;
	uint16_t		offset;
	uint16_t		size;
} DStMsg_regrd_t;


//
// Register write request
//
typedef struct {
	struct DShdr	hdr;
	uint16_t		offset;
	uint8_t			data[DS_DATA_MAX_SIZE];
} DStMsg_regwr_t;


//
// Run
//
typedef struct {
	struct DShdr	hdr;
	union {
		uint32_t		count;
		uint32_t		addr[2];
		} step;
} DStMsg_run_t;


//
// Break
//
typedef struct {
	struct DShdr	hdr;
	uint32_t		addr;
	uint32_t		size;
} DStMsg_brk_t;


//
// Open a file on the target
//
typedef struct {
	struct DShdr	hdr;
	int32_t			mode;
	int32_t			perms;
	char			pathname[DS_DATA_MAX_SIZE];
} DStMsg_fileopen_t;


//
// Read a file on the target
//
typedef struct {
	struct DShdr	hdr;
	uint16_t		size;
} DStMsg_filerd_t;


//
// Write a file on the target
//
typedef struct {
	struct DShdr	hdr;
	uint8_t			data[DS_DATA_MAX_SIZE];
} DStMsg_filewr_t;


//
// Close a file on the target
//
typedef struct {
	struct DShdr	hdr;
	int32_t			mtime;
} DStMsg_fileclose_t;


//
// Get pids and process names in the system
//
typedef struct {
	struct DShdr	hdr;
	int32_t		pid;	// Only valid for type subtype SPECIFIC
	int32_t		tid;	// tid to start reading from
} DStMsg_pidlist_t;


// Set current working directory of process
typedef struct {
	struct DShdr	hdr;
	uint8_t			path[DS_DATA_MAX_SIZE];
} DStMsg_cwd_t;


//
// clear, set, get environment for new process
//
typedef struct {
	struct DShdr	hdr;
	char			data[DS_DATA_MAX_SIZE];
} DStMsg_env_t;


//
// Get the base address of a process
//
typedef struct {
	struct DShdr    hdr;
} DStMsg_baseaddr_t;


//
// Send pdebug protocol version info, get the same in response_ok_status
//
typedef struct {
	struct DShdr    hdr;
	uint8_t		major;
	uint8_t		minor;
} DStMsg_protover_t;


//
// Handle signal message.
//
typedef struct {
	struct DShdr	hdr;
	uint8_t		signals[QNXNTO_NSIG];
	uint32_t	sig_to_pass;
} DStMsg_handlesig_t;


//
// Get some cpu info
//
typedef struct {
	struct DShdr	hdr;
	uint32_t	spare;
} DStMsg_cpuinfo_t;


//
// Messages sent to the host. DStMsg_* (h - for host messages)
//


//
// Notify host that something happened it needs to know about
//
#define NOTIFY_HDR_SIZE				offsetof(DShMsg_notify_t, un)
#define NOTIFY_MEMBER_SIZE(member)	sizeof(member)

// Keep this consistant with neutrino syspage.h
enum {
	CPUTYPE_X86,
	CPUTYPE_PPC,
	CPUTYPE_MIPS,
	CPUTYPE_SPARE,
	CPUTYPE_ARM,
	CPUTYPE_SH
} ;

enum {
	OSTYPE_QNX4,
	OSTYPE_NTO
} ;

typedef struct aaa {
	struct DShdr	hdr;
	int32_t			pid;
	int32_t			tid;
	union {
		struct {
			uint32_t		codeoff;
			uint32_t		dataoff;
			uint16_t		ostype;
			uint16_t		cputype;
			uint32_t		cpuid;		// CPU dependant value
			char			name[DS_DATA_MAX_SIZE];
			}		pidload;
		struct {
			int32_t			status;
			}		pidunload;
		struct {
			int32_t			status;
			uint8_t			faulted;
			uint8_t			reserved[3];
			}		pidunload_v3;
		struct {
			uint32_t		ip;
			uint32_t		dp;
			uint32_t		flags;	// defined in <sys/debug.h>
			}		brk;
		struct {
			uint32_t		ip;
			uint32_t		lastip;
			}		step;
		struct {
			int32_t			signo;
			int32_t			code;
			int32_t			value;
			}		sigev;
	} un;
} DShMsg_notify_t;
 


//
// Responses to a message. DSrMsg_* (r - for response messages)
//

//
// Error response packet
//
typedef struct {
	struct DShdr	hdr;
	int32_t			err;
} DSrMsg_err_t;

//
// Simple OK response. 
//
typedef struct {
	struct DShdr	hdr;
} DSrMsg_ok_t;


//
// Simple OK response with a result. Used where limited data needs
// to be returned. For example, if the number of bytes which were
// successfully written was less than requested on any write cmd the
// status will be the number actually written.
// The 'subcmd' will always be zero.
//
typedef struct {
	struct DShdr	hdr;
	int32_t			status;
} DSrMsg_okstatus_t;


//
// The following structures overlay data[..] on a DSrMsg_okdata_t
//

struct dslinkmap {
    uint32_t        addr;
    uint32_t        size;
    uint32_t        flags;
    uint32_t        debug_vaddr;
    uint64_t		offset;			
};

struct dsmapinfo {
    struct dsmapinfo		*next;
    uint32_t				spare0;
    uint64_t           		ino;
    uint32_t           		dev;
    uint32_t				spare1;
    struct dslinkmap		text;
    struct dslinkmap		data;
    char            		name[256];
};

struct dspidlist {
	int32_t			pid;
	int32_t			num_tids;  // num of threads this pid has
	int32_t			spare[6];
	struct tidinfo {
		int16_t		tid;
		uint8_t		state;
		uint8_t		flags;
		} tids[1];				// Variable length terminated by tid==0
	char			name[1];	// Variable length terminated by \0
} ;

struct dscpuinfo{
	uint32_t	cpuflags;
	uint32_t	spare1;
	uint32_t	spare2;
	uint32_t	spare3;
};

//
// Long OK response with 0..DS_DATA_MAX_SIZE data.
// The 'subcmd' will always be zero.
//
typedef struct {
	struct DShdr	hdr;
	uint8_t			data[DS_DATA_MAX_SIZE];
} DSrMsg_okdata_t;


//
// A union of all possible messages and responses.
//
typedef union {
	struct DShdr				hdr;
	DStMsg_connect_t			connect;
	DStMsg_disconnect_t			disconnect;
	DStMsg_select_t				select;
	DStMsg_load_t				load;
	DStMsg_attach_t				attach;
	DStMsg_detach_t				detach;
	DStMsg_kill_t				kill;
	DStMsg_stop_t				stop;
	DStMsg_memrd_t				memrd;
	DStMsg_memwr_t				memwr;
	DStMsg_regrd_t				regrd;
	DStMsg_regwr_t				regwr;
	DStMsg_run_t				run;
	DStMsg_brk_t				brk;
	DStMsg_fileopen_t			fileopen;
	DStMsg_filerd_t				filerd;
	DStMsg_filewr_t				filewr;
	DStMsg_fileclose_t			fileclose;
	DStMsg_pidlist_t			pidlist;
	DStMsg_mapinfo_t			mapinfo;
	DStMsg_cwd_t				cwd;
	DStMsg_env_t				env;
	DStMsg_baseaddr_t			baseaddr;
	DStMsg_protover_t			protover;
	DStMsg_handlesig_t			handlesig;
	DStMsg_cpuinfo_t			cpuinfo;
	DShMsg_notify_t				notify;
	DSrMsg_err_t				err;
	DSrMsg_ok_t					ok;
	DSrMsg_okstatus_t			okstatus;
	DSrMsg_okdata_t				okdata;
} DSMsg_union_t;


 


///////////////////////////////////////////////////////////////////////////////
// Text channel Messages:   TS - Text services
//

#define TS_TEXT_MAX_SIZE	100

//
// Command types
//
enum {
	TSMsg_text,			/*  0 */
	TSMsg_done,			/*  1 */
	TSMsg_start,		/*  2 */
	TSMsg_stop,			/*  3 */
	TSMsg_ack,			/*  4 */
} ;


struct TShdr {
	uint8_t		cmd;
	uint8_t		console;
	uint8_t		spare1;
	uint8_t		channel;
} ;


//
// Deliver text. This message can be sent by either side.
// The debugger displays it in a window. The agent gives it to a pty
// which a program may be listening on.
//

typedef struct {
	struct TShdr	hdr;
	char			text[TS_TEXT_MAX_SIZE];
} TSMsg_text_t;


//
// There is no longer a program connected to this console.
//
typedef struct {
	struct TShdr	hdr;
} TSMsg_done_t;


//
// TextStart or TextStop flow control�
//
typedef struct {
    struct TShdr    hdr;
} TSMsg_flowctl_t;


//
// Ack a flowctl message
//
typedef struct {
    struct TShdr    hdr;
} TSMsg_ack_t;

#endif

