/*
 *  linux/drivers/thermal/gpu_cooling.c
 *
 *  Copyright (C) 2012	Samsung Electronics Co., Ltd(http://www.samsung.com)
 *  Copyright (C) 2012  Amit Daniel <amit.kachhap@linaro.org>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/module.h>
#include <linux/thermal.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/gpu_cooling.h>


static DEFINE_IDR(gpufreq_idr);
static DEFINE_MUTEX(cooling_gpufreq_lock);


/* notify_table passes value to the gpuFREQ_ADJUST callback function. */
#define NOTIFY_INVALID NULL

/**
 * get_idr - function to get a unique id.
 * @idr: struct idr * handle used to create a id.
 * @id: int * value generated by this function.
 *
 * This function will populate @id with an unique
 * id, using the idr API.
 *
 * Return: 0 on success, an error code on failure.
 */
static int get_idr(struct idr *idr, int *id)
{
	int ret;

	mutex_lock(&cooling_gpufreq_lock);
	ret = idr_alloc(idr, NULL, 0, 0, GFP_KERNEL);
	mutex_unlock(&cooling_gpufreq_lock);
	if (unlikely(ret < 0))
		return ret;
	*id = ret;

	return 0;
}

/**
 * release_idr - function to free the unique id.
 * @idr: struct idr * handle used for creating the id.
 * @id: int value representing the unique id.
 */
static void release_idr(struct idr *idr, int id)
{
	mutex_lock(&cooling_gpufreq_lock);
	idr_remove(idr, id);
	mutex_unlock(&cooling_gpufreq_lock);
}


/* gpufreq cooling device callback functions are defined below */

/**
 * gpufreq_get_max_state - callback function to get the max cooling state.
 * @cdev: thermal cooling device pointer.
 * @state: fill this variable with the max cooling state.
 *
 * Callback for the thermal cooling device to return the gpufreq
 * max cooling state.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int gpufreq_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct gpufreq_cooling_device *gpufreq_device = cdev->devdata;
	if (gpufreq_device->get_gpu_max_level)
		*state = (unsigned long)(gpufreq_device->get_gpu_max_level());
	pr_debug("default max state=%ld\n", *state);
	return 0;
}

/**
 * gpufreq_get_cur_state - callback function to get the current cooling state.
 * @cdev: thermal cooling device pointer.
 * @state: fill this variable with the current cooling state.
 *
 * Callback for the thermal cooling device to return the gpufreq
 * current cooling state.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int gpufreq_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct gpufreq_cooling_device *gpufreq_device = cdev->devdata;
	unsigned long max_state = 0, temp = 0;

	/* *state = gpufreq_device->gpufreq_state; */
	gpufreq_get_max_state(cdev, &max_state);
	if (gpufreq_device->get_gpu_current_max_level) {
		temp = gpufreq_device->get_gpu_current_max_level();
		*state = ((max_state - 1) - temp);
		pr_debug("current max state=%ld\n", *state);
	} else
		return -EINVAL;
	return 0;
}

/**
 * gpufreq_set_cur_state - callback function to set the current cooling state.
 * @cdev: thermal cooling device pointer.
 * @state: set this variable to the current cooling state.
 *
 * Callback for the thermal cooling device to change the gpufreq
 * current cooling state.
 *
 * Return: 0 on success, an error code otherwise.
 */
static int gpufreq_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct gpufreq_cooling_device *gpufreq_device = cdev->devdata;
	unsigned long max_state = 0;
	int ret;
	pr_debug("state=%ld,gpufreq_device->gpufreq_state=%d\n",
		 state, gpufreq_device->gpufreq_state);
	/* if (gpufreq_device->gpufreq_state == state) */
		/* return 0; */
	gpufreq_device->gpufreq_state = state;
	ret = gpufreq_get_max_state(cdev, &max_state);
	state = max_state-1-state;

	pr_debug("state=%ld,gpufreq_device->gpufreq_state=%d\n",
		 state, gpufreq_device->gpufreq_state);
	if (state >= 0 && state <= max_state) {
		if (gpufreq_device->set_gpu_freq_idx)
			gpufreq_device->set_gpu_freq_idx((unsigned int)state);
	}
	return 0;


}

/* Bind gpufreq callbacks to thermal cooling device ops */
static struct thermal_cooling_device_ops const gpufreq_cooling_ops = {
	.get_max_state = gpufreq_get_max_state,
	.get_cur_state = gpufreq_get_cur_state,
	.set_cur_state = gpufreq_set_cur_state,
};


/**
 * gpufreq_cooling_register - function to create gpufreq cooling device.
 * @clip_gpus: gpumask of gpus where the frequency constraints will happen.
 *
 * This interface function registers the gpufreq cooling device with the name
 * "thermal-gpufreq-%x". This api can support multiple instances of gpufreq
 * cooling devices.
 *
 * Return: a valid struct thermal_cooling_device pointer on success,
 * on failure, it returns a corresponding ERR_PTR().
 */
struct gpufreq_cooling_device *gpufreq_cooling_alloc(void)
{
	struct gpufreq_cooling_device *gcdev;
	gcdev = kzalloc(sizeof(struct gpufreq_cooling_device), GFP_KERNEL);
	if (!gcdev) {
		pr_err("%s, %d, allocate memory fail\n", __func__, __LINE__);
		return ERR_PTR(-ENOMEM);
	}
	memset(gcdev, 0, sizeof(*gcdev));
	return gcdev;
}
EXPORT_SYMBOL_GPL(gpufreq_cooling_alloc);
int gpufreq_cooling_register(struct gpufreq_cooling_device *gpufreq_dev)
{
	struct thermal_cooling_device *cool_dev;
	char dev_name[THERMAL_NAME_LENGTH];
	int ret = 0;

	ret = get_idr(&gpufreq_idr, &gpufreq_dev->id);
	if (ret) {
		kfree(gpufreq_dev);
		return -EINVAL;
	}

	snprintf(dev_name, sizeof(dev_name), "thermal-gpufreq-%d",
		 gpufreq_dev->id);

	cool_dev = thermal_cooling_device_register(dev_name, gpufreq_dev,
						   &gpufreq_cooling_ops);
	if (!cool_dev) {
		release_idr(&gpufreq_idr, gpufreq_dev->id);
		kfree(gpufreq_dev);
		return -EINVAL;
	}
	gpufreq_dev->cool_dev = cool_dev;
	gpufreq_dev->gpufreq_state = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(gpufreq_cooling_register);

/**
 * gpufreq_cooling_unregister - function to remove gpufreq cooling device.
 * @cdev: thermal cooling device pointer.
 *
 * This interface function unregisters the "thermal-gpufreq-%x" cooling device.
 */
void gpufreq_cooling_unregister(struct thermal_cooling_device *cdev)
{
	struct gpufreq_cooling_device *gpufreq_dev;

	if (!cdev)
		return;

	gpufreq_dev = cdev->devdata;

	thermal_cooling_device_unregister(gpufreq_dev->cool_dev);
	release_idr(&gpufreq_idr, gpufreq_dev->id);
	kfree(gpufreq_dev);
}
EXPORT_SYMBOL_GPL(gpufreq_cooling_unregister);

unsigned int (*gpu_freq_callback)(void) = NULL;
EXPORT_SYMBOL(gpu_freq_callback);

int register_gpu_freq_info(unsigned int (*fun)(void))
{
	if (fun)
		gpu_freq_callback = fun;

	return 0;
}
EXPORT_SYMBOL(register_gpu_freq_info);
