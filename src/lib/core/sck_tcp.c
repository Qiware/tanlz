#include "sck.h"
#include "redo.h"

/******************************************************************************
 **函数名称: tcp_listen
 **功    能: 侦听指定端口
 **输入参数: 
 **     port: 端口号
 **输出参数: NONE
 **返    回: 套接字ID
 **实现描述: 
 **     1. 创建套接字
 **     2. 绑定指定端口
 **     3. 侦听指定端口
 **     4. 设置套接字属性(可重用、非阻塞)
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.03.24 #
 ******************************************************************************/
int tcp_listen(int port)
{
    int fd, opt = 1;
    struct sockaddr_in svraddr;

    /* 1. 创建套接字 */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));

    /* 2. 绑定指定端口 */
    bzero(&svraddr, sizeof(svraddr));

    svraddr.sin_family = AF_INET;
    svraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    svraddr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&svraddr, sizeof(svraddr)) < 0) {
        close(fd);
        return -1;
    }

    /* 3. 侦听指定端口 */
    if (listen(fd, TCP_LISTEN_BACKLOG) < 0) {
        close(fd);
        return -1;
    }

    /* 4. 设置非阻塞属性 */
    fd_set_nonblocking(fd);

    return fd;
}

/******************************************************************************
 **函数名称: tcp_accept
 **功    能: 接收连接请求
 **输入参数:
 **     lsnfd: 侦听套接字
 **输出参数:
 **     cliaddr: 客户端信息
 **返    回: 套接字ID
 **实现描述: 
 **     1. 接收连接请求
 **     2. 设置套接字属性
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.04.07 #
 ******************************************************************************/
int tcp_accept(int lsnfd, struct sockaddr *cliaddr)
{
    int fd, opt;
    socklen_t len = sizeof(cliaddr);

    /* > 接收连接请求 */
    fd = accept(lsnfd, (struct sockaddr *)cliaddr, &len);
    if (fd < 0) {
        return -1;
    }

    /* > 设置套接字属性 */
    opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)); /* 解决: 端口被占用的问题 */

    fd_set_nonblocking(fd);

    return fd;
}

/******************************************************************************
 **函数名称: tcp_connect
 **功    能: 连接指定服务器
 **输入参数: 
 **     family: 协议(AF_INET/AF_INET6)
 **     ipaddr: IP地址
 **     port: 端口号
 **输出参数: NONE
 **返    回: 套接字ID
 **实现描述: 
 **     1. 创建套接字
 **     2. 连接指定服务器
 **     3. 设置套接字属性(非阻塞)
 **注意事项: 
 **     SO_REUSEADDR可以用在以下四种情况下:
 **         (摘自《Unix网络编程》卷一, 即UNPv1)
 **         1) 当有一个有相同本地地址和端口的socket1处于TIME_WAIT状态时, 而你启
 **     动的程序的socket2要占用该地址和端口, 你的程序就要用到该选项.
 **         2) SO_REUSEADDR允许同一port上启动同一服务器的多个实例(多个进程). 但
 **     每个实例绑定的IP地址是不能相同的. 在有多块网卡或用IP Alias技术的机器可
 **     以测试这种情况.
 **         3) SO_REUSEADDR允许单个进程绑定相同的端口到多个socket上，但每个soc
 **     ket绑定的ip地址不同。这和2很相似，区别请看UNPv1。
 **         4) SO_REUSEADDR允许完全相同的地址和端口的重复绑定。但这只用于UDP的
 **     多播，不用于TCP。 
 **作    者: # Qifeng.zou # 2014.09.24 #
 ******************************************************************************/
int tcp_connect(int family, const char *ipaddr, int port)
{
    int fd, opt = 1;
    struct sockaddr_in svraddr;

    /* 1. 创建套接字 */
    fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));

    /* 2. 连接远程服务器 */
    bzero(&svraddr, sizeof(svraddr));

    svraddr.sin_family = AF_INET;
    inet_pton(AF_INET, ipaddr, &svraddr.sin_addr);
    svraddr.sin_port = htons(port);

    if (0 != connect(fd, (struct sockaddr *)&svraddr, sizeof(svraddr))) {
        close(fd);
        return -1;
    }

    fd_set_nonblocking(fd);

    return fd;
}

/******************************************************************************
 **函数名称: tcp_connect_timeout
 **功    能: 连接指定服务器(超时返回)
 **输入参数: 
 **     family: 协议(AF_INET/AF_INET6)
 **     ipaddr: IP地址
 **     port: 端口号
 **     sec: 超时时间
 **输出参数: NONE
 **返    回: 套接字ID
 **实现描述: 
 **     1. 创建套接字
 **     2. 连接指定服务器
 **     3. 设置套接字属性(非阻塞)
 **注意事项: 
 **     1) SO_REUSEADDR: 
 **作    者: # Qifeng.zou & Menglai.Wang # 2014.09.25 #
 ******************************************************************************/
int tcp_connect_timeout(int family, const char *ipaddr, int port, int sec)
{
    int ret, fd, opt = 1;
    fd_set rdset, wrset;
    struct timeval tv;
    struct sockaddr_in svraddr;

    /* 1. 创建套接字 */
    fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));

    fd_set_nonblocking(fd);

    /* 2. 连接远程服务器 */
    bzero(&svraddr, sizeof(svraddr));

    svraddr.sin_family = AF_INET;
    inet_pton(AF_INET, ipaddr, &svraddr.sin_addr);
    svraddr.sin_port = htons(port);

    if (!connect(fd, (struct sockaddr *)&svraddr, sizeof(svraddr))) {
        return fd;
    }

    if (EINPROGRESS != errno) {
        close(fd);
        return -1;
    }

    /* 3. 判断是否超时 */
AGAIN:
    FD_ZERO(&rdset);
    FD_ZERO(&wrset);

    FD_SET(fd, &rdset);
    FD_SET(fd, &wrset);

    tv.tv_sec = sec;
    tv.tv_usec = 0;
    ret = select(fd+1, &rdset, &wrset, NULL, &tv);
    if (ret < 0) {
        if (EINTR == errno) { goto AGAIN; }
        close(fd);
        return -1;
    }
    else if (0 == ret) {
        close(fd);
        return -1;
    }
    
    return fd;
}

/******************************************************************************
 **函数名称: tcp_connect_async
 **功    能: 连接指定服务器
 **输入参数: 
 **     family: 协议(AF_INET/AF_INET6)
 **     ipaddr: IP地址
 **     port: 端口号
 **输出参数: NONE
 **返    回: 套接字ID
 **实现描述: 
 **     1. 创建套接字
 **     2. 设置为非阻塞套接字
 **     3. 连接指定服务器
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.10.09 #
 ******************************************************************************/
int tcp_connect_async(int family, const char *ipaddr, int port)
{
    int fd, opt = 1;
    struct sockaddr_in svraddr;

    /* 1. 创建套接字 */
    fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));

    fd_set_nonblocking(fd);

    /* 2. 连接远程服务器 */
    bzero(&svraddr, sizeof(svraddr));

    svraddr.sin_family = AF_INET;
    inet_pton(AF_INET, ipaddr, &svraddr.sin_addr);
    svraddr.sin_port = htons(port);

    if (!connect(fd, (struct sockaddr *)&svraddr, sizeof(svraddr))) {
        return fd;
    }

    if (EINPROGRESS != errno) {
        close(fd);
        return -1;
    }

    return fd;
}

/******************************************************************************
 **函数名称: fd_is_writable
 **功    能: 判断文件描述符是否可写
 **输入参数: 
 **     fd: 文件描述符
 **输出参数: NONE
 **返    回: 0:否 !0:是
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2014.06.07 #
 ******************************************************************************/
int fd_is_writable(int fd)
{
    fd_set wset;
    struct timeval tv;

    FD_ZERO(&wset);
    FD_SET(fd, &wset);

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    return select(fd+1, NULL, &wset, NULL, &tv);
}

/******************************************************************************
 **函数名称: tcp_block_send
 **功    能: 阻塞发送
 **输入参数: 
 **    fd: 文件描述符
 **    addr: 被发送数据的起始地址
 **    len: 发送字节数
 **输出参数: NONE
 **返    回: 0:Succ !0:Fail
 **实现描述: 
 **注意事项: 等待所有字节发送完成后才返回，除非出现严重错误.
 **作    者: # Qifeng.zou # 2014.06.24 #
 ******************************************************************************/
int tcp_block_send(int fd, const void *addr, int len, int timeout)
{
    int ret, n, left = len, off = 0;
    fd_set wrset;
    struct timeval tv;

    for (;;) {
        FD_ZERO(&wrset);
        FD_SET(fd, &wrset);

        tv.tv_sec = timeout;
        tv.tv_usec = 0;
        ret = select(fd+1, NULL, &wrset, NULL, &tv);
        if (ret < 0) {
            if (EINTR == errno) { continue; }
            return -1;
        }
        else if (0 == ret) {
            return -1;
        }

        n = Writen(fd, (const char *)addr + off, left);
        if (n < 0) {
            return -1;
        }
        else if (left != n) {
            left -= n;
            off += n;
            continue;
        }

        return 0; /* Done */
    }
}

/******************************************************************************
 **函数名称: tcp_block_recv
 **功    能: 阻塞接收
 **输入参数: 
 **     fd: 文件描述符
 **     addr: 被发送数据的起始地址
 **     len: 发送字节数
 **     timeout: 阻塞时长(秒)
 **输出参数: NONE
 **返    回: 0:Succ !0:Fail
 **实现描述: 
 **注意事项: 等待所有字节接收完成后才返回，除非出现严重错误或超时.
 **作    者: # Qifeng.zou # 2014.06.24 #
 ******************************************************************************/
int tcp_block_recv(int fd, void *addr, int len, int timeout)
{
    int ret, n, left = len, off = 0;
    fd_set rdset;
    struct timeval tv;

    for (;;) {
        FD_ZERO(&rdset);
        FD_SET(fd, &rdset);
        
        tv.tv_sec = timeout;
        tv.tv_usec = 0;
        ret = select(fd+1, &rdset, NULL, NULL, &tv);
        if (ret < 0) {
            if (EINTR == errno) { continue; }
            return -1;
        }
        else if (0 == ret) {
            return -1;
        }

        n = Readn(fd, (char *)addr + off, left);
        if (n < 0) {
            return -1;
        }
        else if (left != n) {
            left -= n;
            off += n;
            continue;
        }

        return 0; /* Done */
    }
}

/******************************************************************************
 **函数名称: ip_isvalid
 **功    能: 判断IP地址的合法性(是否符合点分十进制的格式)
 **输入参数: 
 **     ip: IP地址
 **输出参数: NONE
 **返    回: true:合法 false:非法
 **实现描述: 
 **注意事项: 
 **作    者: # Qifeng.zou # 2015.03.02 #
 ******************************************************************************/
bool ip_isvalid(const char *ip)
{
    int dots = 0, digits;
    const char *p = ip;

    while (' ' == *p) { ++p; }

    digits = 0;
    while ('\0' != *p) {
        while(isdigit(*p)) {
            ++p;
            ++digits;
        }

        if ((digits < 1) || (digits > 3)) {
            return false;
        }

        if ('.' == *p) {
            ++dots;
            if (dots > 3) {
                return false;
            }
            ++p;
            digits = 0;
            continue;
        }
        else if ('\0' == *p) {
            if (3 != dots) {
                return false;
            }
            return true;
        }
        else if (' ' == *p) {
            while (' ' == *p) { ++p; };
            if ('\0' != *p) {
                return false;
            }
            return true;
        }

        return false;
    }

    if ((3 != dots)
        || (0 == digits)
        || (digits > 3))
    {
        return false;
    }

    return true;
}
