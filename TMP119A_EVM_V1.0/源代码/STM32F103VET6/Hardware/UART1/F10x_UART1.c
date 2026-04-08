/**********************************************************************	 
* 公司名称 : 东莞市上古时代电子技术有限公司
* 模块名称 ：UART1(USB-TTL)
* 系统主频 : 72 MHz
* 创建人   : eysmcu
* 修改人   : eysmcu 
* 创建日期 : 2025年1月10日
* 店铺地址 : http://mindesigner.taobao.com
* 淘宝ID号 : eysmcu
* 重要声明 ：本例程为东莞市上古时代电子技术有限公司原创，本程序只供学习
使用，未经作者许可，不得用于其它任何用途，转载清注明出处！！！

						 Copyright(C) 上古时代
***********************************************************************/

#include "F10x_UART1.h"								//UART1

/*****************************参数定义*********************************/
//1.波特率

uint32_t UART1_uBAUD = 115200L;						//串口

/**********************************************************************
* 名称 : F10x_UART1_Initl(uint32_t uBAUD)
* 功能 : UART1初始化函数
* 输入 : uBAUD -- 波特率
* 输出 ：无
* 说明 : 无
***********************************************************************/
void F10x_UART1_Initl(uint32_t uBAUD)
{
    GPIO_InitTypeDef GPIO_InitStructure;   			//GPIO端口设置
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	//1.复位UART1
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1	//使能USART1时钟
				|	RCC_APB2Periph_GPIOA
				|	RCC_APB2Periph_GPIOB
				| 	RCC_APB2Periph_GPIOC
				|	RCC_APB2Periph_GPIOD
				|	RCC_APB2Periph_GPIOE, ENABLE);	
 	USART_DeInit(USART1);  							//复位串口1

	//2.UART1端口设置	 
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; 		//USART1_TX/PA.9
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	//复用推挽输出
    GPIO_Init(GPIOA, &GPIO_InitStructure); 			//初始化PA9
   
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;		//USART1_RX/PA.10
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;	//上拉输入
    GPIO_Init(GPIOA, &GPIO_InitStructure);  		//初始化PA10
	
   	//3.USART 初始化设置
	USART_InitStructure.USART_BaudRate = uBAUD;		//波特率
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;		//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;			//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;				//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;	//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

    USART_Init(USART1, &USART_InitStructure); 						//初始化串口

	//4.USART 接收设置
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=3 ;		//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;				//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;					//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);					//根据指定的参数初始化VIC寄存器  
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);	//开启中断

	USART_Cmd(USART1, ENABLE);                    	//使能UART1 			
}
/**********************************************************************
* 名称 : F10x_UART1_SendByte(uint16_t uData)
* 功能 : UART1 单字节数据发送函数
* 输入 : uData -- 数据
* 输出 ：无
* 说明 : 无
***********************************************************************/
void F10x_UART1_SendByte(uint16_t uData)
{
	USART_SendData(USART1, (uint8_t)uData);
	while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);	
}
/**********************************************************************
* 名称 : F10x_UART1_SendString(char *str)
* 功能 : UART1 字符串发送函数
* 输入 : *str -- 字符串
* 输出 ：无
* 说明 : 无
***********************************************************************/
void F10x_UART1_SendString(char *s)
{
	unsigned int k = 0;
	do 
	{
		F10x_UART1_SendByte(*(s + k));
		k++;
	}while(*(s + k)!='\0');
}
/**********************************************************************
* 名称 : USART1_IRQHandler(void)
* 功能 : 串口1中断服务程序
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void USART1_IRQHandler(void)                		//串口1中断服务程序
{	
	volatile uint8_t rx_value = 0x00;
	//1.串口接收中断
	if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)		//接收中断
	{
		rx_value = USART_ReceiveData(USART1);		//(USART1->DR);	//读取接收到的数据	
	}
}
/**********************************************************************
* 名称 : fputc(int ch, FILE *f)
* 功能 : printf重定函数
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
int fputc(int ch, FILE *f)
{
	USART_SendData(USART1, (uint8_t) ch);				
	while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);	
	return ch;
}



