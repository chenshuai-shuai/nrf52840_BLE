#ifndef  __ANT_I2C_H__
#define  __ANT_I2C_H__

#include "F10x_UART1.h"						//UART1
#include "Board_Driver.h"
/****************************端口定义*********************************/
//1.I2C端口定义

#define I2C_SCL		PBout(6)				//SCL(输出)
#define I2C_SDA  	PBout(7)				//SDA(输出)

#define SDA_READ 	PBin(7)					//SDA(输入)

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

//3.I2C从机地址

extern uint8_t Slave_Addr;					//注意区分8位或7位地址！！！

/*****************************接口函数*********************************/
//1.初始化

void ANT_I2C_IO_Initl(void);				//I2C端口初始化
void I2C_Start(void);
void I2C_Stop(void);
uint8_t I2C_Wait_Ack(void);
void I2C_Send_N_Ack(I2C_nACK nACK);
void I2C_Write_Byte(uint8_t value);
uint8_t I2C_Read_Byte(void);

//2.I2C通用读写操作函数

void ANT_I2C_Wtite_nByte(uint8_t Device_Addr,uint8_t *buf,uint16_t Length);		//向I2C设备写n个数据函数
void ANT_I2C_Read_nByte(uint8_t Device_Addr,uint8_t *buf,uint16_t Length);		//从I2C设备读n个数据函数


#endif



