#if !defined(__SCK_H__)
#define __SCK_H__

#include "comm.h"

#define TCP_LISTEN_BACKLOG  (20)

#define ntoh64(v) ((uint64_t)( \
        (uint64_t)(v) & 0x00000000000000FF) << 56 \
        | ((uint64_t)(v) & 0x000000000000FF00) << 40 \
        | ((uint64_t)(v) & 0x0000000000FF0000) << 24 \
        | ((uint64_t)(v) & 0x00000000FF000000) << 8 \
        | ((uint64_t)(v) & 0x000000FF00000000) >> 8 \
        | ((uint64_t)(v) & 0x0000FF0000000000) >> 24 \
        | ((uint64_t)(v) & 0x00FF000000000000) >> 40 \
        | ((uint64_t)(v) & 0xFF00000000000000) >> 56)

#define hton64(v)    ntoh64(v)

/* IP地址信息 */
typedef struct
{
    int family;                     /* TCP协议(AF_INET/AF_INET6) */
    char addr[IP_ADDR_MAX_LEN];       /* IP地址信息 */
} ipaddr_t;

/* 接收(发送)数据阶段 */
typedef enum
{
    SOCK_PHASE_RECV_INIT            /* 准备接收(如: 分配空间) */
    , SOCK_PHASE_RECV_HEAD          /* 接收报头数据 */
    , SOCK_PHASE_READY_BODY         /* 准备接收报体(如: 分配空间) */
    , SOCK_PHASE_RECV_BODY          /* 接收报体数据 */
    , SOCK_PHASE_RECV_POST          /* 接收完毕(如: 对数据进行处理) */

    , SOCK_PHASE_SEND_HEAD          /* 发送报头数据 */
    , SOCK_PHASE_SEND_BODY          /* 发送报体数据 */
    , SOCK_PHASE_SEND_POST          /* 发送结束 */

    , SOCK_PHASE_TOTAL
} socket_snap_phase_e;

/* 接收/发送快照 */
typedef struct
{
    int phase;                      /* 当前状态 Range: socket_snap_phase_e */
    int off;                        /* 偏移量 */
    int total;                      /* 总字节 */

    char *addr;                     /* 缓存首地址 */
} socket_snap_t;

typedef struct _socket_t socket_t;
typedef int (*socket_recv_cb_t)(void *ctx, void *obj, socket_t *sck);
typedef int (*socket_send_cb_t)(void *ctx, void *obj, socket_t *sck);

/* 套接字对象 */
typedef struct _socket_t
{
    int fd;                         /* 套接字FD */

    struct timeb crtm;              /* 创建时间 */
    time_t wrtm;                    /* 最近写入时间 */
    time_t rdtm;                    /* 最近读取时间 */

    socket_snap_t recv;             /* Recv快照 */
    socket_snap_t send;             /* Send快照 */

    socket_recv_cb_t recv_cb;       /* 接收回调 */
    socket_send_cb_t send_cb;       /* 发送回调 */

    void *extra;                    /* 附加数据(自定义数据) */
} socket_t;

int tcp_listen(int port);
int tcp_accept(int lsnfd, struct sockaddr *cliaddr);
int tcp_connect(int family, const char *ipaddr, int port);
int tcp_connect_timeout(int family, const char *ipaddr, int port, int sec);
int tcp_connect_async(int family, const char *ipaddr, int port);
int tcp_block_send(int fd, const void *addr, int len, int timeout);
int tcp_block_recv(int fd, void *addr, int len, int timeout);

int fd_is_writable(int fd);
#define fd_set_nonblocking(fd)     /* 设置fd为非阻塞模式 */\
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)

int unix_udp_creat(const char *path);
int unix_udp_send(int fd, const char *path, const void *buff, int len);
int unix_udp_recv(int fd, void *buff, int len);

int udp_listen(int port);

bool ip_isvalid(const char *ip);

#endif /*__SCK_H__*/
