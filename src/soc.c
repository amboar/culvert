// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 IBM Corp.

#include "devicetree/g4.h"
#include "devicetree/g5.h"
#include "devicetree/g6.h"

#include "ast.h"
#include "log.h"
#include "soc.h"
#include "rev.h"

#include <libfdt.h>

#include <assert.h>
#include <errno.h>
#include <inttypes.h>

static const struct soc_fdt soc_fdts[] = {
	[ast_g4] = {
		.start = &_binary_src_devicetree_g4_dtb_start,
		.end = &_binary_src_devicetree_g4_dtb_end,
	},
	[ast_g5] = {
		.start = &_binary_src_devicetree_g5_dtb_start,
		.end = &_binary_src_devicetree_g5_dtb_end,
	},
	[ast_g6] = {
		.start = &_binary_src_devicetree_g6_dtb_start,
		.end = &_binary_src_devicetree_g6_dtb_end,
	},
	{ }
};

/* XXX: Use a linker script? I've run out of meson though */
static int soc_align_fdt(struct soc *ctx, const struct soc_fdt *fdt)
{
	size_t len = fdt->end - fdt->start;

	ctx->fdt.start = malloc(len);
	if (!ctx->fdt.start)
		return -ENOMEM;

	ctx->fdt.end = ctx->fdt.start + len;

	memcpy(ctx->fdt.start, fdt->start, len);

	logd("Selected devicetree for SoC '%s'\n",
	     fdt_getprop(ctx->fdt.start, 0, "compatible", NULL));

	return 0;
}

int soc_from_rev(struct soc *ctx, struct ahb *ahb, uint32_t rev)
{
	/* TODO: Map rev to the SoC compatible and find compatible devicetree */
	if (!(rev_is_generation(rev, ast_g4) ||
	      rev_is_generation(rev, ast_g5) ||
	      rev_is_generation(rev, ast_g6))) {
		loge("Found unsupported SoC generation: 0x%08" PRIx32 "\n", rev);
		return -ENOTSUP;
	}

	ctx->rev = rev;
	ctx->ahb = ahb;
	return soc_align_fdt(ctx, &soc_fdts[rev_generation(rev)]);
}

int soc_probe(struct soc *ctx, struct ahb *ahb)
{
	int64_t rc;

	rc = rev_probe(ahb);
	if (rc < 0) {
		loge("Failed to probe SoC revision: %d\n", rc);
		return rc;
	}

	return soc_from_rev(ctx, ahb, (uint32_t)rc);
}

void soc_destroy(struct soc *ctx)
{
	free(ctx->fdt.start);
}

int soc_device_match_node(struct soc *ctx,
			  const struct soc_device_id table[],
			  struct soc_device_node *dn)
{
	const char *pval;
	int rc;

	/* FIXME: Only matches the first device */
	while (table->compatible) {
		logd("Searching devicetree for compatible '%s'\n",
		     table->compatible);

		pval = fdt_getprop(ctx->fdt.start, 0, "compatible", NULL);
		if (pval && !strcmp(table->compatible, pval)) {
			dn->offset = 0;
			return 0;
		}

		rc = fdt_node_offset_by_compatible(ctx->fdt.start, 0,
						   table->compatible);

		/* Found it */
		if (rc >= 0) {
			dn->offset = rc;
			return 0;
		}

		/* Corrupt FDT */
		if (rc != -FDT_ERR_NOTFOUND) {
			loge("fdt: Failed to look up compatible: %d\n", rc);
			return -EUCLEAN;
		}

		/* Keep looking */
		table++;
	}

	/* Failed to find it */
	return -ENOENT;
}

int soc_device_is_compatible(struct soc *ctx,
			     const struct soc_device_id table[],
			     const struct soc_device_node *dn)
{
	while (table->compatible) {
		int rc;

		rc = fdt_node_check_compatible(ctx->fdt.start, dn->offset,
					       table->compatible);

		/* Bad node */
		if (rc < 0) {
			loge("fdt: Failed to look up compatible: %d\n", rc);
			return -EUCLEAN;
		}

		/* Found it */
		if (rc == 0)
			return 1;

		/* Have a compatible property but no match, keep looking */
		assert(rc == 1);
		table++;
	}

	/* Failed to find it */
	return 0;
}

const void *soc_device_get_match_data(struct soc *ctx,
				      const struct soc_device_id table[],
				      const struct soc_device_node *dn)
{
	while (table->compatible) {
		int rc;

		rc = fdt_node_check_compatible(ctx->fdt.start, dn->offset,
					       table->compatible);

		/* Bad node */
		if (rc < 0) {
			loge("fdt: Failed to look up compatible: %d\n", rc);
			return NULL;
		}

		/* Found it */
		if (rc == 0)
			return table->data;

		/* Have a compatible property but no match, keep looking */
		assert(rc == 1);
		table++;
	}

	/* Failed to find it */
	return NULL;
}

int soc_device_from_name(struct soc *ctx, const char *name,
			 struct soc_device_node *dn)
{
	const char *path;
	int rc;

	logd("fdt: Looking up device name '%s'\n", name);

	path = fdt_get_alias(ctx->fdt.start, name);
	if (!path)
		path = name;

	logd("fdt: Locating node with device path '%s'\n", path);

	rc = fdt_path_offset(ctx->fdt.start, path);
	if (rc < 0) {
		if (rc == -FDT_ERR_BADPATH)
			return -EINVAL;

		if (rc == -FDT_ERR_NOTFOUND)
			return -ENOENT;

		return -EUCLEAN;
	}

	dn->offset = rc;

	return 0;
}

int soc_device_from_type(struct soc *ctx, const char *type,
			 struct soc_device_node *dn)
{
	int node;

	logd("fdt: Searching devicetree for type '%s'\n", type);

	fdt_for_each_subnode(node, ctx->fdt.start, 0) {
		const char *found;

		found = fdt_getprop(ctx->fdt.start, node, "device_type", NULL);
		if (found && !strcmp(type, found)) {
			dn->offset = node;
			return 0;
		}
	}

	if ((node < 0) && (node != -FDT_ERR_NOTFOUND))
		return -EUCLEAN;

	return -ENOENT;
}

int soc_device_get_memory_index(struct soc *ctx,
				const struct soc_device_node *dn, int index,
				struct soc_region *region)
{
	const uint32_t *reg;
	int len;

	/* FIXME: Do ranges translation */
	reg = fdt_getprop(ctx->fdt.start, dn->offset, "reg", &len);
	if (!reg) {
		char path[PATH_MAX];
		int rc;

		rc = fdt_get_path(ctx->fdt.start, dn->offset, path, sizeof(path));
		if (rc < 0) {
			loge("fdt: Failed to determine node path while reporting failure to find reg property for offset %d\n",
			     dn->offset);
			return -ENOENT;
		} else {
			loge("fdt: Failed to find reg property in %s (%d): %d\n",
			     path, dn->offset, len);
			return -ENOENT;
		}
	}

	/* FIXME: Assumes #address-cells = <1>, #size-cells = <1> */
	if (len < (8 * (index + 1)))
		return -EINVAL;

	/* <address, size> */
	region->start = be32toh(reg[2 * index + 0]);
	region->length = be32toh(reg[2 * index + 1]);

	return 0;
}
