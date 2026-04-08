#ifndef  __F40X_I2C_H__
#define  __F40X_I2C_H__

#include "delay.h"						//延时
#include "System_Config.h"
/********************************端口定义******************************/
//1.I2C1端口设置

#define I2C_SCL		PBout(6)			//SCL3
#define I2C_SDA		PBout(7)			//SDA3

#define SDA_IN_Val	PBin(7)				//SDA3

typedef enum {
	SDA_IN = 0x00,
	SDA_OUT
}DIR_Def;						

/********************************参数定义******************************/
//1.I2C操作宏定义

typedef enum
{
	I2C_WRITE = 0,						//写操作
	I2C_READ,							//读操作
}I2C_CMD;

//2.应答和非应答信号

typedef enum
{
	I2C_ACK = 0,						//应答信号
	I2C_NAK								//非应答信号
}I2C_nACK;

//3.缓存数组

#define I2C_Buf_Lengh	128				//缓存长度

extern uint8_t I2C_Buffer[I2C_Buf_Lengh];

/********************************接口函数******************************/

void I2C_IO_Initl(void);				//I2C初始化
void ANT_I2C_Wtite_nByte(uint8_t Device_Addr,uint8_t *buf,uint16_t Length);		//多字节写函数
void ANT_I2C_Read_nByte(uint8_t Device_Addr,uint8_t *buf,uint16_t Length);		//多字节读函数
	
#endif
