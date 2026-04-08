/**********************************************************************	 
* 公司名称 : 东莞市上古时代电子技术有限公司
* 模块名称 ：1.14IPS TFT端口驱动程序
* 系统主频 : 72 MHz
* 创建人   : eysmcu
* 修改人   : eysmcu 
* 创建日期 : 2025年5月4日
* 店铺地址 : http://mindesigner.taobao.com
* 淘宝ID号 : eysmcu
* 重要声明 ：本例程为东莞市上古时代电子技术有限公司原创，本程序只供学习
使用，未经作者许可，不得用于其它任何用途，转载清注明出处！！！

						 Copyright(C) 上古时代
***********************************************************************/

#include "TFT_IO.h"

/**********************************************************************
* 名称 : TFT_IO_Initl(void)
* 功能 : TFT端口初始化并设置初始电平
* 输入 : 无
* 输出 : 无
* 说明 : 无
***********************************************************************/ 
void TFT_IO_Initl(void)
{
	//1.屏蔽JTAG接口
	F10x_JTAG_Config(JTAG_DISABLE_SWD_ENABLE);		//屏蔽PB口JTAG相应引脚，做GPIO使用
	
	//2.端口初始化，设置初始化电平
	F10x_GPIO_Port_Initl(GPIOB,GPIO_Pin_4,GPIO_Speed_50MHz,GPIO_Mode_Out_PP,Bit_SET);	//PB4 - BLK
	F10x_GPIO_Port_Initl(GPIOB,GPIO_Pin_5,GPIO_Speed_50MHz,GPIO_Mode_Out_PP,Bit_SET);	//PB5 - RES
	F10x_GPIO_Port_Initl(GPIOB,GPIO_Pin_8,GPIO_Speed_50MHz,GPIO_Mode_Out_PP,Bit_SET);	//PB8 - RS
	F10x_GPIO_Port_Initl(GPIOB,GPIO_Pin_9,GPIO_Speed_50MHz,GPIO_Mode_Out_PP,Bit_SET);	//PB9 - SDA
	F10x_GPIO_Port_Initl(GPIOC,GPIO_Pin_2,GPIO_Speed_50MHz,GPIO_Mode_Out_PP,Bit_SET);	//PC2 - CS
	F10x_GPIO_Port_Initl(GPIOC,GPIO_Pin_3,GPIO_Speed_50MHz,GPIO_Mode_Out_PP,Bit_SET);	//PC3 - SCL
}
/**********************************************************************
* 名称 : TFT_Write_Byte(uint8_t value)
* 功能 : LCD串行数据写入函数
* 输入 : value -- 数据
* 输出 : 无
* 说明 : 无
***********************************************************************/ 
void TFT_Write_Byte(uint8_t value)
{
	TFT_CS = 0;
	for(uint8_t i = 0x00;i < 8;i ++)
	{
		TFT_SCL = 0;
		if((value & 0x80) == 0x80)
			TFT_SDA = 1;
		else TFT_SDA = 0;
		
		TFT_SCL = 1;
		value <<= 1;
	}
	TFT_CS = 1;
}
/**********************************************************************
* 名称 : TFT_Write_Data(DTYPE_Def length,uint16_t value)
* 功能 : 写数据函数
* 输入 : length -- 长度,value -- 数据
* 输出 ：无
* 说明 : 无
***********************************************************************/
void TFT_Write_Data(DTYPE_Def length,uint16_t value)
{
	for(uint8_t i = 0;i < length;i ++)
	{
		TFT_Write_Byte(value >> ((length - i - 1) * 8));
	}
}
/**********************************************************************
* 名称 : TFT_Write_Reg(uint8_t value)
* 功能 : 写寄存器函数
* 输入 : value -- 数据
* 输出 ：无
* 说明 : 无
***********************************************************************/
void TFT_Write_Reg(uint8_t value)
{
	TFT_RS = 0;							//RS = 0(写命令)
	TFT_Write_Byte(value);
	TFT_RS = 1;							//RS = 1(写数据)
}
/**********************************************************************
* 名称 : TFT_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
* 功能 : 设置起始和结束地址函数
* 输入 : x1,x2 设置列的起始和结束地址
		 y1,y2 设置行的起始和结束地址
* 输出 ：无
* 说明 : 无
***********************************************************************/
void TFT_Address_Set(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2)
{
	if(USE_HORIZONTAL==0)
	{
		TFT_Write_Reg(0x2a);			//列地址设置
		TFT_Write_Data(_2BYTE,x1+52);
		TFT_Write_Data(_2BYTE,x2+52);
		TFT_Write_Reg(0x2b);			//行地址设置
		TFT_Write_Data(_2BYTE,y1+40);
		TFT_Write_Data(_2BYTE,y2+40);
		TFT_Write_Reg(0x2c);			//储存器写
	}
	else if(USE_HORIZONTAL==1)
	{
		TFT_Write_Reg(0x2a);			//列地址设置
		TFT_Write_Data(_2BYTE,x1+53);
		TFT_Write_Data(_2BYTE,x2+53);
		TFT_Write_Reg(0x2b);			//行地址设置
		TFT_Write_Data(_2BYTE,y1+40);
		TFT_Write_Data(_2BYTE,y2+40);
		TFT_Write_Reg(0x2c);			//储存器写
	}
	else if(USE_HORIZONTAL==2)
	{
		TFT_Write_Reg(0x2a);			//列地址设置
		TFT_Write_Data(_2BYTE,x1+40);
		TFT_Write_Data(_2BYTE,x2+40);
		TFT_Write_Reg(0x2b);			//行地址设置
		TFT_Write_Data(_2BYTE,y1+53);
		TFT_Write_Data(_2BYTE,y2+53);
		TFT_Write_Reg(0x2c);			//储存器写
	}
	else
	{
		TFT_Write_Reg(0x2a);			//列地址设置
		TFT_Write_Data(_2BYTE,x1+40);
		TFT_Write_Data(_2BYTE,x2+40);
		TFT_Write_Reg(0x2b);			//行地址设置
		TFT_Write_Data(_2BYTE,y1+52);
		TFT_Write_Data(_2BYTE,y2+52);
		TFT_Write_Reg(0x2c);			//储存器写
	}
}
/**********************************************************************
* 名称 : TFT_Initl(void)
* 功能 : TFT初始化
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void TFT_Initl(void)
{
	TFT_IO_Initl();						//端口初始化
	
	TFT_RES = 0;		delay_ms(100);	//复位
	TFT_RES = 1;		delay_ms(100);
	
	TFT_BLK = 0;		delay_ms(100);	//打开背光
		
	TFT_Write_Reg(0x11);delay_ms(120);  	
	TFT_Write_Reg(0x36); 
	
	if(USE_HORIZONTAL == 0)			TFT_Write_Data(_1BYTE,0x00);
	else if(USE_HORIZONTAL == 1)	TFT_Write_Data(_1BYTE,0xC0);
	else if(USE_HORIZONTAL == 2)	TFT_Write_Data(_1BYTE,0x70);
	else TFT_Write_Data(_1BYTE,0xA0);

	TFT_Write_Reg(0x3A);
	TFT_Write_Data(_1BYTE,0x05);

	TFT_Write_Reg(0xB2);
	TFT_Write_Data(_1BYTE,0x0C);
	TFT_Write_Data(_1BYTE,0x0C);
	TFT_Write_Data(_1BYTE,0x00);
	TFT_Write_Data(_1BYTE,0x33);
	TFT_Write_Data(_1BYTE,0x33); 

	TFT_Write_Reg(0xB7); 
	TFT_Write_Data(_1BYTE,0x35);  

	TFT_Write_Reg(0xBB);
	TFT_Write_Data(_1BYTE,0x19);

	TFT_Write_Reg(0xC0);
	TFT_Write_Data(_1BYTE,0x2C);

	TFT_Write_Reg(0xC2);
	TFT_Write_Data(_1BYTE,0x01);

	TFT_Write_Reg(0xC3);
	TFT_Write_Data(_1BYTE,0x12);   

	TFT_Write_Reg(0xC4);
	TFT_Write_Data(_1BYTE,0x20);  

	TFT_Write_Reg(0xC6); 
	TFT_Write_Data(_1BYTE,0x0F);    

	TFT_Write_Reg(0xD0); 
	TFT_Write_Data(_1BYTE,0xA4);
	TFT_Write_Data(_1BYTE,0xA1);

	TFT_Write_Reg(0xE0);
	TFT_Write_Data(_1BYTE,0xD0);
	TFT_Write_Data(_1BYTE,0x04);
	TFT_Write_Data(_1BYTE,0x0D);
	TFT_Write_Data(_1BYTE,0x11);
	TFT_Write_Data(_1BYTE,0x13);
	TFT_Write_Data(_1BYTE,0x2B);
	TFT_Write_Data(_1BYTE,0x3F);
	TFT_Write_Data(_1BYTE,0x54);
	TFT_Write_Data(_1BYTE,0x4C);
	TFT_Write_Data(_1BYTE,0x18);
	TFT_Write_Data(_1BYTE,0x0D);
	TFT_Write_Data(_1BYTE,0x0B);
	TFT_Write_Data(_1BYTE,0x1F);
	TFT_Write_Data(_1BYTE,0x23);

	TFT_Write_Reg(0xE1);
	TFT_Write_Data(_1BYTE,0xD0);
	TFT_Write_Data(_1BYTE,0x04);
	TFT_Write_Data(_1BYTE,0x0C);
	TFT_Write_Data(_1BYTE,0x11);
	TFT_Write_Data(_1BYTE,0x13);
	TFT_Write_Data(_1BYTE,0x2C);
	TFT_Write_Data(_1BYTE,0x3F);
	TFT_Write_Data(_1BYTE,0x44);
	TFT_Write_Data(_1BYTE,0x51);
	TFT_Write_Data(_1BYTE,0x2F);
	TFT_Write_Data(_1BYTE,0x1F);
	TFT_Write_Data(_1BYTE,0x1F);
	TFT_Write_Data(_1BYTE,0x20);
	TFT_Write_Data(_1BYTE,0x23);

	TFT_Write_Reg(0x21); 
	TFT_Write_Reg(0x29); 		
}









