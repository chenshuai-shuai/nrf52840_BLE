#ifndef  __F10X_LED_H__
#define  __F10X_LED_H__

#include "Board_Driver.h"
/*****************************端口定义*********************************/
//1.指示灯端口定义

#define LED1_IO	PCout(1)			//LED1
#define	LED2_IO	PCout(4)			//LED2

/*****************************参数定义*********************************/

typedef enum {
	LED1 = 0,
	LED2,
	ALL_LED
}LED_Def;

enum {
	LED_ON = 0,
	LED_OFF
};

/*****************************接口函数*********************************/

void F10x_Led_Initl(void);		//指示灯初始化
void F10x_Led_Output(LED_Def nLed,uint8_t CMD);	//指示灯控制函数
	
#endif














