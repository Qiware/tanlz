/******************************************************************************
 ** Coypright(C) 2014-2024 Xundao technology Co., Ltd
 **
 ** 文件名: agtd_conf.c
 ** 版本号: 1.0
 ** 描  述: 代理服务配置
 **         负责从代理服务配置文件(agentd.xml)中提取有效信息
 ** 作  者: # Qifeng.zou # 2014.10.28 #
 ******************************************************************************/
#include "agentd.h"
#include "syscall.h"
#include "xml_tree.h" 
#include "mem_pool.h"
#include "agtd_conf.h"

static int agtd_conf_parse(xml_tree_t *xml, agent_conf_t *conf, log_cycle_t *log);

static int agtd_conf_load_comm(xml_tree_t *xml, agtd_conf_t *conf, log_cycle_t *log);
static int agtd_conf_load_gate(xml_tree_t *xml, agent_conf_t *conf, log_cycle_t *log);
static int agtd_conf_load_sdtp(xml_tree_t *xml, dsnd_conf_t *conf, log_cycle_t *log);

/* 加载配置信息 */
agtd_conf_t *agtd_conf_load(const char *path, log_cycle_t *log)
{
    xml_opt_t opt;
    agtd_conf_t *conf;
    mem_pool_t *mem_pool;
    xml_tree_t *xml = NULL;

    /* > 创建配置内存池 */
    mem_pool = mem_pool_creat(4 * KB);
    if (NULL == mem_pool)
    {
        log_error(log, "Create memory pool failed!");
        return NULL;
    }

    do
    {
        /* > 创建配置对象 */
        conf = (agtd_conf_t *)calloc(1, sizeof(agtd_conf_t));
        if (NULL == conf)
        {
            log_error(log, "Alloc memory from pool failed!");
            break;
        }

        /* > 构建XML树 */
        memset(&opt, 0, sizeof(opt));

        opt.pool = mem_pool;
        opt.alloc = (mem_alloc_cb_t)mem_pool_alloc;
        opt.dealloc = (mem_dealloc_cb_t)mem_pool_dealloc;

        xml = xml_creat(path, &opt);
        if (NULL == xml)
        {
            log_error(log, "Create xml failed! path:%s", path);
            break;
        }

        /* > 加载通用配置 */
        if (agtd_conf_load_comm(xml, conf, log))
        {
            log_error(log, "Load common configuration failed!");
            break;
        }

        /* > 加载GATE配置 */
        if (agtd_conf_load_gate(xml, &conf->gate, log))
        {
            log_error(log, "Load GATE conf failed! path:%s", path);
            break;
        }

        /* > 加载SDTP配置 */
        if (agtd_conf_load_sdtp(xml, &conf->sdtp, log))
        {
            log_error(log, "Load sdtp conf failed! path:%s", path);
            break;
        }

        /* > 释放XML树 */
        xml_destroy(xml);
        return conf;
    } while(0);

    /* 异常处理 */
    if (NULL != xml)
    {
        xml_destroy(xml);
    }
    mem_pool_destroy(mem_pool);
    return NULL;
}

/* 解析日志级别配置 */
static int agtd_conf_parse_log(xml_tree_t *xml, agtd_conf_t *conf, log_cycle_t *log)
{
    xml_node_t *node, *fix;

    /* > 定位日志标签 */
    fix = xml_query(xml, ".AGENTD.LOG");
    if (NULL == fix)
    {
        conf->log_level = log_get_level(LOG_DEF_LEVEL_STR);
        return AGTD_OK;
    }

    /* > 日志级别 */
    node = xml_rquery(xml, fix, "LEVEL");
    if (NULL != node)
    {
        conf->log_level = log_get_level(node->value.str);
    }
    else
    {
        conf->log_level = log_get_level(LOG_DEF_LEVEL_STR);
    }

    return AGTD_OK;
}

/* 加载公共配置 */
static int agtd_conf_load_comm(xml_tree_t *xml, agtd_conf_t *conf, log_cycle_t *log)
{
    /* > 加载日志配置 */
    if (agtd_conf_parse_log(xml, conf, log))
    {
        log_error(log, "Parse log configuration failed!");
        return AGTD_ERR;
    }

    return AGTD_OK;
}

/* 解析并发配置 */
static int agtd_conf_parse_agent_connections(
        xml_tree_t *xml, agent_conf_t *conf, log_cycle_t *log)
{
    xml_node_t *node, *fix;

    /* > 定位并发配置 */
    fix = xml_query(xml, ".AGENTD.GATE.CONNECTIONS");
    if (NULL == fix)
    {
        log_error(log, "Didn't configure connections!");
        return AGTD_ERR;
    }

    /* > 获取最大并发数 */
    node = xml_rquery(xml, fix, "MAX");
    if (NULL == node)
    {
        log_error(log, "Get max number of connections failed!");
        return AGTD_ERR;
    }

    conf->connections.max = atoi(node->value.str);

    /* > 获取连接超时时间 */
    node = xml_rquery(xml, fix, "TIMEOUT");
    if (NULL == node)
    {
        log_error(log, "Get timeout of connection failed!");
        return AGTD_ERR;
    }

    conf->connections.timeout = atoi(node->value.str);

    /* > 获取侦听端口 */
    node = xml_rquery(xml, fix, "PORT");
    if (NULL == node)
    {
        log_error(log, "Get port of connection failed!");
        return AGTD_ERR;
    }

    conf->connections.port = atoi(node->value.str);

    return AGTD_OK;
}

/* 解析队列配置 */
static int agtd_conf_parse_agent_queue(xml_tree_t *xml, agent_conf_t *conf, log_cycle_t *log)
{
    xml_node_t *node, *fix;

    /* 加载队列信息 */
#define AGTD_LOAD_QUEUE(xml, fix,  _path, conf) \
    {\
        char node_path[FILE_PATH_MAX_LEN]; \
        \
        snprintf(node_path, sizeof(node_path), "%s.MAX", _path); \
        \
        node = xml_rquery(xml, fix, node_path); \
        if (NULL == node) \
        { \
            return AGTD_ERR; \
        } \
        \
        (conf)->max = atoi(node->value.str); \
        \
        snprintf(node_path, sizeof(node_path), "%s.SIZE", _path); \
        \
        node = xml_rquery(xml, fix, node_path); \
        if (NULL == node) \
        { \
            return AGTD_ERR; \
        } \
        \
        (conf)->size = atoi(node->value.str); \
    }

    /* > 定位队列标签 */
    fix = xml_query(xml, ".AGENTD.GATE.QUEUE");
    if (NULL == fix)
    {
        log_error(log, "Get queue configuration failed!");
        return AGTD_ERR;
    }

    /* > 获取队列配置 */
    AGTD_LOAD_QUEUE(xml, fix, ".CONNQ", &conf->connq);
    AGTD_LOAD_QUEUE(xml, fix, ".TASKQ", &conf->taskq);

    return AGTD_OK;
}

/* 加载GATE配置 */
static int agtd_conf_load_gate(xml_tree_t *xml, agent_conf_t *conf, log_cycle_t *log)
{
    xml_node_t *node;

    /* > 加载连接配置 */
    if (agtd_conf_parse_agent_connections(xml, conf, log))
    {
        log_error(log, "Parse connections of GATEe configuration failed!");
        return AGTD_ERR;
    }

    /* > 加载连接配置 */
    if (agtd_conf_parse_agent_queue(xml, conf, log))
    {
        log_error(log, "Parse queue of GATEe configuration failed!");
        return AGTD_ERR;
    }

    /* > 获取WORKER.NUM标签 */
    node = xml_query(xml, ".AGENTD.GATE.WORKER.NUM");
    if (NULL == node)
    {
        log_error(log, "Didn't configure number of worker!");
        return AGTD_ERR;
    }

    conf->worker_num = atoi(node->value.str);

    /* 4. 获取AGENT.NUM标签 */
    node = xml_query(xml, ".AGENTD.GATE.AGENT.NUM");
    if (NULL == node)
    {
        log_error(log, "Didn't configure number of agent!");
        return AGTD_ERR;
    }

    conf->agent_num = atoi(node->value.str);

    return AGTD_OK;
}

/* 加载SDTP配置 */
static int agtd_conf_load_sdtp(xml_tree_t *xml, dsnd_conf_t *conf, log_cycle_t *log)
{
    memset(conf, 0, sizeof(dsnd_conf_t));

    snprintf(conf->name, sizeof(conf->name), "SDTP-SEND");

    conf->auth.devid = 1;
    snprintf(conf->auth.usr, sizeof(conf->auth.usr), "qifeng");
    snprintf(conf->auth.passwd, sizeof(conf->auth.passwd), "111111");

    snprintf(conf->ipaddr, sizeof(conf->ipaddr), "127.0.0.1");

    conf->send_thd_num = 1;
    conf->send_buff_size = 5 * MB;
    conf->recv_buff_size = 2 * MB;

    snprintf(conf->sendq.name, sizeof(conf->sendq.name), "../temp/sdtp/sdtp-ssvr.key");
    conf->sendq.size = 4096;
    conf->sendq.count = 2048;

    return AGTD_OK;
}
