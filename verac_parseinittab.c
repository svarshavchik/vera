#include "config.h"
#include "verac.h"
#include <string.h>
#include <stdio.h>

void parse_inittab(FILE *fp,
		   void (*cb)(const char *original_line,
			      const char *identifier,
			      const char *runlevels,
			      const char *action,
			      const char *command,
			      void *),
		   void *cbarg)
{
	char buf[1024];
	char orig_buf[1024];

	while (fgets(buf, sizeof(buf), fp))
	{
		const char *identifier=NULL;
		const char *runlevels="";
		const char *action="";
		const char *command="";
		char *p, *q;

		if ((p=strchr(buf, '#')) != 0)
			*p=0;

		for (p=q=buf; *p; ++p)
		{
			if (strchr(" \t\r\n", *p) == 0)
			{
				q=p+1;
			}
		}
		*q=0;

		strcpy(orig_buf, buf);

		p=strchr(buf, ':');

		if (p)
		{
			identifier=buf;
			*p++=0;
			runlevels=p;

			if ((p=strchr(p, ':')) != 0)
			{
				*p++=0;
				action=p;

				if ((p=strchr(p, ':')) != 0)
				{
					*p++=0;
					command=p;
				}
			}
		}

		(*cb)(orig_buf, identifier, runlevels, action, command, cbarg);
	}
}
