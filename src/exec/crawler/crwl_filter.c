/******************************************************************************
 ** Coypright(C) 2014-2024 Xundao technology Co., Ltd
 **
 ** 文件名: crwl_filter.c
 ** 版本号: 1.0
 ** 描  述: 超链接的提取程序
 **         从爬取的网页中提取超链接
 ** 作  者: # Qifeng.zou # 2014.10.17 #
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "log.h"
#include "list.h"
#include "http.h"
#include "redis.h"
#include "xd_str.h"
#include "common.h"
#include "syscall.h"
#include "crawler.h"
#include "xml_tree.h"
#include "crwl_conf.h"
#include "crwl_filter.h"

#define CRWL_PARSER_LOG_NAME    "filter"

static int crwl_filter_webpage_info(crwl_webpage_info_t *info);
static int crwl_filter_work_flow(crwl_filter_t *filter);
static int crwl_filter_deep_hdl(crwl_filter_t *filter, gumbo_result_t *result);

bool crwl_set_uri_exists(redis_cluster_t *cluster, const char *hash, const char *uri);

/* 判断uri是否已下载 */
#define crwl_is_uri_down(cluster, hash, uri) crwl_set_uri_exists(cluster, hash, uri)

/* 判断uri是否已推送 */
#define crwl_is_uri_push(cluster, hash, uri) crwl_set_uri_exists(cluster, hash, uri)

/******************************************************************************
 **函数名称: crwl_filter_init
 **功    能: 初始化Filter对象
 **输入参数: 
 **     conf: 配置信息
 **     log: 日志信息
 **输出参数:
 **返    回: Filter对象
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.18 #
 ******************************************************************************/
crwl_filter_t *crwl_filter_init(crwl_conf_t *conf, log_cycle_t *log)
{
    crwl_filter_t *filter;

    /* 1. 申请对象空间 */
    filter = (crwl_filter_t *)calloc(1, sizeof(crwl_filter_t));
    if (NULL == filter)
    {
        log_error(log, "errmsg:[%d] %s!", errno, strerror(errno));
        return NULL;
    }

    filter->log = log;
    filter->conf = conf;

    log_set_level(log, conf->log.level);
    log2_set_level(conf->log.level2);

    /* 2. 连接Redis集群 */
    filter->redis = redis_cluster_init(&conf->redis.master, &conf->redis.slave_list);
    if (NULL == filter->redis)
    {
        log_error(filter->log, "Initialize redis context failed!");
        free(filter);
        return NULL;
    }

    return filter;
}

/******************************************************************************
 **函数名称: crwl_filter_destroy
 **功    能: 销毁Filter对象
 **输入参数: 
 **输出参数:
 **返    回: VOID
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.04 #
 ******************************************************************************/
void crwl_filter_destroy(crwl_filter_t *filter)
{
    if (filter->log)
    {
        log_destroy(&filter->log);
        filter->log = NULL;
    }
    log2_destroy();
    if (filter->redis)
    {
        redis_cluster_destroy(filter->redis);
        filter->redis = NULL;
    }
    if (filter->conf)
    {
        crwl_conf_destroy(filter->conf);
        filter->conf = NULL;
    }
    free(filter);
}

/******************************************************************************
 **函数名称: crwl_filter_work
 **功    能: 网页解析处理
 **输入参数: 
 **     p: 解析器对象
 **输出参数:
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.18 #
 ******************************************************************************/
int crwl_filter_work(crwl_filter_t *filter)
{
    DIR *dir;
    struct stat st;
    struct dirent *item;
    char path[FILE_PATH_MAX_LEN],
         new_path[FILE_PATH_MAX_LEN],
         html_path[FILE_PATH_MAX_LEN];
    crwl_conf_t *conf = filter->conf;

    while (1)
    {
        snprintf(path, sizeof(path), "%s/wpi", conf->download.path);

        /* 1. 打开目录 */
        dir = opendir(path);
        if (NULL == dir)
        {
            Mkdir(path, 0777);
            continue;
        }

        /* 2. 遍历文件 */
        while (NULL != (item = readdir(dir)))
        {
            snprintf(filter->info.fname,
                    sizeof(filter->info.fname), "%s/%s", path, item->d_name); 

            /* 判断文件类型 */
            stat(filter->info.fname, &st);
            if (!S_ISREG(st.st_mode))
            {
                continue;
            }

            /* 获取网页信息 */
            if (crwl_filter_webpage_info(&filter->info))
            {
                snprintf(new_path, sizeof(new_path),
                        "%s/%s", conf->filter.store.err_path, item->d_name);
                rename(filter->info.fname, new_path);

                snprintf(html_path, sizeof(html_path),
                        "%s/%s", conf->download.path, filter->info.html);
                remove(html_path);

                log_error(filter->log, "Get webpage information failed! fname:%s",
                        filter->info.fname);
                continue;
            }

            /* 主处理流程 */
            crwl_filter_work_flow(filter);

            snprintf(new_path, sizeof(new_path),
                    "%s/%s", conf->filter.store.path, item->d_name);

            rename(filter->info.fname, new_path);


            snprintf(html_path, sizeof(html_path),
                    "%s/%s", conf->download.path, filter->info.html);
            remove(html_path);
        }

        /* 3. 关闭目录 */
        closedir(dir);

        Mkdir(conf->filter.store.path, 0777);

        Sleep(5);
    }

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_filter_webpage_info
 **功    能: 获取网页信息
 **输入参数:
 **     info: 网页信息
 **输出参数:
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.18 #
 ******************************************************************************/
static int crwl_filter_webpage_info(crwl_webpage_info_t *info)
{
    xml_tree_t *xml;
    xml_node_t *node, *fix;

    /* 1. 新建XML树 */
    xml = xml_creat(info->fname);
    if (NULL == xml)
    {
        return CRWL_ERR;
    }

    /* 2. 提取网页信息 */
    do
    {
        fix = xml_query(xml, ".WPI");
        if (NULL == fix)
        {
            break;
        }

        /* 获取URI字段 */
        node = xml_rquery(xml, fix, "URI");
        if (NULL == fix)
        {
            break;
        }

        snprintf(info->uri, sizeof(info->uri), "%s", node->value);

        /* 获取DEPTH字段 */
        node = xml_rquery(xml, fix, "URI.DEPTH");
        if (NULL == fix)
        {
            break;
        }

        info->depth = atoi(node->value);

        /* 获取IP字段 */
        node = xml_rquery(xml, fix, "URI.IP");
        if (NULL == fix)
        {
            break;
        }

        snprintf(info->ip, sizeof(info->ip), "%s", node->value);

        /* 获取PORT字段 */
        node = xml_rquery(xml, fix, "URI.PORT");
        if (NULL == fix)
        {
            break;
        }

        info->port = atoi(node->value);

        /* 获取HTML字段 */
        node = xml_rquery(xml, fix, "HTML");
        if (NULL == fix)
        {
            break;
        }

        snprintf(info->html, sizeof(info->html), "%s", node->value);

        xml_destroy(xml);
        return CRWL_OK;
    } while(0);

    /* 3. 释放XML树 */
    xml_destroy(xml);
    return CRWL_ERR;
}

/******************************************************************************
 **函数名称: crwl_filter_work_flow
 **功    能: 解析器处理流程
 **输入参数: 
 **     p: 解析器对象
 **输出参数:
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.17 #
 ******************************************************************************/
static int crwl_filter_work_flow(crwl_filter_t *filter)
{
    gumbo_html_t *html;             /* HTML对象 */
    gumbo_result_t *result;         /* 结果集合 */
    char fpath[FILE_PATH_MAX_LEN];  /* HTML文件名 */
    crwl_conf_t *conf = filter->conf;
    crwl_webpage_info_t *info = &filter->info;

    /* 1. 判断网页深度 */
    if (info->depth > conf->download.depth)
    {
        log_info(filter->log, "Drop handle webpage! uri:%s depth:%d",
                info->uri, info->depth);
        return CRWL_OK;
    }

    /* 判断网页(URI)是否已下载
     *  判断的同时设置网页的下载标志
     *  如果已下载，则不做提取该网页中的超链接
     * */
    if (crwl_is_uri_down(filter->redis, conf->redis.done_tab, info->uri))
    {
        log_info(filter->log, "Uri [%s] was downloaded!", info->uri);
        return CRWL_OK;
    }

    snprintf(fpath, sizeof(fpath), "%s/%s", conf->download.path, info->html);

    /* 2. 解析HTML文件 */
    html = gumbo_html_parse(fpath);
    if (NULL == html)
    {
        log_error(filter->log, "Parse html failed! fpath:%s", fpath);
        return CRWL_ERR;
    }

    /* 3. 提取超链接 */
    result = gumbo_parse_href(html);
    if (NULL == result)
    {
        log_error(filter->log, "Parse href failed! fpath:%s", fpath);

        gumbo_result_destroy(result);
        gumbo_html_destroy(html);
        return CRWL_ERR;
    }

    /* 4. 深入处理超链接
     *  1. 判断超链接深度
     *  2. 判断超链接是否已被爬取
     *  3. 将超链接插入任务队列
     * */
    if (crwl_filter_deep_hdl(filter, result))
    {
        log_error(filter->log, "Deep handler failed! fpath:%s", fpath);

        gumbo_result_destroy(result);
        gumbo_html_destroy(html);
        return CRWL_ERR;
    }

    /* 5. 内存释放 */
    gumbo_result_destroy(result);
    gumbo_html_destroy(html);
    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_filter_deep_hdl
 **功    能: 超链接的深入分析和处理
 **输入参数: 
 **     p: 解析器对象
 **     result: URI集合
 **输出参数:
 **返    回: 0:成功 !0:失败
 **实现描述: 
 **     1. 判断超链接深度
 **     2. 判断超链接是否已被爬取
 **     3. 将超链接插入任务队列
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.17 #
 ******************************************************************************/
static int crwl_filter_deep_hdl(crwl_filter_t *filter, gumbo_result_t *result)
{
    int len;
    redisReply *r; 
    uri_field_t field;
    char task_str[CRWL_TASK_STR_LEN];
    crwl_conf_t *conf = filter->conf;
    list_node_t *node = result->list.head;
    crwl_webpage_info_t *info = &filter->info;

    /* 遍历URL集合 */
    for (; NULL != node; node = node->next)
    {
        /* 1. 将href转至uri */
        if (0 != href_to_uri((const char *)node->data, info->uri, &field))
        {
            log_info(filter->log, "Uri [%s] is invalid!", (char *)node->data);
            continue;
        }

        /* 2. 判断URI是否已经被推送到队列中 */
        if (crwl_is_uri_push(filter->redis, conf->redis.push_tab, field.uri))
        {
            log_info(filter->log, "Uri [%s] was pushed!", (char *)node->data);
            continue;
        }

        /* 3. 组装任务格式 */
        len = crwl_get_task_str(task_str, sizeof(task_str), field.uri, info->depth+1);
        if (len >= sizeof(task_str))
        {
            log_info(filter->log, "Task string is too long! [%s]", task_str);
            continue;
        }

        /* 4. 插入Undo任务队列 */
        r = redis_rpush(filter->redis->master, filter->conf->redis.undo_taskq, task_str);
        if (REDIS_REPLY_NIL == r->type)
        {
            freeReplyObject(r);
            log_error(filter->log, "Push into undo task queue failed!");
            return CRWL_ERR;
        }

        freeReplyObject(r);
    }

    return CRWL_OK;
}

/******************************************************************************
 **函数名称: crwl_set_uri_exists
 **功    能: 设置uri是否已存在
 **输入参数: 
 **     cluster: Redis集群
 **     hash: 哈希表名
 **     uri: 判断对象-URI
 **输出参数:
 **返    回: true:已下载 false:未下载
 **实现描述: 
 **     1) 当URI已存在时, 返回true;
 **     2) 当URI不存在时, 返回false, 并设置uri的值为1.
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.11.04 #
 ******************************************************************************/
bool crwl_set_uri_exists(redis_cluster_t *cluster, const char *hash, const char *uri)
{
    redisReply *r;

    if (0 == cluster->slave_num)
    {
        return !redis_hsetnx(cluster->master, hash, uri, "1");
    }

    do
    {
        r = redisCommand(
                cluster->slave[random() % cluster->slave_num],
                "HEXISTS %s %s", hash, uri);
        if (REDIS_REPLY_INTEGER != r->type)
        {
            break;
        }

        if (0 == r->integer)
        {
            break;
        }

        freeReplyObject(r);
        return true; /* 已存在 */
    } while(0);

    freeReplyObject(r);

    return !redis_hsetnx(cluster->master, hash, uri, "1");
}