/**********************************************************************	 
* 公司名称 : 东莞市上古时代电子技术有限公司
* 模块名称 ：独立按键(状态机+定时器)
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

#include "F10x_Key.h"

/*****************************参数定义*********************************/
//1.按键定义

Key_TypeDef keys[KEY_NUM];  					// 按键对象数组
static uint32_t key_tick = 0; 					// 全局时间计数器(每10ms增加1)

/**********************************************************************
* 名称 : F10x_Key_Initl(void)
* 功能 : 独立按键端口初始化
* 输入 : 无
* 输出 : 无
* 说明 : 无
***********************************************************************/ 
void F10x_Key_Initl(void)
{
	//1.初始化按键端口
	F10x_GPIO_Port_Initl(GPIOD,GPIO_Pin_8,GPIO_Speed_50MHz,GPIO_Mode_IN_FLOATING,Bit_SET);	//KEY1
	F10x_GPIO_Port_Initl(GPIOD,GPIO_Pin_9,GPIO_Speed_50MHz,GPIO_Mode_IN_FLOATING,Bit_SET);	//KEY2
	F10x_GPIO_Port_Initl(GPIOC,GPIO_Pin_5,GPIO_Speed_50MHz,GPIO_Mode_IN_FLOATING,Bit_SET);	//KEY3
	F10x_GPIO_Port_Initl(GPIOB,GPIO_Pin_0,GPIO_Speed_50MHz,GPIO_Mode_IN_FLOATING,Bit_SET);	//KEY4
	F10x_GPIO_Port_Initl(GPIOB,GPIO_Pin_1,GPIO_Speed_50MHz,GPIO_Mode_IN_FLOATING,Bit_SET);	//KEY5
	
	//2.初始化按键参数
    for(uint8_t i = 0; i < KEY_NUM; i ++) 
	{
        keys[i].state = KEY_STATE_IDLE;
        keys[i].event = KEY_EVENT_NONE;
        keys[i].press_tick = 0;
        keys[i].click_count = 0;
    }
	
	//3.初始化定时器
	F10x_TIM3_Initl(10000 - 1,72);				//10ms中断一次	
}
/**********************************************************************
* 名称 : Read_KeyState(uint8_t nKey)
* 功能 : 获取指定按键的状态
* 输入 : nKey -- 按键编号
* 输出 ：无
* 说明 : 无
***********************************************************************/
uint8_t Read_KeyState(uint8_t nKey)
{
	uint8_t Key_State = 0x00;
	
	switch(nKey)
	{
		case 0:Key_State = GPIO_ReadInputDataBit(GPIOD,GPIO_Pin_8);
			break;
		case 1:Key_State = GPIO_ReadInputDataBit(GPIOD,GPIO_Pin_9);
			break;
		case 2:Key_State = GPIO_ReadInputDataBit(GPIOC,GPIO_Pin_5);
			break;
		case 3:Key_State = GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_0);
			break;
		case 4:Key_State = GPIO_ReadInputDataBit(GPIOB,GPIO_Pin_1);
			break;
		default:
			break;
	}
	return Key_State;
}
/**********************************************************************
* 名称 : Key_Scan(void)
* 功能 : 按键状态扫描函数
* 输入 : 无
* 输出 ：无
* 说明 : 需在10ms定时中断中调用，使用状态机实现按键检测
***********************************************************************/
void Key_Scan(void) 
{
    key_tick ++;  								//每调用一次增加10ms
    
    for (int i = 0; i < KEY_NUM; i++) 			//读取按键当前状态(按下为RESET)
	{	
        uint8_t key_val = Read_KeyState(i) == RESET;
        
        switch (keys[i].state) 
		{
			case KEY_STATE_IDLE:if (key_val) 	//检测到按键按下
							{
								keys[i].state = KEY_STATE_PRESS_DOWN;
								keys[i].press_tick = key_tick; 				//记录按下时刻
							}
            break;         
			case KEY_STATE_PRESS_DOWN:			//消抖时间到(30ms)
							if ((key_tick - keys[i].press_tick) >= DEBOUNCE_TIME / 10) 
							{
								if (key_val)	//确认按键按下，进入持续按下状态 
								{
									keys[i].state = KEY_STATE_PRESS;
								} else {		//抖动，返回空闲状态
									
									keys[i].state = KEY_STATE_IDLE;
								}
							}
				break;           
			case KEY_STATE_PRESS:			 	//按键释放
							if (!key_val) 		//判断是否为长按释放
							{
								if ((key_tick - keys[i].press_tick) >= LONG_PRESS_TIME / 10) 
								{
									keys[i].event = KEY_EVENT_LONG_PRESS;
									keys[i].click_count = 0;  				//长按后重置点击计数
								} 							
								else 			//短按释放
								{			
									keys[i].click_count++;					//第一次点击，进入等待双击状态
									
									if (keys[i].click_count == 1) 
									{
										keys[i].state = KEY_STATE_WAIT_DOUBLE;
										keys[i].press_tick = key_tick; 		//重置计时
									} 								
									else if (keys[i].click_count == 2) 		//第二次点击，触发双击事件
									{
										keys[i].event = KEY_EVENT_DOUBLE;
										keys[i].click_count = 0;  			//重置点击计数
										keys[i].state = KEY_STATE_IDLE;
									}
								}
							} 
							else if ((key_tick - keys[i].press_tick) >= LONG_PRESS_TIME / 10)			// 持续按下达到长按时间
							{
								keys[i].event = KEY_EVENT_LONG_PRESS;
								keys[i].click_count = 0;  					//长按后重置点击计数
								keys[i].state = KEY_STATE_WAIT_RELEASE;
							}
				break;
			case KEY_STATE_WAIT_DOUBLE:			//双击等待超时(300ms)				
							if ((key_tick - keys[i].press_tick) >= DOUBLE_CLICK_TIME / 10) 
							{
								keys[i].event = KEY_EVENT_CLICK;  			//触发单击事件
								keys[i].click_count = 0;
								keys[i].state = KEY_STATE_IDLE;
							} 
							else if (key_val) 	// 在等待期间再次按下
							{
								keys[i].state = KEY_STATE_PRESS;  			//返回按下状态
							}
							break;
				
			case KEY_STATE_WAIT_RELEASE:		// 长按后等待释放				
							if (!key_val) 
							{
								keys[i].state = KEY_STATE_IDLE;
							}
				break;
        }
    }
}
/**********************************************************************
* 名称 : Key_GetEvent(uint8_t key_id) 
* 功能 : 获取按键事件
* 输入 : key_id 按键编号(0-4)
* 输出 ：按键事件(KEY_EVENT枚举值)
* 说明 : 调用后会清除该按键的事件标志
***********************************************************************/
KeyEvent Key_GetEvent(uint8_t key_id) 
{
    if (key_id >= KEY_NUM) return KEY_EVENT_NONE;
	
    KeyEvent event = keys[key_id].event;
    keys[key_id].event = KEY_EVENT_NONE;  		//读取后清除事件
	
    return event;
}
/**********************************************************************
* 名称 : Board_Key_Manage(void)
* 功能 : 按键管理
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void Board_Key_Manage(void)
{
	//1.检测按键事件处理
	for (uint8_t key_id = 0; key_id < KEY_NUM; key_id++) 
	{
		KeyEvent event = Key_GetEvent(key_id);
		if(event == KEY_EVENT_CLICK)			//单击
		{
			printf("KEY%d Click\r\n", key_id + 1);
		}
		else if(event == KEY_EVENT_DOUBLE)		//双击
		{
			printf("KEY%d Double Click\r\n", key_id + 1);
		}
		else if(event == KEY_EVENT_LONG_PRESS)	//长按
		{
			printf("KEY%d Long Press\r\n", key_id + 1);
		}
	}	
}








