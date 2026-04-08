#ifndef  __F10X_UART1_H__
#define  __F10X_UART1_H__

#include "string.h"
#include "stdio.h"	 
#include "Board_Driver.h"
/****************************参数定义*********************************/
//1.UART1参数定义

extern  uint32_t  UART1_uBAUD;				//波特率

/*****************************接口函数*********************************/

void F10x_UART1_Initl(uint32_t BAUD);		//UART1初始化
void F10x_UART1_SendByte(uint16_t uData);	//UART1数据发送
void F10x_UART1_SendString(char *str);		//UART1字符串发送

#endif



