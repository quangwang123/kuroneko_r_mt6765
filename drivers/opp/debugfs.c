/*
 * Generic OPP debugfs interface
 *
 * Copyright (C) 2015-2016 Viresh Kumar <viresh.kumar@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/limits.h>
#include <linux/slab.h>

#include "opp.h"

static struct dentry *rootdir;

static void opp_set_dev_name(const struct device *dev, char *name)
{
	if (dev->parent)
		snprintf(name, NAME_MAX, "%s-%s", dev_name(dev->parent),
			 dev_name(dev));
	else
		snprintf(name, NAME_MAX, "%s", dev_name(dev));
}

void opp_debug_remove_one(struct dev_pm_opp *opp)
{
	debugfs_remove_recursive(opp->dentry);
}

static bool opp_debug_create_supplies(struct dev_pm_opp *opp,
				      struct opp_table *opp_table,
				      struct dentry *pdentry)
{
	struct dentry *d;
	int i;

	for (i = 0; i < opp_table->regulator_count; i++) {
		char name[15];

		snprintf(name, sizeof(name), "supply-%d", i);

		/* Create per-opp directory */
		d = debugfs_create_dir(name, pdentry);

		if (!d)
			return false;

		if (!debugfs_create_ulong("u_volt_target", S_IRUGO, d,
					  &opp->supplies[i].u_volt))
			return false;

		if (!debugfs_create_ulong("u_volt_min", S_IRUGO, d,
					  &opp->supplies[i].u_volt_min))
			return false;

		if (!debugfs_create_ulong("u_volt_max", S_IRUGO, d,
					  &opp->supplies[i].u_volt_max))
			return false;

		if (!debugfs_create_ulong("u_amp", S_IRUGO, d,
					  &opp->supplies[i].u_amp))
			return false;
	}

	return true;
}

int opp_debug_create_one(struct dev_pm_opp *opp, struct opp_table *opp_table)
{
	struct dentry *pdentry = opp_table->dentry;
	struct dentry *d;
	unsigned long id;
	char name[25];	/* 20 chars for 64 bit value + 5 (opp:\0) */

	/*
	 * Get directory name for OPP.
	 *
	 * - Normally rate is unique to each OPP, use it to get unique opp-name.
	 * - For some devices rate isn't available, use index instead.
	 */
	if (likely(opp->rate))
		id = opp->rate;
	else
		id = _get_opp_count(opp_table);

	snprintf(name, sizeof(name), "opp:%lu", id);

	/* Create per-opp directory */
	d = debugfs_create_dir(name, pdentry);
	if (!d)
		return -ENOMEM;

	if (!debugfs_create_bool("available", S_IRUGO, d, &opp->available))
		return -ENOMEM;

	if (!debugfs_create_bool("dynamic", S_IRUGO, d, &opp->dynamic))
		return -ENOMEM;

	if (!debugfs_create_bool("turbo", S_IRUGO, d, &opp->turbo))
		return -ENOMEM;

	if (!debugfs_create_bool("suspend", S_IRUGO, d, &opp->suspend))
		return -ENOMEM;

	if (!debugfs_create_u32("performance_state", S_IRUGO, d, &opp->pstate))
		return -ENOMEM;

	if (!debugfs_create_ulong("rate_hz", S_IRUGO, d, &opp->rate))
		return -ENOMEM;

	if (!opp_debug_create_supplies(opp, opp_table, d))
		return -ENOMEM;

	if (!debugfs_create_ulong("clock_latency_ns", S_IRUGO, d,
				  &opp->clock_latency_ns))
		return -ENOMEM;

	opp->dentry = d;
	return 0;
}

static int opp_list_debug_create_dir(struct opp_device *opp_dev,
				     struct opp_table *opp_table)
{
	const struct device *dev = opp_dev->dev;
	struct dentry *d;

	opp_set_dev_name(dev, opp_table->dentry_name);

	/* Create device specific directory */
	d = debugfs_create_dir(opp_table->dentry_name, rootdir);
	if (!d) {
		dev_err(dev, "%s: Failed to create debugfs dir\n", __func__);
		return -ENOMEM;
	}

	opp_dev->dentry = d;
	opp_table->dentry = d;

	return 0;
}

static int opp_list_debug_create_link(struct opp_device *opp_dev,
				      struct opp_table *opp_table)
{
	const struct device *dev = opp_dev->dev;
	char name[NAME_MAX];
	struct dentry *d;

	opp_set_dev_name(opp_dev->dev, name);

	/* Create device specific directory link */
	d = debugfs_create_symlink(name, rootdir, opp_table->dentry_name);
	if (!d) {
		dev_err(dev, "%s: Failed to create link\n", __func__);
		return -ENOMEM;
	}

	opp_dev->dentry = d;

	return 0;
}

/**
 * opp_debug_register - add a device opp node to the debugfs 'opp' directory
 * @opp_dev: opp-dev pointer for device
 * @opp_table: the device-opp being added
 *
 * Dynamically adds device specific directory in debugfs 'opp' directory. If the
 * device-opp is shared with other devices, then links will be created for all
 * devices except the first.
 *
 * Return: 0 on success, otherwise negative error.
 */
int opp_debug_register(struct opp_device *opp_dev, struct opp_table *opp_table)
{
	return 0;
}

static void opp_migrate_dentry(struct opp_device *opp_dev,
			       struct opp_table *opp_table)
{
	struct opp_device *new_dev;
	const struct device *dev;
	struct dentry *dentry;

	/* Look for next opp-dev */
	list_for_each_entry(new_dev, &opp_table->dev_list, node)
		if (new_dev != opp_dev)
			break;

	/* new_dev is guaranteed to be valid here */
	dev = new_dev->dev;
	debugfs_remove_recursive(new_dev->dentry);

	opp_set_dev_name(dev, opp_table->dentry_name);

	dentry = debugfs_rename(rootdir, opp_dev->dentry, rootdir,
				opp_table->dentry_name);
	if (!dentry) {
		dev_err(dev, "%s: Failed to rename link from: %s to %s\n",
			__func__, dev_name(opp_dev->dev), dev_name(dev));
		return;
	}

	new_dev->dentry = dentry;
	opp_table->dentry = dentry;
}

/**
 * opp_debug_unregister - remove a device opp node from debugfs opp directory
 * @opp_dev: opp-dev pointer for device
 * @opp_table: the device-opp being removed
 *
 * Dynamically removes device specific directory from debugfs 'opp' directory.
 */
void opp_debug_unregister(struct opp_device *opp_dev,
			  struct opp_table *opp_table)
{
	if (opp_dev->dentry == opp_table->dentry) {
		/* Move the real dentry object under another device */
		if (!list_is_singular(&opp_table->dev_list)) {
			opp_migrate_dentry(opp_dev, opp_table);
			goto out;
		}
		opp_table->dentry = NULL;
	}

	debugfs_remove_recursive(opp_dev->dentry);

out:
	opp_dev->dentry = NULL;
}

static int __init opp_debug_init(void)
{
	/* Create /sys/kernel/debug/opp directory */
	rootdir = debugfs_create_dir("opp", NULL);
	if (!rootdir) {
		pr_err("%s: Failed to create root directory\n", __func__);
		return -ENOMEM;
	}

	return 0;
}
core_initcall(opp_debug_init);
