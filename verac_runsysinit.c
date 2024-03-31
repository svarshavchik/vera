#include "config.h"
#include "verac.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

static void do_run_sysinit(const char *original_line,
			   const char *identifier,
			   const char *runlevels,
			   const char *action,
			   const char *command,
			   void *cbarg)
{
	pid_t p;
	int wstatus;

	if (strcmp(action, "sysinit"))
		return;

	printf("vera: running %s\n", command);

	while ((p=fork()) == -1)
	{
		perror("fork");
		sleep(5);
	}

	if (p == 0)
	{
		execl("/bin/sh", "/bin/sh", "-c", command, NULL);
		perror("/bin/sh");
		exit(1);
	}

	if (waitpid(p, &wstatus, 0) != p)
	{
		fprintf(stderr, "%s: waitpid() failed.\n", command);
		fflush(stderr);
	}
	else
	{
		if (WIFEXITED(wstatus))
		{
			if (WEXITSTATUS(wstatus) != 0)
			{
				fprintf(stderr, "%s: terminated with exit %d\n",
					command,
					WEXITSTATUS(wstatus));
				return;
			}
		}
		else
		{
			fprintf(stderr, "%s: aborted with signal %d\n",
				command,
				WIFSIGNALED(wstatus) ? WTERMSIG(wstatus) : -1);
		}
	}
}

void run_sysinit(const char *etc_inittab)
{
	int fd=open(etc_inittab, O_RDONLY|O_CLOEXEC);
	FILE *fp;

	if (fd < 0)
	{
		perror(etc_inittab);
		return;
	}
	fp=fdopen(fd, "r");

	if (fp)
	{
		parse_inittab(fp, do_run_sysinit, NULL);
		fclose(fp);
	}
}
