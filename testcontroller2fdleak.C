/*
** Copyright 2022 Double Precision, Inc.
** See COPYING for distribution information.
*/

#include "config.h"
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <iostream>

int main()
{
	DIR *dirp=opendir("/proc/self/fd");

	if (!dirp)
	{
		perror("/proc/self/fd");
		exit(1);
	}

	while (auto de=readdir(dirp))
	{
		if (de->d_name[0] == '.')
			continue;

		auto n=atoi(de->d_name);

		if (n == dirfd(dirp))
			continue;
		if (n > 2)
		{
			std::cout << "Leaked " << n << "\n";
			exit(1);
		}
	}
	closedir(dirp);
	std::cout << "foo\nbar\n";
	return 0;
}
