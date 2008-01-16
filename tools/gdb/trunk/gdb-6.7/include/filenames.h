/* Macros for taking apart, interpreting and processing file names.

   These are here because some non-Posix (a.k.a. DOSish) systems have
   drive letter brain-damage at the beginning of an absolute file name,
   use forward- and back-slash in path names interchangeably, and
   some of them have case-insensitive file names.

   Copyright 2000, 2001, 2007 Free Software Foundation, Inc.

This file is part of BFD, the Binary File Descriptor library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef FILENAMES_H
#define FILENAMES_H

/* For DOS style paths.  */

#define IS_DIR_SEPARATOR_DOS(c)	  ((c) == '/' || (c) == '\\')

/* IS_ABSOLUTE_PATH_DOS behaves as IS_DIR_SEPARATOR macro when 
  configured for DOS-like file system (see comment for 
  IS_ABSOLUTE_PATH below).  */

#define IS_ABSOLUTE_PATH_DOS(f)	  (IS_DIR_SEPARATOR_DOS((f)[0]) || (((f)[0]) && ((f)[1] == ':')))

/* For POSIX style paths.  */

#define IS_DIR_SEPARATOR_POSIX(c)	  ((c) == '/')
#define IS_ABSOLUTE_PATH_POSIX(f)	  (IS_DIR_SEPARATOR_POSIX((f)[0]))

#if defined(__MSDOS__) || defined(_WIN32) || defined(__OS2__) || defined (__CYGWIN__)

#ifndef HAVE_DOS_BASED_FILE_SYSTEM
#define HAVE_DOS_BASED_FILE_SYSTEM 1
#endif

#define IS_DIR_SEPARATOR(c)	((c) == '/' || (c) == '\\')
/* Note that IS_ABSOLUTE_PATH accepts d:foo as well, although it is
   only semi-absolute.  This is because the users of IS_ABSOLUTE_PATH
   want to know whether to prepend the current working directory to
   a file name, which should not be done with a name like d:foo.  */
//#define IS_ABSOLUTE_PATH(f)	(IS_DIR_SEPARATOR((f)[0]) || (((f)[0]) && ((f)[1] == ':')))

#else  /* not DOSish */

#define IS_DIR_SEPARATOR(c)	((c) == '/')
// #define IS_ABSOLUTE_PATH(f)	(IS_DIR_SEPARATOR((f)[0]))

#endif /* not DOSish */

extern int qnx_filename_cmp (const char *s1, const char *s2);
extern int filename_cmp (const char *s1, const char *s2);
#define FILENAME_CMP(s1, s2)	qnx_filename_cmp(s1, s2)

extern int is_absolute_path (const char *path);
#define IS_ABSOLUTE_PATH(f) is_absolute_path(f)
#endif /* FILENAMES_H */
