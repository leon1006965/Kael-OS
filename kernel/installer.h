#ifndef INSTALLER_H
#define INSTALLER_H

#include "types.h"

/* Install a Kael OS to a target disk */
int installer_run(uint32_t target_disk_lba);

#endif
