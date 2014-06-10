//===-- sanitizer_nto.cc --------------------------------------------------===//
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries and implements linux-specific functions from
// sanitizer_libc.h.
//===----------------------------------------------------------------------===//
#ifdef __QNXNTO__

#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_libc.h"
#include "sanitizer_mutex.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_procmaps.h"
#include "sanitizer_stacktrace.h"

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unwind.h>
#include <sys/procfs.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <devctl.h>
#include <sys/iomsg.h>
#include <sys/memmsg.h>
#include <share.h>

#define LOWEST_FD (3)

namespace __sanitizer {

// --------------- sanitizer_libc.h
void *internal_mmap(void *addr, uptr length, int prot, int flags,
                    int fd, u64 offset) {
  mem_map_t msg;

  msg.i.type = _MEM_MAP;
  msg.i.zero = 0;
  msg.i.addr = (uintptr_t)addr;
  msg.i.len = length;
  msg.i.prot = prot;
  msg.i.flags = flags;
  msg.i.fd = fd;
  msg.i.offset = offset;
  msg.i.align = 0;
  msg.i.preload = 0;
  msg.i.reserved1 = 0;

  if (MsgSendnc(MEMMGR_COID, &msg.i, sizeof(msg.i), &msg.o,
                sizeof(msg.o)) == -1) {
    return MAP_FAILED;
  }

  return (void *)(uintptr_t)msg.o.addr;
}

int internal_munmap(void *addr, uptr length) {
  mem_ctrl_t msg;

  msg.i.type = _MEM_CTRL;
  msg.i.subtype = _MEM_CTRL_UNMAP;
  msg.i.addr = (uintptr_t)addr;
  msg.i.len = length;
  msg.i.flags = 0;

  return MsgSendnc(MEMMGR_COID, &msg.i, sizeof(msg.i), 0, 0);
}

int internal_close(fd_t fd) {
  io_close_t msg;
  int ret, err;

  msg.i.type = _IO_CLOSE;
  msg.i.combine_len = sizeof(msg.i);
  for (;;) {
    int ret = MsgSend(fd, &msg.i, sizeof(msg.i), 0, 0);
    if (ret != -1 || errno != EINTR) {
      if (ConnectDetach_r(fd) == EOK) {
        return ret;
      }
    }
  }
  return -1;
}

fd_t internal_open(const char *filename, int flags) {
  internal_open(filename, flags, 0);
}

fd_t internal_open(const char *filename, int flags, u32 mode) {
  flags |= O_LARGEFILE;

  return _connect(LOWEST_FD, filename, mode, flags, SH_DENYNO, _IO_CONNECT_OPEN,
                  1, _IO_FLAG_RD|_IO_FLAG_WR, 0, 0, 0, 0, 0, 0, 0);
}

fd_t OpenFile(const char *filename, bool write) {
  return internal_open(filename,
      write ? O_WRONLY | O_CREAT : O_RDONLY, 0660);
}

uptr internal_read(fd_t fd, void *buf, uptr count) {
  io_read_t msg;

  msg.i.type = _IO_READ;
  msg.i.combine_len = sizeof msg.i;
  msg.i.nbytes = count;
  msg.i.xtype = _IO_XTYPE_NONE;
  msg.i.zero = 0;

  return MsgSend(fd, &msg.i, sizeof msg.i, buf, count);
}

uptr internal_write(fd_t fd, const void *buf, uptr count) {
  io_write_t msg;
  iov_t iov[2];

  msg.i.type = _IO_WRITE;
  msg.i.combine_len = sizeof(msg.i);
  msg.i.xtype = _IO_XTYPE_NONE;
  msg.i.nbytes = count;
  msg.i.zero = 0;
  SETIOV(iov + 0, &msg.i, sizeof(msg.i));
  SETIOV(iov + 1, buf, count);

  return MsgSendv(fd, iov, 2, 0, 0);
}

int internal_stat(const char *path, void *buf) {
  struct _io_stat s;
  int oflag = O_NONBLOCK|O_NOCTTY|O_LARGEFILE;

  s.type = _IO_STAT;
  s.combine_len = sizeof(s);
  s.zero = 0;

  if (_connect_combine(path, 0, oflag, SH_DENYNO, 0, 0,
                       sizeof(s), &s, sizeof(struct stat), buf) == -1) {
    return -1;
  }

  return 0;
}

int internal_lstat(const char *path, void *buf) {
  struct _io_stat s;
  int oflag = O_NONBLOCK|O_NOCTTY|O_LARGEFILE;

  s.type = _IO_STAT;
  s.combine_len = sizeof(s);
  s.zero = 0;

  if (_connect_combine(path, S_IFLNK, oflag, SH_DENYNO, 0, 0,
                       sizeof(s), &s, sizeof(struct stat), buf) == -1) {
    return -1;
  }

  return 0;
}

int internal_fstat(fd_t fd, void *buf) {
  io_stat_t msg;

  msg.i.type = _IO_STAT;
  msg.i.combine_len = sizeof(msg.i);
  msg.i.zero = 0;

  if (MsgSendnc(fd, &msg.i, sizeof(msg.i), buf, sizeof(struct stat)) == -1) {
    return(-1);
  }

  return 0;
}

uptr internal_filesize(fd_t fd) {
  struct stat st;
  if (internal_fstat(fd, &st)) {
    return -1;
  }
  return (uptr)st.st_size;
}

int internal_dup2(int oldfd, int newfd) {
  UNIMPLEMENTED();
}

uptr internal_readlink(const char *path, char *buf, uptr bufsize) {
  UNIMPLEMENTED();
}

int internal_sched_yield() {
  return SchedYield();
}

void internal__exit(int exitcode) {
  ThreadDestroy(-1, -1, (void *)(intptr_t)exitcode);
}

// ----------------- sanitizer_common.h
bool FileExists(const char *filename) {
  struct stat st;
  if (stat(filename, &st))
    return false;
  // Sanity check: filename is a regular file.
  return S_ISREG(st.st_mode);
}

uptr GetTid() {
  return static_cast<uptr>(pthread_self());
}

void GetThreadStackTopAndBottom(bool at_initialization, uptr *stack_top,
                                uptr *stack_bottom) {
  *stack_top = RoundUpTo(reinterpret_cast<uptr>(__tls()), GetPageSizeCached());
  *stack_bottom = reinterpret_cast<uptr>(__tls()->__stackaddr);
}

const char *GetEnv(const char *name) {
  char** env = environ;
  uptr name_len = internal_strlen(name);
  while (*env != 0) {
    uptr len = internal_strlen(*env);
    if (len > name_len) {
      const char *p = *env;
      if (!internal_memcmp(p, name, name_len) &&
        p[name_len] == '=') {  // Match.
        return *env + name_len + 1;  // String starting after =.
      }
    }
    env++;
  }
  return NULL;
}

void PrepareForSandboxing() {
}

// ----------------- sanitizer_procmaps.h
MemoryMappingLayout::MemoryMappingLayout() {
  maps_ = NULL;
  Reset();
}

MemoryMappingLayout::~MemoryMappingLayout() {
  if (maps_) {
      internal_munmap(maps_, nmaps_*sizeof(*maps_));
  };
}

void MemoryMappingLayout::Reset() {
  int n;
  int fd = -1;
  procfs_mapinfo *maps = NULL;
  size_t maps_len = 0;

  if (maps_) {
      internal_munmap(maps_, nmaps_*sizeof(*maps_));
  }
  maps_ = NULL;
  nmaps_ = 0;
  idx_ = 0;

  fd = internal_open("/proc/self/as", O_RDONLY);
  if (fd == -1) {
    goto cleanup;
  }

  if (devctl(fd, DCMD_PROC_MAPINFO, NULL, 0, &n) != EOK) {
    goto cleanup;
  }

  maps_len = sizeof(procfs_mapinfo) * n;
  maps = reinterpret_cast<procfs_mapinfo*>(
              internal_mmap(0, maps_len, PROT_READ|PROT_WRITE,
                            MAP_PRIVATE|MAP_ANON, NOFD, 0));
  if (maps == MAP_FAILED) {
    maps = NULL;
    goto cleanup;
  }

  int dummy;
  if (devctl(fd, DCMD_PROC_MAPINFO, maps, maps_len, &dummy) != EOK) {
    goto cleanup;
  }

  maps_ = reinterpret_cast<typeof(maps_)>(
              internal_mmap(0, n*sizeof(*maps_), PROT_READ|PROT_WRITE,
                            MAP_PRIVATE|MAP_ANON, NOFD, 0));
  if (maps_ == MAP_FAILED) {
    goto cleanup;
  }
  nmaps_ = n;

  for (u32 i=0; i<nmaps_; i++) {
    struct {
      procfs_debuginfo dinfo;
      char buff[_POSIX_PATH_MAX];
    } info;

    info.dinfo.vaddr = maps[i].vaddr;
    if (devctl(fd, DCMD_PROC_MAPDEBUG, &info, sizeof(info), NULL) == EOK) {
      strlcpy(maps_[i].name, info.dinfo.path, sizeof(maps_[i].name));
    }
    maps_[i].start = maps[i].vaddr;
    maps_[i].end = maps[i].vaddr + maps[i].size-1;
    maps_[i].offset = maps[i].offset;
  }

cleanup:
  if (maps) {
    internal_munmap(maps, maps_len);
  }
  if (fd != -1) {
    internal_close(fd);
  }
}

bool MemoryMappingLayout::Next(uptr *start, uptr *end, uptr *offset,
                               char filename[], uptr filename_size) {
    if (maps_ == NULL || idx_ >= nmaps_) {
        return false;
    }

    if (start) {
        *start = maps_[idx_].start;
    }
    if (end) {
        *end = maps_[idx_].end;
    }
    if (offset) {
    *offset = maps_[idx_].offset;
    }
    if (filename) {
        strlcpy(filename, maps_[idx_].name, filename_size);
    }

    idx_++;

    return true;
}

bool MemoryMappingLayout::GetObjectNameAndOffset(uptr addr, uptr *offset,
                                                 char filename[],
                                                 uptr filename_size) {
  return IterateForObjectNameAndOffset(addr, offset, filename, filename_size);
}


//------------------------- SlowUnwindStack -----------------------------------
#ifdef __arm__
#define UNWIND_STOP _URC_END_OF_STACK
#define UNWIND_CONTINUE _URC_NO_REASON
#else
#define UNWIND_STOP _URC_NORMAL_STOP
#define UNWIND_CONTINUE _URC_NO_REASON
#endif

uptr Unwind_GetIP(struct _Unwind_Context *ctx) {
#ifdef __arm__
  uptr val;
  _Unwind_VRS_Result res = _Unwind_VRS_Get(ctx, _UVRSC_CORE,
      15 /* r15 = PC */, _UVRSD_UINT32, &val);
  CHECK(res == _UVRSR_OK && "_Unwind_VRS_Get failed");
  // Clear the Thumb bit.
  return val & ~(uptr)1;
#else
  return _Unwind_GetIP(ctx);
#endif
}

_Unwind_Reason_Code Unwind_Trace(struct _Unwind_Context *ctx, void *param) {
  StackTrace *b = (StackTrace*)param;
  CHECK(b->size < b->max_size);
  uptr pc = Unwind_GetIP(ctx);
  b->trace[b->size++] = pc;
  if (b->size == b->max_size) return UNWIND_STOP;
  return UNWIND_CONTINUE;
}

static bool MatchPc(uptr cur_pc, uptr trace_pc) {
  return cur_pc - trace_pc <= 64 || trace_pc - cur_pc <= 64;
}

void StackTrace::SlowUnwindStack(uptr pc, uptr max_depth) {
  this->size = 0;
  this->max_size = max_depth;
  if (max_depth > 1) {
    _Unwind_Backtrace(Unwind_Trace, this);
    // We need to pop a few frames so that pc is on top.
    // trace[0] belongs to the current function so we always pop it.
    int to_pop = 1;
    /**/ if (size > 1 && MatchPc(pc, trace[1])) to_pop = 1;
    else if (size > 2 && MatchPc(pc, trace[2])) to_pop = 2;
    else if (size > 3 && MatchPc(pc, trace[3])) to_pop = 3;
    else if (size > 4 && MatchPc(pc, trace[4])) to_pop = 4;
    else if (size > 5 && MatchPc(pc, trace[5])) to_pop = 5;
    this->PopStackFrames(to_pop);
  }
  this->trace[0] = pc;
}

BlockingMutex::BlockingMutex(LinkerInitialized) {
  CHECK_EQ(owner_, 0);
  CHECK(sizeof(pthread_mutex_t) <= sizeof(opaque_storage_));
  pthread_mutex_init(reinterpret_cast<pthread_mutex_t*>(&opaque_storage_), NULL);
}

void BlockingMutex::Lock() {
  pthread_mutex_lock(reinterpret_cast<pthread_mutex_t*>(&opaque_storage_));
  owner_ = pthread_self();
}

void BlockingMutex::Unlock() {
  owner_ = 0;
  pthread_mutex_unlock(reinterpret_cast<pthread_mutex_t*>(&opaque_storage_));
}

}  // namespace __sanitizer

#endif  // __QNXNTO__
