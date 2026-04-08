/**********************************************************************						
* 公司名称 : 东莞市上古时代电子技术有限公司
* 模块名称 ：UART1
* 系统主频 : 168.0MHz
* 创建人   : 向 恒
* 修改人   : 向 恒  
* 版本号   : Ver 1.0
* 创建日期 : 2022年1月16日
                            版权所有 @ 上古时代
***********************************************************************/

#include "F40x_UART1.h"

/********************************参数定义******************************/
//1.串口参数

uint32_t UART1_BAUD = 115200L;									//波特率

/**********************************************************************
* 名称 : F40x_UART1_Initl(uint32_t uBAUD)
* 功能 : UART1初始化
* 输入 : uBAUD -- 波特率
* 输出 ：无
* 说明 : 无
***********************************************************************/
void F40x_UART1_Initl(uint32_t uBAUD)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	
	//1.UART1端口初始化
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA					//使能GPIO时钟
						 | RCC_AHB1Periph_GPIOB
						 | RCC_AHB1Periph_GPIOC
						 | RCC_AHB1Periph_GPIOD
						 | RCC_AHB1Periph_GPIOE,ENABLE); 		
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1,ENABLE);		//使能USART1时钟
	
	//2.串口1对应引脚复用映射
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource9,GPIO_AF_USART1); 	//GPIOA9 复用为USART1
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource10,GPIO_AF_USART1); 	//GPIOA10复用为USART1	
	
	//3.USART1端口配置
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10; 	//GPIOA9与GPIOA10
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;				//复用功能
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;			//速度50MHz
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP; 				//推挽复用输出
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; 				//上拉
	GPIO_Init(GPIOA,&GPIO_InitStructure); 						//初始化PA9，PA10
	
	//4.USART1 初始化设置
	USART_InitStructure.USART_BaudRate = uBAUD;					//波特率设置
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;	//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;		//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;			//无奇偶校验位i
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;	//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;					//收发模式
	USART_Init(USART1, &USART_InitStructure); 					//初始化串口1
	USART_Cmd(USART1, ENABLE);  								//使能串口1 
	
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);				//开启相关中断

	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;			//串口1中断通道
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;	//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;			//子优先级2
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;				//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);								//根据指定的参数初始化VIC寄存器、
}
/**********************************************************************
* 名称 : F40x_UART1_WR_Byte(uint16_t _1Byte)
* 功能 : UART1单字节数据发送函数
* 输入 : _1Byte -- 单字节数据
* 输出 ：无
* 说明 : 无
***********************************************************************/
void F40x_UART1_WR_Byte(uint16_t _1Byte)
{
	USART_SendData(USART1,_1Byte);         						//向串口1发送数据
	while(USART_GetFlagStatus(USART1,USART_FLAG_TC) != SET);	//等待发送结束
}
/**********************************************************************
* 名称 : F40x_UART1_WR_String(uint8_t *s)
* 功能 : UART1字符串发送函数
* 输入 : UART1_BAUD -- 波特率
* 输出 ：无
* 说明 : 无
***********************************************************************/
void F40x_UART1_WR_String(uint8_t *s)
{
	while(0 != *s)                  							//等待发送完成
	{
		F40x_UART1_WR_Byte(*s);	        		 				//写数据
		s ++;
	}  	
}
/**********************************************************************
* 名称 : USART1_IRQHandler(void)
* 功能 : UART1中断服务程序
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void USART1_IRQHandler(void)                		
{
	volatile uint8_t rx_value = 0x00;
	//1.接收中断
	if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)  		//接收中断(接收到的数据必须是0x0d 0x0a结尾)
	{
		rx_value = USART_ReceiveData(USART1);					//(USART1->DR);	//读取接收到的数据	
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





