/*
 * S5P/EXYNOS4 SoC series camera host interface media device driver
 *
 * Copyright (C) 2011 - 2012 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 */

#include <linux/bug.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <media/v4l2-ctrls.h>
#include <media/media-device.h>
#include <media/s5p_fimc.h>

#include "fimc-core.h"
#include "fimc-lite.h"
#include "fimc-mdevice.h"
#include "mipi-csis.h"

static int __fimc_md_set_camclk(struct fimc_md *fmd,
				struct fimc_sensor_info *s_info,
				bool on);
/**
 * fimc_pipeline_prepare - update pipeline information with subdevice pointers
 * @fimc: fimc device terminating the pipeline
 *
 * Caller holds the graph mutex.
 */
static void fimc_pipeline_prepare(struct fimc_pipeline *p,
				  struct media_entity *me)
{
	struct media_pad *pad = &me->pads[0];
	struct v4l2_subdev *sd;
	int i;

	for (i = 0; i < IDX_MAX; i++)
		p->subdevs[i] = NULL;

	while (1) {
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		/* source pad */
		pad = media_entity_remote_source(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		sd = media_entity_to_v4l2_subdev(pad->entity);

		switch (sd->grp_id) {
		case GRP_ID_FIMC_IS_SENSOR:
		case GRP_ID_SENSOR:
			p->subdevs[IDX_SENSOR] = sd;
			break;
		case GRP_ID_CSIS:
			p->subdevs[IDX_CSIS] = sd;
			break;
		case GRP_ID_FLITE:
			p->subdevs[IDX_FLITE] = sd;
			break;
		case GRP_ID_FIMC:
			/* No need to control FIMC subdev through subdev ops */
			break;
		default:
			pr_warn("%s: Unknown subdev grp_id: %#x\n",
				__func__, sd->grp_id);
		}
		/* sink pad */
		pad = &sd->entity.pads[0];
	}
}

/**
 * __subdev_set_power - change power state of a single subdev
 * @sd: subdevice to change power state for
 * @on: 1 to enable power or 0 to disable
 *
 * Return result of s_power subdev operation or -ENXIO if sd argument
 * is NULL. Return 0 if the subdevice does not implement s_power.
 */
static int __subdev_set_power(struct v4l2_subdev *sd, int on)
{
	int *use_count;
	int ret;

	if (sd == NULL)
		return -ENXIO;

	use_count = &sd->entity.use_count;
	if (on && (*use_count)++ > 0)
		return 0;
	else if (!on && (*use_count == 0 || --(*use_count) > 0))
		return 0;
	ret = v4l2_subdev_call(sd, core, s_power, on);

	return ret != -ENOIOCTLCMD ? ret : 0;
}

/**
 * fimc_pipeline_s_power - change power state of all pipeline subdevs
 * @fimc: fimc device terminating the pipeline
 * @state: true to power on, false to power off
 *
 * Needs to be called with the graph mutex held.
 */
static int fimc_pipeline_s_power(struct fimc_pipeline *p, bool state)
{
	unsigned int i;
	int ret;

	if (p->subdevs[IDX_SENSOR] == NULL)
		return -ENXIO;

	for (i = 0; i < IDX_MAX; i++) {
		unsigned int idx = state ? (IDX_MAX - 1) - i : i;

		ret = __subdev_set_power(p->subdevs[idx], state);
		if (ret < 0 && ret != -ENXIO)
			return ret;
	}

	return 0;
}

/**
 * __fimc_pipeline_open - update the pipeline information, enable power
 *                        of all pipeline subdevs and the sensor clock
 * @me: media entity to start graph walk with
 * @prep: true to acquire sensor (and csis) subdevs
 *
 * Called with the graph mutex held.
 */
static int __fimc_pipeline_open(struct fimc_pipeline *p,
				struct media_entity *me, bool prep)
{
	int ret;

	if (prep)
		fimc_pipeline_prepare(p, me);

	if (p->subdevs[IDX_SENSOR] == NULL)
		return -EINVAL;

	ret = fimc_md_set_camclk(p->subdevs[IDX_SENSOR], true);
	if (ret)
		return ret;

	return fimc_pipeline_s_power(p, 1);
}

/**
 * __fimc_pipeline_close - disable the sensor clock and pipeline power
 * @fimc: fimc device terminating the pipeline
 *
 * Disable power of all subdevs and turn the external sensor clock off.
 */
static int __fimc_pipeline_close(struct fimc_pipeline *p)
{
	int ret = 0;

	if (!p || !p->subdevs[IDX_SENSOR])
		return -EINVAL;

	if (p->subdevs[IDX_SENSOR]) {
		ret = fimc_pipeline_s_power(p, 0);
		fimc_md_set_camclk(p->subdevs[IDX_SENSOR], false);
	}
	return ret == -ENXIO ? 0 : ret;
}

/**
 * __fimc_pipeline_s_stream - invoke s_stream on pipeline subdevs
 * @pipeline: video pipeline structure
 * @on: passed as the s_stream call argument
 */
static int __fimc_pipeline_s_stream(struct fimc_pipeline *p, bool on)
{
	int i, ret;

	if (p->subdevs[IDX_SENSOR] == NULL)
		return -ENODEV;

	for (i = 0; i < IDX_MAX; i++) {
		unsigned int idx = on ? (IDX_MAX - 1) - i : i;

		ret = v4l2_subdev_call(p->subdevs[idx], video, s_stream, on);

		if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
			return ret;
	}

	return 0;

}

/* Media pipeline operations for the FIMC/FIMC-LITE video device driver */
static const struct fimc_pipeline_ops fimc_pipeline_ops = {
	.open		= __fimc_pipeline_open,
	.close		= __fimc_pipeline_close,
	.set_stream	= __fimc_pipeline_s_stream,
};

/*
 * Sensor subdevice helper functions
 */
static struct v4l2_subdev *fimc_md_register_sensor(struct fimc_md *fmd,
				   struct fimc_sensor_info *s_info)
{
	struct i2c_adapter *adapter;
	struct v4l2_subdev *sd = NULL;

	if (!s_info || !fmd)
		return NULL;

	adapter = i2c_get_adapter(s_info->pdata.i2c_bus_num);
	if (!adapter) {
		v4l2_warn(&fmd->v4l2_dev,
			  "Failed to get I2C adapter %d, deferring probe\n",
			  s_info->pdata.i2c_bus_num);
		return ERR_PTR(-EPROBE_DEFER);
	}
	sd = v4l2_i2c_new_subdev_board(&fmd->v4l2_dev, adapter,
				       s_info->pdata.board_info, NULL);
	if (IS_ERR_OR_NULL(sd)) {
		i2c_put_adapter(adapter);
		v4l2_warn(&fmd->v4l2_dev,
			  "Failed to acquire subdev %s, deferring probe\n",
			  s_info->pdata.board_info->type);
		return ERR_PTR(-EPROBE_DEFER);
	}
	v4l2_set_subdev_hostdata(sd, s_info);
	sd->grp_id = GRP_ID_SENSOR;

	v4l2_info(&fmd->v4l2_dev, "Registered sensor subdevice %s\n",
		  s_info->pdata.board_info->type);
	return sd;
}

static void fimc_md_unregister_sensor(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_adapter *adapter;

	if (!client)
		return;
	v4l2_device_unregister_subdev(sd);
	adapter = client->adapter;
	i2c_unregister_device(client);
	if (adapter)
		i2c_put_adapter(adapter);
}

static int fimc_md_register_sensor_entities(struct fimc_md *fmd)
{
	struct s5p_platform_fimc *pdata = fmd->pdev->dev.platform_data;
	struct fimc_dev *fd = NULL;
	int num_clients, ret, i;

	/*
	 * Runtime resume one of the FIMC entities to make sure
	 * the sclk_cam clocks are not globally disabled.
	 */
	for (i = 0; !fd && i < ARRAY_SIZE(fmd->fimc); i++)
		if (fmd->fimc[i])
			fd = fmd->fimc[i];
	if (!fd)
		return -ENXIO;
	ret = pm_runtime_get_sync(&fd->pdev->dev);
	if (ret < 0)
		return ret;

	WARN_ON(pdata->num_clients > ARRAY_SIZE(fmd->sensor));
	num_clients = min_t(u32, pdata->num_clients, ARRAY_SIZE(fmd->sensor));

	fmd->num_sensors = num_clients;
	for (i = 0; i < num_clients; i++) {
		struct v4l2_subdev *sd;

		fmd->sensor[i].pdata = pdata->source_info[i];
		ret = __fimc_md_set_camclk(fmd, &fmd->sensor[i], true);
		if (ret)
			break;
		sd = fimc_md_register_sensor(fmd, &fmd->sensor[i]);
		ret = __fimc_md_set_camclk(fmd, &fmd->sensor[i], false);

		if (!IS_ERR(sd)) {
			fmd->sensor[i].subdev = sd;
		} else {
			fmd->sensor[i].subdev = NULL;
			ret = PTR_ERR(sd);
			break;
		}
		if (ret)
			break;
	}
	pm_runtime_put(&fd->pdev->dev);
	return ret;
}

/*
 * MIPI-CSIS, FIMC and FIMC-LITE platform devices registration.
 */

static int register_fimc_lite_entity(struct fimc_md *fmd,
				     struct fimc_lite *fimc_lite)
{
	struct v4l2_subdev *sd;
	int ret;

	if (WARN_ON(fimc_lite->index >= FIMC_LITE_MAX_DEVS ||
		    fmd->fimc_lite[fimc_lite->index]))
		return -EBUSY;

	sd = &fimc_lite->subdev;
	sd->grp_id = GRP_ID_FLITE;
	v4l2_set_subdev_hostdata(sd, (void *)&fimc_pipeline_ops);

	ret = v4l2_device_register_subdev(&fmd->v4l2_dev, sd);
	if (!ret)
		fmd->fimc_lite[fimc_lite->index] = fimc_lite;
	else
		v4l2_err(&fmd->v4l2_dev, "Failed to register FIMC.LITE%d\n",
			 fimc_lite->index);
	return ret;
}

static int register_fimc_entity(struct fimc_md *fmd, struct fimc_dev *fimc)
{
	struct v4l2_subdev *sd;
	int ret;

	if (WARN_ON(fimc->id >= FIMC_MAX_DEVS || fmd->fimc[fimc->id]))
		return -EBUSY;

	sd = &fimc->vid_cap.subdev;
	sd->grp_id = GRP_ID_FIMC;
	v4l2_set_subdev_hostdata(sd, (void *)&fimc_pipeline_ops);

	ret = v4l2_device_register_subdev(&fmd->v4l2_dev, sd);
	if (!ret) {
		fmd->fimc[fimc->id] = fimc;
		fimc->vid_cap.user_subdev_api = fmd->user_subdev_api;
	} else {
		v4l2_err(&fmd->v4l2_dev, "Failed to register FIMC.%d (%d)\n",
			 fimc->id, ret);
	}
	return ret;
}

static int register_csis_entity(struct fimc_md *fmd,
				struct platform_device *pdev,
				struct v4l2_subdev *sd)
{
	struct device_node *node = pdev->dev.of_node;
	int id, ret;

	id = node ? of_alias_get_id(node, "csis") : max(0, pdev->id);

	if (WARN_ON(id >= CSIS_MAX_ENTITIES || fmd->csis[id].sd))
		return -EBUSY;

	if (WARN_ON(id >= CSIS_MAX_ENTITIES))
		return 0;

	sd->grp_id = GRP_ID_CSIS;
	ret = v4l2_device_register_subdev(&fmd->v4l2_dev, sd);
	if (!ret)
		fmd->csis[id].sd = sd;
	else
		v4l2_err(&fmd->v4l2_dev,
			 "Failed to register MIPI-CSIS.%d (%d)\n", id, ret);
	return ret;
}

static int fimc_md_register_platform_entity(struct fimc_md *fmd,
					    struct platform_device *pdev,
					    int plat_entity)
{
	struct device *dev = &pdev->dev;
	int ret = -EPROBE_DEFER;
	void *drvdata;

	/* Lock to ensure dev->driver won't change. */
	device_lock(dev);

	if (!dev->driver || !try_module_get(dev->driver->owner))
		goto dev_unlock;

	drvdata = dev_get_drvdata(dev);
	/* Some subdev didn't probe succesfully id drvdata is NULL */
	if (drvdata) {
		switch (plat_entity) {
		case IDX_FIMC:
			ret = register_fimc_entity(fmd, drvdata);
			break;
		case IDX_FLITE:
			ret = register_fimc_lite_entity(fmd, drvdata);
			break;
		case IDX_CSIS:
			ret = register_csis_entity(fmd, pdev, drvdata);
			break;
		default:
			ret = -ENODEV;
		}
	}

	module_put(dev->driver->owner);
dev_unlock:
	device_unlock(dev);
	if (ret == -EPROBE_DEFER)
		dev_info(&fmd->pdev->dev, "deferring %s device registration\n",
			dev_name(dev));
	else if (ret < 0)
		dev_err(&fmd->pdev->dev, "%s device registration failed (%d)\n",
			dev_name(dev), ret);
	return ret;
}

static int fimc_md_pdev_match(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	int plat_entity = -1;
	int ret;
	char *p;

	if (!get_device(dev))
		return -ENODEV;

	if (!strcmp(pdev->name, CSIS_DRIVER_NAME)) {
		plat_entity = IDX_CSIS;
	} else if (!strcmp(pdev->name, FIMC_LITE_DRV_NAME)) {
		plat_entity = IDX_FLITE;
	} else {
		p = strstr(pdev->name, "fimc");
		if (p && *(p + 4) == 0)
			plat_entity = IDX_FIMC;
	}

	if (plat_entity >= 0)
		ret = fimc_md_register_platform_entity(data, pdev,
						       plat_entity);
	put_device(dev);
	return 0;
}

static void fimc_md_unregister_entities(struct fimc_md *fmd)
{
	int i;

	for (i = 0; i < FIMC_MAX_DEVS; i++) {
		if (fmd->fimc[i] == NULL)
			continue;
		v4l2_device_unregister_subdev(&fmd->fimc[i]->vid_cap.subdev);
		fmd->fimc[i]->pipeline_ops = NULL;
		fmd->fimc[i] = NULL;
	}
	for (i = 0; i < FIMC_LITE_MAX_DEVS; i++) {
		if (fmd->fimc_lite[i] == NULL)
			continue;
		v4l2_device_unregister_subdev(&fmd->fimc_lite[i]->subdev);
		fmd->fimc_lite[i]->pipeline_ops = NULL;
		fmd->fimc_lite[i] = NULL;
	}
	for (i = 0; i < CSIS_MAX_ENTITIES; i++) {
		if (fmd->csis[i].sd == NULL)
			continue;
		v4l2_device_unregister_subdev(fmd->csis[i].sd);
		module_put(fmd->csis[i].sd->owner);
		fmd->csis[i].sd = NULL;
	}
	for (i = 0; i < fmd->num_sensors; i++) {
		if (fmd->sensor[i].subdev == NULL)
			continue;
		fimc_md_unregister_sensor(fmd->sensor[i].subdev);
		fmd->sensor[i].subdev = NULL;
	}
	v4l2_info(&fmd->v4l2_dev, "Unregistered all entities\n");
}

/**
 * __fimc_md_create_fimc_links - create links to all FIMC entities
 * @fmd: fimc media device
 * @source: the source entity to create links to all fimc entities from
 * @sensor: sensor subdev linked to FIMC[fimc_id] entity, may be null
 * @pad: the source entity pad index
 * @link_mask: bitmask of the fimc devices for which link should be enabled
 */
static int __fimc_md_create_fimc_sink_links(struct fimc_md *fmd,
					    struct media_entity *source,
					    struct v4l2_subdev *sensor,
					    int pad, int link_mask)
{
	struct fimc_sensor_info *s_info = NULL;
	struct media_entity *sink;
	unsigned int flags = 0;
	int ret, i;

	for (i = 0; i < FIMC_MAX_DEVS; i++) {
		if (!fmd->fimc[i])
			continue;
		/*
		 * Some FIMC variants are not fitted with camera capture
		 * interface. Skip creating a link from sensor for those.
		 */
		if (!fmd->fimc[i]->variant->has_cam_if)
			continue;

		flags = ((1 << i) & link_mask) ? MEDIA_LNK_FL_ENABLED : 0;

		sink = &fmd->fimc[i]->vid_cap.subdev.entity;
		ret = media_entity_create_link(source, pad, sink,
					      FIMC_SD_PAD_SINK, flags);
		if (ret)
			return ret;

		/* Notify FIMC capture subdev entity */
		ret = media_entity_call(sink, link_setup, &sink->pads[0],
					&source->pads[pad], flags);
		if (ret)
			break;

		v4l2_info(&fmd->v4l2_dev, "created link [%s] %c> [%s]\n",
			  source->name, flags ? '=' : '-', sink->name);

		if (flags == 0 || sensor == NULL)
			continue;
		s_info = v4l2_get_subdev_hostdata(sensor);
		if (!WARN_ON(s_info == NULL)) {
			unsigned long irq_flags;
			spin_lock_irqsave(&fmd->slock, irq_flags);
			s_info->host = fmd->fimc[i];
			spin_unlock_irqrestore(&fmd->slock, irq_flags);
		}
	}

	for (i = 0; i < FIMC_LITE_MAX_DEVS; i++) {
		if (!fmd->fimc_lite[i])
			continue;

		if (link_mask & (1 << (i + FIMC_MAX_DEVS)))
			flags = MEDIA_LNK_FL_ENABLED;
		else
			flags = 0;

		sink = &fmd->fimc_lite[i]->subdev.entity;
		ret = media_entity_create_link(source, pad, sink,
					       FLITE_SD_PAD_SINK, flags);
		if (ret)
			return ret;

		/* Notify FIMC-LITE subdev entity */
		ret = media_entity_call(sink, link_setup, &sink->pads[0],
					&source->pads[pad], flags);
		if (ret)
			break;

		v4l2_info(&fmd->v4l2_dev, "created link [%s] %c> [%s]\n",
			  source->name, flags ? '=' : '-', sink->name);
	}
	return 0;
}

/* Create links from FIMC-LITE source pads to other entities */
static int __fimc_md_create_flite_source_links(struct fimc_md *fmd)
{
	struct media_entity *source, *sink;
	unsigned int flags = MEDIA_LNK_FL_ENABLED;
	int i, ret = 0;

	for (i = 0; i < FIMC_LITE_MAX_DEVS; i++) {
		struct fimc_lite *fimc = fmd->fimc_lite[i];
		if (fimc == NULL)
			continue;
		source = &fimc->subdev.entity;
		sink = &fimc->vfd.entity;
		/* FIMC-LITE's subdev and video node */
		ret = media_entity_create_link(source, FLITE_SD_PAD_SOURCE_DMA,
					       sink, 0, flags);
		if (ret)
			break;
		/* TODO: create links to other entities */
	}

	return ret;
}

/**
 * fimc_md_create_links - create default links between registered entities
 *
 * Parallel interface sensor entities are connected directly to FIMC capture
 * entities. The sensors using MIPI CSIS bus are connected through immutable
 * link with CSI receiver entity specified by mux_id. Any registered CSIS
 * entity has a link to each registered FIMC capture entity. Enabled links
 * are created by default between each subsequent registered sensor and
 * subsequent FIMC capture entity. The number of default active links is
 * determined by the number of available sensors or FIMC entities,
 * whichever is less.
 */
static int fimc_md_create_links(struct fimc_md *fmd)
{
	struct v4l2_subdev *csi_sensors[CSIS_MAX_ENTITIES] = { NULL };
	struct v4l2_subdev *sensor, *csis;
	struct fimc_source_info *pdata;
	struct fimc_sensor_info *s_info;
	struct media_entity *source, *sink;
	int i, pad, fimc_id = 0, ret = 0;
	u32 flags, link_mask = 0;

	for (i = 0; i < fmd->num_sensors; i++) {
		if (fmd->sensor[i].subdev == NULL)
			continue;

		sensor = fmd->sensor[i].subdev;
		s_info = v4l2_get_subdev_hostdata(sensor);
		if (!s_info)
			continue;

		source = NULL;
		pdata = &s_info->pdata;

		switch (pdata->sensor_bus_type) {
		case FIMC_BUS_TYPE_MIPI_CSI2:
			if (WARN(pdata->mux_id >= CSIS_MAX_ENTITIES,
				"Wrong CSI channel id: %d\n", pdata->mux_id))
				return -EINVAL;

			csis = fmd->csis[pdata->mux_id].sd;
			if (WARN(csis == NULL,
				 "MIPI-CSI interface specified "
				 "but s5p-csis module is not loaded!\n"))
				return -EINVAL;

			pad = sensor->entity.num_pads - 1;
			ret = media_entity_create_link(&sensor->entity, pad,
					      &csis->entity, CSIS_PAD_SINK,
					      MEDIA_LNK_FL_IMMUTABLE |
					      MEDIA_LNK_FL_ENABLED);
			if (ret)
				return ret;

			v4l2_info(&fmd->v4l2_dev, "created link [%s] => [%s]\n",
				  sensor->entity.name, csis->entity.name);

			source = NULL;
			csi_sensors[pdata->mux_id] = sensor;
			break;

		case FIMC_BUS_TYPE_ITU_601...FIMC_BUS_TYPE_ITU_656:
			source = &sensor->entity;
			pad = 0;
			break;

		default:
			v4l2_err(&fmd->v4l2_dev, "Wrong bus_type: %x\n",
				 pdata->sensor_bus_type);
			return -EINVAL;
		}
		if (source == NULL)
			continue;

		link_mask = 1 << fimc_id++;
		ret = __fimc_md_create_fimc_sink_links(fmd, source, sensor,
						       pad, link_mask);
	}

	for (i = 0; i < CSIS_MAX_ENTITIES; i++) {
		if (fmd->csis[i].sd == NULL)
			continue;
		source = &fmd->csis[i].sd->entity;
		pad = CSIS_PAD_SOURCE;
		sensor = csi_sensors[i];

		link_mask = 1 << fimc_id++;
		ret = __fimc_md_create_fimc_sink_links(fmd, source, sensor,
						       pad, link_mask);
	}

	/* Create immutable links between each FIMC's subdev and video node */
	flags = MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED;
	for (i = 0; i < FIMC_MAX_DEVS; i++) {
		if (!fmd->fimc[i])
			continue;
		source = &fmd->fimc[i]->vid_cap.subdev.entity;
		sink = &fmd->fimc[i]->vid_cap.vfd.entity;
		ret = media_entity_create_link(source, FIMC_SD_PAD_SOURCE,
					      sink, 0, flags);
		if (ret)
			break;
	}

	return __fimc_md_create_flite_source_links(fmd);
}

/*
 * The peripheral sensor clock management.
 */
static void fimc_md_put_clocks(struct fimc_md *fmd)
{
	int i = FIMC_MAX_CAMCLKS;

	while (--i >= 0) {
		if (IS_ERR(fmd->camclk[i].clock))
			continue;
		clk_unprepare(fmd->camclk[i].clock);
		clk_put(fmd->camclk[i].clock);
		fmd->camclk[i].clock = ERR_PTR(-EINVAL);
	}
}

static int fimc_md_get_clocks(struct fimc_md *fmd)
{
	struct device *dev = NULL;
	char clk_name[32];
	struct clk *clock;
	int ret, i;

	for (i = 0; i < FIMC_MAX_CAMCLKS; i++)
		fmd->camclk[i].clock = ERR_PTR(-EINVAL);

	if (fmd->pdev->dev.of_node)
		dev = &fmd->pdev->dev;

	for (i = 0; i < FIMC_MAX_CAMCLKS; i++) {
		snprintf(clk_name, sizeof(clk_name), "sclk_cam%u", i);
		clock = clk_get(dev, clk_name);

		if (IS_ERR(clock)) {
			dev_err(&fmd->pdev->dev, "Failed to get clock: %s\n",
								clk_name);
			ret = PTR_ERR(clock);
			break;
		}
		ret = clk_prepare(clock);
		if (ret < 0) {
			clk_put(clock);
			fmd->camclk[i].clock = ERR_PTR(-EINVAL);
			break;
		}
		fmd->camclk[i].clock = clock;
	}
	if (ret)
		fimc_md_put_clocks(fmd);

	return ret;
}

static int __fimc_md_set_camclk(struct fimc_md *fmd,
				struct fimc_sensor_info *s_info,
				bool on)
{
	struct fimc_source_info *pdata = &s_info->pdata;
	struct fimc_camclk_info *camclk;
	int ret = 0;

	if (WARN_ON(pdata->clk_id >= FIMC_MAX_CAMCLKS) || fmd == NULL)
		return -EINVAL;

	camclk = &fmd->camclk[pdata->clk_id];

	dbg("camclk %d, f: %lu, use_count: %d, on: %d",
	    pdata->clk_id, pdata->clk_frequency, camclk->use_count, on);

	if (on) {
		if (camclk->use_count > 0 &&
		    camclk->frequency != pdata->clk_frequency)
			return -EINVAL;

		if (camclk->use_count++ == 0) {
			clk_set_rate(camclk->clock, pdata->clk_frequency);
			camclk->frequency = pdata->clk_frequency;
			ret = clk_enable(camclk->clock);
			dbg("Enabled camclk %d: f: %lu", pdata->clk_id,
			    clk_get_rate(camclk->clock));
		}
		return ret;
	}

	if (WARN_ON(camclk->use_count == 0))
		return 0;

	if (--camclk->use_count == 0) {
		clk_disable(camclk->clock);
		dbg("Disabled camclk %d", pdata->clk_id);
	}
	return ret;
}

/**
 * fimc_md_set_camclk - peripheral sensor clock setup
 * @sd: sensor subdev to configure sclk_cam clock for
 * @on: 1 to enable or 0 to disable the clock
 *
 * There are 2 separate clock outputs available in the SoC for external
 * image processors. These clocks are shared between all registered FIMC
 * devices to which sensors can be attached, either directly or through
 * the MIPI CSI receiver. The clock is allowed here to be used by
 * multiple sensors concurrently if they use same frequency.
 * This function should only be called when the graph mutex is held.
 */
int fimc_md_set_camclk(struct v4l2_subdev *sd, bool on)
{
	struct fimc_sensor_info *s_info = v4l2_get_subdev_hostdata(sd);
	struct fimc_md *fmd = entity_to_fimc_mdev(&sd->entity);

	return __fimc_md_set_camclk(fmd, s_info, on);
}

static int fimc_md_link_notify(struct media_pad *source,
			       struct media_pad *sink, u32 flags)
{
	struct fimc_lite *fimc_lite = NULL;
	struct fimc_dev *fimc = NULL;
	struct fimc_pipeline *pipeline;
	struct v4l2_subdev *sd;
	struct mutex *lock;
	int ret = 0;
	int ref_count;

	if (media_entity_type(sink->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
		return 0;

	sd = media_entity_to_v4l2_subdev(sink->entity);

	switch (sd->grp_id) {
	case GRP_ID_FLITE:
		fimc_lite = v4l2_get_subdevdata(sd);
		if (WARN_ON(fimc_lite == NULL))
			return 0;
		pipeline = &fimc_lite->pipeline;
		lock = &fimc_lite->lock;
		break;
	case GRP_ID_FIMC:
		fimc = v4l2_get_subdevdata(sd);
		if (WARN_ON(fimc == NULL))
			return 0;
		pipeline = &fimc->pipeline;
		lock = &fimc->lock;
		break;
	default:
		return 0;
	}

	if (!(flags & MEDIA_LNK_FL_ENABLED)) {
		int i;
		mutex_lock(lock);
		ret = __fimc_pipeline_close(pipeline);
		for (i = 0; i < IDX_MAX; i++)
			pipeline->subdevs[i] = NULL;
		if (fimc)
			fimc_ctrls_delete(fimc->vid_cap.ctx);
		mutex_unlock(lock);
		return ret;
	}
	/*
	 * Link activation. Enable power of pipeline elements only if the
	 * pipeline is already in use, i.e. its video node is opened.
	 * Recreate the controls destroyed during the link deactivation.
	 */
	mutex_lock(lock);

	ref_count = fimc ? fimc->vid_cap.refcnt : fimc_lite->ref_count;
	if (ref_count > 0)
		ret = __fimc_pipeline_open(pipeline, source->entity, true);
	if (!ret && fimc)
		ret = fimc_capture_ctrls_create(fimc);

	mutex_unlock(lock);
	return ret ? -EPIPE : ret;
}

static ssize_t fimc_md_sysfs_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fimc_md *fmd = platform_get_drvdata(pdev);

	if (fmd->user_subdev_api)
		return strlcpy(buf, "Sub-device API (sub-dev)\n", PAGE_SIZE);

	return strlcpy(buf, "V4L2 video node only API (vid-dev)\n", PAGE_SIZE);
}

static ssize_t fimc_md_sysfs_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct fimc_md *fmd = platform_get_drvdata(pdev);
	bool subdev_api;
	int i;

	if (!strcmp(buf, "vid-dev\n"))
		subdev_api = false;
	else if (!strcmp(buf, "sub-dev\n"))
		subdev_api = true;
	else
		return count;

	fmd->user_subdev_api = subdev_api;
	for (i = 0; i < FIMC_MAX_DEVS; i++)
		if (fmd->fimc[i])
			fmd->fimc[i]->vid_cap.user_subdev_api = subdev_api;
	return count;
}
/*
 * This device attribute is to select video pipeline configuration method.
 * There are following valid values:
 *  vid-dev - for V4L2 video node API only, subdevice will be configured
 *  by the host driver.
 *  sub-dev - for media controller API, subdevs must be configured in user
 *  space before starting streaming.
 */
static DEVICE_ATTR(subdev_conf_mode, S_IWUSR | S_IRUGO,
		   fimc_md_sysfs_show, fimc_md_sysfs_store);

static int fimc_md_probe(struct platform_device *pdev)
{
	struct v4l2_device *v4l2_dev;
	struct fimc_md *fmd;
	int ret;

	fmd = devm_kzalloc(&pdev->dev, sizeof(*fmd), GFP_KERNEL);
	if (!fmd)
		return -ENOMEM;

	spin_lock_init(&fmd->slock);
	fmd->pdev = pdev;

	strlcpy(fmd->media_dev.model, "SAMSUNG S5P FIMC",
		sizeof(fmd->media_dev.model));
	fmd->media_dev.link_notify = fimc_md_link_notify;
	fmd->media_dev.dev = &pdev->dev;

	v4l2_dev = &fmd->v4l2_dev;
	v4l2_dev->mdev = &fmd->media_dev;
	v4l2_dev->notify = fimc_sensor_notify;
	snprintf(v4l2_dev->name, sizeof(v4l2_dev->name), "%s",
		 dev_name(&pdev->dev));

	ret = v4l2_device_register(&pdev->dev, &fmd->v4l2_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register v4l2_device: %d\n", ret);
		return ret;
	}
	ret = media_device_register(&fmd->media_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register media device: %d\n", ret);
		goto err_md;
	}
	ret = fimc_md_get_clocks(fmd);
	if (ret)
		goto err_clk;

	fmd->user_subdev_api = false;

	/* Protect the media graph while we're registering entities */
	mutex_lock(&fmd->media_dev.graph_mutex);

	ret = bus_for_each_dev(&platform_bus_type, NULL, fmd,
					fimc_md_pdev_match);
	if (ret)
		goto err_unlock;

	if (pdev->dev.platform_data) {
		ret = fimc_md_register_sensor_entities(fmd);
		if (ret)
			goto err_unlock;
	}
	ret = fimc_md_create_links(fmd);
	if (ret)
		goto err_unlock;
	ret = v4l2_device_register_subdev_nodes(&fmd->v4l2_dev);
	if (ret)
		goto err_unlock;

	ret = device_create_file(&pdev->dev, &dev_attr_subdev_conf_mode);
	if (ret)
		goto err_unlock;

	platform_set_drvdata(pdev, fmd);
	mutex_unlock(&fmd->media_dev.graph_mutex);
	return 0;

err_unlock:
	mutex_unlock(&fmd->media_dev.graph_mutex);
err_clk:
	media_device_unregister(&fmd->media_dev);
	fimc_md_put_clocks(fmd);
	fimc_md_unregister_entities(fmd);
err_md:
	v4l2_device_unregister(&fmd->v4l2_dev);
	return ret;
}

static int fimc_md_remove(struct platform_device *pdev)
{
	struct fimc_md *fmd = platform_get_drvdata(pdev);

	if (!fmd)
		return 0;
	device_remove_file(&pdev->dev, &dev_attr_subdev_conf_mode);
	fimc_md_unregister_entities(fmd);
	media_device_unregister(&fmd->media_dev);
	fimc_md_put_clocks(fmd);
	return 0;
}

static struct platform_driver fimc_md_driver = {
	.probe		= fimc_md_probe,
	.remove		= fimc_md_remove,
	.driver = {
		.name	= "s5p-fimc-md",
		.owner	= THIS_MODULE,
	}
};

static int __init fimc_md_init(void)
{
	int ret;

	request_module("s5p-csis");
	ret = fimc_register_driver();
	if (ret)
		return ret;

	return platform_driver_register(&fimc_md_driver);
}

static void __exit fimc_md_exit(void)
{
	platform_driver_unregister(&fimc_md_driver);
	fimc_unregister_driver();
}

module_init(fimc_md_init);
module_exit(fimc_md_exit);

MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_DESCRIPTION("S5P FIMC camera host interface/video postprocessor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("2.0.1");
