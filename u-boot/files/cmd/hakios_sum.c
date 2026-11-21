// SPDX-License-Identifier: GPL-2.0+
/* Generic block-device checksum command used for board bring-up. */

#include <blk.h>
#include <command.h>
#include <dm.h>
#include <malloc.h>
#include <memalign.h>
#include <part.h>
#include <u-boot/sha256.h>
#include <linux/errno.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <time.h>
#include <vsprintf.h>
#include "hakios.h"

/*
 * hakios sum PART [BLOCK COUNT]: the sha256 of a partition's blocks (all of them by default), read from the disk that
 * has it: what was flashed, checked from the phone itself, with nothing of Android's needed.
 */
int hakios_sum(int argc, char *const argv[])
{
	const ulong chunk = SZ_4M;
	struct disk_partition info;
	struct udevice *dev;
	struct blk_desc *desc = NULL;
	u8 digest[SHA256_SUM_LEN];
	sha256_context ctx;
	lbaint_t first, count, done = 0;
	ulong t0 = get_timer(0), ms;
	void *buf;

	if (argc != 2 && argc != 4)
		return -EINVAL;
	uclass_foreach_dev_probe(UCLASS_BLK, dev) {
		struct blk_desc *d = dev_get_uclass_plat(dev);

		part_init(d);		/* (a disk no command has looked at yet has no partition type) */
		if (part_get_info_by_name(d, argv[1], &info) >= 0) {
			desc = d;
			break;
		}
	}
	if (!desc) {
		printf("hakios: no partition %s\n", argv[1]);
		return -ENODEV;
	}
	first = argc == 4 ? simple_strtoul(argv[2], NULL, 0) : 0;
	count = argc == 4 ? simple_strtoul(argv[3], NULL, 0) : info.size;
	if (first > info.size || count > info.size - first) {
		printf("hakios: blocks %lu..+%lu are not inside %s (%lu blocks)\n", (ulong)first, (ulong)count, argv[1],
		       (ulong)info.size);
		return -EINVAL;
	}
	buf = memalign(ARCH_DMA_MINALIGN, chunk);
	if (!buf) {
		printf("hakios: no memory for a buffer of %lu bytes\n", chunk);
		return -ENOMEM;
	}
	sha256_starts(&ctx);
	while (done < count) {
		lbaint_t n = min_t(lbaint_t, count - done, chunk / info.blksz);

		if (blk_dread(desc, info.start + first + done, n, buf) != n) {
			printf("hakios: %s: reading block %lu failed\n", argv[1], (ulong)(first + done));
			free(buf);
			return -EIO;
		}
		sha256_update(&ctx, buf, n * info.blksz);
		done += n;
	}
	free(buf);
	sha256_finish(&ctx, digest);
	ms = max(get_timer(t0), 1UL);
	printf("[oosexgang] hakios: %s blocks %lu..%lu: sha256 ", argv[1], (ulong)first, (ulong)(first + count - 1));
	for (int i = 0; i < SHA256_SUM_LEN; i++)
		printf("%02x", digest[i]);
	printf(" (%lu MiB in %lu ms)\n", (ulong)((u64)count * info.blksz >> 20), ms);
	return 0;
}
