/**********************************************************************	 
* 公司名称 : 东莞市上古时代电子技术有限公司
* 模块名称 ：TMP119 测试程序
* 系统主频 : 72 MHz
* 创建人   : eysmcu
* 修改人   : eysmcu 
* 创建日期 : 2025年12月15日
* 店铺地址 : http://mindesigner.taobao.com
* 淘宝ID号 : eysmcu
* 重要声明 ：本例程为东莞市上古时代电子技术有限公司原创，本程序只供学习
使用，未经作者许可，不得用于其它任何用途，转载清注明出处！！！

						 Copyright(C) 上古时代
***********************************************************************/

#include "F10x_Led.h"
#include "F10x_UART1.h"
#include "TFT_Driver.h"
#include "F10x_Key.h"
#include "TMP119_EVM.h"

/*****************************参数定义*********************************/

#define VER_TFT							//用TFT显示电压值

static float Temperature = 0x00;		//当前温度值

/**********************************************************************
* 名称 : main(void)
* 功能 : 主函数
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
int main(void)
{
	F10x_Clock_Initl(HSE_ON,9);			//初始化时钟
	F10x_SysTick_Initl();				//延时函数初始化
	F10x_Led_Initl();					//初始化LED
#ifdef VER_TFT	
	TFT_Initl();						//初始化TFT
#endif
	F10x_Key_Initl();					//初始化按键
	F10x_UART1_Initl(UART1_uBAUD);		//初始化UART1
	ANT_I2C_IO_Initl();					//初始化I2C
	TMP_Initl();						//初始化TMP117
	delay_ms(10);
#ifdef VER_TFT		
	TFT_Fill(0,0,LCD_W,LCD_H,BLACK);	//设置TFT背景
	TFT_ShowString(16, 16,(const uint8_t *)" TMP119 EVM DEMO ",GREEN ,BLACK,24,0);
	TFT_ShowChinese(160,56,"℃",RED,BLACK,32,0);
#endif	
	while(1)
	{
		Board_Key_Manage();				//按键扫描
		F10x_Led_Output(LED1,LED_OFF);	//关指示灯
		Temperature = TMP119_Read_Temperature();					//读取温度值	
		F10x_Led_Output(LED1,LED_ON);	//开指示灯
#ifdef VER_TFT							//TFT显示
		TFT_ShowDouble(64,56,Temperature,2,0,RED ,BLACK,32,0);		//显示温度
#endif
		printf("Temperature = %.2f ℃\r\n",Temperature);			//串口打印
	}
}
