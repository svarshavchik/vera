#include "config.h"
#include "configdirs.h"
#include "verac.h"
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

const char reexec_envar[]="VERA_REEXEC_FD";

static int hooked(const char *hookfile, const char *init_path)
{
	char buf[256];
	struct stat pid1_stat, hookfile_stat;
	FILE *fp;

	if (stat(init_path, &pid1_stat) && errno == ENOENT)
		return 1;

	fp=fopen(hookfile, "r");

	if (!fp)
		return 0;

	if (!fgets(buf, sizeof(buf), fp))
		buf[0]=0;
	fclose(fp);
	strtok(buf, "\n");

	if (stat("/proc/1", &pid1_stat) == 0 &&
	    stat(hookfile, &hookfile_stat) == 0 &&
	    pid1_stat.st_mtime == hookfile_stat.st_mtime)
		return 0;

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
	int flag;

	flag=getenv(reexec_envar) ? 1:hooked(hookfile, init_path);

	if (!flag)
	{
		printf("vera: not hooked, running init\r\n");
		fflush(stdout);
		return init_path;
	}

	// vera is getting re-execed
	if (!getenv(reexec_envar))
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
