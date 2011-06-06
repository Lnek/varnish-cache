/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/uio.h>

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "vsb.h"

#include "libvarnish.h"

#include "cli.h"
#include "cli_priv.h"
#include "cli_common.h"

/*lint -e{818} cli could be const */
void
VCLI_Out(struct cli *cli, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (cli != NULL)
		(void)VSB_vprintf(cli->sb, fmt, ap);
	else
		(void)vfprintf(stdout, fmt, ap);
	va_end(ap);
}

/*lint -e{818} cli could be const */
void
VCLI_Quote(struct cli *cli, const char *s)
{

	VSB_quote(cli->sb, s, -1, 0);
}

void
VCLI_SetResult(struct cli *cli, unsigned res)
{

	if (cli != NULL)
		cli->result = res;	/*lint !e64 type mismatch */
	else
		printf("CLI result = %u\n", res);
}

int
VCLI_WriteResult(int fd, unsigned status, const char *result)
{
	int i, l;
	struct iovec iov[3];
	char nl[2] = "\n";
	char res[CLI_LINE0_LEN + 2];	/*
					 * NUL + one more so we can catch
					 * any misformats by snprintf
					 */

	assert(status >= 100);
	assert(status <= 999);		/*lint !e650 const out of range */

	i = snprintf(res, sizeof res,
	    "%-3d %-8jd\n", status, (intmax_t)strlen(result));
	assert(i == CLI_LINE0_LEN);

	iov[0].iov_base = res;
	iov[0].iov_len = CLI_LINE0_LEN;

	iov[1].iov_base = (void*)(uintptr_t)result;	/* TRUST ME */
	iov[1].iov_len = strlen(result);

	iov[2].iov_base = nl;
	iov[2].iov_len = 1;

	for (l = i = 0; i < 3; i++)
		l += iov[i].iov_len;
	i = writev(fd, iov, 3);
	return (i != l);
}

static int
read_tmo(int fd, char *ptr, unsigned len, double tmo)
{
	int i, j, to;
	struct pollfd pfd;

	if (tmo > 0) 
		to = tmo * 1e3;
	else
		to = -1;
	pfd.fd = fd;
	pfd.events = POLLIN;
	for (j = 0; len > 0; ) {
		i = poll(&pfd, 1, to);
		if (i == 0) {
			errno = ETIMEDOUT;
			return (-1);
		}
		i = read(fd, ptr, len);
		if (i < 0)
			return (i);
		if (i == 0)
			break;
		len -= i;
		ptr += i;
		j += i;
	}
	return (j);
}

int
VCLI_ReadResult(int fd, unsigned *status, char **ptr, double tmo)
{
	char res[CLI_LINE0_LEN];	/* For NUL */
	int i, j;
	unsigned u, v, s;
	char *p;

	if (status == NULL)
		status = &s;
	if (ptr != NULL)
		*ptr = NULL;
	i = read_tmo(fd, res, CLI_LINE0_LEN, tmo);
	if (i != CLI_LINE0_LEN) {
		*status = CLIS_COMMS;
		if (ptr != NULL)
			*ptr = strdup("CLI communication error (hdr)");
		if (i != 0)
			return (i);
		return (*status);
	}
	assert(i == CLI_LINE0_LEN);
	assert(res[3] == ' ');
	assert(res[CLI_LINE0_LEN - 1] == '\n');
	res[CLI_LINE0_LEN - 1] = '\0';
	j = sscanf(res, "%u %u\n", &u, &v);
	assert(j == 2);
	*status = u;
	p = malloc(v + 1L);
	assert(p != NULL);
	i = read_tmo(fd, p, v + 1, tmo);
	if (i < 0) {
		*status = CLIS_COMMS;
		free(p);
		if (ptr != NULL)
			*ptr = strdup("CLI communication error (body)");
		return (i);
	}
	assert(i == v + 1);
	assert(p[v] == '\n');
	p[v] = '\0';
	if (ptr == NULL)
		free(p);
	else
		*ptr = p;
	return (0);
}
