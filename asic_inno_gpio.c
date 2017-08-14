#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "asic_inno.h"

#include "asic_inno_gpio.h"

/*
int SPI_PIN_POWER_EN[] = {
};
*/
	
int SPI_PIN_START_EN[ASIC_CHAIN_NUM] = {
883,
885,
//887,
//889,
//891,
};

int SPI_PIN_RESET[ASIC_CHAIN_NUM] = {
884,
886,
//888,
//890,
//892,
};


void asic_gpio_init(int gpio, int direction)
{
	int fd;
	char fvalue[64];
	char fpath[64];

	fd = open(SYSFS_GPIO_EXPORT, O_WRONLY);
	if(fd == -1)
	{
		return;
	}
	memset(fvalue, 0, sizeof(fvalue));
	sprintf(fvalue, "%d", gpio);
	write(fd, fvalue, strlen(fvalue));
	close(fd);

	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_DIR_STR, gpio);	
	fd = open(fpath, O_WRONLY);
	if(fd == -1)
	{
		return;
	}
	if(direction == 0)
	{
		write(fd, SYSFS_GPIO_DIR_OUT, sizeof(SYSFS_GPIO_DIR_OUT));
	}
	else
	{
		write(fd, SYSFS_GPIO_DIR_IN, sizeof(SYSFS_GPIO_DIR_IN));
	}	
	close(fd);
}

void asic_gpio_write(int gpio, int value)
{
	int fd;
	char fvalue[64];
	char fpath[64];

#if 0
	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_DIR_STR, gpio);	
	fd = open(fpath, O_WRONLY);
	if(fd == -1)
	{
		return;
	}
	write(fd, SYSFS_GPIO_DIR_OUT, sizeof(SYSFS_GPIO_DIR_OUT));
	close(fd);
#endif	
	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_VAL_STR, gpio);
	fd = open(fpath, O_WRONLY);
	if(fd == -1)
	{
		return;
	}

	if(value == 0)
	{
		write(fd, SYSFS_GPIO_VAL_LOW, sizeof(SYSFS_GPIO_VAL_LOW));
	}
	else
	{
		write(fd, SYSFS_GPIO_VAL_HIGH, sizeof(SYSFS_GPIO_VAL_HIGH));
	}	
	close(fd);	
}

int asic_gpio_read(int gpio)
{
	int fd;
	char fvalue[64];
	char fpath[64];

#if 0
	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_DIR_STR, gpio);
	fd = open(fpath, O_WRONLY);
	if(fd == -1)
	{
		//printf("%s\n", strerror(errno));
		return -1;
	}
	write(fd, SYSFS_GPIO_DIR_IN, sizeof(SYSFS_GPIO_DIR_IN));
	close(fd);
#endif	
	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_VAL_STR, gpio);
	fd = open(fpath, O_RDONLY);
	if(fd == -1)
	{
		return -1;
	}
	memset(fvalue, 0, sizeof(fvalue));
	read(fd, fvalue, 1);
	if(fvalue[0] == '0')
	{
		return 0;
	}
	else
	{
		return 1;
	}	
}  
