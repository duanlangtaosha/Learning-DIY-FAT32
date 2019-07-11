//
// Created by Administrator on 2019/2/4.
//

#ifndef XCLUSTER_H
#define XCLUSTER_H

#include "xtypes.h"

#define XCLUSTER_FLAG_CLEAN         (0 << 0)
#define XCLUSTER_FLAG_DIRTY         (1 << 0)
#define XCLUSTER_FLAG_DIRTY_MSK     (1 << 0)

/**
 * 簇链项
 */
typedef struct _xcluster_item_t {
    u32_t begin;            // 簇项的起始簇号
    u32_t end;              // 簇项的结束簇号
    u32_t flags;            // 相关配置标记
}xcluster_item_t;

void xcluster_item_init (xcluster_item_t * item, u32_t begin, u32_t end, u32_t flags);
void xcluster_item_set(xcluster_item_t * item, u32_t begin, u32_t end, u32_t flags);

#define xcluster_item_begin(item)               ((item)->begin)
#define xcluster_item_end(item)                 ((item)->end)
#define xcluster_is_dirty(item)                 ((item)->flags & XCLUSTER_FLAG_DIRTY)

/**
 * 簇链项
 */
typedef struct _xcluster_chain_t {
    u32_t begin;
    u32_t end;
    u32_t count;
    xcluster_item_t * items;
    u32_t size;
}xcluster_chain_t;

void xcluster_chain_init(xcluster_chain_t * chain, xcluster_item_t * items, u32_t size);
xfat_err_t xcluster_chain_add(xcluster_chain_t * chain, xcluster_item_t * item, int merge);
xfat_err_t xcluster_chain_alloc(xcluster_chain_t * chain, xcluster_item_t ** item);
xfat_err_t xcluster_chian_contains_sector(xcluster_chain_t * chain, u32_t sector);

#define xcluster_chain_begin(chain)         ((chain)->begin)
#define xcluster_chain_end(chain)           ((chain)->end)
#define xcluster_chain_count(chain)         ((chain)->count)
#define xcluster_chain_size(chain)          ((chain)->size)
#define xcluster_chain_free_count(chain)    (xcluster_chain_size(chain) - xcluster_chain_count(chain))
#define xcluster_chain_item(chain, index)   (&(chain)->items[(index)])
#define xcluster_chain_last_item(chain)     (&(chain)->items[(xcluster_chain_count(chain) - 1)])


#endif //XCLUSTER_H
