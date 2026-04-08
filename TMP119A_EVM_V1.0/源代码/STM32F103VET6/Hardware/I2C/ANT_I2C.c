/**********************************************************************	 
* 公司名称 : 东莞市上古时代电子技术有限公司
* 模块名称 ：软件模拟I2C驱动
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

#include "ANT_I2C.h"

/*****************************参数定义*********************************/
//1.I2C缓存数组

uint8_t I2C_Buffer[I2C_Buf_Length] = {0x00}; 	

//2.I2C地址

uint8_t Slave_Addr = 0xA0;				

/**********************************************************************
* 名称 : I2C_SDA_Input(void)
* 功能 : SDA方向设置为输入
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void I2C_SDA_Input(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;   			

	RCC_APB2PeriphClockCmd(	RCC_APB2Periph_GPIOA	//使能端口时钟
						|	RCC_APB2Periph_GPIOB
						| 	RCC_APB2Periph_GPIOC
						|	RCC_APB2Periph_GPIOD
						|	RCC_APB2Periph_GPIOE, ENABLE);	
						  						  	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7; 		//PB7 - SDA
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;	//设置为上拉输入
    GPIO_Init(GPIOB, &GPIO_InitStructure); 			//初始化PB7
}
/**********************************************************************
* 名称 : I2C_SDA_Output(void)
* 功能 : SDA方向设置为输出
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void I2C_SDA_Output(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;   			

	RCC_APB2PeriphClockCmd(	RCC_APB2Periph_GPIOA	//使能端口时钟
						|	RCC_APB2Periph_GPIOB
						| 	RCC_APB2Periph_GPIOC
						|	RCC_APB2Periph_GPIOD
						|	RCC_APB2Periph_GPIOE, ENABLE);	
						  					  	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7; 		//PB7 - SDA
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;//设置为推挽输出
    GPIO_Init(GPIOB, &GPIO_InitStructure); 			//初始化PB7
}
/**********************************************************************
* 名称 : ANT_I2C_IO_Initl(void)
* 功能 : I2C端口初始化
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void ANT_I2C_IO_Initl(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;   			

	RCC_APB2PeriphClockCmd(	RCC_APB2Periph_GPIOA	//使能端口时钟
						|	RCC_APB2Periph_GPIOB
						| 	RCC_APB2Periph_GPIOC
						|	RCC_APB2Periph_GPIOD
						|	RCC_APB2Periph_GPIOE, ENABLE);	
	
	//1.初始化SCL
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6; 		//PB6 - SCL
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;//设置为推挽式输出
    GPIO_Init(GPIOB, &GPIO_InitStructure); 			//初始化PB6
	
	//2.初始化SDA并设置初始电平
	I2C_SDA_Output();								//上电为输出	
	
	I2C_SCL = 1;
	I2C_SDA = 1;
	
	//3.设置从机地址
	Slave_Addr = 0xA0;								//设置设备地址
	memset(I2C_Buffer,0x00,sizeof(I2C_Buffer));		
}
/**********************************************************************
* 名称 : I2C_Start(void)
* 功能 : I2C起始信号
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void I2C_Start(void)
{
	I2C_SDA_Output();     	//SDA为输出
	I2C_SDA = 1;	  	  
	I2C_SCL = 1;		delay_us(4);
 	I2C_SDA = 0;		delay_us(4);
	I2C_SCL = 0;
}
/**********************************************************************
* 名称 : I2C_Stop(void)
* 功能 : I2C停止信号
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void I2C_Stop(void)
{
	I2C_SDA_Output();     	//SDA为输出
	I2C_SCL = 0;
	I2C_SDA = 0;		delay_us(4);
	I2C_SCL = 1; 		
	I2C_SDA = 1;		delay_us(4);							   	
}     
/**********************************************************************
* 名称 : I2C_Wait_Ack(void)
* 功能 : I2C等待应答信号到来
* 输入 : 无
* 输出 ：1 -- 接收应答失败
		 0 -- 接收应答成功
* 说明 : 无
***********************************************************************/
uint8_t I2C_Wait_Ack(void)
{
	uint8_t ucErrTime = 0;
	
	I2C_SDA_Output();     	//SDA为输出
	I2C_SDA = 1;		delay_us(1);	   
	I2C_SCL = 1;		delay_us(1);	
	I2C_SDA_Input();     	//SDA为输入
	while(SDA_READ)
	{
		ucErrTime ++;
		if(ucErrTime > 250)
		{
			I2C_Stop();
			return 1;
		}
	}
	I2C_SCL = 0;				
	
	return 0;  
} 
/**********************************************************************
* 名称 : I2C_Send_N_Ack(I2C_nACK nACK)
* 功能 : I2C产生应答/非应答信号
* 输入 : 0 -- 应答信号
		 1 -- 非应答信号
* 输出 ：无
* 说明 : 无
***********************************************************************/
void I2C_Send_N_Ack(I2C_nACK nACK)
{
	I2C_SDA_Output();     	//SDA为输出
	I2C_SCL = 0;
	I2C_SDA = nACK;		delay_us(2);
	I2C_SCL = 1;		delay_us(4);
	I2C_SCL = 0;
}
/**********************************************************************
* 名称 : I2C_Write_Byte(uint8_t value)
* 功能 : I2C写单字节数据
* 输入 : value -- 输入数据
* 输出 ：无
* 说明 : 无
***********************************************************************/
void I2C_Write_Byte(uint8_t value)
{                         
	I2C_SDA_Output();     	//SDA为输出
    I2C_SCL = 0;			//拉低时钟开始数据传输
    for(uint8_t i = 0;i <8;i ++)
    {     
		if((value & 0x80) == 0x80)
			I2C_SDA = 1;
		else I2C_SDA = 0;
		
		delay_us(2); 
		I2C_SCL = 1;	delay_us(2); 
		I2C_SCL = 0;	delay_us(2);
		value <<= 1;
    }	 
} 
/**********************************************************************
* 名称 : I2C_Read_Byte(void)
* 功能 : I2C读单字节数据
* 输入 : 无
* 输出 ：value -- 读到的数据
* 说明 : 无
***********************************************************************/
uint8_t I2C_Read_Byte(void)
{
	uint8_t value = 0x00;
	
	I2C_SDA_Input();     	//SDA为输入
	for(uint8_t i = 0;i < 8;i ++)
	{
		value <<= 1;
		I2C_SCL = 0;	delay_us(2); 
		I2C_SCL = 1;
		if(SDA_READ == 1)	
		{
			value ++;
		}
		delay_us(1); 
	}
	return value;
}
/**********************************************************************
* 名称 : ANT_I2C_Wtite_nByte(uint8_t Device_Addr,uint8_t *buf,uint16_t Length)
* 功能 : I2C向指定设备写n个字节函数
* 输入 : Device_Addr -- 设备地址(从机地址)
		 *buf -- 数据
		 Length -- 写入长度
* 输出 ：无
* 说明 : 无
***********************************************************************/
void ANT_I2C_Wtite_nByte(uint8_t Device_Addr,uint8_t *buf,uint16_t Length)
{
	I2C_Start();            //起始信号
	I2C_Write_Byte(Device_Addr | I2C_WRITE);//发送设备地址+写信号	
	I2C_Wait_Ack();
	delay_us(10);			//延迟10US
	for(uint16_t i = 0;i < Length;i ++)
	{
		I2C_Write_Byte(*buf ++); 			//写入数据
		I2C_Wait_Ack();	 
		delay_us(10);		//数据间隔
	}
	I2C_Stop();           	//停止信号
}
/**********************************************************************
* 名称 : ANT_I2C_Read_nByte(uint8_t Device_Addr,uint8_t *buf,uint16_t Length)
* 功能 : I2C从指定设备读n个字节函数
* 输入 : Device_Addr -- 设备地址(从机地址)
		 *buf -- 数据
		 Length -- 写入长度
* 输出 ：buf中的数据！！！
* 说明 : 无
***********************************************************************/
void ANT_I2C_Read_nByte(uint8_t Device_Addr,uint8_t *buf,uint16_t Length)
{
	I2C_Start();      		//起始信号
	I2C_Write_Byte(Device_Addr | I2C_READ);	//发送设备地址+读信号
	I2C_Wait_Ack();
	delay_us(10);			//延迟10US
	for(uint16_t i = 0;i < Length;i ++)
	{
		*buf ++ = I2C_Read_Byte(); 			//接收数据
		
		if (i == (Length - 1))
			I2C_Send_N_Ack(I2C_NAK); 		//非应答信号	
		else I2C_Send_N_Ack(I2C_ACK);		//应答信号

		delay_us(10);		//数据间隔
	}
	I2C_Stop();          	//停止信号
}



