#include "uri.h"
#include "comm.h"
#include "http.h"

/******************************************************************************
 **函数名称: http_get_request
 **功    能: 构造HTTP GET请求
 **输入参数:
 **     uri: URI
 **     size: req长度
 **输出参数:
 **     req: HTTP GET请求字串
 **返    回: 0:成功 !0:失败
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2014.09.23 #
 ******************************************************************************/
int http_get_request(const char *uri, char *req, int size)
{
    uri_field_t field;

    /* 1. 获取GET相关字段 */
    if (0 != uri_reslove(uri, &field)) {
        return HTTP_ERR;
    }

    /* 2. 构建HTTP GET请求 */
    snprintf(req, size,
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Accept: */*\r\n"
        "Accept-Language: zh-cn\r\n"
        "Accept-Charset: utf-8\r\n"
        "Connection: close\r\n" /* keep-alive */
        "User-Agent: XunDao Crawler\r\n"
        "\r\n", field.path, field.host);

    return HTTP_OK;
}

/******************************************************************************
 **函数名称: http_parse_response
 **功    能: 解析HTTP应答
 **输入参数:
 **     str: 应答字串
 **输出参数:
 **     resp: 应答信息
 **返    回: 应答头长度
 **实现描述:
 **注意事项:
 **作    者: # Qifeng.zou # 2015.03.03 #
 ******************************************************************************/
int http_parse_response(const char *str, http_response_t *rep)
{
    const char *p, *end, *tmp;

    end = strstr(str, "\r\n\r\n");
    if (NULL == end) {
        return 0; /* 不完整 */
    }

    rep->header_len = (end - str) + 4; /* 应答长度 */
    rep->content_len = HTTP_CONTENT_MAX_LEN;

    p = str;
    while (' ' == *p) { ++p; }

    /* > 获取版本, 状态 */
    if (!strncasecmp(p, HTTP_KEY_VERS_09, HTTP_KEY_VERS_LEN)) {
        rep->version = HTTP_VERSION_09;
    }
    else if (!strncasecmp(p, HTTP_KEY_VERS_10, HTTP_KEY_VERS_LEN)) {
        rep->version = HTTP_VERSION_10;
    }
    else if (!strncasecmp(p, HTTP_KEY_VERS_11, HTTP_KEY_VERS_LEN)) {
        rep->version = HTTP_VERSION_11;
    }
    else if (!strncasecmp(p, HTTP_KEY_VERS_20, HTTP_KEY_VERS_LEN)) {
        rep->version = HTTP_VERSION_20;
    }
    else {
        rep->status = -1;
        rep->total_len = rep->header_len + rep->content_len;
        return -1;
    }

    p += HTTP_KEY_VERS_LEN;
    tmp = p;
    while (isdigit(*tmp)) {
        rep->status *= 10;
        rep->status += (*tmp - '0');
        ++tmp;
    }

    if (rep->status < 100 || rep->status > 999) {
        rep->total_len = rep->header_len + rep->content_len;
        return rep->header_len;
    }
    else if (200 != rep->status) {
        rep->total_len = rep->header_len + rep->content_len;
        return rep->header_len;
    }

    p = strstr(p, "\r\n");
    if (p == end) {
        rep->total_len = rep->header_len + rep->content_len;
        return -1;
    }

    p += 2;

    /* > 获取其他信息 */
    while (p != end) {
        while (' ' == *p) { ++p; }
        if (p == end) {
            break;
        }

        /* 连接方式 */
        if (!strncasecmp(p, HTTP_KEY_CONNECTION, HTTP_KEY_CONNECTION_LEN)) {
            p += HTTP_KEY_CONNECTION_LEN;
            while (' ' == *p) { ++p; }
            if (!strncasecmp(p, HTTP_KEY_CONNECTION_CLOSE, HTTP_KEY_CONNECTION_CLOSE_LEN)) {
                rep->connection = HTTP_CONNECTION_CLOSE;
            }
            else if (!strncasecmp(p, HTTP_KEY_CONNECTION_KEEPALIVE, HTTP_KEY_CONNECTION_KEEPALIVE_LEN)) {
                rep->connection = HTTP_CONNECTION_KEEPALIVE;
            }
            else {
                rep->total_len = rep->header_len + rep->content_len;
                return -1;
            }
        }
        /* 内容长度 */
        else if (!strncasecmp(p, HTTP_KEY_CONTENT_LEN, HTTP_KEY_CONTENT_LEN_LEN)) {
            p += HTTP_KEY_CONTENT_LEN_LEN;
            while (' ' == *p) { ++p; }

            rep->content_len = 0;
            while (isdigit(*p)) {
                rep->content_len *= 10;
                rep->content_len += (*p - '0');
                ++p;
            }
        }

        p = strstr(p, "\r\n");
        if (p == end) {
            break;
        }
        p += 2;
    }

    rep->total_len = rep->header_len + rep->content_len;

    return rep->header_len;
}
