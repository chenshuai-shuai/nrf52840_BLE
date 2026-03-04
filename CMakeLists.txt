cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(nrf52840_BLE)

set(APP_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/Src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/Src/app_lifecycle.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/Src/app_rtc.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/Src/app_state.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/Src/system_state.c
)
if(CONFIG_PPG_SPI_PROBE)
  list(APPEND APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/Src/app_ppg_hr.c
  )
endif()

if(CONFIG_IMU_TEST)
  list(APPEND APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/Src/app_imu_test.c
  )
endif()
if(CONFIG_PM_TEST)
  list(APPEND APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/Src/app_pm_test.c
  )
endif()
if(CONFIG_PM_SERVICE)
  list(APPEND APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/Src/pm_service.c
  )
endif()

set(COMMON_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common/Src
)

set(HAL_SOURCES)
if(CONFIG_HAL_AUDIO)
  list(APPEND HAL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/Src/hal_audio.c
  )
endif()

if(CONFIG_HAL_PPG)
  list(APPEND HAL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/Src/hal_ppg.c
  )
endif()
if(CONFIG_HAL_IMU)
  list(APPEND HAL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/Src/hal_imu.c
  )
endif()
if(CONFIG_HAL_FLASH)
  list(APPEND HAL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/Src/hal_flash.c
  )
endif()
if(CONFIG_HAL_GPS)
  list(APPEND HAL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/Src/hal_gps.c
  )
endif()
if(CONFIG_HAL_BLE)
  list(APPEND HAL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/Src/hal_ble.c
  )
endif()
if(CONFIG_HAL_MIC)
  list(APPEND HAL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/Src/hal_mic.c
  )
endif()
if(CONFIG_HAL_SPK)
  list(APPEND HAL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/Src/hal_spk.c
  )
endif()
if(CONFIG_HAL_PM)
  list(APPEND HAL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/Src/hal_pm.c
  )
endif()

set(DRIVER_SOURCES)
if(CONFIG_DRIVER_AUDIO_NRF)
  list(APPEND DRIVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver/Src/audio_nrf.c
  )
endif()

if(CONFIG_DRIVER_PPG_NRF)
  list(APPEND DRIVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver/Src/ppg_nrf.c
  )
endif()
if(CONFIG_DRIVER_IMU_NRF)
  list(APPEND DRIVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver/Src/imu_nrf.c
  )
endif()
if(CONFIG_DRIVER_FLASH_NRF)
  list(APPEND DRIVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver/Src/flash_nrf.c
  )
endif()
if(CONFIG_DRIVER_GPS_NRF)
  list(APPEND DRIVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver/Src/gps_nrf.c
  )
endif()
if(CONFIG_DRIVER_BLE_NRF)
  list(APPEND DRIVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver/Src/ble_nrf.c
  )
endif()
if(CONFIG_DRIVER_MIC_NRF)
  list(APPEND DRIVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver/Src/mic_nrf.c
  )
endif()
if(CONFIG_DRIVER_SPK_NRF)
  list(APPEND DRIVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver/Src/spk_nrf.c
  )
endif()
if(CONFIG_DRIVER_PM_NRF)
  list(APPEND DRIVER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver/Src/pm_nrf.c
  )
endif()

set(PLATFORM_SOURCES)
if(CONFIG_PLATFORM_REGISTRY)
  list(APPEND PLATFORM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/Src/platform_registry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/Src/platform_caps.c
  )
endif()
if(CONFIG_PLATFORM_NRF52840)
  list(APPEND PLATFORM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/Src/platform_init.c
  )
endif()

list(APPEND PLATFORM_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/Src/boot_tone.c
)

# App target
file(GLOB_RECURSE COMMON_C_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common/Src/*.c
)

target_sources(app PRIVATE
  ${APP_SOURCES}
  ${HAL_SOURCES}
  ${DRIVER_SOURCES}
  ${PLATFORM_SOURCES}
  ${COMMON_C_SOURCES}
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/Inc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/driver/Inc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/Inc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/Inc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common/Inc
)

if(CONFIG_DRIVER_PPG_NRF)
  file(GLOB GH3X2X_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gh3x2x_lib_v4100/demo_kernel_code/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gh3x2x_lib_v4100/algo_params/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gh3x2x_lib_v4100/demo_algo_code/goodix_algo_call/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gh3x2x_lib_v4100/demo_algo_code/goodix_algo_application/src/*.c
  )
  target_sources(app PRIVATE ${GH3X2X_SOURCES})

  target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gh3x2x_lib_v4100/drv_lib
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gh3x2x_lib_v4100/demo_kernel_code/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gh3x2x_lib_v4100/algo_params
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gh3x2x_lib_v4100/demo_algo_code/goodix_algo_call/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gh3x2x_lib_v4100/demo_algo_code/goodix_algo_application/inc
  )

  target_link_libraries(app PRIVATE
    -L${CMAKE_CURRENT_SOURCE_DIR}/src/gh3x2x_lib_v4100/drv_lib/gcc_M4_softfp/_build/gcc_softfp
    -L${CMAKE_CURRENT_SOURCE_DIR}/src/gh3x2x_lib_v4100/algo_lib/M4_gcc_softfp
    -Wl,--start-group
    libgh3x2x_drv_cortexM4l_band_gcc_softfp_agc.a
    libgh3x2x_drv_cortexM4l_band_gcc_softfp_ecg.a
    libgh3x2x_drv_cortexM4l_band_gcc_softfp_kernel.a
    libgh3x2x_drv_cortexM4l_band_gcc_softfp_zip.a
    libcortexM4_eabi-gcc_softfp_bt.a
    libcortexM4_eabi-gcc_softfp_ecg.a
    libcortexM4_eabi-gcc_softfp_hr.a
    libcortexM4_eabi-gcc_softfp_hrv.a
    libcortexM4_eabi-gcc_softfp_nadt.a
    libcortexM4_eabi-gcc_softfp_spo2.a
    libcortexM4_eabi-gcc_softfp_common.a
    -Wl,--end-group
  )
endif()
