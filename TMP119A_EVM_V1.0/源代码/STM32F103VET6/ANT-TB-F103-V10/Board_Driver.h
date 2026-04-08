#ifndef  __BOARD_DRIVER_H__
#define  __BOARD_DRIVER_H__

#include "stm32f10x.h"
#include "stm32f10x_bkp.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_pwr.h"
/*****************************参数定义*********************************/
//1.JTAG模式设置定义

typedef enum {
	JTAG_SWD_ALL_ENABLE = 0x00,					//JTAG与SWD全使能
	JTAG_DISABLE_SWD_ENABLE,					//SWD被使能
	JTAG_SWD_ALL_DISABLE						//JTAG与SWD全禁用
}JTAG_Def;

//2.时钟参数

#define SYSTEM_CLOCK_FREQ  	72					//系统时钟频率(MHz)

typedef enum {
	HSE_ON = 0x00010000,						//外部高速时钟使能
	HSI_ON = 0x00000001,						//内部高速时钟使能
	HSE_RDY= 0x00020000,						//外部高速时钟就绪
	HSI_RDY= 0x00000002							//内部高速时钟就绪
}HSX_Def;

//3.位带操作的基本概念(STM32F103的位带区域有两个)
//SRAM 位带区域：0x20000000 - 0x200FFFFF
//外设位带区域 ：0x40000000 - 0x400FFFFF
//每个位带区域都有一个对应的位带别名区域，通过位带别名区域可以访问单个位

#define BITBAND(addr, bitnum) ((addr & 0xF0000000) + 0x2000000 + ((addr & 0xFFFFF) << 5) + (bitnum << 2)) 
#define MEM_ADDR(addr)  *((volatile unsigned long  *)(addr)) 		//把一个地址转换成一个指针
#define BIT_ADDR(addr, bitnum)   MEM_ADDR(BITBAND(addr, bitnum)) 	//把位带别名区地址转换成指针

//3.1.GPIO_ODR/IDR 寄存器地址映射 

#define GPIOA_ODR_Addr    (GPIOA_BASE + 12) 	//0x4001080C   
#define GPIOB_ODR_Addr    (GPIOB_BASE + 12) 	//0x40010C0C   
#define GPIOC_ODR_Addr    (GPIOC_BASE + 12) 	//0x4001100C   
#define GPIOD_ODR_Addr    (GPIOD_BASE + 12) 	//0x4001140C   
#define GPIOE_ODR_Addr    (GPIOE_BASE + 12) 	//0x4001180C   
#define GPIOF_ODR_Addr    (GPIOF_BASE + 12) 	//0x40011A0C      
#define GPIOG_ODR_Addr    (GPIOG_BASE + 12) 	//0x40011E0C      
  
#define GPIOA_IDR_Addr    (GPIOA_BASE + 8)  	//0x40010808   
#define GPIOB_IDR_Addr    (GPIOB_BASE + 8)  	//0x40010C08   
#define GPIOC_IDR_Addr    (GPIOC_BASE + 8)  	//0x40011008   
#define GPIOD_IDR_Addr    (GPIOD_BASE + 8)  	//0x40011408   
#define GPIOE_IDR_Addr    (GPIOE_BASE + 8)  	//0x40011808   
#define GPIOF_IDR_Addr    (GPIOF_BASE + 8)  	//0x40011A08   
#define GPIOG_IDR_Addr    (GPIOG_BASE + 8)  	//0x40011E08 

//3.2.单独操作 GPIO的某一个IO口，n(0,1,2...16),n表示具体是哪一个IO口

#define PAout(n)   BIT_ADDR(GPIOA_ODR_Addr,n) 	//输出   
#define PAin(n)    BIT_ADDR(GPIOA_IDR_Addr,n)  	//输入   
  
#define PBout(n)   BIT_ADDR(GPIOB_ODR_Addr,n)  	//输出   
#define PBin(n)    BIT_ADDR(GPIOB_IDR_Addr,n)  	//输入   
  
#define PCout(n)   BIT_ADDR(GPIOC_ODR_Addr,n)  	//输出   
#define PCin(n)    BIT_ADDR(GPIOC_IDR_Addr,n)  	//输入   
  
#define PDout(n)   BIT_ADDR(GPIOD_ODR_Addr,n)  	//输出   
#define PDin(n)    BIT_ADDR(GPIOD_IDR_Addr,n)  	//输入   
  
#define PEout(n)   BIT_ADDR(GPIOE_ODR_Addr,n)  	//输出   
#define PEin(n)    BIT_ADDR(GPIOE_IDR_Addr,n)  	//输入  
  
#define PFout(n)   BIT_ADDR(GPIOF_ODR_Addr,n)  	//输出   
#define PFin(n)    BIT_ADDR(GPIOF_IDR_Addr,n)  	//输入  
  
#define PGout(n)   BIT_ADDR(GPIOG_ODR_Addr,n)  	//输出   
#define PGin(n)    BIT_ADDR(GPIOG_IDR_Addr,n)  	//输入  

/*****************************接口函数*********************************/

void F10x_JTAG_Config(JTAG_Def mode);			//JTAG模式设置
void F10x_PC13_GPIO_Config(void);				//PC13设置为GPIO模式
void F10x_PC14_PC15_GPIO_Config(void);			//PC14/15设置为GPIO模式
void F10x_Software_Reset(void);					//单片机软复位
void F10x_Clock_Initl(HSX_Def HSX,uint8_t pll_value);	//单片机时钟初始化函数
void F10x_SysTick_Initl(void);					//Systick延时函数初始化
void delay_us(uint32_t nus);					//us延时
void delay_ms(uint16_t nms);					//ms延时

void F10x_GPIO_Port_Initl(GPIO_TypeDef* GPIOx, 
				uint16_t GPIO_Pin, 
				GPIOSpeed_TypeDef GPIO_Speed,
				GPIOMode_TypeDef GPIO_Mode,
				BitAction io_Logic) ; 			//GPIO初始化函数

void F10x_EXIT_Channel_Set(uint32_t nChannel,FunctionalState EXIT_EN);	//指定通道中断关闭或启动(屏蔽或开放)

#endif






