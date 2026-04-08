/**********************************************************************   
* 公司名称 : 东莞市上古时代电子技术有限公司
* 程序名称 : TMP119 EVM V1.0 驱动程序
* 程序编写 : eysmcu
* 创建日期 : 2025年12月15日
* 官方店铺 : http://mindesigner.taobao.com
* 淘宝ID号 : eysmcu
* 软件版本 : Arduino 1.8.19
* 重要声明 ：本例程为东莞市上古时代电子技术有限公司原创，本程序只供学习使用，未经作者许
* 可，不得用于其它任何用途，转载清注明出处！！！

                      Copyright(C)  @  上古时代
***********************************************************************/

#include <Wire.h>

/*****************************链接方式说明******************************/
//  Arduino MEGA 2560          TMP119 EVM
//       SDA (20)     <-->         SDA
//       SCL (21)     <-->         SCL 
//           (19)     <-->         ALT   
//       +5V          <-->         VDD
//       GND          <-->         GND
// 使用串口助手打印温度值，串口参数：9600,8,1+NONE,设置为字符显示！！！！
/****************************端口/参数定义******************************/
//1.端口定义

const int ALT_PIN = 19;               //转换完成指示

//2.TMP119寄存器定义

typedef enum {
  REG_Temp_Result = 0x00,             //00h R 8000h Temp_Result
  REG_Configuration,                  //01h R/W 0220h(1) Configuration
  REG_THigh_Limit,                    //02h R/W 6000h(1) THigh_Limit
  REG_TLow_Limit,                     //03h R/W 8000h(1) TLow_Limit
  REG_EEPROM_UL,                      //04h R/W 0000h EEPROM_UL EEPROM
  REG_EEPROM1,                        //05h R/W xxxxh(1) EEPROM1 EEPROM1
  REG_EEPROM2,                        //06h R/W xxxxh(1) EEPROM2 EEPROM2
  REG_Temp_Offset,                    //07h R/W 0000h(1) Temp_Offset
  REG_EEPROM3,                        //08h R/W xxxxh(1) EEPROM3 EEPROM3
  REG_Device_ID = 0x0F                //0Fh R 0117h Device_ID  
}REG_Def;

//3.温度寄存器（地址 = 00h）[默认复位 = 8000h]

typedef union REG_RESULT                      
{                     
  unsigned short WORD;                //WORD Access       
  struct                    
  {                                   //Byte Access 
    unsigned char L;          
    unsigned char H;          
  } BYTE;
}RESULT;

//4.配置寄存器（地址 = 01h）[出厂默认复位 = 0220h]

typedef union REG_CFG                        
{                     
  unsigned int WORD;                  //WORD Access       
  struct                    
  {                                   //Byte Access 
    unsigned char L;          
    unsigned char H;          
  } BYTE;                         
  struct                              //Bit  Access
  {                     
    unsigned char            :1;      
    unsigned char Soft_Reset :1;
    unsigned char DR_Alert   :1;
    unsigned char POL        :1;
    unsigned char T_nA       :1;      
    unsigned char AVG1_0     :2;      
    unsigned char CONV2_0    :3;        
    unsigned char MOD1_0     :2;     
    unsigned char EEPROM_Busy:1;
    unsigned char Data_Ready :1;
    unsigned char LOW_Alert  :1;
    unsigned char HIGH_Alert :1;
  } BIT;
}CFG;

extern union REG_CFG CFG_REG;

typedef enum {
  CC1_MODE = 0x00,
  SD_MODE,
  CC2_MODE,
  OS_MODE
}MOD_Def;

typedef enum {                
  AVG_NULL = 0x00,
  AVG_8,
  AVG_32,
  AVG_64
}AVG_Def;

//5.上限寄存器（地址 = 02h）[出厂默认复位 = 6000h]

typedef union REG_H_LIMIT                      
{                     
  unsigned short WORD;                //WORD Access       
  struct                    
  {                                   //Byte Access 
    unsigned char L;          
    unsigned char H;          
  } BYTE;
}H_LIMIT; 

//6.下限寄存器（地址 = 03h）[出厂默认复位 = 8000h]

typedef union REG_L_LIMIT                      
{                     
  unsigned short WORD;                //WORD Access       
  struct                    
  {                                   //Byte Access 
    unsigned char L;          
    unsigned char H;          
  } BYTE;
}L_LIMIT; 

//7.EEPROM 解锁寄存器（地址 = 04h）[复位 = 0000h]

typedef union REG_EEPROM_UL                       
{                     
  unsigned int WORD;                  //WORD Access       
  struct                    
  {                                   //Byte Access 
    unsigned char L;          
    unsigned char H;          
  } BYTE;                         
  struct                              //Bit  Access
  {                     
    unsigned char            :8;      
    unsigned char            :6;
    unsigned char EEPROM_Busy:1;      
    unsigned char EUN        :1;     
  } BIT;
}EEPROM_UL;

//8.EEPROM1 寄存器（地址 = 05h）[复位 = XXXXh]

typedef union REG_EEPROM1                      
{                     
  unsigned short WORD;                //WORD Access       
  struct                    
  {                                   //Byte Access 
    unsigned char L;          
    unsigned char H;          
  } BYTE;
}EEPROM1; 

//9.EEPROM2 寄存器（地址 = 06h）[复位 = 0000h]

typedef union REG_EEPROM2                      
{                     
  unsigned short WORD;                //WORD Access       
  struct                    
  {                                   //Byte Access 
    unsigned char L;          
    unsigned char H;          
  } BYTE;
}EEPROM2; 

//10.温度偏移寄存器（地址 = 07h）[复位 = 0000h]

typedef union REG_T_OFFSET                      
{                     
  unsigned short WORD;                //WORD Access       
  struct                    
  {                                   //Byte Access 
    unsigned char L;          
    unsigned char H;          
  } BYTE;
}T_OFFSET; 

//11.EEPROM3 寄存器（地址 = 08h）[复位 = xxxxh]

typedef union REG_EEPROM3                      
{                     
  unsigned short WORD;                //WORD Access       
  struct                    
  {                                   //Byte Access 
    unsigned char L;          
    unsigned char H;          
  } BYTE;
}EEPROM3; 

//12.器件 ID 寄存器（地址 = 0Fh）[复位 = 0117h]

typedef union REG_DEVICE_ID                       
{                     
  unsigned int WORD;                  //WORD Access       
  struct                    
  {                                   //Byte Access 
    unsigned char L;          
    unsigned char H;          
  } BYTE;                         
  struct                              //Bit  Access
  {                     
    unsigned char DID7_0 :8;      
    unsigned char DID11_8:4;
    unsigned char REV    :4;    
  } BIT;
}DEVICE_ID;

//13.芯片地址和温度参数

#define TMP_SLA       (0x90 >> 1)     //Slave Addr
#define TEMP_LSB      0.0078125f      //单位数据对应温度值

/***************************寄存器定义*********************************/

union REG_RESULT RESULT_REG;          //转换结果寄存器
union REG_CFG CFG_REG;                //配置寄存器
union REG_H_LIMIT H_LIMIT_REG;        //上限寄存器
union REG_L_LIMIT L_LIMIT_REG;        //下限寄存器
union REG_EEPROM_UL EEPROM_UL_REG;    //EEPROM 解锁寄存器
union REG_EEPROM1 EEPROM1_REG;        //EEPROM1 寄存器
union REG_EEPROM2 EEPROM2_REG;        //EEPROM2 寄存器
union REG_T_OFFSET T_OFFSET_REG;      //温度偏移寄存器
union REG_EEPROM3 EEPROM3_REG;        //EEPROM3 寄存器
union REG_DEVICE_ID DEVICE_ID_REG;    //器件 ID 寄存器

/**********************************************************************
  名称 : setup()
  功能 : 初始化
  输入 : 无
  输出 : 无
  说明 : 无
***********************************************************************/
void setup()
{
  //1.初始化串口
  Serial.begin(9600);
  Serial.println("TMP119 EVM V1.0 Demo\r\n");

  //2.初始化I2C和控制端口
  pinMode(ALT_PIN, INPUT);            //数据转换完成信号
  Wire.begin();                       //初始化I2C
  Wire.setClock(100000);              //设置为100KHz
  delay(10);                          //延时10ms

  //3.初始化TMP119A模块
  TMP_Initl();
  delay(10);                          //延时10ms
}
/**********************************************************************
  名称 : loop()
  功能 : 大循环
  输入 : 无
  输出 : 无
  说明 : 无
***********************************************************************/
void loop()                           // put your main code here, to run repeatedly:
{
  float Temperature = 0x00;
  Temperature = TMP119_Read_Temperature();  //读取转换温度
  Serial.print("Temperature = ");
  Serial.println(Temperature, 2);     //结果保留2位小数
}
/**********************************************************************
  名称 : TMP119_Write_Register(unsigned char nSLA,REG_Def nReg,unsigned int value)
  功能 : TMP119写指定寄存器函数
  输入 : nSLA -- 设备地址
        nReg -- 寄存器
        value -- 数据
  输出 : 无
  说明 : 无
***********************************************************************/
void TMP119_Write_Register(unsigned char nSLA,REG_Def nReg,unsigned int value)
{
  Wire.beginTransmission(nSLA);
  Wire.write(nReg);                           // 指向指定寄存器
  Wire.write((unsigned char)(value >> 8));    // 高字节
  Wire.write((unsigned char)(value & 0xFF));  // 低字节
  Wire.endTransmission();
}
/**********************************************************************
  名称 : TMP119_Read_Register(unsigned char nSLA,REG_Def nReg)
  功能 : TMP119读指定寄存器函数
  输入 : nSLA -- 设备地址
        nReg -- 寄存器      
  输出 : value -- 数据
  说明 : 无
***********************************************************************/
unsigned int TMP119_Read_Register(unsigned char nSLA,REG_Def nReg)
{
  unsigned int value = 0x00;
  
  Wire.beginTransmission(nSLA);
  Wire.write(nReg);                           // 指向设定寄存器

  unsigned char error = Wire.endTransmission(false);   
  unsigned char bytesReceived = Wire.requestFrom(uint8_t(nSLA), uint8_t (2));
  if (bytesReceived == 2) 
  {
    value = Wire.read() << 8; 
    value |= Wire.read(); 
  }
  return  value;
}
/**********************************************************************
  名称 : TMP119_Read_Register(unsigned char nSLA,REG_Def nReg)
  功能 : TMP119读指定寄存器函数
  输入 : nSLA -- 设备地址
        nReg -- 寄存器      
  输出 : value -- 数据
  说明 : 无
***********************************************************************/
void TMP119_SoftReset(void)
{
  CFG_REG.WORD = 0x00;                        //复位寄存器
  CFG_REG.BIT.Soft_Reset = 1;                 //设置软复位
  
  TMP119_Write_Register(TMP_SLA,REG_Configuration,CFG_REG.WORD);          //启动软复位
}
/**********************************************************************
* 名称 : TMP119_Write_CFG_Register(MOD_Def cMode,unsigned char cRate,AVG_Def nAVG)
* 功能 : 写配置寄存器函数
* 输入 : cMode -- 转换模式
     cRate -- 转换周期
     nAVG -- 均值计算模式
* 输出 ：无
* 说明 : 无
***********************************************************************/
void TMP119_Write_CFG_Register(MOD_Def cMode,unsigned char cRate,AVG_Def nAVG)
{
  CFG_REG.WORD = 0X00;                        //复位寄存器
  
  //1.设置配置寄存器参数
  CFG_REG.BIT.MOD1_0 = cMode;                 //转换模式
  CFG_REG.BIT.CONV2_0 = cRate;                //转换周期
  CFG_REG.BIT.AVG1_0 = nAVG;                  //均值计算模式
  CFG_REG.BIT.T_nA = 0;                       //报警模式
  CFG_REG.BIT.POL = 0;                        //低电平有效
  CFG_REG.BIT.DR_Alert = 1;                   //ALERT引脚指示数据转换完成
  
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
    TMP119_Write_Register(TMP_SLA,nReg,(unsigned int)((float)Temp / TEMP_LSB));  //写入寄存器 
  }
}
/**********************************************************************
* 名称 : TMP119_Write_EEPROM(REG_Def nReg,unsigned int value)
* 功能 : 写EEPROM寄存器函数
* 输入 : nReg -- 寄存器
     value -- 数据
* 输出 ：状态：0 -- 成功，1 -- 失败
* 说明 : 无
***********************************************************************/
unsigned char TMP119_Write_EEPROM(REG_Def nReg,unsigned int value)
{
  EEPROM_UL_REG.WORD = 0x00;                   //复位寄存器
  if(nReg == REG_EEPROM2 || nReg == REG_EEPROM3)    //为支持NIST的可追溯性，切勿对EEPROM1进行编程
  {
    EEPROM_UL_REG.BIT.EUN = 1;                //解锁  
    TMP119_Write_Register(TMP_SLA,REG_EEPROM_UL,EEPROM_UL_REG.WORD);    //写入寄存器等待解锁成功
    delay(8);                                 //等待8ms
    EEPROM_UL_REG.WORD = TMP119_Read_Register(TMP_SLA,REG_EEPROM_UL);   //回读寄存器的值
    if(EEPROM_UL_REG.BIT.EEPROM_Busy == 0)    //解锁成功
    {
      TMP119_Write_Register(TMP_SLA,nReg,value);                        //编程EEPROM
    }
    else return 1;                            //解锁失败
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
unsigned int TMP119_Read_EEPROM(REG_Def nReg)
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
  TMP119_Write_Register(TMP_SLA,REG_Temp_Offset,(signed int)((float)Temp / TEMP_LSB));
}
/**********************************************************************
* 名称 : TMP119_Read_Device_ID(void)
* 功能 : 读取芯片ID号
* 输入 : 无
* 输出 ：ID号(默认0x2117)
* 说明 : 无
***********************************************************************/
unsigned int TMP119_Read_Device_ID(void)
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
  TMP119_SoftReset();     delay(10);
  
  //5.初始化TMP119
  TMP119_Write_CFG_Register(CC1_MODE,0,AVG_32); //0.5S输出一个(参考芯片手册，表 8-6. Conversion Cycle Time in CC Mode) 
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
  
  while(digitalRead(ALT_PIN) != 0);             //等待转换完成
  reg_value = TMP119_Read_Register(TMP_SLA,REG_Temp_Result);

  return (float)(reg_value * TEMP_LSB);         //转换成温度输出
}
