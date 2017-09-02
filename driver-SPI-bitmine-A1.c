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

#include "spi-context.h"
#include "logging.h"
#include "miner.h"
#include "util.h"

#include "A1-board-selector.h"
#include "A1-trimpot-mcp4x.h"

#include "asic_inno.h"
#include "asic_inno_clock.h"
#include "asic_inno_cmd.h"
#include "asic_inno_gpio.h"

#include "inno_fan.h"

struct spi_config cfg[ASIC_CHAIN_NUM];
struct spi_ctx *spi[ASIC_CHAIN_NUM];
struct A1_chain *chain[ASIC_CHAIN_NUM];

static uint8_t A1Pll1=A5_PLL_CLOCK_800MHz;
static uint8_t A1Pll2=A5_PLL_CLOCK_800MHz;
static uint8_t A1Pll3=A5_PLL_CLOCK_800MHz;
static uint8_t A1Pll4=A5_PLL_CLOCK_800MHz;
static uint8_t A1Pll5=A5_PLL_CLOCK_800MHz;
static uint8_t A1Pll6=A5_PLL_CLOCK_800MHz;

/* FAN CTRL */
static INNO_FAN_CTRL_T s_fan_ctrl;

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
	a1->chips = NULL;
	a1->spi_ctx = NULL;
	free(a1);
}

struct A1_chain *init_A1_chain(struct spi_ctx *ctx, int chain_id)
{
	int i;
	struct A1_chain *a1 = malloc(sizeof(*a1));
	assert(a1 != NULL);

	applog(LOG_DEBUG, "%d: A1 init chain", chain_id);
	
	memset(a1, 0, sizeof(*a1));
	a1->spi_ctx = ctx;
	a1->chain_id = chain_id;

	applog(LOG_INFO,"chain_id:%d", chain_id);
	switch(chain_id){
		case 0:a1->num_chips = chain_detect(a1, A1Pll1);break;
		case 1:a1->num_chips = chain_detect(a1, A1Pll2);break;
		case 2:a1->num_chips = chain_detect(a1, A1Pll3);break;
		case 3:a1->num_chips = chain_detect(a1, A1Pll4);break;
		case 4:a1->num_chips = chain_detect(a1, A1Pll5);break;
		case 5:a1->num_chips = chain_detect(a1, A1Pll6);break;
		default:;
	}
	usleep(10000);
	
	if (a1->num_chips == 0)
		goto failure;

	applog(LOG_WARNING, "spidev%d.%d: %d: Found %d A1 chips",
	       a1->spi_ctx->config.bus, a1->spi_ctx->config.cs_line,
	       a1->chain_id, a1->num_chips);
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
		applog(LOG_WARNING, "%d: limiting chain to %d chips",
		       a1->chain_id, a1->num_active_chips);
	}

	a1->chips = calloc(a1->num_active_chips, sizeof(struct A1_chip));
	assert (a1->chips != NULL);

	if (!inno_cmd_bist_fix(a1, ADDR_BROADCAST))
		goto failure;

	usleep(200);

	for (i = 0; i < a1->num_active_chips; i++)
    {
		check_chip(a1, i);

        /* 温度值 */
        inno_fan_temp_add(&s_fan_ctrl, chain_id, a1->chips[i].temp);
    }
    /* 设置初始值 */ 
    inno_fan_temp_init(&s_fan_ctrl, chain_id);

	applog(LOG_WARNING, "%d: found %d chips with total %d active cores",
	       a1->chain_id, a1->num_active_chips, a1->num_cores);

	mutex_init(&a1->lock);
	INIT_LIST_HEAD(&a1->active_wq.head);

	return a1;

failure:
	exit_A1_chain(a1);
	return NULL;
}

static bool detect_A1_chain(void)
{
	int i;
	//board_selector = (struct board_selector*)&dummy_board_selector;
	applog(LOG_WARNING, "A1: checking A1 chain");

	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		cfg[i].bus     = i;
		cfg[i].cs_line = 0;
		cfg[i].mode    = SPI_MODE_1;
		cfg[i].speed   = DEFAULT_SPI_SPEED;
		cfg[i].bits    = DEFAULT_SPI_BITS_PER_WORD;
		cfg[i].delay   = DEFAULT_SPI_DELAY_USECS;

		spi[i] = spi_init(&cfg[i]);
		if(spi[i] == NULL)
		{
			applog(LOG_ERR, "spi init fail");
			return false;
		}

		spi[i]->power_en = SPI_PIN_POWER_EN[i];		
		spi[i]->start_en = SPI_PIN_START_EN[i];		
		spi[i]->reset = SPI_PIN_RESET[i];
		//spi[i]->plug  = SPI_PIN_PLUG[i];
		//spi[i]->led   = SPI_PIN_LED[i];
		

		asic_gpio_init(spi[i]->power_en, 0);
		asic_gpio_init(spi[i]->start_en, 0);
		asic_gpio_init(spi[i]->reset, 0);
		//asic_gpio_init(spi[i]->plug, 0);
		//asic_gpio_init(spi[i]->led, 0);
	}

	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		asic_gpio_write(spi[i]->power_en, 1);
		sleep(1);
		asic_gpio_write(spi[i]->start_en, 1);
		asic_gpio_write(spi[i]->reset, 1);
		sleep(1);
		asic_gpio_write(spi[i]->reset, 0);
		sleep(1);
		asic_gpio_write(spi[i]->reset, 1);	
		sleep(1);
	}

	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		chain[i] = init_A1_chain(spi[i], i);
		if (chain[i] == NULL)
		{
			applog(LOG_ERR, "init a1 chain fail");
			return false;
		}

		struct cgpu_info *cgpu = malloc(sizeof(*cgpu));
		assert(cgpu != NULL);
	
		memset(cgpu, 0, sizeof(*cgpu));
		cgpu->drv = &bitmineA1_drv;
		cgpu->name = "BitmineA1.SingleChain";
		cgpu->threads = 1;

		cgpu->device_data = chain[i];

		chain[i]->cgpu = cgpu;
		add_cgpu(cgpu);

		applog(LOG_WARNING, "Detected the %d A1 chain with %d chips / %d cores",
		       i, chain[i]->num_active_chips, chain[i]->num_cores);
	}


	return true;
}

#if 0
bool detect_coincraft_desk(void)
{
	static const uint8_t mcp4x_mapping[] = { 0x2c, 0x2b, 0x2a, 0x29, 0x28 };
	board_selector = ccd_board_selector_init();
	if (board_selector == NULL) {
		applog(LOG_INFO, "No CoinCrafd Desk backplane detected.");
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

		applog(LOG_WARNING, "checking board %d...", board_id);
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

	applog(LOG_WARNING, "Detected CoinCraft Desk with %d boards",
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
		applog(LOG_WARNING, "checking RIG chain %d...", c);

		if (!board_selector->select(c))
			continue;

		struct A1_chain *a1 = init_A1_chain(spi, c);
		board_selector->release();

		if (a1 == NULL)
			continue;

		if (A1_config_options.wiper != 0 && (c & 1) == 0) {
			struct mcp4x *mcp = mcp4x_init(0x28);
			if (mcp == NULL) {
				applog(LOG_ERR, "%d: Cant access poti", c);
			} else {
				mcp->set_wiper(mcp, 0, A1_config_options.wiper);
				mcp->set_wiper(mcp, 1, A1_config_options.wiper);
				mcp->exit(mcp);
				applog(LOG_WARNING, "%d: set wiper to 0x%02x",
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

	applog(LOG_WARNING, "Detected CoinCraft Rig with %d chains",
	       chains_detected);
	return true;
}
#endif

/* Probe SPI channel and register chip chain */
void A1_detect(bool hotplug)
{
	/* no hotplug support for SPI */
	if (hotplug)
		return;

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
	applog(LOG_DEBUG, "A1 detect");

    /* 初始化风扇控制 */
    inno_fan_init(&s_fan_ctrl);
	
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

	applog(LOG_WARNING, "A1 dectect finish");

    int i = 0;
	/* release SPI context if no A1 products found */
	for(i = 0; i < ASIC_CHAIN_NUM; i++)
	{
		spi_exit(spi[i]);
	}	
}

#define TEMP_UPDATE_INT_MS	2000
static int64_t A1_scanwork(struct thr_info *thr)
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

	//board_selector->select(a1->chain_id);
	//applog(LOG_DEBUG, "A1 running scanwork");

	uint32_t nonce;
	uint8_t chip_id;
	uint8_t job_id;
	bool work_updated = false;

	mutex_lock(&a1->lock);

	if (a1->last_temp_time + TEMP_UPDATE_INT_MS < get_current_ms())
	{
		//a1->temp = board_selector->get_temp(0);
		a1->last_temp_time = get_current_ms();
	}
	int cid = a1->chain_id; 

	/* poll queued results */
	while (true)
	{
		if (!get_nonce(a1, (uint8_t*)&nonce, &chip_id, &job_id))
			break;

		//nonce = bswap_32(nonce);   //modify for A4

		work_updated = true;
		if (chip_id < 1 || chip_id > a1->num_active_chips) 
		{
			applog(LOG_WARNING, "%d: wrong chip_id %d", cid, chip_id);
			continue;
		}
		if (job_id < 1 && job_id > 4) 
		{
			applog(LOG_WARNING, "%d: chip %d: result has wrong ""job_id %d", cid, chip_id, job_id);
			flush_spi(a1);
			continue;
		}

		struct A1_chip *chip = &a1->chips[chip_id - 1];
		struct work *work = chip->work[job_id - 1];
		if (work == NULL) 
		{
			/* already been flushed => stale */
			applog(LOG_WARNING, "%d: chip %d: stale nonce 0x%08x", cid, chip_id, nonce);
			chip->stales++;
			continue;
		}
		if (!submit_nonce(thr, work, nonce)) 
		{
			applog(LOG_WARNING, "%d: chip %d: invalid nonce 0x%08x", cid, chip_id, nonce);
			chip->hw_errors++;
			/* add a penalty of a full nonce range on HW errors */
			nonce_ranges_processed--;
			continue;
		}
		applog(LOG_INFO, "YEAH: %d: chip %d / job_id %d: nonce 0x%08x", cid, chip_id, job_id, nonce);
		chip->nonces_found++;
	}

	uint8_t reg[REG_LENGTH];
	/* check for completed works */
	if(a1->work_start_delay > 0)
	{
		applog(LOG_INFO, "wait for pll stable");
		a1->work_start_delay--;
	}
	else
	{
		for (i = a1->num_active_chips; i > 0; i--) 
		{
			uint8_t c = i;
			if (is_chip_disabled(a1, c))
				continue;
			if (!inno_cmd_read_reg(a1, c, reg)) 
			{
				disable_chip(a1, c);
				continue;
			}
//            else
//            {
//                /* update temp database */
//                uint32_t temp = 0;
//                float    temp_f = 0.0f;

//                temp = 0x000003ff & ((reg[7] << 8) | reg[8]);
//                inno_fan_temp_add(&s_fan_ctrl, cid, temp);
//            }

			uint8_t qstate = reg[9] & 0x01;
			uint8_t qbuff = 0;
			struct work *work;
			struct A1_chip *chip = &a1->chips[i - 1];
			switch(qstate) 
			{
			
			case 1:
				//applog(LOG_INFO, "chip %d busy now", i);
				break;
				/* fall through */
			case 0:
				work_updated = true;

				work = wq_dequeue(&a1->active_wq);
				if (work == NULL) 
				{
					applog(LOG_INFO, "%d: chip %d: work underflow", cid, c);
					break;
				}
				if (set_work(a1, c, work, qbuff)) 
				{
					chip->nonce_ranges_done++;
					nonce_ranges_processed++;
					applog(LOG_INFO, "set work success %d, nonces processed %d", cid, nonce_ranges_processed);
				}
				
				//applog(LOG_INFO, "%d: chip %d: job done: %d/%d/%d/%d",
				//       cid, c,
				//       chip->nonce_ranges_done, chip->nonces_found,
				//       chip->hw_errors, chip->stales);
				break;
			}
		} 
        //inno_fan_speed_update(&s_fan_ctrl, cid);
	}

	switch(cid){
		case 0:check_disabled_chips(a1, A1Pll1);;break;
		case 1:check_disabled_chips(a1, A1Pll2);;break;
		case 2:check_disabled_chips(a1, A1Pll3);;break;
		case 3:check_disabled_chips(a1, A1Pll4);;break;
		case 4:check_disabled_chips(a1, A1Pll5);;break;
		case 5:check_disabled_chips(a1, A1Pll6);;break;
		default:;
	}

	mutex_unlock(&a1->lock);

	//board_selector->release();

	if (nonce_ranges_processed < 0)
	{
		applog(LOG_INFO, "nonce_ranges_processed less than 0");
		nonce_ranges_processed = 0;
	}

	if (nonce_ranges_processed != 0) 
	{
		applog(LOG_INFO, "%d, nonces processed %d", cid, nonce_ranges_processed);
	}
	/* in case of no progress, prevent busy looping */
	//if (!work_updated)
	//	cgsleep_ms(40);

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


	return (int64_t)(2011173.18 * A1Pll / 1000 * (a1->num_cores/9.0) * (a1->tvScryptDiff.tv_usec / 1000000.0));

}


/* queue two work items per chip in chain */
static bool A1_queue_full(struct cgpu_info *cgpu)
{
	struct A1_chain *a1 = cgpu->device_data;
	int queue_full = false;

	mutex_lock(&a1->lock);
	//applog(LOG_DEBUG, "%d, A1 running queue_full: %d/%d",
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
	if (!abort_work(a1)) 
	{
		applog(LOG_ERR, "%d: failed to abort work in chip chain!", cid);
	}
	/* flush the work chips were currently hashing */
	for (i = 0; i < a1->num_active_chips; i++) 
	{
		int j;
		struct A1_chip *chip = &a1->chips[i];
		for (j = 0; j < 1; j++) 
		{
			struct work *work = chip->work[j];
			if (work == NULL)
				continue;
			//applog(LOG_DEBUG, "%d: flushing chip %d, work %d: 0x%p",
			//       cid, i, j + 1, work);
			work_completed(cgpu, work);
			chip->work[j] = NULL;
		}
		
		chip->last_queued_id = 0;

		if(!inno_cmd_resetjob(a1, i+1))
		{
			applog(LOG_WARNING, "chip %d clear work false", i);
			continue;
		}
		
		//applog(LOG_INFO, "chip :%d flushing queued work success", i);
	}
	/* flush queued work */
	//applog(LOG_DEBUG, "%d: flushing queued work...", cid);
	while (a1->active_wq.num_elems > 0) 
	{
		struct work *work = wq_dequeue(&a1->active_wq);
		assert(work != NULL);
		work_completed(cgpu, work);
	}
	mutex_unlock(&a1->lock);

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

struct device_drv bitmineA1_drv = {
	.drv_id = DRIVER_bitmineA1,
	.dname = "BitmineA1",
	.name = "BA1",
	.drv_detect = A1_detect,

	.hash_work = hash_queued_work,
	.scanwork = A1_scanwork,
	.queue_full = A1_queue_full,
	.flush_work = A1_flush_work,
	.get_statline_before = A1_get_statline_before,
};
