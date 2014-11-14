/******************************************************************************
 ** Coypright(C) 2014-2024 Xundao technology Co., Ltd
 **
 ** 文件名: filter.c
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

int main(int argc, char *argv[])
{
    crwl_opt_t opt;
    log_cycle_t *log;
    crwl_conf_t *conf;
    crwl_filter_t *filter;

    memset(&opt, 0, sizeof(opt));

    /* 1. 解析输入参数 */
    if (crwl_getopt(argc, argv, &opt))
    {
        return crwl_usage(argv[0]);
    }

    if (opt.isdaemon)
    {
        daemon(1, 0);
    }

    /* 2. 初始化日志模块 */
    log = crwl_init_log(argv[0]);
    if (NULL == log)
    {
        return CRWL_ERR;
    }

    /* 3. 加载配置信息 */
    conf = crwl_conf_load(opt.conf_path, log);
    if (NULL == conf)
    {
        log_error(log, "Initialize log failed!");

        log2_destroy();
        log_destroy(&log);
        return CRWL_ERR;
    }

    /* 4. 初始化Filter对象 */
    filter = crwl_filter_init(conf, log);
    if (NULL == filter)
    {
        log_error(log, "Init filter failed!");

        crwl_conf_destroy(conf);
        log2_destroy();
        log_destroy(&log);
        return CRWL_ERR;
    }

    /* 5. 处理网页信息 */
    crwl_filter_work(filter);

    /* 6. 释放GUMBO对象 */
    crwl_filter_destroy(filter);

    return CRWL_OK;
}