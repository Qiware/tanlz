/******************************************************************************
 ** Copyright(C) 2014-2024 Qiware technology Co., Ltd
 **
 ** 文件名: shm_btree.c
 ** 版本号: 1.0
 ** 描  述: B树
 **         一棵m阶的B树, 或为空树, 或为满足下列特征的m叉树:
 **         ①. 树中每个结点至多有m棵子树;
 **         ②. 若根结点不是终端结点, 则至少有2棵子树;
 **         ③. 除根之外, 所有非终端结点至少有棵子树;
 **         ④. 所有的非终端结点中包含下列信息数据;
 **            [n, C0, K0, C1, K1, C2, K2, ...., Kn-1, Cn]
 **            其中：Ki[i=0,1,...,n-1]为关键字, 且Ki<Ki+1[i=0,1,...,n-2];
 **            Ci[i=0,1,...,n]为至上子树根结点的指针, 且指针Ci所指子树中所有结点的
 **            关键字均小于Ki[i=0,1,...,n-1], 但都大于Ki-1[i=1,...,n-1];
 ** 作  者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
#include "shm_btree.h"

#define shm_btree_ptr_to_off(ctx, ptr) (off_t)((void *)(ptr) - (ctx)->addr)
#define shm_btree_off_to_ptr(ctx, off) (void *)((ctx)->addr + (off))

static shm_btree_node_t *shm_btree_node_alloc(shm_btree_cntx_t *ctx);
static int shm_btree_node_dealloc(shm_btree_cntx_t *ctx, shm_btree_node_t *node);

static int _shm_btree_insert(shm_btree_cntx_t *ctx, shm_btree_node_t *node, int key, int idx, void *data);
static int shm_btree_split(shm_btree_cntx_t *ctx, shm_btree_node_t *node);
static int shm_btree_merge(shm_btree_cntx_t *ctx, shm_btree_node_t *node);
static int _shm_btree_merge(shm_btree_cntx_t *ctx, shm_btree_node_t *left, shm_btree_node_t *right, int idx);

/******************************************************************************
 **函数名称: shm_btree_creat
 **功    能: 创建B树
 **输入参数:
 **     path: B树路径
 **     m: 阶(m >= 3)
 **     opt: 参数选项
 **输出参数: NONE
 **返    回: B树
 **实现描述:
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
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
shm_btree_cntx_t *shm_btree_creat(const char *path, int m, size_t total)
{
    int fd;
    void *addr;
    shm_btree_t *btree;
    shm_slab_pool_t *pool;
    shm_btree_cntx_t *ctx;

    if (m < 3) {
        return NULL;
    }

    /* > 载入内存 */
    fd = open(path, OPEN_FLAGS, OPEN_MODE);
    if (fd < 0) {
        return NULL;
    }

    lseek(fd, total, SEEK_SET);

    write(fd, "", 1);

    addr = mmap(NULL, total, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (NULL == addr) {
        close(fd);
        return NULL;
    }

    close(fd);

    /* > 创建对象 */
    ctx = (shm_btree_cntx_t *)calloc(1, sizeof(shm_btree_cntx_t));
    if (NULL == ctx) {
        return NULL;
    }

    ctx->addr = addr;
    ctx->btree = (shm_btree_t *)addr;
    ctx->pool = (shm_slab_pool_t *)(addr + sizeof(shm_btree_t));

    btree = ctx->btree;

    btree->max = m - 1;
    btree->min = m / 2;
    if (0 != m%2) { btree->min++; }
    btree->min--;
    btree->sep_idx = m/2;
    btree->root = 0; /* 空 */
    btree->total = total;

    pool = ctx->pool;
    pool->pool_size = total - sizeof(shm_btree_t);

    if (shm_slab_init(ctx->pool)) {
        munmap(ctx->addr, btree->total);
        free(ctx);
        return NULL;
    }

    return ctx;
}


/******************************************************************************
 **函数名称: shm_btree_attach
 **功    能: 附着B树
 **输入参数:
 **     path: B树路径
 **     m: 阶(m >= 3)
 **     total: B树空间总大小
 **输出参数: NONE
 **返    回: B树
 **实现描述:
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
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.11 #
 ******************************************************************************/
shm_btree_cntx_t *shm_btree_attach(const char *path, int m, size_t total)
{
    int fd;
    void *addr;
    struct stat st;
    shm_btree_t *btree;
    shm_btree_cntx_t *ctx;

    if (m < 3) {
        return NULL;
    }

    if (stat(path, &st)
        || total != (size_t)st.st_size)
    {
        return NULL;
    }

    /* > 载入内存 */
    fd = open(path, OPEN_FLAGS, OPEN_MODE);
    if (fd < 0) {
        return NULL;
    }

    addr = mmap(NULL, total, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (NULL == addr) {
        close(fd);
        return NULL;
    }

    close(fd);

    /* > 创建对象 */
    ctx = (shm_btree_cntx_t *)calloc(1, sizeof(shm_btree_cntx_t));
    if (NULL == ctx) {
        return NULL;
    }

    ctx->addr = addr;
    ctx->btree = (shm_btree_t *)addr;
    ctx->pool = (shm_slab_pool_t *)(addr + sizeof(shm_btree_t));

    /* > 校验合法性 */
    btree = ctx->btree;
    if ((btree->max != m-1)
        || (btree->total != total))
    {
        munmap(ctx->addr, total);
        free(ctx);
        return NULL;
    }

    return ctx;
}

/******************************************************************************
 **函数名称: shm_btree_key_bsearch
 **功    能: B树键的二分查找
 **输入参数:
 **     keys: B树键值数组
 **     num: 键值数组长度
 **     key: 需要查找的键
 **输出参数: NONE
 **返    回: 离key最近的键值索引
 **实现描述: 使用二分查找算法实现
 **注意事项:
 **     返回值存mid在以下几种可能性:
 **     1. 返回等于key的值索引: keys[mid] == key
 **     2. 返回小于key的最大值索引: (keys[mid] < key) && (keys[mid+1] > key)
 **     3. 返回大于key的最小值索引: (keys[mid-1] < key) && (keys[mid] > key)
 **作    者: # Qifeng.zou # 2015.04.30 #
 ******************************************************************************/
static int shm_btree_key_bsearch(const int *keys, int num, int key)
{
    int low, mid = 0, high;

    low = 0;
    high = num - 1;

    while (high >= low) {
        mid = (low + high) >> 1;
        if (key == keys[mid]) {
            return mid;
        }
        else if (key < keys[mid]) {
            high = mid - 1;
            continue;
        }

        low = mid + 1;
    }

    return mid;
}

/******************************************************************************
 **函数名称: shm_btree_insert
 **功    能: 向B树中插入一个关键字
 **输入参数:
 **     btree: B树
 **     key: 将被插入的关键字
 **     data: 关键字对应数据
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
int shm_btree_insert(shm_btree_cntx_t *ctx, int key, void *data)
{
    int idx;
    int *node_key;
    shm_btree_node_t *node;
    off_t *node_data, *node_child;
    shm_btree_t *btree = ctx->btree;

    /* 1. 插入根结点 */
    if (0 == btree->root) {
        node = shm_btree_node_alloc(ctx);
        if (NULL == node) {
            return -1;
        }
        
        node_key = (int *)shm_btree_off_to_ptr(ctx, node->key);
        node_data = (off_t *)shm_btree_off_to_ptr(ctx, node->data);

        node->num = 1;
        node_key[0] = key;
        node->parent = 0; /* 空 */
        node_data[0] = (off_t)shm_btree_ptr_to_off(ctx, data);
        btree->root = (off_t)shm_btree_ptr_to_off(ctx, node);
        return 0;
    }

    /* 2. 查找关键字的插入位置 */
    node = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, btree->root);
    while (1) {
        node_key = (int *)shm_btree_off_to_ptr(ctx, node->key);
        node_data = (off_t *)shm_btree_off_to_ptr(ctx, node->data);

        /* 二分查找算法实现 */
        idx = shm_btree_key_bsearch(node_key, node->num, key);
        if (key == node_key[idx]) {
            return 0;
        }
        else if (key > node_key[idx]) {
            idx += 1;
        }

        node_child = (off_t *)shm_btree_off_to_ptr(ctx, node->child);
        if (0 == node_child[idx]) {
            break;
        }

        node = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, node_child[idx]);
    }

    /* 3. 执行插入操作 */
    return _shm_btree_insert(ctx, node, key, idx, data);
}

/******************************************************************************
 **函数名称: _shm_btree_insert
 **功    能: 插入关键字到指定节点
 **输入参数:
 **     btree: B树
 **     node: 指定节点
 **     key: 需被插入的关键字
 **     idx: 插入位置
 **     data: 承载数据
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
static int _shm_btree_insert(shm_btree_cntx_t *ctx,
        shm_btree_node_t *node, int key, int idx, void *data)
{
    int i;
    int *node_key;
    off_t *node_data;
    shm_btree_t *btree = ctx->btree;

    node_key = (int *)shm_btree_off_to_ptr(ctx, node->key);
    node_data = (off_t *)shm_btree_off_to_ptr(ctx, node->data);

    /* 1. 插入最底层的节点: 孩子节点都是空指针 */
    for (i=node->num; i>idx; i--) {
        node_key[i] = node_key[i-1];
        node_data[i] = node_data[i-1];
    }

    node_key[idx] = key;
    node_data[idx] = (off_t)shm_btree_ptr_to_off(ctx, data);
    node->num++;

    /* 2. 分化节点 */
    if (node->num > btree->max) {
        return shm_btree_split(ctx, node);
    }

    return 0;
}

/******************************************************************************
 **函数名称: shm_btree_split
 **功    能: 插入关键字到指定节点, 并进行分裂处理
 **输入参数:
 **     btree: B树
 **     node: 指定节点
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
static int shm_btree_split(shm_btree_cntx_t *ctx, shm_btree_node_t *node)
{
    int idx, total, sep_idx;
    shm_btree_t *btree = ctx->btree;
    shm_btree_node_t *parent, *node2, *child;
    int *node_key, *node2_key, *parent_key;
    off_t *node_data, *node2_data, *parent_data;
    off_t *node_child, *node2_child, *parent_child;

    sep_idx = btree->sep_idx;

    while (node->num > btree->max) {
        /* Split node */
        total = node->num;

        node2 = shm_btree_node_alloc(ctx);
        if (NULL == node2) {
            return -1;
        }

        node_key = (int *)shm_btree_off_to_ptr(ctx, node->key);
        node_data = (off_t *)shm_btree_off_to_ptr(ctx, node->data);
        node_child = (off_t *)shm_btree_off_to_ptr(ctx, node->child);

        node2_key = (int *)shm_btree_off_to_ptr(ctx, node2->key);
        node2_data = (off_t *)shm_btree_off_to_ptr(ctx, node2->data);
        node2_child = (off_t *)shm_btree_off_to_ptr(ctx, node2->child);

        /* Copy data */
        memcpy(node2_key, node_key+sep_idx+1, (total-sep_idx-1) * sizeof(int));
        memcpy(node2_data, node_data+sep_idx+1, (total-sep_idx-1) * sizeof(off_t));
        memcpy(node2_child, node_child+sep_idx+1, (total-sep_idx) * sizeof(off_t));

        node2->num = (total - sep_idx - 1);
        node2->parent  = node->parent;

        node->num = sep_idx;

        /* Insert into parent */
        if (0 == node->parent) {  /* Parent is NULL */
            /* Split root node */
            parent = shm_btree_node_alloc(ctx);
            if (NULL == parent) {
                return -1;
            }

            parent_key = (int *)shm_btree_off_to_ptr(ctx, parent->key);
            parent_data = (off_t *)shm_btree_off_to_ptr(ctx, parent->data);
            parent_child = (off_t *)shm_btree_off_to_ptr(ctx, parent->child);

            btree->root = (off_t)shm_btree_ptr_to_off(ctx, parent);
            parent_child[0] = (off_t)shm_btree_ptr_to_off(ctx, node);
            node->parent = btree->root;
            node2->parent = node->parent;

            parent_key[0] = node_key[sep_idx];
            parent_data[0] = node_data[sep_idx];
            parent_child[1] = (off_t)shm_btree_ptr_to_off(ctx, node2);
            parent->num++;
        }
        else {
            /* Insert into parent node */
            parent = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, node->parent);

            parent_key = (int *)shm_btree_off_to_ptr(ctx, parent->key);
            parent_data = (off_t *)shm_btree_off_to_ptr(ctx, parent->data);
            parent_child = (off_t *)shm_btree_off_to_ptr(ctx, parent->child);

            for (idx=parent->num; idx>0; idx--) {
                if (node_key[sep_idx] < parent_key[idx-1]) {
                    parent_key[idx] = parent_key[idx-1];
                    parent_data[idx] = parent_data[idx-1];
                    parent_child[idx+1] = parent_child[idx];
                }
                else {
                    parent_key[idx] = node_key[sep_idx];
                    parent_data[idx] = node_data[sep_idx];
                    parent_child[idx+1] = (off_t)shm_btree_ptr_to_off(ctx, node2);
                    node2->parent = (off_t)shm_btree_ptr_to_off(ctx, parent);
                    parent->num++;
                    break;
                }
            }

            if (0 == idx) {
                parent_key[0] = node_key[sep_idx];
                parent_data[0] = node_data[sep_idx];
                parent_child[1] = (off_t)shm_btree_ptr_to_off(ctx, node2);
                node2->parent = (off_t)shm_btree_ptr_to_off(ctx, parent);
                parent->num++;
            }
        }

        memset(node_key+sep_idx, 0, (total - sep_idx) * sizeof(int));
        memset(node_data+sep_idx, 0, (total - sep_idx) * sizeof(off_t));
        memset(node_child+sep_idx+1, 0, (total - sep_idx) * sizeof(off_t));

        /* Change node2's child->parent */
        for (idx=0; idx<=node2->num; idx++) {
            if (0 != node2_child[idx]) {
                child = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, node2_child[idx]);
                child->parent = (off_t)shm_btree_ptr_to_off(ctx, node2);
            }
        }
        node = parent;
    }

    return 0;
}

/******************************************************************************
 **函数名称: _shm_btree_remove
 **功    能: 在指定结点删除指定关键字
 **输入参数:
 **     btree: B树
 **     node: 指定结点
 **     idx: 将被删除的关键字在结点node中位置(0 ~ node->num - 1)
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **     使用node->child[idx]中的最大值替代被删除的关键字, 并依次向下处理直至最底
 **     层结点. -- 其实最终其处理过程相当于是删除最底层结点的关键字
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
static int _shm_btree_remove(shm_btree_cntx_t *ctx, shm_btree_node_t *node, int idx)
{
    shm_btree_node_t *orig = node, *child;
    int *node_key, *orig_key;
    off_t *node_data, *orig_data;
    off_t *node_child;
    shm_btree_t *btree = ctx->btree;

    /* 使用node->child[idx]中的最大值替代被删除的关键字 */
    do {
        node_child = (off_t *)shm_btree_off_to_ptr(ctx, node->child);
        if (0 == node_child[idx]) {
            break;
        }
        child = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, node_child[idx]);
        node = child;
    } while(1);

    node_key = (int *)shm_btree_off_to_ptr(ctx, node->key);
    node_data = (off_t *)shm_btree_off_to_ptr(ctx, node->data);

    orig_key = (int *)shm_btree_off_to_ptr(ctx, orig->key);
    orig_data = (off_t *)shm_btree_off_to_ptr(ctx, orig->data);

    orig_key[idx] = node_key[node->num - 1];
    orig_data[idx] = node_data[node->num - 1];

    /* 最终其处理过程相当于是删除最底层结点的关键字 */
    --node->num;
    node_key[node->num] = 0;
    node_data[node->num] = 0; /* 空 */
    if (node->num < btree->min) {
        return shm_btree_merge(ctx, node);
    }

    return 0;
}

/******************************************************************************
 **函数名称: shm_btree_merge
 **功    能: 合并结点
 **输入参数:
 **     btree: B树
 **     node: 该结点关键字数num<min
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **    处理情况分类:
 **     1) 合并结点的情况: node->num + brother->num + 1 <= max
 **     2) 借用结点的情况: node->num + brother->num + 1 >  max
 **注意事项:
 **     node此时为最底层结点
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
static int shm_btree_merge(shm_btree_cntx_t *ctx, shm_btree_node_t *node)
{
    off_t off;
    int idx, m, mid;
    shm_btree_t *btree = ctx->btree;
    shm_btree_node_t *parent, *right, *left, *child;
    int *node_key, *parent_key, *left_key, *right_key;
    off_t *node_data, *parent_data, *left_data, *right_data;
    off_t *node_child, *parent_child, *left_child, *right_child;


    node_key = (int *)shm_btree_off_to_ptr(ctx, node->key);
    node_data = (off_t *)shm_btree_off_to_ptr(ctx, node->data);
    node_child = (off_t *)shm_btree_off_to_ptr(ctx, node->child);

    /* 1. node是根结点, 不必进行合并处理 */
    if (0 == node->parent) {
        if (0 == node->num) {
            if (0 != node_child[0]) {
                btree->root = node_child[0];
                child = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, node_child[0]);
                child->parent = 0; /* 空 */
            }
            else {
                btree->root = 0; /* 空 */
            }
            shm_slab_dealloc(ctx->pool, node_child);
            shm_slab_dealloc(ctx->pool, node_key);
            shm_slab_dealloc(ctx->pool, node_data);
            shm_slab_dealloc(ctx->pool, node);
        }
        return 0;
    }

    /* 2. 查找node是其父结点的第几个孩子结点 */
    parent = (shm_btree_node_t *)((void *)ctx->addr + node->parent);

    parent_key = (int *)shm_btree_off_to_ptr(ctx, parent->key);
    parent_data = (off_t *)shm_btree_off_to_ptr(ctx, parent->data);
    parent_child = (off_t *)shm_btree_off_to_ptr(ctx, parent->child);

    off = (off_t)shm_btree_ptr_to_off(ctx, node);
    for (idx=0; idx<=parent->num; idx++) {
        if (parent_child[idx] == off) {
            break;
        }
    }

    if (idx > parent->num) {
        return -1;
    }
    /* 3. node: 最后一个孩子结点(left < node)
     * node as right child */
    else if (idx == parent->num) {
        mid = idx - 1;
        left = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, parent_child[mid]);

        /* 1) 合并结点 */
        if ((node->num + left->num + 1) <= btree->max) {
            return _shm_btree_merge(ctx, left, node, mid);
        }

        left_key = (int *)shm_btree_off_to_ptr(ctx, left->key);
        left_data = (off_t *)shm_btree_off_to_ptr(ctx, left->data);
        left_child = (off_t *)shm_btree_off_to_ptr(ctx, left->child);

        /* 2) 借用结点:brother->key[num-1] */
        for (m=node->num; m>0; m--) {
            node_key[m] = node_key[m - 1];
            node_data[m] = node_data[m - 1];
            node_child[m+1] = node_child[m];
        }
        node_child[1] = node_child[0];

        node_key[0] = parent_key[mid];
        node_data[0] = parent_data[mid];
        node->num++;
        node_child[0] = left_child[left->num];
        if (0 != left_child[left->num]) {
            child = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, left_child[left->num]);
            child->parent = (off_t)shm_btree_ptr_to_off(ctx, node);
        }

        parent_key[mid] = left_key[left->num - 1];
        parent_data[mid] = left_data[left->num - 1];
        left_key[left->num - 1] = 0;
        left_data[left->num - 1] = 0; /* 空 */
        left_child[left->num] = 0; /* 空 */
        left->num--;
        return 0;
    }

    /* 4. node: 非最后一个孩子结点(node < right)
     * node as left child */
    mid = idx;
    right = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, parent_child[mid + 1]);

    right_key = (int *)shm_btree_off_to_ptr(ctx, right->key);
    right_data = (off_t *)shm_btree_off_to_ptr(ctx, right->data);
    right_child = (off_t *)shm_btree_off_to_ptr(ctx, right->child);

    /* 1) 合并结点 */
    if ((node->num + right->num + 1) <= btree->max) {
        return _shm_btree_merge(ctx, node, right, mid);
    }

    /* 2) 借用结点: right->key[0] */
    node_key[node->num] = parent_key[mid];
    node_data[node->num] = parent_data[mid];
    ++node->num;
    node_child[node->num] = right_child[0];
    if (0 != right_child[0]) {
        child = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, right_child[0]);
        child->parent = (off_t)shm_btree_ptr_to_off(ctx, node);
    }

    parent_key[mid] = right_key[0];
    parent_data[mid] = right_data[0];
    for (m=0; m<right->num; m++) {
        right_key[m] = right_key[m+1];
        right_data[m] = right_data[m+1];
        right_child[m] = right_child[m+1];
    }
    right_child[m] = 0; /* 空 */
    right->num--;
    return 0;
}

/******************************************************************************
 **函数名称: _shm_btree_merge
 **功    能: 合并结点
 **输入参数:
 **     btree: B树
 **     node:
 **     brother:
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
static int _shm_btree_merge(shm_btree_cntx_t *ctx,
        shm_btree_node_t *left, shm_btree_node_t *right, int mid)
{
    int m;
    shm_btree_t *btree = ctx->btree;
    shm_btree_node_t *parent, *child;
    int *parent_key, *left_key, *right_key;
    off_t *parent_data, *left_data, *right_data;
    off_t *parent_child, *left_child, *right_child;

    parent = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, left->parent);

    parent_key = (int *)shm_btree_off_to_ptr(ctx, parent->key);
    parent_data = (off_t *)shm_btree_off_to_ptr(ctx, parent->data);
    parent_child = (off_t *)shm_btree_off_to_ptr(ctx, parent->child);

    left_key = (int *)shm_btree_off_to_ptr(ctx, left->key);
    left_data = (off_t *)shm_btree_off_to_ptr(ctx, left->data);
    left_child = (off_t *)shm_btree_off_to_ptr(ctx, left->child);

    right_key = (int *)shm_btree_off_to_ptr(ctx, right->key);
    right_data = (off_t *)shm_btree_off_to_ptr(ctx, right->data);
    right_child = (off_t *)shm_btree_off_to_ptr(ctx, right->child);

    left_key[left->num] = parent_key[mid];
    left_data[left->num] = parent_data[mid];
    ++left->num;

    memcpy(left_key + left->num, right_key, right->num*sizeof(int));
    memcpy(left_data + left->num, right_data, right->num*sizeof(off_t));
    memcpy(left_child + left->num, right_child, (right->num+1)*sizeof(off_t));

    for (m=0; m<=right->num; m++) {
        if (0 != right_child[m]) { /* 不空 */
            child = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, right_child[m]);
            child->parent = (off_t)shm_btree_ptr_to_off(ctx, left);
        }
    }
    left->num += right->num;

    for (m=mid; m<parent->num-1; m++) {
        parent_key[m] = parent_key[m+1];
        parent_data[m] = parent_data[m+1];
        parent_child[m+1] = parent_child[m+2];
    }

    parent_key[m] = 0;
    parent_data[m] = 0; /* 空 */
    parent_child[m+1] = 0; /* 空 */
    parent->num--;
    shm_slab_dealloc(ctx->pool, right_child);
    shm_slab_dealloc(ctx->pool, right_key);
    shm_slab_dealloc(ctx->pool, right_data);
    shm_slab_dealloc(ctx->pool, right);

    /* Check */
    if (parent->num < btree->min) {
        return shm_btree_merge(ctx, parent);
    }

    return 0;
}

/******************************************************************************
 **函数名称: shm_btree_destroy
 **功    能: 销毁B树
 **输入参数:
 **     ctx: B树
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
int shm_btree_destroy(shm_btree_cntx_t *ctx)
{
    int idx;
    int *node_key;
    void *node_data;
    off_t *node_child;
    shm_btree_node_t *node, *child;
    shm_btree_t *btree = ctx->btree;

    if (0 == btree->root) {
        munmap(ctx->addr, btree->total);
        free(ctx);
        return 0;
    }

    node = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, btree->root);

    node_key = (int *)shm_btree_off_to_ptr(ctx, node->key);
    node_data = (off_t *)shm_btree_off_to_ptr(ctx, node->data);
    node_child = (off_t *)shm_btree_off_to_ptr(ctx, node->child);

    for (idx=0; idx<=node->num; ++idx) {
        if (0 != node_child[idx]) {
            child = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, node_child[idx]);
            shm_btree_node_dealloc(ctx, child);
        }
    }

    shm_slab_dealloc(ctx->pool, node_key);
    shm_slab_dealloc(ctx->pool, node_data);
    shm_slab_dealloc(ctx->pool, node_child);
    shm_slab_dealloc(ctx->pool, node);
    btree->root = 0;

    munmap(ctx->addr, btree->total);
    free(ctx);
    return 0;
}

/******************************************************************************
 **函数名称: _shm_btree_print
 **功    能: 打印B树结构
 **输入参数:
 **     node: B树结点
 **     deep: 结点深度
 **输出参数: NONE
 **返    回: 节点地址
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
static void _shm_btree_print(shm_btree_cntx_t *ctx, shm_btree_node_t *node, int deep)
{
    int idx, d, flag = 0;
    int *node_key;
    off_t *node_child;
    shm_btree_node_t *child;

    node_key = (int *)shm_btree_off_to_ptr(ctx, node->key);
    node_child = (off_t *)shm_btree_off_to_ptr(ctx, node->child);

    /* 1. Print Start */
    for (d=0; d<deep; d++) {
        if (d == deep-1) {
            fprintf(stderr, "|-------");
        }
        else {
            fprintf(stderr, "|        ");
        }
    }

    fprintf(stderr, "<%d| ", node->num);
    for (idx=0; idx<node->num; idx++) {
        fprintf(stderr, "%d ", node_key[idx]);
    }

    fprintf(stderr, ">\n");

    /* 2. Print node's children */
    for (idx=0; idx<node->num+1; idx++) {
        if (0 != node_child[idx]) { /* 不空 */
            child = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, node_child[idx]);
            _shm_btree_print(ctx, child, deep+1);
            flag = 1;
        }
    }

    if (1 == flag) {
        for (d=0; d<deep; d++) {
            fprintf(stderr, "|        ");
        }

        fprintf(stderr, "</%d| ", node->num);
        for (idx=0; idx<node->num; idx++) {
            fprintf(stderr, "%d ", node_key[idx]);
        }

        fprintf(stderr, ">\n");
    }
}

/******************************************************************************
 **函数名称: shm_btree_print
 **功    能: 打印B树
 **输入参数:
 **     ctx: B树
 **输出参数: NONE
 **返    回: VOID
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
void shm_btree_print(shm_btree_cntx_t *ctx)
{
    shm_btree_node_t *node;
    shm_btree_t *btree = ctx->btree;

    if (0 != btree->root) { /* 不空 */
        node = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, btree->root);

        _shm_btree_print(ctx, node, 0);
    }
}

/******************************************************************************
 **函数名称: shm_btree_node_alloc
 **功    能: 创建一个节点
 **输入参数:
 **     btree: B树
 **输出参数: NONE
 **返    回: 节点地址
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
static shm_btree_node_t *shm_btree_node_alloc(shm_btree_cntx_t *ctx)
{
    int *node_key;
    shm_btree_node_t *node;
    off_t *node_data, *node_child;
    shm_btree_t *btree = ctx->btree;

    node = (shm_btree_node_t *)shm_slab_alloc(ctx->pool, sizeof(shm_btree_node_t));
    if (NULL == node) {
        return NULL;
    }

    node->num = 0;
    node->parent = 0; /* 空 */

    /* More than (max) is for move */
    node_key = (int *)shm_slab_alloc(ctx->pool, (btree->max + 1) * sizeof(int));
    if (NULL == node_key) {
        shm_slab_dealloc(ctx->pool, node);
        return NULL;
    }

    node->key = (off_t)shm_btree_ptr_to_off(ctx, node_key);

    node_data = (off_t *)shm_slab_alloc(ctx->pool, (btree->max + 1) * sizeof(off_t));
    if (NULL == node_data) {
        shm_slab_dealloc(ctx->pool, node_key);
        shm_slab_dealloc(ctx->pool, node);
        return NULL;
    }

    node->data = (off_t)shm_btree_ptr_to_off(ctx, node_data);

    /* More than (max+1) is for move */
    node_child = (off_t *)shm_slab_alloc(ctx->pool, (btree->max+2) * sizeof(off_t));
    if (NULL == node_child) {
        shm_slab_dealloc(ctx->pool, node_key);
        shm_slab_dealloc(ctx->pool, node_data);
        shm_slab_dealloc(ctx->pool, node);
        return NULL;
    }

    node->child = (off_t)shm_btree_ptr_to_off(ctx, node_child);
                                          
    return node;
}

/******************************************************************************
 **函数名称: shm_btree_node_dealloc
 **功    能: 销毁指定结点
 **输入参数:
 **     btree: B树
 **     node: 将被销毁的结点
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 使用递归算法释放结点空间
 **注意事项:
 **作    者: # Qifeng.zou # 2015.04.25 #
 ******************************************************************************/
static int shm_btree_node_dealloc(shm_btree_cntx_t *ctx, shm_btree_node_t *node)
{
    int idx;
    int *node_key;
    off_t *node_data, *node_child;
    shm_btree_node_t *child;

    node_key = (int *)shm_btree_off_to_ptr(ctx, node->key);
    node_data = (off_t *)shm_btree_off_to_ptr(ctx, node->data);
    node_child = (off_t *)shm_btree_off_to_ptr(ctx, node->child);

    for (idx=0; idx<=node->num; ++idx) {
        if (0 != node_child[idx]) {
            child = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, node_child[idx]);
            shm_btree_node_dealloc(ctx, child);
            continue;
        }
    }

    shm_slab_dealloc(ctx->pool, node_key);
    shm_slab_dealloc(ctx->pool, node_data);
    shm_slab_dealloc(ctx->pool, node_child);
    shm_slab_dealloc(ctx->pool, node);
    return 0;
}

/******************************************************************************
 **函数名称: shm_btree_remove
 **功    能: 删除指定关键字
 **输入参数:
 **     btree: B树
 **     key: 关键字
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
int shm_btree_remove(shm_btree_cntx_t *ctx, int key)
{
    int idx;
    off_t off;
    void *data;
    int *node_key;
    shm_btree_node_t *node;
    off_t *node_data, *node_child;
    shm_btree_t *btree = ctx->btree;

    off = btree->root;
    while (0 != off) {
        node = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, off);

        node_key = (int *)shm_btree_off_to_ptr(ctx, node->key);
        node_data = (off_t *)shm_btree_off_to_ptr(ctx, node->data);
        node_child = (off_t *)shm_btree_off_to_ptr(ctx, node->child);

        idx = shm_btree_key_bsearch(node_key, node->num, key);
        if (key == node_key[idx]) {
            data = (void *)shm_btree_off_to_ptr(ctx, node_data[idx]);
            shm_slab_dealloc(ctx->pool, data);
            return _shm_btree_remove(ctx, node, idx);
        }
        else if (key < node_key[idx]) {
            off = node_child[idx];
            continue;
        }

        off = node_child[idx+1];
    }

    return -1; /* Not found */
}

/******************************************************************************
 **函数名称: shm_btree_query
 **功    能: 查询指定关键字
 **输入参数:
 **     btree: B树
 **     key: 关键字
 **输出参数: NONE
 **返    回: key对应的数据
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.10 #
 ******************************************************************************/
void *shm_btree_query(shm_btree_cntx_t *ctx, int key)
{
    int idx;
    off_t off;
    int *node_key;
    shm_btree_node_t *node;
    off_t *node_data, *node_child;
    shm_btree_t *btree = ctx->btree;

    off = btree->root;
    while (0 != off) {
        node = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, off);

        node_key = (int *)shm_btree_off_to_ptr(ctx, node->key);
        node_data = (off_t *)shm_btree_off_to_ptr(ctx, node->data);
        node_child = (off_t *)shm_btree_off_to_ptr(ctx, node->child);

        idx = shm_btree_key_bsearch(node_key, node->num, key);
        if (key == node_key[idx]) {
            return (void *)shm_btree_off_to_ptr(ctx, node_data[idx]); /* 找到 */
        }
        else if (key < node_key[idx]) {
            off = node_child[idx];
            continue;
        }

        node = (shm_btree_node_t *)shm_btree_off_to_ptr(ctx, node_child[idx+1]);
    }

    return NULL; /* 未找到 */
}

/******************************************************************************
 **函数名称: shm_btree_dump
 **功    能: 固化B树数据
 **输入参数:
 **     btree: B树
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.08.11 #
 ******************************************************************************/
int shm_btree_dump(shm_btree_cntx_t *ctx)
{
    shm_btree_t *btree = ctx->btree;

    msync(ctx->addr, btree->total, MS_ASYNC);

    return 0;
}
