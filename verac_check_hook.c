#include "config.h"
#include "configdirs.h"
#include "verac.h"
#include <sys/un.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

static int hooked(const char *hookfile)
{
	char buf[256];

	FILE *fp=fopen(hookfile, "r");

	if (!fp)
		return 0;

	if (!fgets(buf, sizeof(buf), fp))
		buf[0]=0;
	fclose(fp);
	strtok(buf, "\n");

	if (strcmp(buf, HOOKED_ON) == 0)
		return 1;

	if (strcmp(buf, HOOKED_ONCE) == 0)
		return -1;

	return 0;
}

const char *check_hookfile(const char *hookfile,
			   void (*run_sysinit_cb)(const char *inittab),
			   const char *init_path,
			   const char *vera_path)
{
	int flag=hooked(hookfile);

	if (!flag)
	{
		printf("vera: not hooked, running init\r\n");
		fflush(stdout);
		return init_path;
	}

	(*run_sysinit_cb)("/etc/inittab");

	if (flag < 0)
	{
		if (unlink(hookfile) < 0)
		{
			perror(hookfile);
		}
		else
		{
			printf("vera: one-time only hook removed\r\n");
		}
	}
	return vera_path;
}
