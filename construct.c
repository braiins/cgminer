#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include "construct.h"

int construct_printf(struct construct_buf *cbuf, const char *fmt, ...)
{
	va_list ap;
	int ret;
	int rem;

	if (cbuf->overflow)
		return 0;

	rem = cbuf->end - cbuf->ptr;
	if (rem <= 0)
		goto overflow;

	va_start(ap, fmt);
	ret = vsnprintf(cbuf->ptr, rem, fmt, ap);
	va_end(ap);

	if (ret >= rem)
		goto overflow;

	cbuf->ptr += ret;
	return 1;

overflow:
	cbuf->overflow = 1;
	return 0;
}

int construct_json_quote(struct construct_buf *cbuf, char *buf, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		char c = buf[i];
		if (c < 0x20)
			construct_printf(cbuf, "\\u%04x", c);
		else if (c == '\\' || c == '\"')
			construct_printf(cbuf, "\\%c", c);
		else
			construct_putc(cbuf, c);
	}
	return !cbuf->overflow;
}

int construct_print_hex(struct construct_buf *cbuf, void *mem, int len)
{
	uint8_t *buf = mem;
	int i;

	for (i = 0; i < len; i++)
		construct_printf(cbuf, "%02x", buf[i]);

	return !cbuf->overflow;
}


