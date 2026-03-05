#ifndef SPI_BUS_ARBITER_H
#define SPI_BUS_ARBITER_H

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPI_BUS_CLIENT_GH3X2X = 0,
    SPI_BUS_CLIENT_IMU = 1,
    SPI_BUS_CLIENT_OTHER = 2,
} spi_bus_client_t;

int spi_bus_arbiter_init(void);
int spi_bus_lock(spi_bus_client_t client, k_timeout_t timeout);
int spi_bus_unlock(spi_bus_client_t client);

#ifdef __cplusplus
}
#endif

#endif /* SPI_BUS_ARBITER_H */
