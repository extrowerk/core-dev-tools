// -*- C++ -*- std::terminate, std::unexpected and friends.
// Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001 
// Free Software Foundation
//
// This file is part of GNU CC.
//
// GNU CC is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// GNU CC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with GNU CC; see the file COPYING.  If not, write to
// the Free Software Foundation, 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA. 

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

// This is a copy of eh_terminate.cc, with the 'std::' declarations removed.
// This is to facilitate supporting Dinkumware libcpp 4.0.2 and gcc-3.2.3
// by building a 'libcxa.a' without the symbols that conflict with the
// Dinkum libcpp.
//
// GP QNX, May 20, 2003.

#include "typeinfo"
#include "exception"
#include <cstdlib>
#include "unwind-cxx.h"
#include "exception_defines.h"

using namespace __cxxabiv1;

/* The current installed user handlers.  */
std::terminate_handler __cxxabiv1::__terminate_handler = std::abort;
std::unexpected_handler __cxxabiv1::__unexpected_handler = std::terminate;

void
__cxxabiv1::__terminate (std::terminate_handler handler)
{
  try {
    handler ();
    std::abort ();
  } catch (...) {
    std::abort ();
  }
}

void
__cxxabiv1::__unexpected (std::unexpected_handler handler)
{
  handler();
  std::terminate ();
}

