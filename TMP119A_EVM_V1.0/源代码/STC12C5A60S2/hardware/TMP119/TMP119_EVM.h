#ifndef  __TMP119_EVM_H__
#define  __TMP119_EVM_H__

#include "ANT_I2C.h"
#include "ANT_UART1.h"
#include "delay.h"
/*****************************端口定义*********************************/
//1.ALERT端口

sbit ALERT = P2^2;							//过热警报或数据就绪信号

/****************************寄存器定义********************************/
//1.TMP119寄存器定义

typedef enum {
	REG_Temp_Result = 0x00,					//00h R 8000h Temp_Result 温度结果寄存器
	REG_Configuration,						//01h R/W 0220h(1) Configuration 配置寄存器
	REG_THigh_Limit,						//02h R/W 6000h(1) THigh_Limit 温度上限寄存器
	REG_TLow_Limit,							//03h R/W 8000h(1) TLow_Limit 温度下限寄存器
	REG_EEPROM_UL,							//04h R/W 0000h EEPROM_UL EEPROM 解锁寄存器
	REG_EEPROM1,							//05h R/W xxxxh(1) EEPROM1 EEPROM1 寄存器
	REG_EEPROM2,							//06h R/W xxxxh(1) EEPROM2 EEPROM2 寄存器
	REG_Temp_Offset,						//07h R/W 0000h(1) Temp_Offset 温度偏移寄存器
	REG_EEPROM3,							//08h R/W xxxxh(1) EEPROM3 EEPROM3 寄存器
	REG_Device_ID = 0x0F					//0Fh R 0117h Device_ID 器件 ID 寄存器	
}REG_Def;

//2.7.6.2 温度寄存器（地址 = 00h）[默认复位 = 8000h]

typedef union REG_RESULT											
{											
	unsigned short WORD;					//WORD Access 			
	struct 										
	{										//Byte Access	
		unsigned char H;					
		unsigned char L;					
	}	BYTE;
}RESULT;

extern union REG_RESULT RESULT_REG;

//3.7.6.3 配置寄存器（地址 = 01h）[出厂默认复位 = 0220h]

typedef union REG_CFG  											
{											
	unsigned int WORD;						//WORD Access 			
	struct 										
	{										//Byte Access	
		unsigned char H;					
		unsigned char L;					
	} BYTE;													
	struct 									//Bit  Access
	{	
	
		unsigned char HIGH_Alert :1;
		unsigned char LOW_Alert  :1;
		unsigned char Data_Ready :1;
		unsigned char EEPROM_Busy:1;
		unsigned char MOD1_0     :2;
		unsigned char CONV2_0    :3;
		unsigned char AVG1_0     :2;
		unsigned char T_nA       :1; 
		unsigned char POL        :1;
		unsigned char DR_Alert   :1;
		unsigned char Soft_Reset :1;										
		unsigned char NA         :1;				
	} BIT;
}CFG;

extern union REG_CFG CFG_REG;

typedef enum {
	CC1_MODE = 0x00,
	SD_MODE,
	CC2_MODE,
	OS_MODE
}MOD_Def;

typedef enum {								//平均次数
	AVG_NULL = 0x00,
	AVG_8,
	AVG_32,
	AVG_64
}AVG_Def;

//4.7.6.4 上限寄存器（地址 = 02h）[出厂默认复位 = 6000h]

typedef union REG_H_LIMIT											
{											
	unsigned short WORD;					//WORD Access 			
	struct 										
	{										//Byte Access	
		unsigned char H;					
		unsigned char L;					
	}	BYTE;
}H_LIMIT; 

extern union REG_H_LIMIT H_LIMIT_REG;

//5.7.6.5 下限寄存器（地址 = 03h）[出厂默认复位 = 8000h]

typedef union REG_L_LIMIT											
{											
	unsigned short WORD;					//WORD Access 			
	struct 										
	{										//Byte Access	
		unsigned char H;					
		unsigned char L;					
	}	BYTE;
}L_LIMIT; 

extern union REG_L_LIMIT L_LIMIT_REG;

//6.7.6.6 EEPROM 解锁寄存器（地址 = 04h）[复位 = 0000h]

typedef union REG_EEPROM_UL 											
{											
	unsigned int WORD;						//WORD Access 			
	struct 										
	{										//Byte Access	
		unsigned char H;					
		unsigned char L;					
	} BYTE;													
	struct 									//Bit  Access
	{
		unsigned char EUN        :1;		//EEPROM 解锁
		unsigned char EEPROM_Busy:1;		//EEPROM 忙碌	
		unsigned char            :6;										
		unsigned char            :8;			
	} BIT;
}EEPROM_UL;

extern union REG_EEPROM_UL EEPROM_UL_REG;

//7.7.6.7 EEPROM1 寄存器（地址 = 05h）[复位 = XXXXh]

typedef union REG_EEPROM1											
{											
	unsigned short WORD;					//WORD Access 			
	struct 										
	{										//Byte Access	
		unsigned char H;					
		unsigned char L;					
	}	BYTE;
}EEPROM1; 

extern union REG_EEPROM1 EEPROM1_REG;

//8.7.6.8 EEPROM2 寄存器（地址 = 06h）[复位 = 0000h]

typedef union REG_EEPROM2											
{											
	unsigned short WORD;					//WORD Access 			
	struct 										
	{										//Byte Access	
		unsigned char H;					
		unsigned char L;					
	}	BYTE;
}EEPROM2; 

extern union REG_EEPROM2 EEPROM2_REG;

//9.7.6.9 温度偏移寄存器（地址 = 07h）[复位 = 0000h]

typedef union REG_T_OFFSET											
{											
	unsigned short WORD;					//WORD Access 			
	struct 										
	{										//Byte Access	
		unsigned char H;					
		unsigned char L;					
	}	BYTE;
}T_OFFSET; 

extern union REG_T_OFFSET T_OFFSET_REG;

//10.7.6.10 EEPROM3 寄存器（地址 = 08h）[复位 = xxxxh]

typedef union REG_EEPROM3											
{											
	unsigned short WORD;					//WORD Access 			
	struct 										
	{										//Byte Access	
		unsigned char H;					
		unsigned char L;					
	}	BYTE;
}EEPROM3; 

extern union REG_EEPROM3 EEPROM3_REG;

//11.7.6.11 器件 ID 寄存器（地址 = 0Fh）[复位 = 0117h]

typedef union REG_DEVICE_ID 											
{											
	unsigned int WORD;						//WORD Access 			
	struct 										
	{										//Byte Access	
		unsigned char H;					
		unsigned char L;					
	} BYTE;													
	struct 									//Bit  Access
	{
		unsigned char REV    :4;			//版本号	
		unsigned char DID11_8:4;										
		unsigned char DID7_0 :8;			//器件ID			
		
	} BIT;
}DEVICE_ID;

extern union REG_DEVICE_ID DEVICE_ID_REG;

/*****************************参数定义*********************************/
//1.芯片地址

#define TMP_SLA			0x90				//Slave Addr(8bit)

//2.温度参数

#define TEMP_LSB		0.0078125f			//单位数据对应的温度值	

/*****************************接口函数*********************************/

void TMP119_Write_Register(uint8_t nSLA,REG_Def nReg,uint16_t value);		//写指定寄存器函数
uint16_t TMP119_Read_Register(uint8_t nSLA,REG_Def nReg);	//读指定寄存器函数
void TMP119_SoftReset(void);				//软复位函数
void TMP119_Write_CFG_Register(MOD_Def cMode,uint8_t cRate,AVG_Def nAVG);	//写配置寄存器函数
void TMP119_Write_TL_Limit_Register(REG_Def nReg,float Temp);	//上下限设置函数
uint8_t TMP119_Write_EEPROM(REG_Def nReg,uint16_t value);	//写EEPROM函数
uint16_t TMP119_Read_EEPROM(REG_Def nReg);	//读EEPROM函数
void TMP119_Write_Temp_Offset_Register(float Temp);			//写温度偏移寄存器
uint16_t TMP119_Read_Device_ID(void);		//读取芯片ID号
void TMP_Initl(void);						//初始化
uint8_t TMP119_Data_Ready(void);			//TMP119转换完成检测
float TMP119_Read_Temperature(void);		//温度读取函数	

#endif




