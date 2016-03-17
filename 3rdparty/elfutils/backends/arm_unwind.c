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

#define BACKEND arm_
#include "libebl_CPU.h"
#include <stdio.h>

/* There was no CFI. Maybe we happen to have a frame pointer and can unwind from that?  */

bool
arm_unwind (Ebl *ebl __attribute__ ((unused)), Dwarf_Addr pc,
         ebl_tid_registers_t *setfunc, ebl_tid_registers_get_t *getfunc,
         ebl_pid_memory_read_t *readfunc, void *arg, bool *signal_framep)
{
  // There is no really clear convention about frame pointers on ARM. QV4 uses r7 for THUMB and r11
  // for ARM mode. We pray that we get the mode encoded into the pc from the last bl(x) instruction.
  const int fpReg = (pc & 1) ? 7 : 11;
  const int lrReg = 14;
  const int spReg = 13;

  Dwarf_Word fp = 0, lr = 0, sp = 0; // have to be initialized because registers are 32bit only
  if (!getfunc(fpReg, 1, &fp, arg) || fp == 0)
      return false;
  if (!getfunc(lrReg, 1, &lr, arg))
      return false;
  if (!getfunc(spReg, 1, &sp, arg))
      return false;

  Dwarf_Word newFp; // no initialization needed because we read 64bit
  if (!readfunc(fp, &newFp, arg))
      return false;
  Dwarf_Word newLr = newFp >> 32;
  newFp &= 0xffffffff;

  fp += 8;
  if (!setfunc(spReg, 1, &fp, arg))
      return false;
  if (!setfunc(fpReg, 1, &newFp, arg))
      return false;
  if (!setfunc(-1, 1, &newLr, arg))
      return false;

  // unset the "thumb" bit. We get LR without thumb bit, so let's also pass it on that way.
  newLr &= 0xfffffffe;
  if (!setfunc(lrReg, 1, &newLr, arg))
      return false;

  *signal_framep = false;
  return true;
}
