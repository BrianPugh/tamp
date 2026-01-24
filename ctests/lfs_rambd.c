/**
 * @file lfs_rambd.c
 * @brief RAM block device implementation for LittleFS testing
 */
#include "lfs_rambd.h"

#include <string.h>

static int rambd_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {
    lfs_rambd_t *bd = (lfs_rambd_t *)c->context;
    memcpy(buffer, bd->buffer + (block * bd->block_size) + off, size);
    return 0;
}

static int rambd_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer,
                      lfs_size_t size) {
    lfs_rambd_t *bd = (lfs_rambd_t *)c->context;
    memcpy(bd->buffer + (block * bd->block_size) + off, buffer, size);
    return 0;
}

static int rambd_erase(const struct lfs_config *c, lfs_block_t block) {
    lfs_rambd_t *bd = (lfs_rambd_t *)c->context;
    memset(bd->buffer + (block * bd->block_size), 0xFF, bd->block_size);
    return 0;
}

static int rambd_sync(const struct lfs_config *c) {
    (void)c;
    return 0;
}

void lfs_rambd_create(struct lfs_config *cfg, lfs_rambd_t *bd, unsigned char *buffer, lfs_size_t size) {
    bd->buffer = buffer;
    bd->read_size = 16;
    bd->prog_size = 16;
    bd->block_size = 256;
    bd->block_count = size / bd->block_size;

    memset(cfg, 0, sizeof(*cfg));

    cfg->context = bd;
    cfg->read = rambd_read;
    cfg->prog = rambd_prog;
    cfg->erase = rambd_erase;
    cfg->sync = rambd_sync;

    cfg->read_size = bd->read_size;
    cfg->prog_size = bd->prog_size;
    cfg->block_size = bd->block_size;
    cfg->block_count = bd->block_count;
    cfg->cache_size = 64;
    cfg->lookahead_size = 16;
    cfg->block_cycles = 500;
    cfg->compact_thresh = 0; /* 0 = use default (block_size - read_size) */
}
