/****************************************************************************
 *   Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
 *   gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License in the files COPYING and COPYING.LESSER.  If not, see           *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#ifndef _SPLIT_PROCESS_H
#define _SPLIT_PROCESS_H

#include <string.h>
#include <pthread.h>
#include <stdio.h>

#include "jassert.h"
#include "lower_half_api.h"

#define SET_FS_CONTEXT

// Helper class to save and restore context (in particular, the FS register),
// when switching between the upper half and the lower half. In the current
// design, the caller would generally be the upper half, trying to jump into
// the lower half. An example would be calling a real function defined in the
// lower half from a function wrapper defined in the upper half.
// Example usage:
//   int function_wrapper()
//   {
//     SwitchContext ctx;
//     return _real_function();
//   }
// The idea is to leverage the C++ language semantics to help us automatically
// restore the context when the object goes out of scope.
class SwitchContext
{
  private:
    unsigned long upperHalfFs; // The base value of the FS register of the upper half thread
    unsigned long lowerHalfFs; // The base value of the FS register of the lower half

  public:
    // Saves the current FS register value to 'upperHalfFs' and then
    // changes the value of the FS register to the given 'lowerHalfFs'
    explicit SwitchContext(unsigned long );

    // Restores the FS register value to 'upperHalfFs'
    ~SwitchContext();
};

#ifndef SET_FS_CONTEXT
// ===================================================
// FIXME: Fix this after the Linux 5.9 FSGSBASE patch
// Helper macro to be used whenever making a jump from the upper half to
// the lower half.
#define JUMP_TO_LOWER_HALF(lhFs) \
  do { \
    SwitchContext ctx((unsigned long)lhFs)

// Helper macro to be used whenever making a returning from the lower half to
// the upper half.
#define RETURN_TO_UPPER_HALF(__func__) \
  } while (0)
#endif
// ===================================================
// Workaround for quickly changing the fs address
static void* lh_fsaddr;
static void *uh_fsaddr;
static int fsaddr_initialized = 0;
// in glibc 2.26 for x86_64
// typedef struct
// {
//   void *tcb;            /* Pointer to the TCB.  Not necessarily the
//                            thread descriptor used by libpthread.  */
//   dtv_t *dtv;
//   void *self;           /* Pointer to the thread descriptor.  */
//   int multiple_threads;
//   int gscope_flag;
//   uintptr_t sysinfo;
//   uintptr_t stack_guard;
//   uintptr_t pointer_guard;
//   unsigned long int vgetcpu_cache[2];
// # ifndef __ASSUME_PRIVATE_FUTEX
//   int private_futex;
// # else
//   int __glibc_reserved1;
// # endif
//   int __glibc_unused1;
//   /* Reservation of some values for the TM ABI.  */
//   void *__private_tm[4];
//   /* GCC split stack support.  */
//   void *__private_ss;
//   long int __glibc_reserved2;
//   /* Must be kept even if it is no longer used by glibc since programs,
//      like AddressSanitizer, depend on the size of tcbhead_t.  */
//   __128bits __glibc_unused2[8][4] __attribute__ ((aligned (32)));
//
//   void *__padding[8];
// } tcbhead_t
// TODO: make this configuable
#ifndef LH_TLS_SIZE
/* readelf -S lh_proxy
 * [14] .tdata            PROGBITS         000000000e64d500  0044d500
 *      000000000000002c  0000000000000000 WAT       0     0     8
 * [15] .tbss             NOBITS           000000000e64d530  0044d52c
 *      0000000000000462  0000000000000000 WAT       0     0     8
 */
#define LH_TLS_SIZE 0xcc0
#endif
static const size_t TCB_HEADER_SIZE = 120; // offset of __glibc_reserved2

static char fsaddr_buf[LH_TLS_SIZE + TCB_HEADER_SIZE];
static char debug_buf[LH_TLS_SIZE + TCB_HEADER_SIZE];

#ifdef SET_FS_CONTEXT
struct dtv_pointer
{
  void *val;                    /* Pointer to data, or TLS_DTV_UNALLOCATED.  */
  void *to_free;                /* Unaligned pointer, for deallocation.  */
};

/* Type for the dtv.  */
typedef union dtv
{
  size_t counter;
  struct dtv_pointer pointer;
} dtv_t;

typedef struct
{
  void *tcb;		/* Pointer to the TCB.  Not necessarily the
			   thread descriptor used by libpthread.  */
  dtv_t *dtv;
  void *self;		/* Pointer to the thread descriptor.  */
  int multiple_threads;
  int gscope_flag;
  uintptr_t sysinfo;
  uintptr_t stack_guard;
  uintptr_t pointer_guard;
  unsigned long int vgetcpu_cache[2];
  int __glibc_reserved1;
  int __glibc_unused1;
  /* Reservation of some values for the TM ABI.  */
  void *__private_tm[4];
  /* GCC split stack support.  */
  void *__private_ss;
  long int __glibc_reserved2;
  /* Must be kept even if it is no longer used by glibc since programs,
     like AddressSanitizer, depend on the size of tcbhead_t.  */
  //__128bits __glibc_unused2[8][4] __attribute__ ((aligned (32)));

  //void *__padding[8];
} tcbhead_t;


static inline void JUMP_TO_LOWER_HALF(void *lhFs) {
  // Compute the upper-half and lower-half fs addresses
  if (!fsaddr_initialized) {
    fsaddr_initialized = 1;
    printf("%d %d %p %p %p %lx\n", __LINE__, getpid(), lh_fsaddr, lh_info.fsaddr, uh_fsaddr, pthread_self());
    lh_fsaddr = lh_info.fsaddr - LH_TLS_SIZE;
    uh_fsaddr = (char*)pthread_self() - LH_TLS_SIZE;
    printf("%d %d %p %p %p %lx\n", __LINE__, getpid(), lh_fsaddr, lh_info.fsaddr, uh_fsaddr, pthread_self());
    fflush(stdout);
tcbhead_t *uhhead = (tcbhead_t *)(uh_fsaddr + LH_TLS_SIZE);
tcbhead_t *lhhead = (tcbhead_t *)(lh_fsaddr + LH_TLS_SIZE);
    printf("%d %d %p %p %p %d %x %lx %lx %lx %lu %lu %d %d %p %p %p %p %p\n", __LINE__, getpid(),
lhhead->tcb, lhhead->dtv, lhhead->self, lhhead->multiple_threads, lhhead->gscope_flag, lhhead->sysinfo, lhhead->stack_guard, lhhead->pointer_guard, lhhead->vgetcpu_cache[0], lhhead->vgetcpu_cache[1],
lhhead->__glibc_reserved1, lhhead->__glibc_unused1, lhhead->__private_tm[0], lhhead->__private_tm[1], lhhead->__private_tm[2], lhhead->__private_tm[3], lhhead->__private_ss);
		  fflush(stdout);
    printf("%d %d %p %p %p %d %x %lx %lx %lx %lu %lu %d %d %p %p %p %p %p\n", __LINE__, getpid(),
uhhead->tcb, uhhead->dtv, uhhead->self, uhhead->multiple_threads, uhhead->gscope_flag, uhhead->sysinfo, uhhead->stack_guard, uhhead->pointer_guard, uhhead->vgetcpu_cache[0], uhhead->vgetcpu_cache[1],
uhhead->__glibc_reserved1, uhhead->__glibc_unused1, uhhead->__private_tm[0], uhhead->__private_tm[1], uhhead->__private_tm[2], uhhead->__private_tm[3], uhhead->__private_ss);
		  fflush(stdout);

  }
  memcpy(fsaddr_buf, uh_fsaddr, LH_TLS_SIZE + TCB_HEADER_SIZE);
  memcpy(uh_fsaddr, lh_fsaddr, LH_TLS_SIZE + TCB_HEADER_SIZE);

  // on x86_64:
  // the tcb starts with 3 ptrs: self, dtv, header
  // self is used to get TLS variables
  // dtv is used to load tls for dynamically loaded libraries
  // header is used to access the tcb
  // self, header, and fs reg all point to the start of the tcb

  // we change the self pointed to point to the tcb at the new location
  // we leave the header pointer intact so that the tcb is modified in-place
  // at the old location

  // changing the location breaks things that keep pointers into the struct,
  // like linked lists. TCB has a lot of internal self-pointers and lists, so
  // it is advantageous to keep it in place. TLS usually has less (application
  // dependent), and must be moved because it can be accessed relative to fs
  // with no indirection. So we change self but not header.

  // change self pointer to new application half TLS location
  ((void **)(uh_fsaddr + LH_TLS_SIZE))[0] = (void *) (uh_fsaddr + LH_TLS_SIZE);
  ((void **)(uh_fsaddr + LH_TLS_SIZE))[2] = (void *) (uh_fsaddr + LH_TLS_SIZE);
}

static inline void RETURN_TO_UPPER_HALF(const char* func) {
	bool changed = false;
tcbhead_t *uhhead = (tcbhead_t *)(uh_fsaddr + LH_TLS_SIZE);
tcbhead_t *lhhead = (tcbhead_t *)(lh_fsaddr + LH_TLS_SIZE);
  if (uhhead->dtv != lhhead->dtv
      || uhhead->multiple_threads != lhhead->multiple_threads
      || uhhead->gscope_flag != lhhead->gscope_flag
      || uhhead->sysinfo != lhhead->sysinfo
      || uhhead->stack_guard != lhhead->stack_guard
      || uhhead->pointer_guard != lhhead->pointer_guard
      || uhhead->vgetcpu_cache[0] != lhhead->vgetcpu_cache[0]
      || uhhead->vgetcpu_cache[1] != lhhead->vgetcpu_cache[1]
      || uhhead->__glibc_reserved1 != lhhead->__glibc_reserved1
      || uhhead->__glibc_unused1 != lhhead->__glibc_unused1
      || uhhead->__private_tm[0] != lhhead->__private_tm[0]
      || uhhead->__private_tm[1] != lhhead->__private_tm[1]
      || uhhead->__private_tm[2] != lhhead->__private_tm[2]
      || uhhead->__private_tm[3] != lhhead->__private_tm[3]
      || uhhead->__private_ss != lhhead->__private_ss
      ) {
	  changed = true;
          memcpy(debug_buf, lh_fsaddr, LH_TLS_SIZE + TCB_HEADER_SIZE);
  }
  
  memcpy(lh_fsaddr, uh_fsaddr, LH_TLS_SIZE + TCB_HEADER_SIZE);
  memcpy(uh_fsaddr, fsaddr_buf, LH_TLS_SIZE + TCB_HEADER_SIZE);

  // restore self pointer to original driver-half TLS location
  // Only copy TLS back to driver half
  ((void **)(lh_fsaddr + LH_TLS_SIZE))[0] = (void *) (lh_fsaddr + LH_TLS_SIZE);
  ((void **)(lh_fsaddr + LH_TLS_SIZE))[2] = (void *) (lh_fsaddr + LH_TLS_SIZE);
  // Can only call printf in uh
  if (changed) {
//    JTRACE("changed ls_fsaddr uh_fsaddr") (lh_fsaddr) (uh_fsaddr);
//tcbhead_t *lhhead = (tcbhead_t *)(lh_fsaddr + LH_TLS_SIZE);
tcbhead_t *debughead = (tcbhead_t *)(debug_buf + LH_TLS_SIZE);
    printf("%d %d API %s changed ls_fsaddr %p uh_fsaddr %p %p %p %s %d\n", __LINE__, getpid(), func, lh_fsaddr, uh_fsaddr, lhhead, uhhead, __func__, __LINE__);
		  fflush(stdout);
    printf("%d %d %p %p %p %d %x %lx %lx %lx %lu %lu %d %d %p %p %p %p %p\n", __LINE__, getpid(),
lhhead->tcb, lhhead->dtv, lhhead->self, lhhead->multiple_threads, lhhead->gscope_flag, lhhead->sysinfo, lhhead->stack_guard, lhhead->pointer_guard, lhhead->vgetcpu_cache[0], lhhead->vgetcpu_cache[1],
lhhead->__glibc_reserved1, lhhead->__glibc_unused1, lhhead->__private_tm[0], lhhead->__private_tm[1], lhhead->__private_tm[2], lhhead->__private_tm[3], lhhead->__private_ss);
		  fflush(stdout);
    printf("%d %d %p %p %p %d %x %lx %lx %lx %lu %lu %d %d %p %p %p %p %p\n", __LINE__, getpid(),
debughead->tcb, debughead->dtv, debughead->self, debughead->multiple_threads, debughead->gscope_flag, debughead->sysinfo, debughead->stack_guard, debughead->pointer_guard, debughead->vgetcpu_cache[0], debughead->vgetcpu_cache[1],
debughead->__glibc_reserved1, debughead->__glibc_unused1, debughead->__private_tm[0], debughead->__private_tm[1], debughead->__private_tm[2], debughead->__private_tm[3], debughead->__private_ss);
		  fflush(stdout);

  }
}
#endif

// ===================================================
// This function splits the process by initializing the lower half with the
// lh_proxy code. It returns 0 on success.
extern int splitProcess();

#endif // ifndef _SPLIT_PROCESS_H
