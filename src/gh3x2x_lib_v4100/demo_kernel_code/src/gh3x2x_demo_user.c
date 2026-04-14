/**
 * @copyright (c) 2003 - 2022, Goodix Co., Ltd. All rights reserved.
 * 
 * @file    gh3x2x_demo_user.c
 * 
 * @brief   gh3x2x driver lib demo code that user defined
 * 
 * @author  
 * 
 */

/* includes */
#include <errno.h>
#include "stdint.h"
#include "string.h"
#include "gh3x2x_drv.h"
#include "gh3x2x_demo_config.h"
#include "gh3x2x_demo_inner.h"
#include "gh3x2x_demo.h"

#if (__DRIVER_LIB_MODE__ == __DRV_LIB_WITH_ALGO__)
#include "gh3x2x_demo_algo_call.h"
#endif

#if (__GH3X2X_MP_MODE__)
#include "gh3x2x_mp_common.h"
#endif

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include "spi_bus_arbiter.h"
#include "hal_imu.h"

// extern struct k_sem gh3x2x_int_sem;

#define SPI_CS_PIN   (12)
#define SPI_INT_PIN  (7)
#define SPI_RST_PIN  (6)

struct spi_buf_set tx_bufs, rx_bufs;
struct spi_buf txb, rxb;
struct spi_config spi_cfg_single;

struct gh3x2x_dev_info
{
    const struct device *spi_dev;
    const struct device *spi_cs_dev;
    const struct device *spi_int_dev;
    const struct device *spi_rst_dev;
};
static struct gh3x2x_dev_info gh3x2x_info;
static const struct device * const g_gh_spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
static const struct device * const g_gh_gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));

static int gh3x2x_pin_level(const struct device *dev, gpio_pin_t pin)
{
    if (dev == NULL || !device_is_ready(dev)) {
        return -ENODEV;
    }
    return gpio_pin_get(dev, pin);
}

void gh3x2x_port_dump_state(const char *tag)
{
    int cs = gh3x2x_pin_level(gh3x2x_info.spi_cs_dev, SPI_CS_PIN);
    int irq = gh3x2x_pin_level(gh3x2x_info.spi_int_dev, SPI_INT_PIN);
    int rst = gh3x2x_pin_level(gh3x2x_info.spi_rst_dev, SPI_RST_PIN);

    printk("gh3026 hw[%s]: cs=P0.%02u=%d int=P0.%02u=%d rst=P0.%02u=%d spi=%s freq=%u op=0x%x\n",
           (tag != NULL) ? tag : "?",
           (unsigned int)SPI_CS_PIN, cs,
           (unsigned int)SPI_INT_PIN, irq,
           (unsigned int)SPI_RST_PIN, rst,
           (gh3x2x_info.spi_dev != NULL) ? gh3x2x_info.spi_dev->name : "null",
           (unsigned int)spi_cfg_single.frequency,
           (unsigned int)spi_cfg_single.operation);
}

#if ( __GH3X2X_INTERFACE__ == __GH3X2X_INTERFACE_I2C__ )

/* i2c interface */
/**
 * @fn     void hal_gh3x2x_i2c_init(void)
 * 
 * @brief  hal i2c init for gh3x2x
 *
 * @attention   None
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */
void hal_gh3x2x_i2c_init(void)
{

    /* code implement by user */
    GOODIX_PLANFROM_I2C_INIT_ENTITY();

}

/**
 * @fn     uint8_t hal_gh3x2x_i2c_write(uint8_t device_id, const uint8_t write_buffer[], uint16_t length)
 * 
 * @brief  hal i2c write for gh3x2x
 *
 * @attention   device_id is 8bits, if platform i2c use 7bits addr, should use (device_id >> 1)
 *
 * @param[in]   device_id       device addr
 * @param[in]   write_buffer    write data buffer
 * @param[in]   length          write data len
 * @param[out]  None
 *
 * @return  status
 * @retval  #1      return successfully
 * @retval  #0      return error
 */
GU8 hal_gh3x2x_i2c_write(GU8 device_id, const GU8 write_buffer[], GU16 length)
{
    uint8_t ret = 1;

    /* code implement by user */

    GOODIX_PLANFROM_I2C_WRITE_ENTITY(device_id, write_buffer,length);
    return ret;
}

/**
 * @fn     uint8_t hal_gh3x2x_i2c_read(uint8_t device_id, const uint8_t write_buffer[], uint16_t write_length,
 *                            uint8_t read_buffer[], uint16_t read_length)
 * 
 * @brief  hal i2c read for gh3x2x
 *
 * @attention   device_id is 8bits, if platform i2c use 7bits addr, should use (device_id >> 1)
 *
 * @param[in]   device_id       device addr
 * @param[in]   write_buffer    write data buffer
 * @param[in]   write_length    write data len
 * @param[in]   read_length     read data len
 * @param[out]  read_buffer     pointer to read buffer
 *
 * @return  status
 * @retval  #1      return successfully
 * @retval  #0      return error
 */
GU8 hal_gh3x2x_i2c_read(GU8 device_id, const GU8 write_buffer[], GU16 write_length, GU8 read_buffer[], GU16 read_length)
{
    uint8_t ret = 1;

    /* code implement by user */

    GOODIX_PLANFROM_I2C_READ_ENTITY(device_id, write_buffer, write_length, read_buffer, read_length);
    return ret;
}

#else // __GH3X2X_INTERFACE__ == __GH3X2X_INTERFACE_SPI__

/* spi interface */
/**
 * @fn     void hal_gh3x2x_spi_init(void)
 * 
 * @brief  hal spi init for gh3x2x
 *
 * @attention   None
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */

void hal_gh3x2x_spi_init(void)
{
    /* init spi and cs pin */
    gh3x2x_info.spi_dev = g_gh_spi_dev;
    if(!device_is_ready(gh3x2x_info.spi_dev))
    {
        printk("Failed to init spi1\n");
        return;
    }
    spi_cfg_single.frequency = 1000000U;
    spi_cfg_single.operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB
		                     | SPI_WORD_SET(8);
    spi_cfg_single.slave = 0;

    gh3x2x_info.spi_cs_dev = g_gh_gpio_dev;
    if(!device_is_ready(gh3x2x_info.spi_cs_dev))
    {
        printk("Failed to init gpio0\n");
        return;
    }
    gpio_pin_configure(gh3x2x_info.spi_cs_dev, SPI_CS_PIN, GPIO_OUTPUT_HIGH);
    printk("gh3026 spi init: spi1 sck=P0.24 mosi=P0.13 miso=P0.20 cs=P0.%02u int=P0.%02u rst=P0.%02u freq=%uHz op=0x%x\n",
           (unsigned int)SPI_CS_PIN,
           (unsigned int)SPI_INT_PIN,
           (unsigned int)SPI_RST_PIN,
           (unsigned int)spi_cfg_single.frequency,
           (unsigned int)spi_cfg_single.operation);
    gh3x2x_port_dump_state("spi_init");
}

/**
 * @fn     GU8 hal_gh3x2x_spi_write(GU8 write_buffer[], GU16 length)
 * 
 * @brief  hal spi write for gh3x2x
 *
 * @attention   if __GH3X2X_SPI_TYPE__ == __GH3X2X_SPI_TYPE_SOFTWARE_CS__  , user need generate timming: write_buf[0](W) + write_buf[1](W) + ...
 * @attention   if __GH3X2X_SPI_TYPE__ == __GH3X2X_SPI_TYPE_HARDWARE_CS__  , user need generate timming: CS LOW  + write_buf[0](W) + write_buf[1](W) + ... + CS HIGH
 *
 * @param[in]   write_buffer    write data buffer
 * @param[in]   length          write data len
 * @param[out]  None
 *
 * @return  status
 * @retval  #1      return successfully
 * @retval  #0      return error
 */
GU8 hal_gh3x2x_spi_write(GU8 write_buffer[], GU16 length)
{
    int spi_ret = 0;
    if (spi_bus_lock(SPI_BUS_CLIENT_GH3X2X, K_MSEC(20)) != 0) {
        printk("gh3026 spi write lock timeout len=%u\n", (unsigned int)length);
        return 0;
    }
#if (__GH3X2X_SPI_TYPE__ == __GH3X2X_SPI_TYPE_HARDWARE_CS__)
    gpio_pin_set(gh3x2x_info.spi_cs_dev, SPI_CS_PIN,  0);
#endif
    txb.buf = write_buffer;
    txb.len = length;
    tx_bufs.buffers = (const struct spi_buf *)&txb;
    tx_bufs.count = 1U;
    spi_ret = spi_write(gh3x2x_info.spi_dev, &spi_cfg_single, &tx_bufs);
#if (__GH3X2X_SPI_TYPE__ == __GH3X2X_SPI_TYPE_HARDWARE_CS__)
    gpio_pin_set(gh3x2x_info.spi_cs_dev, SPI_CS_PIN,  1);
#endif
    (void)spi_bus_unlock(SPI_BUS_CLIENT_GH3X2X);
    if (spi_ret != 0) {
        printk("gh3026 spi write fail: ret=%d len=%u first=0x%02x\n",
               spi_ret,
               (unsigned int)length,
               (unsigned int)((length > 0U && write_buffer != NULL) ? write_buffer[0] : 0U));
    }
    return (spi_ret == 0);
}


#if (__GH3X2X_SPI_TYPE__ == __GH3X2X_SPI_TYPE_SOFTWARE_CS__) 
/**
 * @fn     GU8 hal_gh3x2x_spi_read(GU8 read_buffer[], GU16 length)
 * 
 * @brief  hal spi read for gh3x2x
 *
 * @attention   user need generate timming: read_buf[0](R) + write_buf[1](R) + ... 
 *
 * @param[in]   read_length     read data len
 * @param[out]  read_buffer     pointer to read buffer
 *
 * @return  status
 * @retval  #1      return successfully
 * @retval  #0      return error
 */
GU8 hal_gh3x2x_spi_read(GU8 read_buffer[], GU16 length)
{
    int spi_ret = 0;
    if (spi_bus_lock(SPI_BUS_CLIENT_GH3X2X, K_MSEC(20)) != 0) {
        printk("gh3026 spi read lock timeout len=%u\n", (unsigned int)length);
        return 0;
    }
    rxb.buf = read_buffer;
    rxb.len = length;
    rx_bufs.buffers = &rxb;
    rx_bufs.count = 1U;

    spi_ret = spi_read(gh3x2x_info.spi_dev, &spi_cfg_single, &rx_bufs);
    (void)spi_bus_unlock(SPI_BUS_CLIENT_GH3X2X);
    if (spi_ret != 0) {
        printk("gh3026 spi read fail: ret=%d len=%u\n",
               spi_ret,
               (unsigned int)length);
    }
    return (spi_ret == 0);
}
#elif (__GH3X2X_SPI_TYPE__ == __GH3X2X_SPI_TYPE_HARDWARE_CS__)
/**
 * @fn     GU8 hal_gh3x2x_spi_write_F1_and_read(GU8 read_buffer[], GU16 length)
 * 
 * @brief  hal spi write F1 and read for gh3x2x
 *
 * @attention    user need generate timming: CS LOW + F1(W) + read_buf[0](R) + read_buf[1](R) + ... + CS HIGH
 *
 * @param[in]   write_buf     write data
 * @param[in]   length     write data len
 *
 * @return  status
 * @retval  #1      return successfully
 * @retval  #0      return error
 */
GU8 hal_gh3x2x_spi_write_F1_and_read(GU8 read_buffer[], GU16 length)
{
    int spi_ret = 0;
    GU8 write_buffer[1] = {0xf1};
    if (spi_bus_lock(SPI_BUS_CLIENT_GH3X2X, K_MSEC(20)) != 0) {
        printk("gh3026 spi f1+read lock timeout len=%u\n", (unsigned int)length);
        return 0;
    }
    gpio_pin_set(gh3x2x_info.spi_cs_dev, SPI_CS_PIN,  0);

    txb.buf = write_buffer;
    txb.len = 1;
    tx_bufs.buffers = (const struct spi_buf *)&txb;
    tx_bufs.count = 1U;
    spi_ret = spi_write(gh3x2x_info.spi_dev, &spi_cfg_single, &tx_bufs);

    rxb.buf = read_buffer;
    rxb.len = length;
    rx_bufs.buffers = &rxb;
    rx_bufs.count = 1U;

    if (spi_ret == 0) {
        spi_ret = spi_read(gh3x2x_info.spi_dev, &spi_cfg_single, &rx_bufs);
    }

    gpio_pin_set(gh3x2x_info.spi_cs_dev, SPI_CS_PIN,  1);
    (void)spi_bus_unlock(SPI_BUS_CLIENT_GH3X2X);
    if (spi_ret != 0) {
        printk("gh3026 spi f1+read fail: ret=%d len=%u\n",
               spi_ret,
               (unsigned int)length);
    }
    return (spi_ret == 0);
}
#endif

/**
 * @fn     void hal_gh3x2x_spi_cs_ctrl(GU8 cs_pin_level)
 * 
 * @brief  hal spi cs pin ctrl for gh3x2x
 *
 * @attention   pin level set 1 [high level] or 0 [low level]
 *
 * @param[in]   cs_pin_level     spi cs pin level
 * @param[out]  None
 *
 * @return  None
 */
#if (__GH3X2X_SPI_TYPE__ == __GH3X2X_SPI_TYPE_SOFTWARE_CS__) 
void hal_gh3x2x_spi_cs_ctrl(GU8 cs_pin_level)
{

    GU8 ret = gpio_pin_set(gh3x2x_info.spi_cs_dev, SPI_CS_PIN,  cs_pin_level);
    if (ret < 0) {
        printk("Cannot write gpio");
    }

}
#endif

#endif

#if __SUPPORT_HARD_RESET_CONFIG__

/**
 * @fn     void hal_gh3x2x_int_init(void)
 * 
 * @brief  gh3x2x int init
 *
 * @attention   None
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */



void hal_gh3x2x_reset_pin_init(void)
{
    gh3x2x_info.spi_rst_dev = g_gh_gpio_dev;
    if(!device_is_ready(gh3x2x_info.spi_rst_dev))
    {
        printk("Failed to init gpio0\n");
        return;
    }
    gpio_pin_configure(gh3x2x_info.spi_rst_dev, SPI_RST_PIN, GPIO_OUTPUT_HIGH);
    gh3x2x_port_dump_state("rst_init");
}

/**
 * @fn     void hal_gh3x2x_reset_pin_ctrl(GU8 pin_level)
 * 
 * @brief  hal reset pin ctrl for gh3x2x
 *
 * @attention   pin level set 1 [high level] or 0 [low level]
 *
 * @param[in]   pin_level     reset pin level
 * @param[out]  None
 *
 * @return  None
 */

void hal_gh3x2x_reset_pin_ctrl(GU8 pin_level)
{
    static uint8_t s_reset_log_cnt;
    GU8 ret = gpio_pin_set(gh3x2x_info.spi_rst_dev, SPI_RST_PIN,  pin_level);
    if (ret < 0) {
        printk("Cannot write gpio");
    } else if (s_reset_log_cnt < 6U) {
        s_reset_log_cnt++;
        printk("gh3026 rst -> %u\n", (unsigned int)pin_level);
        gh3x2x_port_dump_state("rst_set");
    }
}

#endif

static struct gpio_callback gh3x2x_int_callback;
static void gh3x2x_int_isr(const struct device *port, struct gpio_callback *cb, uint32_t pin_msk)
{
    extern void hal_gh3x2x_int_handler_call_back(void);
    hal_gh3x2x_int_handler_call_back();
}
/**
 * @fn     void hal_gh3x2x_int_init(void)
 * 
 * @brief  gh3x2x int init
 *
 * @attention   None
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */
void hal_gh3x2x_int_init(void)
{
    gh3x2x_info.spi_int_dev = g_gh_gpio_dev;
    if(!device_is_ready(gh3x2x_info.spi_int_dev))
    {
        printk("Failed to init gpio0\n");
        return;
    }
    gpio_pin_configure(gh3x2x_info.spi_int_dev, SPI_INT_PIN, GPIO_INPUT);

    gpio_init_callback(&gh3x2x_int_callback, gh3x2x_int_isr, BIT(SPI_INT_PIN));

    gpio_add_callback(gh3x2x_info.spi_int_dev, &gh3x2x_int_callback);

    gpio_pin_interrupt_configure(gh3x2x_info.spi_int_dev, SPI_INT_PIN, GPIO_INT_EDGE_RISING);
    gh3x2x_port_dump_state("int_init");

}

#if (__NORMAL_INT_PROCESS_MODE__ == __INTERRUPT_PROCESS_MODE__ || __MIX_INT_PROCESS_MODE__ == __INTERRUPT_PROCESS_MODE__)
/**
 * @fn     void hal_gh3x2x_int_handler_call_back(void)
 * 
 * @brief  call back of gh3x2x int handler
 *
 * @attention   None
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */
void hal_gh3x2x_int_handler_call_back(void)
{
    extern void ppg_nrf_on_irq(void);
    if (Gh3x2xGetInterruptMode() == __NORMAL_INT_PROCESS_MODE__)
    {
        GH3X2X_ClearChipSleepFlag(0);
        g_uchGh3x2xIntCallBackIsCalled = 1;
        ppg_nrf_on_irq();
#if (__GH3X2X_MP_MODE__)
        GH3X2X_MP_SET_INT_FLAG();  //gh3x2x mp test must call it
#endif
        GOODIX_PLANFROM_INT_HANDLER_CALL_BACK_ENTITY();
    }
}
#endif

/**
 * @fn     void hal_gsensor_start_cache_data(void)
 * 
 * @brief  Start cache gsensor data for gh3x2x
 *
 * @attention   This function will be called when start gh3x2x sampling.
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */
void hal_gsensor_start_cache_data(void)
{
#if (__DRIVER_LIB_MODE__ == __DRV_LIB_WITH_ALGO__)
    GH3X2X_TimestampSyncAccInit();
#endif
    GOODIX_PLANFROM_INT_GS_START_CACHE_ENTITY();
}

/**
 * @fn     void hal_gsensor_stop_cache_data(void)
 * 
 * @brief  Stop cache gsensor data for gh3x2x
 *
 * @attention   This function will be called when stop gh3x2x sampling.
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */
void hal_gsensor_stop_cache_data(void)
{
    GOODIX_PLANFROM_INT_GS_STOP_CACHE_ENTITY();
}

void hal_cap_start_cache_data(void)
{
    GOODIX_PLANFROM_INT_CAP_START_CACHE_ENTITY();
}

void hal_cap_stop_cache_data(void)
{
    GOODIX_PLANFROM_INT_CAP_STOP_CACHE_ENTITY();
}

void hal_temp_start_cache_data(void)
{
    GOODIX_PLANFROM_INT_TEMP_START_CACHE_ENTITY();
}

void hal_temp_stop_cache_data(void)
{
    GOODIX_PLANFROM_INT_TEMP_STOP_CACHE_ENTITY();
}

void hal_gh3x2x_write_cap_to_flash(GS32 WearCap1,GS32 UnwearCap1,GS32 WearCap2,GS32 UnwearCap2)
{
    GOODIX_PLANFROM_WRITE_CAP_TO_FLASH_ENTITY();
}
void hal_gh3x2x_read_cap_from_flash(GS32* WearCap1,GS32* UnwearCap1,GS32* WearCap2,GS32* UnwearCap2)
{
    GOODIX_PLANFROM_READ_CAP_FROM_FLASH_ENTITY();
}

/**
 * @fn     void hal_gsensor_drv_get_fifo_data(STGsensorRawdata gsensor_buffer[], GU16 *gsensor_buffer_index)
 * 
 * @brief  get gsensor fifo data
 *
 * @attention   When read fifo data of GH3x2x, will call this function to get corresponding cached gsensor data.
 *
 * @param[in]   None
 * @param[out]  gsensor_buffer          pointer to gsensor data buffer
 * @param[out]  gsensor_buffer_index    pointer to number of gsensor data(every gsensor data include x,y,z axis data)
 *
 * @return  None
 */
void hal_gsensor_drv_get_fifo_data(STGsensorRawdata gsensor_buffer[], GU16 *gsensor_buffer_index)
{
/**************************** WARNNING  START***************************************************/
/*  (*gsensor_buffer_index) can not be allowed bigger than __GSENSOR_DATA_BUFFER_SIZE__  ****************/
/* Be care for copying data to gsensor_buffer, length of gsensor_buffer is __GSENSOR_DATA_BUFFER_SIZE__ *****/
/**************************** WARNNING END*****************************************************/
    GU16 count = 0U;

    if (gsensor_buffer == NULL || gsensor_buffer_index == NULL) {
        return;
    }

    while (count < (__GSENSOR_DATA_BUFFER_SIZE__)) {
        imu_sample_t imu = {0};
        uint64_t imu_ts_us = 0U;
        int iret = hal_imu_read_timed(&imu, sizeof(imu), &imu_ts_us, 0);
        if (iret != 0) {
            iret = hal_imu_read(&imu, sizeof(imu), 0);
            if (iret == 0U) {
                (void)hal_imu_get_latest_us(&imu, &imu_ts_us);
            }
        }
        if (iret != 0) {
            break;
        }

        gsensor_buffer[count].sXAxisVal = (GS16)(imu.accel_x / 4);
        gsensor_buffer[count].sYAxisVal = (GS16)(imu.accel_y / 4);
        gsensor_buffer[count].sZAxisVal = (GS16)(imu.accel_z / 4);
#if (__DRIVER_LIB_MODE__ == __DRV_LIB_WITH_ALGO__)
        GH3X2X_TimestampSyncFillAccSyncBuffer((GU32)(imu_ts_us / 1000U),
                                             gsensor_buffer[count].sXAxisVal,
                                             gsensor_buffer[count].sYAxisVal,
                                             gsensor_buffer[count].sZAxisVal);
#endif
        count++;
    }

    if (count == 0U) {
        imu_sample_t imu = {0};
        uint64_t imu_ts_us = 0U;
        if (hal_imu_get_latest_us(&imu, &imu_ts_us) == 0) {
            gsensor_buffer[0].sXAxisVal = (GS16)(imu.accel_x / 4);
            gsensor_buffer[0].sYAxisVal = (GS16)(imu.accel_y / 4);
            gsensor_buffer[0].sZAxisVal = (GS16)(imu.accel_z / 4);
#if (__DRIVER_LIB_MODE__ == __DRV_LIB_WITH_ALGO__)
            GH3X2X_TimestampSyncFillAccSyncBuffer((GU32)(imu_ts_us / 1000U),
                                                 gsensor_buffer[0].sXAxisVal,
                                                 gsensor_buffer[0].sYAxisVal,
                                                 gsensor_buffer[0].sZAxisVal);
#endif
            count = 1U;
        }
    }

    *gsensor_buffer_index = count;

    GOODIX_PLANFROM_INT_GET_GS_DATA_ENTITY();


/**************************** WARNNING: DO NOT REMOVE OR MODIFY THIS CODE   ---START***************************************************/
    if((*gsensor_buffer_index) > (__GSENSOR_DATA_BUFFER_SIZE__))
    {
        while(1);   // Fatal error !!!
    }
/**************************** WARNNING: DO NOT REMOVE OR MODIFY THIS CODE   ---END***************************************************/
}

void hal_cap_drv_get_fifo_data(STCapRawdata cap_data_buffer[], GU16 *cap_buffer_index)
{
    GOODIX_PLANFROM_INT_GET_CAP_DATA_ENTITY()  ;
    if((*cap_buffer_index) > (__CAP_DATA_BUFFER_SIZE__))
    {
        while(1);   // Fatal error !!!
    }
}

void hal_temp_drv_get_fifo_data(STTempRawdata temp_data_buffer[], GU16 *temp_buffer_index)
{
    GOODIX_PLANFROM_INT_GET_TEMP_DATA_ENTITY();
    if((*temp_buffer_index) > (__TEMP_DATA_BUFFER_SIZE__))
    {
        while(1);   // Fatal error !!!
    }
}

#if (__EXAMPLE_LOG_TYPE__)
/**
 * @fn     void GH3X2X_Log(char *log_string)
 * 
 * @brief  for debug version, log
 *
 * @attention   this function must define that use debug version lib
 *
 * @param[in]   log_string      pointer to log string
 * @param[out]  None
 *
 * @return  None
 */

// #if __EXAMPLE_LOG_TYPE__ == __EXAMPLE_LOG_METHOD_0__
void GH3X2X_Log(GCHAR *log_string)
{
    printk("%s\n", log_string);
}
// #endif

#if __EXAMPLE_LOG_TYPE__ == __EXAMPLE_LOG_METHOD_1__
void GH3X2X_RegisterPrintf(int (**pPrintfUser)(const char *format, ...))
{
    (*pPrintfUser) = printf;   //use printf in <stdio.h>  or use equivalent function in your platform
    GOODIX_PLANFROM_PRINTF_ENTITY();
}
#endif




#endif

/**
 * @fn     void Gh3x2x_BspDelayUs(GU16 usUsec)
 * 
 * @brief  Delay in us,user should register this function into driver lib
 *
 * @attention   None
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */

void Gh3x2x_BspDelayUs(GU16 usUsec)
{
    k_busy_wait(usUsec);
}

void GH3X2X_AdtFuncStartWithGsDetectHook(void)
{
    GOODIX_PLANFROM_START_WITH_CONFIRM_HOOK_ENTITY();
}

/**
 * @fn     void Gh3x2x_BspDelayMs(GU16 usMsec)
 * 
 * @brief  Delay in ms
 *
 * @attention   None
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */
void Gh3x2x_BspDelayMs(GU16 usMsec)
{
    k_busy_wait(usMsec * 1000);
}

#if (__FUNC_TYPE_SOFT_ADT_ENABLE__)
/**
 * @fn     void Gh3x2x_CreateAdtConfirmTimer(void)
 * 
 * @brief  Create a timer for adt confirm which will read gsensor data periodically
 *
 * @attention   Period of timer can be set by 
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */
#if (__USE_POLLING_TIMER_AS_ADT_TIMER__)&&\
    (__POLLING_INT_PROCESS_MODE__ == __INTERRUPT_PROCESS_MODE__ || __MIX_INT_PROCESS_MODE__ == __INTERRUPT_PROCESS_MODE__)
#else
void Gh3x2xCreateAdtConfirmTimer(void)
{
    GOODIX_PLANFROM_CREAT_ADT_CONFIRM_ENTITY();
}
#endif

/**
 * @fn     void Gh3x2x_StartAdtConfirmTimer(void)
 * 
 * @brief  Start time of adt confirm to get g sensor
 *
 * @attention   None        
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */
#if __FUNC_TYPE_SOFT_ADT_ENABLE__
#if (__USE_POLLING_TIMER_AS_ADT_TIMER__)&&\
    (__POLLING_INT_PROCESS_MODE__ == __INTERRUPT_PROCESS_MODE__ || __MIX_INT_PROCESS_MODE__ == __INTERRUPT_PROCESS_MODE__)
#else
void Gh3x2x_StartAdtConfirmTimer(void)
{
    GOODIX_PLANFROM_START_TIMER_ENTITY();
}
#endif
#endif

/**
 * @fn     void Gh3x2x_StopAdtConfirmTimer(void)
 * 
 * @brief  Stop time of adt confirm
 *
 * @attention   None        
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */
#if __FUNC_TYPE_SOFT_ADT_ENABLE__
#if (__USE_POLLING_TIMER_AS_ADT_TIMER__)&&\
    (__POLLING_INT_PROCESS_MODE__ == __INTERRUPT_PROCESS_MODE__ || __MIX_INT_PROCESS_MODE__ == __INTERRUPT_PROCESS_MODE__)
#else
void Gh3x2x_StopAdtConfirmTimer(void)
{
    GOODIX_PLANFROM_STOP_TIMER_ENTITY();
}
#endif
#endif
#endif




/**
 * @fn     void Gh3x2x_UserHandleCurrentInfo(void)
 * 
 * @brief  handle gh3x2x chip current information for user
 *
 * @attention   None        
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */
void Gh3x2x_UserHandleCurrentInfo(void)
{
    //read or write  slotx current

    //GH3X2X_GetSlotLedCurrent(0,0); //read slot 0  drv 0
    //GH3X2X_GetSlotLedCurrent(1,0); // read  slot 1  drv 0

    //GH3X2X_SlotLedCurrentConfig(0,0,50);  //set slot0 drv0 50 LSB
    //GH3X2X_SlotLedCurrentConfig(1,0,50);  //set slot1 drv0 50 LSB
}

#if (__SUPPORT_PROTOCOL_ANALYZE__)
/**
 * @fn     void Gh3x2x_HalSerialSendData(GU8* uchTxDataBuf, GU16 usBufLen)
 *
 * @brief  Serial send data
 *
 * @attention   None
 *
 * @param[in]   uchTxDataBuf        pointer to data buffer to be transmitted
 * @param[in]   usBufLen            data buffer length
 * @param[out]  None
 *
 * @return  None
 */
extern int bt_ghealth_send(struct bt_conn *conn, const uint8_t *data, uint16_t len);
void Gh3x2x_HalSerialSendData(GU8* uchTxDataBuf, GU16 usBufLen)
{
    bt_ghealth_send(NULL, uchTxDataBuf, usBufLen);
}


/**
 * @fn      void Gh3x2xSerialSendTimerInit(GU8 uchPeriodMs)
 *
 * @brief  Gh3x2xSerialSendTimerInit
 *
 * @attention   None
 *
 * @param[in]   uchPeriodMs    timer period (ms)
 * @param[out]  None
 *
 * @return  None
 */
void Gh3x2xSerialSendTimerInit(GU8 uchPeriodMs)
{
    k_timer_init()
}


/**
 * @fn     void Gh3x2xSerialSendTimerStop(void)
 *
 * @brief  Serial send timer stop
 *
 * @attention   None
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */
void Gh3x2xSerialSendTimerStop(void)
{
    GOODIX_PLANFROM_SERIAL_TIMER_STOP_ENTITY();
}



/**
 * @fn     void Gh3x2xSerialSendTimerStart(void)
 *
 * @brief  Serial send timer start
 *
 * @attention   None
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  None
 */
void Gh3x2xSerialSendTimerStart(void)
{
    GOODIX_PLANFROM_SERIAL_TIMER_START_ENTITY();
}

#endif





void* Gh3x2xMallocUser(GU32 unMemSize)
{
#if (__USER_DYNAMIC_DRV_BUF_EN__)
#ifdef GOODIX_DEMO_PLANFORM
    GOODIX_PLANFROM_MALLOC_USER_ENTITY();
#else
    //return malloc(unMemSize);
    return 0;
#endif
#else
    return 0;
#endif
}

void Gh3x2xFreeUser(void* MemAddr)
{
#if (__USER_DYNAMIC_DRV_BUF_EN__)
    GOODIX_PLANFROM_FREE_USER_ENTITY();
    //free(MemAddr);
#endif
}









#if ( __FUNC_TYPE_BP_ENABLE__ && (__GH3X2X_INTERFACE__ == __GH3X2X_INTERFACE_I2C__))
/**
 * @fn     GS8 gh3x2x_write_pressure_parameters(GS32 *buffer)
 * 
 * @brief  gh3x2x get pressure value
 *
 * @attention   None
 *
 * @param[out]   buffer     buffer[0] = rawdata_a0,
 *                          buffer[1] = rawdata_a1,
 *                          buffer[2] = pressure g value of rawdata_a1,
 *
 * @return  error code
 */
GS8 gh3x2x_write_pressure_parameters(GS32 *buffer)
{
    return GOODIX_PLANFROM_PRESSURE_PARAS_WRITE_ENTITY();
}

/**
 * @fn     GS8 gh3x2x_read_pressure_parameters(GS32 *buffer)
 * 
 * @brief  gh3x2x get pressure value
 *
 * @attention   None
 *
 * @param[out]   buffer     buffer[0] = area,
 *                          buffer[1] = rawdata_a0,
 *                          buffer[2] = rawdata_a1,
 *                          buffer[3] = pressure g value,
 *
 * @return  error code
 */
GS8 gh3x2x_read_pressure_parameters(GS32 *buffer)
{
    return GOODIX_PLANFROM_PRESSURE_PARAS_READ_ENTITY();
}
#endif





/********END OF FILE********* Copyright (c) 2003 - 2022, Goodix Co., Ltd. ********/

