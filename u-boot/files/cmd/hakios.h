/* SPDX-License-Identifier: GPL-2.0+ */
/* the hakios command's parts (hakios.c, hakios_slot.c) */
#ifndef __HAKIOS_H
#define __HAKIOS_H

/* hakios slot [a | b | successful]: Qualcomm's A/B slots; 0 or a negative error */
int hakios_slot(int argc, char *const argv[]);

/* hakios sum PART [BLOCK COUNT]: the sha256 of a partition's blocks; 0 or a negative error */
int hakios_sum(int argc, char *const argv[]);

/* hakios bootloader: restart into ABL's fastboot; returns only on failure */
int hakios_bootloader(void);

#endif
