/*
 * addi_apci_1516.c
 * Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
 * Project manager: Eric Stolz
 *
 *	ADDI-DATA GmbH
 *	Dieselstrasse 3
 *	D-77833 Ottersweier
 *	Tel: +19(0)7223/9493-0
 *	Fax: +49(0)7223/9493-92
 *	http://www.addi-data.com
 *	info@addi-data.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * You should also find the complete GPL in the COPYING file accompanying
 * this source code.
 */

#include "../comedidev.h"
#include "comedi_fc.h"

/*
 * PCI device ids supported by this driver
 */
#define PCI_DEVICE_ID_APCI1016		0x1000
#define PCI_DEVICE_ID_APCI1516		0x1001
#define PCI_DEVICE_ID_APCI2016		0x1002

/*
 * PCI bar 1 I/O Register map - Digital input/output
 */
#define APCI1516_DI_REG			0x00
#define APCI1516_DO_REG			0x04

/*
 * PCI bar 2 I/O Register map - Watchdog (APCI-1516 and APCI-2016)
 */
#define APCI1516_WDOG_REG		0x00
#define APCI1516_WDOG_RELOAD_REG	0x04
#define APCI1516_WDOG_CTRL_REG		0x0c
#define APCI1516_WDOG_CTRL_ENABLE	(1 << 0)
#define APCI1516_WDOG_CTRL_SW_TRIG	(1 << 9)
#define APCI1516_WDOG_STATUS_REG	0x10
#define APCI1516_WDOG_STATUS_ENABLED	(1 << 0)
#define APCI1516_WDOG_STATUS_SW_TRIG	(1 << 1)

struct apci1516_boardinfo {
	const char *name;
	unsigned short device;
	int di_nchan;
	int do_nchan;
	int has_wdog;
};

static const struct apci1516_boardinfo apci1516_boardtypes[] = {
	{
		.name		= "apci1016",
		.device		= PCI_DEVICE_ID_APCI1016,
		.di_nchan	= 16,
	}, {
		.name		= "apci1516",
		.device		= PCI_DEVICE_ID_APCI1516,
		.di_nchan	= 8,
		.do_nchan	= 8,
		.has_wdog	= 1,
	}, {
		.name		= "apci2016",
		.device		= PCI_DEVICE_ID_APCI2016,
		.do_nchan	= 16,
		.has_wdog	= 1,
	},
};

struct apci1516_private {
	unsigned long wdog_iobase;
	unsigned int ctrl;
};

static int apci1516_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	data[1] = inw(dev->iobase + APCI1516_DI_REG);

	return insn->n;
}

static int apci1516_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	s->state = inw(dev->iobase + APCI1516_DO_REG);
	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		outw(s->state, dev->iobase + APCI1516_DO_REG);
	}

	data[1] = s->state;

	return insn->n;
}

/*
 * The watchdog subdevice is configured with two INSN_CONFIG instructions:
 *
 * Enable the watchdog and set the reload timeout:
 *	data[0] = INSN_CONFIG_ARM
 *	data[1] = timeout reload value
 *
 * Disable the watchdog:
 *	data[0] = INSN_CONFIG_DISARM
 */
static int apci1516_wdog_insn_config(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct apci1516_private *devpriv = dev->private;
	unsigned int reload;

	switch (data[0]) {
	case INSN_CONFIG_ARM:
		devpriv->ctrl = APCI1516_WDOG_CTRL_ENABLE;
		reload = data[1] & s->maxdata;
		outw(reload, devpriv->wdog_iobase + APCI1516_WDOG_RELOAD_REG);

		/* Time base is 20ms, let the user know the timeout */
		dev_info(dev->class_dev, "watchdog enabled, timeout:%dms\n",
			20 * reload + 20);
		break;
	case INSN_CONFIG_DISARM:
		devpriv->ctrl = 0;
		break;
	default:
		return -EINVAL;
	}

	outw(devpriv->ctrl, devpriv->wdog_iobase + APCI1516_WDOG_CTRL_REG);

	return insn->n;
}

static int apci1516_wdog_insn_write(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct apci1516_private *devpriv = dev->private;
	int i;

	if (devpriv->ctrl == 0) {
		dev_warn(dev->class_dev, "watchdog is disabled\n");
		return -EINVAL;
	}

	/* "ping" the watchdog */
	for (i = 0; i < insn->n; i++) {
		outw(devpriv->ctrl | APCI1516_WDOG_CTRL_SW_TRIG,
			devpriv->wdog_iobase + APCI1516_WDOG_CTRL_REG);
	}

	return insn->n;
}

static int apci1516_wdog_insn_read(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct apci1516_private *devpriv = dev->private;
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = inw(devpriv->wdog_iobase + APCI1516_WDOG_STATUS_REG);

	return insn->n;
}

static int apci1516_reset(struct comedi_device *dev)
{
	const struct apci1516_boardinfo *this_board = comedi_board(dev);
	struct apci1516_private *devpriv = dev->private;

	if (!this_board->has_wdog)
		return 0;

	outw(0x0, dev->iobase + APCI1516_DO_REG);
	outw(0x0, devpriv->wdog_iobase + APCI1516_WDOG_CTRL_REG);
	outw(0x0, devpriv->wdog_iobase + APCI1516_WDOG_RELOAD_REG);

	return 0;
}

static const void *apci1516_find_boardinfo(struct comedi_device *dev,
					   struct pci_dev *pcidev)
{
	const struct apci1516_boardinfo *this_board;
	int i;

	for (i = 0; i < dev->driver->num_names; i++) {
		this_board = &apci1516_boardtypes[i];
		if (this_board->device == pcidev->device)
			return this_board;
	}
	return NULL;
}

static int apci1516_auto_attach(struct comedi_device *dev,
					  unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct apci1516_boardinfo *this_board;
	struct apci1516_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	this_board = apci1516_find_boardinfo(dev, pcidev);
	if (!this_board)
		return -ENODEV;
	dev->board_ptr = this_board;
	dev->board_name = this_board->name;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;

	dev->iobase = pci_resource_start(pcidev, 1);
	devpriv->wdog_iobase = pci_resource_start(pcidev, 2);

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	/* Initialize the digital input subdevice */
	s = &dev->subdevices[0];
	if (this_board->di_nchan) {
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= this_board->di_nchan;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= apci1516_di_insn_bits;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* Initialize the digital output subdevice */
	s = &dev->subdevices[1];
	if (this_board->do_nchan) {
		s->type		= COMEDI_SUBD_DO;
		s->subdev_flags	= SDF_WRITEABLE;
		s->n_chan	= this_board->do_nchan;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= apci1516_do_insn_bits;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* Initialize the watchdog subdevice */
	s = &dev->subdevices[2];
	if (this_board->has_wdog) {
		s->type		= COMEDI_SUBD_TIMER;
		s->subdev_flags	= SDF_WRITEABLE;
		s->n_chan	= 1;
		s->maxdata	= 0xff;
		s->insn_write	= apci1516_wdog_insn_write;
		s->insn_read	= apci1516_wdog_insn_read;
		s->insn_config	= apci1516_wdog_insn_config;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	apci1516_reset(dev);
	return 0;
}

static void apci1516_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (dev->iobase) {
		apci1516_reset(dev);
		comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver apci1516_driver = {
	.driver_name	= "addi_apci_1516",
	.module		= THIS_MODULE,
	.auto_attach	= apci1516_auto_attach,
	.detach		= apci1516_detach,
};

static int apci1516_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci1516_driver);
}

static void apci1516_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci1516_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, PCI_DEVICE_ID_APCI1016) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, PCI_DEVICE_ID_APCI1516) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, PCI_DEVICE_ID_APCI2016) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci1516_pci_table);

static struct pci_driver apci1516_pci_driver = {
	.name		= "addi_apci_1516",
	.id_table	= apci1516_pci_table,
	.probe		= apci1516_pci_probe,
	.remove		= apci1516_pci_remove,
};
module_comedi_pci_driver(apci1516_driver, apci1516_pci_driver);

MODULE_DESCRIPTION("ADDI-DATA APCI-1016/1516/2016, 16 channel DIO boards");
MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_LICENSE("GPL");
