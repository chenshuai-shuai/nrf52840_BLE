#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_bus.h"
#include "app_gps.h"
#include "error.h"
#include "hal_gps.h"
#include "rt_thread.h"
#include "system_state.h"

LOG_MODULE_REGISTER(app_gps, LOG_LEVEL_WRN);

#define GPS_APP_STACK_SIZE 2048
#define GPS_APP_PRIORITY   8
#define GPS_FIELD_MAX      24
#define GPS_LINE_TIMEOUT_MS 2000

typedef struct {
    uint8_t sats;
    uint8_t fix_quality;
} gps_gga_info_t;

static struct k_thread g_gps_thread;
RT_THREAD_STACK_DEFINE(g_gps_stack, GPS_APP_STACK_SIZE);

static int gps_split_fields(char *line, char *fields[], int max_fields)
{
    int n = 0;
    char *p = line;

    while (p != NULL && n < max_fields) {
        fields[n++] = p;
        char *comma = strchr(p, ',');
        if (comma == NULL) {
            break;
        }
        *comma = '\0';
        p = comma + 1;
    }

    return n;
}

static double gps_nmea_to_deg(const char *val, bool is_lon)
{
    if (val == NULL || val[0] == '\0') {
        return 0.0;
    }

    double raw = strtod(val, NULL);
    int deg = is_lon ? (int)(raw / 100.0) : (int)(raw / 100.0);
    double min = raw - (double)deg * 100.0;
    return (double)deg + (min / 60.0);
}

static bool gps_parse_gga(char *line, gps_gga_info_t *gga)
{
    char *fields[GPS_FIELD_MAX] = {0};
    int n = gps_split_fields(line, fields, GPS_FIELD_MAX);
    if (n < 8) {
        return false;
    }

    gga->fix_quality = (uint8_t)atoi(fields[6]);
    gga->sats = (uint8_t)atoi(fields[7]);
    return true;
}

static bool gps_parse_rmc(char *line, gps_state_t *out, const gps_gga_info_t *gga)
{
    char *fields[GPS_FIELD_MAX] = {0};
    int n = gps_split_fields(line, fields, GPS_FIELD_MAX);
    if (n < 8) {
        return false;
    }

    const char *status = fields[2];
    if (status == NULL || status[0] != 'A') {
        out->fix_valid = 0;
        return false;
    }

    double lat = gps_nmea_to_deg(fields[3], false);
    double lon = gps_nmea_to_deg(fields[5], true);
    const char lat_h = (fields[4] != NULL && fields[4][0] != '\0') ? fields[4][0] : 'N';
    const char lon_h = (fields[6] != NULL && fields[6][0] != '\0') ? fields[6][0] : 'E';

    if (lat_h == 'S') {
        lat = -lat;
    }
    if (lon_h == 'W') {
        lon = -lon;
    }

    float speed_knots = 0.0f;
    if (fields[7] != NULL && fields[7][0] != '\0') {
        speed_knots = strtof(fields[7], NULL);
    }

    out->lat_deg = lat;
    out->lon_deg = lon;
    out->speed_kmh = speed_knots * 1.852f;
    out->fix_valid = 1;
    out->sats = gga->sats;
    out->timestamp_ms = (uint32_t)k_uptime_get();
    out->valid = 1;
    return true;
}

static bool gps_is_sentence(const char *s, const char *tag)
{
    if (s == NULL || tag == NULL) {
        return false;
    }
    if (s[0] != '$') {
        return false;
    }

    size_t len = strlen(s);
    if (len < 6) {
        return false;
    }

    return (s[3] == tag[0] && s[4] == tag[1] && s[5] == tag[2]);
}

static void gps_app_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int ret = hal_gps_init();
    if (ret != HAL_OK) {
        LOG_ERR("gps app: hal_gps_init failed: %d", ret);
        return;
    }

    ret = hal_gps_start();
    if (ret != HAL_OK) {
        LOG_ERR("gps app: hal_gps_start failed: %d", ret);
        return;
    }

    LOG_INF("gps app: start");

    gps_gga_info_t gga = {0};
    uint32_t raw_cnt = 0;

    while (1) {
        hal_gps_packet_t pkt;
        memset(&pkt, 0, sizeof(pkt));

        ret = hal_gps_read(&pkt, sizeof(pkt), GPS_LINE_TIMEOUT_MS);
        if (ret != HAL_OK) {
            if (ret == HAL_EBUSY) {
                continue;
            }
            LOG_WRN("gps app: read failed: %d", ret);
            k_msleep(50);
            continue;
        }

        char line[HAL_GPS_PACKET_MAX_LEN];
        memcpy(line, pkt.sentence, sizeof(line));
        line[HAL_GPS_PACKET_MAX_LEN - 1] = '\0';

        for (int i = 0; i < HAL_GPS_PACKET_MAX_LEN; i++) {
            if (line[i] == '\r' || line[i] == '\n') {
                line[i] = '\0';
                break;
            }
        }

        if (line[0] == '\0') {
            continue;
        }

        raw_cnt++;
        if ((raw_cnt % 30U) == 0U) {
            LOG_INF("gps raw: %s", line);
        }

        if (gps_is_sentence(line, "GGA")) {
            (void)gps_parse_gga(line, &gga);
            continue;
        }

        if (gps_is_sentence(line, "RMC")) {
            gps_state_t st = {0};
            if (gps_parse_rmc(line, &st, &gga)) {
                system_state_set_gps(&st);

                app_event_t evt = {
                    .id = APP_EVT_GPS_FIX,
                    .timestamp_ms = st.timestamp_ms,
                    .data.gps = st,
                };
                (void)app_bus_publish(&evt);

                int32_t lat_i = (int32_t)(st.lat_deg * 1000000.0);
                int32_t lon_i = (int32_t)(st.lon_deg * 1000000.0);
                int32_t spd_i = (int32_t)(st.speed_kmh * 100.0f);
                LOG_INF("gps fix: lat=%d.%06d lon=%d.%06d speed=%d.%02dkm/h sats=%u",
                        (int)(lat_i / 1000000), (int)abs(lat_i % 1000000),
                        (int)(lon_i / 1000000), (int)abs(lon_i % 1000000),
                        (int)(spd_i / 100), (int)abs(spd_i % 100), st.sats);
            }
        }
    }
}

void app_gps_start(void)
{
    static bool started;
    if (started) {
        return;
    }

    int ret = rt_thread_start(&g_gps_thread,
                              g_gps_stack,
                              K_THREAD_STACK_SIZEOF(g_gps_stack),
                              gps_app_entry,
                              NULL, NULL, NULL,
                              GPS_APP_PRIORITY, 0,
                              "app_gps");
    if (ret != 0) {
        LOG_ERR("gps app: thread start failed: %d", ret);
        return;
    }

    started = true;
}

