#ifndef  __TFT_IO_H__
#define  __TFT_IO_H__

#include "Board_Driver.h"
/*****************************端口定义*********************************/
//1.TFT驱动端口定义

#define TFT_CS		PCout(2)			//CS
#define TFT_SCL		PCout(3)			//SCL

#define TFT_SDA		PBout(9)			//SDA
#define TFT_RS		PBout(8)			//RS
#define TFT_RES		PBout(5)			//RES
#define TFT_BLK		PBout(4)			//BLK

/*****************************参数定义*********************************/
//1.数据长度

typedef enum
{
	_1BYTE = 1,
	_2BYTE
}DTYPE_Def;

//2.屏幕参数

#define 	USE_HORIZONTAL 	2  			//设置横屏或者竖屏显示 0或1为竖屏 2或3为横屏

#if USE_HORIZONTAL == 0 || USE_HORIZONTAL == 1
	#define 	LCD_W 	135
	#define		LCD_H 	240
#else
	#define 	LCD_W 	240
	#define 	LCD_H 	135
#endif

//3.画笔颜色

#define 	WHITE         	 0xFFFF
#define 	BLACK         	 0x0000	  
#define 	BLUE           	 0x001F  
#define 	BRED             0XF81F
#define 	GRED 			 0XFFE0
#define 	GBLUE			 0X07FF
#define 	RED           	 0xF800
#define 	MAGENTA       	 0xF81F
#define 	GREEN         	 0x07E0
#define 	CYAN          	 0x7FFF
#define 	YELLOW        	 0xFFE0
#define 	BROWN 			 0XBC40 	//棕色
#define 	BRRED 			 0XFC07 	//棕红色
#define 	GRAY  			 0X8430 	//灰色
#define 	DARKBLUE      	 0X01CF		//深蓝色
#define 	LIGHTBLUE      	 0X7D7C		//浅蓝色  
#define 	GRAYBLUE       	 0X5458 	//灰蓝色
#define 	LIGHTGREEN     	 0X841F 	//浅绿色
#define 	LGRAY 			 0XC618 	//浅灰色(PANNEL),窗体背景色
#define 	LGRAYBLUE        0XA651 	//浅灰蓝色(中间层颜色)
#define 	LBBLUE           0X2B12 	//浅棕蓝色(选择条目的反色)

/*****************************接口函数*********************************/

void TFT_IO_Initl(void);				//端口初始化
void TFT_Write_Byte(uint8_t value);		//单字节写函数
void TFT_Write_Data(DTYPE_Def length,uint16_t value);	//写数据函数
void TFT_Write_Reg(uint8_t value);		//写寄存器函数
void TFT_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2);	//设置起始和结束地址函数
void TFT_Initl(void);					//初始化

#endif




