# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gh3x2x_demo_based_on_nsc)

FILE(GLOB app_sources 
  src/*.c 
  src/service/*.c 
  src/gh3x2x_lib_v4100/*.c
  src/gh3x2x_lib_v4100/demo_kernel_code/src/*.c
  src/gh3x2x_lib_v4100/algo_params/*.c
  src/gh3x2x_lib_v4100/demo_algo_code/goodix_algo_call/src/*.c
  src/gh3x2x_lib_v4100/demo_algo_code/goodix_algo_application/src/*.c
  )
target_link_directories(
    app PRIVATE
    
  )
target_sources(app PRIVATE ${app_sources})

include_directories(
  src/gh3x2x_lib_v4100/demo_algo_code/goodix_algo_application/inc
  src/gh3x2x_lib_v4100/demo_algo_code/goodix_algo_call/inc
  src/gh3x2x_lib_v4100/algo_params
  src/gh3x2x_lib_v4100/demo_kernel_code/inc
  src/gh3x2x_lib_v4100/drv_lib
  src/service
)

zephyr_library_include_directories(
  ${ZEPHYR_BASE}/samples/bluetooth
  )

target_link_libraries(app PRIVATE
  -L../src/gh3x2x_lib_v4100/drv_lib/gcc_M4_softfp/_build/gcc_softfp
  -L../src/gh3x2x_lib_v4100/algo_lib/M4_gcc_softfp

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

  libm.a
  libc.a
  libnosys.a
  libgcc.a

  -Wl,--end-group

)

