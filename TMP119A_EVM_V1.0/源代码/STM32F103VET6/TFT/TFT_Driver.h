#ifndef  __TFT_DRIVER_H__
#define  __TFT_DRIVER_H__

#include "TFT_IO.h"
#include "math.h"
#include "string.h"
#include "stdint.h"
#include "stdio.h"
/*****************************字符定义*********************************/

extern const unsigned char ascii_1206[][12];
extern const unsigned char ascii_1608[][16];
extern const unsigned char ascii_2412[][48];
extern const unsigned char ascii_3216[][64];

/*****************************中文定义*********************************/
typedef struct 
{
	unsigned char Index[2];	
	unsigned char Msk[24];
}typFNT_GB12; 

extern const typFNT_GB12 tfont12[];

typedef struct 
{
	unsigned char Index[2];	
	unsigned char Msk[32];
}typFNT_GB16; 

extern const typFNT_GB16 tfont16[];

typedef struct 
{
	unsigned char Index[2];	
	unsigned char Msk[72];
}typFNT_GB24; 

extern const typFNT_GB24 tfont24[];

typedef struct 
{
	unsigned char Index[2];	
	unsigned char Msk[128];
}typFNT_GB32; 

extern const typFNT_GB32 tfont32[];

/*****************************图片定义*********************************/

extern const unsigned char Image_QQ[3200];

/*****************************接口函数*********************************/

void TFT_Fill(uint16_t xsta,uint16_t ysta,uint16_t xend,uint16_t yend,uint16_t color);			//TFT在指定区域填充颜色
void TFT_DrawPoint(uint16_t x,uint16_t y,uint16_t color);										//设置光标位置 
void TFT_DrawLine(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2,uint16_t color);				//画线
void TFT_DrawRectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,uint16_t color);						//画矩形
void TFT_Draw_Circle(uint16_t x0,uint16_t y0,uint8_t r,uint16_t color);							//画圆
void TFT_ShowChinese(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode);		//显示汉字串
void TFT_ShowChinese12x12(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode);	//显示单个12x12汉字
void TFT_ShowChinese16x16(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode);	//显示单个16x16汉字
void TFT_ShowChinese24x24(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode);	//显示单个24x24汉字
void TFT_ShowChinese32x32(uint16_t x,uint16_t y,uint8_t *s,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode);	//显示单个32x32汉字
void TFT_ShowChar(uint16_t x,uint16_t y,uint8_t num,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode);		//显示单个字符
void TFT_ShowString(uint16_t x,uint16_t y,const uint8_t *p,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode);	//显示字符串
uint32_t mypow(uint8_t m,uint8_t n);		//显示数字
void TFT_ShowInt(uint16_t x, uint16_t y, int32_t num, uint8_t show_zero,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode);	//整数显示函数
void TFT_ShowDouble(uint16_t x, uint16_t y, double num, uint8_t dp_len, uint8_t show_zero,uint16_t fc,uint16_t bc,uint8_t sizey,uint8_t mode) ;	//浮点数显示
void TFT_ShowPicture(uint16_t x,uint16_t y,uint16_t length,uint16_t width,const uint8_t pic[]);	//显示图片

#endif

















