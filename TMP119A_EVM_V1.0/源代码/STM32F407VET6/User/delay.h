#ifndef  __DELAY_H__
#define  __DELAY_H__

#include "System_Config.h"				//系统配置文件

/*****************************接口函数*********************************/

extern void delay_init(u8 SYSCLK);		//延时初始化
extern void delay_ms(u16 nms);			//ms延时
extern void delay_us(u32 nus);			//us延时	

#endif



