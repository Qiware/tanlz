#if !defined(__MONITOR_H__)
#define __MONITOR_H__

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "sck.h"
#include "menu.h"
#include "mon_conf.h"

/* 输入参数 */
typedef struct
{
    char conf_path[FILE_NAME_MAX_LEN];      /* 配置路径 */
} mon_opt_t;

/* 全局信息 */
typedef struct
{
    int fd;                                 /* 描述符 */
    mon_conf_t *conf;                       /* 配置信息 */
    menu_cntx_t *menu;                      /* 菜单对象 */

    struct sockaddr_in to;                  /* 目标地址 */
} mon_cntx_t;

menu_item_t *mon_agent_menu(menu_cntx_t *ctx, void *args);
menu_item_t *mon_crwl_menu(menu_cntx_t *ctx, void *args);
menu_item_t *mon_flt_menu(menu_cntx_t *ctx, void *args);

#endif /*__MONITOR_H__*/
