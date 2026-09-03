#ifndef __SONOFF_LOG_H__
#define __SONOFF_LOG_H__

#include "stdio.h"

#define LOG_PRINT_ENABLE      1

#if LOG_PRINT_ENABLE
#define snf_log_printf(format, ...) \
do { \
    printf(""format"", ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)
#else
#define snf_log_printf(format, ...)
#endif /* LOG_PRINT_ENABLE */

#define LOG_E(tag,format, ...) \
do { \
        snf_log_printf("%15s-%04d | %s, %s: " format "\r\n", \
                         tag, __LINE__, __FILE__,  __func__, ##__VA_ARGS__ ); \
} while (0)

#define LOG_W(tag,format, ...) \
do { \
        snf_log_printf("%15s-%04d | %s: " format "\r\n", tag, __LINE__, __func__, ##__VA_ARGS__ ); \
} while (0)

#define LOG_I(tag,format, ...) \
do { \
        snf_log_printf("%15s-%04d | "format"\r\n", tag, __LINE__, ##__VA_ARGS__ ); \
} while (0)

#define LOG_RAW(format, ...) \
do { \
        snf_log_printf(format"\r\n", ##__VA_ARGS__ ); \
} while (0)

#endif /* __SONOFF_LOG_H__ */


