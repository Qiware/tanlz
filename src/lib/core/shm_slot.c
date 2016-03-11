/******************************************************************************
 ** Copyright(C) 2014-2024 Qiware technology Co., Ltd
 **
 ** 文件名: shm_slot.c
 ** 版本号: 1.0
 ** 描  述: 
 ** 作  者: # Qifeng.zou # Wed 06 May 2015 10:03:15 AM CST #
 ******************************************************************************/
#include "comm.h"
#include "shm_ring.h"
#include "shm_slot.h"

/******************************************************************************
 **函数名称: shm_slot_total
 **功    能: 计算共享内存池总空间
 **输入参数: 
 **     num: 内存块数
 **     size: 内存块大小
 **输出参数: NONE
 **返    回: 共享内存池总空间
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.05.06 #
 ******************************************************************************/
size_t shm_slot_total(int num, size_t size)
{
    /* > max必须为2的n次方 */
    if (!ISPOWEROF2(num)) {
        return (size_t)-1;
    }

    return sizeof(shm_slot_t) + shm_ring_total(num) + num * size;
}

/******************************************************************************
 **函数名称: shm_slot_init
 **功    能: 初始化共享内存池
 **输入参数: 
 **     num: 内存块数
 **     size: 内存块大小
 **输出参数: NONE
 **返    回: 内存池对象
 **实现描述: 
 **      ----------------------------------------------------------------
 **     |      |      |                                                  |
 **     | slot | ring |           可  分  配  空  间                     |
 **     |      |      |                                                  |
 **      ----------------------------------------------------------------
 **     slot: 头部数据
 **     ring: 管理队列 - 负责可分配内存的申请(弹出队列)和回收(放入队列)
 **注意事项: 放入队列的偏移量是相对内存池首地址而言的! 
 **作    者: # Qifeng.zou # 2015.05.06 #
 ******************************************************************************/
shm_slot_t *shm_slot_init(void *addr, int num, size_t size)
{
    int i;
    off_t off, tsz;
    shm_slot_t *slot;
    shm_ring_t *ring;

    /* > 初始化SHM-SLOT */
    slot = (shm_slot_t *)addr;
    slot->max = num;
    slot->size = size;

    /* > 初始化SHM-RING */
    tsz = shm_ring_total(num);
    if ((off_t)-1 == tsz) {
        return NULL;
    }

    ring = shm_ring_init(addr + sizeof(shm_slot_t), num);
    if (NULL == ring) {
        return NULL;
    }

    /* > 插入管理队列 */
    off = sizeof(shm_slot_t) + tsz;
    for (i=0; i<num; ++i, off+=size) {
        if (shm_ring_push(ring, off)) {
            return NULL;
        }
    }

    return slot;
}

/******************************************************************************
 **函数名称: shm_slot_alloc
 **功    能: 申请空间
 **输入参数: 
 **     slot: 内存块对象
 **输出参数: NONE
 **返    回: 内存地址
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.05.06 #
 ******************************************************************************/
void *shm_slot_alloc(shm_slot_t *slot, int size)
{
    off_t off;
    shm_ring_t *ring = (shm_ring_t *)(slot + 1);

    if (size > slot->size) {
        return NULL;
    }

    off = shm_ring_pop(ring);
    if ((off_t)-1 == off) {
        return NULL;
    }

    return (void *)slot + off;
}

/******************************************************************************
 **函数名称: shm_slot_dealloc
 **功    能: 回收空间
 **输入参数: 
 **     slot: 内存块对象
 **     p: 需要释放的空间地址
 **输出参数: NONE
 **返    回: VOID
 **实现描述: 将指针转化为偏移量再放入环形队列!
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.05.06 #
 ******************************************************************************/
void shm_slot_dealloc(shm_slot_t *slot, void *p)
{
    shm_ring_t *ring = (shm_ring_t *)(slot + 1);

    if (NULL == p) return;

    if (shm_ring_push(ring, p - (void *)slot)) {
        assert(0);
    }
}

/******************************************************************************
 **函数名称: shm_slot_print
 **功    能: 打印内存池可分配空间
 **输入参数: 
 **     slot: 内存块对象
 **输出参数: NONE
 **返    回: VOID
 **实现描述: 打印内存池队列中存在的偏移量
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.05.06 #
 ******************************************************************************/
void shm_slot_print(shm_slot_t *slot)
{
    shm_ring_t *ring = (shm_ring_t *)(slot + 1);

    shm_ring_print(ring);
}
