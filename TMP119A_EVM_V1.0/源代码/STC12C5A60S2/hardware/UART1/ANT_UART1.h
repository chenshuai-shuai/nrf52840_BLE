#ifndef  __ANT_UART1_H__
#define  __ANT_UART1_H__

#include "STC12C5A60S2.h"
#include "stdio.h"
#include "string.h"
/****************************参数定义*********************************/
//1.Define UART parity mode

#define NONE_PARITY     0   			//None parity
#define ODD_PARITY      1   			//Odd parity
#define EVEN_PARITY     2  				//Even parity
#define MARK_PARITY     3   			//Mark parity
#define SPACE_PARITY    4   			//Space parity

#define PARITYBIT 		NONE_PARITY  	//Testing even parity

//2.串口标志位

enum {
  Idle = 0,
  Busy
};

extern bit u1_Busy;

/*****************************接口函数*********************************/

void ANT_UART1_Initl(uint32_t uBaud);	//初始化函数 
void ANT_UART1_SendByte(uint8_t value);	//单字节发送
void ANT_UART1_SendString(char *s);		//字符串发送


#endif










