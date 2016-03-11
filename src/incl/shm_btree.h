#if !defined(__SHM_BTREE_H__)
#define __SHM_BTREE_H__

#include "comm.h"
#include "redo.h"
#include "shm_slab.h"

/******************************************************************************
 **
 ** SHM-B树的内存结构图:
 **
 **      -----------------------------------------------------------------
 **     |       |      |                                                  |
 **     | btree | pool |           可  分  配  空  间                     |
 **     |       |      |                                                  |
 **      -----------------------------------------------------------------
 **     ^       ^
 **     |       |
 **   btree    pool 
 **     btree: B树对象
 **     pool: 内存池对象
 ******************************************************************************/

/* B树结点 */
typedef struct _shm_btree_node_t
{
    int num;                                        /* 关键字数 */
    off_t key;                                      /* 关键字(int *) */
    off_t data;                                     /* 结点承载(void **) */
    off_t child;                                    /* 孩子结点(shm_btree_node_t **) */
    off_t parent;                                   /* 父亲结点(shm_btree_node_t *) */
} shm_btree_node_t;

/* B树对象 */
typedef struct
{
    int max;                                        /* 关键字最大个数 */
    int min;                                        /* 最小关键字个数 */
    int sep_idx;                                    /* 结点分化的分割索引 */
    off_t root;                                     /* 根结点(shm_btree_node_t *) */

    size_t total;                                   /* 总大小 */
} shm_btree_t;

/* 全局信息 */
typedef struct
{
    void *addr;                                     /* 首地址 */

    shm_btree_t *btree;                             /* B树 */
    shm_slab_pool_t *pool;                          /* 内存池 */
} shm_btree_cntx_t;

extern shm_btree_cntx_t *shm_btree_creat(const char *path, int m, size_t total);
shm_btree_cntx_t *shm_btree_attach(const char *path, int m, size_t total);
int shm_btree_insert(shm_btree_cntx_t *ctx, int key, void *data);
int shm_btree_remove(shm_btree_cntx_t *ctx, int key);
void *shm_btree_query(shm_btree_cntx_t *ctx, int key);
int shm_btree_dump(shm_btree_cntx_t *ctx);
extern int shm_btree_destroy(shm_btree_cntx_t *ctx);
void shm_btree_print(shm_btree_cntx_t *ctx);

void *shm_btree_alloc(shm_btree_cntx_t *ctx, size_t size);
#define shm_btree_alloc(ctx, size) shm_slab_alloc((ctx)->pool, (size))
#define shm_btree_dealloc(ctx, ptr) shm_slab_dealloc((ctx)->pool, (ptr))

#endif /*__SHM_BTREE_H__*/
