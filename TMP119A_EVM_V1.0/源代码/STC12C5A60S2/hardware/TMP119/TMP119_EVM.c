/**********************************************************************	 
* 公司名称 : 东莞市上古时代电子技术有限公司
* 模块名称 ：TMP119 驱动程序
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

#include "TMP119_EVM.h"

/***************************寄存器定义*********************************/

union REG_RESULT RESULT_REG;				//转换结果寄存器
union REG_CFG CFG_REG;						//配置寄存器
union REG_H_LIMIT H_LIMIT_REG;				//上限寄存器
union REG_L_LIMIT L_LIMIT_REG;				//下限寄存器
union REG_EEPROM_UL EEPROM_UL_REG;			//EEPROM 解锁寄存器
union REG_EEPROM1 EEPROM1_REG;				//EEPROM1 寄存器
union REG_EEPROM2 EEPROM2_REG;				//EEPROM2 寄存器
union REG_T_OFFSET T_OFFSET_REG;			//温度偏移寄存器
union REG_EEPROM3 EEPROM3_REG;				//EEPROM3 寄存器
union REG_DEVICE_ID DEVICE_ID_REG;			//器件 ID 寄存器

/**********************************************************************
* 名称 : TMP119_Write_Register(uint8_t nSLA,REG_Def nReg,uint16_t value)
* 功能 : 写寄存器函数
* 输入 : nSLA -- 设备地址
		 nReg -- 寄存器
		 value -- 数据
* 输出 ：无
* 说明 : 无
***********************************************************************/
void TMP119_Write_Register(uint8_t nSLA,REG_Def nReg,uint16_t value)
{
	I2C_Buffer[0] = nReg;					//选择要写入的寄存器
	I2C_Buffer[1] = value >> 8;		
	I2C_Buffer[2] = value >> 0;
	ANT_I2C_Wtite_nByte(nSLA,I2C_Buffer,3);	
}
/**********************************************************************
* 名称 : TMP119_Read_Register(uint8_t nSLA,REG_Def nReg)
* 功能 : 从指定寄存器读数据
* 输入 : nSLA -- 设备地址
		 nReg -- 指定寄存器
* 输出 ：reg_value -- 寄存器数据
* 说明 : 无
***********************************************************************/
uint16_t TMP119_Read_Register(uint8_t nSLA,REG_Def nReg)
{
	uint16_t reg_value = 0x00;

	I2C_Buffer[0] = nReg;					//选择要读取的寄存器
	ANT_I2C_Wtite_nByte(nSLA,I2C_Buffer,1);	//写Pointer Register
	delay_15us();							//延时15us
	ANT_I2C_Read_nByte(nSLA,I2C_Buffer,2);	//读寄存器
	
	reg_value = I2C_Buffer[0];				//组合数据 
	reg_value = (reg_value << 8) + I2C_Buffer[1];
	
	return reg_value;
}
/**********************************************************************
* 名称 : TMP119_SoftReset(void)
* 功能 : 软复位函数
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void TMP119_SoftReset(void)
{
	CFG_REG.WORD = 0x00;					//复位寄存器
	CFG_REG.BIT.Soft_Reset = 1;				//设置软复位
	
	TMP119_Write_Register(TMP_SLA,REG_Configuration,CFG_REG.WORD);				//启动软复位
}
/**********************************************************************
* 名称 : TMP119_Write_CFG_Register(MOD_Def cMode,uint8_t cRate,AVG_Def nAVG)
* 功能 : 写配置寄存器函数
* 输入 : cMode -- 转换模式
		 cRate -- 转换周期
		 nAVG -- 均值计算模式
* 输出 ：无
* 说明 : 无
***********************************************************************/
void TMP119_Write_CFG_Register(MOD_Def cMode,uint8_t cRate,AVG_Def nAVG)
{
	CFG_REG.WORD = 0X00;					//复位寄存器
	
	//1.设置配置寄存器参数
	CFG_REG.BIT.MOD1_0 = cMode;				//转换模式
	CFG_REG.BIT.CONV2_0 = cRate;			//转换周期
	CFG_REG.BIT.AVG1_0 = nAVG;				//均值计算模式
	CFG_REG.BIT.T_nA = 0;					//报警模式
	CFG_REG.BIT.POL = 0;					//低电平有效
	CFG_REG.BIT.DR_Alert = 0;				//ALERT 引脚反映警报标志的状态
	CFG_REG.BIT.NA = 0;						//未使用，直接赋值0

	//2.写入配置寄存器
	TMP119_Write_Register(TMP_SLA,REG_Configuration,CFG_REG.WORD);		
}
/**********************************************************************
* 名称 : TMP119_Write_TL_Limit_Register(REG_Def nReg,float Temp)
* 功能 : 温度上/下限设置函数
* 输入 : nReg -- 寄存器
		 Temp -- 温度值
* 输出 ：无
* 说明 : 无
***********************************************************************/
void TMP119_Write_TL_Limit_Register(REG_Def nReg,float Temp)
{
	if(nReg == REG_THigh_Limit || nReg == REG_TLow_Limit)
	{
		TMP119_Write_Register(TMP_SLA,nReg,(int16_t)((float)Temp / TEMP_LSB));	//写入寄存器	
	}
}
/**********************************************************************
* 名称 : TMP119_Write_EEPROM(REG_Def nReg,uint16_t value)
* 功能 : 写EEPROM寄存器函数
* 输入 : nReg -- 寄存器
		 value -- 数据
* 输出 ：状态：0 -- 成功，1 -- 失败
* 说明 : 无
***********************************************************************/
uint8_t TMP119_Write_EEPROM(REG_Def nReg,uint16_t value)
{
	EEPROM_UL_REG.WORD = 0x00;				//复位寄存器
	if(nReg == REG_EEPROM2 || nReg == REG_EEPROM3)		//为支持NIST的可追溯性，切勿对EEPROM1进行编程
	{
		EEPROM_UL_REG.BIT.EUN = 1;			//解锁	
		TMP119_Write_Register(TMP_SLA,REG_EEPROM_UL,EEPROM_UL_REG.WORD);		//写入寄存器等待解锁成功
		delay_ms(8);						//等待8ms
		EEPROM_UL_REG.WORD = TMP119_Read_Register(TMP_SLA,REG_EEPROM_UL);		//回读寄存器的值
		if(EEPROM_UL_REG.BIT.EEPROM_Busy == 0)			//解锁成功
		{
			TMP119_Write_Register(TMP_SLA,nReg,value);	//编程EEPROM
		}
		else return 1;						//解锁失败
	}
	return 0;
}
/**********************************************************************
* 名称 : TMP119_Read_EEPROM(REG_Def nReg)
* 功能 : 读EEPROM寄存器函数
* 输入 : nReg -- 寄存器
* 输出 ：EEPROM数据
* 说明 : 无
***********************************************************************/
uint16_t TMP119_Read_EEPROM(REG_Def nReg)
{
	if(nReg == REG_EEPROM1 || nReg == REG_EEPROM2 || nReg == REG_EEPROM3)		
	{
		return(TMP119_Read_Register(TMP_SLA,nReg));//读取EEPROM数据
	}	
	else return 0;
}
/**********************************************************************
* 名称 : TMP119_Write_Temp_Offset_Register(float Temp)
* 功能 : 写温度偏移寄存器
* 输入 : Temp -- 温度值
* 输出 ：无
* 说明 : 无
***********************************************************************/
void TMP119_Write_Temp_Offset_Register(float Temp)
{
	TMP119_Write_Register(TMP_SLA,REG_Temp_Offset,(int16_t)((float)Temp / TEMP_LSB));
}
/**********************************************************************
* 名称 : TMP119_Read_Device_ID(void)
* 功能 : 读取芯片ID号
* 输入 : 无
* 输出 ：ID号(默认0x117)
* 说明 : 无
***********************************************************************/
uint16_t TMP119_Read_Device_ID(void)
{
	return TMP119_Read_Register(TMP_SLA,REG_Device_ID);
}
/**********************************************************************
* 名称 : TMP_Initl(void)
* 功能 : TMP119初始化函数
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
void TMP_Initl(void)
{
	//1.复位芯片
	TMP119_SoftReset();			delay_ms(10);
	
	//2.初始化TMP119
	TMP119_Write_CFG_Register(CC1_MODE,0,AVG_32);	//0.5S输出一个(参考芯片手册，表8-6. Conversion Cycle Time in CC Mode)	
}
/**********************************************************************
* 名称 : TMP119_Data_Ready(void)
* 功能 : TMP119转换完成检测
* 输入 : 无
* 输出 ：无
* 说明 : 0 -- 转换完成,1 -- 正在转换
***********************************************************************/
uint8_t TMP119_Data_Ready(void)
{
 	if((TMP119_Read_Register(TMP_SLA,REG_Configuration) & 0x2000) == 0x2000)
		return 1;
	else return 0;
}
/**********************************************************************
* 名称 : TMP119_Read_Temperature(void)
* 功能 : TMP119温度读取函数
* 输入 : 无
* 输出 ：无
* 说明 : 无
***********************************************************************/
float TMP119_Read_Temperature(void)
{
	int16_t reg_value = 0x00;
	
	while(TMP119_Data_Ready() != 0);		//等待转换完成
	reg_value = TMP119_Read_Register(TMP_SLA,REG_Temp_Result);

	return (float)(reg_value * TEMP_LSB);	//转换成温度输出
}






