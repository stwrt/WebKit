/*
 * Copyright (c) 2022 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PAS_DARWIN_SPI_H
#define PAS_DARWIN_SPI_H

#include "pas_utils.h"
#include "pas_thread.h"

#if PAS_OS(DARWIN)
#if defined(__has_include) && __has_include(<pthread/private.h>)

// FIXME: rdar://140431798 Remove PAS_{BEGIN/END}_EXTERN_C when WebKit does not need to support the platform versions anymore.
#if !((PAS_PLATFORM(MAC) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 150400) \
    || (PAS_PLATFORM(IOS) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 180400) \
    || (PAS_PLATFORM(APPLETV) && __TV_OS_VERSION_MAX_ALLOWED >= 180400) \
    || (PAS_PLATFORM(WATCHOS) && __WATCH_OS_VERSION_MAX_ALLOWED >= 110400) \
    || (PAS_PLATFORM(VISION) && __VISION_OS_VERSION_MAX_ALLOWED >= 20040))
PAS_BEGIN_EXTERN_C;
#endif
#include <pthread/private.h>
#if !((PAS_PLATFORM(MAC) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 150400) \
    || (PAS_PLATFORM(IOS) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 180400) \
    || (PAS_PLATFORM(APPLETV) && __TV_OS_VERSION_MAX_ALLOWED >= 180400) \
    || (PAS_PLATFORM(WATCHOS) && __WATCH_OS_VERSION_MAX_ALLOWED >= 110400) \
    || (PAS_PLATFORM(VISION) && __VISION_OS_VERSION_MAX_ALLOWED >= 20040))
PAS_END_EXTERN_C;
#endif

#define PAS_HAVE_PTHREAD_PRIVATE 1
#else
PAS_BEGIN_EXTERN_C;
int pthread_self_is_exiting_np(void);
PAS_END_EXTERN_C;
#define PAS_HAVE_PTHREAD_PRIVATE 0
#endif

PAS_BEGIN_EXTERN_C;

/* From OSS libmalloc stack_logging.h
   https://github.com/apple-oss-distributions/libmalloc/blob/main/private/stack_logging.h */
/*********    MallocStackLogging permanant SPIs  ************/

#define pas_stack_logging_type_free                           0
#define pas_stack_logging_type_generic                        1    /* anything that is not allocation/deallocation */
#define pas_stack_logging_type_alloc                          2    /* malloc, realloc, etc... */
#define pas_stack_logging_type_dealloc                        4    /* free, realloc, etc... */
#define pas_stack_logging_type_vm_allocate                   16    /* vm_allocate or mmap */
#define pas_stack_logging_type_vm_deallocate                 32    /* vm_deallocate or munmap */
#define pas_stack_logging_type_mapped_file_or_shared_mem    128

// The valid flags include those from VM_FLAGS_ALIAS_MASK, which give the user_tag of allocated VM regions.
#define pas_stack_logging_valid_type_flags ( \
pas_stack_logging_type_generic | \
pas_stack_logging_type_alloc | \
pas_stack_logging_type_dealloc | \
pas_stack_logging_type_vm_allocate | \
pas_stack_logging_type_vm_deallocate | \
pas_stack_logging_type_mapped_file_or_shared_mem | \
VM_FLAGS_ALIAS_MASK);

// Following flags are absorbed by stack_logging_log_stack()
#define pas_stack_logging_flag_zone        8    /* NSZoneMalloc, etc... */
#define pas_stack_logging_flag_cleared    64    /* for NewEmptyHandle */

PAS_END_EXTERN_C;

// In a build using clang modules, we must never redeclare items
// from other headers, but instead include those headers.
#if defined(__has_include) && __has_include(<stack_logging.h>)
#include <stack_logging.h>
#else
// But, we want to ensure these symbols are available even
// in the case that we don't have such headers available.

PAS_BEGIN_EXTERN_C;

typedef void(malloc_logger_t)(uint32_t type,
                              uintptr_t arg1,
                              uintptr_t arg2,
                              uintptr_t arg3,
                              uintptr_t result,
                              uint32_t num_hot_frames_to_skip);
// FIXME: Workaround for rdar://119319825
#if !defined(__swift__)
extern malloc_logger_t* malloc_logger;
#endif

PAS_END_EXTERN_C;

#endif /* defined(__has_include) && __has_include(<stack_logging.h>) */

#endif /* PAS_OS(DARWIN) */

#endif /* PAS_DARWIN_SPI_H */
