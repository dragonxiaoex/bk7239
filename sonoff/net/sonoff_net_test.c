/**
 * @file    sonoff_net_test.c
 * @brief   网络吞吐压力测试模块
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-28
 *
 * @copyright Copyright (c) 2026 深圳松诺技术有限公司
 *
 */

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include <common/bk_err.h>
#include <modules/wifi.h>
#include <sdkconfig.h>

#include <lwip/def.h>
#include <lwip/inet.h>
#include <lwip/opt.h>
#include <lwip/sockets.h>
#include <lwip/stats.h>

#include <openthread/error.h>
#include <openthread/instance.h>
#include <openthread/ip6.h>
#include <openthread/message.h>
#include <openthread/thread.h>
#include <openthread/udp.h>

#if CONFIG_TASK_WDT
#include "bk_private/bk_wdt.h"
#endif

#include "sonoff_log.h"
#include "sonoff_net_test.h"
#include "sonoff_task_def.h"

#define SNF_NET_TEST_TASK_STACK_SIZE                6144U
#define SNF_NET_TEST_SAMPLE_INTERVAL_MS             1000U
#define SNF_NET_TEST_SOCKET_TIMEOUT_MS              3000U
#define SNF_NET_TEST_TCP_TIMEOUT_RETRY_MAX          10U
#define SNF_NET_TEST_RECEIVER_GRACE_MS              5000U
#define SNF_NET_TEST_FIN_RETRY_COUNT                3U
#define SNF_NET_TEST_FIN_RETRY_WAIT_MS              1000U
#define SNF_NET_TEST_FIN_RETRY_GAP_MS               100U
#define SNF_NET_TEST_SEND_BURST_MAX                 32U
#define SNF_NET_TEST_IPERF_SERVER_FLAG              0x80000000UL
#define SNF_NET_TEST_IPERF_FIN_FLAG                 0x80000000UL
#define SNF_NET_TEST_IPERF_SEQUENCE_MASK            0x7FFFFFFFUL
#define SNF_NET_TEST_IPERF_UDP_HEADER_SIZE          16U
#define SNF_NET_TEST_IPERF_SERVER_HEADER_SIZE       40U
#define SNF_NET_LOG_FRACTION_SCALE                  100U
#define SNF_NET_BITS_PER_KILOBIT                    1000U /**< 每千比特包含的比特数。 */
#define SNF_NET_BITS_PER_MEGABIT                    1000000U
#define SNF_NET_BYTES_PER_KILOBIT                   125U /**< 每千比特包含的字节数。 */
#define SNF_NET_BYTES_PER_MEGABIT                   125000U

static const char *tag = "SNF-NET-TEST";

#if SNR_NET_TEST_THREAD_TEST_ENABLE
extern otInstance *g_otInst;
#endif
/*
 * UDP线格式兼容iperf2：0~15字节为数据报头，负序号表示测试结束；
 * 16~55字节为标准服务端回报头。
 */
typedef struct
{
    int32_t id;
    uint32_t seconds;
    uint32_t microseconds;
    int32_t id_high;
} SnfNetTestIperfUdpHeader;

typedef struct
{
    int32_t flags;
    int32_t total_length_high;
    int32_t total_length_low;
    int32_t duration_seconds;
    int32_t duration_microseconds;
    int32_t error_count;
    int32_t out_of_order_count;
    int32_t datagram_count;
    int32_t jitter_seconds;
    int32_t jitter_microseconds;
} SnfNetTestIperfServerHeader;

typedef struct
{
    TickType_t start_tick;
    TickType_t sample_tick;
    TickType_t progress_tick;
    uint64_t total_bytes;
    uint64_t sample_bytes;
    uint32_t current_bitrate;
    uint32_t peak_bitrate;
} SnfNetTestStats;

typedef struct
{
    SnfNetTestStats stats;
    uint32_t target_bitrate;
    uint32_t received_packets;
    uint32_t sender_packets;
    uint32_t highest_sequence;
    uint32_t out_of_order_packets;
    uint32_t finish_id;
    uint32_t finish_id_high;
    bool started;
    bool finish_received;
} SnfNetTestUdpReceiver;

typedef struct
{
    uint64_t received_bytes;
    uint32_t sent_packets;
    uint32_t received_packets;
    uint32_t lost_packets;
    uint32_t out_of_order_packets;
    uint32_t average_bitrate;
    bool received;
} SnfNetTestUdpRemoteReport;

typedef struct
{
    SemaphoreHandle_t thread_mutex;
    TaskHandle_t thread_task_handle;
    TaskHandle_t wifi_tcp_task_handle;
    TaskHandle_t wifi_udp_task_handle;
    uint32_t wifi_ps_hold_count;
} SnfNetTestConfig;

typedef struct
{
    otUdpSocket socket;
    otMessageInfo peer_info;
    SnfNetTestUdpReceiver receiver;
    SnfNetTestUdpRemoteReport remote_report;
    bool socket_open;
    bool peer_valid;
} SnfNetTestThreadContext;

typedef char SnfNetTestUdpHeaderSizeCheck[
    (sizeof(SnfNetTestIperfUdpHeader) == SNF_NET_TEST_IPERF_UDP_HEADER_SIZE) ? 1 : -1];
typedef char SnfNetTestServerHeaderSizeCheck[
    (sizeof(SnfNetTestIperfServerHeader) == SNF_NET_TEST_IPERF_SERVER_HEADER_SIZE) ? 1 : -1];

static SnfNetTestConfig snf_net_test_config = {
    .thread_mutex = NULL,
    .thread_task_handle = NULL,
    .wifi_tcp_task_handle = NULL,
    .wifi_udp_task_handle = NULL,
    .wifi_ps_hold_count = 0U,
};

#if SNR_NET_TEST_THREAD_TEST_ENABLE
static SnfNetTestThreadContext snf_net_test_thread_context;
#endif

static void snfNetTestThreadReceive(void *context, otMessage *message, const otMessageInfo *message_info);

static void snfNetTestFeedWatchdog(void)
{
#if CONFIG_TASK_WDT
    bk_task_wdt_feed();
#endif
}

static TickType_t snfNetTestDurationTicks(void)
{
    return pdMS_TO_TICKS(SNF_NET_TEST_DURATION_SECONDS * 1000U);
}

static uint32_t snfNetTestCalculateBitrate(uint64_t bytes, TickType_t elapsed_ticks)
{
    uint64_t bitrate = 0U;

    if (elapsed_ticks > 0U)
    {
        bitrate = (bytes * 8U * configTICK_RATE_HZ) / elapsed_ticks;
        if (bitrate > UINT32_MAX)
        {
            bitrate = UINT32_MAX;
        }
    }

    return (uint32_t)bitrate;
}

static uint32_t snfNetTestTicksToMilliseconds(TickType_t ticks)
{
    uint64_t milliseconds = ((uint64_t)ticks * 1000U) / configTICK_RATE_HZ;

    if (milliseconds > UINT32_MAX)
    {
        milliseconds = UINT32_MAX;
    }

    return (uint32_t)milliseconds;
}

static uint32_t snfNetTestBitrateToLogScale(uint32_t bitrate, bool use_kilo_units)
{
    uint32_t bits_per_unit = use_kilo_units ? SNF_NET_BITS_PER_KILOBIT : SNF_NET_BITS_PER_MEGABIT;
    uint32_t whole_units = bitrate / bits_per_unit;
    uint32_t remainder = bitrate % bits_per_unit;

    return (whole_units * SNF_NET_LOG_FRACTION_SCALE) +
           ((remainder * SNF_NET_LOG_FRACTION_SCALE) / bits_per_unit);
}

static uint64_t snfNetTestBytesToLogScale(uint64_t bytes, bool use_kilo_units)
{
    uint32_t bytes_per_unit = use_kilo_units ? SNF_NET_BYTES_PER_KILOBIT : SNF_NET_BYTES_PER_MEGABIT;
    uint64_t whole_units = bytes / bytes_per_unit;
    uint64_t remainder = bytes % bytes_per_unit;

    return (whole_units * SNF_NET_LOG_FRACTION_SCALE) +
           ((remainder * SNF_NET_LOG_FRACTION_SCALE) / bytes_per_unit);
}

static void snfNetTestStatsInit(SnfNetTestStats *stats, TickType_t now)
{
    memset(stats, 0, sizeof(*stats));
    stats->start_tick = now;
    stats->sample_tick = now;
    stats->progress_tick = now;
}

static void snfNetTestStatsRecord(SnfNetTestStats *stats, uint32_t byte_count)
{
    stats->total_bytes += byte_count;
    stats->sample_bytes += byte_count;
}

static void snfNetTestStatsSample(SnfNetTestStats *stats, TickType_t now, bool force)
{
    TickType_t elapsed_ticks = now - stats->sample_tick;
    TickType_t sample_ticks = pdMS_TO_TICKS(SNF_NET_TEST_SAMPLE_INTERVAL_MS);

    if (((elapsed_ticks >= sample_ticks) || force) && (elapsed_ticks > 0U))
    {
        stats->current_bitrate = snfNetTestCalculateBitrate(stats->sample_bytes, elapsed_ticks);
        if (stats->current_bitrate > stats->peak_bitrate)
        {
            stats->peak_bitrate = stats->current_bitrate;
        }
        stats->sample_bytes = 0U;
        stats->sample_tick = now;
    }
}

static bool snfNetTestStatsPollProgress(SnfNetTestStats *stats, TickType_t now)
{
    TickType_t progress_ticks = pdMS_TO_TICKS(SNF_NET_TEST_PROGRESS_INTERVAL_SECONDS * 1000U);
    bool report_due = false;

    snfNetTestStatsSample(stats, now, false);
    if ((now - stats->progress_tick) >= progress_ticks)
    {
        stats->progress_tick = now;
        report_due = true;
    }

    return report_due;
}

static uint32_t snfNetTestStatsAverage(const SnfNetTestStats *stats, TickType_t end_tick)
{
    return snfNetTestCalculateBitrate(stats->total_bytes, end_tick - stats->start_tick);
}

static void snfNetTestLogLossRate(const char *protocol, uint32_t lost_packets, uint32_t sent_packets)
{
    uint32_t basis_points = 0U;

    if (sent_packets > 0U)
    {
        basis_points = (uint32_t)(((uint64_t)lost_packets * 10000U) / sent_packets);
    }
    LOG_I(tag,
          "%s RESULT loss_rate=%u.%02u%%",
          protocol,
          basis_points / 100U,
          basis_points % 100U);
}

static void snfNetTestLogProgress(const char *protocol, const SnfNetTestStats *stats, TickType_t now)
{
    bool use_kilo_units = (strcmp(protocol, "THREAD_UDP") == 0);
    uint32_t current_rate_scaled = snfNetTestBitrateToLogScale(stats->current_bitrate, use_kilo_units);
    uint32_t peak_rate_scaled = snfNetTestBitrateToLogScale(stats->peak_bitrate, use_kilo_units);
    uint64_t transferred_scaled = snfNetTestBytesToLogScale(stats->total_bytes, use_kilo_units);
    const char *rate_unit = use_kilo_units ? "Kbps" : "Mbps";
    const char *data_unit = use_kilo_units ? "Kb" : "Mb";

    LOG_I(tag,
          "%s progress elapsed_s=%u current_bitrate=%u.%02u%s peak_bitrate=%u.%02u%s "
          "transferred=%llu.%02llu%s",
          protocol,
          snfNetTestTicksToMilliseconds(now - stats->start_tick) / 1000U,
          current_rate_scaled / SNF_NET_LOG_FRACTION_SCALE,
          current_rate_scaled % SNF_NET_LOG_FRACTION_SCALE,
          rate_unit,
          peak_rate_scaled / SNF_NET_LOG_FRACTION_SCALE,
          peak_rate_scaled % SNF_NET_LOG_FRACTION_SCALE,
          rate_unit,
          (unsigned long long)(transferred_scaled / SNF_NET_LOG_FRACTION_SCALE),
          (unsigned long long)(transferred_scaled % SNF_NET_LOG_FRACTION_SCALE),
          data_unit);
}

static void snfNetTestLogTcpResult(const char *role,
                                   const SnfNetTestStats *stats,
                                   TickType_t end_tick,
                                   bool retransmission_valid,
                                   uint32_t retransmission_count,
                                   uint32_t transmitted_segment_count)
{
    uint32_t retransmission_basis_points = 0U;
    uint32_t average_mbps_scaled = snfNetTestBitrateToLogScale(snfNetTestStatsAverage(stats, end_tick), false);
    uint32_t peak_mbps_scaled = snfNetTestBitrateToLogScale(stats->peak_bitrate, false);
    uint64_t transferred_mb_scaled = snfNetTestBytesToLogScale(stats->total_bytes, false);

    LOG_I(tag, "WIFI_TCP RESULT role=%s duration_ms=%u", role,
          snfNetTestTicksToMilliseconds(end_tick - stats->start_tick));
    LOG_I(tag,
          "WIFI_TCP RESULT average_throughput=%u.%02uMbps",
          average_mbps_scaled / SNF_NET_LOG_FRACTION_SCALE,
          average_mbps_scaled % SNF_NET_LOG_FRACTION_SCALE);
    LOG_I(tag,
          "WIFI_TCP RESULT peak_throughput=%u.%02uMbps",
          peak_mbps_scaled / SNF_NET_LOG_FRACTION_SCALE,
          peak_mbps_scaled % SNF_NET_LOG_FRACTION_SCALE);
    LOG_I(tag,
          "WIFI_TCP RESULT transferred=%llu.%02lluMb",
          (unsigned long long)(transferred_mb_scaled / SNF_NET_LOG_FRACTION_SCALE),
          (unsigned long long)(transferred_mb_scaled % SNF_NET_LOG_FRACTION_SCALE));

    if (retransmission_valid && (transmitted_segment_count > 0U))
    {
        retransmission_basis_points =
            (uint32_t)(((uint64_t)retransmission_count * 10000U) / transmitted_segment_count);
        LOG_I(tag,
              "WIFI_TCP RESULT retransmission_rate=%u.%02u%% retransmitted_segments=%u transmitted_segments=%u",
              retransmission_basis_points / 100U,
              retransmission_basis_points % 100U,
              retransmission_count,
              transmitted_segment_count);
    }
    else
    {
        LOG_I(tag, "WIFI_TCP RESULT retransmission_rate=N/A reason=lwip_MIB2_STATS_unavailable_or_receiver_mode");
    }
}

static void snfNetTestLogUdpSenderResult(const char *protocol,
                                         uint32_t target_bitrate,
                                         uint32_t sent_packets,
                                         uint32_t send_errors,
                                         const SnfNetTestStats *local_stats,
                                         TickType_t end_tick,
                                         const SnfNetTestUdpRemoteReport *remote_report)
{
    bool use_kilo_units = (strcmp(protocol, "THREAD_UDP") == 0);
    uint32_t local_average_bitrate = snfNetTestStatsAverage(local_stats, end_tick);
    uint32_t target_rate_scaled = snfNetTestBitrateToLogScale(target_bitrate, use_kilo_units);
    uint32_t local_average_rate_scaled = snfNetTestBitrateToLogScale(local_average_bitrate, use_kilo_units);
    uint32_t local_peak_rate_scaled = snfNetTestBitrateToLogScale(local_stats->peak_bitrate, use_kilo_units);
    uint32_t remote_average_rate_scaled =
        snfNetTestBitrateToLogScale(remote_report->average_bitrate, use_kilo_units);
    uint64_t received_scaled = snfNetTestBytesToLogScale(remote_report->received_bytes, use_kilo_units);
    const char *rate_unit = use_kilo_units ? "Kbps" : "Mbps";
    const char *data_unit = use_kilo_units ? "Kb" : "Mb";

    LOG_I(tag,
          "%s RESULT role=sender target_bitrate=%u.%02u%s",
          protocol,
          target_rate_scaled / SNF_NET_LOG_FRACTION_SCALE,
          target_rate_scaled % SNF_NET_LOG_FRACTION_SCALE,
          rate_unit);
    LOG_I(tag, "%s RESULT local_send_average=%u.%02u%s local_send_peak=%u.%02u%s",
          protocol,
          local_average_rate_scaled / SNF_NET_LOG_FRACTION_SCALE,
          local_average_rate_scaled % SNF_NET_LOG_FRACTION_SCALE,
          rate_unit,
          local_peak_rate_scaled / SNF_NET_LOG_FRACTION_SCALE,
          local_peak_rate_scaled % SNF_NET_LOG_FRACTION_SCALE,
          rate_unit);
    LOG_I(tag, "%s RESULT sent_packets=%u local_send_errors=%u", protocol, sent_packets, send_errors);

    if (remote_report->received)
    {
        LOG_I(tag,
              "%s RESULT average_goodput=%u.%02u%s",
              protocol,
              remote_average_rate_scaled / SNF_NET_LOG_FRACTION_SCALE,
              remote_average_rate_scaled % SNF_NET_LOG_FRACTION_SCALE,
              rate_unit);
        LOG_I(tag, "%s RESULT peak_goodput=N/A reason=iperf2_server_report_has_no_peak", protocol);
        LOG_I(tag,
              "%s RESULT received_packets=%u lost_packets=%u out_of_order_packets=%u received=%llu.%02llu%s",
              protocol,
              remote_report->received_packets,
              remote_report->lost_packets,
              remote_report->out_of_order_packets,
              (unsigned long long)(received_scaled / SNF_NET_LOG_FRACTION_SCALE),
              (unsigned long long)(received_scaled % SNF_NET_LOG_FRACTION_SCALE),
              data_unit);
        snfNetTestLogLossRate(protocol, remote_report->lost_packets, remote_report->sent_packets);
    }
    else
    {
        LOG_I(tag, "%s RESULT average_goodput=N/A peak_goodput=N/A", protocol);
        LOG_I(tag, "%s RESULT received_packets=N/A lost_packets=N/A loss_rate=N/A reason=no_peer_report", protocol);
    }
}

static void snfNetTestLogUdpReceiverResult(const char *protocol,
                                           const SnfNetTestUdpReceiver *receiver,
                                           TickType_t end_tick)
{
    bool use_kilo_units = (strcmp(protocol, "THREAD_UDP") == 0);
    uint32_t lost_packets = 0U;
    uint32_t sent_packets = receiver->sender_packets;
    uint32_t average_bitrate = snfNetTestStatsAverage(&receiver->stats, end_tick);
    uint32_t target_rate_scaled = snfNetTestBitrateToLogScale(receiver->target_bitrate, use_kilo_units);
    uint32_t average_rate_scaled = snfNetTestBitrateToLogScale(average_bitrate, use_kilo_units);
    uint32_t peak_rate_scaled = snfNetTestBitrateToLogScale(receiver->stats.peak_bitrate, use_kilo_units);
    uint64_t received_scaled = snfNetTestBytesToLogScale(receiver->stats.total_bytes, use_kilo_units);
    const char *rate_unit = use_kilo_units ? "Kbps" : "Mbps";
    const char *data_unit = use_kilo_units ? "Kb" : "Mb";

    if (sent_packets == 0U)
    {
        sent_packets = receiver->highest_sequence;
    }
    if (sent_packets > receiver->received_packets)
    {
        lost_packets = sent_packets - receiver->received_packets;
    }

    LOG_I(tag,
          "%s RESULT role=receiver target_bitrate=%u.%02u%s target_bitrate_source=configured_value",
          protocol,
          target_rate_scaled / SNF_NET_LOG_FRACTION_SCALE,
          target_rate_scaled % SNF_NET_LOG_FRACTION_SCALE,
          rate_unit);
    LOG_I(tag,
          "%s RESULT average_goodput=%u.%02u%s",
          protocol,
          average_rate_scaled / SNF_NET_LOG_FRACTION_SCALE,
          average_rate_scaled % SNF_NET_LOG_FRACTION_SCALE,
          rate_unit);
    LOG_I(tag,
          "%s RESULT peak_goodput=%u.%02u%s",
          protocol,
          peak_rate_scaled / SNF_NET_LOG_FRACTION_SCALE,
          peak_rate_scaled % SNF_NET_LOG_FRACTION_SCALE,
          rate_unit);
    LOG_I(tag,
          "%s RESULT sent_packets=%u received_packets=%u lost_packets=%u out_of_order_packets=%u "
          "received=%llu.%02llu%s",
          protocol,
          sent_packets,
          receiver->received_packets,
          lost_packets,
          receiver->out_of_order_packets,
          (unsigned long long)(received_scaled / SNF_NET_LOG_FRACTION_SCALE),
          (unsigned long long)(received_scaled % SNF_NET_LOG_FRACTION_SCALE),
          data_unit);
    snfNetTestLogLossRate(protocol, lost_packets, sent_packets);
}

static void snfNetTestBuildUdpData(uint8_t *buffer,
                                   uint16_t length,
                                   uint32_t sequence)
{
    SnfNetTestIperfUdpHeader udp_header = {0};
    TickType_t now = xTaskGetTickCount();

    memset(buffer, 0, length);
    udp_header.id = (int32_t)lwip_htonl(sequence);
    udp_header.seconds = lwip_htonl((uint32_t)(now / configTICK_RATE_HZ));
    udp_header.microseconds = lwip_htonl(
        (uint32_t)(((uint64_t)(now % configTICK_RATE_HZ) * 1000000U) / configTICK_RATE_HZ));
    memcpy(buffer, &udp_header, sizeof(udp_header));
}

static void snfNetTestBuildUdpFinish(uint8_t *buffer,
                                     uint16_t length,
                                     uint32_t sent_packets)
{
    SnfNetTestIperfUdpHeader udp_header = {0};
    TickType_t now = xTaskGetTickCount();
    uint32_t finish_id = sent_packets;

    if (finish_id == 0)
    {
        finish_id = 1U;
    }
    finish_id = (~finish_id) + 1U;
    memset(buffer, 0, length);
    udp_header.id = (int32_t)lwip_htonl(finish_id);
    udp_header.seconds = lwip_htonl((uint32_t)(now / configTICK_RATE_HZ));
    udp_header.microseconds = lwip_htonl(
        (uint32_t)(((uint64_t)(now % configTICK_RATE_HZ) * 1000000U) / configTICK_RATE_HZ));
    udp_header.id_high = (int32_t)lwip_htonl(UINT32_MAX);
    memcpy(buffer, &udp_header, sizeof(udp_header));
}

static void snfNetTestBuildUdpReport(uint8_t *buffer,
                                     uint16_t length,
                                     const SnfNetTestUdpReceiver *receiver,
                                     TickType_t end_tick)
{
    SnfNetTestIperfUdpHeader udp_header = {0};
    SnfNetTestIperfServerHeader server_header = {0};
    TickType_t duration_ticks = end_tick - receiver->stats.start_tick;
    uint32_t lost_packets = 0U;
    uint32_t sent_packets = receiver->sender_packets;
    uint32_t duration_seconds = duration_ticks / configTICK_RATE_HZ;
    uint32_t duration_microseconds =
        (uint32_t)(((uint64_t)(duration_ticks % configTICK_RATE_HZ) * 1000000U) / configTICK_RATE_HZ);

    if (sent_packets == 0U)
    {
        sent_packets = receiver->highest_sequence;
    }
    if (sent_packets > receiver->received_packets)
    {
        lost_packets = sent_packets - receiver->received_packets;
    }

    memset(buffer, 0, length);
    udp_header.id = (int32_t)lwip_htonl(receiver->finish_id);
    udp_header.id_high = (int32_t)lwip_htonl(receiver->finish_id_high);
    memcpy(buffer, &udp_header, sizeof(udp_header));

    server_header.flags = (int32_t)lwip_htonl(SNF_NET_TEST_IPERF_SERVER_FLAG);
    server_header.total_length_high = (int32_t)lwip_htonl((uint32_t)(receiver->stats.total_bytes >> 32U));
    server_header.total_length_low =
        (int32_t)lwip_htonl((uint32_t)(receiver->stats.total_bytes & 0xFFFFFFFFULL));
    server_header.duration_seconds = (int32_t)lwip_htonl(duration_seconds);
    server_header.duration_microseconds = (int32_t)lwip_htonl(duration_microseconds);
    server_header.error_count = (int32_t)lwip_htonl(lost_packets);
    server_header.out_of_order_count = (int32_t)lwip_htonl(receiver->out_of_order_packets);
    server_header.datagram_count = (int32_t)lwip_htonl(sent_packets);
    memcpy(buffer + sizeof(udp_header), &server_header, sizeof(server_header));
}

static bool snfNetTestParseUdpReport(const uint8_t *buffer,
                                     uint16_t length,
                                     SnfNetTestUdpRemoteReport *report)
{
    SnfNetTestIperfServerHeader network_header = {0};
    uint64_t duration_microseconds = 0U;
    uint64_t bitrate = 0U;
    uint32_t error_count = 0U;
    uint32_t datagram_count = 0U;
    bool valid = false;

    if (length >= (SNF_NET_TEST_IPERF_UDP_HEADER_SIZE + SNF_NET_TEST_IPERF_SERVER_HEADER_SIZE))
    {
        memcpy(&network_header, buffer + SNF_NET_TEST_IPERF_UDP_HEADER_SIZE, sizeof(network_header));
        if ((lwip_ntohl((uint32_t)network_header.flags) & SNF_NET_TEST_IPERF_SERVER_FLAG) != 0U)
        {
            memset(report, 0, sizeof(*report));
            report->received_bytes =
                ((uint64_t)lwip_ntohl((uint32_t)network_header.total_length_high) << 32U) |
                lwip_ntohl((uint32_t)network_header.total_length_low);
            duration_microseconds =
                ((uint64_t)lwip_ntohl((uint32_t)network_header.duration_seconds) * 1000000U) +
                lwip_ntohl((uint32_t)network_header.duration_microseconds);
            error_count = lwip_ntohl((uint32_t)network_header.error_count);
            datagram_count = lwip_ntohl((uint32_t)network_header.datagram_count);
            report->sent_packets = datagram_count;
            report->lost_packets = error_count;
            report->received_packets = (datagram_count >= error_count) ? (datagram_count - error_count) : 0U;
            report->out_of_order_packets = lwip_ntohl((uint32_t)network_header.out_of_order_count);
            if (duration_microseconds > 0U)
            {
                bitrate = (report->received_bytes * 8U * 1000000U) / duration_microseconds;
                report->average_bitrate = (bitrate > UINT32_MAX) ? UINT32_MAX : (uint32_t)bitrate;
            }
            report->received = true;
            valid = true;
        }
    }

    return valid;
}

static int snfNetTestUdpReceiverProcess(SnfNetTestUdpReceiver *receiver,
                                        const uint8_t *buffer,
                                        uint16_t length,
                                        uint32_t default_target_bitrate,
                                        TickType_t now)
{
    SnfNetTestIperfUdpHeader network_header = {0};
    uint32_t packet_id = 0U;
    uint32_t sequence = 0U;
    uint32_t negative_sequence = 0U;
    int result = 0;

    if (length >= sizeof(network_header))
    {
        memcpy(&network_header, buffer, sizeof(network_header));
        packet_id = lwip_ntohl((uint32_t)network_header.id);

        if (!receiver->started)
        {
            snfNetTestStatsInit(&receiver->stats, now);
            receiver->started = true;
            receiver->target_bitrate = default_target_bitrate;
        }

        if ((packet_id & SNF_NET_TEST_IPERF_FIN_FLAG) != 0U)
        {
            /* iperf2不同版本使用-sequence或sequence | 0x80000000编码结束序号。 */
            sequence = packet_id & SNF_NET_TEST_IPERF_SEQUENCE_MASK;
            negative_sequence = (~packet_id) + 1U;
            if (negative_sequence < sequence)
            {
                sequence = negative_sequence;
            }
            receiver->sender_packets = sequence;
            if (receiver->sender_packets < UINT32_MAX)
            {
                receiver->sender_packets++;
            }
            receiver->finish_id = packet_id;
            receiver->finish_id_high = lwip_ntohl((uint32_t)network_header.id_high);
            receiver->finish_received = true;
            result = 2;
        }
        else
        {
            sequence = packet_id;
            if ((receiver->received_packets > 0U) && (sequence <= receiver->highest_sequence))
            {
                receiver->out_of_order_packets++;
            }
            if (sequence > receiver->highest_sequence)
            {
                receiver->highest_sequence = sequence;
            }
            receiver->received_packets++;
            snfNetTestStatsRecord(&receiver->stats, length);
            result = 1;
        }
    }

    return result;
}

static int snfNetTestSetSocketTimeout(int socket_fd)
{
    struct timeval timeout = {0};
    int ret = 0;

    timeout.tv_sec = SNF_NET_TEST_SOCKET_TIMEOUT_MS / 1000U;
    timeout.tv_usec = (SNF_NET_TEST_SOCKET_TIMEOUT_MS % 1000U) * 1000U;
    if (lwip_setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
    {
        ret = -1;
    }
    else if (lwip_setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
    {
        ret = -1;
    }

    return ret;
}

static uint8_t *snfNetTestAllocBuffer(uint32_t size)
{
    uint8_t *buffer = (uint8_t *)pvPortMalloc(size);

    if (buffer == NULL)
    {
        LOG_E(tag, "Allocate network test buffer failed, size=%u", size);
    }
    else
    {
        memset(buffer, 0, size);
    }

    return buffer;
}

static void snfNetTestWifiPsHold(void)
{
    SnfNetTestConfig *net_test = &snf_net_test_config;
    bool disable_ps = false;

    taskENTER_CRITICAL();
    if (net_test->wifi_ps_hold_count == 0U)
    {
        disable_ps = true;
    }
    net_test->wifi_ps_hold_count++;
    taskEXIT_CRITICAL();

    if (disable_ps && (bk_wifi_sta_pm_disable() != BK_OK))
    {
        LOG_W(tag, "Disable Wi-Fi STA power save failed");
    }
}

static void snfNetTestWifiPsRelease(void)
{
    SnfNetTestConfig *net_test = &snf_net_test_config;
    bool restore_ps = false;

    taskENTER_CRITICAL();
    if (net_test->wifi_ps_hold_count > 0U)
    {
        net_test->wifi_ps_hold_count--;
        if (net_test->wifi_ps_hold_count == 0U)
        {
            restore_ps = true;
        }
    }
    taskEXIT_CRITICAL();

    if (restore_ps && (bk_wifi_sta_pm_enable() != BK_OK))
    {
        LOG_W(tag, "Restore Wi-Fi STA power save failed");
    }
}

static int snfNetTestTcpSendAll(int socket_fd, const uint8_t *buffer, uint32_t length, uint32_t *sent_bytes)
{
    uint32_t offset = 0U;
    uint32_t timeout_retries = 0U;
    int send_length = 0;
    int ret = 0;

    *sent_bytes = 0U;
    while (offset < length)
    {
        send_length = lwip_send(socket_fd, &buffer[offset], (size_t)(length - offset), 0);
        if (send_length > 0)
        {
            offset += (uint32_t)send_length;
            timeout_retries = 0U;
            snfNetTestFeedWatchdog();
            continue;
        }

        if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
        {
            timeout_retries++;
            if (timeout_retries >= SNF_NET_TEST_TCP_TIMEOUT_RETRY_MAX)
            {
                LOG_E(tag, "Wi-Fi TCP send reached max timeout retry");
                ret = -1;
                break;
            }
            snfNetTestFeedWatchdog();
            continue;
        }

        LOG_E(tag, "Wi-Fi TCP send failed, errno=%d", errno);
        ret = -1;
        break;
    }

    *sent_bytes = offset;
    return ret;
}

static int snfNetTestBuildWifiPeerAddress(struct sockaddr_in *address, uint16_t port)
{
    int ret = 0;

    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = lwip_htons(port);
    if (inet_aton(SNF_NET_TEST_WIFI_PEER_IPV4, &address->sin_addr) == 0)
    {
        LOG_E(tag, "Invalid Wi-Fi peer IPv4 address: %s", SNF_NET_TEST_WIFI_PEER_IPV4);
        ret = -1;
    }

    return ret;
}

static int snfNetTestWifiTcpRunSender(void)
{
    uint8_t *buffer = NULL;
    struct sockaddr_in peer_address = {0};
    SnfNetTestStats stats = {0};
    TickType_t now = 0U;
    TickType_t end_tick = 0U;
    uint32_t sent_bytes = 0U;
    int socket_fd = -1;
    int send_result = 0;
    int tcp_no_delay = 1;
    int ret = -1;
    bool stats_started = false;
    bool wifi_ps_held = false;
#if MIB2_STATS
    uint32_t start_retransmissions = 0U;
    uint32_t start_transmitted_segments = 0U;
    uint32_t retransmissions = 0U;
    uint32_t transmitted_segments = 0U;
#endif

    buffer = snfNetTestAllocBuffer(SNF_NET_TEST_WIFI_TCP_PAYLOAD_SIZE);
    if (buffer == NULL)
    {
        goto cleanup;
    }
    if (snfNetTestBuildWifiPeerAddress(&peer_address, SNF_NET_TEST_WIFI_TCP_PORT) != 0)
    {
        goto cleanup;
    }

    snfNetTestWifiPsHold();
    wifi_ps_held = true;

    socket_fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd < 0)
    {
        LOG_E(tag, "Create Wi-Fi TCP socket failed, errno=%d", errno);
        goto cleanup;
    }
    if (snfNetTestSetSocketTimeout(socket_fd) != 0)
    {
        LOG_E(tag, "Set Wi-Fi TCP socket timeout failed, errno=%d", errno);
        goto cleanup;
    }
    (void)lwip_setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &tcp_no_delay, sizeof(tcp_no_delay));

    LOG_I(tag, "WIFI_TCP sender connecting to %s:%u payload_bytes=%u", SNF_NET_TEST_WIFI_PEER_IPV4,
          SNF_NET_TEST_WIFI_TCP_PORT, SNF_NET_TEST_WIFI_TCP_PAYLOAD_SIZE);
    if (lwip_connect(socket_fd, (const struct sockaddr *)&peer_address, sizeof(peer_address)) != 0)
    {
        LOG_E(tag, "Connect Wi-Fi TCP peer failed, errno=%d", errno);
        goto cleanup;
    }

#if MIB2_STATS
    start_retransmissions = lwip_stats.mib2.tcpretranssegs;
    start_transmitted_segments = lwip_stats.mib2.tcpoutsegs;
#endif
    now = xTaskGetTickCount();
    snfNetTestStatsInit(&stats, now);
    stats_started = true;
    LOG_I(tag, "WIFI_TCP sender started duration_s=%u", SNF_NET_TEST_DURATION_SECONDS);

    while ((xTaskGetTickCount() - stats.start_tick) < snfNetTestDurationTicks())
    {
        send_result = snfNetTestTcpSendAll(socket_fd, buffer, SNF_NET_TEST_WIFI_TCP_PAYLOAD_SIZE, &sent_bytes);
        now = xTaskGetTickCount();
        if (sent_bytes > 0U)
        {
            snfNetTestStatsRecord(&stats, sent_bytes);
        }
        if (send_result != 0)
        {
            break;
        }

        if (snfNetTestStatsPollProgress(&stats, now))
        {
            snfNetTestLogProgress("WIFI_TCP", &stats, now);
        }
        snfNetTestFeedWatchdog();
    }
    ret = 0;

cleanup:
    end_tick = xTaskGetTickCount();
    if (stats_started)
    {
        snfNetTestStatsSample(&stats, end_tick, true);
#if MIB2_STATS
        retransmissions = lwip_stats.mib2.tcpretranssegs - start_retransmissions;
        transmitted_segments = lwip_stats.mib2.tcpoutsegs - start_transmitted_segments;
        snfNetTestLogTcpResult("sender", &stats, end_tick, true, retransmissions, transmitted_segments);
#else
        snfNetTestLogTcpResult("sender", &stats, end_tick, false, 0U, 0U);
#endif
    }
    if (socket_fd >= 0)
    {
        (void)lwip_shutdown(socket_fd, SHUT_RDWR);
        (void)lwip_close(socket_fd);
    }
    if (wifi_ps_held)
    {
        snfNetTestWifiPsRelease();
    }
    if (buffer != NULL)
    {
        vPortFree(buffer);
    }

    return ret;
}

static int snfNetTestWifiTcpRunReceiver(void)
{
    uint8_t *buffer = NULL;
    struct sockaddr_in local_address = {0};
    struct sockaddr_in peer_address = {0};
    SnfNetTestStats stats = {0};
    socklen_t peer_address_length = sizeof(peer_address);
    TickType_t now = 0U;
    TickType_t end_tick = 0U;
    int listen_fd = -1;
    int connection_fd = -1;
    int receive_length = 0;
    int reuse_address = 1;
    int ret = -1;
    bool stats_started = false;
    bool wifi_ps_held = false;

    buffer = snfNetTestAllocBuffer(SNF_NET_TEST_WIFI_TCP_PAYLOAD_SIZE);
    if (buffer == NULL)
    {
        goto cleanup;
    }

    snfNetTestWifiPsHold();
    wifi_ps_held = true;

    listen_fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd < 0)
    {
        LOG_E(tag, "Create Wi-Fi TCP listen socket failed, errno=%d", errno);
        goto cleanup;
    }
    (void)lwip_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address));

    local_address.sin_family = AF_INET;
    local_address.sin_port = lwip_htons(SNF_NET_TEST_WIFI_TCP_PORT);
    local_address.sin_addr.s_addr = PP_HTONL(INADDR_ANY);
    if (lwip_bind(listen_fd, (const struct sockaddr *)&local_address, sizeof(local_address)) != 0)
    {
        LOG_E(tag, "Bind Wi-Fi TCP port %u failed, errno=%d", SNF_NET_TEST_WIFI_TCP_PORT, errno);
        goto cleanup;
    }
    if (lwip_listen(listen_fd, 1) != 0)
    {
        LOG_E(tag, "Listen on Wi-Fi TCP port failed, errno=%d", errno);
        goto cleanup;
    }

    LOG_I(tag, "WIFI_TCP receiver waiting on port %u payload_bytes=%u", SNF_NET_TEST_WIFI_TCP_PORT,
          SNF_NET_TEST_WIFI_TCP_PAYLOAD_SIZE);
    connection_fd = lwip_accept(listen_fd, (struct sockaddr *)&peer_address, &peer_address_length);
    if (connection_fd < 0)
    {
        LOG_E(tag, "Accept Wi-Fi TCP connection failed, errno=%d", errno);
        goto cleanup;
    }
    if (snfNetTestSetSocketTimeout(connection_fd) != 0)
    {
        LOG_E(tag, "Set Wi-Fi TCP connection timeout failed, errno=%d", errno);
        goto cleanup;
    }

    now = xTaskGetTickCount();
    snfNetTestStatsInit(&stats, now);
    stats_started = true;
    LOG_I(tag, "WIFI_TCP receiver started duration_s=%u", SNF_NET_TEST_DURATION_SECONDS);

    while ((xTaskGetTickCount() - stats.start_tick) < snfNetTestDurationTicks())
    {
        receive_length = lwip_recv(connection_fd, buffer, SNF_NET_TEST_WIFI_TCP_PAYLOAD_SIZE, 0);
        now = xTaskGetTickCount();
        if (receive_length > 0)
        {
            snfNetTestStatsRecord(&stats, (uint32_t)receive_length);
        }
        else if (receive_length == 0)
        {
            LOG_W(tag, "Wi-Fi TCP peer closed the connection before local timer expired");
            break;
        }
        else if ((errno != EWOULDBLOCK) && (errno != EAGAIN))
        {
            LOG_E(tag, "Wi-Fi TCP receive failed, errno=%d", errno);
            break;
        }

        if (snfNetTestStatsPollProgress(&stats, now))
        {
            snfNetTestLogProgress("WIFI_TCP", &stats, now);
        }
        snfNetTestFeedWatchdog();
    }
    ret = 0;

cleanup:
    end_tick = xTaskGetTickCount();
    if (stats_started)
    {
        snfNetTestStatsSample(&stats, end_tick, true);
        snfNetTestLogTcpResult("receiver", &stats, end_tick, false, 0U, 0U);
    }
    if (connection_fd >= 0)
    {
        (void)lwip_shutdown(connection_fd, SHUT_RDWR);
        (void)lwip_close(connection_fd);
    }
    if (listen_fd >= 0)
    {
        (void)lwip_close(listen_fd);
    }
    if (wifi_ps_held)
    {
        snfNetTestWifiPsRelease();
    }
    if (buffer != NULL)
    {
        vPortFree(buffer);
    }

    return ret;
}

static int snfNetTestWifiUdpRunSender(void)
{
    uint8_t buffer[SNF_NET_TEST_WIFI_UDP_PAYLOAD_SIZE];
    struct sockaddr_in peer_address = {0};
    struct sockaddr_in report_address = {0};
    SnfNetTestStats stats = {0};
    SnfNetTestUdpRemoteReport remote_report = {0};
    socklen_t report_address_length = sizeof(report_address);
    TickType_t now = 0U;
    TickType_t end_tick = 0U;
    uint64_t desired_packets = 0U;
    uint64_t scheduled_packets = 0U;
    uint64_t pacing_denominator =
        (uint64_t)SNF_NET_TEST_WIFI_UDP_PAYLOAD_SIZE * 8U * configTICK_RATE_HZ;
    uint32_t sent_packets = 0U;
    uint32_t send_errors = 0U;
    uint32_t burst_count = 0U;
    uint32_t retry = 0U;
    uint32_t target_mbps_scaled = snfNetTestBitrateToLogScale(SNF_NET_TEST_WIFI_UDP_TARGET_BITRATE, false);
    int socket_fd = -1;
    int send_length = 0;
    int receive_length = 0;
    int ret = -1;
    bool stats_started = false;
    bool wifi_ps_held = false;

    if (snfNetTestBuildWifiPeerAddress(&peer_address, SNF_NET_TEST_WIFI_UDP_PORT) != 0)
    {
        goto cleanup;
    }

    snfNetTestWifiPsHold();
    wifi_ps_held = true;

    socket_fd = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0)
    {
        LOG_E(tag, "Create Wi-Fi UDP socket failed, errno=%d", errno);
        goto cleanup;
    }
    if (snfNetTestSetSocketTimeout(socket_fd) != 0)
    {
        LOG_E(tag, "Set Wi-Fi UDP socket timeout failed, errno=%d", errno);
        goto cleanup;
    }

    now = xTaskGetTickCount();
    snfNetTestStatsInit(&stats, now);
    stats_started = true;
    LOG_I(tag,
          "WIFI_UDP sender started peer=%s:%u duration_s=%u payload_bytes=%u target_bitrate=%u.%02uMbps",
          SNF_NET_TEST_WIFI_PEER_IPV4,
          SNF_NET_TEST_WIFI_UDP_PORT,
          SNF_NET_TEST_DURATION_SECONDS,
          SNF_NET_TEST_WIFI_UDP_PAYLOAD_SIZE,
          target_mbps_scaled / SNF_NET_LOG_FRACTION_SCALE,
          target_mbps_scaled % SNF_NET_LOG_FRACTION_SCALE);

    while ((xTaskGetTickCount() - stats.start_tick) < snfNetTestDurationTicks())
    {
        now = xTaskGetTickCount();
        desired_packets =
            ((uint64_t)(now - stats.start_tick) * SNF_NET_TEST_WIFI_UDP_TARGET_BITRATE) / pacing_denominator;
        burst_count = 0U;
        while ((scheduled_packets < desired_packets) && (burst_count < SNF_NET_TEST_SEND_BURST_MAX))
        {
            snfNetTestBuildUdpData(buffer,
                                   sizeof(buffer),
                                   sent_packets + 1U);
            send_length = lwip_sendto(socket_fd,
                                      buffer,
                                      sizeof(buffer),
                                      0,
                                      (const struct sockaddr *)&peer_address,
                                      sizeof(peer_address));
            scheduled_packets++;
            burst_count++;
            if (send_length == (int)sizeof(buffer))
            {
                sent_packets++;
                snfNetTestStatsRecord(&stats, (uint32_t)send_length);
            }
            else
            {
                send_errors++;
            }
        }

        now = xTaskGetTickCount();
        if (snfNetTestStatsPollProgress(&stats, now))
        {
            snfNetTestLogProgress("WIFI_UDP", &stats, now);
        }
        if (burst_count == 0U)
        {
            vTaskDelay(1U);
        }
        snfNetTestFeedWatchdog();
    }

    end_tick = xTaskGetTickCount();
    snfNetTestStatsSample(&stats, end_tick, true);
    snfNetTestBuildUdpFinish(buffer,
                             sizeof(buffer),
                             sent_packets);
    for (retry = 0U; (retry < SNF_NET_TEST_FIN_RETRY_COUNT) && !remote_report.received; retry++)
    {
        (void)lwip_sendto(socket_fd,
                          buffer,
                          sizeof(buffer),
                          0,
                          (const struct sockaddr *)&peer_address,
                          sizeof(peer_address));
        report_address_length = sizeof(report_address);
        receive_length = lwip_recvfrom(socket_fd,
                                       buffer,
                                       sizeof(buffer),
                                       0,
                                       (struct sockaddr *)&report_address,
                                       &report_address_length);
        if ((receive_length > 0) &&
            snfNetTestParseUdpReport(buffer, (uint16_t)receive_length, &remote_report))
        {
            (void)lwip_sendto(socket_fd,
                              buffer,
                              (size_t)receive_length,
                              0,
                              (const struct sockaddr *)&report_address,
                              report_address_length);
        }
    }
    ret = 0;

cleanup:
    if (socket_fd >= 0)
    {
        (void)lwip_close(socket_fd);
    }
    if (stats_started)
    {
        if (end_tick == 0U)
        {
            end_tick = xTaskGetTickCount();
            snfNetTestStatsSample(&stats, end_tick, true);
        }
        snfNetTestLogUdpSenderResult("WIFI_UDP",
                                     SNF_NET_TEST_WIFI_UDP_TARGET_BITRATE,
                                     sent_packets,
                                     send_errors,
                                     &stats,
                                     end_tick,
                                     &remote_report);
    }
    if (wifi_ps_held)
    {
        snfNetTestWifiPsRelease();
    }

    return ret;
}

static int snfNetTestWifiUdpRunReceiver(void)
{
    uint8_t buffer[SNF_NET_TEST_WIFI_UDP_PAYLOAD_SIZE];
    struct sockaddr_in local_address = {0};
    struct sockaddr_in peer_address = {0};
    SnfNetTestUdpReceiver receiver = {0};
    socklen_t peer_address_length = sizeof(peer_address);
    TickType_t now = 0U;
    TickType_t end_tick = 0U;
    TickType_t receiver_limit = snfNetTestDurationTicks() + pdMS_TO_TICKS(SNF_NET_TEST_RECEIVER_GRACE_MS);
    uint32_t retry = 0U;
    uint32_t report_send_count = 0U;
    int socket_fd = -1;
    int receive_length = 0;
    int report_send_length = 0;
    int packet_result = 0;
    int reuse_address = 1;
    int ret = -1;
    bool peer_valid = false;
    bool wifi_ps_held = false;

    snfNetTestWifiPsHold();
    wifi_ps_held = true;

    socket_fd = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0)
    {
        LOG_E(tag, "Create Wi-Fi UDP receive socket failed, errno=%d", errno);
        goto cleanup;
    }
    if (snfNetTestSetSocketTimeout(socket_fd) != 0)
    {
        LOG_E(tag, "Set Wi-Fi UDP socket timeout failed, errno=%d", errno);
        goto cleanup;
    }
    (void)lwip_setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address));

    local_address.sin_family = AF_INET;
    local_address.sin_port = lwip_htons(SNF_NET_TEST_WIFI_UDP_PORT);
    local_address.sin_addr.s_addr = PP_HTONL(INADDR_ANY);
    if (lwip_bind(socket_fd, (const struct sockaddr *)&local_address, sizeof(local_address)) != 0)
    {
        LOG_E(tag, "Bind Wi-Fi UDP port %u failed, errno=%d", SNF_NET_TEST_WIFI_UDP_PORT, errno);
        goto cleanup;
    }
    LOG_I(tag, "WIFI_UDP receiver waiting on port %u", SNF_NET_TEST_WIFI_UDP_PORT);

    while (!receiver.started || ((xTaskGetTickCount() - receiver.stats.start_tick) < receiver_limit))
    {
        peer_address_length = sizeof(peer_address);
        receive_length = lwip_recvfrom(socket_fd,
                                       buffer,
                                       sizeof(buffer),
                                       0,
                                       (struct sockaddr *)&peer_address,
                                       &peer_address_length);
        now = xTaskGetTickCount();
        if (receive_length > 0)
        {
            packet_result = snfNetTestUdpReceiverProcess(&receiver,
                                                         buffer,
                                                         (uint16_t)receive_length,
                                                         SNF_NET_TEST_WIFI_UDP_TARGET_BITRATE,
                                                         now);
            if (packet_result > 0)
            {
                peer_valid = true;
            }
            if (packet_result == 2)
            {
                break;
            }
        }
        else if ((receive_length < 0) && (errno != EWOULDBLOCK) && (errno != EAGAIN))
        {
            LOG_E(tag, "Wi-Fi UDP receive failed, errno=%d", errno);
            break;
        }

        if (receiver.started && snfNetTestStatsPollProgress(&receiver.stats, now))
        {
            snfNetTestLogProgress("WIFI_UDP", &receiver.stats, now);
        }
        snfNetTestFeedWatchdog();
    }

    if (receiver.started)
    {
        end_tick = xTaskGetTickCount();
        snfNetTestStatsSample(&receiver.stats, end_tick, true);
        if (peer_valid && receiver.finish_received)
        {
            snfNetTestBuildUdpReport(buffer, sizeof(buffer), &receiver, end_tick);
            for (retry = 0U; retry < SNF_NET_TEST_FIN_RETRY_COUNT; retry++)
            {
                report_send_length = lwip_sendto(socket_fd,
                                                  buffer,
                                                  sizeof(buffer),
                                                  0,
                                                  (const struct sockaddr *)&peer_address,
                                                  peer_address_length);
                if (report_send_length == (int)sizeof(buffer))
                {
                    report_send_count++;
                }
                else
                {
                    LOG_W(tag, "WIFI_UDP iperf report send failed, retry=%u errno=%d", retry + 1U, errno);
                }
                vTaskDelay(pdMS_TO_TICKS(SNF_NET_TEST_FIN_RETRY_GAP_MS));
            }
            LOG_I(tag,
                  "WIFI_UDP iperf report sent=%u/%u peer_port=%u sent_packets=%u received_packets=%u",
                  report_send_count,
                  SNF_NET_TEST_FIN_RETRY_COUNT,
                  lwip_ntohs(peer_address.sin_port),
                  receiver.sender_packets,
                  receiver.received_packets);
        }
        ret = 0;
    }

cleanup:
    if (socket_fd >= 0)
    {
        (void)lwip_close(socket_fd);
    }
    if (receiver.started)
    {
        if (end_tick == 0U)
        {
            end_tick = xTaskGetTickCount();
            snfNetTestStatsSample(&receiver.stats, end_tick, true);
        }
        snfNetTestLogUdpReceiverResult("WIFI_UDP", &receiver, end_tick);
    }
    if (wifi_ps_held)
    {
        snfNetTestWifiPsRelease();
    }

    return ret;
}

bool snfNetTestThreadLock(void)
{
    SnfNetTestConfig *net_test = &snf_net_test_config;
    bool locked = false;

    if (net_test->thread_mutex == NULL)
    {
        LOG_E(tag, "Thread mutex is not initialized");
    }
    else if (xSemaphoreTake(net_test->thread_mutex, portMAX_DELAY) == pdPASS)
    {
        locked = true;
    }
    else
    {
        LOG_E(tag, "Take Thread mutex failed");
    }

    return locked;
}

bool snfNetTestThreadTryLock(void)
{
    SnfNetTestConfig *net_test = &snf_net_test_config;
    bool locked = false;

    if ((net_test->thread_mutex != NULL) && (xSemaphoreTake(net_test->thread_mutex, 0U) == pdPASS))
    {
        locked = true;
    }

    return locked;
}

bool snfNetTestThreadUnlock(void)
{
    SnfNetTestConfig *net_test = &snf_net_test_config;
    bool unlocked = false;

    if (net_test->thread_mutex == NULL)
    {
        LOG_E(tag, "Thread mutex is not initialized");
    }
    else if (xSemaphoreGive(net_test->thread_mutex) == pdPASS)
    {
        unlocked = true;
    }
    else
    {
        LOG_E(tag, "Give Thread mutex failed");
    }

    return unlocked;
}

#if SNR_NET_TEST_THREAD_TEST_ENABLE
static int snfNetTestThreadSocketOpen(SnfNetTestThreadContext *context)
{
    otSockAddr local_address = {0};
    otDeviceRole role = OT_DEVICE_ROLE_DISABLED;
    otError error = OT_ERROR_FAILED;
    int ret = -1;

    if (g_otInst == NULL)
    {
        LOG_E(tag, "OpenThread instance is not ready");
    }
    else if (snfNetTestThreadLock())
    {
        role = otThreadGetDeviceRole(g_otInst);
        if ((role == OT_DEVICE_ROLE_DISABLED) || (role == OT_DEVICE_ROLE_DETACHED))
        {
            LOG_E(tag, "Thread device is not attached, role=%d", role);
        }
        else
        {
            error = otUdpOpen(g_otInst, &context->socket, snfNetTestThreadReceive, context);
            if (error == OT_ERROR_NONE)
            {
                context->socket_open = true;
                local_address.mPort = SNF_NET_TEST_THREAD_UDP_PORT;
                error = otUdpBind(g_otInst,
                                  &context->socket,
                                  &local_address,
                                  OT_NETIF_THREAD_INTERNAL);
            }
            if (error == OT_ERROR_NONE)
            {
                ret = 0;
            }
            else
            {
                LOG_E(tag, "Open or bind Thread UDP socket failed, error=%d", error);
                if (context->socket_open)
                {
                    (void)otUdpClose(g_otInst, &context->socket);
                    context->socket_open = false;
                }
            }
        }
        (void)snfNetTestThreadUnlock();
    }

    return ret;
}

static void snfNetTestThreadSocketClose(SnfNetTestThreadContext *context)
{
    if (context->socket_open && (g_otInst != NULL) && snfNetTestThreadLock())
    {
        (void)otUdpClose(g_otInst, &context->socket);
        context->socket_open = false;
        (void)snfNetTestThreadUnlock();
    }
}

static otError snfNetTestThreadSendBuffer(SnfNetTestThreadContext *context,
                                          const uint8_t *buffer,
                                          uint16_t length,
                                          const otMessageInfo *message_info)
{
    otMessage *message = NULL;
    otError error = OT_ERROR_FAILED;

    if ((g_otInst != NULL) && context->socket_open && snfNetTestThreadLock())
    {
        message = otUdpNewMessage(g_otInst, NULL);
        if (message != NULL)
        {
            error = otMessageAppend(message, buffer, length);
            if (error == OT_ERROR_NONE)
            {
                error = otUdpSend(g_otInst, &context->socket, message, message_info);
            }
            if (error != OT_ERROR_NONE)
            {
                otMessageFree(message);
            }
        }
        else
        {
            error = OT_ERROR_NO_BUFS;
        }
        (void)snfNetTestThreadUnlock();
    }

    return error;
}
#endif

static void snfNetTestThreadReceive(void *context, otMessage *message, const otMessageInfo *message_info)
{
    uint8_t buffer[SNF_NET_TEST_THREAD_UDP_PAYLOAD_SIZE];
    SnfNetTestThreadContext *thread_context = (SnfNetTestThreadContext *)context;
#if SNF_NET_TEST_THREAD_SEND_ENABLE
    SnfNetTestUdpRemoteReport remote_report = {0};
#endif
    uint16_t message_offset = otMessageGetOffset(message);
    uint16_t message_length = otMessageGetLength(message) - message_offset;
    uint16_t read_length = message_length;

    if (read_length > sizeof(buffer))
    {
        read_length = sizeof(buffer);
    }
    if ((read_length >= SNF_NET_TEST_IPERF_UDP_HEADER_SIZE) &&
        (otMessageRead(message, message_offset, buffer, read_length) == read_length) &&
        snfNetTestThreadLock())
    {
#if SNF_NET_TEST_THREAD_SEND_ENABLE
        (void)message_info;
        if (snfNetTestParseUdpReport(buffer, read_length, &remote_report))
        {
            thread_context->remote_report = remote_report;
        }
#else
        int packet_result = snfNetTestUdpReceiverProcess(&thread_context->receiver,
                                                         buffer,
                                                         read_length,
                                                         SNF_NET_TEST_THREAD_UDP_TARGET_BITRATE,
                                                         xTaskGetTickCount());
        if (packet_result > 0)
        {
            memset(&thread_context->peer_info, 0, sizeof(thread_context->peer_info));
            thread_context->peer_info.mSockAddr = message_info->mSockAddr;
            thread_context->peer_info.mPeerAddr = message_info->mPeerAddr;
            thread_context->peer_info.mSockPort = message_info->mSockPort;
            thread_context->peer_info.mPeerPort = message_info->mPeerPort;
            thread_context->peer_valid = true;
        }
#endif
        (void)snfNetTestThreadUnlock();
    }
}

#if SNR_NET_TEST_THREAD_TEST_ENABLE
static int snfNetTestThreadRunSender(void)
{
    uint8_t buffer[SNF_NET_TEST_THREAD_UDP_PAYLOAD_SIZE];
    SnfNetTestThreadContext *context = &snf_net_test_thread_context;
    SnfNetTestStats stats = {0};
    SnfNetTestUdpRemoteReport remote_report = {0};
    TickType_t now = 0U;
    TickType_t end_tick = 0U;
    TickType_t retry_start = 0U;
    uint64_t desired_packets = 0U;
    uint64_t scheduled_packets = 0U;
    uint64_t pacing_denominator =
        (uint64_t)SNF_NET_TEST_THREAD_UDP_PAYLOAD_SIZE * 8U * configTICK_RATE_HZ;
    uint32_t sent_packets = 0U;
    uint32_t send_errors = 0U;
    uint32_t burst_count = 0U;
    uint32_t retry = 0U;
    uint32_t target_kbps_scaled = snfNetTestBitrateToLogScale(SNF_NET_TEST_THREAD_UDP_TARGET_BITRATE, true);
    otError error = OT_ERROR_NONE;
    int ret = -1;
    bool stats_started = false;

    memset(context, 0, sizeof(*context));
    if (snfNetTestThreadSocketOpen(context) != 0)
    {
        goto cleanup;
    }
    if (otIp6AddressFromString(SNF_NET_TEST_THREAD_PEER_IPV6, &context->peer_info.mPeerAddr) != OT_ERROR_NONE)
    {
        LOG_E(tag, "Invalid Thread peer IPv6 address: %s", SNF_NET_TEST_THREAD_PEER_IPV6);
        goto cleanup;
    }
    context->peer_info.mPeerPort = SNF_NET_TEST_THREAD_UDP_PORT;
    context->peer_valid = true;

    now = xTaskGetTickCount();
    snfNetTestStatsInit(&stats, now);
    stats_started = true;
    LOG_I(tag,
          "THREAD_UDP sender started peer=[%s]:%u duration_s=%u payload_bytes=%u target_bitrate=%u.%02uKbps",
          SNF_NET_TEST_THREAD_PEER_IPV6,
          SNF_NET_TEST_THREAD_UDP_PORT,
          SNF_NET_TEST_DURATION_SECONDS,
          SNF_NET_TEST_THREAD_UDP_PAYLOAD_SIZE,
          target_kbps_scaled / SNF_NET_LOG_FRACTION_SCALE,
          target_kbps_scaled % SNF_NET_LOG_FRACTION_SCALE);

    while ((xTaskGetTickCount() - stats.start_tick) < snfNetTestDurationTicks())
    {
        now = xTaskGetTickCount();
        desired_packets =
            ((uint64_t)(now - stats.start_tick) * SNF_NET_TEST_THREAD_UDP_TARGET_BITRATE) / pacing_denominator;
        burst_count = 0U;
        while ((scheduled_packets < desired_packets) && (burst_count < SNF_NET_TEST_SEND_BURST_MAX))
        {
            snfNetTestBuildUdpData(buffer,
                                   sizeof(buffer),
                                   sent_packets + 1U);
            error = snfNetTestThreadSendBuffer(context, buffer, sizeof(buffer), &context->peer_info);
            scheduled_packets++;
            burst_count++;
            if (error == OT_ERROR_NONE)
            {
                sent_packets++;
                snfNetTestStatsRecord(&stats, sizeof(buffer));
            }
            else
            {
                send_errors++;
            }
        }

        now = xTaskGetTickCount();
        if (snfNetTestStatsPollProgress(&stats, now))
        {
            snfNetTestLogProgress("THREAD_UDP", &stats, now);
        }
        if (burst_count == 0U)
        {
            vTaskDelay(1U);
        }
        snfNetTestFeedWatchdog();
    }

    end_tick = xTaskGetTickCount();
    snfNetTestStatsSample(&stats, end_tick, true);
    snfNetTestBuildUdpFinish(buffer,
                             sizeof(buffer),
                             sent_packets);
    for (retry = 0U; (retry < SNF_NET_TEST_FIN_RETRY_COUNT) && !remote_report.received; retry++)
    {
        (void)snfNetTestThreadSendBuffer(context, buffer, sizeof(buffer), &context->peer_info);
        retry_start = xTaskGetTickCount();
        while ((xTaskGetTickCount() - retry_start) < pdMS_TO_TICKS(SNF_NET_TEST_FIN_RETRY_WAIT_MS))
        {
            if (snfNetTestThreadLock())
            {
                remote_report = context->remote_report;
                (void)snfNetTestThreadUnlock();
            }
            if (remote_report.received)
            {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(SNF_NET_TEST_FIN_RETRY_GAP_MS));
        }
    }
    ret = 0;

cleanup:
    snfNetTestThreadSocketClose(context);
    if (stats_started)
    {
        if (end_tick == 0U)
        {
            end_tick = xTaskGetTickCount();
            snfNetTestStatsSample(&stats, end_tick, true);
        }
        snfNetTestLogUdpSenderResult("THREAD_UDP",
                                     SNF_NET_TEST_THREAD_UDP_TARGET_BITRATE,
                                     sent_packets,
                                     send_errors,
                                     &stats,
                                     end_tick,
                                     &remote_report);
    }

    return ret;
}

static int snfNetTestThreadRunReceiver(void)
{
    uint8_t buffer[SNF_NET_TEST_THREAD_UDP_PAYLOAD_SIZE];
    SnfNetTestThreadContext *context = &snf_net_test_thread_context;
    SnfNetTestUdpReceiver receiver = {0};
    otMessageInfo peer_info = {0};
    TickType_t now = 0U;
    TickType_t end_tick = 0U;
    TickType_t receiver_limit = snfNetTestDurationTicks() + pdMS_TO_TICKS(SNF_NET_TEST_RECEIVER_GRACE_MS);
    uint32_t retry = 0U;
    uint32_t report_send_count = 0U;
    bool report_due = false;
    bool peer_valid = false;
    otError report_error = OT_ERROR_NONE;
    int ret = -1;

    memset(context, 0, sizeof(*context));
    if (snfNetTestThreadSocketOpen(context) != 0)
    {
        goto cleanup;
    }
    LOG_I(tag, "THREAD_UDP receiver waiting on port %u", SNF_NET_TEST_THREAD_UDP_PORT);

    while (true)
    {
        now = xTaskGetTickCount();
        report_due = false;
        if (snfNetTestThreadLock())
        {
            if (context->receiver.started)
            {
                report_due = snfNetTestStatsPollProgress(&context->receiver.stats, now);
            }
            receiver = context->receiver;
            peer_info = context->peer_info;
            peer_valid = context->peer_valid;
            (void)snfNetTestThreadUnlock();
        }

        if (report_due)
        {
            snfNetTestLogProgress("THREAD_UDP", &receiver.stats, now);
        }
        if (receiver.finish_received ||
            (receiver.started && ((now - receiver.stats.start_tick) >= receiver_limit)))
        {
            break;
        }
        snfNetTestFeedWatchdog();
        vTaskDelay(pdMS_TO_TICKS(20U));
    }

    end_tick = xTaskGetTickCount();
    if (snfNetTestThreadLock())
    {
        snfNetTestStatsSample(&context->receiver.stats, end_tick, true);
        receiver = context->receiver;
        peer_info = context->peer_info;
        peer_valid = context->peer_valid;
        (void)snfNetTestThreadUnlock();
    }
    if (peer_valid && receiver.finish_received)
    {
        snfNetTestBuildUdpReport(buffer, sizeof(buffer), &receiver, end_tick);
        for (retry = 0U; retry < SNF_NET_TEST_FIN_RETRY_COUNT; retry++)
        {
            report_error = snfNetTestThreadSendBuffer(context, buffer, sizeof(buffer), &peer_info);
            if (report_error == OT_ERROR_NONE)
            {
                report_send_count++;
            }
            else
            {
                LOG_W(tag, "THREAD_UDP iperf report send failed, retry=%u error=%d", retry + 1U, report_error);
            }
            vTaskDelay(pdMS_TO_TICKS(SNF_NET_TEST_FIN_RETRY_GAP_MS));
        }
        LOG_I(tag,
              "THREAD_UDP iperf report sent=%u/%u peer_port=%u sent_packets=%u received_packets=%u",
              report_send_count,
              SNF_NET_TEST_FIN_RETRY_COUNT,
              peer_info.mPeerPort,
              receiver.sender_packets,
              receiver.received_packets);
    }
    ret = 0;

cleanup:
    snfNetTestThreadSocketClose(context);
    if (receiver.started)
    {
        if (end_tick == 0U)
        {
            end_tick = xTaskGetTickCount();
        }
        snfNetTestLogUdpReceiverResult("THREAD_UDP", &receiver, end_tick);
    }

    return ret;
}
#endif

static void snfNetTestThreadTask(void *argument)
{
    SnfNetTestConfig *net_test = &snf_net_test_config;
    int ret = -1;

    (void)argument;
#if SNF_NET_TEST_THREAD_SEND_ENABLE
#if SNR_NET_TEST_THREAD_TEST_ENABLE
    ret = snfNetTestThreadRunSender();
#endif
#else
    ret = snfNetTestThreadRunReceiver();
#endif
    LOG_I(tag, "THREAD_UDP test task finished, ret=%d", ret);
    net_test->thread_task_handle = NULL;
    vTaskDelete(NULL);
}

static void snfNetTestWifiTcpTask(void *argument)
{
    SnfNetTestConfig *net_test = &snf_net_test_config;
    int ret = -1;

    (void)argument;
#if SNF_NET_TEST_WIFI_SEND_ENABLE
    ret = snfNetTestWifiTcpRunSender();
#else
    ret = snfNetTestWifiTcpRunReceiver();
#endif
    LOG_I(tag, "WIFI_TCP test task finished, ret=%d", ret);
    net_test->wifi_tcp_task_handle = NULL;
    vTaskDelete(NULL);
}

static void snfNetTestWifiUdpTask(void *argument)
{
    SnfNetTestConfig *net_test = &snf_net_test_config;
    int ret = -1;

    (void)argument;
#if SNF_NET_TEST_WIFI_SEND_ENABLE
    ret = snfNetTestWifiUdpRunSender();
#else
    ret = snfNetTestWifiUdpRunReceiver();
#endif
    LOG_I(tag, "WIFI_UDP test task finished, ret=%d", ret);
    net_test->wifi_udp_task_handle = NULL;
    vTaskDelete(NULL);
}

int snfNetTestThreadInit(void)
{
    SnfNetTestConfig *net_test = &snf_net_test_config;
    int ret = 0;

    if (snfNetTestInit() != 0)
    {
        ret = -1;
    }
    else if ((net_test->thread_task_handle == NULL) &&
             (xTaskCreate(snfNetTestThreadTask,
                          "snf_net_thread",
                          SNF_NET_TEST_TASK_STACK_SIZE,
                          NULL,
                          TASK_PRIORITY_NORMAL,
                          &net_test->thread_task_handle) != pdPASS))
    {
        LOG_E(tag, "Create Thread network test task failed");
        ret = -1;
    }
    else if (net_test->thread_task_handle != NULL)
    {
        LOG_I(tag, "Thread network test task is running");
    }

    return ret;
}

int snfNetTestWifiTcpInit(void)
{
    SnfNetTestConfig *net_test = &snf_net_test_config;
    int ret = 0;

    if ((net_test->wifi_tcp_task_handle == NULL) &&
        (xTaskCreate(snfNetTestWifiTcpTask,
                     "snf_net_wifi_tcp",
                     SNF_NET_TEST_TASK_STACK_SIZE,
                     NULL,
                     TASK_PRIORITY_NORMAL,
                     &net_test->wifi_tcp_task_handle) != pdPASS))
    {
        LOG_E(tag, "Create Wi-Fi TCP test task failed");
        ret = -1;
    }
    else if (net_test->wifi_tcp_task_handle != NULL)
    {
        LOG_I(tag, "Wi-Fi TCP test task is running");
    }

    return ret;
}

int snfNetTestWifiUdpInit(void)
{
    SnfNetTestConfig *net_test = &snf_net_test_config;
    int ret = 0;

    if ((net_test->wifi_udp_task_handle == NULL) &&
        (xTaskCreate(snfNetTestWifiUdpTask,
                     "snf_net_wifi_udp",
                     SNF_NET_TEST_TASK_STACK_SIZE,
                     NULL,
                     TASK_PRIORITY_NORMAL,
                     &net_test->wifi_udp_task_handle) != pdPASS))
    {
        LOG_E(tag, "Create Wi-Fi UDP test task failed");
        ret = -1;
    }
    else if (net_test->wifi_udp_task_handle != NULL)
    {
        LOG_I(tag, "Wi-Fi UDP test task is running");
    }

    return ret;
}

int snfNetTestInit(void)
{
    SnfNetTestConfig *net_test = &snf_net_test_config;
    int ret = 0;

    if (net_test->thread_mutex == NULL)
    {
        net_test->thread_mutex = xSemaphoreCreateMutex();
        if (net_test->thread_mutex == NULL)
        {
            LOG_E(tag, "Create Thread network test mutex failed");
            ret = -1;
        }
    }

    return ret;
}
