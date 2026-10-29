/* SPDX-License-Identifier: GPL-2.0+ */
/* the hakios command's parts (hakios.c, hakios_slot.c) */
#ifndef __HAKIOS_H
#define __HAKIOS_H

/* hakios slot [a | b | successful]: Qualcomm's A/B slots; 0 or a negative error */
int hakios_slot(int argc, char *const argv[]);

#endif
