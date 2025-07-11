/*
 * Copyright (C) 2023 Igalia S.L.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WPEWaylandSHMPool.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <wtf/CryptographicallyRandomNumber.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/WTFString.h>

#if HAVE(LINUX_MEMFD_H)
#include <linux/memfd.h>
#include <sys/syscall.h>
#endif

namespace WPE {

WTF_MAKE_TZONE_ALLOCATED_IMPL(WaylandSHMPool);

static UnixFileDescriptor createSharedMemory()
{
    int fileDescriptor = -1;

#if HAVE(LINUX_MEMFD_H)
    static bool isMemFdAvailable = true;
    if (isMemFdAvailable) {
        do {
            fileDescriptor = syscall(__NR_memfd_create, "WPEWaylandSHMPool", MFD_CLOEXEC);
        } while (fileDescriptor == -1 && errno == EINTR);

        if (fileDescriptor != -1)
            return UnixFileDescriptor { fileDescriptor, UnixFileDescriptor::Adopt };

        if (errno != ENOSYS)
            return { };

        isMemFdAvailable = false;
    }
#endif

#if HAVE(SHM_ANON)
    do {
        fileDescriptor = shm_open(SHM_ANON, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    } while (fileDescriptor == -1 && errno == EINTR);
#else
    CString tempName;
    for (int tries = 0; fileDescriptor == -1 && tries < 10; ++tries) {
        auto name = makeString("/WPEWaylandSHMPool."_s, cryptographicallyRandomNumber<unsigned>());
        tempName = name.utf8();

        do {
            fileDescriptor = shm_open(tempName.data(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        } while (fileDescriptor == -1 && errno == EINTR);
    }

    if (fileDescriptor != -1)
        shm_unlink(tempName.data());
#endif

    return UnixFileDescriptor { fileDescriptor, UnixFileDescriptor::Adopt };
}

std::unique_ptr<WaylandSHMPool> WaylandSHMPool::create(struct wl_shm* shm, size_t size)
{
    auto fd = createSharedMemory();
    if (!fd)
        return nullptr;

    while (ftruncate(fd.value(), size) == -1) {
        if (errno != EINTR)
            return nullptr;
    }

    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.value(), 0);
    if (data == MAP_FAILED)
        return nullptr;

    return makeUnique<WaylandSHMPool>(data, size, WTFMove(fd), shm);
}

WaylandSHMPool::WaylandSHMPool(void* data, size_t size, UnixFileDescriptor&& fd, struct wl_shm* shm)
    : m_data(data)
    , m_size(size)
    , m_fd(WTFMove(fd))
    , m_pool(wl_shm_create_pool(shm, m_fd.value(), m_size))
{
}

WaylandSHMPool::~WaylandSHMPool()
{
    wl_shm_pool_destroy(m_pool);
    if (m_data != MAP_FAILED)
        munmap(m_data, m_size);
}

int WaylandSHMPool::allocate(size_t size)
{
    if (m_used + size > m_size) {
        if (!resize(2 * m_size + size))
            return -1;
    }

    int offset = m_used;
    m_used += size;
    return offset;
}

bool WaylandSHMPool::resize(size_t size)
{
    while (ftruncate(m_fd.value(), size) == -1) {
        if (errno != EINTR)
            return false;
    }

    wl_shm_pool_resize(m_pool, size);

    if (m_data != MAP_FAILED)
        munmap(m_data, m_size);
    m_data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd.value(), 0);
    if (m_data == MAP_FAILED)
        return false;

    m_size = size;
    return true;
}

struct wl_buffer* WaylandSHMPool::createBuffer(uint32_t offset, uint32_t width, uint32_t height, uint32_t stride)
{
    return wl_shm_pool_create_buffer(m_pool, offset, width, height, stride, WL_SHM_FORMAT_ARGB8888);
}

void WaylandSHMPool::write(std::span<const uint8_t> data, int offset)
{
    memcpySpan(mutableSpan().subspan(offset), data);
}

void WaylandSHMPool::write(std::span<const uint32_t> data, int offset)
{
    write(asBytes(data), offset);
}

} // namespace WPE
