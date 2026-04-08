/**********************************************************************	 
* 公司名称 : 东莞市上古时代电子技术有限公司
* 模块名称 ：UART1 驱动
* 系统主频 : 22.1184 MHz
* 创建人   : eysmcu
* 修改人   : eysmcu 
* 创建日期 : 2025年5月4日
* 店铺地址 : http://mindesigner.taobao.com
* 淘宝ID号 : eysmcu
* 重要声明 ：本例程为东莞市上古时代电子技术有限公司原创，本程序只供学习
使用，未经作者许可，不得用于其它任何用途，转载清注明出处！！！


						 Copyright(C) 上古时代
***********************************************************************/

#include "ANT_UART1.h"

/****************************参数定义*********************************/
//1.串口标志位

bit u1_Busy = Idle;					

/**********************************************************************
* 名称 : ANT_UART1_Initl(uint32_t uBaud)
* 功能 : 串口1初始化函数
* 输入 : uBaud -- 波特率
* 输出 ：无
* 说明 : 无
***********************************************************************/
void ANT_UART1_Initl(uint32_t uBaud)
{
#if (PARITYBIT == NONE_PARITY)
    SCON = 0x50;            //8-bit variable UART
#elif (PARITYBIT == ODD_PARITY) || (PARITYBIT == EVEN_PARITY) || (PARITYBIT == MARK_PARITY)
    SCON = 0xda;            //9-bit variable UART, parity bit initial to 1
#elif (PARITYBIT == SPACE_PARITY)
    SCON = 0xd2;            //9-bit variable UART, parity bit initial to 0
#endif

    TMOD = 0x20;            //Set Timer1 as 8-bit auto reload mode
    TH1 = TL1 = -(F_cpu / 12 / 32 / uBaud); //Set auto-reload vaule
    TR1 = 1;                //Timer1 start run
    ES = 1;                 //Enable UART interrupt
    EA = 1;                 //Open master interrupt switch
	TI = 1;					//printf

	u1_Busy = Idle;
}
/**********************************************************************
* 名称 : ANT_UART1_SendByte(uint8_t value)
* 功能 : 串口1单字节发送函数
* 输入 : value -- 数据
* 输出 ：无
* 说明 : 无
***********************************************************************/
void ANT_UART1_SendByte(uint8_t value)
{
    while (u1_Busy);     	//Wait for the completion of the previous data is sent
    ACC = value;         	//Calculate the even parity bit P (PSW.0)
    if (P)                  //Set the parity bit according to P
    {
#if (PARITYBIT == ODD_PARITY)
        TB8 = 0;            //Set parity bit to 0
#elif (PARITYBIT == EVEN_PARITY)
        TB8 = 1;            //Set parity bit to 1
#endif
    }
    else
    {
#if (PARITYBIT == ODD_PARITY)
        TB8 = 1;            //Set parity bit to 1
#elif (PARITYBIT == EVEN_PARITY)
        TB8 = 0;            //Set parity bit to 0
#endif
    }
    u1_Busy = Busy;
    SBUF = ACC;             //Send data to UART buffer
}
/**********************************************************************
* 名称 : ANT_UART1_SendString(char *s)
* 功能 : 串口1字符串发送函数
* 输入 : *s -- 字符串
* 输出 ：无
* 说明 : 无
***********************************************************************/
 void ANT_UART1_SendString(char *s)
{
    while (*s)              //Check the end of the string
    {
        ANT_UART1_SendByte(*s++);     //Send current char and increment string ptr
    }
}
/**********************************************************************
* 名称 : ANT_UART1_ISR(void) interrupt 4
* 功能 : 串口1中断服务程序
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void ANT_UART1_ISR(void) interrupt 4
{
	//1.接收中断
    if (RI)
    {
        RI = 0;             //Clear receive interrupt flag
    }
	//2.发送中断
    else if (TI)
    {
        TI = 0;             //Clear transmit interrupt flag
       u1_Busy = Idle;    	//Clear transmit busy flag
    }
}
/**********************************************************************
* 名称 : putchar(char c)
* 功能 : 重写printf调用的putchar函数，重定向到串口输出
* 输入 : 无
* 输出 ：无
* 说明 : 常用打印格式！！！
printf("char %bd int%d long %ld\n",a,b,c);
printf("Uchar %bu Uint %u ulong %lu\n",x,y,z);
printf("xchar %bx xint %x xlong %lx\n",x,y,z);
printf("string %s is at address %p\n",buf,p);
printf("%f != %g\n",f,g);printf("%*f!=%*g\n",8,f,8,g);
***********************************************************************/
char putchar(char c)
{
	ANT_UART1_SendByte(c);
	return c;
}



