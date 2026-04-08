/**********************************************************************	 
* 公司名称 : 东莞市上古时代电子技术有限公司
* 模块名称 ：指示灯
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

#include "F10x_Led.h"

/**********************************************************************
* 名称 : F10x_Led_Initl(void)
* 功能 : 指示灯端口初始化
* 输入 : 无
* 输出 : 无
* 说明 : 无
***********************************************************************/ 
void F10x_Led_Initl(void)
{
	F10x_GPIO_Port_Initl(GPIOC,GPIO_Pin_1,GPIO_Speed_50MHz,GPIO_Mode_Out_PP,Bit_SET);	//LED1
	F10x_GPIO_Port_Initl(GPIOC,GPIO_Pin_4,GPIO_Speed_50MHz,GPIO_Mode_Out_PP,Bit_SET);	//LED2
}
/**********************************************************************
* 名称 : F10x_Led_Output(LED_Def nLed,uint8_t CMD)
* 功能 : 指示灯输出控制
* 输入 : nLed -- 指示灯
		 CMD -- 控制命令
* 输出 : 无
* 说明 : 无
***********************************************************************/ 
void F10x_Led_Output(LED_Def nLed,uint8_t CMD)
{
	if(CMD == LED_ON || CMD == LED_OFF)
	{
		switch(nLed)
		{
			case LED1:LED1_IO = CMD;
				break;
			case LED2:LED2_IO = CMD;
				break;
			case ALL_LED:LED1_IO = CMD;
					LED2_IO = CMD;
				break;
			default:
				break;
		}
	}
}


