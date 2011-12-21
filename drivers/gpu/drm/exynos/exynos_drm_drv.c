/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm.h"
#include "drm_crtc_helper.h"

#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_crtc.h"
#include "exynos_drm_fbdev.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_gem.h"

#define DRIVER_NAME	"exynos-drm"
#define DRIVER_DESC	"Samsung SoC DRM"
#define DRIVER_DATE	"20110530"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static int exynos_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct exynos_drm_private *private;
	int ret;
	int nr;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	private = kzalloc(sizeof(struct exynos_drm_private), GFP_KERNEL);
	if (!private) {
		DRM_ERROR("failed to allocate private\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&private->pageflip_event_list);
	dev->dev_private = (void *)private;

	drm_mode_config_init(dev);

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(dev);

	exynos_drm_mode_config_init(dev);

	/*
	 * EXYNOS4 is enough to have two CRTCs and each crtc would be used
	 * without dependency of hardware.
	 */
	for (nr = 0; nr < MAX_CRTC; nr++) {
		ret = exynos_drm_crtc_create(dev, nr);
		if (ret)
			goto err_crtc;
	}

	ret = drm_vblank_init(dev, MAX_CRTC);
	if (ret)
		goto err_crtc;

	/*
	 * probe sub drivers such as display controller and hdmi driver,
	 * that were registered at probe() of platform driver
	 * to the sub driver and create encoder and connector for them.
	 */
	ret = exynos_drm_device_register(dev);
	if (ret)
		goto err_vblank;

	/*
	 * create and configure fb helper and also exynos specific
	 * fbdev object.
	 */
	ret = exynos_drm_fbdev_init(dev);
	if (ret) {
		DRM_ERROR("failed to initialize drm fbdev\n");
		goto err_drm_device;
	}

	return 0;

err_drm_device:
	exynos_drm_device_unregister(dev);
err_vblank:
	drm_vblank_cleanup(dev);
err_crtc:
	drm_mode_config_cleanup(dev);
	kfree(private);

	return ret;
}

static int exynos_drm_unload(struct drm_device *dev)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	exynos_drm_fbdev_fini(dev);
	exynos_drm_device_unregister(dev);
	drm_vblank_cleanup(dev);
	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);
	kfree(dev->dev_private);

	dev->dev_private = NULL;

	return 0;
}

static void exynos_drm_preclose(struct drm_device *dev,
					struct drm_file *file_priv)
{
	struct exynos_drm_private *dev_priv = dev->dev_private;

	/*
	 * drm framework frees all events at release time,
	 * so private event list should be cleared.
	 */
	if (!list_empty(&dev_priv->pageflip_event_list))
		INIT_LIST_HEAD(&dev_priv->pageflip_event_list);
}

static void exynos_drm_lastclose(struct drm_device *dev)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	exynos_drm_fbdev_restore_mode(dev);
}

static struct vm_operations_struct exynos_drm_gem_vm_ops = {
	.fault = exynos_drm_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static struct drm_ioctl_desc exynos_ioctls[] = {
	DRM_IOCTL_DEF_DRV(EXYNOS_GEM_CREATE, exynos_drm_gem_create_ioctl,
			DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYNOS_GEM_MAP_OFFSET,
			exynos_drm_gem_map_offset_ioctl, DRM_UNLOCKED |
			DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYNOS_GEM_MMAP,
			exynos_drm_gem_mmap_ioctl, DRM_UNLOCKED | DRM_AUTH),
};

static struct drm_driver exynos_drm_driver = {
	.driver_features	= DRIVER_HAVE_IRQ | DRIVER_BUS_PLATFORM |
				  DRIVER_MODESET | DRIVER_GEM,
	.load			= exynos_drm_load,
	.unload			= exynos_drm_unload,
	.preclose		= exynos_drm_preclose,
	.lastclose		= exynos_drm_lastclose,
	.get_vblank_counter	= drm_vblank_count,
	.enable_vblank		= exynos_drm_crtc_enable_vblank,
	.disable_vblank		= exynos_drm_crtc_disable_vblank,
	.gem_init_object	= exynos_drm_gem_init_object,
	.gem_free_object	= exynos_drm_gem_free_object,
	.gem_vm_ops		= &exynos_drm_gem_vm_ops,
	.dumb_create		= exynos_drm_gem_dumb_create,
	.dumb_map_offset	= exynos_drm_gem_dumb_map_offset,
	.dumb_destroy		= exynos_drm_gem_dumb_destroy,
	.ioctls			= exynos_ioctls,
	.fops = {
		.owner		= THIS_MODULE,
		.open		= drm_open,
		.mmap		= exynos_drm_gem_mmap,
		.poll		= drm_poll,
		.read		= drm_read,
		.unlocked_ioctl	= drm_ioctl,
		.release	= drm_release,
	},
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

static int exynos_drm_platform_probe(struct platform_device *pdev)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	exynos_drm_driver.num_ioctls = DRM_ARRAY_SIZE(exynos_ioctls);

	return drm_platform_init(&exynos_drm_driver, pdev);
}

static int exynos_drm_platform_remove(struct platform_device *pdev)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	drm_platform_exit(&exynos_drm_driver, pdev);

	return 0;
}

static struct platform_driver exynos_drm_platform_driver = {
	.probe		= exynos_drm_platform_probe,
	.remove		= __devexit_p(exynos_drm_platform_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= DRIVER_NAME,
	},
};

static int __init exynos_drm_init(void)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	return platform_driver_register(&exynos_drm_platform_driver);
}

static void __exit exynos_drm_exit(void)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	platform_driver_unregister(&exynos_drm_platform_driver);
}

module_init(exynos_drm_init);
module_exit(exynos_drm_exit);

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_AUTHOR("Seung-Woo Kim <sw0312.kim@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM Driver");
MODULE_LICENSE("GPL");
