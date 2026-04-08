#ifndef  __F10X_KEY_H__
#define  __F10X_KEY_H__

#include "F10X_TIM3.h"
#include "F10x_UART1.h"
#include "Board_Driver.h"
/*****************************端口定义*********************************/
//1.按键端口定义

#define KEY1		PDin(8)				//KEY1
#define KEY2		PDin(9)				//KEY2
#define KEY3		PCin(5)				//KEY3
#define KEY4		PBin(0)				//KEY4
#define KEY5		PBin(1)				//KEY5

/*****************************按键参数*********************************/

#define KEY_NUM 5  						//定义按键数量

//1.按键状态枚举

typedef enum {
    KEY_STATE_IDLE,         			//空闲状态，无按键动作
    KEY_STATE_PRESS_DOWN,   			//按键按下状态(消抖中)
    KEY_STATE_PRESS,        			//按键持续按下状态
    KEY_STATE_WAIT_RELEASE, 			//等待长按释放状态
    KEY_STATE_WAIT_DOUBLE   			//等待第二次按键(检测双击)
} KeyState;

//2.按键事件枚举

typedef enum {
    KEY_EVENT_NONE = 0,     			// 无事件
    KEY_EVENT_CLICK,        			// 单击事件
    KEY_EVENT_DOUBLE,       			// 双击事件
    KEY_EVENT_LONG_PRESS,   			// 长按事件
} KeyEvent;

//3.按键结构体定义

typedef struct {
    KeyState state;        			 	// 当前按键状态
    KeyEvent event;         			// 触发的按键事件
    uint32_t press_tick;    			// 按下时刻的tick值(用于时间计算)
    uint8_t click_count;    			// 连续点击次数计数器
} Key_TypeDef;

//4.时间阈值定义(单位ms)

#define DEBOUNCE_TIME    	30    		//消抖时间30ms
#define CLICK_TIME      	300    		//单击判定时间300ms
#define LONG_PRESS_TIME 	1000   		//长按判定时间1000ms
#define DOUBLE_CLICK_TIME 	300  		//双击间隔时间300ms

/*****************************接口函数*********************************/

void F10x_Key_Initl(void);				//按键端口初始化
uint8_t Read_KeyState(uint8_t nKey);	//获取指定按键的状态
void Key_Scan(void);					//按键扫描函数(在定时器中断中调用)
KeyEvent Key_GetEvent(uint8_t key_id);	//按键事件管理
void Board_Key_Manage(void);			//按键管理

#endif



