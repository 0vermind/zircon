// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fs/trace.h>

#include <magenta/device/device.h>
#include <magenta/listnode.h>

#include "minfs.h"
#include "minfs-private.h"

#define BLOCK_FLAGS 0xF

struct block {
    list_node_t hashnode;
    list_node_t listnode;
    uint32_t flags;
    uint32_t bno;
    void* data;
};

void* block_data(block_t* blk) {
    return blk->data;
}

struct bcache {
    list_node_t list_busy;  // between bcache_get() and bcache_put()
    list_node_t list_dirty; // waiting for write
    list_node_t list_lru;   // available for re-use
    list_node_t list_free;  // never been used
    list_node_t hash[MINFS_BUCKETS];
    int fd;
    uint32_t blocksize;
    uint32_t blockmax;
};

#define bno_hash(bno) fnv1a_tiny(bno, MINFS_HASH_BITS)

static_assert((1<<MINFS_HASH_BITS) == MINFS_BUCKETS,
              "minfs constants disagree");

mx_status_t readblk(bcache_t* bc, uint32_t bno, void* data) {
    int fd = bc->fd;
    off_t off = bno * MINFS_BLOCK_SIZE;
    trace(IO, "readblk() bno=%u off=%#llx\n", bno, (unsigned long long)off);
    if (lseek(fd, off, SEEK_SET) < 0) {
        error("minfs: cannot seek to block %u\n", bno);
        return ERR_IO;
    }
    if (read(fd, data, MINFS_BLOCK_SIZE) != MINFS_BLOCK_SIZE) {
        error("minfs: cannot read block %u\n", bno);
        return ERR_IO;
    }
    return NO_ERROR;
}

mx_status_t writeblk(bcache_t* bc, uint32_t bno, const void* data) {
    int fd = bc->fd;
    off_t off = bno * MINFS_BLOCK_SIZE;
    trace(IO, "writeblk() bno=%u off=%#llx\n", bno, (unsigned long long)off);
    if (lseek(fd, off, SEEK_SET) < 0) {
        error("minfs: cannot seek to block %u\n", bno);
        return ERR_IO;
    }
    if (write(fd, data, MINFS_BLOCK_SIZE) != MINFS_BLOCK_SIZE) {
        error("minfs: cannot write block %u\n", bno);
        return ERR_IO;
    }
    return NO_ERROR;
}

uint32_t bcache_max_block(bcache_t* bc) {
    return bc->blockmax;
}

#define MODE_FIND 0
#define MODE_LOAD 1
#define MODE_ZERO 2

static const char* modestr(uint32_t mode) {
    switch (mode) {
    case MODE_FIND: return "FIND";
    case MODE_LOAD: return "LOAD";
    case MODE_ZERO: return "ZERO";
    default: return "????";
    }
}

#define BLOCK_BUSY 0x10


void bcache_invalidate(bcache_t* bc) {
    block_t* blk;
    uint32_t n = 0;
    while ((blk = list_remove_head_type(&bc->list_lru, block_t, listnode)) != NULL) {
        if (blk->flags & BLOCK_BUSY) {
            panic("blk %p bno %u is busy on lru\n", blk, blk->bno);
        }
        // remove from hash, bno to be reassigned
        list_delete(&blk->hashnode);
        list_add_tail(&bc->list_free, &blk->listnode);
        n++;
    }
    trace(BCACHE, "[ %d blocks dropped ]\n", n);
}

static block_t* _bcache_get(bcache_t* bc, uint32_t bno, void** data, uint32_t mode) {
    trace(BCACHE,"bcache_get() bno=%u %s\n",bno,modestr(mode));
    if (bno >= bc->blockmax) {
        return NULL;
    }
    block_t* blk;
    uint32_t bucket = bno_hash(bno);
    list_for_every_entry(bc->hash + bucket, blk, block_t, hashnode) {
        if (blk->bno == bno) {
            if (blk->flags & BLOCK_BUSY) {
                panic("blk %p bno %u is busy\n", blk, bno);
            }
            if (mode == MODE_ZERO) {
                blk->flags |= BLOCK_DIRTY;
                memset(blk->data, 0, bc->blocksize);
            }
            // remove from dirty or lru
            list_delete(&blk->listnode);
            goto done;
        }
    }
    if (mode == MODE_FIND) {
        blk = NULL;
    } else {
        if ((blk = list_remove_head_type(&bc->list_free, block_t, listnode)) != NULL) {
            // nothing extra to do
        } else if ((blk = list_remove_head_type(&bc->list_lru, block_t, listnode)) != NULL) {
            if (blk->flags & BLOCK_BUSY) {
                panic("blk %p bno %u is busy on lru\n", blk, blk->bno);
            }
            // remove from hash, bno to be reassigned
            list_delete(&blk->hashnode);
        } else {
            panic("bcache: out of blocks\n");
        }
        blk->bno = bno;
        list_add_tail(bc->hash + bucket, &blk->hashnode);
        if (mode == MODE_ZERO) {
            blk->flags |= BLOCK_DIRTY;
            memset(blk->data, 0, bc->blocksize);
        } else {
            if (readblk(bc, bno, blk->data) < 0) {
                panic("bcache: bno %u read error!\n", bno);
            }
        }
    }
done:
    if (blk) {
        blk->flags |= BLOCK_BUSY;
        list_add_tail(&bc->list_busy, &blk->listnode);
        *data = blk->data;
    }
    trace(BCACHE, "bcache_get bno=%u %p\n", bno, blk);
    return blk;
}

block_t* bcache_get(bcache_t* bc, uint32_t bno, void** bdata) {
    return _bcache_get(bc, bno, bdata, MODE_LOAD);
}

block_t* bcache_get_zero(bcache_t* bc, uint32_t bno, void** bdata) {
    return _bcache_get(bc, bno, bdata, MODE_ZERO);
}

void bcache_put(bcache_t* bc, block_t* blk, uint32_t flags) {
    trace(BCACHE, "bcache_put() bno=%u%s\n", blk->bno, (flags & BLOCK_DIRTY) ? " DIRTY" : "");
    if (!(blk->flags & BLOCK_BUSY)) {
        panic("bcache_put() bno=%u NOT BUSY!\n", blk->bno);
    }
    // remove from busy list
    list_delete(&blk->listnode);
    if ((flags | blk->flags) & BLOCK_DIRTY) {
        if (writeblk(bc, blk->bno, blk->data) < 0) {
            error("block write error!\n");
        }
        blk->flags &= (~(BLOCK_DIRTY|BLOCK_BUSY));
    } else {
        blk->flags &= (~BLOCK_BUSY);
    }
    list_add_tail(&bc->list_lru, &blk->listnode);
}

mx_status_t bcache_read(bcache_t* bc, uint32_t bno, void* data, uint32_t off, uint32_t len) {
    trace(BCACHE, "bcache_read() bno=%u off=%u len=%u\n", bno, off, len);
    if ((off > bc->blocksize) || ((bc->blocksize - off) < len)) {
        return -1;
    }
    void* bdata;
    block_t* blk = bcache_get(bc, bno, &bdata);
    if (blk != NULL) {
        memcpy(data, bdata + off, len);
        bcache_put(bc, blk, 0);
        return 0;
    } else {
        return ERR_IO;
    }
}

mx_status_t bcache_sync(bcache_t* bc) {
    return fsync(bc->fd);
}

int bcache_create(bcache_t** out, int fd, uint32_t blockmax, uint32_t blocksize, uint32_t num) {
    bcache_t* bc;
    if ((bc = calloc(1, sizeof(bcache_t))) == NULL) {
        return -1;
    }
    bc->fd = fd;
    bc->blockmax = blockmax;
    bc->blocksize = blocksize;
    list_initialize(&bc->list_busy);
    list_initialize(&bc->list_dirty);
    list_initialize(&bc->list_lru);
    list_initialize(&bc->list_free);
    for (int n = 0; n < MINFS_BUCKETS; n++) {
        list_initialize(bc->hash + n);
    }
    while (num > 0) {
        block_t* blk;
        if ((blk = calloc(1, sizeof(block_t))) == NULL) {
            break;
        }
        if ((blk->data = malloc(bc->blocksize)) == NULL) {
            free(blk);
            break;
        }
        list_add_tail(&bc->list_free, &blk->listnode);
        num--;
    }
    *out = bc;
    return 0;
}

int bcache_close(bcache_t* bc) {
    return close(bc->fd);
}

#ifndef __Fuchsia__
// This is used by the ioctl wrappers in magenta/device/device.h. It's not
// called by host tools, so just satisfy the linker with a stub.
ssize_t mxio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len) {
    return -1;
}
#endif
