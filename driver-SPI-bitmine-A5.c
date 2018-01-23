/*
 * cgminer SPI driver for Bitmine.ch A1 devices
 *
 * Copyright 2013, 2014 Zefir Kurtisi <zefir.kurtisi@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>

#include "spi-context.h"
#include "logging.h"
#include "miner.h"
#include "util.h"

#include "A1-board-selector.h"
#include "A1-trimpot-mcp4x.h"

#include "A5_inno.h"
#include "A5_inno_clock.h"
#include "A5_inno_cmd.h"
#include "A5_inno_gpio.h"

#include "inno_fan.h"

static int64_t A1_bench_scanwork(struct cgpu_info *cgpu);

struct spi_config cfg[ASIC_CHAIN_NUM];
struct spi_ctx *spi[ASIC_CHAIN_NUM];
struct A1_chain *chain[ASIC_CHAIN_NUM];

/*
struct Test_bench Test_bench_Array[6]={
	{1260,   14,	0,	0}, //default
	{1260,   15,	0,	0}, 
	{1296,   14,	0,	0},
	{1296,   15,	0,	0}, 
	{1332,   14,	0,	0},
	{1332,   15,	0,	0}, 
};
*/
struct Test_bench Test_bench_Array[5]={
	{1332,	0,	0,	0},
	{1332,	0,	0,	0},
	{1332,	0,	0,	0},
	{1332,	0,	0,	0},
	{1332,	0,	0,	0},
};

uint8_t A1Pll1=A5_PLL_CLOCK_800MHz;
uint8_t A1Pll2=A5_PLL_CLOCK_800MHz;
uint8_t A1Pll3=A5_PLL_CLOCK_800MHz;
uint8_t A1Pll4=A5_PLL_CLOCK_800MHz;
uint8_t A1Pll5=A5_PLL_CLOCK_800MHz;
uint8_t A1Pll6=A5_PLL_CLOCK_800MHz;

/* FAN CTRL */
static INNO_FAN_CTRL_T s_fan_ctrl;
static uint32_t show_log[ASIC_CHAIN_NUM];
static uint32_t update_cnt[ASIC_CHAIN_NUM];
static uint32_t write_flag[ASIC_CHAIN_NUM];
static uint32_t check_disbale_flag[ASIC_CHAIN_NUM];
static uint32_t first_flag[ASIC_CHAIN_NUM] = {0};
static inno_reg_ctrl_t s_reg_ctrl;
#define DANGEROUS_TMP  110
#define STD_V          0.84
int spi_plug_status[ASIC_CHAIN_NUM] = {0};

/* one global board_selector and spi context is enough */
//static struct board_selector *board_selector;
//static struct spi_ctx *spi;

/********** work queue */
static bool wq_enqueue(struct work_queue *wq, struct work *work)
{
	if (work == NULL)
		return false;
	struct work_ent *we = malloc(sizeof(*we));
	assert(we != NULL);

	we->work = work;
	INIT_LIST_HEAD(&we->head);
	list_add_tail(&we->head, &wq->head);
	wq->num_elems++;
	return true;
}

static struct work *wq_dequeue(struct work_queue *wq)
{
	if (wq == NULL)
		return NULL;
	if (wq->num_elems == 0)
		return NULL;
	struct work_ent *we;
	we = list_entry(wq->head.next, struct work_ent, head);
	struct work *work = we->work;

	list_del(&we->head);
	free(we);
	wq->num_elems--;
	return work;
}

/*
 * for now, we have one global config, defaulting values:
 * - ref_clk 16MHz / sys_clk 800MHz
 * - 2000 kHz SPI clock
 */
struct A1_config_options A1_config_options = {
	.ref_clk_khz = 16000, .sys_clk_khz = 800000, .spi_clk_khz = 2000,
};

/* override values with --bitmine-a1-options ref:sys:spi: - use 0 for default */
static struct A1_config_options *parsed_config_options;

/********** driver interface */
void exit_A1_chain(struct A1_chain *a1)
{
	if (a1 == NULL)
		return;
	free(a1->chips);
	asic_gpio_write(a1->spi_ctx->led, 1);
	a1->chips = NULL;
	a1->spi_ctx = NULL;
	free(a1);
}

struct A1_chain *init_A1_chain(struct spi_ctx *ctx, int chain_id)
{
	int i;
	struct A1_chain *a1 = malloc(sizeof(*a1));
	if (a1 == NULL){
		goto failure;
	}

	applog_hw_chain(LOG_DEBUG, chain_id, "A1 init chain");
	
	memset(a1, 0, sizeof(*a1));
	a1->spi_ctx = ctx;
	a1->chain_id = chain_id;
	
	a1->num_chips =  chain_detect(a1);
	usleep(10000);
	
	if (a1->num_chips <= 0)
		goto failure;

	applog_hw_chain(LOG_INFO, chain_id, "spidev%d.%d: Found %d A1 chips",
	       a1->spi_ctx->config.bus, a1->spi_ctx->config.cs_line,
	       a1->num_chips);
/*
	if (!set_pll_config(a1, 0, A1_config_options.ref_clk_khz,
			    A1_config_options.sys_clk_khz))
		goto failure;
*/
	/* override max number of active chips if requested */
	a1->num_active_chips = a1->num_chips;
	if (A1_config_options.override_chip_num > 0 &&
	    a1->num_chips > A1_config_options.override_chip_num) 
	{
		a1->num_active_chips = A1_config_options.override_chip_num;
		applog_hw_chain(LOG_WARNING, chain_id, "limiting chain to %d chips",
		       a1->num_active_chips);
	}

	a1->chips = calloc(a1->num_active_chips, sizeof(struct A1_chip));
	if (a1->chips == NULL){
		goto failure;
	}

	if (!inno_cmd_bist_fix(a1, ADDR_BROADCAST))
		goto failure;

	usleep(200);
	//configure for vsensor
	inno_configure_tvsensor(a1,ADDR_BROADCAST,0);

	for (i = 0; i < a1->num_active_chips; i++)
    {
		inno_check_voltage(a1, i+1);
    }
	
	//configure for tsensor
	inno_configure_tvsensor(a1,ADDR_BROADCAST,1);

	for (i = 0; i < a1->num_active_chips; i++)
    {
		check_chip(a1, i);

        inno_fan_temp_add(&s_fan_ctrl, chain_id, a1->chips[i].temp, true);
    }
    /* ÉèÖÃ³õÊ¼Öµ */ 
    inno_fan_temp_init(&s_fan_ctrl, chain_id);
#ifndef CHIP_A6
	inno_temp_contrl(&s_fan_ctrl, a1, chain_id);
#endif
	applog_hw_chain(LOG_INFO, chain_id, "found %d chips with total %d active cores",
	       a1->num_active_chips, a1->num_cores);
	//modify 0922       
	if(inno_fan_temp_get_highest(&s_fan_ctrl,chain_id) > DANGEROUS_TMP)
	{
	 asic_gpio_write(spi[a1->chain_id]->power_en, 0);
	 early_quit(1,"Notice chain %d maybe has some promble in temperate\n",a1->chain_id);
	}
	
	mutex_init(&a1->lock);
	INIT_LIST_HEAD(&a1->active_wq.head);

	return a1;

failure:
	exit_A1_chain(a1);
	return NULL;
}

static void A1_printstats_chain(struct A1_chain *a1)
{
	int i;
	for (i=0; i < a1->num_active_chips; i++) {
		struct A1_chip *chip = &a1->chips[i];

		printf("%d,%d,%d,%d,%d,%d,%d,%d,%5.2f,%f\n",
				a1->chain_id, i + 1, chip->disabled,
				chip->num_cores,
				chip->nonce_ranges_done, chip->nonces_found,
				chip->hw_errors, chip->stales,
				inno_fan_temp_to_float(&s_fan_ctrl,chip->temp),
				s_reg_ctrl.cur_vol[a1->chain_id][i]);
	}
}

static void A1_printstats_all(void)
{
	int i;
	for (i = 0; i < ASIC_CHAIN_NUM; i++) {
		if (chain[i] != NULL)
			A1_printstats_chain(chain[i]);
	}
}


/**
 * Submit chain statistics to current pool (if it's stratum)
 *
 * @param a1 Chain whose statistics to submit
 */
static void A1_submit_stats(struct A1_chain *a1)
{
	struct telemetry *tele;
	int i, j;

	/* create miner_stats */
	tele = make_telemetry_data(a1->num_active_chips, a1->chain_id);
	for (i = 0; i < a1->num_active_chips; i++) {
		struct A1_chip *chip = &a1->chips[i];
		struct chip_stats *chipstats = &tele->data.chips[i];

		chipstats->id = i + 1;
		chipstats->disabled = chip->disabled;
		chipstats->num_cores = chip->num_cores;
		chipstats->nonce_ranges_done = chip->nonce_ranges_done;
		chipstats->nonces_found = chip->nonces_found;
		chipstats->hw_errors = chip->hw_errors;
		chipstats->stales = chip->stales;
		chipstats->temperature = inno_fan_temp_to_float(&s_fan_ctrl,chip->temp);
		chipstats->voltage = measurement_get_avg(&chip->voltage);
	}

	submit_telemetry(tele);
}


bool test_bench_init_chain(struct A1_chain *a1)
{
	int i;
	
	a1->num_chips = chain_detect(a1);
	usleep(10000);
	
	if (a1->num_chips == 0)
		return false;

	applog_hw_chain(LOG_INFO, a1->chain_id, "spidev%d.%d: Found %d A1 chips",
	       a1->spi_ctx->config.bus, a1->spi_ctx->config.cs_line,
	       a1->num_chips);

	a1->num_active_chips = a1->num_chips;

	a1->num_cores = 0;
	free(a1->chips);
	a1->chips = NULL;
	a1->chips = calloc(a1->num_active_chips, sizeof(struct A1_chip));
	assert (a1->chips != NULL);

	if (!inno_cmd_bist_fix(a1, ADDR_BROADCAST))
		return false;

	usleep(200);

	for (i = 0; i < a1->num_active_chips; i++)
    {
		check_chip(a1, i);
    }

	applog_hw_chain(LOG_INFO, a1->chain_id, "found %d chips with total %d active cores",
	       a1->num_active_chips, a1->num_cores);
}

uint32_t pll_vid_test_bench(uint32_t uiPll, int uiVol)
{
	int i;
	uint32_t uiScore = 0;

	if(opt_voltage > 8){
		for(i=opt_voltage-1; i>=8; i--){
			set_vid_value(i);
			usleep(100000);
		}
	}

	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		asic_gpio_init(spi[i]->power_en, 0);
		asic_gpio_init(spi[i]->start_en, 0);
		asic_gpio_init(spi[i]->reset, 0);
		asic_gpio_init(spi[i]->led, 0);
		//asic_gpio_init(spi[i]->plug, 0);
	}

	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		asic_gpio_write(spi[i]->power_en, 1);
		asic_gpio_write(spi[i]->start_en, 1);
		asic_gpio_write(spi[i]->reset, 1);
		usleep(200000);
		asic_gpio_write(spi[i]->reset, 0);
		usleep(200000);
		asic_gpio_write(spi[i]->reset, 1);	
	}
	
	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{		
	    test_bench_pll_config(chain[i], uiPll);
	}
	
	if(uiVol > 8){
		for(i=9; i<=uiVol; i++){
			set_vid_value(i);
			usleep(100000);
		}
	}
			
	opt_voltage = uiVol;

	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		test_bench_init_chain(chain[i]);
		//usleep(100000);
	}
	
	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{		
		uiScore += inno_cmd_test_chip(chain[i]);
	}
	return uiScore;
}

void config_best_pll_vid(uint32_t uiPll, int uiVol)
{
	int i;

	if(opt_voltage > 8){
		for(i=opt_voltage-1; i>=8; i--){
			set_vid_value(i);
			usleep(100000);
		}
	}

	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		asic_gpio_init(spi[i]->power_en, 0);
		asic_gpio_init(spi[i]->start_en, 0);
		asic_gpio_init(spi[i]->reset, 0);
		//asic_gpio_init(spi[i]->plug, 0);
		//asic_gpio_init(spi[i]->led, 0);
	}

	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		asic_gpio_write(spi[i]->power_en, 1);
		asic_gpio_write(spi[i]->start_en, 1);
		asic_gpio_write(spi[i]->reset, 1);
		usleep(200000);
		asic_gpio_write(spi[i]->reset, 0);
		usleep(200000);
		asic_gpio_write(spi[i]->reset, 1);	
	}
	
	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{		
	    test_bench_pll_config(chain[i], uiPll);
	}
	
	if(uiVol > 8){
		for(i=9; i<=uiVol; i++){
			set_vid_value(i);
			usleep(100000);
		}
	}
			
	opt_voltage = uiVol;

	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		test_bench_init_chain(chain[i]);
	}
	
	return;
}

//add  0928
int  cfg_tsadc_divider(struct A1_chain *a1,uint32_t pll_clk)
{
	uint8_t  cmd_return;
	uint32_t tsadc_divider_tmp;
	uint8_t  tsadc_divider;
	
	//cmd0d(0x0d00, 0x0250, 0xa006 | (BYPASS_AUXPLL<<6), 0x2800 | tsadc_divider, 0x0300, 0x0000, 0x0000, 0x0000)
	uint8_t    buffer[64] = {0x02,0x50,0xa0,0x06,0x28,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
	#ifdef MPW
		tsadc_divider_tmp = (pll_clk/2)*1000/256/650;
	#else
		tsadc_divider_tmp = (pll_clk/2)*1000/16/650;
    #endif
	tsadc_divider = tsadc_divider_tmp & 0xff;

	buffer[5] = 0x00 | tsadc_divider;

	if (!inno_cmd_write_sec_reg(a1,ADDR_BROADCAST,buffer)) {
		applog_hw_chain(LOG_ERR, a1->chain_id, "write t/v sensor value failed");
	} else {
		applog_hw_chain(LOG_INFO, a1->chain_id, "write t/v sensor value success");
	}
}

void inno_preinit(struct spi_ctx *ctx, int chain_id)
{
	int i;
	struct A1_chain *a1 = malloc(sizeof(*a1));
	assert(a1 != NULL);

	applog_hw_chain(LOG_INFO, chain_id, "A1 init chain");
	
	memset(a1, 0, sizeof(*a1));
	a1->spi_ctx = ctx;
	a1->chain_id = chain_id;
	
	switch(chain_id){
		case 0:prechain_detect(a1, A1Pll1);break;
		case 1:prechain_detect(a1, A1Pll2);break;
		case 2:prechain_detect(a1, A1Pll3);break;
		case 3:prechain_detect(a1, A1Pll4);break;
		case 4:prechain_detect(a1, A1Pll5);break;
		case 5:prechain_detect(a1, A1Pll6);break;
		default:;
	}
	//add 0929
	cfg_tsadc_divider(a1, 120);
}

static inline int is_chain_enabled(int i)
{
	return opt_enabled_chains < 0 || (opt_enabled_chains & (1 << i));
}

static void chain_power_shutdown(int chain_id)
{
	applog_hw_chain(LOG_INFO, chain_id, "power shutdown for chain");

	asic_gpio_write(spi[chain_id]->reset, 0);
	usleep(200000);
	asic_gpio_write(spi[chain_id]->start_en, 0);
	usleep(200000);
	asic_gpio_write(spi[chain_id]->power_en, 0);
	/* turn off led */
	asic_gpio_init(spi[chain_id]->led, 1);
}

static void shutdown_all_chains(void)
{
	int i;
	for(i = 0; i < ASIC_CHAIN_NUM; i++) {
		if (is_chain_enabled(i)) {
			chain_power_shutdown(i);
		}
	}
}


static int chip_power_enabled = 0;
int chain_flag[ASIC_CHAIN_NUM] = {0};
static bool detect_A1_chain(void)
{
	int i, j, cnt = 0;
	//board_selector = (struct board_selector*)&dummy_board_selector;
	applog_hw(LOG_INFO, "checking available chains");

	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		if (!is_chain_enabled(i))
			continue;
		cfg[i].bus     = i;
		cfg[i].cs_line = 0;
		cfg[i].mode    = SPI_MODE_1;
		cfg[i].speed   = DEFAULT_SPI_SPEED;
		cfg[i].bits    = DEFAULT_SPI_BITS_PER_WORD;
		cfg[i].delay   = DEFAULT_SPI_DELAY_USECS;

		spi[i] = spi_init(&cfg[i]);
		if(spi[i] == NULL)
		{
			applog_hw_chain(LOG_ERR, i, "spi init fail");
			return false;
		}
		
		mutex_init(&spi[i]->spi_lock);
		spi[i]->power_en = SPI_PIN_POWER_EN[i];		
		spi[i]->start_en = SPI_PIN_START_EN[i];		
		spi[i]->reset = SPI_PIN_RESET[i];
		spi[i]->plug  = SPI_PIN_PLUG[i];
		spi[i]->led   = SPI_PIN_LED[i];
		

		asic_gpio_init(spi[i]->power_en, 0);
		asic_gpio_init(spi[i]->start_en, 0);
		asic_gpio_init(spi[i]->reset, 0);
		asic_gpio_init(spi[i]->plug, 1);
		asic_gpio_init(spi[i]->led, 0);

		show_log[i] = 0;
		update_cnt[i] = 0;
		write_flag[i] = 0;
		check_disbale_flag[i] = 0;
	}

	chip_power_enabled = 1;
	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		if (!is_chain_enabled(i))
			continue;

		asic_gpio_write(spi[i]->power_en, 1);
		if (opt_A5_fast_start)
			usleep(200000);
		else
			sleep(2);
		asic_gpio_write(spi[i]->reset, 1);
		if (opt_A5_fast_start)
			usleep(100000);
		else
			sleep(1);
		asic_gpio_write(spi[i]->start_en, 1);
		if (opt_A5_fast_start)
			usleep(200000);
		else
			sleep(2);

		if(asic_gpio_read(spi[i]->plug) != 0)
		{
			applog_hw_chain(LOG_ERR, i, "plat is not inserted");
			return false;
		}
	}

	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{		
		if (!is_chain_enabled(i))
			continue;
		inno_preinit(spi[i], i);
	}

	//divide the init to break two part
	if(opt_voltage > 8){
		for(i=9; i<=opt_voltage; i++){
			set_vid_value(i);
			usleep(200000);
		}
	}
			
	if(opt_voltage < 8){
		for(i=7; i>=opt_voltage; i--){
			set_vid_value(i);
			usleep(200000);
		}
	}

	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		if (!is_chain_enabled(i))
			continue;

		chain[i] = init_A1_chain(spi[i], i);
		if (chain[i] == NULL){
			applog_hw_chain(LOG_ERR, i, "init A1 chain fail");
			return false;
		}else{
			cnt++;
			chain_flag[i] = 1;
			applog_hw_chain(LOG_INFO, i, "detected A1 chain with %d chips",
				chain[i]->num_active_chips);
		}

		struct cgpu_info *cgpu = malloc(sizeof(*cgpu));
		assert(cgpu != NULL);
	
		memset(cgpu, 0, sizeof(*cgpu));
		cgpu->drv = &bitmineA1_drv;
		cgpu->name = "BitmineA1.SingleChain";
		cgpu->threads = 1;

		cgpu->device_data = chain[i];

		chain[i]->cgpu = cgpu;
		add_cgpu(cgpu, i);

		asic_gpio_write(chain[i]->spi_ctx->led, 0);

		applog_hw_chain(LOG_INFO, i, "detected A1 chain with %d chips / %d cores",
			chain[i]->num_active_chips,
			chain[i]->num_cores);
	}
	if (opt_A5_benchmark > 0) {
		A1_printstats_all();
	}
	if (opt_A5_benchmark == 3) {
		/* Benchmark No. 3 - simulate mining and compute resulting speed */
		for(i = 0; i < ASIC_CHAIN_NUM; i++) {
			struct A1_chain *a1 = chain[i];

			if (a1 != NULL) {
				a1->bench_difficulty = 1024;
				a1->last_results_update = get_current_ms();
				for (;;)
					A1_bench_scanwork(chain[i]->cgpu);
			}
		}
	}
	if (opt_A5_benchmark == 2) {
		/* Benchmark No. 2 - test each chip individually and measure
		   the time it takes the chip to complete a job that has known
		   solution */
		for(i = 0; i < ASIC_CHAIN_NUM; i++) {
			if (chain[i] == NULL){
				applog_hw_chain(LOG_ERR, i, "chain not present");
				continue;
			}
			inno_cmd_xtest(chain[i], 0);
			inno_cmd_xtest(chain[i], 1);
		}
	}
	if (opt_A5_benchmark == 1) {
		/* Benchmark No. 1 - test good/bad chips on each chain under
		   load and various voltage settings */
	Test_bench_Array[0].uiVol = opt_voltage;
	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		if(chain_flag[i] != 1)
		{
			continue;
		}
		Test_bench_Array[0].uiScore += inno_cmd_test_chip(chain[i]);
		Test_bench_Array[0].uiCoreNum += chain[i]->num_cores;
	}

	for(i = 1; i < 3; i++)
	{
		if(Test_bench_Array[0].uiVol - i < 1)
		{
			continue;
		}
		sleep(1);
		set_vid_value(Test_bench_Array[0].uiVol - i);
		Test_bench_Array[i].uiVol = Test_bench_Array[0].uiVol - i;
		sleep(1);
		for(j = 0; j < ASIC_CHAIN_NUM; j++)
		{	
			if(chain_flag[j] != 1)
			{
				continue;
			}
			Test_bench_Array[i].uiScore += inno_cmd_test_chip(chain[j]);
	    	Test_bench_Array[i].uiCoreNum += chain[j]->num_cores;
		}
	}

	for(i = 1; i >= 0; i--)
	{
		set_vid_value(Test_bench_Array[0].uiVol - i);
		sleep(1);
	}
	
	for(i = 3; i < 5; i++)
	{
		if(Test_bench_Array[0].uiVol + i - 2 > 31)
		{
			continue;
		}
		sleep(1);
		set_vid_value(Test_bench_Array[0].uiVol + i - 2);
		Test_bench_Array[i].uiVol = Test_bench_Array[0].uiVol + i - 2;
		sleep(1);
		for(j = 0; j < ASIC_CHAIN_NUM; j++)
		{
			if(chain_flag[j] != 1)
			{
				continue;
			}
			Test_bench_Array[i].uiScore += inno_cmd_test_chip(chain[j]);
	    	Test_bench_Array[i].uiCoreNum += chain[j]->num_cores;
		}
	}

	for(j = 0; j < 5; j++)
	{
		printf("after pll_vid_test_bench Test_bench_Array[%d].uiScore=%d,Test_bench_Array[%d].uiCoreNum=%d. \n", j, Test_bench_Array[j].uiScore, j, Test_bench_Array[j].uiCoreNum);
	}

	int index = 0;
	uint32_t cur= 0;
	for(j = 1; j < 5; j++)
	{
		cur = Test_bench_Array[j].uiScore + 5 * (Test_bench_Array[j].uiVol - Test_bench_Array[index].uiVol);
		
		if(cur > Test_bench_Array[index].uiScore)
		{
			index = j;
		}

		if((cur == Test_bench_Array[index].uiScore) && (Test_bench_Array[j].uiVol > Test_bench_Array[index].uiVol))
		{
			index = j;
		}
	}

	printf("The best group is %d. vid is %d! \t \n", index, Test_bench_Array[index].uiVol);
	
	for(i=Test_bench_Array[0].uiVol + 2; i>=Test_bench_Array[index].uiVol; i--){
		set_vid_value(i);
		usleep(500000);
	}

	opt_voltage = Test_bench_Array[index].uiVol;
	}
/*
	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{		
		Test_bench_Array[0].uiScore += inno_cmd_test_chip(chain[i]);
		Test_bench_Array[0].uiCoreNum += chain[i]->num_cores;
	}
	
	for(j = 1; j < 3; j++)
	{
		//printf("pll_vid_test_bench Test_bench_Array[%d].uiPll =%d. \n", j, Test_bench_Array[j].uiPll);
		//printf("pll_vid_test_bench Test_bench_Array[%d].uiVol =%d. \n", j, Test_bench_Array[j].uiVol);
		Test_bench_Array[j].uiScore += pll_vid_test_bench(Test_bench_Array[j].uiPll, Test_bench_Array[j].uiVol);
		//printf("Test_bench_Array[%d].uiScore=%d. \n", j, Test_bench_Array[j].uiScore);
		for(i = 0; i < ASIC_CHAIN_NUM; i++)
		{		
	    	Test_bench_Array[j].uiCoreNum += chain[i]->num_cores;
		}
	}

	for(j = 0; j < 3; j++)
	{
		printf("after pll_vid_test_bench Test_bench_Array[%d].uiScore=%d,Test_bench_Array[%d].uiCoreNum=%d. \n", j, Test_bench_Array[j].uiScore, j, Test_bench_Array[j].uiCoreNum);
	}

	int index = 0;
	double cur= 0;
	double max = ((double)(Test_bench_Array[0].uiPll * Test_bench_Array[0].uiCoreNum)) /((double)(Test_bench_Array[0].uiVol)) * ((double)(Test_bench_Array[0].uiScore) / (double)1134);
	printf("max value:%lf. \n", max);
	for(j = 1; j < 3; j++)
	{
		cur = ((double)(Test_bench_Array[j].uiPll * Test_bench_Array[j].uiCoreNum)) /((double)(Test_bench_Array[j].uiVol)) * ((double)(Test_bench_Array[j].uiScore) / (double)1134);
		printf("OutputData[%d]=%lf. \n", j, cur);
		if(cur >= max)
		{
			index = j;
			max = cur;
		}
	}

	printf("The best group is %d!!! \t \n", index);
	config_best_pll_vid(Test_bench_Array[index].uiPll, Test_bench_Array[index].uiVol);
*/
	if (opt_A5_benchmark > 0) {
		A1_printstats_all();
		exit(0);
	}
	return (cnt == 0) ? false : true;
}

#if 0
bool detect_coincraft_desk(void)
{
	static const uint8_t mcp4x_mapping[] = { 0x2c, 0x2b, 0x2a, 0x29, 0x28 };
	board_selector = ccd_board_selector_init();
	if (board_selector == NULL) {
		applog_hw(LOG_INFO, "No CoinCrafd Desk backplane detected.");
		return false;
	}
	board_selector->reset_all();

	int boards_detected = 0;
	int board_id;
	for (board_id = 0; board_id < CCD_MAX_CHAINS; board_id++) {
		uint8_t mcp_slave = mcp4x_mapping[board_id];
		struct mcp4x *mcp = mcp4x_init(mcp_slave);
		if (mcp == NULL)
			continue;

		if (A1_config_options.wiper != 0)
			mcp->set_wiper(mcp, 0, A1_config_options.wiper);

		applog_hw(LOG_WARNING, "checking board %d...", board_id);
		board_selector->select(board_id);

		struct A1_chain *a1 = init_A1_chain(spi, board_id);
		board_selector->release();
		if (a1 == NULL)
			continue;

		struct cgpu_info *cgpu = malloc(sizeof(*cgpu));
		assert(cgpu != NULL);

		memset(cgpu, 0, sizeof(*cgpu));
		cgpu->drv = &bitmineA1_drv;
		cgpu->name = "BitmineA1.CCD";
		cgpu->threads = 1;

		cgpu->device_data = a1;

		a1->cgpu = cgpu;
		add_cgpu(cgpu);
		boards_detected++;
	}
	if (boards_detected == 0)
		return false;

	applog_hw(LOG_WARNING, "Detected CoinCraft Desk with %d boards",
	       boards_detected);
	return true;
}

bool detect_coincraft_rig_v3(void)
{
	board_selector = ccr_board_selector_init();
	if (board_selector == NULL)
		return false;

	board_selector->reset_all();
	int chains_detected = 0;
	int c;
	for (c = 0; c < CCR_MAX_CHAINS; c++) {
		applog_hw(LOG_WARNING, "checking RIG chain %d...", c);

		if (!board_selector->select(c))
			continue;

		struct A1_chain *a1 = init_A1_chain(spi, c);
		board_selector->release();

		if (a1 == NULL)
			continue;

		if (A1_config_options.wiper != 0 && (c & 1) == 0) {
			struct mcp4x *mcp = mcp4x_init(0x28);
			if (mcp == NULL) {
				applog_hw(LOG_ERR, "%d: Cant access poti", c);
			} else {
				mcp->set_wiper(mcp, 0, A1_config_options.wiper);
				mcp->set_wiper(mcp, 1, A1_config_options.wiper);
				mcp->exit(mcp);
				applog_hw(LOG_WARNING, "%d: set wiper to 0x%02x",
					c, A1_config_options.wiper);
			}
		}

		struct cgpu_info *cgpu = malloc(sizeof(*cgpu));
		assert(cgpu != NULL);

		memset(cgpu, 0, sizeof(*cgpu));
		cgpu->drv = &bitmineA1_drv;
		cgpu->name = "BitmineA1.CCR";
		cgpu->threads = 1;

		cgpu->device_data = a1;

		a1->cgpu = cgpu;
		add_cgpu(cgpu);
		chains_detected++;
	}
	if (chains_detected == 0)
		return false;

	applog_hw(LOG_WARNING, "Detected CoinCraft Rig with %d chains",
	       chains_detected);
	return true;
}
#endif

static int A1_read_hwid(void)
{
	FILE *fp;
	size_t nr;
	int ret = 0;

	fp = fopen(MINER_HWID_PATH, "rb");
	if (fp == 0) {
		applog_system(LOG_WARNING, "cannot open dna: %m");
		goto done;
	}

	nr = fread(miner_hwid, MINER_HWID_LENGTH, 1, fp);
	if (nr != 1) {
		applog_system(LOG_WARNING, "failed to read %d bytes from hwid", MINER_HWID_LENGTH);
		goto done_close;
	}
	ret = 1;
done_close:
	fclose(fp);
done:
	return ret;
}


/* Probe SPI channel and register chip chain */
void A1_detect(bool hotplug)
{
	/* no hotplug support for SPI */
	if (hotplug)
		return;

	/* read HWID */
	A1_read_hwid();

	/* parse bimine-a1-options */
	if (opt_bitmine_a1_options != NULL && parsed_config_options == NULL) {
		int ref_clk = 0;
		int sys_clk = 0;
		int spi_clk = 0;
		int override_chip_num = 0;
		int wiper = 0;

		sscanf(opt_bitmine_a1_options, "%d:%d:%d:%d:%d",
		       &ref_clk, &sys_clk, &spi_clk,  &override_chip_num,
		       &wiper);
		if (ref_clk != 0)
			A1_config_options.ref_clk_khz = ref_clk;
		if (sys_clk != 0) {
			if (sys_clk < 100000)
				quit(1, "system clock must be above 100MHz");
			A1_config_options.sys_clk_khz = sys_clk;
		}
		if (spi_clk != 0)
			A1_config_options.spi_clk_khz = spi_clk;
		if (override_chip_num != 0)
			A1_config_options.override_chip_num = override_chip_num;
		if (wiper != 0)
			A1_config_options.wiper = wiper;

		/* config options are global, scan them once */
		parsed_config_options = &A1_config_options;
	}
	applog_hw(LOG_DEBUG, "A1 detect");
    memset(&s_reg_ctrl,0,sizeof(s_reg_ctrl));
    
    inno_fan_init(&s_fan_ctrl);
	set_vid_value(8);
	
	A1Pll1 = A1_ConfigA1PLLClock(opt_A1Pll1);
	A1Pll2 = A1_ConfigA1PLLClock(opt_A1Pll2);
	A1Pll3 = A1_ConfigA1PLLClock(opt_A1Pll3);
	A1Pll4 = A1_ConfigA1PLLClock(opt_A1Pll4);
	A1Pll5 = A1_ConfigA1PLLClock(opt_A1Pll5);
	A1Pll6 = A1_ConfigA1PLLClock(opt_A1Pll6);

#if 0
	/* detect and register supported products */
	if (detect_coincraft_desk())
		return;
	if (detect_coincraft_rig_v3())
		return;
#endif

	if(detect_A1_chain())
	{
		return;
	}


    int i = 0;
	/* release SPI context if no A1 products found */
	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		spi_exit(spi[i]);
	}	
	early_quit(1, "Chain detection failed, refuse to mine!");
	exit(0);
}

#define TEMP_UPDATE_INT_MS	60000
#define RESULTS_UPDATE_INT_MS	10000
#define VOLTAGE_UPDATE_INT_MS  120000
#define TELEMETRY_SUBMIT_INT_MS 120000
#define WRITE_CONFG_TIME  0
#define CHECK_DISABLE_TIME  0

char szShowLog[ASIC_CHAIN_NUM][ASIC_CHIP_NUM][256] = {0};
FILE* fd[ASIC_CHAIN_NUM];
#define  LOG_FILE_PREFIX "/home/www/conf/analys"

/**
 * Monitors the chain and controls its health.
 *
 * This method scans all chips in for temperatures every TEMP_UPDATE_INT_MS and voltages every VOLTAGE_UPDATE_INT_MS.
 * The health control is responsible for:
 * - adjusting fan speed
 * - detecting dangerous temperature - exceeding DANGEROUS_TMP threshold results in complete shutdown and power
 *   disabling for the affected chain
 * - enables chips that may have cooled down already
 *
 * If any temperature exceeds DANGEROUS_TMP, the power of the chain is disabled and the entire application is shut down.
 *
 * @param *chain
 * @param submit_telemetry - submit telemetry every TELEMETRY_SUBMIT_INT_MS
 * @note this method should be called with chain already locked!
 */
static void monitor_and_control_chain_health(struct cgpu_info *cgpu, bool submit_telemetry)
{
	struct A1_chain *chain = cgpu->device_data;
	unsigned long now_ms = get_current_ms();

	if ((now_ms - chain->last_temp_time) > TEMP_UPDATE_INT_MS) {
		chain->last_temp_time = now_ms;
		// TODO jca: to be cleared/removed
		check_disbale_flag[chain->chain_id]++;

		// Reset the number of cores in the chain as check_chip will sum up all working cores while taking
		// the temperature measurement
		chain->num_cores = 0;
		for (int chip_id = chain->num_active_chips; chip_id > 0; chip_id--) {
			// NOTE JCA: check_chip takes chip index as parameter!
			if (!check_chip(chain, chip_id - 1)) {
				applog_hw_chip(LOG_ERR, chain->chain_id, chip_id, "failed to check chip");
				continue;
			}
			inno_fan_temp_add(&s_fan_ctrl, chain->chain_id, chain->chips[chip_id - 1].temp, false);
		}
		inno_fan_speed_update(&s_fan_ctrl, chain->chain_id, cgpu);
		if(inno_fan_temp_get_highest(&s_fan_ctrl, chain->chain_id) > DANGEROUS_TMP) {
			asic_gpio_write(spi[chain->chain_id]->power_en, 0);
			early_quit(1, "Notice chain %d has some temperature problem, disabling power\n", chain->chain_id);
		}
	}

	if ((now_ms - chain->last_voltage_time) > VOLTAGE_UPDATE_INT_MS) {
		chain->last_voltage_time = now_ms;
		show_log[chain->chain_id]++;

		// configure ADC for voltage measurement (vsensor)
		inno_configure_tvsensor(chain, ADDR_BROADCAST, 0);

		for (int chip_id = chain->num_active_chips; chip_id > 0; chip_id--)
		{
			inno_check_voltage(chain, chip_id);
			//applog_hw(LOG_NOTICE, "%d: chip %d: stat:%f/%f/%f/%d\n",chain->chain_id, c, s_reg_ctrl.highest_vol[0][i],s_reg_ctrl.lowest_vol[0][i],s_reg_ctrl.avarge_vol[0][i],s_reg_ctrl.stat_cnt[0][i]);
			if (is_chip_disabled(chain, chip_id))
				continue;
		}

		// switch ADC back to temperature measurement (tsensor)
		inno_configure_tvsensor(chain, ADDR_BROADCAST, 1);
	}
	if (check_disbale_flag[chain->chain_id] > CHECK_DISABLE_TIME)
	{
		applog_hw_chain(LOG_DEBUG, chain->chain_id, "start to check disabled chips");
		check_disabled_chips(chain);
		check_disbale_flag[chain->chain_id] = 0;
	}
	if (((now_ms - chain->last_telemetry_time) > TELEMETRY_SUBMIT_INT_MS) && submit_telemetry) {
		chain->last_telemetry_time = now_ms;
		A1_submit_stats(chain);
	}

}

static int64_t  A1_scanwork(struct thr_info *thr)
{
	int i;
	int32_t A1Pll = 1000;
	struct cgpu_info *cgpu = thr->cgpu;
	struct A1_chain *a1 = cgpu->device_data;
	int32_t nonce_ranges_processed = 0;

	if (a1->num_cores == 0) {
		cgpu->deven = DEV_DISABLED;
		return 0;
	}

	uint32_t nonce;
	uint8_t chip_id;
	uint8_t job_id;
	bool work_updated = false;
	uint16_t micro_job_id;
	uint8_t reg[REG_LENGTH];

	mutex_lock(&a1->lock);
	int cid = a1->chain_id;

	/* perform chain health monitoring and potentially submit telemetry data */
	monitor_and_control_chain_health(cgpu, true);

	/* poll queued results */
	while (true) 
	{
		if (!get_nonce(a1, (uint8_t*)&nonce, &chip_id, &job_id, (uint8_t*)&micro_job_id))
			break;
		nonce = bswap_32(nonce);   //modify for A4
		work_updated = true;
		if (chip_id < 1 || chip_id > a1->num_active_chips) 
		{
			applog_hw_chip(LOG_ERR, cid, chip_id, "wrong chip_id");
			continue;
		}
		if (job_id < 1 && job_id > 4) 
		{
			applog_hw_chip(LOG_ERR, cid, chip_id, "result has wrong job_id %d", job_id);
			flush_spi(a1);
			continue;
		}

		struct A1_chip *chip = &a1->chips[chip_id - 1];
		struct work *work = chip->work[job_id - 1];
		if (work == NULL) 
		{
			/* already been flushed => stale */
			applog_hw_chip(LOG_ERR, cid, chip_id, "stale nonce 0x%08x", nonce);
			chip->stales++;
			continue;
		}
		work->micro_job_id = micro_job_id;
		work->midstate_idx = ffs(micro_job_id) - 1;
		work->chain_id = cid;
		work->chip_id = chip_id;
		/* Index of the midstate used for calculating this results is indicated by a corresponding bit set */
		if ((work->midstate_idx < 0) || (work->midstate_idx >= MIDSTATE_NUM)) {
			applog_hw_chip(LOG_ERR, cid, chip_id, "Invalid midstate index encoded in micro job ID (%d)",
						   micro_job_id);
			chip->hw_errors++;
			continue;

		}
		memcpy(work->data, &n_version[work->midstate_idx].value_big_endian, 4);
		
		if (!submit_nonce(thr, work, nonce)) 
		{
			applog_hw_chip(LOG_ERR, cid, chip_id, "invalid nonce 0x%08x (micro_job_id=%d)",
				nonce, micro_job_id);
			chip->hw_errors++;
			/* add a penalty of a full nonce range on HW errors */
			nonce_ranges_processed--;
			continue;
		}
		a5_debug("YEAH: %d: chip %d / job_id %d: nonce 0x%08x", cid, chip_id, job_id, nonce);
		chip->nonces_found++;
	}

	/* check for completed works */
	if(a1->work_start_delay > 0)
	{
		applog_hw_chain(LOG_INFO, cid, "wait for pll stable");
		a1->work_start_delay--;
	}
	else
	{
		if (inno_cmd_read_reg(a1, 25, reg)) 
		{
			uint8_t qstate = reg[9] & 0x02;
			//hexdump("reg:", reg, REG_LENGTH);
			//printf("qstate: %d \r\n", qstate);
			if (qstate != 0x02)
			{
				work_updated = true;
				for (i = a1->num_active_chips; i > 0; i--) 
				{
					uint8_t c=i;
					struct A1_chip *chip = &a1->chips[i - 1];
					struct work *work = wq_dequeue(&a1->active_wq);
					assert(work != NULL);

					if (set_work(a1, c, work, 0))
					{
						nonce_ranges_processed++;
						chip->nonce_ranges_done++;
					}
					
					if(show_log[cid] > 0)					
					{												
						a5_debug("%d: chip:%d ,core:%d ,job done: %d/%d/%d/%d/%d/%5.2f",
							   cid, c, chip->num_cores,chip->nonce_ranges_done, chip->nonces_found,
							   chip->hw_errors, chip->stales,chip->temp, inno_fan_temp_to_float(&s_fan_ctrl,chip->temp));
						
						sprintf(szShowLog[cid][c-1], "%8d/%8d/%8d/%8d/%8d/%4d/%2d/%2d\r\n",
								chip->nonce_ranges_done, chip->nonces_found,
								chip->hw_errors, chip->stales,chip->temp,chip->num_cores,c-1,cid);
						
						if(i==1) show_log[cid] = 0;

					}
				}
			}
		}

	}

	mutex_unlock(&a1->lock);

	if (nonce_ranges_processed < 0)
	{
		nonce_ranges_processed = 0;
	}

	/* in case of no progress, prevent busy looping */
	if (!work_updated)
		cgsleep_ms(20);
	/*
	cgtime(&a1->tvScryptCurr);
	timersub(&a1->tvScryptCurr, &a1->tvScryptLast, &a1->tvScryptDiff);
	cgtime(&a1->tvScryptLast);

	switch(cgpu->device_id){
		case 0:A1Pll = PLL_Clk_12Mhz[A1Pll1].speedMHz;break;
		case 1:A1Pll = PLL_Clk_12Mhz[A1Pll2].speedMHz;break;
		case 2:A1Pll = PLL_Clk_12Mhz[A1Pll3].speedMHz;break;
		case 3:A1Pll = PLL_Clk_12Mhz[A1Pll4].speedMHz;break;
		case 4:A1Pll = PLL_Clk_12Mhz[A1Pll5].speedMHz;break;
		case 5:A1Pll = PLL_Clk_12Mhz[A1Pll6].speedMHz;break;
		default:;
	}
	*/
	
	//return (int64_t)(( A1Pll * 2 * a1->num_cores) * (a1->tvScryptDiff.tv_usec / 1000000.0));
	return (int64_t)nonce_ranges_processed << 34;
}

static int64_t A1_bench_scanwork(struct cgpu_info *cgpu)
{
	int i;
	int32_t A1Pll = 1000;
	struct A1_chain *a1 = cgpu->device_data;

	uint32_t nonce;
	uint8_t chip_id;
	uint8_t job_id;
	bool work_updated = false;
	uint16_t micro_job_id;
	uint8_t reg[REG_LENGTH];

	mutex_lock(&a1->lock);
	int cid = a1->chain_id;

	/* Perform chain monitoring/health checks, skip statistics submission */
	monitor_and_control_chain_health(cgpu, false);

	/* poll queued results */
	while (true)
	{
		if (!get_nonce(a1, (uint8_t*)&nonce, &chip_id, &job_id, (uint8_t*)&micro_job_id))
			break;
		nonce = bswap_32(nonce);   //modify for A4
		work_updated = true;
		if (chip_id < 1 || chip_id > a1->num_active_chips)
		{
			applog_hw_chip(LOG_ERR, cid, chip_id, "wrong chip_id");
			continue;
		}
		if (job_id < 1 && job_id > 4)
		{
			applog_hw_chip(LOG_ERR, cid, chip_id, "result has wrong job_id %d", job_id);
			flush_spi(a1);
			continue;
		}

		struct A1_chip *chip = &a1->chips[chip_id - 1];

#if 0
		if (!submit_nonce(thr, work, nonce))
		{
			applog_hw(LOG_WARNING, "%d: chip %d: invalid nonce 0x%08x", cid, chip_id, nonce);
			applog_hw(LOG_WARNING, "micro_job_id %d", micro_job_id);
			chip->hw_errors++;
			/* add a penalty of a full nonce range on HW errors */
			nonce_ranges_processed--;
			continue;
		}
#endif
		a5_debug("YEAH: %d: chip %d / job_id %d: nonce 0x%08x", cid, chip_id, job_id, nonce);
		a1->nonces_found++;
		chip->nonces_found++;
	}

	/* check for completed works */
	if(a1->work_start_delay > 0)
	{
		applog_hw_chain(LOG_INFO, cid, "wait for pll stable");
		a1->work_start_delay--;
	}
	else
	{
		if (inno_cmd_read_reg(a1, 25, reg))
		{
			uint8_t qstate = reg[9] & 0x02;
			//hexdump("reg:", reg, REG_LENGTH);
			if (qstate != 0x02)
			{
				work_updated = true;
				for (i = a1->num_active_chips; i > 0; i--)
				{
					uint8_t c=i;
					struct A1_chip *chip = &a1->chips[i - 1];

					if (set_work_benchmark(a1, c, 0))
					{
						chip->nonce_ranges_done++;
						a1->nonce_ranges_done++;
					}

					if(show_log[cid] > 0)
					{
						a5_debug("%d: chip:%d ,core:%d ,job done: %d/%d/%d/%d/%d/%5.2f",
								cid, c, chip->num_cores,chip->nonce_ranges_done, chip->nonces_found,
								chip->hw_errors, chip->stales,chip->temp, inno_fan_temp_to_float(&s_fan_ctrl,chip->temp));

						sprintf(szShowLog[cid][c-1], "%8d/%8d/%8d/%8d/%8d/%4d/%2d/%2d\r\n",
								chip->nonce_ranges_done, chip->nonces_found,
								chip->hw_errors, chip->stales,chip->temp,chip->num_cores,c-1,cid);

						if(i==1) show_log[cid] = 0;

					}
				}
			}
		}

	}


	mutex_unlock(&a1->lock);

	/* in case of no progress, prevent busy looping */
	if (!work_updated)
		cgsleep_ms(20);

	return 0;
}


/* queue two work items per chip in chain */
static bool A1_queue_full(struct cgpu_info *cgpu)
{
	struct A1_chain *a1 = cgpu->device_data;
	int queue_full = false;

	mutex_lock(&a1->lock);
	//applog_hw(LOG_DEBUG, "%d, A1 running queue_full: %d/%d",
	//       a1->chain_id, a1->active_wq.num_elems, a1->num_active_chips);

	if (a1->active_wq.num_elems >= a1->num_active_chips * 2)
		queue_full = true;
	else
		wq_enqueue(&a1->active_wq, get_queued(cgpu));

	mutex_unlock(&a1->lock);

	return queue_full;
}

static void A1_flush_work(struct cgpu_info *cgpu)
{
	struct A1_chain *a1 = cgpu->device_data;
	int cid = a1->chain_id;
	//board_selector->select(cid);
	int i;

	mutex_lock(&a1->lock);
	/* stop chips hashing current work */
	//if (!abort_work(a1)) 
	//{
	//	applog_hw(LOG_ERR, "%d: failed to abort work in chip chain!", cid);
	//}
	/* flush the work chips were currently hashing */
	for (i = 0; i < a1->num_active_chips; i++) 
	{
		int j;
		struct A1_chip *chip = &a1->chips[i];
		for (j = 0; j < 2; j++) 
		{
			struct work *work = chip->work[j];
			if (work == NULL)
				continue;
			//applog_hw(LOG_DEBUG, "%d: flushing chip %d, work %d: 0x%p",
			//       cid, i, j + 1, work);
			work_completed(cgpu, work);
			chip->work[j] = NULL;
		}
		
		chip->last_queued_id = 0;

		//applog_hw(LOG_INFO, "chip :%d flushing queued work success", i);
	}
/*
	if(!inno_cmd_resetjob(a1, ADDR_BROADCAST))
	{
		applog_hw(LOG_WARNING, "chip clear work false");
	}
*/		
	/* flush queued work */
	applog_hw_chain(LOG_DEBUG, cid, "flushing queued work...");
	while (a1->active_wq.num_elems > 0) 
	{
		struct work *work = wq_dequeue(&a1->active_wq);
		assert(work != NULL);
		work_completed(cgpu, work);
	}
	mutex_unlock(&a1->lock);

	//board_selector->release();
}

static void A1_get_statline_before(char *buf, size_t len, struct cgpu_info *cgpu)
{
	struct A1_chain *a1 = cgpu->device_data;
	char temp[10];
	if (a1->temp != 0)
		snprintf(temp, 9, "%2dC", a1->temp);
	tailsprintf(buf, len, " %2d:%2d/%3d %s",
		    a1->chain_id, a1->num_active_chips, a1->num_cores,
		    a1->temp == 0 ? "   " : temp);
}

static void A1_power_off(void)
{
	if (chip_power_enabled)
		shutdown_all_chains();
}

struct device_drv bitmineA1_drv = {
	.drv_id = DRIVER_bitmineA1,
	.dname = "BitmineA1",
	.name = "BA1",
	.drv_detect = A1_detect,

	.hash_work = hash_queued_work,
	.scanwork = A1_scanwork,
	.queue_full = A1_queue_full,
	.flush_work = A1_flush_work,
	.hw_power_off = A1_power_off,
	.get_statline_before = A1_get_statline_before,
};
