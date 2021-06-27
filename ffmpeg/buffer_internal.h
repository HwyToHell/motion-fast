/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVUTIL_BUFFER_INTERNAL_H
#define AVUTIL_BUFFER_INTERNAL_H


#include <stdint.h>


//#include <stdatomic.h>



#include "buffer.h"
// #include "thread.h"

/**
 * The buffer is always treated as read-only.
 */
#define BUFFER_FLAG_READONLY      (1 << 0)
/**
 * The buffer was av_realloc()ed, so it is reallocatable.
 */
#define BUFFER_FLAG_REALLOCATABLE (1 << 1)

struct AVBuffer {
    uint8_t *data; /**< data described by this buffer */
    int      size; /**< size of data in bytes */

    /**
     *  number of existing AVBufferRef instances referring to this buffer
     */
    //atomic_uint refcount;
    unsigned refcount;

    /**
     * a callback for freeing the data
     */
    void (*free)(void *opaque, uint8_t *data);

    /**
     * an opaque pointer, to be used by the freeing callback
     */
    void *opaque;

    /**
     * A combination of BUFFER_FLAG_*
     */
    int flags;
};

/*
typedef struct BufferPoolEntry {
    uint8_t *data;

    void *opaque;
    void (*free)(void *opaque, uint8_t *data);

    AVBufferPool *pool;
    struct BufferPoolEntry *next;
} BufferPoolEntry;

struct AVBufferPool {
    AVMutex mutex;
    BufferPoolEntry *pool;

    atomic_uint refcount;

    int size;
    void *opaque;
    AVBufferRef* (*alloc)(int size);
    AVBufferRef* (*alloc2)(void *opaque, int size);
    void         (*pool_free)(void *opaque);
};
*/

#endif /* AVUTIL_BUFFER_INTERNAL_H */
