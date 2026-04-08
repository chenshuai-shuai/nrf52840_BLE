/**********************************************************************	 
* 公司名称 : 东莞市上古时代电子技术有限公司
* 模块名称 ：电路板驱动
* 系统主频 : 72 MHz
* 创建人   : eysmcu
* 修改人   : eysmcu 
* 创建日期 : 2025年1月10日
* 店铺地址 : http://mindesigner.taobao.com
* 淘宝ID号 : eysmcu
* 重要声明 ：本例程为东莞市上古时代电子技术有限公司原创，本程序只供学习
使用，未经作者许可，不得用于其它任何用途，转载清注明出处！！！

						 Copyright(C) 上古时代
***********************************************************************/

#include "Board_Driver.h"

/*****************************参数定义*********************************/
//1.延时函数参数

static uint16_t fac_us=0;				//us延时倍乘数			   
static uint16_t fac_ms=0;				//ms延时倍乘数

/**********************************************************************
* 名称 : F10x_JTAG_Config(JTAG_Def mode)
* 功能 : JTAG模式设置,用于设置JTAG的模式
* 输入 : mode -- 设置的模式
* 输出 : 无
* 说明 : 当JTAG相关的端口需要用作GPIO时，需要先调用该函数设置模式，再初
始化GPIO，否则初始化会失败，端口无法调用！！！！
***********************************************************************/ 		  
void F10x_JTAG_Config(JTAG_Def mode)
{
	uint32_t reg_value = 0x0;

	reg_value = mode;
	reg_value <<= 25;
	RCC->APB2ENR |= 1 << 0;     		//开启辅助时钟	   
	AFIO->MAPR &= 0xF8FFFFFF; 			//清除MAPR的[26:24]
	AFIO->MAPR |= reg_value;       		//设置jtag模式
} 
/**********************************************************************
* 名称 : F10x_PC13_GPIO_Config(void)
* 功能 : PC13设置为GPIO模式
* 输入 : 无
* 输出 : 无
* 说明 : PC13作为输出时速度只能工作在2MHz模式下！！！
***********************************************************************/ 
void F10x_PC13_GPIO_Config(void)
{
	BKP_TamperPinCmd(DISABLE);			//PC13设置为GPIO必须禁用TAMPER功能	
}
/**********************************************************************
* 名称 : F10x_PC14_PC15_GPIO_Config(void)
* 功能 : PC14/15设置为GPIO模式
* 输入 : 无
* 输出 : 无
* 说明 : PC14/PC15作为输出时速度只能工作在2MHz模式下！！！
***********************************************************************/ 
void F10x_PC14_PC15_GPIO_Config(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR 
						| RCC_APB1Periph_BKP, ENABLE);		//使能PWR和BK时钟

    PWR_BackupAccessCmd(ENABLE);		//允许访问RTC备份寄存器
    RCC_LSEConfig(RCC_LSE_OFF);			//禁用LSE
    while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == SET);		//等待LSE关闭
    PWR_BackupAccessCmd(DISABLE);		//禁止访问RTC备份寄存器
}
/**********************************************************************
* 名称 : F10x_Software_Reset(void)	 
* 功能 : 单片机软复位
* 输入 : 无
* 输出 : 无
* 说明 : 无
***********************************************************************/  
void F10x_Software_Reset(void)
{   
	SCB->AIRCR = 0x05FA0000 | (uint32_t)0x04;	  
}
/**********************************************************************
* 名称 : F10x_Clock_Initl(HSX_Def HSX,uint8_t pll_value)
* 功能 : 系统时钟初始化函数
* 输入 : HSX -- 时钟源(内部晶振或外部晶振)
		 pll_value -- 选择的倍频数，从2开始，最大值为16	
* 输出 : 无
* 说明 : 无 
***********************************************************************/ 
void F10x_Clock_Initl(HSX_Def HSX,uint8_t pll_value)
{
	uint8_t reg_value = 0x00;   

	RCC->CR |= HSX;  					//时钟使能
	if(HSX == HSE_ON)					//使用外部晶振
	{
		while((RCC->CR & HSE_RDY) != HSE_RDY);		//等待外部时钟就绪	
	}
	else if(HSX == HSI_ON)				//使用内部晶振
	{
		while((RCC->CR & HSI_RDY) != HSI_RDY);		//等待内部时钟就绪		
	}
	RCC->CFGR = 0x00000400; 			//APB1=DIV2;APB2=DIV1;AHB=DIV1;
	pll_value -= 2;						//抵消2个单位
	RCC->CFGR |= pll_value << 18;   	//设置PLL值 2~16
	RCC->CFGR |= 1<<16;	  				//PLLSRC ON 
	FLASH->ACR |= 0x32;	 				//FLASH 2个延时周期

	RCC->CR |= 0x01000000;  			//PLLON
	while(!(RCC->CR >> 25));			//等待PLL锁定
	RCC->CFGR |= 0x00000002;			//PLL作为系统时钟	 
	while(reg_value != 0x02)     		//等待PLL作为系统时钟设置成功
	{   
		reg_value = RCC->CFGR >> 2;
		reg_value &= 0x03;
	}    
}
/**********************************************************************
* 名称 : F10x_SysTick_Initl(void) 
* 功能 : 初始化SysTick延时函数
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void F10x_SysTick_Initl(void) 
{
	SysTick->CTRL &= ~(1 << 2);			//SYSTICK使用外部时钟源	0 = 外部时钟(SYSCLK/8),1 = 外部时钟(SYSCLK)
	fac_us = SYSTEM_CLOCK_FREQ / 8;		//每个us需要的systick时钟数  			
	fac_ms = (uint16_t)fac_us * 1000;	//每个ms需要的systick时钟数   
}
/**********************************************************************
* 名称 : delay_us(uint32_t nus)	 
* 功能 : 延时nus函数
* 输入 : nus为要延时的us数.
* 输出 : 无
* 说明 : 无
***********************************************************************/		    								   
void delay_us(uint32_t nus)
{		
	uint32_t value = 0x0;
	
	SysTick->LOAD = nus * fac_us; 		//时间加载	  		 
	SysTick->VAL = 0x00;        		//清空计数器
	SysTick->CTRL = 0x01 ;      		//开始倒数 	 
	do{
		value = SysTick->CTRL;
	}while((value&0x01)&&!(value&(1<<16)));		//等待时间到达   
	SysTick->CTRL = 0x00;       		//关闭计数器
	SysTick->VAL = 0x00;       			//清空计数器	 
}
/**********************************************************************
* 名称 : delay_ms(uint16_t nms)
* 功能 : 延时nms函数
* 输入 : nms:要延时的ms数(nms <= 1864)
* 输出 : 无
* 说明 : 无
***********************************************************************/
void delay_ms(uint16_t nms)
{	 		  	  
	uint32_t value = 0x0;
	
	SysTick->LOAD = (uint32_t)nms * fac_ms;		//时间加载(SysTick->LOAD为24bit)
	SysTick->VAL = 0x00;           		//清空计数器
	SysTick->CTRL = 0x01 ;          	//开始倒数  
	do{
		value = SysTick->CTRL;
	}while((value&0x01)&&!(value&(1<<16)));		//等待时间到达   
	SysTick->CTRL = 0x00;       		//关闭计数器
	SysTick->VAL = 0x00;       			//清空计数器	  	    
} 

/**********************************************************************
* 名称 : F10x_GPIO_Port_Initl(GPIO_TypeDef* GPIOx, 
				uint16_t GPIO_Pin, 
				GPIOSpeed_TypeDef GPIO_Speed,
				GPIOMode_TypeDef GPIO_Mode,
				BitAction io_Logic)  
* 功能 : GPIO端口初始化函数
* 输入 : GPIOx -- 端口(GPIOA/B/C/D......)
		 GPIO_Pin -- 引脚(GPIO_Pin_0/1/2/3/4/5......)
		 GPIO_Speed -- 速度
		 GPIO_Mode -- 端口模式
		 io_Logic -- 初始电平
* 输出 : 无
* 说明 : PC13/PC14/PC15作为输出时速度只能工作在2MHz模式下！！！
***********************************************************************/	
void F10x_GPIO_Port_Initl(GPIO_TypeDef* GPIOx, 
				uint16_t GPIO_Pin, 
				GPIOSpeed_TypeDef GPIO_Speed,
				GPIOMode_TypeDef GPIO_Mode,
				BitAction io_Logic) 
{
	GPIO_InitTypeDef GPIO_InitStructure;

    //1.使能GPIO时钟
    if (GPIOx == GPIOA) 		{
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    } else if (GPIOx == GPIOB) 	{
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    } else if (GPIOx == GPIOC) 	{
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    } else if (GPIOx == GPIOD) 	{
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    } else if (GPIOx == GPIOE) 	{
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);
    }
	
    //2.配置GPIO引脚
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed;
    GPIO_Init(GPIOx, &GPIO_InitStructure);
	
	//3.设置端口初始电平(仅限输出)
	if(GPIO_Mode == GPIO_Mode_Out_OD || GPIO_Mode == GPIO_Mode_AF_OD
	|| GPIO_Mode == GPIO_Mode_Out_PP || GPIO_Mode == GPIO_Mode_AF_PP)
	{
		GPIO_WriteBit(GPIOx,GPIO_Pin,io_Logic);	
	}
}
/**********************************************************************
* 名称 : F10x_EXIT_Channel_Set(uint32_t nChannel,FunctionalState EXIT_EN)
* 功能 : 指定通道中断关闭或启动(屏蔽或开放)
* 输入 : nChannel -- 通道(EXTI_Line0/1/2/3/4......15)
		 EXIT_EN -- 屏蔽或开放(ENABLE/DISABLE)
* 输出 ：无
* 说明 : 此函数用于实现类似于C51单片机的EX0 = 0或EX0 = 1功能,用来暂时关
闭或打开指定的外部中断！！！！
***********************************************************************/
void F10x_EXIT_Channel_Set(uint32_t nChannel,FunctionalState EXIT_EN)
{
	if(EXIT_EN == ENABLE)
		EXTI->IMR |= nChannel;
	else EXTI->IMR &= ~(nChannel);
}



