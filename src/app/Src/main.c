#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>

#include "platform_init.h"
#include "hal_audio.h"
#include "error.h"

#include <zephyr/logging/log.h>
#include "rt_thread.h"

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

#ifdef CONFIG_BLE_TEST
#include "hal_ble.h"

#define BLE_TEST_STACK_SIZE 2048
#define BLE_TEST_PRIO 3

static struct k_thread g_ble_test_thread;
RT_THREAD_STACK_DEFINE(g_ble_test_stack, BLE_TEST_STACK_SIZE);

static void ble_test_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int ret = hal_ble_init();
    if (ret != HAL_OK) {
        LOG_ERR("hal_ble_init failed: %d", ret);
        return;
    }

    ret = hal_ble_start();
    if (ret != HAL_OK) {
        LOG_ERR("hal_ble_start failed: %d", ret);
        return;
    }

    LOG_INF("BLE test thread started");

    while (1) {
        uint8_t buf[244];
        int r = hal_ble_recv(buf, sizeof(buf), 1000);
        if (r > 0) {
            LOG_INF("BLE RX %d bytes", r);
        }
    }
}
#endif

#ifdef CONFIG_MIC_TEST
#include "hal_mic.h"
#include "hal_ble.h"
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#define MIC_TEST_STACK_SIZE 2048
#define MIC_TEST_PRIO 4
#define MIC_UPLOAD_STACK_SIZE 2048
#define MIC_UPLOAD_PRIO 5

#define AUDIO_PKT_MAGIC0 0xA5
#define AUDIO_PKT_MAGIC1 0x5A
#define AUDIO_PAYLOAD_MAX 200
#define MIC_FRAME_Q_LEN 16
#define MIC_MTU_MIN 23

struct audio_pkt_hdr {
    uint8_t magic0;
    uint8_t magic1;
    uint16_t seq;
    uint8_t frag_idx;
    uint8_t frag_cnt;
    uint16_t payload_len;
} __packed;

static struct k_thread g_mic_test_thread;
RT_THREAD_STACK_DEFINE(g_mic_test_stack, MIC_TEST_STACK_SIZE);

static struct k_thread g_mic_upload_thread;
RT_THREAD_STACK_DEFINE(g_mic_upload_stack, MIC_UPLOAD_STACK_SIZE);

struct mic_frame_msg {
    void *buf;
    size_t len;
    uint16_t seq;
};

K_MSGQ_DEFINE(g_mic_frame_q, sizeof(struct mic_frame_msg), MIC_FRAME_Q_LEN, 4);

static int audio_send_frame(const uint8_t *pcm, size_t len, uint16_t seq)
{
    if (pcm == NULL || len == 0) {
        return HAL_EINVAL;
    }
    if (!hal_ble_is_ready()) {
        return HAL_EBUSY;
    }

    int mtu = hal_ble_get_mtu();
    size_t payload_max = AUDIO_PAYLOAD_MAX;
    if (mtu > 3) {
        size_t att_payload = (size_t)(mtu - 3);
        if (att_payload > sizeof(struct audio_pkt_hdr)) {
            size_t dyn_max = att_payload - sizeof(struct audio_pkt_hdr);
            if (dyn_max < payload_max) {
                payload_max = dyn_max;
            }
        } else {
            return HAL_EINVAL;
        }
    }

    uint16_t frame_seq = seq;
    uint8_t frag_cnt = (uint8_t)((len + payload_max - 1) / payload_max);
    size_t offset = 0;

    for (uint8_t frag = 0; frag < frag_cnt; frag++) {
        size_t remaining = len - offset;
        size_t chunk = (remaining > payload_max) ? payload_max : remaining;

        uint8_t pkt[sizeof(struct audio_pkt_hdr) + AUDIO_PAYLOAD_MAX];
        struct audio_pkt_hdr *hdr = (struct audio_pkt_hdr *)pkt;
        hdr->magic0 = AUDIO_PKT_MAGIC0;
        hdr->magic1 = AUDIO_PKT_MAGIC1;
        sys_put_le16(frame_seq, (uint8_t *)&hdr->seq);
        hdr->frag_idx = frag;
        hdr->frag_cnt = frag_cnt;
        sys_put_le16((uint16_t)chunk, (uint8_t *)&hdr->payload_len);

        memcpy(pkt + sizeof(struct audio_pkt_hdr), pcm + offset, chunk);

        int ret = hal_ble_send(pkt, sizeof(struct audio_pkt_hdr) + chunk, 0);
        if (ret != HAL_OK) {
            return ret;
        }

        offset += chunk;
        if (frag_cnt > 1) {
            k_usleep(200);
        }
    }

    return HAL_OK;
}

static void mic_capture_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int ret = hal_mic_init();
    if (ret != HAL_OK) {
        LOG_ERR("hal_mic_init failed: %d", ret);
        return;
    }
    ret = hal_mic_start();
    if (ret != HAL_OK) {
        LOG_ERR("hal_mic_start failed: %d", ret);
        return;
    }

    LOG_INF("MIC test thread started");

    while (1) {
        void *buf = NULL;
        size_t len = 0;
        int r = hal_mic_read_block(&buf, &len, 1000);
        if (r == HAL_OK && buf != NULL && len > 0) {
            static uint16_t seq = 0;
            struct mic_frame_msg msg = {
                .buf = buf,
                .len = len,
                .seq = seq++,
            };
            int q = k_msgq_put(&g_mic_frame_q, &msg, K_NO_WAIT);
            if (q != 0) {
                (void)hal_mic_release(buf);
            }
        }
    }
}

static void mic_upload_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    uint64_t last_log = k_uptime_get();
    uint32_t frames = 0;
    uint32_t frames_sent = 0;
    uint32_t frames_drop = 0;
    uint32_t pkts_sent = 0;
    uint32_t bytes_sent = 0;

    while (1) {
        struct mic_frame_msg msg;
        int q = k_msgq_get(&g_mic_frame_q, &msg, K_MSEC(1000));
        if (q != 0) {
            continue;
        }

        frames++;

        if (!hal_ble_is_ready() || hal_ble_get_mtu() < MIC_MTU_MIN) {
            frames_drop++;
            (void)hal_mic_release(msg.buf);
            goto log_stats;
        }

        int ret = audio_send_frame((const uint8_t *)msg.buf, msg.len, msg.seq);
        if (ret == HAL_OK) {
            frames_sent++;
            pkts_sent += (uint32_t)((msg.len + AUDIO_PAYLOAD_MAX - 1) / AUDIO_PAYLOAD_MAX);
            bytes_sent += (uint32_t)msg.len;
        } else {
            frames_drop++;
        }

        (void)hal_mic_release(msg.buf);

log_stats:
        {
            uint64_t now = k_uptime_get();
            if (now - last_log >= 1000) {
                LOG_INF("MIC stream: frames=%u sent=%u drop=%u pkts=%u bytes=%u ready=%d mtu=%d",
                        frames, frames_sent, frames_drop, pkts_sent, bytes_sent,
                        hal_ble_is_ready(), hal_ble_get_mtu());
                last_log = now;
            }
        }
    }
}
#endif

int main(void)
{
    int ret = platform_init();
    if (ret != HAL_OK) {
        printk("platform_init failed: %d\n", ret);
        return ret;
    }

    ret = hal_audio_init();
    if (ret != HAL_OK) {
        printk("hal_audio_init failed: %d\n", ret);
        return ret;
    }

#ifdef CONFIG_BLE_TEST
    (void)rt_thread_start(&g_ble_test_thread,
                          g_ble_test_stack,
                          K_THREAD_STACK_SIZEOF(g_ble_test_stack),
                          ble_test_entry,
                          NULL, NULL, NULL,
                          BLE_TEST_PRIO,
                          0,
                          "ble_test");
#endif

#ifdef CONFIG_MIC_TEST
    (void)rt_thread_start(&g_mic_test_thread,
                          g_mic_test_stack,
                          K_THREAD_STACK_SIZEOF(g_mic_test_stack),
                          mic_capture_entry,
                          NULL, NULL, NULL,
                          MIC_TEST_PRIO,
                          0,
                          "mic_cap");

    (void)rt_thread_start(&g_mic_upload_thread,
                          g_mic_upload_stack,
                          K_THREAD_STACK_SIZEOF(g_mic_upload_stack),
                          mic_upload_entry,
                          NULL, NULL, NULL,
                          MIC_UPLOAD_PRIO,
                          0,
                          "mic_up");
#endif

    return 0;
}
