/**********************************************************************						
* 公司名称 : 东莞市上古时代电子技术有限公司
* 模块名称 ：IO模拟I2C
* 系统主频 : 168.0MHz
* 创建人   : 向 恒
* 修改人   : 向 恒  
* 版本号   : Ver 1.0
* 创建日期 : 2022年1月16日
                            版权所有 @ 上古时代
***********************************************************************/

#include "F40x_I2C.h"							

/********************************参数定义******************************/

uint8_t I2C_Buffer[I2C_Buf_Lengh] = {0x00};

/**********************************************************************
* 名称 : I2C_SDA_DIR(DIR_Def DIR)
* 功能 : SDA1方向设置
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void I2C_SDA_DIR(DIR_Def DIR)
{
	GPIO_InitTypeDef  GPIO_InitStructure;

	//1.时钟配置
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA			//使能GPIOA时钟
						 | RCC_AHB1Periph_GPIOB
						 | RCC_AHB1Periph_GPIOC
						 | RCC_AHB1Periph_GPIOD
						 | RCC_AHB1Periph_GPIOE,ENABLE); 
	//2.方向设置
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;			//PB7 - SDA
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;	//外部上拉
	if(DIR == SDA_IN)	
	{
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;	//普通输入模式			
	}
	else 
	{
		GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;	//普通输出模式
		GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;			
	}
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;	//100MHz
	GPIO_Init(GPIOB, &GPIO_InitStructure);						
}
/**********************************************************************
* 名称 : I2C_IO_Initl(void)
* 功能 : I2C上电初始化
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void I2C_IO_Initl(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;

	//1.时钟配置
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA				
						 | RCC_AHB1Periph_GPIOB
						 | RCC_AHB1Periph_GPIOC
						 | RCC_AHB1Periph_GPIOD
						 | RCC_AHB1Periph_GPIOE,ENABLE); 
	
	//2.初始化SCL | SDA
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;			//PB6 -> SCL 												
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;		//普通输出模式
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;				
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;	//100MHz
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;		//上拉
	GPIO_Init(GPIOB, &GPIO_InitStructure);				//初始化	
	
	//3.初始化完成后设置初始电平
	I2C_SDA_DIR(SDA_OUT);
	I2C_SCL = 1;
	I2C_SDA = 1;							//上电后全部设置为1
}
/**********************************************************************
* 名称 : I2C_Start(void)
* 功能 : 起始信号
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void I2C_Start(void)
{
    I2C_SDA_DIR(SDA_OUT);					//SDA设置为输出
	I2C_SDA = 1;	  	  
	I2C_SCL = 1;	delay_us(4);
 	I2C_SDA = 0;	delay_us(4);			//START:when CLK is high,DATA change form high to low 
	I2C_SCL = 0;							//钳住I2C总线，准备发送或接收数据 
}
/**********************************************************************
* 名称 : I2C_Stop(void)
* 功能 : 停止信号
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void I2C_Stop(void)
{
    I2C_SDA_DIR(SDA_OUT);					//SDA设置为输出
	I2C_SCL = 0;
	I2C_SDA = 0;	delay_us(4);			//STOP:when CLK is high DATA change form low to high
	I2C_SCL = 1; 
	I2C_SDA = 1;	delay_us(4);			//发送I2C总线结束信号
}    
/**********************************************************************
* 名称 : I2C_Wait_Ack(void)
* 功能 : 等待应答信号到来
* 输入 : Ack -- 0:ACK 1:NAK
* 输出 ：1，接收应答失败
		 0，接收应答成功
* 说明 : 无
***********************************************************************/
uint8_t I2C_Wait_Ack(void)
{
	uint8_t ucErrTime = 0;
	
	I2C_SDA_DIR(SDA_IN);					//SDA设置为输入
	I2C_SDA = 1;	delay_us(1);	   
	I2C_SCL = 1;	delay_us(1);	 
	while(SDA_IN_Val)
	{
		ucErrTime ++;
		if(ucErrTime > 250)
		{
			I2C_Stop();
			return 1;
		}
	}
	I2C_SCL = 0;							//时钟输出0 	   
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
	I2C_SDA_DIR(SDA_OUT);					//SDA设置为输出
	I2C_SCL = 0;
	I2C_SDA = nACK;		delay_us(2);
	I2C_SCL = 1;		delay_us(4);
	I2C_SCL = 0;
}
/**********************************************************************
* 名称 : I2C_Write_Byte(uint8_t txd)
* 功能 : IIC发送一个字节
* 输入 : 1，有应答
		 0，无应答
* 输出 ：无
* 说明 : 无
***********************************************************************/
void I2C_Write_Byte(uint8_t txd)
{
    uint8_t t = 0x00;  
	
	I2C_SDA_DIR(SDA_OUT);					//SDA设置为输出  
    I2C_SCL = 0;							//拉低时钟开始数据传输
    for(t = 0;t < 8;t ++)
    {              
        I2C_SDA = (txd & 0x80) >> 7;
        txd <<= 1; 	
		delay_us(4);  
		I2C_SCL = 1;
		delay_us(2); 
		I2C_SCL = 0;	
		delay_us(4);
    }	
}
/**********************************************************************
* 名称 : I2C_Read_Byte(void)
* 功能 : IIC读取一个字节
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
uint8_t I2C_Read_Byte(void)
{
	uint8_t i = 0x00,receive = 0;
	
	I2C_SDA_DIR(SDA_IN);					//SDA设置为输入
    for(i = 0;i < 8;i ++ )
	{
        I2C_SCL  = 0; 
        delay_us(4);
		I2C_SCL =  1;
        receive <<= 1;
        if(SDA_IN_Val)	receive ++;   
		delay_us(4); 
    }					 
    return receive;
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
