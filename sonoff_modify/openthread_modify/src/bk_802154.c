// Copyright 2020-2025 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdbool.h>
#include <string.h>
#include "common/bk_err.h"
#include "components/system.h"
#include "lw_mac802154_interface.h"
#include "mac802154_adapter.h"

//#include "driver/aon_rtc.h"
#include "bk_802154.h"
#include "assert.h"
#include "bk_ieee802154_ack.h"

#include "driver/hal/hal_timer_types.h"
#include "driver/timer_types.h"
#include "os/os.h"
#include "os/mem.h"
#include "os/str.h"
#include "sys_driver.h"
#include "bk_rf_internal.h"

#define MAC802154_TAG "OT"
#define MAC802154_LOGI(...) BK_LOGI(MAC802154_TAG, ##__VA_ARGS__)
#define MAC802154_LOGW(...) BK_LOGW(MAC802154_TAG, ##__VA_ARGS__)
#define MAC802154_LOGE(...) BK_LOGE(MAC802154_TAG, ##__VA_ARGS__)
#define MAC802154_LOGD(...) BK_LOGD(MAC802154_TAG, ##__VA_ARGS__)

#define BK_SELF_TEST_ENABLE 0

#define BK_ED_DURATION                      8      //number of symbol duration

#define BK_FRAME_BUFFER_MAX_SIZE            8 //Must be 2^n

#define MSG_QUEUE_SIZE                      16

#define BK_MAC_MIN_BE                       3
#define BK_MAC_MAX_BE                       5
#define BK_MAC_MAX_CSMA_BACKOFFS            4

#define ACK_TIMER_ID                        3

beken_queue_t g_802154_msg_queue;

enum {
    TIMER_NO_PENDING = 0,
    TIMER_TX_PENDING = 1,
    TIMER_RX_PENDING = 2
};


static bk_802154_frame_t s_frame_buf[BK_FRAME_BUFFER_MAX_SIZE] = {0};

typedef struct {
    bk_802154_frame_t rx_frame[BK_FRAME_BUFFER_MAX_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
} rx_pkt_ringbuffer;

static bool s_rx_when_idle = false;
static uint8_t s_timer_pending = TIMER_NO_PENDING;
volatile bk_802154_state_t s_802154_state = BK_802154_STATE_IDLE;
static int8_t s_recent_rssi = 0;
static bool s_cca_channel_clear = false;
static bk_802154_frame_t s_ack_frame;
static uint8_t s_pending_tx_frame[128]; //Transmitting
static uint8_t s_delay_send = 0; //cached tx frame


static rx_pkt_ringbuffer g_rx_buffer;
static bk_802154_tx_err_t s_tx_err;

extern uint64_t bk_aon_rtc_get_us(void);
extern bk_err_t bk_timer_stop(timer_id_t timer_id);
extern bk_err_t bk_timer_delay_with_callback(timer_id_t timer_id, uint64_t time_us, timer_isr_t callback);

#define ATOMIC_STATE(s) \
    ((s)==BK_802154_STATE_RECEIVE_BUSY || \
     (s)==BK_802154_STATE_TRANSMIT || \
     (s)==BK_802154_STATE_TRANSMIT_CCA || \
     (s)==BK_802154_STATE_TRANSMIT_IMM_ACK || \
     (s)==BK_802154_STATE_TRANSMIT_ENH_ACK)

#if 0
static __attribute__((optimize("O0"))) bk_802154_frame_t* bk_ieee802154_get_rx_write_buffer()
{
    bk_802154_frame_t* buf;
    uint32_t next_head = (g_rx_buffer.head + 1) & (BK_FRAME_BUFFER_MAX_SIZE - 1);
    if (next_head == g_rx_buffer.tail)
    {
        bk_802154_log("rx buffer is full!\r\n");
        g_rx_buffer.head = next_head;
    }
    bk_802154_log("write buf idx:%d\r\n", g_rx_buffer.head);
    buf = &g_rx_buffer.rx_frame[g_rx_buffer.head];
    g_rx_buffer.head = next_head;
    return buf;
}

__attribute__((optimize("O0"))) bk_802154_frame_t* bk_ieee802154_get_rx_read_buffer()
{
    bk_802154_frame_t* buf;
    uint32_t next_tail = g_rx_buffer.tail;
    if (g_rx_buffer.head == next_tail)
    {
        bk_802154_log("rx buffer is empty!\r\n");
        return NULL;
    }

    bk_802154_log("read buf idx:%d\r\n", next_tail);
    buf = &g_rx_buffer.rx_frame[next_tail];
    g_rx_buffer.tail = (next_tail + 1) & (BK_FRAME_BUFFER_MAX_SIZE - 1);
    return buf;
}
#endif
bk_802154_frame_t* bk_ieee802154_get_ack_buffer()
{
    return &s_ack_frame;
}

uint8_t bk_ieee802154_check_delayed_send()
{
    return s_delay_send;
}

static void bk_802154_transmit_failed(bk_802154_tx_err_t err)
{
    bk_err_t ret;
    bk_802154_msg_t data_msg = {0};

    data_msg.msg_type = BK_802154_MSG_TX_FAIL;
    ret = rtos_push_to_queue(&g_802154_msg_queue, &data_msg, 0);
    if (BK_OK != ret){
        MAC802154_LOGE("[%s] notify failed\n", __func__);
    }
    s_tx_err = err;
}

static void bk_802154_transmit_done(bk_802154_frame_t *data_p)
{
    bk_err_t ret;
    bk_802154_msg_t data_msg = {0};

    if (data_p)
    {
        data_msg.msg_type = BK_802154_MSG_TX_DONE_WITH_ACK;
        data_msg.msg.frame = data_p;
    }
    else
    {
        data_msg.msg_type = BK_802154_MSG_TX_DONE_NO_ACK;
    }
    ret = rtos_push_to_queue(&g_802154_msg_queue, &data_msg, 0);
    if (BK_OK != ret)
    {
        MAC802154_LOGE("[%s] notify failed\n", __func__);
    }
#if 0
    if (with_ack)
    {
        ret = os_task_notify(BK_802154_MSG_TX_DONE_WITH_ACK);
        if (BK_OK != ret)
        {
            bk_802154_log("[%s] notify failed\n", __func__);
        }
    }
    else
    {
        ret = os_task_notify(BK_802154_MSG_TX_DONE_NO_ACK);
        if (BK_OK != ret)
        {
            bk_802154_log("[%s] notify failed\n", __func__);
        }
    }
#endif
}

static void bk_802154_energy_detected(int8_t result)
{
    bk_err_t ret;

    bk_802154_msg_t data_msg = {0};

    data_msg.msg_type = BK_802154_MSG_ED_DONE;
    ret = rtos_push_to_queue(&g_802154_msg_queue, &data_msg, 0);
    if (BK_OK != ret){
        MAC802154_LOGE("[%s] notify failed\n", __func__);
    }
#if 0
    ret = os_task_notify(BK_802154_MSG_ED_DONE);
    if (BK_OK != ret){
        bk_802154_log("[%s] notify failed\n", __func__);
    }
#endif
}

static void bk_802154_received_done(bk_802154_frame_t *data_p)
{
    bk_err_t ret;

    bk_802154_msg_t data_msg = {0};

    data_msg.msg_type = BK_802154_MSG_RX_DONE;
    data_msg.msg.frame = data_p;
    ret = rtos_push_to_queue(&g_802154_msg_queue, &data_msg, 0);
    if (BK_OK != ret){
        MAC802154_LOGE("[%s] notify failed\n", __func__);
    }
#if 0
    ret = os_task_notify(BK_802154_MSG_RX_DONE);
    if (BK_OK != ret){
        bk_802154_log("[%s] notify failed\n", __func__);
    }
#endif
}

static void bk_802154_receive_failed(bk_802154_rx_err_t error)
{
    bk_err_t ret;

    bk_802154_msg_t data_msg = {0};

    data_msg.msg_type = BK_802154_MSG_RX_FAIL;
    ret = rtos_push_to_queue(&g_802154_msg_queue, &data_msg, 0);
    if (BK_OK != ret){
        MAC802154_LOGE("[%s] notify failed\n", __func__);
    }
#if 0
    ret = os_task_notify(BK_802154_MSG_RX_FAIL);
    if (BK_OK != ret){
        bk_802154_log("[%s] notify failed\n", __func__);
    }
#endif
}

static int8_t find_active_buffer_index(void)
{
    uint8_t index = 0xFF;
    for (uint8_t i = 0; i < BK_FRAME_BUFFER_MAX_SIZE; i++) {
        if (!s_frame_buf[i].used){
            index = i;
            s_frame_buf[index].used = true;
            break;
        }
    }

    if (0xFF == index) {
        MAC802154_LOGE("No available rx buffer found\n");
        return -1;
    }

    return index;
}

static void init_frame_buffer(void)
{
    os_memset(&g_rx_buffer, 0, sizeof(rx_pkt_ringbuffer));
}

static void ieee802154_timer0_start(void)
{
    lw_mac802154_lw_timer0_start();
}

static void ieee802154_timer0_stop(void)
{
    lw_mac802154_lw_timer0_stop();
}

static void ieee802154_timer0_set_max_value(uint32_t value)
{
    lw_mac802154_lw_timer0_set_max_value(value);
}

static void isr_handle_delay_tx(void)
{
    MAC802154_LOGD("handle delay tx\n");
    s_timer_pending = TIMER_NO_PENDING;
    /* If PHY is in atomic state, cache OT request and return success */
    if (ATOMIC_STATE(s_802154_state))
    {
        s_delay_send = 1;
        MAC802154_LOGE("[%s]tx when invalid state(%d), send later",__func__, s_802154_state);
    }
    else
    {
        s_802154_state = BK_802154_STATE_TRANSMIT;
        s_delay_send = 0;
        lw_mac802154_lw_macl_tx_frame_pl((s_pending_tx_frame + 1), s_pending_tx_frame[0] - 2, 0);
    }
}

static void isr_handle_delay_rx(void)
{
    MAC802154_LOGD("handle delay rx\n");
    s_timer_pending = TIMER_NO_PENDING;
    lw_mac802154_lw_macl_rx_config_pl(LW_TRUE, 1);
}

static int8_t bk_802154_convert_lqi_to_rssi(uint8_t lqi_val)
{
    int8_t rssi_val;
    if (lqi_val == 0) {
        rssi_val = LW_MAC_RSSI_LOWER_LIMIT;
    } else if (lqi_val == 0xFF) {
        rssi_val = LW_MAC_RSSI_UPPER_LIMIT;
    } else {
        rssi_val = (lqi_val / LW_MAC_RSSI_TO_LQI_OFFSET_MULTIPLIER) - LW_MAC_RSSI_TO_LQI_OFFSET;
    }
    return rssi_val;
}

static void bk_timer_overflow_cb(timer_id_t id)
{
#if 0
    bk_printf("ack timeout...\n");
    if (s_802154_state == BK_802154_STATE_RECEIVE_ACK) {
       bk_printf("recevive ack time out...\n");
       bk_802154_transmit_failed(s_tx_frame, BK_802154_TX_ERR_NO_ACK);
       s_802154_state = BK_802154_STATE_IDLE;
    }
#endif
    if (s_timer_pending == TIMER_TX_PENDING) {
       isr_handle_delay_tx();
    }
    if (s_timer_pending == TIMER_RX_PENDING) {
       s_timer_pending = TIMER_NO_PENDING;
       isr_handle_delay_rx();
    }
    bk_timer_stop(ACK_TIMER_ID);
}

extern void bk_802154_transmit_done_with_ack_rapid(bk_802154_frame_t *data_p);
extern uint64_t otPlatTimeGet(void);

static API_RESULT macl_cb(uint8_t event_type, uint16_t event_result, void *data, uint16_t datalen)
{
    static uint8_t cca_nb = 0;
    static uint8_t cca_be = BK_MAC_MIN_BE;
    uint32_t cca_random_delay = 0;
    uint8_t *data_buf = (uint8_t *)data;
    static bk_802154_frame_t* rx_pkt_with_ack;
    static uint64_t rx_sfd_timestamp;

    switch (event_type)
    {
        case MACL_ISR_TX_COMPLETE:
        {
            if ((BK_802154_STATE_TRANSMIT_IMM_ACK == s_802154_state) ||
                (BK_802154_STATE_TRANSMIT_ENH_ACK == s_802154_state))
            {
                // event after sending imm-ack & enh-ack
                bk_802154_received_done(rx_pkt_with_ack);
            }
            else if ((BK_802154_STATE_TRANSMIT == s_802154_state) ||
                    (BK_802154_STATE_TRANSMIT_CCA == s_802154_state))
            {
                // event after sending packet without ack
                if ((datalen == 2) && (data_buf[0] == LW_MAC_ENUM_NO_ACK) && (data_buf[1] == 0))
                {
                    bk_802154_transmit_failed(BK_802154_TX_ERR_NO_ACK);
                }
                else
                {
                    //MAC802154_LOGE("frame tx done...\n");
                    bk_802154_transmit_done(NULL);
                }
            }
            s_802154_state = BK_802154_STATE_IDLE;
         }
        break;
        case MACL_ISR_RX_ACK:
        {
#if 0
            int8_t index = find_active_buffer_index();
            if (index < 0)
            {
                MAC802154_LOGE("No rx buffer\r\n");
            }
            bk_802154_frame_t* rx_ack = &s_frame_buf[index];
#endif

#if 1
            static bk_802154_frame_t s_rx_ack = {0};
            memset(&s_rx_ack, 0, sizeof(bk_802154_frame_t));
            bk_802154_frame_t *rx_ack = &s_rx_ack;
#endif

            rx_ack->frame[0] = datalen + 1;  //add 1 byte for crc, 1 byte is LQI which is already added.
            MAC802154_LOGD(">>>>received rx ack: %d, datalen:%d", data_buf[2], datalen);
            memcpy(&rx_ack->frame[1], data, datalen);
            rx_ack->pending = data_buf[0] & 0x10;
            rx_ack->lqi = data_buf[datalen-1];
            rx_ack->rssi = bk_802154_convert_lqi_to_rssi(data_buf[datalen-1]);
            //rx_ack->time = otPlatTimeGet() - datalen * 32 - 120;
            rx_ack->time = rx_sfd_timestamp;
            //bk_802154_transmit_done(&s_frame_buf[index]);
            bk_802154_transmit_done_with_ack_rapid(&s_rx_ack);
            s_802154_state = BK_802154_STATE_IDLE;
         }
         break;
        case MACL_ISR_RX_FRAME:
        {
            s_802154_state  = BK_802154_STATE_RECEIVE_BUSY;
            int8_t index = find_active_buffer_index();
            if (index < 0)
            {
                MAC802154_LOGE("No rx buffer\r\n");
                s_802154_state  = BK_802154_STATE_IDLE;
                break;
            }
            bk_802154_frame_t* rx_pkt = &s_frame_buf[index];
            rx_pkt->enh_ack = false;
            rx_pkt->frame[0] = datalen + 1;  //add 1 byte for crc, 1 byte is LQI which is already added.
            memcpy(&rx_pkt->frame[1], data, datalen);
            MAC802154_LOGD("received frame, sn:%d, datalen:%d",rx_pkt->frame[3],datalen);
            //rx_pkt->time = otPlatTimeGet() - datalen * 32 - 120;
            rx_pkt->time = rx_sfd_timestamp;
            rx_pkt->lqi = data_buf[datalen-1];
            rx_pkt->rssi = bk_802154_convert_lqi_to_rssi(data_buf[datalen-1]);
            s_recent_rssi = rx_pkt->rssi;
            MAC802154_LOGD(">>>>>>>>lqi:%d, rssi:%d\n", rx_pkt->lqi, rx_pkt->rssi);
            if (rx_pkt->frame[1] & 0x20) 
            {
                s_802154_state = BK_802154_STATE_TRANSMIT_IMM_ACK;
                rx_pkt->pending = ieee802154_ack_config_pending_bit(data);
                if (ieee802154_frame_get_version(data) < IEEE802154_FRAME_VERSION_2)
                {
                    MAC802154_LOGD("send Imm-ACK");
                    uint8_t * ack_data = bk_ieee802154_imm_ack_generator_create(data, rx_pkt->pending);
                    lw_mac802154_lw_macl_tx_frame_pl(ack_data, IEEE802154_IMM_ACK_LENGTH - 2, 0);
                }
                else
                {
                    MAC802154_LOGD("send Enh-ACK");
                    s_802154_state = BK_802154_STATE_TRANSMIT_ENH_ACK;
                    lw_mac802154_lw_macl_tx_frame_pl_ack_start_tx((uint8_t*)rx_pkt, rx_pkt->frame[0], 0);
                    extern uint8_t* bk_802154_send_enh_ack(bk_802154_frame_t *data_p);
                    uint8_t * enh_ack = bk_802154_send_enh_ack(rx_pkt);
                    lw_mac802154_lw_macl_tx_frame_pl_ack_payload(enh_ack + 1, enh_ack[0] - 2, 0);
                    rx_pkt->enh_ack = true;
                }
                rx_pkt_with_ack = rx_pkt;
                break;
            }
            else
            {
                bk_802154_received_done(&s_frame_buf[index]);
                s_802154_state = BK_802154_STATE_IDLE;
            }
        }
        break;
        case MACL_ISR_CH_ED:
        {
            MAC802154_LOGD("ed done...\n");
            s_recent_rssi = bk_802154_convert_lqi_to_rssi(*((uint8_t *)data));
            bk_802154_energy_detected(s_recent_rssi);
            s_802154_state = BK_802154_STATE_IDLE;
        }
        break;
        case MACL_ISR_CH_CCA:
        {
            uint8_t channel_state;
            MAC802154_LOGD("cca done...\n");
            if (s_802154_state == BK_802154_STATE_TRANSMIT_CCA)
            {
                channel_state = ((uint8_t *)data)[0];
                if ( LW_FALSE == channel_state)
                {
                    MAC802154_LOGD("channel is free\n");
                    cca_nb = 0;
                    cca_be = BK_MAC_MIN_BE;
                    lw_mac802154_lw_macl_tx_frame_pl((s_pending_tx_frame + 1), s_pending_tx_frame[0] - 2, 0);
                }
                else
                {
                    //bk_802154_transmit_failed(BK_802154_TX_ERR_CCA_BUSY);
                    //break;
                    MAC802154_LOGE("channel is busy\n");
#if 1
                    //TODO back-off is not implemented for now, to do if necessary.
                    s_cca_channel_clear = false;
                    cca_nb += 1;
                    cca_be += 1;
                    if (cca_be > BK_MAC_MAX_BE)
                    {
                        cca_be = BK_MAC_MAX_BE;
                    }
                    if (cca_nb > BK_MAC_MAX_CSMA_BACKOFFS)
                    {
                        MAC802154_LOGE("failed:exceed max number of CSMA retries...\n");
                        cca_nb = 0;
                        cca_be = BK_MAC_MIN_BE;
                        s_802154_state = BK_802154_STATE_IDLE;
                        bk_802154_transmit_failed(BK_802154_TX_ERR_CCA_BUSY);
                    }
                    else
                    {
                        cca_random_delay = lw_mac802154_lw_macl_generate_random_number_pl((1<<cca_be)-1);
                        lw_mac802154_lw_macl_start_hw_timer_pl(cca_random_delay * LW_MAC_A_UNIT_BACKOFF_PERIOD,
                                                  0x00,
                                                  0x01);
                    }
#endif
                }
            }
        }
        break;
        case MACL_ISR_HW_TIMEOUT:
        {
            MAC802154_LOGE("hw time out...%d\n",s_802154_state);
            if ((s_802154_state == BK_802154_STATE_TRANSMIT_CCA) && (!s_cca_channel_clear))
            {
                MAC802154_LOGD("cca backoff...\n");
                lw_mac802154_lw_macl_start_cca_pl (BK_ED_DURATION);
            }
#if 0
            if (s_timer_pending == TIMER_TX_PENDING)
            {
                s_timer_pending = TIMER_NO_PENDING;
                isr_handle_delay_tx();
            }
            if (s_timer_pending == TIMER_RX_PENDING)
            {
                s_timer_pending = TIMER_NO_PENDING;
                isr_handle_delay_rx();
            }
#endif
        }
        break;
        case MACL_ISR_RX_SFD:
        {

            rx_sfd_timestamp = otPlatTimeGet();
        }
        break;
        default:
            MAC802154_LOGE("undefined event...\n");
        break;
    }
    return 0;
}

bk_err_t bk_ieee802154_enable(void)
{
    init_frame_buffer();
    rtos_init_queue(&g_802154_msg_queue, "802154 msg queue", sizeof(bk_802154_msg_t), MSG_QUEUE_SIZE);

    sys_drv_thread_rf_ctrl(1);
    rf_module_vote_ctrl(1, RF_BY_THREAD_BIT);

    /* MACL platform init */
    lw_mac802154_lw_macl_init_pl();
    lw_mac802154_set_register_thread_intr_en(0x09FF);

     //by default, it's false because Thread needs to send 802154-2015 Enh-ACK by software.
    lw_mac802154_lw_macl_set_auto_tx_ack(0);

    /* Register ISR */
    lw_mac802154_lw_macl_register_isr_pl(macl_cb);

    s_802154_state = BK_802154_STATE_IDLE;
    /* sonoff modify start */
    /* 通知共存逻辑 Thread 射频已就绪，允许 Wi-Fi 使用射频时退出 Thread 模式。 */
#if CONFIG_WIFI_THREAD_COEX_EN
    vOpenthreadSetStartStatus(true);
#endif
    /* sonoff modify end */
    return BK_OK;
}

bk_err_t bk_ieee802154_disable(void)
{
    mac802154_mac_deinit();
    s_802154_state = BK_802154_STATE_DISABLE;
    /* sonoff modify start */
    /* Thread 停止后，Wi-Fi 释放射频时不再切回 Thread 模式。 */
#if CONFIG_WIFI_THREAD_COEX_EN
    vOpenthreadSetStartStatus(false);
#endif
    /* sonoff modify end */
    return BK_OK;
}

uint8_t bk_ieee802154_channel_get(void)
{
    uint8_t value = 0;
    lw_mac802154_lw_mac_get_cur_channel_pl(&value);
    value = value/5 + 10;
    return value;
}

bk_err_t bk_ieee802154_channel_set(uint8_t channel)
{
    if (channel == bk_ieee802154_channel_get())
    {
        return BK_OK;
    }
    if ((channel < 11) || (channel > 26))
    {
        MAC802154_LOGE("[Error]%s channel%d out of range\r\n",__func__,channel);
        return BK_ERR_PARAM;
    }
    lw_mac802154_lw_mac_set_channel_pl(channel*5-50);
    return BK_OK;
}

int8_t bk_ieee802154_get_txpower(void)
{
    return BK_OK; //TODO
}

bk_err_t bk_ieee802154_set_txpower(int8_t power)
{
    return BK_OK; //TODO
}

bool bk_ieee802154_get_promiscuous(void)
{
    uint16_t val = 0;
    val = lw_mac802154_le_read_THRAD_CTRL_CONFIG();
    val = val & 0x0080;

    return (val == 0x0080 ? true : false);
}

bk_err_t bk_ieee802154_set_promiscuous(bool enable)
{
    uint16_t val;
    val = lw_mac802154_le_read_THRAD_CTRL_CONFIG();
    if (enable)
    {
        /* Set 7th bit position */
        val = (val | (0x0080));
    }
    else
    {
        /* Reset 7th bit position */
        val = (val & 0xFF7F);
    }

    lw_mac802154_le_write_THREAD_CTRL_CONFIG(val);
    return BK_OK;
}

bk_err_t bk_ieee802154_receive(void)
{
    if (ATOMIC_STATE(s_802154_state))
    {
        return BK_OK;
    }
    int i = 100;
    s_802154_state = BK_802154_STATE_RECEIVE;
    lw_mac802154_le_write_command_rx_stop();
    while (i > 0)
    {
        i--;
    }
    lw_mac802154_lw_macl_rx_config_pl(LW_TRUE, 1);
    return BK_OK;
}

bk_err_t bk_ieee802154_transmit(const uint8_t *frame, bool cca)
{
    /* If PHY is in atomic state, cache OT request and return success */
    if (ATOMIC_STATE(s_802154_state))
    {
        s_delay_send = 1;
        MAC802154_LOGE("[%s]tx when invalid state(%d), send later",__func__, s_802154_state);
        return BK_OK;
    }
    s_delay_send = 0;
    //cache the transmitting frame, in case failure
    memcpy(s_pending_tx_frame, frame, frame[0] + 1);
    if (cca)
    {
        MAC802154_LOGD("cca mode\n");
        s_802154_state = BK_802154_STATE_TRANSMIT_CCA;
        lw_mac802154_lw_macl_start_cca_pl(BK_ED_DURATION);
    }
    else
    {
        s_802154_state = BK_802154_STATE_TRANSMIT;
        lw_mac802154_lw_macl_tx_frame_pl((uint8_t *)frame + 1, frame[0]-2, 1);
    }
    return BK_OK;
}

bk_err_t bk_ieee802154_receive_at(uint32_t time)
{
    s_802154_state = BK_802154_STATE_RECEIVE;
    uint64_t current_time;
    current_time = bk_aon_rtc_get_us();

    s_timer_pending = TIMER_RX_PENDING;
    bk_timer_stop(ACK_TIMER_ID);
    bk_timer_delay_with_callback(ACK_TIMER_ID,(time > current_time) ? (time - current_time) : 0, bk_timer_overflow_cb);
#if 0
    ieee802154_timer0_stop();
    ieee802154_timer0_set_max_value((time > current_time) ? (time - current_time) : 0);
    ieee802154_timer0_start();
#endif
    return BK_OK;
}

bk_err_t bk_ieee802154_transmit_at(const uint8_t *frame, bool cca, uint32_t time)
{
    memcpy(s_pending_tx_frame, frame, frame[0] + 1);

    uint64_t current_time;
    //uint32_t fire_time;
    current_time = otPlatTimeGet();
    if (cca) {
        //when the time is over
        //need to wait cca to trigger tx
        MAC802154_LOGD("cca mode\n");
        s_802154_state = BK_802154_STATE_TRANSMIT_CCA;
        lw_mac802154_lw_macl_start_cca_pl(BK_ED_DURATION);
    } else {
        //when the time is over
        //set flag
        s_timer_pending = TIMER_TX_PENDING;
        bk_timer_stop(ACK_TIMER_ID);
#if 0
        fire_time = (time > current_time) ? (time - current_time) : 0;
        if (0 == fire_time)
            MAC802154_LOGE("!\n");
#endif
        bk_timer_delay_with_callback(ACK_TIMER_ID, (time > current_time) ? (time - current_time) : 0, bk_timer_overflow_cb);
#if 0
        ieee802154_timer0_stop();
        ieee802154_timer0_set_max_value((time > current_time) ? (time - current_time) : 0);
        ieee802154_timer0_start();
#endif
    }

    return BK_OK;
}

uint16_t bk_ieee802154_get_panid(void)
{
    return lw_mac802154_get_mac_pan_id();
}

bk_err_t bk_ieee802154_set_panid(uint16_t panid)
{
    lw_mac802154_set_mac_pan_id(panid);
    return BK_OK;
}

uint16_t bk_ieee802154_get_short_address(void)
{
    return lw_mac802154_get_short_address();
}

bk_err_t bk_ieee802154_set_short_address(uint16_t short_address)
{
    lw_mac802154_set_short_address(short_address);
    return BK_OK;
}

bk_err_t bk_ieee802154_get_extended_address(uint8_t *ext_addr)
{
    uint16_t temp = 0;
    temp = lw_mac802154_get_mac_extnd_addr0();
    ext_addr[0] = temp & 0x00FF;
    ext_addr[1] = temp >> 8;
    temp = lw_mac802154_get_mac_extnd_addr1();
    ext_addr[2] = temp & 0x00FF;
    ext_addr[3] = temp >> 8;
    temp = lw_mac802154_get_mac_extnd_addr2();
    ext_addr[4] = temp & 0x00FF;
    ext_addr[5] = temp >> 8;
    temp = lw_mac802154_get_mac_extnd_addr3();
    ext_addr[6] = temp & 0x00FF;
    ext_addr[7] = temp >> 8;

    return BK_OK;
}

bk_err_t bk_ieee802154_set_extended_address(const uint8_t *ext_addr)
{
    lw_mac802154_set_mac_extnd_addr0( (ext_addr[0]) | (ext_addr[1] << 8));
    lw_mac802154_set_mac_extnd_addr1( (ext_addr[2]) | (ext_addr[3] << 8));
    lw_mac802154_set_mac_extnd_addr2( (ext_addr[4]) | (ext_addr[5] << 8));
    lw_mac802154_set_mac_extnd_addr3( (ext_addr[6]) | (ext_addr[7] << 8));

    return BK_OK;
}

bk_ieee802154_pending_mode_t bk_ieee802154_get_pending_mode(void)
{
    return ieee802154_get_pending_mode();
}

bk_err_t bk_ieee802154_set_pending_mode(bool pending_mode)
{
    ieee802154_set_pending_mode(pending_mode);
#if 0
    uint16_t value = le_read_register(THREAD_CTRL_CONFIG);
    if(!pending_mode){
        le_write_register(THREAD_CTRL_CONFIG, value | (1 << 10));
    }else{
        le_write_register(THREAD_CTRL_CONFIG, value & (~(1 << 10)));
    }
#endif
    return BK_OK;
}

bk_err_t bk_ieee802154_add_pending_addr(const uint8_t *addr, bool is_short)
{
    return ieee802154_add_pending_addr(addr, is_short);;
}

bk_err_t bk_ieee802154_clear_pending_addr(const uint8_t *addr, bool is_short)
{
    return ieee802154_clear_pending_addr(addr, is_short);;
}

bk_err_t bk_ieee802154_reset_pending_table(bool is_short)
{
    ieee802154_reset_pending_table(is_short);
    return BK_OK;
}

int8_t bk_ieee802154_get_cca_threshold(void)
{
    uint16_t value = lw_mac802154_get_cca_threshold();
    return (int8_t) (value & 0xFF);
}

bk_err_t bk_ieee802154_set_cca_threshold(int8_t cca_threshold)
{
    lw_mac802154_set_cca_threshold( (uint16_t)cca_threshold);
    return BK_OK;
}

bk_err_t bk_ieee802154_set_rx_when_idle(bool enable)
{
    s_rx_when_idle = enable;
    if (enable) {
        lw_mac802154_lw_macl_rx_config_pl_ext(LW_TRUE);
    } else {
        lw_mac802154_lw_macl_rx_config_pl_ext(LW_FALSE);
    }
    return BK_OK;
}

bool bk_ieee802154_get_rx_when_idle(void)
{
    return s_rx_when_idle;
}

bk_err_t bk_ieee802154_energy_detect(uint32_t duration)
{
    lw_mac802154_lw_macl_start_ed_pl(duration, 0);
    s_802154_state = BK_802154_STATE_ED;
    return BK_OK;
}

bk_802154_state_t bk_ieee802154_state_get(void)
{
    return s_802154_state;
}

void bk_ieee802154_energy_detect_done(int8_t power)
{}

int8_t bk_ieee802154_get_recent_rssi(void)
{
    return s_recent_rssi;
}

uint8_t bk_ieee802154_get_recent_lqi(void)
{
    return lw_mac802154_lw_mac_convert_rssi_to_lqi(bk_ieee802154_get_recent_rssi());
}

bk_802154_rx_err_t bk_ieee802154_get_tx_err()
{
    return s_tx_err;
}

/* sonoff modify start */
/* 将 CLI 占位实现设为弱符号，允许 SDK 的 CLI 初始化实现生效。 */
__attribute__((weak)) int bk_cli_init(void)
{
    os_printf("openthread not use default cli\n");
    return 0;
}
/* sonoff modify end */

#if 0
void bk_ieee802154_transmit_security_config(uint8_t *frame, uint8_t *key, uint8_t *addr)
{
}
#endif

#if BK_SELF_TEST_ENABLE
//self test
//
//as test code
static void lp_init(void)
{
    // BK7236_TRX_REG.REG0x13->bits.lpfouttsten = 1;
    // BK7236_TRX_REG.REG0x13->bits.entst = 1;
    volatile uint32_t *trx = (volatile uint32_t*)(0x4980c24c);
    *trx |= ((1 <<0 ) | (1 << 4));

    //config p21/p22 9 function
    uint32_t temp = *(volatile uint32_t*)(0x44010000 + 0x32 * 4);
    temp &= ~(0xF << 24 | 0xF << 20);
    temp |= ((9 << 24) | (9 << 20));
    *(volatile uint32_t*)(0x44010000 + 0x32 * 4) = temp;
}

uint8_t mac802154_mac_init(void)
{
    mac802154_mac_enable();

    lp_init();

#ifdef LW_USE_eOSAL
    /* Initialize OSAL */
    EM_timer_init();
    timer_em_init();
#endif /* BT_USE_eOSAL */

    LW_init();

#ifdef LW_SUPPORT_GET_VERSION_INFO
    LW_VERSION_NUMBER  version;
    LW_get_version_number (&version);
    printf (
            "IEEE 802.15.4 Stack Version %03d:%03d:%03d\r\n",
            version.major,version.minor,version.subminor);
#endif /* LW_SUPPORT_GET_VERSION_INFO */

    return LW_TRUE;
}

#define THREAD_TX_LENGTH1           9
#define THREAD_TX_LENGTH2           10
#define THREAD_TX_LENGTH3           11
#define THREAD_ACK_LENGTH           5
#define TX_FRAME_CONTROL_LSB        0x63
#define TX_FRAME_CONTROL_MSB        0x23
#define TX_SEQUENCE_NUMBER          0x12

uint8_t tx_data[THREAD_TX_LENGTH1+1] = {THREAD_TX_LENGTH1,TX_FRAME_CONTROL_LSB,TX_FRAME_CONTROL_MSB,TX_SEQUENCE_NUMBER,0x45,0x14,0xAA,0XBB};



void cli_mac802154(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (strcmp(argv[1], "tx") == 0) {

        //THREAD_CHANNEL REG hold freq offset value.example THREAD_CHANNEL = 0x5 is 2405Mhz
        uint8_t channel = 0x05;
        if (argc > 2) {
            channel = os_strtoul(argv[2], NULL, 10);
        }

        MAC802154_LOGD("tx channel %d\r\n",channel);
        bk_ieee802154_channel_set(channel);
#if 0
        sys_int_isr_register(lw_mac_hw_isr_handler,NULL);
        lw_macl_register_isr_pl(macl_isr);
#endif
        bk_ieee802154_transmit(tx_data, 0);
    } else if (strcmp(argv[1], "rx") == 0) {
        uint8_t channel = 0x05;
        uint8_t rx_mode = 0;
        static uint8_t rx_mode_save = 0;

        if (argc > 2) {
            channel = os_strtoul(argv[2], NULL, 10);
        }

        if (argc > 3) {
            rx_mode = (os_strtoul(argv[3], NULL, 10) > 0) ? 1 : 0;
        }

        MAC802154_LOGD("rx channel %d rx_mode %d\r\n",channel,rx_mode);
#if 0
        sys_int_isr_register(lw_mac_hw_isr_handler,NULL);
        lw_macl_register_isr_pl(macl_isr);
#endif

        if (rx_mode_save == 1) {
            le_write_register(COMMAND_REGISTER, CONT_RX_STOP);
        } else if (rx_mode_save == 2) {
            le_write_register(COMMAND_REGISTER, RX_STOP);
        }

        lw_macl_set_channel(channel);

        thread_hw_filter_disable();

        os_delay_milliseconds(10);

        if (rx_mode) {
            thread_continuous_rx_start();
            rx_mode_save = 1;
        } else {
            thread_rx_start();
            rx_mode_save = 2;
        }
    } else if (strcmp(argv[1], "ed") == 0) {
        uint8_t channel = 5;
        if (argc > 2) {
            channel = os_strtoul(argv[2], NULL, 10);
        }
        lw_macl_set_channel(channel);
        le_write_register(COMMAND_REGISTER, ED_START);
    } else if (strcmp(argv[1], "tx_at") == 0) {
        MAC802154_LOGD("test tramsit at specific time\n");
        uint8_t channel = 0x05;
        uint8_t time = 20;
        if (argc > 2) {
            channel = os_strtoul(argv[2], NULL, 10);
        }

        if (argc > 3) {
            time = os_strtoul(argv[3], NULL, 10);
        }
        MAC802154_LOGD("tx channel %d\r\n",channel);
        bk_ieee802154_channel_set(channel);
        bk_ieee802154_transmit_at(tx_data, 0, time);
    } else if (strcmp(argv[1], "set_get_ch") == 0) {
        MAC802154_LOGD("test set and get channel\n");
        uint8_t channel_set = 6;
        bk_ieee802154_channel_set(channel_set);
        uint8_t channel_get = bk_ieee802154_channel_get();
        if (channel_get == channel_set){
            MAC802154_LOGD(" test pass \n");
        }
    } else if (strcmp(argv[1], "set_get_ch") == 0) {
        MAC802154_LOGD("test set and get cca threshold\n");
        int8_t cca_threshold_set = -85;
        bk_ieee802154_set_cca_threshold(cca_threshold_set);
        int8_t cca_threshold_get = bk_ieee802154_get_cca_threshold();
        if(cca_threshold_get == cca_threshold_set){
            MAC802154_LOGD(" test pass \n");
        }else {
            MAC802154_LOGD(" test failed, get threshold %d\n", cca_threshold_get);
        }
    } else if (strcmp(argv[1], "set_get_panid") == 0) {
        MAC802154_LOGD("test set and get panid\n");
        uint16_t panid_set = 0x7;
        bk_ieee802154_set_panid(panid_set);
        uint16_t panid_get = bk_ieee802154_get_panid();
        if(panid_get == panid_set){
            MAC802154_LOGD(" test pass \n");
        }
    } else if (strcmp(argv[1], "set_get_short_addr") == 0) {
        MAC802154_LOGD("test set and get short addr\n");
        uint16_t short_addr_set = 0x1234;
        bk_ieee802154_set_short_address(short_addr_set);
        uint16_t short_addr_get = 0;
        short_addr_get = bk_ieee802154_get_short_address();
        if(short_addr_get == short_addr_set){
            MAC802154_LOGD(" test pass \n");
        }
    } else if (strcmp(argv[1], "set_get_extend_addr") == 0) {
        MAC802154_LOGD("test set and get extend_addr\n");
        const uint8_t extend_addr_set[8] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
        bk_ieee802154_set_extended_address(extend_addr_set);
        uint8_t extend_addr_get[8] = { 0 };
        bk_ieee802154_get_extended_address(extend_addr_get);
        if(0 == memcmp(extend_addr_get, extend_addr_set, 8)){
            MAC802154_LOGD(" test pass \n");
        }
    } else if (strcmp(argv[1], "set_get_promiscuous") == 0) {
        MAC802154_LOGD("test set and get promiscuous\n");
        bool promiscuous_set = true;
        bk_ieee802154_set_promiscuous(promiscuous_set);
        bool promiscuous_get = bk_ieee802154_get_promiscuous();
        if ( promiscuous_get == promiscuous_set){
            MAC802154_LOGD(" test pass \n");
        }
    } else if (strcmp(argv[1], "set_get_txpower") == 0) {
        MAC802154_LOGD("test set and get txpower\n");
        int8_t txpower_set = 0;
        bk_ieee802154_set_txpower(txpower_set);
        int8_t txpower_get = bk_ieee802154_get_txpower();
        if ( txpower_get == txpower_set ){
            MAC802154_LOGD(" test pass \n");
        }
        //} else if (strcmp(argv[1], "") == 0) {
} else {
    printf("cmd not support!!!\r\n");
}
}
#endif
