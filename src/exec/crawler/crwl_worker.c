/******************************************************************************
 ** Copyright(C) 2014-2024 Qiware technology Co., Ltd
 **
 ** 文件名: crwl_worker.c
 ** 版本号: 1.0
 ** 描  述: 网络爬虫
 **         负责下载指定URL网页
 ** 作  者: # Qifeng.zou # 2014.09.04 #
 ******************************************************************************/
#include "comm.h"
#include "list.h"
#include "http.h"
#include "redo.h"
#include "crawler.h"
#include "xml_tree.h"
#include "thread_pool.h"
#include "crwl_worker.h"

static int crwl_worker_task_down_webpage(
        crwl_cntx_t *ctx, crwl_worker_t *worker, const crwl_task_down_webpage_t *args);
static int crwl_worker_task_unknown_hdl(crwl_cntx_t *ctx, crwl_worker_t *worker, const void *args);

static socket_t *crwl_worker_socket_alloc(crwl_worker_t *worker);
#define crwl_worker_socket_dealloc(worker, sck) /* 释放Socket空间 */\
{ \
    if (NULL != sck->extra) { \
        slot_dealloc(worker->slot_for_sck_extra, sck->extra); \
    } \
    FREE(sck); \
}

/* 任务处理回调函数
 *  注意：必须与crwl_task_type_e个数、顺序保持一致, 否则将会出严重问题 */
typedef int (*crwl_worker_task_hdl_t)(crwl_cntx_t *ctx, crwl_worker_t *worker, const void *args);

static crwl_worker_task_hdl_t g_crwl_worker_task_hdl[CRWL_TASK_TYPE_TOTAL] =
{
    [CRWL_TASK_TYPE_UNKNOWN] = (crwl_worker_task_hdl_t)crwl_worker_task_unknown_hdl    /* CRWL_TASK_TYPE_UNKNOWN */
    , [CRWL_TASK_DOWN_WEBPAGE] = (crwl_worker_task_hdl_t)crwl_worker_task_down_webpage /* CRWL_TASK_DOWN_WEBPAGE */
};

#define crwl_worker_task_handler(ctx, worker, task)         /* 任务处理回调 */\
    ((task->type < CRWL_TASK_TYPE_TOTAL)? \
        g_crwl_worker_task_hdl[task->type](ctx, worker, task+1) : \
        g_crwl_worker_task_hdl[CRWL_TASK_TYPE_UNKNOWN](ctx, worker, task+1))

/******************************************************************************
 **函数名称: crwl_worker_get_by_idx
 **功    能: 通过索引获取WORKER对象
 **输入参数:
 **     ctx: 全局信息
 **     idx: 索引号
 **输出参数: NONE
 **返    回: 爬虫对象
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.11.22 #
 ******************************************************************************/
crwl_worker_t *crwl_worker_get_by_idx(crwl_cntx_t *ctx, int idx)
{
    crwl_worker_t *worker;

    worker = thread_pool_get_args(ctx->workers);

    return worker + idx;
}

/******************************************************************************
 **函数名称: crwl_worker_get
 **功    能: 获取爬虫对象
 **输入参数:
 **     ctx: 全局信息
 **输出参数: NONE
 **返    回: 爬虫对象
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.10.09 #
 ******************************************************************************/
static crwl_worker_t *crwl_worker_self(crwl_cntx_t *ctx)
{
    int id;

    id = thread_pool_get_tidx(ctx->workers);
    if (id < 0) {
        return NULL;
    }

    return crwl_worker_get_by_idx(ctx, id);
}

/******************************************************************************
 **函数名称: crwl_worker_init
 **功    能: 创建爬虫对象
 **输入参数:
 **     ctx: 全局信息
 **     worker: Worker对象
 **     id: 线程索引
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 依次创建Worker的成员和所依赖的资源.
 **注意事项:
 **作    者: # Qifeng.zou # 2014.09.23 #
 ******************************************************************************/
int crwl_worker_init(crwl_cntx_t *ctx, crwl_worker_t *worker, int id)
{
    list_opt_t opt;
    crwl_conf_t *conf = &ctx->conf;

    memset(worker, 0, sizeof(crwl_worker_t));

    worker->id = id;
    worker->log = ctx->log;
    worker->scan_tm = time(NULL);

    worker->slot_for_sck_extra = slot_creat(
            conf->worker.conn_max_num,
            sizeof(crwl_worker_socket_extra_t));
    if (NULL == worker->slot_for_sck_extra) {
        log_error(worker->log, "Initialize slot pool failed!");
        return CRWL_ERR;
    }

    do {
        /* > 创建SCK链表 */
        memset(&opt, 0, sizeof(opt));

        opt.pool = (void *)NULL;
        opt.alloc = (mem_alloc_cb_t)mem_alloc;
        opt.dealloc = (mem_dealloc_cb_t)mem_dealloc;

        worker->sock_list = list_creat(&opt);
        if (NULL == worker->sock_list) {
            log_error(worker->log, "Create list failed!");
            break;
        }

        /* > 创建epoll对象 */
        worker->epid = epoll_create(CRWL_EVENT_MAX_NUM);
        if (worker->epid < 0) {
            log_error(worker->log, "Create epoll failed! errmsg:[%d] %s!");
            break;
        }

        worker->events = (struct epoll_event *)calloc(
                conf->worker.conn_max_num,
                sizeof(struct epoll_event));
        if (NULL == worker->events) {
            log_error(worker->log, "Alloc memory failed! errmsg:[%d] %s", errno, strerror(errno));
            break;
        }

        return CRWL_OK;
    } while(0);

    /* > 释放空间 */
    if (worker->epid) { CLOSE(worker->epid); }
    if (worker->slot_for_sck_extra) { slot_destroy(worker->slot_for_sck_extra); }

    return CRWL_ERR;
}

/******************************************************************************
 **函数名称: crwl_worker_destroy
 **功    能: 销毁爬虫对象
 **输入参数:
 **     ctx: 全局对象
 **     worker: 爬虫对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项: TODO: 未完成所有内存的释放!
 **作    者: # Qifeng.zou # 2014.09.23 #
 ******************************************************************************/
int crwl_worker_destroy(crwl_cntx_t *ctx, crwl_worker_t *worker)
{
    CLOSE(worker->epid);
    FREE(worker->events);
    slot_destroy(worker->slot_for_sck_extra);
    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_worker_fetch_task
 **功    能: 获取工作任务
 **输入参数:
 **     worker: 爬虫对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.09.25 #
 ******************************************************************************/
static int crwl_worker_fetch_task(crwl_cntx_t *ctx, crwl_worker_t *worker)
{
    int idx, num;
    void *data;
    crwl_task_t *t;
    crwl_conf_t *conf = &ctx->conf;
    queue_t *workq = ctx->workq[worker->id];

    /* 1. 判断是否应该取任务 */
    if (0 == queue_used(workq)
        || worker->sock_list->num >= conf->worker.conn_max_num)
    {
        return CRWL_OK;
    }

    /* 2. 从任务队列取数据 */
    num = conf->worker.conn_max_num - worker->sock_list->num;

    for (idx=0; idx<num; ++idx) {
        data = queue_pop(workq);
        if (NULL == data) {
            log_trace(worker->log, "Didn't pop data from queue!");
            return CRWL_OK;
        }

        /* 3. 连接远程Web服务器 */
        t = (crwl_task_t *)data;

        crwl_worker_task_handler(ctx, worker, t);

        queue_dealloc(workq, data);
    }
    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_worker_recv_data
 **功    能: 接收数据
 **输入参数:
 **     worker: 爬虫对象
 **     sck: 套接字对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 接收数据直到出现EAGAIN -- 一次性接收最大量的数据
 **注意事项:
 **作    者: # Qifeng.zou # 2014.10.30 #
 ******************************************************************************/
int crwl_worker_recv_data(crwl_cntx_t *ctx, crwl_worker_t *worker, socket_t *sck)
{
    int n, left;
    crwl_worker_socket_extra_t *extra = (crwl_worker_socket_extra_t *)sck->extra;
    http_response_t *response = &extra->response;

    sck->rdtm = time(NULL);

    while (1) {
        left = sck->recv.total - sck->recv.off;

        n = read(sck->fd, sck->recv.addr + sck->recv.off, left);
        if (n > 0) {
            log_debug(worker->log, "Recv! uri:%s n:%d", extra->webpage.uri, n);

            /* 提取应答信息 */
            if (!response->status) {
                http_parse_response(sck->recv.addr, response);
            }

            /* 将HTML数据写入文件 */
            extra->webpage.size += n;
            sck->recv.off += n;
            sck->recv.addr[sck->recv.off] = '\0';
            if (sck->recv.off >= CRWL_SYNC_SIZE) {
                crwl_worker_webpage_fsync(worker, sck);
            }

            /* 判断是否接收完所有字节 */
            if (response->status
                && extra->webpage.size >= response->total_len)
            {
                log_info(worker->log, "Recv all bytes! uri:%s size:%d total:%u fname:%s.html",
                        extra->webpage.uri, extra->webpage.size,
                        response->total_len, extra->webpage.fname);
                goto RECV_DATA_END;
            }
            continue;
        }
        else if (0 == n) {
        RECV_DATA_END:
            log_info(worker->log, "End of recv! uri:%s size:%d",
                    extra->webpage.uri, extra->webpage.size);

            crwl_worker_webpage_fsync(worker, sck);
            crwl_worker_webpage_finfo(ctx, worker, sck);
            crwl_worker_remove_sock(worker, sck);
            return CRWL_SCK_CLOSE;
        }
        else if (n < 0 && EAGAIN == errno) {
            log_debug(worker->log, "Again! uri:%s", extra->webpage.uri);
            return CRWL_OK;
        }
        else if (EINTR == errno) {
            continue;
        }

        /* > 异常情况处理: 释放内存空间 */
        crwl_worker_webpage_fsync(worker, sck);
        crwl_worker_webpage_finfo(ctx, worker, sck);
        crwl_worker_remove_sock(worker, sck);

        log_error(worker->log, "errmsg:[%d] %s! uri:%s ip:%s",
                errno, strerror(errno), extra->webpage.uri, extra->webpage.ip);
        return CRWL_ERR;
    }
}

/******************************************************************************
 **函数名称: crwl_worker_send_data
 **功    能: 发送数据
 **输入参数:
 **     worker: 爬虫对象
 **     sck: 套接字对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **     1. 如果发送指针为空, 则从发送队列取数据
 **     2. 发送数据
 **注意事项: 发送数据直到出现EAGAIN -- 一次性发送最大量的数据
 **作    者: # Qifeng.zou # 2014.09.24 #
 ******************************************************************************/
int crwl_worker_send_data(crwl_cntx_t *ctx, crwl_worker_t *worker, socket_t *sck)
{
    int n, left;
    crwl_data_info_t *info;
    struct epoll_event ev;
    crwl_worker_socket_extra_t *extra = (crwl_worker_socket_extra_t *)sck->extra;

    sck->wrtm = time(NULL);

    while (1) {
        /* 1. 从发送列表取数据 */
        if (!sck->send.addr) {
            info = (crwl_data_info_t *)list_lpop(extra->send_list);
            if (NULL == info) {
                memset(&ev, 0, sizeof(ev));

                ev.data.ptr = sck;
                ev.events = EPOLLIN | EPOLLET;  /* 边缘触发 */

                epoll_ctl(worker->epid, EPOLL_CTL_MOD, sck->fd, &ev);

                return CRWL_OK;
            }

            sck->send.addr = (void *)info;
            sck->send.off = sizeof(crwl_data_info_t); /* 不发送头部信息 */
            sck->send.total = info->length;
        }

        /* 2. 发送数据 */
        left = sck->send.total - sck->send.off;

        log_debug(worker->log, "[HTTP] %s", sck->send.addr + sck->send.off);

        n = Writen(sck->fd, sck->send.addr + sck->send.off, left);
        if (n < 0) {
            log_error(worker->log, "errmsg:[%d] %s! total:%d off:%d",
                    errno, strerror(errno), sck->send.total, sck->send.off);

            FREE(sck->send.addr);
            sck->send.addr = NULL;
            crwl_worker_remove_sock(worker, sck);
            return CRWL_ERR;
        }
        else if (n != left) {
            sck->send.off += n;
            left = sck->send.total - sck->send.off;
            /* 等待下次继续发送, 空间暂不释放 */
            return CRWL_OK;
        }

        sck->send.off += n;
        left = sck->send.total - sck->send.off;

        /* 释放空间 */
        FREE(sck->send.addr);

        sck->send.addr = NULL;
        sck->send.total = 0;
        sck->send.off = 0;
    }

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_worker_timeout_hdl
 **功    能: 爬虫的超时处理
 **输入参数:
 **     ctx: 全局对象
 **     worker: 爬虫对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **     1. 依次遍历套接字, 判断是否超时
 **     2. 超时关闭套接字、释放内存等
 **注意事项: TODO: 可通过封装链表操作+回调函数增强代码复用性
 **作    者: # Qifeng.zou # 2014.09.28 #
 ******************************************************************************/
static int crwl_worker_timeout_hdl(crwl_cntx_t *ctx, crwl_worker_t *worker)
{
    time_t ctm = time(NULL);
    list_node_t *node, *next;
    socket_t *sck;
    crwl_conf_t *conf = &ctx->conf;
    crwl_worker_socket_extra_t *extra;

    /* 1. 依次遍历套接字, 判断是否超时 */
    node = worker->sock_list->head;
    for (; NULL != node; node = next) {
        next = node->next;

        sck = (socket_t *)node->data;
        if (NULL == sck) {
            continue;
        }

        extra = (crwl_worker_socket_extra_t *)sck->extra;

        /* 超时未发送或接收数据时, 认为无数据传输, 将直接关闭套接字 */
        if ((ctm - sck->rdtm <= conf->worker.conn_tmout_sec)
            || (ctm - sck->wrtm <= conf->worker.conn_tmout_sec))
        {
            continue; /* 未超时 */
        }

        log_warn(worker->log, "Timeout! uri:%s ip:%s size:%d fname:%s!",
                extra->webpage.uri, extra->webpage.ip,
                extra->webpage.size, extra->webpage.fname);

        crwl_worker_webpage_fsync(worker, sck);
        crwl_worker_webpage_finfo(ctx, worker, sck);

        crwl_worker_remove_sock(worker, sck);
    }

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_worker_event_hdl
 **功    能: 爬虫的事件处理
 **输入参数:
 **     ctx: 全局对象
 **     worker: 爬虫对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.09.23 #
 ******************************************************************************/
static int crwl_worker_event_hdl(crwl_cntx_t *ctx, crwl_worker_t *worker)
{
    int idx;
    socket_t *sck;
    time_t ctm = time(NULL);

    /* 1. 依次遍历套接字, 判断是否可读可写 */
    for (idx=0; idx<worker->fds; ++idx) {
        sck = (socket_t *)worker->events[idx].data.ptr;
        /* 1.1 判断是否可读 */
        if (worker->events[idx].events & EPOLLIN) {
            /* 接收网络数据 */
            if (sck->recv_cb(ctx, worker, sck)) {
                continue; /* 异常: 不必判断是否可写(套接字已关闭) */
            }
        }

        /* 1.2 判断是否可写 */
        if (worker->events[idx].events & EPOLLOUT) {
            /* 发送网络数据 */
            if (sck->send_cb(ctx, worker, sck)) {
                continue; /* 异常: 套接字已关闭 */
            }
        }
    }

    /* 2. 超时扫描 */
    if (ctm - worker->scan_tm > CRWL_SCAN_TMOUT_SEC) {
        worker->scan_tm = ctm;

        crwl_worker_timeout_hdl(ctx, worker);
    }

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_worker_routine
 **功    能: 运行爬虫线程
 **输入参数:
 **     _ctx: 全局信息
 **输出参数: NONE
 **返    回: VOID *
 **实现描述:
 **     1. 创建爬虫对象
 **     2. 设置读写集合
 **     3. 等待事件通知
 **     4. 进行事件处理
 **注意事项: 当无套接字时, epoll_wait()将不断的返回-1.
 **作    者: # Qifeng.zou # 2014.09.23 #
 ******************************************************************************/
void *crwl_worker_routine(void *_ctx)
{
    crwl_worker_t *worker;
    crwl_cntx_t *ctx = (crwl_cntx_t *)_ctx;
    crwl_conf_t *conf = &ctx->conf;

    /* 1. 获取爬虫对象 */
    worker = crwl_worker_self(ctx);
    if (NULL == worker) {
        log_error(ctx->log, "Initialize worker failed!");
        pthread_exit((void *)-1);
        return (void *)-1;
    }

    while (1) {
        /* 2. 获取爬虫任务 */
        crwl_worker_fetch_task(ctx, worker);

        /* 3. 等待事件通知 */
        worker->fds = epoll_wait(
                worker->epid, worker->events,
                conf->worker.conn_max_num, CRWL_EVENT_TMOUT_MSEC);
        if (worker->fds < 0) {
            if (EINTR == errno) {
                usleep(500);
                continue;
            }

            log_error(ctx->log, "errmsg:[%d] %s!", errno, strerror(errno));

            crwl_worker_destroy(ctx, worker);
            abort();
            return (void *)-1;
        }
        else if (0 == worker->fds) { /* Timeout */
            crwl_worker_timeout_hdl(ctx, worker);
            continue;
        }

        /* 4. 进行事件处理 */
        crwl_worker_event_hdl(ctx, worker);
    }

    crwl_worker_destroy(ctx, worker);
    pthread_exit((void *)-1);
    return (void *)-1;
}

/******************************************************************************
 **函数名称: crwl_worker_add_sock
 **功    能: 添加套接字
 **输入参数:
 **     worker: 爬虫对象
 **     sck: 套接字
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.09.24 #
 ******************************************************************************/
int crwl_worker_add_sock(crwl_worker_t *worker, socket_t *sck)
{
    struct epoll_event ev;
    crwl_worker_socket_extra_t *extra = (crwl_worker_socket_extra_t *)sck->extra;

    /* > 设置标识量 */
    sck->recv.addr = extra->recv;
    sck->recv.off = 0;
    sck->recv.total = CRWL_RECV_SIZE;

    /* > 插入链表尾 */
    if (list_rpush(worker->sock_list, sck)) {
        log_error(worker->log, "Insert socket node failed!");
        return CRWL_ERR;
    }

    /* > 加入epoll监听(首先是发送数据, 所以需设置EPOLLOUT,
     * 又可能服务端主动断开连接, 所以需要设置EPOLLIN, 否则可能出现EPIPE的异常) */
    memset(&ev, 0, sizeof(ev));

    ev.data.ptr = sck;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET; /* 边缘触发 */

    epoll_ctl(worker->epid, EPOLL_CTL_ADD, sck->fd, &ev);

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_worker_query_sock
 **功    能: 通过fd查找socket对象
 **输入参数:
 **     worker: 爬虫对象
 **     fd: 套接字ID
 **输出参数: NONE
 **返    回: Socket对象
 **实现描述:
 **注意事项: TODO: 可通过封装链表操作+回调函数增强代码复用性
 **作    者: # Qifeng.zou # 2014.10.30 #
 ******************************************************************************/
socket_t *crwl_worker_query_sock(crwl_worker_t *worker, int fd)
{
    list_node_t *node;
    socket_t *sck;

    node = (list_node_t *)worker->sock_list->head;
    for (; NULL != node; node = node->next) {
        sck = (socket_t *)node->data;
        if (fd == sck->fd) {
            return sck;
        }
    }

    return NULL;
}

/******************************************************************************
 **函数名称: crwl_worker_remove_sock
 **功    能: 删除套接字
 **输入参数:
 **     worker: 爬虫对象
 **     sck: 套接字
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.09.24 #
 ******************************************************************************/
int crwl_worker_remove_sock(crwl_worker_t *worker, socket_t *sck)
{
    void *p;
    struct epoll_event ev;
    crwl_worker_socket_extra_t *extra = (crwl_worker_socket_extra_t *)sck->extra;

    log_debug(worker->log, "Remove socket! ip:%s port:%d",
            extra->webpage.ip, extra->webpage.port);

    /* >> 移除epoll监听 */
    epoll_ctl(worker->epid, EPOLL_CTL_DEL, sck->fd, &ev);

    if (extra->webpage.fp) {
        FCLOSE(extra->webpage.fp);
    }
    CLOSE(sck->fd);

    /* >> 释放发送链表 */
    while (1) {
        p = list_lpop(extra->send_list);
        if (NULL == p) {
            break;
        }

        FREE(p);
    }

    list_destroy(extra->send_list, NULL, (mem_dealloc_cb_t)mem_dealloc);

    /* >> 从套接字链表剔除SCK */
    if (list_remove(worker->sock_list, sck)) {
        log_fatal(worker->log, "Didn't find special socket!");
    }

    crwl_worker_socket_dealloc(worker, sck);

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_worker_add_http_get_req
 **功    能: 添加HTTP GET请求
 **输入参数:
 **     worker: 爬虫对象
 **     sck: 指定套接字
 **     uri: 源URI
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.10.12 #
 ******************************************************************************/
int crwl_worker_add_http_get_req(crwl_worker_t *worker, socket_t *sck, const char *uri)
{
    void *addr, *p;
    crwl_data_info_t *info;
    crwl_worker_socket_extra_t *extra = (crwl_worker_socket_extra_t *)sck->extra;

    do {
        /* > 新建HTTP GET请求 */
        p = (void *)calloc(1, sizeof(crwl_data_info_t) + HTTP_GET_REQ_STR_LEN);
        if (NULL == p) {
            log_error(worker->log, "Alloc memory from slab failed!");
            break;
        }

        info = p;
        addr = p + sizeof(crwl_data_info_t);

        if (http_get_request(uri, addr, HTTP_GET_REQ_STR_LEN)) {
            log_error(worker->log, "HTTP GET request string failed");
            break;
        }

        log_debug(worker->log, "HTTP: %s", addr);

        info->type = CRWL_HTTP_GET_REQ;
        info->length = sizeof(crwl_data_info_t) + strlen((const char *)addr);

        /* > 将结点插入链表 */
        if (list_rpush(extra->send_list, p)) {
            log_error(worker->log, "Insert list tail failed");
            break;
        }

        return CRWL_OK;
    } while(0);

    /* 释放空间 */
    FREE(p);

    return CRWL_ERR;
}

/******************************************************************************
 **函数名称: crwl_worker_webpage_creat
 **功    能: 打开网页存储文件
 **输入参数:
 **     sck: 套接字对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项: 网页存储文件在此fopen(), 在crwl_worker_remove_sock()中fclose().
 **作    者: # Qifeng.zou # 2014.10.15 #
 ******************************************************************************/
int crwl_worker_webpage_creat(crwl_cntx_t *ctx, crwl_worker_t *worker, socket_t *sck)
{
    struct tm loctm;
    char path[FILE_NAME_MAX_LEN];
    crwl_conf_t *conf = &ctx->conf;
    crwl_worker_socket_extra_t *extra = (crwl_worker_socket_extra_t *)sck->extra;

    local_time(&sck->crtm.time, &loctm);

    extra->webpage.size = 0;
    extra->webpage.idx = ++worker->total;

    snprintf(extra->webpage.fname, sizeof(extra->webpage.fname),
            "%02d-%08llu-%04d%02d%02d%02d%02d%02d%03d",
            worker->id, extra->webpage.idx,
            loctm.tm_year+1900, loctm.tm_mon+1, loctm.tm_mday,
            loctm.tm_hour, loctm.tm_min, loctm.tm_sec, sck->crtm.millitm);

    snprintf(path, sizeof(path), "%s/%s.html",
            conf->download.path, extra->webpage.fname);

    Mkdir2(path, 0777);

    extra->webpage.fp = fopen(path, "w");
    if (NULL == extra->webpage.fp) {
        log_error(worker->log, "errmsg:[%d] %s! path:%s", errno, strerror(errno), path);
        return CRWL_ERR;
    }

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_worker_webpage_finfo
 **功    能: 创建网页的信息文件
 **输入参数:
 **     worker: 爬虫对象
 **     sck: 套接字对象
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 首先将数据写入临时目录, 再将临时文件移入指定目录
 **注意事项:
 **作    者: # Qifeng.zou # 2014.10.17 #
 ******************************************************************************/
int crwl_worker_webpage_finfo(crwl_cntx_t *ctx, crwl_worker_t *worker, socket_t *sck)
{
    FILE *fp;
    struct tm loctm;
    char path[FILE_NAME_MAX_LEN],
         temp[FILE_NAME_MAX_LEN];
    crwl_conf_t *conf = &ctx->conf;
    crwl_worker_socket_extra_t *extra = (crwl_worker_socket_extra_t *)sck->extra;

    ++worker->down_webpage_total;
    if (HTTP_REP_STATUS_OK != extra->response.status) {
        ++worker->err_webpage_total;
    }

    local_time(&sck->crtm.time, &loctm);
    snprintf(temp, sizeof(temp), "%s/wpi/.temp/%s.wpi", conf->download.path, extra->webpage.fname);
    snprintf(path, sizeof(path), "%s/wpi/%s.wpi", conf->download.path, extra->webpage.fname);

    Mkdir2(temp, 0777);

    /* 1. 新建文件 */
    fp = fopen(temp, "w");
    if (NULL == fp) {
        log_error(worker->log, "errmsg:[%d] %s! path:%s", errno, strerror(errno), path);
        return CRWL_ERR;
    }

    /* 2. 写入校验内容 */
    crwl_write_webpage_finfo(fp, extra);

    fclose(fp);

    rename(temp, path);

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_worker_socket_alloc
 **功    能: 为Socket对象申请空间
 **输入参数:
 **     worker: 爬虫对象
 **输出参数: NONE
 **返    回: Socket对象
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.11.27 #
 ******************************************************************************/
static socket_t *crwl_worker_socket_alloc(crwl_worker_t *worker)
{
    socket_t *sck;
    list_opt_t opt;
    crwl_worker_socket_extra_t *extra;

    /* > 创建SCK对象 */
    sck = (socket_t *)calloc(1, sizeof(socket_t));
    if (NULL == sck) {
        log_error(worker->log, "Alloc memory from slab failed!");
        return NULL;
    }

    /* > 分配SCK数据 */
    extra = slot_alloc(worker->slot_for_sck_extra, sizeof(crwl_worker_socket_extra_t));
    if (NULL == extra) {
        log_error(worker->log, "Alloc memory from slab failed!");
        FREE(sck);
        return NULL;
    }

    memset(extra, 0, sizeof(crwl_worker_socket_extra_t));

    /* > 创建发送链表 */
    memset(&opt, 0, sizeof(opt));

    opt.pool = (void *)NULL;
    opt.alloc = (mem_alloc_cb_t)mem_alloc;
    opt.dealloc = (mem_dealloc_cb_t)mem_dealloc;

    extra->send_list = list_creat(&opt);
    if (NULL == extra->send_list) {
        log_error(worker->log, "Create list failed!");
        FREE(sck);
        slot_dealloc(worker->slot_for_sck_extra, extra);
        return NULL;
    }

    sck->extra = extra;

    return sck;
}

/******************************************************************************
 **函数名称: crwl_worker_task_down_webpage
 **功    能: 加载网页的任务处理
 **输入参数:
 **     ctx: 全局对象
 **     worker: 爬虫对象
 **     args: 通过URL加载网页的任务的参数
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.09.25 #
 ******************************************************************************/
static int crwl_worker_task_down_webpage(
        crwl_cntx_t *ctx, crwl_worker_t *worker, const crwl_task_down_webpage_t *args)
{
    int fd;
    socket_t *sck;
    crwl_worker_socket_extra_t *extra;

    /* > 连接远程WEB服务器 */
    fd = tcp_connect_async(args->family, args->ip, args->port);
    if (fd < 0) {
        log_error(worker->log, "errmsg:[%d] %s! family:%d ip:%s uri:%s",
            errno, strerror(errno), args->family, args->ip, args->uri);
        return CRWL_OK;
    }

    /* > 将FD等信息加入套接字链表 */
    sck = crwl_worker_socket_alloc(worker);
    if (NULL == sck) {
        CLOSE(fd);
        log_error(worker->log, "Alloc memory from slab failed!");
        return CRWL_ERR;
    }

    extra = (crwl_worker_socket_extra_t *)sck->extra;

    sck->fd = fd;
    ftime(&sck->crtm);
    sck->rdtm = sck->crtm.time;
    sck->wrtm = sck->crtm.time;
    sck->recv.addr = extra->recv;

    snprintf(extra->webpage.uri, sizeof(extra->webpage.uri), "%s", args->uri);
    snprintf(extra->webpage.ip, sizeof(extra->webpage.ip), "%s", args->ip);
    extra->webpage.port = args->port;
    extra->webpage.depth = args->depth;

    sck->recv_cb = (socket_recv_cb_t)crwl_worker_recv_data;
    sck->send_cb = (socket_send_cb_t)crwl_worker_send_data;

    if (crwl_worker_add_sock(worker, sck)) {
        CLOSE(fd);
        log_error(worker->log, "Add socket into list failed!");
        crwl_worker_socket_dealloc(worker, sck);
        return CRWL_ERR;
    }

    /* > 添加HTTP GET请求 */
    if (crwl_worker_add_http_get_req(worker, sck, args->uri)) {
        log_error(worker->log, "Add http get request failed!");

        crwl_worker_remove_sock(worker, sck);
        return CRWL_ERR;
    }

    /* > 新建存储文件 */
    if (crwl_worker_webpage_creat(ctx, worker, sck)) {
        log_error(worker->log, "Save webpage failed!");

        crwl_worker_remove_sock(worker, sck);
        return CRWL_ERR;
    }

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_worker_task_unknown_hdl
 **功    能: 未知类型的任务处理
 **输入参数:
 **     worker: 爬虫对象
 **     args: 附加参数
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.12.12 #
 ******************************************************************************/
static int crwl_worker_task_unknown_hdl(
    crwl_cntx_t *ctx, crwl_worker_t *worker, const void *args)
{
    log_error(worker->log, "Task type is unknown!");
    return CRWL_ERR;
}
