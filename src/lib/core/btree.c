/******************************************************************************
 ** Copyright(C) 2014-2024 Qiware technology Co., Ltd
 **
 ** 文件名: btree.c
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
 ** 作  者: # Qifeng.zou # 2014.03.12 #
 ******************************************************************************/
#include "btree.h"

static btree_node_t *btree_node_alloc(btree_t *btree);
static int btree_node_dealloc(btree_t *btree, btree_node_t *node);

static int _btree_insert(btree_t *btree, btree_node_t *node, int key, int idx, void *data);
static int btree_split(btree_t *btree, btree_node_t *node);
static int btree_merge(btree_t *btree, btree_node_t *node);
static int _btree_merge(btree_t *btree, btree_node_t *left, btree_node_t *right, int idx);

/******************************************************************************
 **函数名称: btree_creat
 **功    能: 创建B树
 **输入参数:
 **     m: 阶(m >= 3)
 **     opt: 参数选项
 **输出参数: NONE
 **返    回: B树
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.03.12 #
 ******************************************************************************/
btree_t *btree_creat(int m, btree_opt_t *opt)
{
    btree_t *btree;

    if (m < 3) {
        return NULL;
    }

    btree = (btree_t *)opt->alloc(opt->pool, sizeof(btree_t));
    if (NULL == btree) {
        return NULL;
    }

    btree->max = m - 1;
    btree->min = m / 2;
    if (0 != m%2) { btree->min++; }
    btree->min--;
    btree->sep_idx = m/2;
    btree->root = NULL;

    btree->pool = opt->pool;
    btree->alloc = opt->alloc;
    btree->dealloc = opt->dealloc;

    return btree;
}

/******************************************************************************
 **函数名称: btree_key_bsearch
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
static int btree_key_bsearch(const int *keys, int num, int key)
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
 **函数名称: btree_insert
 **功    能: 向B树中插入一个关键字
 **输入参数:
 **     btree: B树
 **     key: 将被插入的关键字
 **     data: 关键字对应数据
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.03.12 #
 ******************************************************************************/
int btree_insert(btree_t *btree, int key, void *data)
{
    int idx;
    btree_node_t *node = btree->root;

    /* 1. 插入根结点 */
    if (NULL == node) {
        node = btree_node_alloc(btree);
        if (NULL == node) {
            return -1;
        }

        node->num = 1;
        node->key[0] = key;
        node->data[0] = data;
        node->parent = NULL;

        btree->root = node;
        return 0;
    }

    /* 2. 查找关键字的插入位置 */
    while (NULL != node) {
        /* 二分查找算法实现 */
        idx = btree_key_bsearch(node->key, node->num, key);
        if (key == node->key[idx]) {
            return 0;
        }
        else if (key > node->key[idx]) {
            idx += 1;
        }

        if (NULL == node->child[idx]) {
            break;
        }

        node = node->child[idx];
    }

    /* 3. 执行插入操作 */
    return _btree_insert(btree, node, key, idx, data);
}

/******************************************************************************
 **函数名称: _btree_insert
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
 **作    者: # Qifeng.zou # 2014.03.12 #
 ******************************************************************************/
static int _btree_insert(btree_t *btree, btree_node_t *node, int key, int idx, void *data)
{
    int i;

    /* 1. 插入最底层的节点: 孩子节点都是空指针 */
    for (i=node->num; i>idx; i--) {
        node->key[i] = node->key[i-1];
        node->data[i] = node->data[i-1];
    }

    node->key[idx] = key;
    node->data[idx] = data;
    node->num++;

    /* 2. 分化节点 */
    if (node->num > btree->max) {
        return btree_split(btree, node);
    }

    return 0;
}

/******************************************************************************
 **函数名称: btree_split
 **功    能: 插入关键字到指定节点, 并进行分裂处理
 **输入参数:
 **     btree: B树
 **     node: 指定节点
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.03.12 #
 ******************************************************************************/
static int btree_split(btree_t *btree, btree_node_t *node)
{
    int idx, total, sep_idx = btree->sep_idx;
    btree_node_t *parent, *node2;

    while (node->num > btree->max) {
        /* Split node */
        total = node->num;

        node2 = btree_node_alloc(btree);
        if (NULL == node2) {
            return -1;
        }

        /* Copy data */
        memcpy(node2->key, node->key+sep_idx+1, (total-sep_idx-1) * sizeof(int));
        memcpy(node2->data, node->data+sep_idx+1, (total-sep_idx-1) * sizeof(void *));
        memcpy(node2->child, node->child+sep_idx+1, (total-sep_idx) * sizeof(btree_node_t *));

        node2->num = (total - sep_idx - 1);
        node2->parent  = node->parent;

        node->num = sep_idx;

        /* Insert into parent */
	    parent  = node->parent;
        if (NULL == parent) {
            /* Split root node */
            parent = btree_node_alloc(btree);
            if (NULL == parent) {
                return -1;
            }

            btree->root = parent;
            parent->child[0] = node;
            node->parent = parent;
            node2->parent = parent;

            parent->key[0] = node->key[sep_idx];
            parent->data[0] = node->data[sep_idx];
            parent->child[1] = node2;
            parent->num++;
        }
        else {
            /* Insert into parent node */
            for (idx=parent->num; idx>0; idx--) {
                if (node->key[sep_idx] < parent->key[idx-1]) {
                    parent->key[idx] = parent->key[idx-1];
                    parent->data[idx] = parent->data[idx-1];
                    parent->child[idx+1] = parent->child[idx];
                }
                else {
                    parent->key[idx] = node->key[sep_idx];
                    parent->data[idx] = node->data[sep_idx];
                    parent->child[idx+1] = node2;
                    node2->parent = parent;
                    parent->num++;
                    break;
                }
            }

            if (0 == idx) {
                parent->key[0] = node->key[sep_idx];
                parent->data[0] = node->data[sep_idx];
                parent->child[1] = node2;
                node2->parent = parent;
                parent->num++;
            }
        }

        memset(node->key+sep_idx, 0, (total - sep_idx) * sizeof(int));
        memset(node->data+sep_idx, 0, (total - sep_idx) * sizeof(void *));
        memset(node->child+sep_idx+1, 0, (total - sep_idx) * sizeof(btree_node_t *));

        /* Change node2's child->parent */
        for (idx=0; idx<=node2->num; idx++) {
            if (NULL != node2->child[idx]) {
                node2->child[idx]->parent = node2;
            }
        }
        node = parent;
    }

    return 0;
}

/******************************************************************************
 **函数名称: _btree_remove
 **功    能: 在指定结点删除指定关键字
 **输入参数:
 **     btree: B树
 **     node: 指定结点
 **     idx: 将被删除的关键字在结点node中位置(0 ~ node->num - 1)
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **     使用node->child[idx]中的最大值替代被删除的关键字,
 **     并依次向下处理直至最底层结点,
 **     -- 其实最终其处理过程相当于是删除最底层结点的关键字
 **注意事项:
 **作    者: # Qifeng.zou # 2014.03.12 #
 ******************************************************************************/
static int _btree_remove(btree_t *btree, btree_node_t *node, int idx)
{
    btree_node_t *orig = node, *child = node->child[idx];

    /* 使用node->child[idx]中的最大值替代被删除的关键字 */
    while (NULL != child) {
        node = child;
        child = node->child[child->num];
    }

    orig->key[idx] = node->key[node->num - 1];
    orig->data[idx] = node->data[node->num - 1];

    /* 最终其处理过程相当于是删除最底层结点的关键字 */
    --node->num;
    node->key[node->num] = 0;
    node->data[node->num] = NULL;
    if (node->num < btree->min) {
        return btree_merge(btree, node);
    }

    return 0;
}

/******************************************************************************
 **函数名称: btree_merge
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
 **作    者: # Qifeng.zou # 2014.03.12 #
 ******************************************************************************/
static int btree_merge(btree_t *btree, btree_node_t *node)
{
    int idx, m, mid;
    btree_node_t *parent = node->parent, *right, *left;

    /* 1. node是根结点, 不必进行合并处理 */
    if (NULL == parent) {
        if (0 == node->num) {
            if (NULL != node->child[0]) {
                btree->root = node->child[0];
                node->child[0]->parent = NULL;
            }
            else {
                btree->root = NULL;
            }
            btree->dealloc(btree->pool, node->child);
            btree->dealloc(btree->pool, node->key);
            btree->dealloc(btree->pool, node->data);
            btree->dealloc(btree->pool, node);
        }
        return 0;
    }

    /* 2. 查找node是其父结点的第几个孩子结点 */
    for (idx=0; idx<=parent->num; idx++) {
        if (parent->child[idx] == node) {
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
        left = parent->child[mid];

        /* 1) 合并结点 */
        if ((node->num + left->num + 1) <= btree->max) {
            return _btree_merge(btree, left, node, mid);
        }

        /* 2) 借用结点:brother->key[num-1] */
        for (m=node->num; m>0; m--) {
            node->key[m] = node->key[m - 1];
            node->data[m] = node->data[m - 1];
            node->child[m+1] = node->child[m];
        }
        node->child[1] = node->child[0];

        node->key[0] = parent->key[mid];
        node->data[0] = parent->data[mid];
        node->num++;
        node->child[0] = left->child[left->num];
        if (NULL != left->child[left->num]) {
            left->child[left->num]->parent = node;
        }

        parent->key[mid] = left->key[left->num - 1];
        parent->data[mid] = left->data[left->num - 1];
        left->key[left->num - 1] = 0;
        left->data[left->num - 1] = NULL;
        left->child[left->num] = NULL;
        left->num--;
        return 0;
    }

    /* 4. node: 非最后一个孩子结点(node < right)
     * node as left child */
    mid = idx;
    right = parent->child[mid + 1];

    /* 1) 合并结点 */
    if ((node->num + right->num + 1) <= btree->max) {
        return _btree_merge(btree, node, right, mid);
    }

    /* 2) 借用结点: right->key[0] */
    node->key[node->num] = parent->key[mid];
    node->data[node->num] = parent->data[mid];
    ++node->num;
    node->child[node->num] = right->child[0];
    if (NULL != right->child[0]) {
        right->child[0]->parent = node;
    }

    parent->key[mid] = right->key[0];
    parent->data[mid] = right->data[0];
    for (m=0; m<right->num; m++) {
        right->key[m] = right->key[m+1];
        right->data[m] = right->data[m+1];
        right->child[m] = right->child[m+1];
    }
    right->child[m] = NULL;
    right->num--;
    return 0;
}

/******************************************************************************
 **函数名称: _btree_merge
 **功    能: 合并结点
 **输入参数:
 **     btree: B树
 **     node:
 **     brother:
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.03.12 #
 ******************************************************************************/
static int _btree_merge(btree_t *btree, btree_node_t *left, btree_node_t *right, int mid)
{
    int m;
    btree_node_t *parent = left->parent;

    left->key[left->num] = parent->key[mid];
    left->data[left->num] = parent->data[mid];
    ++left->num;

    memcpy(left->key + left->num, right->key, right->num*sizeof(int));
    memcpy(left->data + left->num, right->data, right->num*sizeof(void *));
    memcpy(left->child + left->num, right->child, (right->num+1)*sizeof(btree_node_t *));
    for (m=0; m<=right->num; m++) {
        if (NULL != right->child[m]) {
            right->child[m]->parent = left;
        }
    }
    left->num += right->num;

    for (m=mid; m<parent->num-1; m++) {
        parent->key[m] = parent->key[m+1];
        parent->data[m] = parent->data[m+1];
        parent->child[m+1] = parent->child[m+2];
    }

    parent->key[m] = 0;
    parent->data[m] = NULL;
    parent->child[m+1] = NULL;
    parent->num--;
    btree->dealloc(btree->pool, right->child);
    btree->dealloc(btree->pool, right->key);
    btree->dealloc(btree->pool, right->data);
    btree->dealloc(btree->pool, right);

    /* Check */
    if (parent->num < btree->min) {
        return btree_merge(btree, parent);
    }

    return 0;
}

/******************************************************************************
 **函数名称: btree_destroy
 **功    能: 销毁B树
 **输入参数:
 **     btree: B树
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.03.12 #
 ******************************************************************************/
int btree_destroy(btree_t *btree)
{
    int idx;
    btree_node_t *node = btree->root;

    if (NULL == node) {
        btree->dealloc(btree->pool, btree);
        return 0;
    }

    for (idx=0; idx<=node->num; ++idx) {
        if (NULL != node->child[idx]) {
            btree_node_dealloc(btree, node->child[idx]);
            continue;
        }
    }

    btree->dealloc(btree->pool, node->key);
    btree->dealloc(btree->pool, node->data);
    btree->dealloc(btree->pool, node->child);
    btree->dealloc(btree->pool, node);
    btree->dealloc(btree->pool, btree);
    return 0;
}

/******************************************************************************
 **函数名称: _btree_print
 **功    能: 打印B树结构
 **输入参数:
 **     node: B树结点
 **     deep: 结点深度
 **输出参数: NONE
 **返    回: 节点地址
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.03.12 #
 ******************************************************************************/
void _btree_print(const btree_node_t *node, int deep)
{
    int idx, d, flag = 0;

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
        fprintf(stderr, "%d ", node->key[idx]);
    }

    fprintf(stderr, ">\n");

    /* 2. Print node's children */
    for (idx=0; idx<node->num+1; idx++) {
        if (NULL != node->child[idx]) {
            _btree_print(node->child[idx], deep+1);
            flag = 1;
        }
    }

    if (1 == flag) {
        for (d=0; d<deep; d++) {
            fprintf(stderr, "|        ");
        }

        fprintf(stderr, "</%d| ", node->num);
        for (idx=0; idx<node->num; idx++) {
            fprintf(stderr, "%d ", node->key[idx]);
        }

        fprintf(stderr, ">\n");
    }
}

/******************************************************************************
 **函数名称: btree_node_alloc
 **功    能: 创建一个节点
 **输入参数:
 **     btree: B树
 **输出参数: NONE
 **返    回: 节点地址
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.03.12 #
 ******************************************************************************/
static btree_node_t *btree_node_alloc(btree_t *btree)
{
    btree_node_t *node;

    node = (btree_node_t *)btree->alloc(btree->pool, sizeof(btree_node_t));
    if (NULL == node) {
        return NULL;
    }

    node->num = 0;
    node->parent = NULL;

    /* More than (max) is for move */
    node->key = (int *)btree->alloc(btree->pool, (btree->max + 1) * sizeof(int));
    if (NULL == node->key) {
        btree->dealloc(btree->pool, node);
        return NULL;
    }

    node->data = (void **)btree->alloc(btree->pool, (btree->max + 1) * sizeof(void *));
    if (NULL == node->data) {
        btree->dealloc(btree->pool, node->key);
        btree->dealloc(btree->pool, node);
        return NULL;
    }

    /* More than (max+1) is for move */
    node->child = (btree_node_t **)btree->alloc(
        btree->pool, (btree->max+2) * sizeof(btree_node_t *));
    if (NULL == node->child) {
        btree->dealloc(btree->pool, node->key);
        btree->dealloc(btree->pool, node->data);
        btree->dealloc(btree->pool, node);
        return NULL;
    }

    return node;
}

/******************************************************************************
 **函数名称: btree_node_dealloc
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
static int btree_node_dealloc(btree_t *btree, btree_node_t *node)
{
    int idx;

    for (idx=0; idx<=node->num; ++idx) {
        if (NULL != node->child[idx]) {
            btree_node_dealloc(btree, node->child[idx]);
            continue;
        }
    }

    btree->dealloc(btree->pool, node->key);
    btree->dealloc(btree->pool, node->data);
    btree->dealloc(btree->pool, node->child);
    btree->dealloc(btree->pool, node);
    return 0;
}

/******************************************************************************
 **函数名称: btree_remove
 **功    能: 删除指定关键字
 **输入参数:
 **     btree: B树
 **     key: 关键字
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.03.12 #
 ******************************************************************************/
int btree_remove(btree_t *btree, int key, void **data)
{
    int idx;
    btree_node_t *node = btree->root;

    while (NULL != node) {
        idx = btree_key_bsearch(node->key, node->num, key);
        if (key == node->key[idx]) {
            *data = node->data[idx];
            return _btree_remove(btree, node, idx);
        }
        else if (key < node->key[idx]) {
            node = node->child[idx];
            continue;
        }

        node = node->child[idx+1];
    }

    *data = NULL;
    return 0;
}

/******************************************************************************
 **函数名称: btree_query
 **功    能: 查询指定关键字
 **输入参数:
 **     btree: B树
 **     key: 关键字
 **输出参数: NONE
 **返    回: key对应的数据
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.04.28 #
 ******************************************************************************/
void *btree_query(btree_t *btree, int key)
{
    int idx;
    btree_node_t *node = btree->root;

    while (NULL != node) {
        idx = btree_key_bsearch(node->key, node->num, key);
        if (key == node->key[idx]) {
            return node->data[idx]; /* 找到 */
        }
        else if (key < node->key[idx]) {
            node = node->child[idx];
            continue;
        }

        node = node->child[idx+1];
    }

    return NULL; /* 未找到 */
}
