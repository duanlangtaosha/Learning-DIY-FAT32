//
// Created by Administrator on 2019/2/4.
//

#include "xcluster.h"

/**
 * 初始化簇项
 * @param item
 * @param begin
 * @param end
 * @param flags
 */
void xcluster_item_init (xcluster_item_t * item, u32_t begin, u32_t end, u32_t flags) {
    xcluster_item_set(item, begin, end, flags);
}

/**
 * 设置簇项
 * @param item
 * @param begin
 * @param end
 * @param flags
 */
void xcluster_item_set(xcluster_item_t * item, u32_t begin, u32_t end, u32_t flags) {
    item->begin = begin;
    item->end = end;
    item->flags = flags;
}

/**
 * 设置簇项是否干净，即是否需要回写到fat表中
 * @param item
 * @param dirty
 */
void xcluster_set_dirty(xcluster_item_t * item, int dirty) {
    item->flags &= ~XCLUSTER_FLAG_DIRTY_MSK;
    item->flags |= dirty ? XCLUSTER_FLAG_DIRTY : XCLUSTER_FLAG_CLEAN;
}

void xcluster_chain_init(xcluster_chain_t * chain, xcluster_item_t * items, u32_t size) {
    chain->begin = 0;
    chain->end = 0;
    chain->count = 0;
    chain->items = items;
    chain->size = size;
}

static xfat_err_t try_merge_items(xcluster_item_t * dest_item, xcluster_item_t * src_item) {
    xfat_err_t err = FS_ERR_OK;

    if (dest_item->begin == (src_item->end + 1)) {
        // s->b -- s->e .... d->b -- d->e
        dest_item->begin = src_item->begin;
    } else if ((dest_item->end + 1) == src_item->begin) {
        // d->b -- d->e .... s->b -- s->e
        dest_item->end = src_item->end;
    } else {
        err = FS_ERR_FAILED;
    }
     
    return err;
}

/**
 * 将簇项添加到簇链中
 * @param item
 * @param dirty
 */
xfat_err_t xcluster_chain_add(xcluster_chain_t * chain, xcluster_item_t * item, int merge) {
    xcluster_item_t * target_item;

    if (merge) {
        // 遍历，合并重叠的区间
        u32_t curr = chain->begin;
        u32_t count = chain->count;
        while (count > 0) {
            xcluster_item_t * item_in_chain = &chain->items[curr];
            if (try_merge_items(item_in_chain, item) == FS_ERR_OK) {
                return FS_ERR_OK;
            }

            count--;
            if (++curr >= chain->size) {
                curr = 0;
            }
        }
    }

    // 插入至尾部
    if (chain->count == chain->size) {
        // 已经满，则覆盖掉开头处的
        target_item = &chain->items[chain->begin];
    } else {
        target_item = &chain->items[chain->end];
        if (++chain->end >= chain->size) {
            chain->end = 0;
        }
    }

    target_item->begin = item->begin;
    target_item->end = item->end;
    target_item->flags = item->flags;
    chain->count++;
    return FS_ERR_OK;
}

