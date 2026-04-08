#ifndef  __ANT_I2C_H__
#define  __ANT_I2C_H__

#include "delay.h"
#include "STC12C5A60S2.h"
/****************************端口定义*********************************/
//1.I2C端口定义

sbit 	I2C_SCL = P2^0;						//SCL
sbit 	I2C_SDA = P2^1;						//SDA

/****************************参数定义*********************************/
//1.I2C协议相关

typedef enum
{
	I2C_WRITE = 0,							//写操作
	I2C_READ,								//读操作
}I2C_CMD;

typedef enum
{
	I2C_ACK = 0,							//应答信号
	I2C_NAK									//非应答信号
}I2C_nACK;

//2.I2C缓存数组

#define I2C_Buf_Length		64				//缓存区长度

extern uint8_t I2C_Buffer[I2C_Buf_Length]; 	//缓存区

/*****************************接口函数*********************************/
//1.初始化

void ANT_I2C_Initl(void);					//I2C端口初始化
void I2C_Start(void);
void I2C_Stop(void);
bit I2C_Wait_Ack(void);
void I2C_Send_N_Ack(I2C_nACK nACK);
void I2C_Send_Byte(uint8_t value);
uint8_t I2C_Read_Byte(void);

//2.I2C通用读写操作函数

void ANT_I2C_Wtite_nByte(uint8_t Device_Addr,uint8_t *buf,uint16_t Length);		//向I2C设备写n个数据函数
void ANT_I2C_Read_nByte(uint8_t Device_Addr,uint8_t *buf,uint16_t Length);		//从I2C设备读n个数据函数

#endif



