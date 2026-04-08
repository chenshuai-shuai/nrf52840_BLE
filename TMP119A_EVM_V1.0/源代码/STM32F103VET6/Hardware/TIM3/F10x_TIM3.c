/**********************************************************************	 
* 公司名称 : 东莞市上古时代电子技术有限公司
* 模块名称 ：TIM3  基本定时器
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

#include "F10X_TIM3.h"

/*******************************定时参数*******************************/
//1.定时变量

uint32_t TIM3_COUNT = 0x00;	
uint8_t  Flag_TIM3 = 0x00;

/**********************************************************************
* 名称 : F10x_TIM3_Initl(uint16_t arr,uint16_t psc)
* 功能 : 通用定时器3中断初始化
* 输入 : arr -- 自动重装值，psc -- 时钟预分频数
* 输出 ：无
* 说明 : 这里时钟选择为APB1的2倍，而APB1为36M
***********************************************************************/
void F10x_TIM3_Initl(uint16_t arr,uint16_t psc)
{
    TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);//使能时钟

	TIM_TimeBaseStructure.TIM_Period = arr; 			//设置在下一个更新事件装入活动的自动重装载寄存器周期的值
	TIM_TimeBaseStructure.TIM_Prescaler =psc; 			//设置用来作为TIMx时钟频率除数的预分频值  10Khz的计数频率  
	TIM_TimeBaseStructure.TIM_ClockDivision = 0; 		//设置时钟分割:TDTS = Tck_tim
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;  //TIM向上计数模式
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure); 	//根据TIM_TimeBaseInitStruct中指定的参数初始化TIMx的时间基数单位
 
	TIM_ITConfig(TIM3,TIM_IT_Update,ENABLE ); 			//使能指定的TIM3中断,允许更新中断

	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;  	//TIM3中断
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;  //先占优先级0级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;  //从优先级3级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; 	//IRQ通道被使能
	NVIC_Init(&NVIC_InitStructure); 				 	//根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器

	TIM_Cmd(TIM3, ENABLE);  							//使能TIMx外设
}
/**********************************************************************
* 名称 : TIM3_IRQHandler(void)
* 功能 : 定时器3中断服务程序
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void TIM3_IRQHandler(void)   							//TIM3中断
{
	if(TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET) 	//检查指定的TIM中断发生与否:TIM 中断源 
	{
		TIM_ClearITPendingBit(TIM3, TIM_IT_Update);  	//清除TIMx的中断待处理位:TIM 中断源 
		Key_Scan();										//按键扫描
	}
}















