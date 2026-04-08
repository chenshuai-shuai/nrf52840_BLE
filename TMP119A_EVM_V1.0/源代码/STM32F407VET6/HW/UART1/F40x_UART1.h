#ifndef  __F40x_UART1_H__
#define  __F40x_UART1_H__

#include "delay.h"										//延时
#include "stdio.h"
#include "string.h"
/********************************端口定义******************************/
//1.UART1端口

// RXD -> PA09
// TXD -> PA10

/********************************参数定义******************************/
//1.串口参数

extern uint32_t UART1_BAUD;								//波特率

/********************************接口函数******************************/

void F40x_UART1_Initl(uint32_t uBAUD);					//UART1初始化
void F40x_UART1_WR_Byte(uint16_t _1Byte);				//写一个字节
void F40x_UART1_WR_String(uint8_t *s);					//写字符串

#endif
