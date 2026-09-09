/**
 * @file    sonoff_http_cgi.c
 * @brief   HTTP网页事件处理.
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <FreeRTOS.h>
#include <task.h>

#include "sonoff_http_cgi.h"
#include "sonoff_log.h"
#include "sonoff_plug_handle.h"

static const char *tag = "SNF-HTTP-CGI";

/** @brief HTTP网页默认时间. */
#define SNF_HTTP_DEFAULT_YEAR               (2026)      /* 年 */
#define SNF_HTTP_DEFAULT_MONTH              (8)         /* 月 */
#define SNF_HTTP_DEFAULT_DAY                (26)        /* 日 */
#define SNF_HTTP_DEFAULT_HOUR               (9)         /* 时 */
#define SNF_HTTP_DEFAULT_MINUTE             (0)         /* 分 */
#define SNF_HTTP_DEFAULT_SECOND             (0)         /* 秒 */

/**
 * @brief HTTP CGI运行状态.
 */
typedef struct
{
    const SnfHttpCgiRoute *routes; /* 已注册的CGI路由 */
    size_t route_count;            /* 已注册的CGI路由数量 */
    TickType_t time_start_tick;    /* 时间计时起始tick */
} SnfHttpCgiState;

/** @brief HTTP CGI运行状态实例. */
static SnfHttpCgiState cgi_state_data = {
    .routes = NULL,
    .route_count = 0U,
    .time_start_tick = 0,
};

static int snfHttpCgiIsLeapYear(int year)
{
    int ret = 0;

    if (((year % 4) == 0) && ((year % 100) != 0 || (year % 400) == 0))
    {
        ret = 1;
    }

    return ret;
}

static int snfHttpCgiGetDaysInMonth(int year, int month)
{
    static const int days_in_month[] = {
        0, 31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31,
    };
    int days;

    days = days_in_month[month];
    if ((month == 2) && (snfHttpCgiIsLeapYear(year) != 0))
    {
        days = 29;
    }

    return days;
}

int snfHttpCgiRegister(const SnfHttpCgiRoute *routes, size_t route_count)
{
    SnfHttpCgiState *cgi_state = &cgi_state_data;
    size_t index;
    int ret = 0;

    if ((routes == NULL) || (route_count == 0U))
    {
        ret = -1;
    }
    else
    {
        for (index = 0U; index < route_count; index++)
        {
            if ((routes[index].path == NULL) || (routes[index].callback == NULL))
            {
                ret = -2;
            }
        }

        if (ret == 0)
        {
            cgi_state->routes = routes;
            cgi_state->route_count = route_count;
            cgi_state->time_start_tick = xTaskGetTickCount();
        }
    }

    return ret;
}

int snfHttpCgiHandleRequest(const char *request,
                            char *response,
                            size_t response_size)
{
    SnfHttpCgiState *cgi_state = &cgi_state_data;
    const char *path_start;
    const char *path_end;
    const char *query_start;
    const char *route_end;
    const SnfHttpCgiRoute *route;
    size_t path_length;
    size_t query_length = 0U;
    size_t index;
    int handled = 0;
    int ret = -1;

    if ((request != NULL) && (response != NULL) && (response_size > 0U)
        && (cgi_state->routes != NULL))
    {
        if (strncmp(request, "GET ", 4U) == 0)
        {
            path_start = request + 4U;
            path_end = strchr(path_start, ' ');
            if (path_end != NULL)
            {
                query_start = memchr(path_start,
                                     '?',
                                     (size_t)(path_end - path_start));
                route_end = path_end;
                if (query_start != NULL)
                {
                    route_end = query_start;
                    query_length = (size_t)(path_end - query_start) - 1U;
                }

                path_length = (size_t)(route_end - path_start);
                for (index = 0U; index < cgi_state->route_count; index++)
                {
                    route = &cgi_state->routes[index];
                    if ((handled == 0)
                        && (strlen(route->path) == path_length)
                        && (strncmp(path_start, route->path, path_length) == 0))
                    {
                        ret = route->callback(query_start != NULL ? query_start + 1U : NULL,
                                              query_length,
                                              response,
                                              response_size);
                        handled = 1;
                    }
                }
            }
        }
    }

    return ret;
}

int snfHttpCgiToggle(const char *query,
                     size_t query_length,
                     char *response,
                     size_t response_size)
{
    uint8_t next_onoff;
    int onoff;
    int ret;

    (void)query;
    (void)query_length;
    (void)response;
    (void)response_size;

    onoff = snfPlugOnOffGet();
    next_onoff = (onoff == 0) ? 1 : 0;
    ret = snfPlugOnOffSet(next_onoff);
    if (ret != 0)
    {
        LOG_E(tag, "toggle onoff failed");
        return ret;
    }

    LOG_I(tag, "toggle onoff to %d", next_onoff);

    return 0;
}

int snfHttpCgiGetToggleStatus(const char *query,
                              size_t query_length,
                              char *response,
                              size_t response_size)
{
    int onoff;
    int response_length;

    (void)query;
    (void)query_length;

    if ((response == NULL) || (response_size == 0U))
    {
        return -1;
    }

    onoff = snfPlugOnOffGet();
    response_length = snprintf(response,
                               response_size,
                               "%s",
                               onoff != 0 ? "HIGH" : "LOW");
    if ((response_length < 0) || ((size_t)response_length >= response_size))
    {
        return -2;
    }

    return response_length;
}

int snfHttpCgiGetTime(const char *query,
                      size_t query_length,
                      char *response,
                      size_t response_size)
{
    SnfHttpCgiState *cgi_state = &cgi_state_data;
    TickType_t elapsed_ticks;
    uint32_t elapsed_seconds;
    uint32_t total_seconds;
    uint32_t elapsed_days;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int response_length;

    (void)query;
    (void)query_length;

    if ((response == NULL) || (response_size == 0U)
        || (cgi_state->routes == NULL))
    {
        return -1;
    }

    elapsed_ticks = xTaskGetTickCount() - cgi_state->time_start_tick;
    elapsed_seconds = (uint32_t)(elapsed_ticks / configTICK_RATE_HZ);
    total_seconds = (SNF_HTTP_DEFAULT_HOUR * 3600U)
                    + (SNF_HTTP_DEFAULT_MINUTE * 60U)
                    + SNF_HTTP_DEFAULT_SECOND
                    + elapsed_seconds;
    elapsed_days = total_seconds / (24U * 3600U);
    year = SNF_HTTP_DEFAULT_YEAR;
    month = SNF_HTTP_DEFAULT_MONTH;
    day = SNF_HTTP_DEFAULT_DAY;
    hour = (int)((total_seconds / 3600U) % 24U);
    minute = (int)((total_seconds / 60U) % 60U);
    second = (int)(total_seconds % 60U);

    while (elapsed_days > 0U)
    {
        day++;
        elapsed_days--;
        if (day > snfHttpCgiGetDaysInMonth(year, month))
        {
            day = 1;
            month++;
            if (month > 12)
            {
                month = 1;
                year++;
            }
        }
    }

    response_length = snprintf(response,
                               response_size,
                               "%04d.%02d.%02d-%02d:%02d:%02d",
                               year,
                               month,
                               day,
                               hour,
                               minute,
                               second);
    if ((response_length < 0) || ((size_t)response_length >= response_size))
    {
        return -3;
    }

    return response_length;
}
