#ifndef _ASIC_INNO_CLOCK_
#define _ASIC_INNO_CLOCK_

#define A1_PLL_POSTDIV_MASK		0b11
#define A1_PLL_PREDIV_MASK		0b11111
#define A1_PLL_FBDIV_H_MASK		0b111111111

// Register masks for A1
//Reg32 Upper
#define A1_PLL_POSTDIV			(46-32)	// (2) pll post divider
#define A1_PLL_PREDIV			(41-32)	// (5) pll pre divider
#define A1_PLL_FBDIV_H			(32-32)	// (9) pll fb divider
//Reg32 Lower
#define A1_PLL_PD				25 // (1) PLL Reset
#define A1_PLL_LOCK				24 // (1) PLL Lock status (1: PLL locked , 0: PLL not locked )
#define A1_INTERNAL_SPI_FREQUENCY_CONFIG	18 // (1) 1:  Internal  SPI  frequency  is System  clock  frequency  divide 64
						   //     0:  Internal  SPI  frequency  is System  clock  frequency  divide 128


#define A4_PLL(prediv,fbdiv,postdiv) ((prediv<<(89-64))|fbdiv<<(80-64)|0b010<<(77-64)|postdiv<<(70-64))


typedef enum
{
	A4_PLL_CLOCK_1600MHz = 0,
    A4_PLL_CLOCK_1500MHz,
    A4_PLL_CLOCK_1400MHz,
    A4_PLL_CLOCK_1300MHz,
	A4_PLL_CLOCK_1272MHz,
	A4_PLL_CLOCK_1248MHz,
	A4_PLL_CLOCK_1224MHz,
    A4_PLL_CLOCK_1200MHz,
	A4_PLL_CLOCK_1176MHz,
	A4_PLL_CLOCK_1128MHz,
    A4_PLL_CLOCK_1100MHz,
	A4_PLL_CLOCK_1074MHz,
	A4_PLL_CLOCK_1050MHz,
	A4_PLL_CLOCK_1026MHz,
    A4_PLL_CLOCK_1000MHz,
	A4_PLL_CLOCK_978MHz,
	A4_PLL_CLOCK_948MHz,
	A4_PLL_CLOCK_924MHz,
    A4_PLL_CLOCK_900MHz,
	A4_PLL_CLOCK_876MHz,
    A4_PLL_CLOCK_860MHz,
	A4_PLL_CLOCK_852MHz,
	A4_PLL_CLOCK_828MHz,
    A4_PLL_CLOCK_800MHz,
	A4_PLL_CLOCK_774MHz,
	A4_PLL_CLOCK_750MHz,
	A4_PLL_CLOCK_726MHz,
    A4_PLL_CLOCK_700MHz,
	A4_PLL_CLOCK_675MHz,
	A4_PLL_CLOCK_650MHz,
	A4_PLL_CLOCK_624MHz,
    A4_PLL_CLOCK_600MHz,
	A4_PLL_CLOCK_576MHz,
	A4_PLL_CLOCK_550MHz,
	A4_PLL_CLOCK_525MHz,
    A4_PLL_CLOCK_500MHz,
	A4_PLL_CLOCK_450MHz,
	A4_PLL_CLOCK_400MHz,
	A4_PLL_CLOCK_350MHz,
	A4_PLL_CLOCK_300MHz,
	A4_PLL_CLOCK_200MHz,
	A4_PLL_CLOCK_MAX,
} A4_PLL_CLOCK;

typedef enum
{
    A5_PLL_CLOCK_1300MHz = 0,
    A5_PLL_CLOCK_1200MHz,
    A5_PLL_CLOCK_1100MHz,
    A5_PLL_CLOCK_1000MHz,
    A5_PLL_CLOCK_900MHz,
    A5_PLL_CLOCK_800MHz,
    A5_PLL_CLOCK_700MHz,
    A5_PLL_CLOCK_600MHz,
    A5_PLL_CLOCK_500MHz,
	A5_PLL_CLOCK_400MHz,
	A5_PLL_CLOCK_300MHz,
	A5_PLL_CLOCK_200MHz,
	A5_PLL_CLOCK_120MHz,
	A5_PLL_CLOCK_MAX,
} A5_PLL_CLOCK;



struct PLL_Clock {
	uint32_t num;  	// divider 1000
	uint32_t speedMHz;  	// unit MHz
	uint32_t pll_reg;
};


struct A1_config_options {
	int ref_clk_khz;
	int sys_clk_khz;
	int spi_clk_khz;
	/* limit chip chain to this number of chips (testing only) */
	int override_chip_num;
	int wiper;
};

void A1_SetA1PLLClock(struct A1_chain *a1,int pllClkIdx);
int A1_ConfigA1PLLClock(int optPll);

extern const struct PLL_Clock PLL_Clk_12Mhz[118];




#endif
