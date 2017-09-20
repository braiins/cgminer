#include "config.h"

#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>

#include "logging.h"
#include "miner.h"
#include "util.h"

#include "A5_inno_cmd.h"
#include "A5_inno_clock.h"

const struct PLL_Clock PLL_Clk_12Mhz[142]={
	{0,   120,	A4_PLL(1, 80, 3)}, //default
	{1,   125,	A4_PLL(1, 83, 3)}, //
	{2,   129,	A4_PLL(1, 86, 3)},
	{3,   140,	A4_PLL(1, 93, 3)},
	{4,   159,	A4_PLL(1, 53, 2)},
	{5,   171,	A4_PLL(1, 57, 2)},
	{6,   180,	A4_PLL(1, 60, 2)},
	{7,   189,	A4_PLL(1, 63, 2)},
	{8,   201,	A4_PLL(1, 67, 2)},
	{9,   210,	A4_PLL(1, 70, 2)},
	{10,  219,	A4_PLL(1, 73, 2)},
	{11,  231,	A4_PLL(1, 77, 2)},
	{12,  240,	A4_PLL(1, 80, 2)},
	{13,  249,	A4_PLL(1, 83, 2)},
	{14,  261,	A4_PLL(1, 87, 2)},
	{15,  270,	A4_PLL(1, 90, 2)},
	{16,  279,	A4_PLL(1, 93, 2)},
	{17,  291,	A4_PLL(1, 97, 2)},
	{18,  300,	A4_PLL(1, 50, 1)},
	{19,  312,	A4_PLL(1, 52, 1)},
	{20,  318,	A4_PLL(1, 53, 1)},
	{21,  330,	A4_PLL(1, 55, 1)},
	{22,  342,	A4_PLL(1, 57, 1)},
	{23,  348,	A4_PLL(1, 58, 1)},
	{24,  360,	A4_PLL(1, 60, 1)},
	{25,  372,	A4_PLL(1, 62, 1)},
	{26,  378,	A4_PLL(1, 63, 1)},
	{27,  390,	A4_PLL(1, 65, 1)},
	{28,  402,	A4_PLL(1, 67, 1)},
	{29,  408,	A4_PLL(1, 68, 1)},
	{30,  420,	A4_PLL(1, 70, 1)},
	{31,  432,	A4_PLL(1, 72, 1)},
	{32,  438,	A4_PLL(1, 73, 1)},
	{33,  450,	A4_PLL(1, 75, 1)},
	{34,  462,	A4_PLL(1, 77, 1)},
	{35,  468,	A4_PLL(1, 78, 1)},
	{36,  480,	A4_PLL(1, 80, 1)},
	{37,  492,	A4_PLL(1, 82, 1)},
	{38,  498,	A4_PLL(1, 83, 1)},
	{39,  510,	A4_PLL(1, 85, 1)},
	{40,  522,	A4_PLL(1, 87, 1)},
	{41,  528,	A4_PLL(1, 88, 1)},
	{42,  540,	A4_PLL(1, 90, 1)},
	{43,  552,	A4_PLL(1, 92, 1)},
	{44,  558,	A4_PLL(1, 93, 1)},
	{45,  570,	A4_PLL(1, 95, 1)},
	{46,  582,	A4_PLL(1, 97, 1)},
	{47,  588,	A4_PLL(1, 98, 1)},
	{48,  600,	A4_PLL(1, 50, 0)},
	{49,  612,	A4_PLL(1, 51, 0)},
	{50,  624,	A4_PLL(1, 52, 0)},
	{51,  630,	A4_PLL(2, 105,0)},
	{52,  636,	A4_PLL(1, 53, 0)},
	{53,  648,	A4_PLL(1, 54, 0)},
	{54,  660,	A4_PLL(1, 55, 0)},
	{55,  672,	A4_PLL(1, 56, 0)},
	{56,  684,	A4_PLL(1, 57, 0)},
	{57,  690,	A4_PLL(2, 115,0)},
	{58,  696,	A4_PLL(1, 58, 0)},
	{59,  708,	A4_PLL(1, 59, 0)},
	{60,  720,	A4_PLL(1, 60, 0)},
	{61,  732,	A4_PLL(1, 61, 0)},
	{62,  744,	A4_PLL(1, 62, 0)},
	{63,  750,	A4_PLL(2, 125,0)},
	{64,  756,	A4_PLL(1, 63, 0)},
	{65,  768,	A4_PLL(1, 64, 0)},
	{66,  780,	A4_PLL(1, 65, 0)},
	{67,  792,	A4_PLL(1, 66, 0)},
	{68,  804,	A4_PLL(1, 67, 0)},
	{69,  810,	A4_PLL(2, 135,0)},
	{70,  816,	A4_PLL(1, 68, 0)},
	{71,  828,	A4_PLL(1, 69, 0)},
	{72,  840,	A4_PLL(1, 70, 0)},
	{73,  852,	A4_PLL(1, 71, 0)},
	{74,  864,	A4_PLL(1, 72, 0)},
	{75,  870,	A4_PLL(2, 145,0)},
	{76,  876,	A4_PLL(1, 73, 0)},
	{77,  888,	A4_PLL(1, 74, 0)},
	{78,  900,	A4_PLL(1, 75, 0)},
	{79,  912,	A4_PLL(1, 76, 0)},
	{80,  924,	A4_PLL(1, 77, 0)},
	{81,  930,	A4_PLL(2, 155,0)},
	{82,  936,	A4_PLL(1, 78, 0)},
	{83,  948,	A4_PLL(1, 79, 0)},
	{84,  960,	A4_PLL(1, 80, 0)},
	{85,  972,	A4_PLL(1, 81, 0)},
	{86,  984,	A4_PLL(1, 82, 0)},
	{87,  990,	A4_PLL(2, 165,0)},
	{88,  996,	A4_PLL(1, 83, 0)},
	{89,  1008, A4_PLL(1, 84, 0)},
	{90,  1020, A4_PLL(1, 85, 0)},
	{91,  1032, A4_PLL(1, 86, 0)},
	{92,  1044, A4_PLL(1, 87, 0)},
	{93,  1050, A4_PLL(2, 175,0)},
	{94,  1056, A4_PLL(1, 88, 0)},
	{95,  1068, A4_PLL(1, 89, 0)},
	{96,  1080, A4_PLL(1, 90, 0)},
	{97,  1092, A4_PLL(1, 91, 0)},
	{98,  1104, A4_PLL(1, 92, 0)},
	{99,  1110, A4_PLL(2, 185,0)},
	{100, 1116, A4_PLL(1, 93, 0)},
	{101, 1128, A4_PLL(1, 94, 0)},
	{102, 1140, A4_PLL(1, 95, 0)},
	{103, 1152, A4_PLL(1, 96, 0)},
	{104, 1164, A4_PLL(1, 97, 0)},
	{105, 1170, A4_PLL(2, 195,0)},
	{106, 1176, A4_PLL(1, 98, 0)},
	{107, 1188, A4_PLL(1, 99, 0)},
	{108, 1200, A4_PLL(1, 100,0)},
	{109, 1212, A4_PLL(1, 101,0)},
	{110, 1224, A4_PLL(1, 102,0)},
	{111, 1236, A4_PLL(1, 103,0)},
	{112, 1248, A4_PLL(1, 104,0)},
	{113, 1260, A4_PLL(1, 105,0)},
	{114, 1272, A4_PLL(1, 106,0)},
	{115, 1284, A4_PLL(1, 107,0)},
	{116, 1296, A4_PLL(1, 108,0)},
	{117, 1308, A4_PLL(1, 109,0)},
	{118, 1320, A4_PLL(2, 110,0)},
	{119, 1332, A4_PLL(1, 111,0)},
	{120, 1344, A4_PLL(1, 112,0)},
	{121, 1356, A4_PLL(1, 113,0)},
	{122, 1368, A4_PLL(1, 114,0)},
	{123, 1380, A4_PLL(1, 115,0)},
	{124, 1392, A4_PLL(2, 116,0)},
	{125, 1404, A4_PLL(1, 117,0)},
	{126, 1416, A4_PLL(1, 118,0)},
	{127, 1428, A4_PLL(1, 119,0)},
	{128, 1440, A4_PLL(1, 120,0)},
	{129, 1452, A4_PLL(1, 121,0)},
	{130, 1464, A4_PLL(1, 122,0)},
	{131, 1476, A4_PLL(1, 123,0)},
	{132, 1488, A4_PLL(1, 124,0)},
	{133, 1500, A4_PLL(1, 125,0)},
	{134, 1512, A4_PLL(1, 126,0)},
	{135, 1524, A4_PLL(1, 127,0)},
	{136, 1536, A4_PLL(1, 128,0)},
	{137, 1548, A4_PLL(1, 129,0)},
	{138, 1560, A4_PLL(1, 130,0)},
	{139, 1572, A4_PLL(1, 131,0)},
	{140, 1584, A4_PLL(1, 132,0)},
	{141, 1596, A4_PLL(1, 133,0)}
};   

int A1_ConfigA1PLLClock(int optPll)
{
	int i;
	int A1Pll;

	if(optPll>0)
	{
		A1Pll=0;
		if(optPll<=PLL_Clk_12Mhz[0].speedMHz) 
		{
			A1Pll=0; //found
		}
		else
		{
			for(i=1;i<142;i++)
			{
				if((optPll<PLL_Clk_12Mhz[i].speedMHz)&&(optPll>=PLL_Clk_12Mhz[i-1].speedMHz))
				{
					A1Pll=i-1; //found
					break;
				}
			}
		}

		applog(LOG_NOTICE, "A1 = %d,%d",optPll,A1Pll);
		applog(LOG_NOTICE, "A1 PLL Clock = %dMHz",PLL_Clk_12Mhz[A1Pll].speedMHz);
	}

	return A1Pll;
}


void A1_SetA1PLLClock(struct A1_chain *a1,int pllClkIdx)
{
	uint8_t i;
	struct A1_chip *chip;
	uint32_t regPll;
	uint8_t rxbuf[12];
	
	uint8_t fix_val[8] = {0x00, 0x00, 0x00, 0xA8, 0x00, 0xE4, 0xFF, 0xFF}; 
	

	assert(a1->chips != NULL);
	assert((pllClkIdx > 0) && (pllClkIdx < A5_PLL_CLOCK_MAX));

	regPll = PLL_Clk_12Mhz[pllClkIdx].pll_reg;

	chip = &a1->chips[0];
	memcpy(chip->reg,     (uint8_t*)&regPll + 3 ,1);
	memcpy(chip->reg + 1, (uint8_t*)&regPll + 2 ,1);
	memcpy(chip->reg + 2, (uint8_t*)&regPll + 1 ,1);
	memcpy(chip->reg + 3, (uint8_t*)&regPll + 0 ,1);
	memcpy(chip->reg + 4, fix_val , 8);
	
	//chip->reg[6] = (asic_vol_set&0xff00)>>8;
	//chip->reg[7] = asic_vol_set&0x00ff;
	//chip->reg[8] = pllClkIdx;
	//chip->reg[9] = pllClkIdx;
	//applog(LOG_INFO,"pllClkIdx is %d %d", chip->reg[8],chip->reg[9]);	

	inno_cmd_write_reg(a1, ADDR_BROADCAST, chip->reg);
	usleep(100000);
	inno_cmd_read_reg(a1, ADDR_BROADCAST, rxbuf);
	hexdump("read value", rxbuf, 12);	
	
}


