/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __HAKIOS_H
#define __HAKIOS_H

/* hakios sum PART [BLOCK COUNT]: SHA-256 of a partition's blocks; returns 0 or a negative errno. */
int hakios_sum(int argc, char *const argv[]);

#endif
