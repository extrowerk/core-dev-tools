/* Definitions for specs for C++.
   Copyright (C) 1995-2014 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* This is the contribution to the `default_compilers' array in gcc.c for
   g++.  */

#ifndef CPLUSPLUS_CPP_SPEC
#define CPLUSPLUS_CPP_SPEC 0
#endif

  {".cc",  "@c++", 0, 0, 0},
  {".cp",  "@c++", 0, 0, 0},
  {".cxx", "@c++", 0, 0, 0},
  {".cpp", "@c++", 0, 0, 0},
  {".c++", "@c++", 0, 0, 0},
  {".C",   "@c++", 0, 0, 0},
  {".CPP", "@c++", 0, 0, 0},
  {".H",   "@c++-header", 0, 0, 0},
  {".hpp", "@c++-header", 0, 0, 0},
  {".hp",  "@c++-header", 0, 0, 0},
  {".hxx", "@c++-header", 0, 0, 0},
  {".h++", "@c++-header", 0, 0, 0},
  {".HPP", "@c++-header", 0, 0, 0},
  {".tcc", "@c++-header", 0, 0, 0},
  {".hh",  "@c++-header", 0, 0, 0},
  {"@c++-header",
    "%{E|M|MM:cc1plus -E %(cpp_options) %2 %(cpp_debug_options)}\
     %{!E:%{!M:%{!MM:\
       %{save-temps*|no-integrated-cpp:cc1plus -E\
		%(cpp_options) %2 -o %{save-temps*:%b.ii} %{!save-temps*:%g.ii} \n}\
      cc1plus %{save-temps*|no-integrated-cpp:-fpreprocessed %{save-temps*:%b.ii} %{!save-temps*:%g.ii}}\
	      %{!save-temps*:%{!no-integrated-cpp:%(cpp_unique_options)}}\
	%(cc1_options) %(ssp_default) %2\
	%{!fsyntax-only:%{!fdump-ada-spec*:-o %g.s %{!o*:--output-pch=%i.gch}\
        %W{o*:--output-pch=%*}}%V}}}}",
     CPLUSPLUS_CPP_SPEC, 0, 0},
  {"@c++",
    "%{E|M|MM:cc1plus -E %(cpp_options) %2 %(cpp_debug_options)}\
     %{!E:%{!M:%{!MM:\
       %{save-temps*|no-integrated-cpp:cc1plus -E\
		%(cpp_options) %2 -o %{save-temps*:%b.ii} %{!save-temps*:%g.ii} \n}\
      cc1plus %{save-temps*|no-integrated-cpp:-fpreprocessed %{save-temps*:%b.ii} %{!save-temps*:%g.ii}}\
	      %{!save-temps*:%{!no-integrated-cpp:%(cpp_unique_options)}}\
	%(cc1_options) %(ssp_default) %2\
       %{!fsyntax-only:%(invoke_as)}}}}",
     CPLUSPLUS_CPP_SPEC, 0, 0},
  {".ii", "@c++-cpp-output", 0, 0, 0},
  {"@c++-cpp-output",
   "%{!M:%{!MM:%{!E:\
    cc1plus -fpreprocessed %i %(cc1_options) %(ssp_default) %2\
    %{!fsyntax-only:%(invoke_as)}}}}", 0, 0, 0},
