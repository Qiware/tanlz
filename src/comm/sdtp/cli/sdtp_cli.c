#include "shm_opt.h"
#include "syscall.h"
#include "sdtp_cmd.h"
#include "sdtp_cli.h"
#include "sdtp_ssvr.h"
#include "sdtp_priv.h"

static int _sdtp_cli_init(sdtp_cli_t *cli, int idx);
static int sdtp_cli_shmat(sdtp_cli_t *cli);
static int sdtp_cli_cmd_usck(sdtp_cli_t *cli, int idx);

#define sdtp_cli_unix_path(cli, path, idx) \
    snprintf(path, sizeof(path), "./temp/sdtp/snd/%s/%s_cli_%d.usck", \
            cli->conf.name, cli->conf.name, idx)

/******************************************************************************
 **函数名称: sdtp_cli_init
 **功    能: 发送端初始化(对外接口)
 **输入参数: 
 **     conf: 配置信息
 **     idx: CLI编号
 **     log: 日志对象
 **输出参数: NONE
 **返    回: 发送对象
 **实现描述: 
 **     1. 创建CLI对象
 **     2. 加载配置信息
 **     3. 初始化处理
 **注意事项: 
 **     某发送服务的不同cli对象的编号必须不同，否则将会出现绑定失败的问题!
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
sdtp_cli_t *sdtp_cli_init(const sdtp_ssvr_conf_t *conf, int idx, log_cycle_t *log)
{
    sdtp_cli_t *cli;
    mem_pool_t *pool;

    /* 1. 创建内存池 */
    pool = mem_pool_creat(1 * KB);
    if (NULL == pool)
    {
        log_error(log, "errmsg:[%d] %s!", errno, strerror(errno));
        return NULL;
    }

    /* 2. 创建CLI对象 */
    cli = (sdtp_cli_t *)mem_pool_alloc(pool, sizeof(sdtp_cli_t));
    if (NULL == cli)
    {
        log_error(log, "Alloc memory from pool failed!");
        mem_pool_destroy(pool);
        return NULL;
    }

    cli->log = log;
    cli->pool = pool;
    
    /* 2. 加载配置信息 */
    memcpy(&cli->conf, conf, sizeof(sdtp_ssvr_conf_t));

    /* 3. 根据配置进行初始化 */
    if (_sdtp_cli_init(cli, idx))
    {
        log_error(log, "Initialize client of sdtp failed!");
        mem_pool_destroy(pool);
        return NULL;
    }

    return cli;
}

/******************************************************************************
 **函数名称: _sdtp_cli_init
 **功    能: 发送端初始化
 **输入参数: 
 **     cli: CLI对象
 **     idx: CLI编号
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int _sdtp_cli_init(sdtp_cli_t *cli, int idx)
{
    /* 1. 连接共享内存
     * 2. 创建通信套接字 */
    if (sdtp_cli_shmat(cli)
        || sdtp_cli_cmd_usck(cli, idx))
    {
        log_error(cli->log, "Initialize client of sdtp failed!");
        return SDTP_ERR;
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_cli_shmat
 **功    能: Attach发送队列
 **输入参数: 
 **     cli: 上下文信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_cli_shmat(sdtp_cli_t *cli)
{
    int idx;
    key_t key;
    sdtp_queue_conf_t *qcf;
    sdtp_ssvr_conf_t *conf = &cli->conf;

    /* 1. 新建队列对象 */
    cli->sq = (shm_queue_t **)mem_pool_alloc(cli->pool, conf->snd_thd_num * sizeof(shm_queue_t *));
    if (NULL == cli->sq)
    {
        log_error(cli->log, "errmsg:[%d] %s!", errno, strerror(errno));
        return SDTP_ERR;
    }

    /* 2. 连接共享队列 */
    qcf = &conf->qcf;
    for (idx=0; idx<conf->snd_thd_num; ++idx)
    {
        key = shm_ftok(qcf->name, idx);

        cli->sq[idx] = shm_queue_attach(key);
        if (NULL == cli->sq[idx])
        {
            log_error(cli->log, "errmsg:[%d] %s! path:[%s]", errno, strerror(errno), qcf->name);
            return SDTP_ERR;
        }
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_cli_cmd_usck
 **功    能: 创建命令套接字
 **输入参数: 
 **     cli: 上下文信息
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_cli_cmd_usck(sdtp_cli_t *cli, int idx)
{
    char path[FILE_NAME_MAX_LEN];

    sdtp_cli_unix_path(cli, path, idx);

    cli->cmdfd = unix_udp_creat(path);
    if (cli->cmdfd < 0)
    {
        log_error(cli->log, "errmsg:[%d] %s! path:%s", errno, strerror(errno), path);
        return SDTP_ERR;                    
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_cli_cmd_send_req
 **功    能: 通知Send服务线程
 **输入参数: 
 **     cli: 上下文信息
 **     idx: 发送服务ID
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
static int sdtp_cli_cmd_send_req(sdtp_cli_t *cli, int idx)
{
    sdtp_cmd_t cmd;
    char path[FILE_NAME_MAX_LEN];
    sdtp_ssvr_conf_t *conf = &cli->conf;

    memset(&cmd, 0, sizeof(cmd));

    cmd.type = SDTP_CMD_SEND_ALL;
    sdtp_ssvr_usck_path(conf, path, idx);

    if (unix_udp_send(cli->cmdfd, path, &cmd, sizeof(cmd)) < 0)
    {
        log_debug(cli->log, "errmsg:[%d] %s! path:%s", errno, strerror(errno), path);
        return SDTP_ERR;
    }

    return SDTP_OK;
}

/******************************************************************************
 **函数名称: sdtp_cli_send
 **功    能: 发送指定数据(对外接口)
 **输入参数: 
 **     cli: 上下文信息
 **     type: 数据类型
 **     data: 数据地址
 **     size: 数据长度
 **输出参数: NONE
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     将数据按照约定格式放入队列中
 **注意事项: 
 **     1. 只能用于发送自定义数据类型, 而不能用于系统数据类型
 **     2. 不用关注变量num在多线程中的值, 因其不影响安全性
 **作    者: # Qifeng.zou # 2015.01.14 #
 ******************************************************************************/
int sdtp_cli_send(sdtp_cli_t *cli, int type, const void *data, size_t size)
{
    void *addr;
    uint32_t idx;
    static uint32_t num = 0;
    sdtp_header_t *header;
    sdtp_ssvr_conf_t *conf = &cli->conf;
    shm_queue_info_t *info;

    idx = (num++) % conf->snd_thd_num;

    /* 1. 校验类型和长度 */
    info = cli->sq[idx]->info;

    if ((type >= SDTP_TYPE_MAX)
        || info->num >= info->max
        || (size + sizeof(sdtp_header_t) > info->size))
    {
        log_error(cli->log, "Type or length is invalid! type:%d size:%u num:%d/%d",
                type, size, info->num, info->max);
        return SDTP_ERR;
    }

    /* 2. 申请存储空间 */
    addr = shm_queue_malloc(cli->sq[idx]);
    if (NULL == addr)
    {
        if (0 == num%2)
        {
            sdtp_cli_cmd_send_req(cli, idx);
        }
        log_error(cli->log, "Queue space isn't enough!");
        return SDTP_ERR;
    }

    log_debug(cli->log, "%d: max:%d num:%d", num, info->max, info->num);

    /* 3. 放入队列空间 */
    header = (sdtp_header_t *)addr;
    header->type = type;
    header->length = size;
    header->flag = SDTP_EXP_MESG; /* 自定义类型 */
    header->checksum = SDTP_CHECK_SUM;

    memcpy(addr + sizeof(sdtp_header_t), data, size);

    /* 4. 压入发送队列 */
    if (shm_queue_push(cli->sq[idx], addr))
    {
        log_error(cli->log, "Push data into queue failed!");
        shm_queue_dealloc(cli->sq[idx], addr);
        return SDTP_ERR;
    }

    /* 5. 通知发送服务 */
    if (0 == num % 50)
    {
        sdtp_cli_cmd_send_req(cli, idx);
    }

    log_debug(cli->log, "[%d] Push Success! type:%d size:%u", num, type, size);

    return SDTP_OK;
}
