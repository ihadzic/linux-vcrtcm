
/*
 * Copyright (c) 2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/mmc/sdio_func.h>
#include "core.h"
#include "cfg80211.h"
#include "target.h"
#include "debug.h"
#include "hif-ops.h"

unsigned int debug_mask;
static unsigned int testmode;

module_param(debug_mask, uint, 0644);
module_param(testmode, uint, 0644);

/*
 * Include definitions here that can be used to tune the WLAN module
 * behavior. Different customers can tune the behavior as per their needs,
 * here.
 */

/*
 * This configuration item enable/disable keepalive support.
 * Keepalive support: In the absence of any data traffic to AP, null
 * frames will be sent to the AP at periodic interval, to keep the association
 * active. This configuration item defines the periodic interval.
 * Use value of zero to disable keepalive support
 * Default: 60 seconds
 */
#define WLAN_CONFIG_KEEP_ALIVE_INTERVAL 60

/*
 * This configuration item sets the value of disconnect timeout
 * Firmware delays sending the disconnec event to the host for this
 * timeout after is gets disconnected from the current AP.
 * If the firmware successly roams within the disconnect timeout
 * it sends a new connect event
 */
#define WLAN_CONFIG_DISCONNECT_TIMEOUT 10

#define CONFIG_AR600x_DEBUG_UART_TX_PIN 8

#define ATH6KL_DATA_OFFSET    64
struct sk_buff *ath6kl_buf_alloc(int size)
{
	struct sk_buff *skb;
	u16 reserved;

	/* Add chacheline space at front and back of buffer */
	reserved = (2 * L1_CACHE_BYTES) + ATH6KL_DATA_OFFSET +
		   sizeof(struct htc_packet) + ATH6KL_HTC_ALIGN_BYTES;
	skb = dev_alloc_skb(size + reserved);

	if (skb)
		skb_reserve(skb, reserved - L1_CACHE_BYTES);
	return skb;
}

void ath6kl_init_profile_info(struct ath6kl *ar)
{
	ar->ssid_len = 0;
	memset(ar->ssid, 0, sizeof(ar->ssid));

	ar->dot11_auth_mode = OPEN_AUTH;
	ar->auth_mode = NONE_AUTH;
	ar->prwise_crypto = NONE_CRYPT;
	ar->prwise_crypto_len = 0;
	ar->grp_crypto = NONE_CRYPT;
	ar->grp_crypto_len = 0;
	memset(ar->wep_key_list, 0, sizeof(ar->wep_key_list));
	memset(ar->req_bssid, 0, sizeof(ar->req_bssid));
	memset(ar->bssid, 0, sizeof(ar->bssid));
	ar->bss_ch = 0;
	ar->nw_type = ar->next_mode = INFRA_NETWORK;
}

static u8 ath6kl_get_fw_iftype(struct ath6kl *ar)
{
	switch (ar->nw_type) {
	case INFRA_NETWORK:
		return HI_OPTION_FW_MODE_BSS_STA;
	case ADHOC_NETWORK:
		return HI_OPTION_FW_MODE_IBSS;
	case AP_NETWORK:
		return HI_OPTION_FW_MODE_AP;
	default:
		ath6kl_err("Unsupported interface type :%d\n", ar->nw_type);
		return 0xff;
	}
}

static int ath6kl_set_host_app_area(struct ath6kl *ar)
{
	u32 address, data;
	struct host_app_area host_app_area;

	/* Fetch the address of the host_app_area_s
	 * instance in the host interest area */
	address = ath6kl_get_hi_item_addr(ar, HI_ITEM(hi_app_host_interest));
	address = TARG_VTOP(ar->target_type, address);

	if (ath6kl_diag_read32(ar, address, &data))
		return -EIO;

	address = TARG_VTOP(ar->target_type, data);
	host_app_area.wmi_protocol_ver = WMI_PROTOCOL_VERSION;
	if (ath6kl_diag_write(ar, address, (u8 *) &host_app_area,
			      sizeof(struct host_app_area)))
		return -EIO;

	return 0;
}

static inline void set_ac2_ep_map(struct ath6kl *ar,
				  u8 ac,
				  enum htc_endpoint_id ep)
{
	ar->ac2ep_map[ac] = ep;
	ar->ep2ac_map[ep] = ac;
}

/* connect to a service */
static int ath6kl_connectservice(struct ath6kl *ar,
				 struct htc_service_connect_req  *con_req,
				 char *desc)
{
	int status;
	struct htc_service_connect_resp response;

	memset(&response, 0, sizeof(response));

	status = ath6kl_htc_conn_service(ar->htc_target, con_req, &response);
	if (status) {
		ath6kl_err("failed to connect to %s service status:%d\n",
			   desc, status);
		return status;
	}

	switch (con_req->svc_id) {
	case WMI_CONTROL_SVC:
		if (test_bit(WMI_ENABLED, &ar->flag))
			ath6kl_wmi_set_control_ep(ar->wmi, response.endpoint);
		ar->ctrl_ep = response.endpoint;
		break;
	case WMI_DATA_BE_SVC:
		set_ac2_ep_map(ar, WMM_AC_BE, response.endpoint);
		break;
	case WMI_DATA_BK_SVC:
		set_ac2_ep_map(ar, WMM_AC_BK, response.endpoint);
		break;
	case WMI_DATA_VI_SVC:
		set_ac2_ep_map(ar, WMM_AC_VI, response.endpoint);
		break;
	case WMI_DATA_VO_SVC:
		set_ac2_ep_map(ar, WMM_AC_VO, response.endpoint);
		break;
	default:
		ath6kl_err("service id is not mapped %d\n", con_req->svc_id);
		return -EINVAL;
	}

	return 0;
}

static int ath6kl_init_service_ep(struct ath6kl *ar)
{
	struct htc_service_connect_req connect;

	memset(&connect, 0, sizeof(connect));

	/* these fields are the same for all service endpoints */
	connect.ep_cb.rx = ath6kl_rx;
	connect.ep_cb.rx_refill = ath6kl_rx_refill;
	connect.ep_cb.tx_full = ath6kl_tx_queue_full;

	/*
	 * Set the max queue depth so that our ath6kl_tx_queue_full handler
	 * gets called.
	*/
	connect.max_txq_depth = MAX_DEFAULT_SEND_QUEUE_DEPTH;
	connect.ep_cb.rx_refill_thresh = ATH6KL_MAX_RX_BUFFERS / 4;
	if (!connect.ep_cb.rx_refill_thresh)
		connect.ep_cb.rx_refill_thresh++;

	/* connect to control service */
	connect.svc_id = WMI_CONTROL_SVC;
	if (ath6kl_connectservice(ar, &connect, "WMI CONTROL"))
		return -EIO;

	connect.flags |= HTC_FLGS_TX_BNDL_PAD_EN;

	/*
	 * Limit the HTC message size on the send path, although e can
	 * receive A-MSDU frames of 4K, we will only send ethernet-sized
	 * (802.3) frames on the send path.
	 */
	connect.max_rxmsg_sz = WMI_MAX_TX_DATA_FRAME_LENGTH;

	/*
	 * To reduce the amount of committed memory for larger A_MSDU
	 * frames, use the recv-alloc threshold mechanism for larger
	 * packets.
	 */
	connect.ep_cb.rx_alloc_thresh = ATH6KL_BUFFER_SIZE;
	connect.ep_cb.rx_allocthresh = ath6kl_alloc_amsdu_rxbuf;

	/*
	 * For the remaining data services set the connection flag to
	 * reduce dribbling, if configured to do so.
	 */
	connect.conn_flags |= HTC_CONN_FLGS_REDUCE_CRED_DRIB;
	connect.conn_flags &= ~HTC_CONN_FLGS_THRESH_MASK;
	connect.conn_flags |= HTC_CONN_FLGS_THRESH_LVL_HALF;

	connect.svc_id = WMI_DATA_BE_SVC;

	if (ath6kl_connectservice(ar, &connect, "WMI DATA BE"))
		return -EIO;

	/* connect to back-ground map this to WMI LOW_PRI */
	connect.svc_id = WMI_DATA_BK_SVC;
	if (ath6kl_connectservice(ar, &connect, "WMI DATA BK"))
		return -EIO;

	/* connect to Video service, map this to to HI PRI */
	connect.svc_id = WMI_DATA_VI_SVC;
	if (ath6kl_connectservice(ar, &connect, "WMI DATA VI"))
		return -EIO;

	/*
	 * Connect to VO service, this is currently not mapped to a WMI
	 * priority stream due to historical reasons. WMI originally
	 * defined 3 priorities over 3 mailboxes We can change this when
	 * WMI is reworked so that priorities are not dependent on
	 * mailboxes.
	 */
	connect.svc_id = WMI_DATA_VO_SVC;
	if (ath6kl_connectservice(ar, &connect, "WMI DATA VO"))
		return -EIO;

	return 0;
}

static void ath6kl_init_control_info(struct ath6kl *ar)
{
	u8 ctr;

	clear_bit(WMI_ENABLED, &ar->flag);
	ath6kl_init_profile_info(ar);
	ar->def_txkey_index = 0;
	memset(ar->wep_key_list, 0, sizeof(ar->wep_key_list));
	ar->ch_hint = 0;
	ar->listen_intvl_t = A_DEFAULT_LISTEN_INTERVAL;
	ar->listen_intvl_b = 0;
	ar->tx_pwr = 0;
	clear_bit(SKIP_SCAN, &ar->flag);
	set_bit(WMM_ENABLED, &ar->flag);
	ar->intra_bss = 1;
	memset(&ar->sc_params, 0, sizeof(ar->sc_params));
	ar->sc_params.short_scan_ratio = WMI_SHORTSCANRATIO_DEFAULT;
	ar->sc_params.scan_ctrl_flags = DEFAULT_SCAN_CTRL_FLAGS;
	ar->lrssi_roam_threshold = DEF_LRSSI_ROAM_THRESHOLD;

	memset((u8 *)ar->sta_list, 0,
	       AP_MAX_NUM_STA * sizeof(struct ath6kl_sta));

	spin_lock_init(&ar->mcastpsq_lock);

	/* Init the PS queues */
	for (ctr = 0; ctr < AP_MAX_NUM_STA; ctr++) {
		spin_lock_init(&ar->sta_list[ctr].psq_lock);
		skb_queue_head_init(&ar->sta_list[ctr].psq);
	}

	skb_queue_head_init(&ar->mcastpsq);

	memcpy(ar->ap_country_code, DEF_AP_COUNTRY_CODE, 3);
}

/*
 * Set HTC/Mbox operational parameters, this can only be called when the
 * target is in the BMI phase.
 */
static int ath6kl_set_htc_params(struct ath6kl *ar, u32 mbox_isr_yield_val,
				 u8 htc_ctrl_buf)
{
	int status;
	u32 blk_size;

	blk_size = ar->mbox_info.block_size;

	if (htc_ctrl_buf)
		blk_size |=  ((u32)htc_ctrl_buf) << 16;

	/* set the host interest area for the block size */
	status = ath6kl_bmi_write(ar,
			ath6kl_get_hi_item_addr(ar,
			HI_ITEM(hi_mbox_io_block_sz)),
			(u8 *)&blk_size,
			4);
	if (status) {
		ath6kl_err("bmi_write_memory for IO block size failed\n");
		goto out;
	}

	ath6kl_dbg(ATH6KL_DBG_TRC, "block size set: %d (target addr:0x%X)\n",
		   blk_size,
		   ath6kl_get_hi_item_addr(ar, HI_ITEM(hi_mbox_io_block_sz)));

	if (mbox_isr_yield_val) {
		/* set the host interest area for the mbox ISR yield limit */
		status = ath6kl_bmi_write(ar,
				ath6kl_get_hi_item_addr(ar,
				HI_ITEM(hi_mbox_isr_yield_limit)),
				(u8 *)&mbox_isr_yield_val,
				4);
		if (status) {
			ath6kl_err("bmi_write_memory for yield limit failed\n");
			goto out;
		}
	}

out:
	return status;
}

#define REG_DUMP_COUNT_AR6003   60
#define REGISTER_DUMP_LEN_MAX   60

static void ath6kl_dump_target_assert_info(struct ath6kl *ar)
{
	u32 address;
	u32 regdump_loc = 0;
	int status;
	u32 regdump_val[REGISTER_DUMP_LEN_MAX];
	u32 i;

	if (ar->target_type != TARGET_TYPE_AR6003)
		return;

	/* the reg dump pointer is copied to the host interest area */
	address = ath6kl_get_hi_item_addr(ar, HI_ITEM(hi_failure_state));
	address = TARG_VTOP(ar->target_type, address);

	/* read RAM location through diagnostic window */
	status = ath6kl_diag_read32(ar, address, &regdump_loc);

	if (status || !regdump_loc) {
		ath6kl_err("failed to get ptr to register dump area\n");
		return;
	}

	ath6kl_dbg(ATH6KL_DBG_TRC, "location of register dump data: 0x%X\n",
		regdump_loc);
	regdump_loc = TARG_VTOP(ar->target_type, regdump_loc);

	/* fetch register dump data */
	status = ath6kl_diag_read(ar, regdump_loc, (u8 *)&regdump_val[0],
				  REG_DUMP_COUNT_AR6003 * (sizeof(u32)));

	if (status) {
		ath6kl_err("failed to get register dump\n");
		return;
	}
	ath6kl_dbg(ATH6KL_DBG_TRC, "Register Dump:\n");

	for (i = 0; i < REG_DUMP_COUNT_AR6003; i++)
		ath6kl_dbg(ATH6KL_DBG_TRC, " %d :  0x%8.8X\n",
			   i, regdump_val[i]);

}

void ath6kl_target_failure(struct ath6kl *ar)
{
	ath6kl_err("target asserted\n");

	/* try dumping target assertion information (if any) */
	ath6kl_dump_target_assert_info(ar);

}

static int ath6kl_target_config_wlan_params(struct ath6kl *ar)
{
	int status = 0;
	int ret;

	/*
	 * Configure the device for rx dot11 header rules. "0,0" are the
	 * default values. Required if checksum offload is needed. Set
	 * RxMetaVersion to 2.
	 */
	if (ath6kl_wmi_set_rx_frame_format_cmd(ar->wmi,
					       ar->rx_meta_ver, 0, 0)) {
		ath6kl_err("unable to set the rx frame format\n");
		status = -EIO;
	}

	if (ar->conf_flags & ATH6KL_CONF_IGNORE_PS_FAIL_EVT_IN_SCAN)
		if ((ath6kl_wmi_pmparams_cmd(ar->wmi, 0, 1, 0, 0, 1,
		     IGNORE_POWER_SAVE_FAIL_EVENT_DURING_SCAN)) != 0) {
			ath6kl_err("unable to set power save fail event policy\n");
			status = -EIO;
		}

	if (!(ar->conf_flags & ATH6KL_CONF_IGNORE_ERP_BARKER))
		if ((ath6kl_wmi_set_lpreamble_cmd(ar->wmi, 0,
		     WMI_DONOT_IGNORE_BARKER_IN_ERP)) != 0) {
			ath6kl_err("unable to set barker preamble policy\n");
			status = -EIO;
		}

	if (ath6kl_wmi_set_keepalive_cmd(ar->wmi,
			WLAN_CONFIG_KEEP_ALIVE_INTERVAL)) {
		ath6kl_err("unable to set keep alive interval\n");
		status = -EIO;
	}

	if (ath6kl_wmi_disctimeout_cmd(ar->wmi,
			WLAN_CONFIG_DISCONNECT_TIMEOUT)) {
		ath6kl_err("unable to set disconnect timeout\n");
		status = -EIO;
	}

	if (!(ar->conf_flags & ATH6KL_CONF_ENABLE_TX_BURST))
		if (ath6kl_wmi_set_wmm_txop(ar->wmi, WMI_TXOP_DISABLED)) {
			ath6kl_err("unable to set txop bursting\n");
			status = -EIO;
		}

	if (ar->p2p) {
		ret = ath6kl_wmi_info_req_cmd(ar->wmi,
					      P2P_FLAG_CAPABILITIES_REQ |
					      P2P_FLAG_MACADDR_REQ |
					      P2P_FLAG_HMODEL_REQ);
		if (ret) {
			ath6kl_dbg(ATH6KL_DBG_TRC, "failed to request P2P "
				   "capabilities (%d) - assuming P2P not "
				   "supported\n", ret);
			ar->p2p = 0;
		}
	}

	if (ar->p2p) {
		/* Enable Probe Request reporting for P2P */
		ret = ath6kl_wmi_probe_report_req_cmd(ar->wmi, true);
		if (ret) {
			ath6kl_dbg(ATH6KL_DBG_TRC, "failed to enable Probe "
				   "Request reporting (%d)\n", ret);
		}
	}

	return status;
}

int ath6kl_configure_target(struct ath6kl *ar)
{
	u32 param, ram_reserved_size;
	u8 fw_iftype;

	fw_iftype = ath6kl_get_fw_iftype(ar);
	if (fw_iftype == 0xff)
		return -EINVAL;

	/* Tell target which HTC version it is used*/
	param = HTC_PROTOCOL_VERSION;
	if (ath6kl_bmi_write(ar,
			     ath6kl_get_hi_item_addr(ar,
			     HI_ITEM(hi_app_host_interest)),
			     (u8 *)&param, 4) != 0) {
		ath6kl_err("bmi_write_memory for htc version failed\n");
		return -EIO;
	}

	/* set the firmware mode to STA/IBSS/AP */
	param = 0;

	if (ath6kl_bmi_read(ar,
			    ath6kl_get_hi_item_addr(ar,
			    HI_ITEM(hi_option_flag)),
			    (u8 *)&param, 4) != 0) {
		ath6kl_err("bmi_read_memory for setting fwmode failed\n");
		return -EIO;
	}

	param |= (1 << HI_OPTION_NUM_DEV_SHIFT);
	param |= (fw_iftype << HI_OPTION_FW_MODE_SHIFT);
	if (ar->p2p && fw_iftype == HI_OPTION_FW_MODE_BSS_STA) {
		param |= HI_OPTION_FW_SUBMODE_P2PDEV <<
			HI_OPTION_FW_SUBMODE_SHIFT;
	}
	param |= (0 << HI_OPTION_MAC_ADDR_METHOD_SHIFT);
	param |= (0 << HI_OPTION_FW_BRIDGE_SHIFT);

	if (ath6kl_bmi_write(ar,
			     ath6kl_get_hi_item_addr(ar,
			     HI_ITEM(hi_option_flag)),
			     (u8 *)&param,
			     4) != 0) {
		ath6kl_err("bmi_write_memory for setting fwmode failed\n");
		return -EIO;
	}

	ath6kl_dbg(ATH6KL_DBG_TRC, "firmware mode set\n");

	/*
	 * Hardcode the address use for the extended board data
	 * Ideally this should be pre-allocate by the OS at boot time
	 * But since it is a new feature and board data is loaded
	 * at init time, we have to workaround this from host.
	 * It is difficult to patch the firmware boot code,
	 * but possible in theory.
	 */

	param = ar->hw.board_ext_data_addr;
	ram_reserved_size = ar->hw.reserved_ram_size;

	if (ath6kl_bmi_write(ar, ath6kl_get_hi_item_addr(ar,
					HI_ITEM(hi_board_ext_data)),
			     (u8 *)&param, 4) != 0) {
		ath6kl_err("bmi_write_memory for hi_board_ext_data failed\n");
		return -EIO;
	}

	if (ath6kl_bmi_write(ar, ath6kl_get_hi_item_addr(ar,
					HI_ITEM(hi_end_ram_reserve_sz)),
			     (u8 *)&ram_reserved_size, 4) != 0) {
		ath6kl_err("bmi_write_memory for hi_end_ram_reserve_sz failed\n");
		return -EIO;
	}

	/* set the block size for the target */
	if (ath6kl_set_htc_params(ar, MBOX_YIELD_LIMIT, 0))
		/* use default number of control buffers */
		return -EIO;

	return 0;
}

struct ath6kl *ath6kl_core_alloc(struct device *sdev)
{
	struct net_device *dev;
	struct ath6kl *ar;
	struct wireless_dev *wdev;

	wdev = ath6kl_cfg80211_init(sdev);
	if (!wdev) {
		ath6kl_err("ath6kl_cfg80211_init failed\n");
		return NULL;
	}

	ar = wdev_priv(wdev);
	ar->dev = sdev;
	ar->wdev = wdev;
	wdev->iftype = NL80211_IFTYPE_STATION;

	if (ath6kl_debug_init(ar)) {
		ath6kl_err("Failed to initialize debugfs\n");
		ath6kl_cfg80211_deinit(ar);
		return NULL;
	}

	dev = alloc_netdev(0, "wlan%d", ether_setup);
	if (!dev) {
		ath6kl_err("no memory for network device instance\n");
		ath6kl_cfg80211_deinit(ar);
		return NULL;
	}

	dev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(dev, wiphy_dev(wdev->wiphy));
	wdev->netdev = dev;
	ar->sme_state = SME_DISCONNECTED;

	init_netdev(dev);

	ar->net_dev = dev;
	set_bit(WLAN_ENABLED, &ar->flag);

	ar->wlan_pwr_state = WLAN_POWER_STATE_ON;

	spin_lock_init(&ar->lock);

	ath6kl_init_control_info(ar);
	init_waitqueue_head(&ar->event_wq);
	sema_init(&ar->sem, 1);
	clear_bit(DESTROY_IN_PROGRESS, &ar->flag);

	INIT_LIST_HEAD(&ar->amsdu_rx_buffer_queue);

	setup_timer(&ar->disconnect_timer, disconnect_timer_handler,
		    (unsigned long) dev);

	return ar;
}

int ath6kl_unavail_ev(struct ath6kl *ar)
{
	ath6kl_destroy(ar->net_dev, 1);

	return 0;
}

/* firmware upload */
static int ath6kl_get_fw(struct ath6kl *ar, const char *filename,
			 u8 **fw, size_t *fw_len)
{
	const struct firmware *fw_entry;
	int ret;

	ret = request_firmware(&fw_entry, filename, ar->dev);
	if (ret)
		return ret;

	*fw_len = fw_entry->size;
	*fw = kmemdup(fw_entry->data, fw_entry->size, GFP_KERNEL);

	if (*fw == NULL)
		ret = -ENOMEM;

	release_firmware(fw_entry);

	return ret;
}

#ifdef CONFIG_OF
static const char *get_target_ver_dir(const struct ath6kl *ar)
{
	switch (ar->version.target_ver) {
	case AR6003_REV1_VERSION:
		return "ath6k/AR6003/hw1.0";
	case AR6003_REV2_VERSION:
		return "ath6k/AR6003/hw2.0";
	case AR6003_REV3_VERSION:
		return "ath6k/AR6003/hw2.1.1";
	}
	ath6kl_warn("%s: unsupported target version 0x%x.\n", __func__,
		    ar->version.target_ver);
	return NULL;
}

/*
 * Check the device tree for a board-id and use it to construct
 * the pathname to the firmware file.  Used (for now) to find a
 * fallback to the "bdata.bin" file--typically a symlink to the
 * appropriate board-specific file.
 */
static bool check_device_tree(struct ath6kl *ar)
{
	static const char *board_id_prop = "atheros,board-id";
	struct device_node *node;
	char board_filename[64];
	const char *board_id;
	int ret;

	for_each_compatible_node(node, NULL, "atheros,ath6kl") {
		board_id = of_get_property(node, board_id_prop, NULL);
		if (board_id == NULL) {
			ath6kl_warn("No \"%s\" property on %s node.\n",
				    board_id_prop, node->name);
			continue;
		}
		snprintf(board_filename, sizeof(board_filename),
			 "%s/bdata.%s.bin", get_target_ver_dir(ar), board_id);

		ret = ath6kl_get_fw(ar, board_filename, &ar->fw_board,
				    &ar->fw_board_len);
		if (ret) {
			ath6kl_err("Failed to get DT board file %s: %d\n",
				   board_filename, ret);
			continue;
		}
		return true;
	}
	return false;
}
#else
static bool check_device_tree(struct ath6kl *ar)
{
	return false;
}
#endif /* CONFIG_OF */

static int ath6kl_fetch_board_file(struct ath6kl *ar)
{
	const char *filename;
	int ret;

	if (ar->fw_board != NULL)
		return 0;

	switch (ar->version.target_ver) {
	case AR6003_REV2_VERSION:
		filename = AR6003_REV2_BOARD_DATA_FILE;
		break;
	case AR6004_REV1_VERSION:
		filename = AR6004_REV1_BOARD_DATA_FILE;
		break;
	default:
		filename = AR6003_REV3_BOARD_DATA_FILE;
		break;
	}

	ret = ath6kl_get_fw(ar, filename, &ar->fw_board,
			    &ar->fw_board_len);
	if (ret == 0) {
		/* managed to get proper board file */
		return 0;
	}

	if (check_device_tree(ar)) {
		/* got board file from device tree */
		return 0;
	}

	/* there was no proper board file, try to use default instead */
	ath6kl_warn("Failed to get board file %s (%d), trying to find default board file.\n",
		    filename, ret);

	switch (ar->version.target_ver) {
	case AR6003_REV2_VERSION:
		filename = AR6003_REV2_DEFAULT_BOARD_DATA_FILE;
		break;
	case AR6004_REV1_VERSION:
		filename = AR6004_REV1_DEFAULT_BOARD_DATA_FILE;
		break;
	default:
		filename = AR6003_REV3_DEFAULT_BOARD_DATA_FILE;
		break;
	}

	ret = ath6kl_get_fw(ar, filename, &ar->fw_board,
			    &ar->fw_board_len);
	if (ret) {
		ath6kl_err("Failed to get default board file %s: %d\n",
			   filename, ret);
		return ret;
	}

	ath6kl_warn("WARNING! No proper board file was not found, instead using a default board file.\n");
	ath6kl_warn("Most likely your hardware won't work as specified. Install correct board file!\n");

	return 0;
}

static int ath6kl_fetch_otp_file(struct ath6kl *ar)
{
	const char *filename;
	int ret;

	if (ar->fw_otp != NULL)
		return 0;

	switch (ar->version.target_ver) {
	case AR6003_REV2_VERSION:
		filename = AR6003_REV2_OTP_FILE;
		break;
	case AR6004_REV1_VERSION:
		ath6kl_dbg(ATH6KL_DBG_TRC, "AR6004 doesn't need OTP file\n");
		return 0;
		break;
	default:
		filename = AR6003_REV3_OTP_FILE;
		break;
	}

	ret = ath6kl_get_fw(ar, filename, &ar->fw_otp,
			    &ar->fw_otp_len);
	if (ret) {
		ath6kl_err("Failed to get OTP file %s: %d\n",
			   filename, ret);
		return ret;
	}

	return 0;
}

static int ath6kl_fetch_fw_file(struct ath6kl *ar)
{
	const char *filename;
	int ret;

	if (ar->fw != NULL)
		return 0;

	if (testmode) {
		switch (ar->version.target_ver) {
		case AR6003_REV2_VERSION:
			filename = AR6003_REV2_TCMD_FIRMWARE_FILE;
			break;
		case AR6003_REV3_VERSION:
			filename = AR6003_REV3_TCMD_FIRMWARE_FILE;
			break;
		case AR6004_REV1_VERSION:
			ath6kl_warn("testmode not supported with ar6004\n");
			return -EOPNOTSUPP;
		default:
			ath6kl_warn("unknown target version: 0x%x\n",
				       ar->version.target_ver);
			return -EINVAL;
		}

		set_bit(TESTMODE, &ar->flag);

		goto get_fw;
	}

	switch (ar->version.target_ver) {
	case AR6003_REV2_VERSION:
		filename = AR6003_REV2_FIRMWARE_FILE;
		break;
	case AR6004_REV1_VERSION:
		filename = AR6004_REV1_FIRMWARE_FILE;
		break;
	default:
		filename = AR6003_REV3_FIRMWARE_FILE;
		break;
	}

get_fw:
	ret = ath6kl_get_fw(ar, filename, &ar->fw, &ar->fw_len);
	if (ret) {
		ath6kl_err("Failed to get firmware file %s: %d\n",
			   filename, ret);
		return ret;
	}

	return 0;
}

static int ath6kl_fetch_patch_file(struct ath6kl *ar)
{
	const char *filename;
	int ret;

	switch (ar->version.target_ver) {
	case AR6003_REV2_VERSION:
		filename = AR6003_REV2_PATCH_FILE;
		break;
	case AR6004_REV1_VERSION:
		/* FIXME: implement for AR6004 */
		return 0;
		break;
	default:
		filename = AR6003_REV3_PATCH_FILE;
		break;
	}

	if (ar->fw_patch == NULL) {
		ret = ath6kl_get_fw(ar, filename, &ar->fw_patch,
				    &ar->fw_patch_len);
		if (ret) {
			ath6kl_err("Failed to get patch file %s: %d\n",
				   filename, ret);
			return ret;
		}
	}

	return 0;
}

static int ath6kl_fetch_fw_api1(struct ath6kl *ar)
{
	int ret;

	ret = ath6kl_fetch_otp_file(ar);
	if (ret)
		return ret;

	ret = ath6kl_fetch_fw_file(ar);
	if (ret)
		return ret;

	ret = ath6kl_fetch_patch_file(ar);
	if (ret)
		return ret;

	return 0;
}

static int ath6kl_fetch_fw_api2(struct ath6kl *ar)
{
	size_t magic_len, len, ie_len;
	const struct firmware *fw;
	struct ath6kl_fw_ie *hdr;
	const char *filename;
	const u8 *data;
	int ret, ie_id, i, index, bit;
	__le32 *val;

	switch (ar->version.target_ver) {
	case AR6003_REV2_VERSION:
		filename = AR6003_REV2_FIRMWARE_2_FILE;
		break;
	case AR6003_REV3_VERSION:
		filename = AR6003_REV3_FIRMWARE_2_FILE;
		break;
	case AR6004_REV1_VERSION:
		filename = AR6004_REV1_FIRMWARE_2_FILE;
		break;
	default:
		return -EOPNOTSUPP;
	}

	ret = request_firmware(&fw, filename, ar->dev);
	if (ret)
		return ret;

	data = fw->data;
	len = fw->size;

	/* magic also includes the null byte, check that as well */
	magic_len = strlen(ATH6KL_FIRMWARE_MAGIC) + 1;

	if (len < magic_len) {
		ret = -EINVAL;
		goto out;
	}

	if (memcmp(data, ATH6KL_FIRMWARE_MAGIC, magic_len) != 0) {
		ret = -EINVAL;
		goto out;
	}

	len -= magic_len;
	data += magic_len;

	/* loop elements */
	while (len > sizeof(struct ath6kl_fw_ie)) {
		/* hdr is unaligned! */
		hdr = (struct ath6kl_fw_ie *) data;

		ie_id = le32_to_cpup(&hdr->id);
		ie_len = le32_to_cpup(&hdr->len);

		len -= sizeof(*hdr);
		data += sizeof(*hdr);

		if (len < ie_len) {
			ret = -EINVAL;
			goto out;
		}

		switch (ie_id) {
		case ATH6KL_FW_IE_OTP_IMAGE:
			ath6kl_dbg(ATH6KL_DBG_BOOT, "found otp image ie (%zd B)\n",
				ie_len);

			ar->fw_otp = kmemdup(data, ie_len, GFP_KERNEL);

			if (ar->fw_otp == NULL) {
				ret = -ENOMEM;
				goto out;
			}

			ar->fw_otp_len = ie_len;
			break;
		case ATH6KL_FW_IE_FW_IMAGE:
			ath6kl_dbg(ATH6KL_DBG_BOOT, "found fw image ie (%zd B)\n",
				ie_len);

			ar->fw = kmemdup(data, ie_len, GFP_KERNEL);

			if (ar->fw == NULL) {
				ret = -ENOMEM;
				goto out;
			}

			ar->fw_len = ie_len;
			break;
		case ATH6KL_FW_IE_PATCH_IMAGE:
			ath6kl_dbg(ATH6KL_DBG_BOOT, "found patch image ie (%zd B)\n",
				ie_len);

			ar->fw_patch = kmemdup(data, ie_len, GFP_KERNEL);

			if (ar->fw_patch == NULL) {
				ret = -ENOMEM;
				goto out;
			}

			ar->fw_patch_len = ie_len;
			break;
		case ATH6KL_FW_IE_RESERVED_RAM_SIZE:
			val = (__le32 *) data;
			ar->hw.reserved_ram_size = le32_to_cpup(val);

			ath6kl_dbg(ATH6KL_DBG_BOOT,
				   "found reserved ram size ie 0x%d\n",
				   ar->hw.reserved_ram_size);
			break;
		case ATH6KL_FW_IE_CAPABILITIES:
			ath6kl_dbg(ATH6KL_DBG_BOOT,
				   "found firmware capabilities ie (%zd B)\n",
				   ie_len);

			for (i = 0; i < ATH6KL_FW_CAPABILITY_MAX; i++) {
				index = ALIGN(i, 8) / 8;
				bit = i % 8;

				if (data[index] & (1 << bit))
					__set_bit(i, ar->fw_capabilities);
			}

			ath6kl_dbg_dump(ATH6KL_DBG_BOOT, "capabilities", "",
					ar->fw_capabilities,
					sizeof(ar->fw_capabilities));
			break;
		case ATH6KL_FW_IE_PATCH_ADDR:
			if (ie_len != sizeof(*val))
				break;

			val = (__le32 *) data;
			ar->hw.dataset_patch_addr = le32_to_cpup(val);

			ath6kl_dbg(ATH6KL_DBG_BOOT,
				   "found patch address ie 0x%d\n",
				   ar->hw.dataset_patch_addr);
			break;
		default:
			ath6kl_dbg(ATH6KL_DBG_BOOT, "Unknown fw ie: %u\n",
				   le32_to_cpup(&hdr->id));
			break;
		}

		len -= ie_len;
		data += ie_len;
	};

	ret = 0;
out:
	release_firmware(fw);

	return ret;
}

static int ath6kl_fetch_firmwares(struct ath6kl *ar)
{
	int ret;

	ret = ath6kl_fetch_board_file(ar);
	if (ret)
		return ret;

	ret = ath6kl_fetch_fw_api2(ar);
	if (ret == 0) {
		ath6kl_dbg(ATH6KL_DBG_BOOT, "using fw api 2\n");
		return 0;
	}

	ret = ath6kl_fetch_fw_api1(ar);
	if (ret)
		return ret;

	ath6kl_dbg(ATH6KL_DBG_BOOT, "using fw api 1\n");

	return 0;
}

static int ath6kl_upload_board_file(struct ath6kl *ar)
{
	u32 board_address, board_ext_address, param;
	u32 board_data_size, board_ext_data_size;
	int ret;

	if (WARN_ON(ar->fw_board == NULL))
		return -ENOENT;

	/*
	 * Determine where in Target RAM to write Board Data.
	 * For AR6004, host determine Target RAM address for
	 * writing board data.
	 */
	if (ar->target_type == TARGET_TYPE_AR6004) {
		board_address = AR6004_REV1_BOARD_DATA_ADDRESS;
		ath6kl_bmi_write(ar,
				ath6kl_get_hi_item_addr(ar,
				HI_ITEM(hi_board_data)),
				(u8 *) &board_address, 4);
	} else {
		ath6kl_bmi_read(ar,
				ath6kl_get_hi_item_addr(ar,
				HI_ITEM(hi_board_data)),
				(u8 *) &board_address, 4);
	}

	/* determine where in target ram to write extended board data */
	ath6kl_bmi_read(ar,
			ath6kl_get_hi_item_addr(ar,
			HI_ITEM(hi_board_ext_data)),
			(u8 *) &board_ext_address, 4);

	if (board_ext_address == 0) {
		ath6kl_err("Failed to get board file target address.\n");
		return -EINVAL;
	}

	switch (ar->target_type) {
	case TARGET_TYPE_AR6003:
		board_data_size = AR6003_BOARD_DATA_SZ;
		board_ext_data_size = AR6003_BOARD_EXT_DATA_SZ;
		break;
	case TARGET_TYPE_AR6004:
		board_data_size = AR6004_BOARD_DATA_SZ;
		board_ext_data_size = AR6004_BOARD_EXT_DATA_SZ;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
		break;
	}

	if (ar->fw_board_len == (board_data_size +
				 board_ext_data_size)) {

		/* write extended board data */
		ath6kl_dbg(ATH6KL_DBG_BOOT,
			   "writing extended board data to 0x%x (%d B)\n",
			   board_ext_address, board_ext_data_size);

		ret = ath6kl_bmi_write(ar, board_ext_address,
				       ar->fw_board + board_data_size,
				       board_ext_data_size);
		if (ret) {
			ath6kl_err("Failed to write extended board data: %d\n",
				   ret);
			return ret;
		}

		/* record that extended board data is initialized */
		param = (board_ext_data_size << 16) | 1;

		ath6kl_bmi_write(ar,
				 ath6kl_get_hi_item_addr(ar,
				 HI_ITEM(hi_board_ext_data_config)),
				 (unsigned char *) &param, 4);
	}

	if (ar->fw_board_len < board_data_size) {
		ath6kl_err("Too small board file: %zu\n", ar->fw_board_len);
		ret = -EINVAL;
		return ret;
	}

	ath6kl_dbg(ATH6KL_DBG_BOOT, "writing board file to 0x%x (%d B)\n",
		   board_address, board_data_size);

	ret = ath6kl_bmi_write(ar, board_address, ar->fw_board,
			       board_data_size);

	if (ret) {
		ath6kl_err("Board file bmi write failed: %d\n", ret);
		return ret;
	}

	/* record the fact that Board Data IS initialized */
	param = 1;
	ath6kl_bmi_write(ar,
			 ath6kl_get_hi_item_addr(ar,
			 HI_ITEM(hi_board_data_initialized)),
			 (u8 *)&param, 4);

	return ret;
}

static int ath6kl_upload_otp(struct ath6kl *ar)
{
	u32 address, param;
	int ret;

	if (WARN_ON(ar->fw_otp == NULL))
		return -ENOENT;

	address = ar->hw.app_load_addr;

	ath6kl_dbg(ATH6KL_DBG_BOOT, "writing otp to 0x%x (%zd B)\n", address,
		   ar->fw_otp_len);

	ret = ath6kl_bmi_fast_download(ar, address, ar->fw_otp,
				       ar->fw_otp_len);
	if (ret) {
		ath6kl_err("Failed to upload OTP file: %d\n", ret);
		return ret;
	}

	/* read firmware start address */
	ret = ath6kl_bmi_read(ar,
			      ath6kl_get_hi_item_addr(ar,
						      HI_ITEM(hi_app_start)),
			      (u8 *) &address, sizeof(address));

	if (ret) {
		ath6kl_err("Failed to read hi_app_start: %d\n", ret);
		return ret;
	}

	ar->hw.app_start_override_addr = address;

	ath6kl_dbg(ATH6KL_DBG_BOOT, "app_start_override_addr 0x%x\n",
		   ar->hw.app_start_override_addr);

	/* execute the OTP code */
	ath6kl_dbg(ATH6KL_DBG_BOOT, "executing OTP at 0x%x\n", address);
	param = 0;
	ath6kl_bmi_execute(ar, address, &param);

	return ret;
}

static int ath6kl_upload_firmware(struct ath6kl *ar)
{
	u32 address;
	int ret;

	if (WARN_ON(ar->fw == NULL))
		return -ENOENT;

	address = ar->hw.app_load_addr;

	ath6kl_dbg(ATH6KL_DBG_BOOT, "writing firmware to 0x%x (%zd B)\n",
		   address, ar->fw_len);

	ret = ath6kl_bmi_fast_download(ar, address, ar->fw, ar->fw_len);

	if (ret) {
		ath6kl_err("Failed to write firmware: %d\n", ret);
		return ret;
	}

	/*
	 * Set starting address for firmware
	 * Don't need to setup app_start override addr on AR6004
	 */
	if (ar->target_type != TARGET_TYPE_AR6004) {
		address = ar->hw.app_start_override_addr;
		ath6kl_bmi_set_app_start(ar, address);
	}
	return ret;
}

static int ath6kl_upload_patch(struct ath6kl *ar)
{
	u32 address, param;
	int ret;

	if (WARN_ON(ar->fw_patch == NULL))
		return -ENOENT;

	address = ar->hw.dataset_patch_addr;

	ath6kl_dbg(ATH6KL_DBG_BOOT, "writing patch to 0x%x (%zd B)\n",
		   address, ar->fw_patch_len);

	ret = ath6kl_bmi_write(ar, address, ar->fw_patch, ar->fw_patch_len);
	if (ret) {
		ath6kl_err("Failed to write patch file: %d\n", ret);
		return ret;
	}

	param = address;
	ath6kl_bmi_write(ar,
			 ath6kl_get_hi_item_addr(ar,
			 HI_ITEM(hi_dset_list_head)),
			 (unsigned char *) &param, 4);

	return 0;
}

static int ath6kl_init_upload(struct ath6kl *ar)
{
	u32 param, options, sleep, address;
	int status = 0;

	if (ar->target_type != TARGET_TYPE_AR6003 &&
		ar->target_type != TARGET_TYPE_AR6004)
		return -EINVAL;

	/* temporarily disable system sleep */
	address = MBOX_BASE_ADDRESS + LOCAL_SCRATCH_ADDRESS;
	status = ath6kl_bmi_reg_read(ar, address, &param);
	if (status)
		return status;

	options = param;

	param |= ATH6KL_OPTION_SLEEP_DISABLE;
	status = ath6kl_bmi_reg_write(ar, address, param);
	if (status)
		return status;

	address = RTC_BASE_ADDRESS + SYSTEM_SLEEP_ADDRESS;
	status = ath6kl_bmi_reg_read(ar, address, &param);
	if (status)
		return status;

	sleep = param;

	param |= SM(SYSTEM_SLEEP_DISABLE, 1);
	status = ath6kl_bmi_reg_write(ar, address, param);
	if (status)
		return status;

	ath6kl_dbg(ATH6KL_DBG_TRC, "old options: %d, old sleep: %d\n",
		   options, sleep);

	/* program analog PLL register */
	/* no need to control 40/44MHz clock on AR6004 */
	if (ar->target_type != TARGET_TYPE_AR6004) {
		status = ath6kl_bmi_reg_write(ar, ATH6KL_ANALOG_PLL_REGISTER,
					      0xF9104001);

		if (status)
			return status;

		/* Run at 80/88MHz by default */
		param = SM(CPU_CLOCK_STANDARD, 1);

		address = RTC_BASE_ADDRESS + CPU_CLOCK_ADDRESS;
		status = ath6kl_bmi_reg_write(ar, address, param);
		if (status)
			return status;
	}

	param = 0;
	address = RTC_BASE_ADDRESS + LPO_CAL_ADDRESS;
	param = SM(LPO_CAL_ENABLE, 1);
	status = ath6kl_bmi_reg_write(ar, address, param);
	if (status)
		return status;

	/* WAR to avoid SDIO CRC err */
	if (ar->version.target_ver == AR6003_REV2_VERSION) {
		ath6kl_err("temporary war to avoid sdio crc error\n");

		param = 0x20;

		address = GPIO_BASE_ADDRESS + GPIO_PIN10_ADDRESS;
		status = ath6kl_bmi_reg_write(ar, address, param);
		if (status)
			return status;

		address = GPIO_BASE_ADDRESS + GPIO_PIN11_ADDRESS;
		status = ath6kl_bmi_reg_write(ar, address, param);
		if (status)
			return status;

		address = GPIO_BASE_ADDRESS + GPIO_PIN12_ADDRESS;
		status = ath6kl_bmi_reg_write(ar, address, param);
		if (status)
			return status;

		address = GPIO_BASE_ADDRESS + GPIO_PIN13_ADDRESS;
		status = ath6kl_bmi_reg_write(ar, address, param);
		if (status)
			return status;
	}

	/* write EEPROM data to Target RAM */
	status = ath6kl_upload_board_file(ar);
	if (status)
		return status;

	/* transfer One time Programmable data */
	status = ath6kl_upload_otp(ar);
	if (status)
		return status;

	/* Download Target firmware */
	status = ath6kl_upload_firmware(ar);
	if (status)
		return status;

	status = ath6kl_upload_patch(ar);
	if (status)
		return status;

	/* Restore system sleep */
	address = RTC_BASE_ADDRESS + SYSTEM_SLEEP_ADDRESS;
	status = ath6kl_bmi_reg_write(ar, address, sleep);
	if (status)
		return status;

	address = MBOX_BASE_ADDRESS + LOCAL_SCRATCH_ADDRESS;
	param = options | 0x20;
	status = ath6kl_bmi_reg_write(ar, address, param);
	if (status)
		return status;

	/* Configure GPIO AR6003 UART */
	param = CONFIG_AR600x_DEBUG_UART_TX_PIN;
	status = ath6kl_bmi_write(ar,
				  ath6kl_get_hi_item_addr(ar,
				  HI_ITEM(hi_dbg_uart_txpin)),
				  (u8 *)&param, 4);

	return status;
}

static int ath6kl_init_hw_params(struct ath6kl *ar)
{
	switch (ar->version.target_ver) {
	case AR6003_REV2_VERSION:
		ar->hw.dataset_patch_addr = AR6003_REV2_DATASET_PATCH_ADDRESS;
		ar->hw.app_load_addr = AR6003_REV2_APP_LOAD_ADDRESS;
		ar->hw.board_ext_data_addr = AR6003_REV2_BOARD_EXT_DATA_ADDRESS;
		ar->hw.reserved_ram_size = AR6003_REV2_RAM_RESERVE_SIZE;
		break;
	case AR6003_REV3_VERSION:
		ar->hw.dataset_patch_addr = AR6003_REV3_DATASET_PATCH_ADDRESS;
		ar->hw.app_load_addr = 0x1234;
		ar->hw.board_ext_data_addr = AR6003_REV3_BOARD_EXT_DATA_ADDRESS;
		ar->hw.reserved_ram_size = AR6003_REV3_RAM_RESERVE_SIZE;
		break;
	case AR6004_REV1_VERSION:
		ar->hw.dataset_patch_addr = AR6003_REV2_DATASET_PATCH_ADDRESS;
		ar->hw.app_load_addr = AR6003_REV3_APP_LOAD_ADDRESS;
		ar->hw.board_ext_data_addr = AR6004_REV1_BOARD_EXT_DATA_ADDRESS;
		ar->hw.reserved_ram_size = AR6004_REV1_RAM_RESERVE_SIZE;
		break;
	default:
		ath6kl_err("Unsupported hardware version: 0x%x\n",
			   ar->version.target_ver);
		return -EINVAL;
	}

	ath6kl_dbg(ATH6KL_DBG_BOOT,
		   "target_ver 0x%x target_type 0x%x dataset_patch 0x%x app_load_addr 0x%x\n",
		   ar->version.target_ver, ar->target_type,
		   ar->hw.dataset_patch_addr, ar->hw.app_load_addr);
	ath6kl_dbg(ATH6KL_DBG_BOOT,
		   "app_start_override_addr 0x%x board_ext_data_addr 0x%x reserved_ram_size 0x%x",
		   ar->hw.app_start_override_addr, ar->hw.board_ext_data_addr,
		   ar->hw.reserved_ram_size);

	return 0;
}

static int ath6kl_init(struct net_device *dev)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	int status = 0;
	s32 timeleft;

	if (!ar)
		return -EIO;

	/* Do we need to finish the BMI phase */
	if (ath6kl_bmi_done(ar)) {
		status = -EIO;
		goto ath6kl_init_done;
	}

	/* Indicate that WMI is enabled (although not ready yet) */
	set_bit(WMI_ENABLED, &ar->flag);
	ar->wmi = ath6kl_wmi_init(ar);
	if (!ar->wmi) {
		ath6kl_err("failed to initialize wmi\n");
		status = -EIO;
		goto ath6kl_init_done;
	}

	ath6kl_dbg(ATH6KL_DBG_TRC, "%s: got wmi @ 0x%p.\n", __func__, ar->wmi);

	/*
	 * The reason we have to wait for the target here is that the
	 * driver layer has to init BMI in order to set the host block
	 * size.
	 */
	if (ath6kl_htc_wait_target(ar->htc_target)) {
		status = -EIO;
		goto err_node_cleanup;
	}

	if (ath6kl_init_service_ep(ar)) {
		status = -EIO;
		goto err_cleanup_scatter;
	}

	/* setup access class priority mappings */
	ar->ac_stream_pri_map[WMM_AC_BK] = 0; /* lowest  */
	ar->ac_stream_pri_map[WMM_AC_BE] = 1;
	ar->ac_stream_pri_map[WMM_AC_VI] = 2;
	ar->ac_stream_pri_map[WMM_AC_VO] = 3; /* highest */

	/* give our connected endpoints some buffers */
	ath6kl_rx_refill(ar->htc_target, ar->ctrl_ep);
	ath6kl_rx_refill(ar->htc_target, ar->ac2ep_map[WMM_AC_BE]);

	/* allocate some buffers that handle larger AMSDU frames */
	ath6kl_refill_amsdu_rxbufs(ar, ATH6KL_MAX_AMSDU_RX_BUFFERS);

	/* setup credit distribution */
	ath6k_setup_credit_dist(ar->htc_target, &ar->credit_state_info);

	ath6kl_cookie_init(ar);

	/* start HTC */
	status = ath6kl_htc_start(ar->htc_target);

	if (status) {
		ath6kl_cookie_cleanup(ar);
		goto err_rxbuf_cleanup;
	}

	/* Wait for Wmi event to be ready */
	timeleft = wait_event_interruptible_timeout(ar->event_wq,
						    test_bit(WMI_READY,
							     &ar->flag),
						    WMI_TIMEOUT);

	ath6kl_dbg(ATH6KL_DBG_BOOT, "firmware booted\n");

	if (ar->version.abi_ver != ATH6KL_ABI_VERSION) {
		ath6kl_err("abi version mismatch: host(0x%x), target(0x%x)\n",
			   ATH6KL_ABI_VERSION, ar->version.abi_ver);
		status = -EIO;
		goto err_htc_stop;
	}

	if (!timeleft || signal_pending(current)) {
		ath6kl_err("wmi is not ready or wait was interrupted\n");
		status = -EIO;
		goto err_htc_stop;
	}

	ath6kl_dbg(ATH6KL_DBG_TRC, "%s: wmi is ready\n", __func__);

	/* communicate the wmi protocol verision to the target */
	if ((ath6kl_set_host_app_area(ar)) != 0)
		ath6kl_err("unable to set the host app area\n");

	ar->conf_flags = ATH6KL_CONF_IGNORE_ERP_BARKER |
			 ATH6KL_CONF_ENABLE_11N | ATH6KL_CONF_ENABLE_TX_BURST;

	ar->wdev->wiphy->flags |= WIPHY_FLAG_SUPPORTS_FW_ROAM;

	status = ath6kl_target_config_wlan_params(ar);
	if (!status)
		goto ath6kl_init_done;

err_htc_stop:
	ath6kl_htc_stop(ar->htc_target);
err_rxbuf_cleanup:
	ath6kl_htc_flush_rx_buf(ar->htc_target);
	ath6kl_cleanup_amsdu_rxbufs(ar);
err_cleanup_scatter:
	ath6kl_hif_cleanup_scatter(ar);
err_node_cleanup:
	ath6kl_wmi_shutdown(ar->wmi);
	clear_bit(WMI_ENABLED, &ar->flag);
	ar->wmi = NULL;

ath6kl_init_done:
	return status;
}

int ath6kl_core_init(struct ath6kl *ar)
{
	int ret = 0;
	struct ath6kl_bmi_target_info targ_info;

	ar->ath6kl_wq = create_singlethread_workqueue("ath6kl");
	if (!ar->ath6kl_wq)
		return -ENOMEM;

	ret = ath6kl_bmi_init(ar);
	if (ret)
		goto err_wq;

	ret = ath6kl_bmi_get_target_info(ar, &targ_info);
	if (ret)
		goto err_bmi_cleanup;

	ar->version.target_ver = le32_to_cpu(targ_info.version);
	ar->target_type = le32_to_cpu(targ_info.type);
	ar->wdev->wiphy->hw_version = le32_to_cpu(targ_info.version);

	ret = ath6kl_init_hw_params(ar);
	if (ret)
		goto err_bmi_cleanup;

	ret = ath6kl_configure_target(ar);
	if (ret)
		goto err_bmi_cleanup;

	ar->htc_target = ath6kl_htc_create(ar);

	if (!ar->htc_target) {
		ret = -ENOMEM;
		goto err_bmi_cleanup;
	}

	ar->aggr_cntxt = aggr_init(ar->net_dev);
	if (!ar->aggr_cntxt) {
		ath6kl_err("failed to initialize aggr\n");
		ret = -ENOMEM;
		goto err_htc_cleanup;
	}

	ret = ath6kl_fetch_firmwares(ar);
	if (ret)
		goto err_htc_cleanup;

	ret = ath6kl_init_upload(ar);
	if (ret)
		goto err_htc_cleanup;

	ret = ath6kl_init(ar->net_dev);
	if (ret)
		goto err_htc_cleanup;

	/* This runs the init function if registered */
	ret = register_netdev(ar->net_dev);
	if (ret) {
		ath6kl_err("register_netdev failed\n");
		ath6kl_destroy(ar->net_dev, 0);
		return ret;
	}

	set_bit(NETDEV_REGISTERED, &ar->flag);

	ath6kl_dbg(ATH6KL_DBG_TRC, "%s: name=%s dev=0x%p, ar=0x%p\n",
			__func__, ar->net_dev->name, ar->net_dev, ar);

	return ret;

err_htc_cleanup:
	ath6kl_htc_cleanup(ar->htc_target);
err_bmi_cleanup:
	ath6kl_bmi_cleanup(ar);
err_wq:
	destroy_workqueue(ar->ath6kl_wq);
	return ret;
}

void ath6kl_stop_txrx(struct ath6kl *ar)
{
	struct net_device *ndev = ar->net_dev;

	if (!ndev)
		return;

	set_bit(DESTROY_IN_PROGRESS, &ar->flag);

	if (down_interruptible(&ar->sem)) {
		ath6kl_err("down_interruptible failed\n");
		return;
	}

	if (ar->wlan_pwr_state != WLAN_POWER_STATE_CUT_PWR)
		ath6kl_stop_endpoint(ndev, false, true);

	clear_bit(WLAN_ENABLED, &ar->flag);
}

/*
 * We need to differentiate between the surprise and planned removal of the
 * device because of the following consideration:
 *
 * - In case of surprise removal, the hcd already frees up the pending
 *   for the device and hence there is no need to unregister the function
 *   driver inorder to get these requests. For planned removal, the function
 *   driver has to explicitly unregister itself to have the hcd return all the
 *   pending requests before the data structures for the devices are freed up.
 *   Note that as per the current implementation, the function driver will
 *   end up releasing all the devices since there is no API to selectively
 *   release a particular device.
 *
 * - Certain commands issued to the target can be skipped for surprise
 *   removal since they will anyway not go through.
 */
void ath6kl_destroy(struct net_device *dev, unsigned int unregister)
{
	struct ath6kl *ar;

	if (!dev || !ath6kl_priv(dev)) {
		ath6kl_err("failed to get device structure\n");
		return;
	}

	ar = ath6kl_priv(dev);

	destroy_workqueue(ar->ath6kl_wq);

	if (ar->htc_target)
		ath6kl_htc_cleanup(ar->htc_target);

	aggr_module_destroy(ar->aggr_cntxt);

	ath6kl_cookie_cleanup(ar);

	ath6kl_cleanup_amsdu_rxbufs(ar);

	ath6kl_bmi_cleanup(ar);

	ath6kl_debug_cleanup(ar);

	if (unregister && test_bit(NETDEV_REGISTERED, &ar->flag)) {
		unregister_netdev(dev);
		clear_bit(NETDEV_REGISTERED, &ar->flag);
	}

	free_netdev(dev);

	kfree(ar->fw_board);
	kfree(ar->fw_otp);
	kfree(ar->fw);
	kfree(ar->fw_patch);

	ath6kl_cfg80211_deinit(ar);
}
