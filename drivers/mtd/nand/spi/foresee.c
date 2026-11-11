// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2023, SberDevices. All Rights Reserved.
 *
 * Author: Martin Kurbanov <mmkurbanov@salutedevices.com>
 */

#ifndef __UBOOT__
#include <linux/device.h>
#include <linux/kernel.h>
#endif
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_FORESEE		0xCD

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_4S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_2S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_FAST_1S_1S_1S_OP(0, 1, NULL, 0, 0),
		SPINAND_PAGE_READ_FROM_CACHE_1S_1S_1S_OP(0, 1, NULL, 0, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_1S_1S_4S_OP(true, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_1S_OP(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_1S_1S_4S_OP(false, 0, NULL, 0),
		SPINAND_PROG_LOAD_1S_1S_1S_OP(false, 0, NULL, 0));

static int f35sqa002g_ooblayout_ecc(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *region)
{
	return -ERANGE;
}

static int f35sqa002g_ooblayout_free(struct mtd_info *mtd, int section,
				     struct mtd_oob_region *region)
{
	if (section)
		return -ERANGE;

	/* Reserve 2 bytes for the BBM. */
	region->offset = 2;
	region->length = 62;

	return 0;
}

static const struct mtd_ooblayout_ops f35sqa002g_ooblayout = {
	.ecc = f35sqa002g_ooblayout_ecc,
	.rfree = f35sqa002g_ooblayout_free,
};

static int f35sqa002g_ecc_get_status(struct spinand_device *spinand, u8 status)
{
	struct nand_device *nand = spinand_to_nand(spinand);

	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case STATUS_ECC_HAS_BITFLIPS:
		return nanddev_get_ecc_conf(nand)->strength;

	default:
		break;
	}

	/* More than 1-bit error was detected in one or more sectors and
	 * cannot be corrected.
	 */
	return -EBADMSG;
}

static const struct spinand_info foresee_spinand_table[] = {
	/*
	 * FiberHome HG5580A production batch.  Raw ID reported by the chip is
	 * "cd 52 52": manufacturer 0xCD is FORESEE, device 0x52 was missing
	 * from this table, which is why the board logged
	 *
	 *   spi_nand nand@0: unknown raw ID cd 52 52 cd
	 *
	 * and never registered the NAND device.
	 *
	 * Geometry matches the F35SQA002G above: 2 KiB page, 64-byte OOB,
	 * 2048 blocks of 128 KiB = 256 MiB.  The stock U-Boot on the same board
	 * reports 0xee00000 bytes (238 MiB) addressable, which is consistent
	 * with 2048 blocks once the vendor's reserved area is excluded.
	 *
	 * Reusing f35sqa002g_ooblayout / f35sqa002g_ecc_get_status is
	 * deliberate: both describe page/OOB layout and status-bit decoding for
	 * this FORESEE family, not a specific die.
	 */
	SPINAND_INFO("F35SQB002G",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x52, 0x52),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&f35sqa002g_ooblayout,
				     f35sqa002g_ecc_get_status)),
	SPINAND_INFO("F35SQA002G",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x72, 0x72),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&f35sqa002g_ooblayout,
				     f35sqa002g_ecc_get_status)),
	SPINAND_INFO("F35SQA001G",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x71, 0x71),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(1, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&f35sqa002g_ooblayout,
				     f35sqa002g_ecc_get_status)),
};

static const struct spinand_manufacturer_ops foresee_spinand_manuf_ops = {
};

const struct spinand_manufacturer foresee_spinand_manufacturer = {
	.id = SPINAND_MFR_FORESEE,
	.name = "FORESEE",
	.chips = foresee_spinand_table,
	.nchips = ARRAY_SIZE(foresee_spinand_table),
	.ops = &foresee_spinand_manuf_ops,
};
