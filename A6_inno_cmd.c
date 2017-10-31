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

#include "A6_inno.h"
#include "A6_inno_cmd.h"
#include "A6_inno_clock.h"



const unsigned short wCRCTalbeAbs[] =
{
	0x0000, 0xCC01, 0xD801, 0x1400, 
	0xF001, 0x3C00, 0x2800, 0xE401, 
	0xA001, 0x6C00, 0x7800, 0xB401, 
	0x5000, 0x9C01, 0x8801, 0x4400,
};


unsigned short CRC16_2(unsigned char* pchMsg, unsigned short wDataLen)
{
	volatile unsigned short wCRC = 0xFFFF;
	unsigned short i;
	unsigned char chChar;

	for (i = 0; i < wDataLen; i++)
	{
		chChar = *pchMsg++;
		wCRC = wCRCTalbeAbs[(chChar ^ wCRC) & 15] ^ (wCRC >> 4);
		wCRC = wCRCTalbeAbs[((chChar >> 4) ^ wCRC) & 15] ^ (wCRC >> 4);
	}

	return wCRC;
}



static void applog_hexdump(char *prefix, uint8_t *buff, int len, int level)
{
	static char line[512];
	char *pos = line;
	int i;
	if (len < 1)
	{
		return;
	}

	pos += sprintf(pos, "%s: %d bytes:", prefix, len);
	for (i = 0; i < len; i++) 
	{
		if (i > 0 && (i % 32) == 0) 
		{
			applog(LOG_INFO, "%s", line);
			pos = line;
			pos += sprintf(pos, "\t");
		}
		pos += sprintf(pos, "%.2X ", buff[i]);
	}
	applog(level, "%s", line);
}

void hexdump(char *prefix, uint8_t *buff, int len)
{
	applog_hexdump(prefix, buff, len, LOG_WARNING);
}

void hexdump_error(char *prefix, uint8_t *buff, int len)
{
	applog_hexdump(prefix, buff, len, LOG_ERR);
}

void flush_spi(struct A1_chain *pChain)
{
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	struct spi_ctx *ctx = pChain->spi_ctx;

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	spi_write_data(ctx, spi_tx, 64);
}

bool spi_send_zero(struct spi_ctx *ctx, uint8_t *txbuf, int len)
{
	bool ret;
	int index = 0;
	uint8_t spi_tx[256];
	uint8_t spi_rx[256];

	memset(spi_tx, 0, sizeof(spi_tx));
	memcpy(spi_tx, txbuf, len);
	
	do{
		memset(spi_rx, 0, sizeof(spi_rx));
		ret = spi_write_data(ctx, spi_tx + index, 2);
		if(!ret)
		{
			return false;
		}		
		
		index = index + 2;
	}while(index < len);
	
	return true;
}


bool spi_send_data(struct spi_ctx *ctx, uint8_t *txbuf, int len)
{
	bool ret;
	int index = 0;
	uint8_t spi_tx[256];
	uint8_t spi_rx[256];

	memset(spi_tx, 0, sizeof(spi_tx));
	memcpy(spi_tx, txbuf, len);
	
	do{
		ret = spi_write_data(ctx, spi_tx + index, 2);
		if(!ret)
		{
			return false;
		}		
		
		index = index + 2;
	}while(index < len);
	
	return true;
}


bool spi_send_command(struct A1_chain *pChain, uint8_t cmd, uint8_t chip_id, uint8_t *buff, int len)
{
	int tx_len;
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	struct spi_ctx *ctx = pChain->spi_ctx;
	
	assert(buff != NULL);

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));
	
	spi_tx[0] = cmd;
	spi_tx[1] = chip_id;
	
	if(len > 0)
	{
		memcpy(spi_tx + 2, buff, len);
	}
	
	tx_len = (2 + len + 1) & ~1;
	//hexdump("send: TX", spi_tx, tx_len);

	if(spi_send_data(ctx, spi_tx, tx_len))
	{
		return true;
	}
	else
	{
		applog(LOG_WARNING, "send command fail !");
		return false;
	}

}


bool spi_poll_result(struct A1_chain *pChain, uint8_t cmd, uint8_t chip_id, uint8_t *buff, int len)
{
	int ret1, ret2;
	int tx_len;
	int tmp_len;
	int index,ret;
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	struct spi_ctx *ctx = pChain->spi_ctx;
	
	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));
	
	tx_len = ASIC_CHIP_NUM*4;

	for(tmp_len = 0; tmp_len < tx_len; tmp_len += 2)
	{
		if(!spi_read_data(ctx, spi_rx, 2))
		{
			applog(LOG_WARNING, "poll result: transfer fail !");
			return false;
		}
		//hexdump("poll: RX", spi_rx, 2);
		if((spi_rx[0] & 0x0f) == cmd)
		{
			index = 0;	
			do{
				ret = spi_read_data(ctx, spi_rx + 2 + index, 2);
				if(!ret)
				{
					return false;
				}					
				index = index + 2;
			}while(index < len);

			memcpy(buff, spi_rx, len);
			return true;
		}
	}
	
	return false;
}


bool inno_cmd_reset(struct A1_chain *pChain, uint8_t chip_id)
{
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	
	printf("send command [reset] \n");

	memset(spi_tx, 0, sizeof(spi_tx));
	if(!spi_send_command(pChain, CMD_RESET, chip_id, spi_tx, 2))
	{
		applog(LOG_WARNING, "cmd reset: send fail !");
		return false;
	}

	memset(spi_rx, 0, sizeof(spi_rx));
	if(!spi_poll_result(pChain, CMD_RESET, chip_id, spi_rx, 4))
	{
		applog(LOG_WARNING, "cmd reset: poll fail !");
		return false;
	}

	return true;
}

bool inno_cmd_resetjob(struct A1_chain *pChain, uint8_t chip_id)
{
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	uint8_t i,tx_len;
	uint8_t buffer[64];	

	memset(spi_tx, 0, sizeof(spi_tx));
	memset(spi_rx, 0, sizeof(spi_rx));

	spi_tx[0] = CMD_RESET;
	spi_tx[1] = chip_id;
	spi_tx[2] = 0xed;
	spi_tx[3] = 0xed;

	if(!spi_write_data(pChain->spi_ctx, spi_tx, 6))
	{
		applog(LOG_WARNING, "send command fail !");
		return false;
	}

/*
	memset(spi_rx, 0, sizeof(spi_rx));
	if(!spi_poll_result(pChain, CMD_RESET, chip_id, spi_rx, 4))
	{
		applog(LOG_WARNING, "cmd reset: poll fail !");
		return false;
	}
*/

	memset(spi_rx, 0, sizeof(spi_rx));
	if(!spi_poll_result(pChain, CMD_RESET, chip_id, spi_rx, 4))
	{
		applog(LOG_WARNING, "cmd reset: poll fail !");
		return false;
	}

	return true;
}



bool inno_cmd_bist_start(struct A1_chain *pChain, uint8_t chip_id, uint8_t *num)
{
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	
	printf("send command [bist_start] \n");

	memset(spi_tx, 0, sizeof(spi_tx));
	if(!spi_send_command(pChain, CMD_BIST_START, chip_id, spi_tx, 2))
	{
		applog(LOG_WARNING, "cmd bist start: send fail !");
		return false;
	}

	memset(spi_rx, 0, sizeof(spi_rx));
	if(!spi_poll_result(pChain, CMD_BIST_START, chip_id, num, 4))
	{
		applog(LOG_WARNING, "cmd bist start: poll fail !");
		return false;
	}

	return true;
}

bool inno_cmd_bist_collect(struct A1_chain *pChain, uint8_t chip_id)
{
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	
	printf("send command [bist_collect] \n");

	memset(spi_tx, 0, sizeof(spi_tx));
	if(!spi_send_command(pChain, CMD_BIST_COLLECT, chip_id, spi_tx, 2))
	{
		return false;
	}

	memset(spi_rx, 0, sizeof(spi_rx));
	if(!spi_poll_result(pChain, CMD_BIST_COLLECT, chip_id, spi_rx, 4))
	{
		return false;
	}

	return true;
}


bool inno_cmd_bist_fix(struct A1_chain *pChain, uint8_t chip_id)
{
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	
	printf("send command [bist_fix] \n");

	memset(spi_tx, 0, sizeof(spi_tx));
	if(!spi_send_command(pChain, CMD_BIST_FIX, chip_id, spi_tx, 2))
	{
		return false;
	}

	memset(spi_rx, 0, sizeof(spi_rx));
	if(!spi_poll_result(pChain, CMD_BIST_FIX, chip_id, spi_rx, 4))
	{
		return false;
	}

	return true;
}

//add  0929
bool inno_cmd_write_sec_reg(struct A1_chain *pChain, uint8_t chip_id, uint8_t *reg)
{
	int tx_len;
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	uint8_t tmp_buf[MAX_CMD_LENGTH];
	uint16_t clc_crc;
	uint8_t j;
		
	applog(LOG_INFO,"send command [write_reg] \n");
	assert(reg != NULL);
	
	memset(spi_tx, 0, sizeof(spi_tx));
		
	spi_tx[0] = CMD_READ_SEC_REG;
	spi_tx[1] = chip_id;
	memcpy(spi_tx+2, reg, REG_LENGTH-2);
	memset(tmp_buf, 0, sizeof(tmp_buf));

	clc_crc = CRC16_2(tmp_buf, REG_LENGTH);
	
	spi_tx[REG_LENGTH+0] = (uint8_t)(clc_crc >> 8);
	spi_tx[REG_LENGTH+1] = (uint8_t)(clc_crc);
	
	//hexdump("write reg", spi_tx, REG_LENGTH+2);
	if(!spi_write_data(pChain->spi_ctx, spi_tx, 16))
	{
		applog(LOG_WARNING, "send command fail !");
		return false;
	}
	//printf("reg:0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",spi_tx[0],spi_tx[1],spi_tx[2],spi_tx[3],spi_tx[4],spi_tx[5],spi_tx[6],spi_tx[7],spi_tx[8],spi_tx[9],spi_tx[10],spi_tx[11],spi_tx[12],spi_tx[13],spi_tx[14],spi_tx[15],spi_tx[16],spi_tx[17]);
	memset(spi_rx, 0, sizeof(spi_rx));
	if(!spi_poll_result(pChain, CMD_READ_SEC_REG, chip_id, spi_rx, REG_LENGTH+4))
	{
		applog(LOG_WARNING, "cmd write reg: poll fail !");
		return false;
	}
	
	return true;
}

bool inno_cmd_write_reg(struct A1_chain *pChain, uint8_t chip_id, uint8_t *reg)
{
	int tx_len;
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	uint8_t tmp_buf[MAX_CMD_LENGTH];
	uint16_t clc_crc;
	uint8_t j;
	
	//printf("send command [write_reg] \n");
	assert(reg != NULL);

	memset(spi_tx, 0, sizeof(spi_tx));
	
 	spi_tx[0] = CMD_WRITE_REG;
 	spi_tx[1] = chip_id;
 	memcpy(spi_tx+2, reg, REG_LENGTH-2);

	memset(tmp_buf, 0, sizeof(tmp_buf));
	for(j = 0; j < REG_LENGTH; j = j + 2)
	{
		tmp_buf[j + 0] = spi_tx[j + 1];
		tmp_buf[j + 1] = spi_tx[j + 0]; 	
	}
	clc_crc = CRC16_2(tmp_buf, REG_LENGTH);

	spi_tx[REG_LENGTH+0] = (uint8_t)(clc_crc >> 8);
	spi_tx[REG_LENGTH+1] = (uint8_t)(clc_crc);

	//hexdump("write reg", spi_tx, REG_LENGTH+2);
	if(!spi_write_data(pChain->spi_ctx, spi_tx, 16))
	{
		applog(LOG_WARNING, "send command fail !");
		return false;
	}

	memset(spi_rx, 0, sizeof(spi_rx));
	if(!spi_poll_result(pChain, CMD_WRITE_REG, chip_id, spi_rx, REG_LENGTH))
	{
		applog(LOG_WARNING, "cmd write reg: poll fail !");
		return false;
	}

	return true;
}


bool inno_cmd_read_reg(struct A1_chain *pChain, uint8_t chip_id, uint8_t *reg)
{
	int i;
	int tx_len;
	int ret,index; 
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	struct spi_ctx *ctx = pChain->spi_ctx;
	
	//printf("send command [read_reg] \r\n");
	//assert(reg != NULL);

	memset(spi_tx, 0, sizeof(spi_tx));
	spi_tx[0] = CMD_READ_REG;
	spi_tx[1] = chip_id;
	
	if(!spi_write_data(ctx, spi_tx, 2))
	{
		return false;
	}

	tx_len = ASIC_CHIP_NUM*4;
	memset(spi_rx, 0, sizeof(spi_rx));
	for(i = 0; i < tx_len; i = i + 2)
	{
		if(!spi_read_data(ctx, spi_rx, 2))
		{
			applog(LOG_WARNING, "poll result: transfer fail !");
			return false;
		}
		//hexdump("poll: RX", spi_rx, 2);
		if(spi_rx[0] == CMD_READ_REG_RESP)
		{		
			index = 0;	
			do{
				ret = spi_read_data(ctx, spi_rx + 2 + index, 2);
				if(!ret)
				{
					return false;
				}					
				index = index + 2;
			}while(index < REG_LENGTH);

			//hexdump("poll: RX", spi_rx + 2, REG_LENGTH);
			
			memcpy(reg, spi_rx + 2, REG_LENGTH);

			return true;
		}
	}
	
	return false;
}

bool inno_cmd_read_result(struct A1_chain *pChain, uint8_t chip_id, uint8_t *res)
{
	int i,j;
	int tx_len,index,ret;		
	uint16_t clc_crc; 
	uint16_t res_crc;
	uint8_t tmp_buf[64];
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	struct spi_ctx *ctx = pChain->spi_ctx;

	//printf("send command [read_result] \r\n");
	assert(res != NULL);
	
	memset(spi_tx, 0, sizeof(spi_tx));
	spi_tx[0] = CMD_READ_RESULT;
	spi_tx[1] = chip_id;
	
	if(!spi_write_data(ctx, spi_tx, 2))
	{
		return false;
	}

	tx_len = 4 * ASIC_CHIP_NUM;
	memset(spi_rx, 0, sizeof(spi_rx));
	for(i = 0; i < tx_len; i += 2)
	{
		if(!spi_read_data(ctx, spi_rx, 2))		
		{
			return false;
		}

		if(((spi_rx[0] & 0x0f) == CMD_READ_RESULT) && (spi_rx[1] != 0))
		{
			//applog(LOG_INFO, "GET GOOD RESULT");
			index = 0;	
			do{
				ret = spi_read_data(ctx, spi_rx + 2 + index, 2);
				if(!ret)
				{
					return false;
				}					
				index = index + 2;
			}while(index < ASIC_RESULT_LEN);

			memset(tmp_buf, 0, sizeof(tmp_buf));
			for(j = 0; j < READ_RESULT_LEN; j = j + 2)
			{
				tmp_buf[j + 0] = spi_rx[j + 1];
				tmp_buf[j + 1] = spi_rx[j + 0];
			}
			clc_crc = CRC16_2(tmp_buf, ASIC_RESULT_LEN);
			res_crc = (spi_rx[ASIC_RESULT_LEN] << 8) + (spi_rx[ASIC_RESULT_LEN+1] << 0);

			//hexdump("result: RX", spi_rx, READ_RESULT_LEN);
			if(clc_crc == res_crc)
			{
				memcpy(res, spi_rx, READ_RESULT_LEN);
				return true;
			}
			else
			{
				applog(LOG_INFO, "crc error clc=0x%4x, res=0x%4x \r\n", clc_crc, res_crc);
				return false;
			}				
		}
	}

	return false;

}

uint8_t inno_cmd_isBusy(struct A1_chain *pChain, uint8_t chip_id)
{
	uint8_t buffer[REG_LENGTH];

	  
	if(!inno_cmd_read_reg(pChain, chip_id, buffer))
	{
		applog(LOG_WARNING, "read chip %d busy status error", chip_id);
		return -1;
	}
	//printf("[check busy] \r\n");
	//hexdump("reg:", buffer, REG_LENGTH);

	if((buffer[9] & 0x01) == 1)
	{
		return WORK_BUSY;
	}
	else
	{
		return WORK_FREE;
	}

}



bool inno_cmd_write_job(struct A1_chain *pChain, uint8_t chip_id, uint8_t *job)
{
	int tx_len,i;
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	struct spi_ctx *ctx = pChain->spi_ctx;
	
	memset(spi_tx, 0, sizeof(spi_tx));
	memcpy(spi_tx, job, JOB_LENGTH);

	if(!spi_write_data(ctx, spi_tx, JOB_LENGTH + 10))
	{
		return false;
	}

/*
	if(chip_id == 1)
	{
		tx_len = ASIC_CHIP_NUM*4;
		memset(spi_rx, 0, sizeof(spi_rx));
		for(i = 0; i < tx_len; i = i + 2)
		{
			if(!spi_read_data(ctx, spi_rx, 2))
			{
				applog(LOG_WARNING, "poll result: transfer fail !");
				return false;
			}
			hexdump("poll: RX", spi_rx, 2);
			if(spi_rx[0] == 0x00 && spi_rx[1] == 0 && spi_rx[2] == 0x00 && spi_rx[3] == 0x00)
			{		
				return true;
			}
		}
	}
*/

	//printf("[write job] \r\n");
	//hexdump("job:", spi_tx, JOB_LENGTH);

	//usleep(100000);

	//if(inno_cmd_isBusy(pChain, chip_id) != WORK_BUSY)
	//{
	//	return false;
	//}

	return true;

}

uint32_t inno_cmd_test_chip(struct A1_chain *pChain)
{
	int i, j, k;
	struct work work1;
	struct work work2;
	uint32_t nonce;
	uint8_t chip_id;
	uint8_t job_id;
	uint8_t job66,job67;
	uint16_t micro_job_id;
	uint8_t c;
	int bad_chip_num = 0;
	uint32_t uiScore = 0;
	uint32_t rightNonce = 0x556d9801;
	uint32_t rightNonce2 = 0x2e993b02;
	uint32_t chip_valid[ASIC_CHIP_NUM] = {0};
	uint8_t tmp_buf[JOB_LENGTH];
	uint16_t crc;
	uint16_t nonce_16;

	//nonce 0x556d9801,
	uint8_t job1[JOB_LENGTH] = {
		0x17, 0x00,
		0xAB, 0xA9, 0x15, 0x9F, 0xCC, 0x8F, 0xDF, 0xC2,
		0xA2, 0x96, 0x64, 0x29, 0x5E, 0xCB, 0x3E, 0x37,
		0x81, 0x48, 0x41, 0xE7, 0x03, 0x1F, 0x07, 0xA6, 
		0x8D, 0xB7, 0x10, 0x9A, 0x13, 0x72, 0xC4, 0x21, 
		0x8F, 0x5D, 0x6A, 0x96, 0x4E, 0xD9, 0x11, 0x6B, 
		0xFF, 0x8B, 0x2E, 0x5D, 0xFC, 0x8C, 0x16, 0x34, 
		0xC7, 0x13, 0xEA, 0x18, 0xA2, 0xD6, 0x49, 0xFE, 
		0x8D, 0x31, 0xC4, 0xD2, 0x00, 0x00, 0x00, 0x20, 
		0x54, 0x6d, 0x98, 0x01,
		0xD7, 0x89, 0x10, 0x1A, 0x01, 0xD6, 0xED, 0x59,
		0xCD, 0xE7, 0xBA, 0x7B, 
		0xFF, 0x03, 0x00, 0x1E,
		0x57, 0x6d, 0x98, 0x01, 
		0x00, 0x00,
	};

	uint8_t job2[JOB_LENGTH] = {
		0x17, 0x00, 
		0xC2, 0x45, 0xB9, 0xCB, 0x99, 0xC6, 0x58, 0x28, 
		0xF9, 0x46, 0x96, 0xE7, 0x12, 0x3E, 0x00, 0xD1,
		0x8B, 0x2A, 0x2F, 0x29, 0xDC, 0x24, 0x6F, 0xC1, 
		0x1E, 0x5D, 0xA9, 0x2F, 0x13, 0x72, 0xC4, 0x21, 
		0x8F, 0x5D, 0x6A, 0x96, 0x4E, 0xD9, 0x11, 0x6B, 
		0xFF, 0x8B, 0x2E, 0x5D, 0xFC, 0x8C, 0x16, 0x34, 
		0xC7, 0x13, 0xEA, 0x18, 0xA2, 0xD6, 0x49, 0xFE, 
 		0x8D, 0x31, 0xC4, 0xD2, 0x00, 0x00, 0x00, 0x20,
 		0x2e, 0x99, 0x3b, 0x02,
		0xD7, 0x89, 0x10, 0x1A, 0x01, 0xD6, 0xED, 0x59, 
		0xA7, 0x3D, 0xE4, 0xA4,
		0xFF, 0x03, 0x00, 0x1E, 
		0x2e, 0x99, 0x3b, 0x02,
		0x00, 0x00,
	};
	
	applog(LOG_INFO, "ChipNum:%d. \n", pChain->num_active_chips);
	
	for (k = 0; k < 9; k++){
		job66 = 0x54;
		job67 = 0x6d;
		job1[66] = (uint8_t)(((job67 << 8) + job66) - 0x10*k);
		job1[67] = (uint8_t)((((job67 << 8) + job66) - 0x10*k)>>8);

		hexdump("job", job1, 92);
		
		for (i = pChain->num_active_chips; i > 0; i--) 
		{
			c = i;
			job1[1] = i;
			memset(tmp_buf, 0, sizeof(tmp_buf));
    		for(j = 0; j < 45; j++)
    		{
        		tmp_buf[(2 * j) + 1] = job1[(2 * j) + 0];
        		tmp_buf[(2 * j) + 0] = job1[(2 * j) + 1];
    		}
    		crc = CRC16_2(tmp_buf, 90);
    		job1[90] = (uint8_t)((crc >> 8) & 0xff);
    		job1[91] = (uint8_t)((crc >> 0) & 0xff);
			
			if (!inno_cmd_write_job(pChain, c, job1)) 
			{
				applog(LOG_ERR, "failed to write job for chip %d. \n", c);
			} 			
		}

		sleep(1);
		while (true) 
		{
			nonce = 0;
			chip_id = 0;
			job_id = 0;
			micro_job_id = 0;
			usleep(10000);
			if (!get_nonce(pChain, (uint8_t*)&nonce, &chip_id, &job_id))
				break;
			//nonce = bswap_32(nonce);
			if(nonce == rightNonce){
				++chip_valid[chip_id - 1];
				//applog(LOG_ERR, "chip:%d is good, nonce:0x%x. \n", c, nonce);
			}else{
				applog(LOG_ERR, "bad nonce error, chip_id:%d, nonce:0x%x. \n", chip_id, nonce);
			}
		}

		/*
		for (i = pChain->num_active_chips; i > 0; i--) 
		{
			c = i;
			job2[1] = c;
			memset(tmp_buf, 0, sizeof(tmp_buf));
    		for(j = 0; j < 45; j++)
    		{
        		tmp_buf[(2 * j) + 1] = job2[(2 * j) + 0];
        		tmp_buf[(2 * j) + 0] = job2[(2 * j) + 1];
    		}
    		crc = CRC16_2(tmp_buf, 90);
    		job2[90] = (uint8_t)((crc >> 8) & 0xff);
    		job2[91] = (uint8_t)((crc >> 0) & 0xff);
		
			if (!inno_cmd_write_job(pChain, c, job2)) 
			{
				applog(LOG_ERR, "failed to write job for chip %d. \n", c);
			} 			
		}

		sleep(1);
		while (true) 
		{
			nonce = 0;
			chip_id = 0;
			job_id = 0;
			micro_job_id = 0;
			usleep(10000);
			if (!get_nonce(pChain, (uint8_t*)&nonce, &chip_id, &job_id))
				break;
			//nonce = bswap_32(nonce);
			if(nonce == rightNonce2){
				++chip_valid[chip_id - 1];
				//applog(LOG_ERR, "chip:%d is good, nonce:0x%x. \n", c, nonce);
			}else{
				applog(LOG_ERR, "bad nonce error, chip_id:%d, nonce:0x%x. \n", chip_id, nonce);
			}
		}
		*/
			
	}
	
	for (i = 1; i <= pChain->num_active_chips; i++){
		uiScore += chip_valid[i-1];
		/* printf("chip_valid[%d]=%d . \n", i-1, chip_valid[i-1]);
		if(chip_valid[i-1] >= 4){
			applog(LOG_ERR, "inno_cmd_test_chip chip %d is good. \n", i);
		}else{
			bad_chip_num++;
		}
		*/
		if(chip_valid[i-1] < 4){
			bad_chip_num++;
		}
	} 
		
	applog(LOG_ERR, "inno_cmd_test_chip bad chip num is %d. \n", bad_chip_num);
	return uiScore;
}

