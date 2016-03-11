/* Get previous frame state for an existing frame state.
   Copyright (C) 2016 The Qt Company Ltd.
   This file is part of elfutils.

   This file is free software; you can redistribute it and/or modify
   it under the terms of either

     * the GNU Lesser General Public License as published by the Free
       Software Foundation; either version 3 of the License, or (at
       your option) any later version

   or

     * the GNU General Public License as published by the Free
       Software Foundation; either version 2 of the License, or (at
       your option) any later version

   or both in parallel, as here.

   elfutils is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received copies of the GNU General Public License and
   the GNU Lesser General Public License along with this program.  If
   not, see <http://www.gnu.org/licenses/>.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <assert.h>

#define BACKEND x86_64_
#include "libebl_CPU.h"

/* There was no CFI. Maybe we happen to have a frame pointer and can unwind from that?  */

bool
x86_64_unwind (Ebl *ebl __attribute__ ((unused)), Dwarf_Addr pc __attribute__ ((unused)),
         ebl_tid_registers_t *setfunc, ebl_tid_registers_get_t *getfunc,
         ebl_pid_memory_read_t *readfunc, void *arg, bool *signal_framep)
{
  // Register 6 is supposed to be rbp, thus the conventional frame pointer
  const int fpReg = 6;
  const int spReg = 7;

  Dwarf_Word fp;
  if (!getfunc(fpReg, 1, &fp, arg))
    return false;

  Dwarf_Word prev_fp;
  if (!readfunc(fp, &prev_fp, arg))
    return false;

  Dwarf_Word ret;
  if (!readfunc(fp + 8, &ret, arg))
    return false;

  if (!setfunc(fpReg, 1, &prev_fp, arg))
    return false;

  fp += 16;
  if (!setfunc(spReg, 1, &fp, arg))
    return false;

  if (!setfunc(-1, 1, &ret, arg))
    return false;

  *signal_framep = false; // Can we actually have a signal frame here?

  return true;
}
