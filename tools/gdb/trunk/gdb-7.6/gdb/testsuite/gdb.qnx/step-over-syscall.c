/* This testcase is part of GDB, the GNU debugger.

   Copyright 2012 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


#include <stddef.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <sys/iomsg.h>

int
main ()
{
  int err;
  struct read_offset {
    struct _io_read read;
    struct _xtype_offset offset;
  } msg;

  char buff[1000];

  /* Make system call known to fail. */
  memset(&msg, 0x00, sizeof msg);

  msg.read.type = _IO_READ;
  msg.read.combine_len = 10;
  msg.read.nbytes = 4;
  msg.read.xtype = _IO_XTYPE_OFFSET;
  msg.read.zero = 0;
  msg.offset.offset = 0;

  err = MsgSend(-1, &msg, 30, buff, 4);  /* 1 breakpoint here */
  /* err should be != 0 */
  if (err == 0) /* 2 breakpoint here */
    {
      write(STDERR_FILENO, "BAD TEST\n", 9);
    }
  return (err != 0);
}
