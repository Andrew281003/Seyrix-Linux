#ifndef SEYRIX_SETTINGS_SYSTEM_INFO_H
#define SEYRIX_SETTINGS_SYSTEM_INFO_H

#include <limits.h>

struct seyrix_system_info {
	char os_name[256];
	char os_version[128];
	char kernel[128];
	char architecture[64];
	char cpu[256];
	char memory[64];
	char graphics[256];
	char build[192];
};

void seyrix_system_info_collect(struct seyrix_system_info *info);

#endif
