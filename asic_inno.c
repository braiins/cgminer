#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>

#include "logging.h"
#include "miner.h"
#include "util.h"

#include "asic_inno.h"
#include "asic_inno_cmd.h"
#include "asic_inno_clock.h"


int opt_diff=15;

static const uint8_t difficult_Tbl[24][4] = {
	{0x1e, 0xff, 0xff, 0xff},	// 1
	{0x1e, 0x7f, 0xff, 0xff},	// 2
	{0x1e, 0x3f, 0xff, 0xff},	// 4
	{0x1e, 0x1f, 0xff, 0xff},	// 8
	{0x1e, 0x0f, 0xff, 0xff},	// 16
	{0x1e, 0x07, 0xff, 0xff},	// 32
	{0x1e, 0x03, 0xff, 0xff},	// 64 
	{0x1e, 0x01, 0xff, 0xff},	// 128
	{0x1e, 0x00, 0xff, 0xff},	// 256
	{0x1e, 0x00, 0x7f, 0xff},	// 512
	{0x1e, 0x00, 0x3f, 0xff},	// 1024
	{0x1e, 0x00, 0x1f, 0xff},	// 2048
	{0x1e, 0x00, 0x0f, 0xff},	// 4096
	{0x1e, 0x00, 0x07, 0xff},	// 8192
	{0x1e, 0x00, 0x03, 0xff},	// 16384
	{0x1e, 0x00, 0x01, 0xff},	// 32768
	{0x1e, 0x00, 0x00, 0xff}	// 65536
};

static const uint8_t default_reg[14][12] = 
{
	{0x07, 0x45, 0x40, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //1.3GHz  0xE482 
	{0x07, 0x2C, 0x40, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //1.2GHz  0x781E 
	{0x07, 0x13, 0x40, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //1.1GHz  0x2C4A  
	{0x06, 0xFA, 0x40, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //1.0GHz  0x32D0  
	{0x06, 0xE1, 0x40, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //900MHz 0x16F4
	{0x06, 0xD7, 0x40, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //860MHz 0x5EBC  
	{0x06, 0xC8, 0x40, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //800MHz 0xB569 
	{0x06, 0xAF, 0x40, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //700MHz 0xFE1C 
	{0x07, 0x2C, 0x40, 0x40, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //600MHz 0xAC1C  
	{0x06, 0xFA, 0x40, 0x40, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //500MHz 0xE6D2  
	{0x06, 0xC8, 0x40, 0x40, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //400MHz 0x616B 
	{0x07, 0x2C, 0x40, 0x80, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //300MHz 0x9019 
	{0x06, 0xC8, 0x40, 0x80, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //200MHz 0x5D6E  
	{0x02, 0x50, 0x40, 0xC0, 0x00, 0x00, 0x00, 0xA8, 0x00, 0x24, 0xFF, 0xFF},  //120MHz 0xE75B 
};



static void rev(unsigned char *s, size_t l)
{
	size_t i, j;
	unsigned char t;

	for (i = 0, j = l - 1; i < j; i++, j--) {
		t = s[i];
		s[i] = s[j];
		s[j] = t;
	}
}

static uint8_t *create_job(uint8_t chip_id, uint8_t job_id, struct work *work)
{
	unsigned char *wdata = work->data;
	double sdiff = work->sdiff;
	uint8_t tmp_buf[JOB_LENGTH];
	uint16_t crc;
	uint8_t i;
			
	static uint8_t job[JOB_LENGTH] = {
		/* command */
		0x00, 0x00,
		/* wdata 63 to 0 */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* start nonce */
		0x00, 0x00, 0x00, 0x00,
		/* wdata 75 to 64 */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		/* difficulty */
		0x00, 0x00, 0x00, 0x00,
		/* end nonce */
		0x00, 0x00, 0x00, 0x00,
//		/* crc data */
//		0x00, 0x00
	};
	
	uint8_t diffIdx;
	uint8_t data63to0[64];
	uint8_t data75to64[12];
	uint8_t diff[4] = {0x1e, 0x03, 0xff, 0xff};
	uint8_t startnonce[4] = {0x00, 0x00, 0x00, 0x00};	
	uint8_t endnonce[4] = {0x00, 0x40, 0x00, 0x00};	// 10s

	memcpy(data63to0, wdata, 64);
	memcpy(data75to64, wdata+64, 12);

	if(sdiff > 65535.0)
		memcpy(diff, difficult_Tbl[16], 4);
	else if(sdiff > 32767.0)
	        memcpy(diff, difficult_Tbl[15], 4);
	else if(sdiff > 16383.0)
	        memcpy(diff, difficult_Tbl[14], 4);
	else if(sdiff > 8191.0)
	        memcpy(diff, difficult_Tbl[13], 4);
	else if(sdiff > 4095.0)
                memcpy(diff, difficult_Tbl[12], 4);
	else if(sdiff > 2047.0)
		memcpy(diff, difficult_Tbl[11], 4);
	else if(sdiff > 1023.0)
		memcpy(diff, difficult_Tbl[10], 4);
	else if(sdiff > 511.0)
		memcpy(diff, difficult_Tbl[9], 4);
	else if(sdiff > 255.0)
		memcpy(diff, difficult_Tbl[8], 4);
	else {
		if(opt_diff>=1&&opt_diff<=8)
		{
			diffIdx=opt_diff-1;
			memcpy(diff, difficult_Tbl[diffIdx], 4);
		}
		else
		{
			memcpy(diff, difficult_Tbl[7], 4);
		}
	}
	
	startnonce[0]=0x00;
	startnonce[1]=0x00;
	startnonce[2]=0x00;
	startnonce[3]=0x00;
	
	endnonce[0]=0x0f;
	endnonce[1]=0xff;
	endnonce[2]=0xff;
	endnonce[3]=0xff;

	rev(data63to0, 64);
	rev(startnonce, 4);
	rev(data75to64, 12);
	rev(diff, 4);
	rev(endnonce, 4);

	job[0] = (job_id << 4) | CMD_WRITE_JOB;
	job[1] = chip_id;
	memcpy(job+2, 			data63to0,  64);
	memcpy(job+2+64, 			startnonce, 4);
	memcpy(job+2+64+4, 		data75to64, 12);
	memcpy(job+2+64+4+12, 	diff, 4);
	memcpy(job+2+64+4+12+4, 	endnonce, 4);

    /* crc */
#ifdef CHIP_A6
    memset(tmp_buf, 0, sizeof(tmp_buf));
    for(i = 0; i < 45; i++)
    {
        tmp_buf[(2 * i) + 1] = job[(2 * i) + 0];
        tmp_buf[(2 * i) + 0] = job[(2 * i) + 1];
    }
    crc = CRC16_2(tmp_buf, 90);
    job[90] = (uint8_t)((crc >> 8) & 0xff);
    job[91] = (uint8_t)((crc >> 0) & 0xff);
#endif

	return job;
}


#define COOLDOWN_MS			(30 * 1000)
#define DISABLE_CHIP_FAIL_THRESHOLD	3
/********** disable / re-enable related section (temporary for testing) */
int get_current_ms(void)
{
	cgtimer_t ct;
	cgtimer_time(&ct);
	return cgtimer_to_ms(&ct);
}

bool is_chip_disabled(struct A1_chain *a1, uint8_t chip_id)
{
	struct A1_chip *chip = &a1->chips[chip_id - 1];
	return chip->disabled || chip->cooldown_begin != 0;
}

/* check and disable chip, remember time */
void disable_chip(struct A1_chain *a1, uint8_t chip_id)
{
	flush_spi(a1);
	struct A1_chip *chip = &a1->chips[chip_id - 1];
	int cid = a1->chain_id;
	if (is_chip_disabled(a1, chip_id)) {
		applog(LOG_WARNING, "%d: chip %d already disabled",
		       cid, chip_id);
		return;
	}
	applog(LOG_WARNING, "%d: temporary disabling chip %d", cid, chip_id);
	chip->cooldown_begin = get_current_ms();
}

/* check if disabled chips can be re-enabled */
void check_disabled_chips(struct A1_chain *a1)
{
	int i;
	int cid = a1->chain_id;
	uint8_t reg[REG_LENGTH];

	for (i = 0; i < a1->num_active_chips; i++) 
	{
		int chip_id = i + 1;
		struct A1_chip *chip = &a1->chips[i];
		if (!is_chip_disabled(a1, chip_id))
			continue;
		/* do not re-enable fully disabled chips */
		if (chip->disabled)
			continue;
		if (chip->cooldown_begin + COOLDOWN_MS > get_current_ms())
			continue;
		if (!inno_cmd_read_reg(a1, chip_id, reg)) 
		{
			chip->fail_count++;
			applog(LOG_WARNING, "%d: chip %d not yet working - %d",
			       cid, chip_id, chip->fail_count);
			if (chip->fail_count > DISABLE_CHIP_FAIL_THRESHOLD) 
			{
				applog(LOG_WARNING, "%d: completely disabling chip %d at %d",
				       cid, chip_id, chip->fail_count);
				chip->disabled = true;
				a1->num_cores -= chip->num_cores;
				continue;
			}
			/* restart cooldown period */
			chip->cooldown_begin = get_current_ms();
			continue;
		}
		applog(LOG_WARNING, "%d: chip %d is working again", cid, chip_id);
		chip->cooldown_begin = 0;
		chip->fail_count = 0;
	}
}



bool set_work(struct A1_chain *a1, uint8_t chip_id, struct work *work, uint8_t queue_states)
{
	int cid = a1->chain_id;
	struct A1_chip *chip = &a1->chips[chip_id - 1];
	bool retval = false;

	int job_id = chip->last_queued_id + 1;

	applog(LOG_INFO, "%d: queuing chip %d with job_id %d, state=0x%02x", cid, chip_id, job_id, queue_states);
	if (job_id == (queue_states & 0x0f) || job_id == (queue_states >> 4))
	{
		applog(LOG_WARNING, "%d: job overlap: %d, 0x%02x", cid, job_id, queue_states);
	}

	if (chip->work[chip->last_queued_id] != NULL) 
	{
		work_completed(a1->cgpu, chip->work[chip->last_queued_id]);
		chip->work[chip->last_queued_id] = NULL;	
		retval = true;
	}
	
	uint8_t *jobdata = create_job(chip_id, job_id, work);
	if (!inno_cmd_write_job(a1, chip_id, jobdata)) 
	{
		/* give back work */
		work_completed(a1->cgpu, work);
		applog(LOG_ERR, "%d: failed to set work for chip %d.%d", cid, chip_id, job_id);
		disable_chip(a1, chip_id);
	} 
	else 
	{
		chip->work[chip->last_queued_id] = work;
		chip->last_queued_id++;
		chip->last_queued_id &= 3;
	}
	
	return retval;
}


bool get_nonce(struct A1_chain *a1, uint8_t *nonce, uint8_t *chip_id, uint8_t *job_id)
{
	uint8_t buffer[64];

	memset(buffer, 0, sizeof(buffer));
	if(inno_cmd_read_result(a1, ADDR_BROADCAST, buffer))
	{
		*job_id = buffer[0] >> 4;
		*chip_id = buffer[1];

		memcpy(nonce, buffer + 2, 4);

		applog(LOG_DEBUG, "Got nonce for chip %d / job_id %d",
			   *chip_id, *job_id);
		
		return true;
	}
	
	return false;
}

bool abort_work(struct A1_chain *a1)
{

	applog(LOG_INFO,"Start to reset ");

	return true;
}

bool check_chip(struct A1_chain *a1, int i)
{
	uint8_t buffer[64];
	int chip_id = i + 1;
	int cid = a1->chain_id;

	memset(buffer, 0, sizeof(buffer));
	if (!inno_cmd_read_reg(a1, chip_id, buffer)) 
	{
		applog(LOG_WARNING, "%d: Failed to read register for "
			"chip %d -> disabling", cid, chip_id);
		a1->chips[i].num_cores = 0;
		a1->chips[i].disabled = 1;
		return false;;
	}

	a1->chips[i].num_cores = buffer[11];
	a1->num_cores += a1->chips[i].num_cores;
	applog(LOG_WARNING, "%d: Found chip %d with %d active cores",
	       cid, chip_id, a1->chips[i].num_cores);

	//keep ASIC register value
	memcpy(a1->chips[i].reg, buffer, 12);
	a1->chips[i].temp= (buffer[8]<<8)|buffer[9];

	if (a1->chips[i].num_cores < BROKEN_CHIP_THRESHOLD) 
	{
		applog(LOG_WARNING, "%d: broken chip %d with %d active "
		       "cores (threshold = %d)", cid, chip_id,
		       a1->chips[i].num_cores, BROKEN_CHIP_THRESHOLD);

		//set low pll
		//A1_SetA1PLLClock(a1, BROKEN_CHIP_SYS_CLK, chip_id);
		//cmd_READ_REG(a1, chip_id);
		hexdump_error("new.PLL", a1->spi_rx, 8);
		a1->chips[i].disabled = true;
		a1->num_cores -= a1->chips[i].num_cores;
		
		return false;
	}

	if (a1->chips[i].num_cores < WEAK_CHIP_THRESHOLD) 
	{
		applog(LOG_WARNING, "%d: weak chip %d with %d active "
		       "cores (threshold = %d)", cid,
		       chip_id, a1->chips[i].num_cores, WEAK_CHIP_THRESHOLD);

		//A1_SetA1PLLClock(a1, WEAK_CHIP_SYS_CLK, chip_id);
		//cmd_READ_REG(a1, chip_id);
		hexdump_error("new.PLL", a1->spi_rx, 8);
		
		return false;
	}

	return true;
}

/*
 * BIST_START works only once after HW reset, on subsequent calls it
 * returns 0 as number of chips.
 */
int chain_detect(struct A1_chain *a1)
{
	uint8_t buffer[64];
	int cid = a1->chain_id;
	uint8_t temp_reg[REG_LENGTH];

	inno_cmd_reset(a1, ADDR_BROADCAST);

	sleep(1);

	memset(buffer, 0, sizeof(buffer));
	if(!inno_cmd_bist_start(a1, 0, buffer))
	{
		applog(LOG_WARNING, "bist start fail");
		return -1;
	}
	a1->num_chips = buffer[3]; 
	applog(LOG_WARNING, "%d: detected %d chips", cid, a1->num_chips);

	sleep(1);

	//add for A6
	memcpy(temp_reg, default_reg[A5_PLL_CLOCK_300MHz], REG_LENGTH);
	if(!inno_cmd_write_reg(a1, ADDR_BROADCAST, temp_reg))
	{
		applog(LOG_WARNING, "set default PLL fail");
		return -1;
	}
	applog(LOG_WARNING, "set default PLL success");

	sleep(1);
	
	if(!inno_cmd_bist_collect(a1, ADDR_BROADCAST))
	{
		applog(LOG_WARNING, "bist collect fail");
		return -1;
	}

	applog(LOG_WARNING, "collect core success");
	applog(LOG_WARNING, "%d: no A1 chip-chain detected", cid);
	return a1->num_chips;

}



