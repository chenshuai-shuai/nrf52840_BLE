#ifndef  __F10X_TIM3_H__
#define  __F10X_TIM3_H__

#include "stm32f10x_tim.h"
#include "F10x_Key.h"
/*******************************定时参数*******************************/
//1.定时变量

extern uint32_t TIM3_COUNT;							//计时变量
extern uint8_t  Flag_TIM3;

/*****************************接口函数*********************************/

void F10x_TIM3_Initl(uint16_t arr,uint16_t psc);	//TIM3初始化

#endif












