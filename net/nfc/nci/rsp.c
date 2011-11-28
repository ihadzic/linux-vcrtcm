/*
 *  The NFC Controller Interface is the communication protocol between an
 *  NFC Controller (NFCC) and a Device Host (DH).
 *
 *  Copyright (C) 2011 Texas Instruments, Inc.
 *
 *  Written by Ilan Elias <ilane@ti.com>
 *
 *  Acknowledgements:
 *  This file is based on hci_event.c, which was written
 *  by Maxim Krasnyansky.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/skbuff.h>

#include "../nfc.h"
#include <net/nfc/nci.h>
#include <net/nfc/nci_core.h>

/* Handle NCI Response packets */

static void nci_core_reset_rsp_packet(struct nci_dev *ndev, struct sk_buff *skb)
{
	struct nci_core_reset_rsp *rsp = (void *) skb->data;

	nfc_dbg("entry, status 0x%x", rsp->status);

	if (rsp->status == NCI_STATUS_OK)
		ndev->nci_ver = rsp->nci_ver;

	nfc_dbg("nci_ver 0x%x", ndev->nci_ver);

	nci_req_complete(ndev, rsp->status);
}

static void nci_core_init_rsp_packet(struct nci_dev *ndev, struct sk_buff *skb)
{
	struct nci_core_init_rsp_1 *rsp_1 = (void *) skb->data;
	struct nci_core_init_rsp_2 *rsp_2;

	nfc_dbg("entry, status 0x%x", rsp_1->status);

	if (rsp_1->status != NCI_STATUS_OK)
		return;

	ndev->nfcc_features = __le32_to_cpu(rsp_1->nfcc_features);
	ndev->num_supported_rf_interfaces = rsp_1->num_supported_rf_interfaces;

	if (ndev->num_supported_rf_interfaces >
		NCI_MAX_SUPPORTED_RF_INTERFACES) {
		ndev->num_supported_rf_interfaces =
			NCI_MAX_SUPPORTED_RF_INTERFACES;
	}

	memcpy(ndev->supported_rf_interfaces,
		rsp_1->supported_rf_interfaces,
		ndev->num_supported_rf_interfaces);

	rsp_2 = (void *) (skb->data + 6 + ndev->num_supported_rf_interfaces);

	ndev->max_logical_connections =
		rsp_2->max_logical_connections;
	ndev->max_routing_table_size =
		__le16_to_cpu(rsp_2->max_routing_table_size);
	ndev->max_control_packet_payload_length =
		rsp_2->max_control_packet_payload_length;
	ndev->rf_sending_buffer_size =
		__le16_to_cpu(rsp_2->rf_sending_buffer_size);
	ndev->rf_receiving_buffer_size =
		__le16_to_cpu(rsp_2->rf_receiving_buffer_size);
	ndev->manufacturer_id =
		__le16_to_cpu(rsp_2->manufacturer_id);

	nfc_dbg("nfcc_features 0x%x",
		ndev->nfcc_features);
	nfc_dbg("num_supported_rf_interfaces %d",
		ndev->num_supported_rf_interfaces);
	nfc_dbg("supported_rf_interfaces[0] 0x%x",
		ndev->supported_rf_interfaces[0]);
	nfc_dbg("supported_rf_interfaces[1] 0x%x",
		ndev->supported_rf_interfaces[1]);
	nfc_dbg("supported_rf_interfaces[2] 0x%x",
		ndev->supported_rf_interfaces[2]);
	nfc_dbg("supported_rf_interfaces[3] 0x%x",
		ndev->supported_rf_interfaces[3]);
	nfc_dbg("max_logical_connections %d",
		ndev->max_logical_connections);
	nfc_dbg("max_routing_table_size %d",
		ndev->max_routing_table_size);
	nfc_dbg("max_control_packet_payload_length %d",
		ndev->max_control_packet_payload_length);
	nfc_dbg("rf_sending_buffer_size %d",
		ndev->rf_sending_buffer_size);
	nfc_dbg("rf_receiving_buffer_size %d",
		ndev->rf_receiving_buffer_size);
	nfc_dbg("manufacturer_id 0x%x",
		ndev->manufacturer_id);

	nci_req_complete(ndev, rsp_1->status);
}

static void nci_core_conn_create_rsp_packet(struct nci_dev *ndev,
						struct sk_buff *skb)
{
	struct nci_core_conn_create_rsp *rsp = (void *) skb->data;

	nfc_dbg("entry, status 0x%x", rsp->status);

	if (rsp->status != NCI_STATUS_OK)
		return;

	ndev->max_pkt_payload_size = rsp->max_pkt_payload_size;
	ndev->initial_num_credits = rsp->initial_num_credits;
	ndev->conn_id = rsp->conn_id;

	atomic_set(&ndev->credits_cnt, ndev->initial_num_credits);

	nfc_dbg("max_pkt_payload_size %d", ndev->max_pkt_payload_size);
	nfc_dbg("initial_num_credits %d", ndev->initial_num_credits);
	nfc_dbg("conn_id %d", ndev->conn_id);
}

static void nci_rf_disc_map_rsp_packet(struct nci_dev *ndev,
					struct sk_buff *skb)
{
	__u8 status = skb->data[0];

	nfc_dbg("entry, status 0x%x", status);

	nci_req_complete(ndev, status);
}

static void nci_rf_disc_rsp_packet(struct nci_dev *ndev, struct sk_buff *skb)
{
	__u8 status = skb->data[0];

	nfc_dbg("entry, status 0x%x", status);

	if (status == NCI_STATUS_OK)
		set_bit(NCI_DISCOVERY, &ndev->flags);

	nci_req_complete(ndev, status);
}

static void nci_rf_deactivate_rsp_packet(struct nci_dev *ndev,
					struct sk_buff *skb)
{
	__u8 status = skb->data[0];

	nfc_dbg("entry, status 0x%x", status);

	clear_bit(NCI_DISCOVERY, &ndev->flags);

	nci_req_complete(ndev, status);
}

void nci_rsp_packet(struct nci_dev *ndev, struct sk_buff *skb)
{
	__u16 rsp_opcode = nci_opcode(skb->data);

	/* we got a rsp, stop the cmd timer */
	del_timer(&ndev->cmd_timer);

	nfc_dbg("NCI RX: MT=rsp, PBF=%d, GID=0x%x, OID=0x%x, plen=%d",
			nci_pbf(skb->data),
			nci_opcode_gid(rsp_opcode),
			nci_opcode_oid(rsp_opcode),
			nci_plen(skb->data));

	/* strip the nci control header */
	skb_pull(skb, NCI_CTRL_HDR_SIZE);

	switch (rsp_opcode) {
	case NCI_OP_CORE_RESET_RSP:
		nci_core_reset_rsp_packet(ndev, skb);
		break;

	case NCI_OP_CORE_INIT_RSP:
		nci_core_init_rsp_packet(ndev, skb);
		break;

	case NCI_OP_CORE_CONN_CREATE_RSP:
		nci_core_conn_create_rsp_packet(ndev, skb);
		break;

	case NCI_OP_RF_DISCOVER_MAP_RSP:
		nci_rf_disc_map_rsp_packet(ndev, skb);
		break;

	case NCI_OP_RF_DISCOVER_RSP:
		nci_rf_disc_rsp_packet(ndev, skb);
		break;

	case NCI_OP_RF_DEACTIVATE_RSP:
		nci_rf_deactivate_rsp_packet(ndev, skb);
		break;

	default:
		nfc_err("unknown rsp opcode 0x%x", rsp_opcode);
		break;
	}

	kfree_skb(skb);

	/* trigger the next cmd */
	atomic_set(&ndev->cmd_cnt, 1);
	if (!skb_queue_empty(&ndev->cmd_q))
		queue_work(ndev->cmd_wq, &ndev->cmd_work);
}
