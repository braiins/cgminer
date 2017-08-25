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

#include "asic_inno_cmd.h"
#include "asic_inno_clock.h"

const struct PLL_Clock PLL_Clk_12Mhz[A4_PLL_CLOCK_MAX]={
		{1300, 1300, A4_PLL(0b00011,0b101000101,0b00)},
		{1200, 1200, A4_PLL(0b00001,0b001100100,0b00)},
		{1100, 1100, A4_PLL(0b00011,0b100010011,0b00)},
		{1000, 1000, A4_PLL(0b00011,0b011111010,0b00)},
		{900,	900, A4_PLL(0b00011,0b011100001,0b00)},
		{800,	800, A4_PLL(0b00011,0b011001000,0b00)},
		{700,	700, A4_PLL(0b00011,0b010101111,0b00)},
		{600,	600, A4_PLL(0b00011,0b100101100,0b01)},
		{500,	500, A4_PLL(0b00011,0b011111010,0b01)},
		{400,	400, A4_PLL(0b00011,0b011001000,0b01)},
		{300,	300, A4_PLL(0b00001,0b001100100,0b10)},
		{200,	200, A4_PLL(0b00011,0b011001000,0b10)},
		{120,	120, A4_PLL(0b00011,0b011001000,0b10)},
};



int A1_ConfigA1PLLClock(int optPll)
{
	int i;
	int A1Pll;

	if(optPll>0)
	{
		A1Pll=A5_PLL_CLOCK_800MHz;
		if(optPll<=PLL_Clk_12Mhz[A5_PLL_CLOCK_120MHz].speedMHz)
		{
			A1Pll=A5_PLL_CLOCK_120MHz; //found
		}
		else
		{
			for(i=1;i<A5_PLL_CLOCK_MAX;i++)
			{
				if((optPll>PLL_Clk_12Mhz[i].speedMHz)&&(optPll<=PLL_Clk_12Mhz[i-1].speedMHz))
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
	
#ifdef CHIP_A6
	uint8_t fix_val[8] = {0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF}; 
#else
	uint8_t fix_val[8] = {0x00, 0x00, 0x00, 0xA8, 0x00, 0xE4, 0xFF, 0xFF}; 
#endif
	

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


