#ifndef pdbgerr_h_included
#define pdbgerr_h_included

//
// These are pdebug specific errors, sent sometimes with the errno after
// an action failed.  Simply provides additional info on the reason for the 
// error.  Sent in the DSrMsg_err_t.hdr.subcmd byte.
//

#define PDEBUG_ENOERR		0	/* No error									*/
#define PDEBUG_ENOPTY		1	/* No Pseudo Terminals found				*/
#define PDEBUG_ETHREAD		2	/* Thread Create error						*/
#define PDEBUG_ECONINV		3	/* Invalid Console number 					*/
#define PDEBUG_ESPAWN		4	/* Spawn error								*/
#define PDEBUG_EPROCFS		5	/* NTO Proc File System error				*/
#define PDEBUG_EPROCSTOP	6	/* NTO Process Stop error					*/
#define PDEBUG_EQPSINFO		7	/* QNX4 PSINFO error						*/
#define PDEBUG_EQMEMMODEL	8	/* QNX4 - Flat Memory Model only supported	*/
#define PDEBUG_EQPROXY		9	/* QNX4 Proxy error							*/
#define PDEBUG_EQDBG		10	/* QNX4 qnx_debug_* error					*/

// 
//  There is room for pdebug_errnos up to sizeof(uint8_t)
//

#endif

