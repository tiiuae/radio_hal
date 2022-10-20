#include <unistd.h>
#include <cstdint>
#include <cstdarg>
#include <cerrno>
#include <netlink/attr.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/handlers.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <linux/if_ether.h>
#include <linux/nl80211.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "debug.h"
#include "radio_hal.h"
#include "radio_hal_yaml.h"
#include "wifi_hal.h"

#include "wpa_socket/wpa_ctrl.h"

#define WIFI_RADIO_HAL_MAJOR_VERSION 1
#define WIFI_RADIO_HAL_MINOR_VERSION 0

#define WIFI_HAL_WPA_SOCK_PATH (const char *) "/var/run/wpa_supplicant/"
#define WPA_SUPPLICANT_DEFAULT_CONFIG (const char *)"/tmp/wpa_supplicant.conf"

#define CMD_BUFFER_SIZE sizeof(char) * 2048
#define MAC_ADDRESS_LENGTH sizeof(char) * 18 // aa:bb:cc:dd:ee:ff + NULL terminator
#define RESP_BUFFER_SIZE sizeof(char) * 2048
#define SOCKET_PATH_LENGTH sizeof(char) * 64

static const char *client_socket_dir[WIFI_RADIO_MAX] = {0 };
static int debug = 0;

static int wifi_hal_nl_finish_handler(struct nl_msg *msg, void *arg)
{
	int *err = (int *)arg;

	*err = 0;
	return NL_SKIP;
}

void wifi_hal_mac_addr_n2a(char *mac_addr, unsigned char *arg)
{
	int i, l;

	l = 0;
	for (i = 0; i < ETH_ALEN ; i++) {
		if (i == 0) {
			snprintf(mac_addr+l, MAC_ADDRESS_LENGTH, "%02x", arg[i]);
			l += 2;
		} else {
			snprintf(mac_addr+l, MAC_ADDRESS_LENGTH, ":%02x", arg[i]);
			l += 3;
		}
	}
}

int wifi_hal_run_sys_cmd(char *cmd, char *resp_buf, int resp_size)
{
	FILE *f;
	char *buf = resp_buf;
	int size=resp_size, resp_buf_bytes, readbytes;
    char *ret;

	if((f = popen(cmd, "r")) == nullptr) {
		hal_err(HAL_DBG_WIFI, "popen %s error\n", cmd);
		return -1;
	}

	while(!feof(f))
	{
		*buf = 0;
		if(size>=128) {
			resp_buf_bytes=128;
		} else {
			resp_buf_bytes=size-1;
		}

		ret = fgets(buf,resp_buf_bytes,f);
		readbytes = (int)strlen(buf);
		if (!readbytes || ret == nullptr)
			break;

		size -= readbytes;
		buf += readbytes;

	}
	pclose(f);
	resp_buf[resp_size-1]=0;

	if (debug)
		hal_err(HAL_DBG_WIFI, "sys cmd:%s resp:%s\n", cmd, resp_buf);

	return 0;
}

static int replace_line_change(char *buf, size_t size) {
	int len;

	len = strnlen(buf, size);
	if (len > 0 && (buf[len-1] == '\n'|| buf[len-1] == '\0') ) {
		buf[len-1] = '\0';
	} else {
		hal_err(HAL_DBG_WIFI, "fail to find line change \n");
		return -1;
	}
	return 0;
}

static int wifi_hal_get_phyname(struct wifi_softc *sc)
{
	char cmd_buf[CMD_BUFFER_SIZE] = {0};
	char resp_buf[RESP_BUFFER_SIZE] = {0};
	int ret = 0;

	for (int i = 0; i < WIFI_RADIO_MAX; i++) {
		snprintf(cmd_buf, CMD_BUFFER_SIZE, "iw dev %s info | grep wiphy | awk '{print $2}'", sc->nl_ctx.ifname[i]);
		ret = wifi_hal_run_sys_cmd(cmd_buf, resp_buf, RESP_BUFFER_SIZE - 1);
		if (ret) {
			hal_err(HAL_DBG_WIFI, "failed to get phyname\n");
			return -1;
		}
		ret = replace_line_change(resp_buf, (size_t)RESP_BUFFER_SIZE-1);
		if (ret<0) {
			hal_err(HAL_DBG_WIFI, "fail with phyname\n");
			return -1;
		}

		if (strlen(resp_buf) < sizeof(sc->nl_ctx.phyname[i]) - 1)
			strncpy(sc->nl_ctx.phyname[i], resp_buf, sizeof(sc->nl_ctx.phyname[i]));
		sc->radio_amount+=1;
		hal_info(HAL_DBG_WIFI, "phy%s %s\n", sc->nl_ctx.phyname[i], sc->nl_ctx.ifname[i]);
	}

	return 0;
}

int wifi_hal_channel_to_frequency(int chan, enum nl80211_band band)
{
	if (chan <= 0)
		return 0;

	switch (band) {
		case NL80211_BAND_2GHZ:
			if (chan == 14)
				return 2484;
			else if (chan < 14)
				return 2407 + chan * 5;
			break;
		case NL80211_BAND_5GHZ:
			if (chan >= 182 && chan <= 196)
				return 4000 + chan * 5;
			else
				return 5000 + chan * 5;
			break;
		case NL80211_BAND_60GHZ:
			if (chan < 5)
				return 56160 + chan * 2160;
			break;
		default:
			;
	}

	return 0;
}

__attribute__((unused)) int wifi_hal_frequency_to_channel(int freq)
{
	if (freq == 2484)
		return 14;
	else if (freq < 2484)
		return (freq - 2407) / 5;
	else if (freq >= 4910 && freq <= 4980)
		return (freq - 4000) / 5;
	else if (freq <= 45000)
		return (freq - 5000) / 5;
	else if (freq >= 58320 && freq <= 64800)
		return (freq - 56160) / 2160;
	else
		return 0;
}

static void get_mac_addr(struct wifi_softc *sc, char *mac_addr, int index)
{
	struct ifreq if_req = {{0}};
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	unsigned char *mac;

	if_req.ifr_addr.sa_family = AF_INET;
	strncpy(if_req.ifr_name, sc->nl_ctx.ifname[index], RADIO_IFNAME_SIZE-1);
	ioctl(fd, SIOCGIFHWADDR, &if_req);
	close(fd);
	mac = (unsigned char*)if_req.ifr_hwaddr.sa_data;
	wifi_hal_mac_addr_n2a(sc->mac_addr[index], mac);
	wifi_hal_mac_addr_n2a(mac_addr, mac);
}

static int wifi_hal_ifname_resp_hdlr(struct nl_msg *msg, void *arg)
{
	struct wifi_softc *sc = (struct wifi_softc *)arg;
	struct netlink_ctx *nl_ctx = &sc->nl_ctx;
	struct genlmsghdr *hdr = (struct genlmsghdr *)nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];

	nla_parse(tb_msg,
			NL80211_ATTR_MAX,
			genlmsg_attrdata(hdr, 0),
			genlmsg_attrlen(hdr, 0),
			nullptr);

	for (int i=0; i < WIFI_RADIO_MAX; i++) {

		for (int x=0; x < WIFI_RADIO_MAX; x++){
			if (strncmp(nl_ctx->ifname[x], nla_get_string(tb_msg[NL80211_ATTR_IFNAME]), strlen("wlp5s0")) == 0) {
				goto skip;
			}
		}

		if (nl_ctx->ifname[i][0] ==  '\0' && tb_msg[NL80211_ATTR_IFNAME]) {
			snprintf(nl_ctx->ifname[i], RADIO_IFNAME_SIZE, "%s", nla_get_string(tb_msg[NL80211_ATTR_IFNAME]));
			nl_ctx->ifindex[i] = (int) nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]);
		}
skip:
		asm("NOP");
	}

	return NL_SKIP;
}

static int wifi_hal_connection_info_hdlr(struct nl_msg *msg, void *arg)
{
	struct wifi_softc *sc = (struct wifi_softc *)arg;
	struct netlink_ctx *nl_ctx = &sc->nl_ctx;
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *hdr = (struct genlmsghdr *)nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
	struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
	static struct nla_policy sta_info[NL80211_STA_INFO_MAX + 1] = {{0}};
	static struct nla_policy rate_info[NL80211_RATE_INFO_MAX + 1] = {{0}};

	sta_info[NL80211_STA_INFO_INACTIVE_TIME].type = NLA_U32;
	sta_info[NL80211_STA_INFO_RX_BYTES].type = NLA_U32;
	sta_info[NL80211_STA_INFO_TX_BYTES].type = NLA_U32;
	sta_info[NL80211_STA_INFO_RX_PACKETS].type = NLA_U32;
	sta_info[NL80211_STA_INFO_TX_PACKETS].type = NLA_U32;
	sta_info[NL80211_STA_INFO_SIGNAL].type = NLA_U8;
	sta_info[NL80211_STA_INFO_TX_BITRATE].type = NLA_NESTED;
	sta_info[NL80211_STA_INFO_RX_BITRATE].type = NLA_NESTED;
	sta_info[NL80211_STA_INFO_LLID].type = NLA_U16;
	sta_info[NL80211_STA_INFO_PLID].type = NLA_U16;
	sta_info[NL80211_STA_INFO_PLINK_STATE].type = NLA_U8;
	rate_info[NL80211_RATE_INFO_BITRATE].type = NLA_U16;
	rate_info[NL80211_RATE_INFO_MCS].type = NLA_U8;
	rate_info[NL80211_RATE_INFO_40_MHZ_WIDTH].type = NLA_FLAG;
	rate_info[NL80211_RATE_INFO_SHORT_GI].type = NLA_FLAG;

	nla_parse(tb_msg,
			NL80211_ATTR_MAX,
			genlmsg_attrdata(hdr, 0),
			genlmsg_attrlen(hdr, 0),
			nullptr);

	if (!tb_msg[NL80211_ATTR_STA_INFO]) {
		hal_err(HAL_DBG_WIFI, "failed to parse sta info\n");
		return NL_SKIP;
	}

	if (nla_parse_nested(sinfo, NL80211_STA_INFO_MAX,
				tb_msg[NL80211_ATTR_STA_INFO], sta_info)) {
		hal_err(HAL_DBG_WIFI, "failed to parse nested sta info\n");
		return NL_SKIP;
	}

	if (sinfo[NL80211_STA_INFO_SIGNAL]) {
		for (int i=0; i < sc->radio_amount; i++) {
			if (nl_ctx->ifindex[i] == (int)nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX])) {
				sc->signal[i] = (unsigned char) nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]);
			}
		}
	}

	if (sinfo[NL80211_STA_INFO_TX_BITRATE])
	{
		if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX, sinfo[NL80211_STA_INFO_TX_BITRATE], rate_info)) {
			hal_err(HAL_DBG_WIFI, "failed to parse nested rate attributes!\n");
		}
		else {
			for (int i=0; i < sc->radio_amount; i++) {
				if ((nl_ctx->ifindex[i] == (int)nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX])) &&
						rinfo[NL80211_RATE_INFO_BITRATE]) {
					sc->txrate[i] = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE])/10;
				}
			}
		}
	}

	if (sinfo[NL80211_STA_INFO_RX_BITRATE])
	{
		if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX,
					sinfo[NL80211_STA_INFO_RX_BITRATE], rate_info))
		{
			hal_err(HAL_DBG_WIFI, "failed to parse nested rate attributes!\n");
		}
		else {

			for (int i=0; i < sc->radio_amount; i++) {
				if ((nl_ctx->ifindex[i] == (int)nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX])) &&
					rinfo[NL80211_RATE_INFO_BITRATE]) {
					sc->rxrate[i] = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE])/10;
				}
			}
		}
	}

	return NL_SKIP;
}

static int wifi_hal_register_nl_cb(struct wifi_softc *sc)
{
	struct netlink_ctx *nl_ctx = &sc->nl_ctx;

	nl_ctx->if_cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!nl_ctx->if_cb)
	{
		hal_err(HAL_DBG_WIFI, "failed to allocate if NL callback.\n");
		goto error;
	}

	nl_ctx->link_info_cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!nl_ctx->link_info_cb)
	{
		hal_err(HAL_DBG_WIFI, "failed to allocate link info NL callback.\n");
		goto error;
	}

	nl_cb_set(nl_ctx->if_cb, NL_CB_VALID , NL_CB_CUSTOM, wifi_hal_ifname_resp_hdlr, sc);
	nl_cb_set(nl_ctx->if_cb, NL_CB_FINISH, NL_CB_CUSTOM, wifi_hal_nl_finish_handler, &(nl_ctx->if_cb_err));
	nl_cb_set(nl_ctx->link_info_cb, NL_CB_VALID , NL_CB_CUSTOM, wifi_hal_connection_info_hdlr, sc);
	nl_cb_set(nl_ctx->link_info_cb, NL_CB_FINISH, NL_CB_CUSTOM, wifi_hal_nl_finish_handler, &(nl_ctx->linkinfo_cb_err));

	return 0;

error:
	nl_cb_put(nl_ctx->if_cb );
	nl_cb_put(nl_ctx->link_info_cb);
	return -ENOMEM;
}

__attribute__((unused)) static int wifi_hal_switch_channel(struct netlink_ctx *nl_ctx, char *channel,
		unsigned int count)
{
	static const struct {
		const char *name;
		unsigned int val;
	} htmap[] = {
		{ .name = "HT20", .val = NL80211_CHAN_HT20, },
		{ .name = "HT40+", .val = NL80211_CHAN_HT40PLUS, },
		{ .name = "HT40-", .val = NL80211_CHAN_HT40MINUS, },
	};
	unsigned int htval = NL80211_CHAN_NO_HT;
	enum nl80211_band band;
	long unsigned int i;
	char *end;
	int err = 0;
	struct nl_msg* ch_sw_msg = nlmsg_alloc();
	int chan;

	if (!ch_sw_msg) {
		hal_err(HAL_DBG_WIFI, "failed to allocate ch switch NL message.\n");
		return -ENOMEM;
	}

	genlmsg_put(ch_sw_msg,
			NL_AUTO_PORT,
			NL_AUTO_SEQ,
			nl_ctx->nl80211_id,
			0,
			NLM_F_DUMP,
			NL80211_CMD_GET_STATION,
			0);

	chan = (int)strtoul(channel, &end, 10);
	band = chan <= 14 ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
	chan = wifi_hal_channel_to_frequency(chan, band);

	NLA_PUT_U32(ch_sw_msg, NL80211_ATTR_WIPHY_FREQ, chan);
	NLA_PUT_U32(ch_sw_msg, NL80211_ATTR_CH_SWITCH_COUNT, count);

	for (i = 0; i < sizeof(htmap)/sizeof(htmap[0]); i++) {
		if (strcasecmp(htmap[i].name, channel) == 0) {
			htval = htmap[i].val;
			break;
		}
	}

	NLA_PUT_U32(ch_sw_msg, NL80211_ATTR_WIPHY_CHANNEL_TYPE, htval);

	nl_ctx->csa_cb_err = 1;
	err = nl_send_auto((nl_sock *)(nl_ctx->sock), ch_sw_msg);
	while (nl_ctx->csa_cb_err > 0)
	{
		nl_recvmsgs((nl_sock *)nl_ctx->sock, nl_ctx->gen_nl_cb);
	}
	nlmsg_free(ch_sw_msg);

	return 0;

nla_put_failure:
	nlmsg_free(ch_sw_msg);
	return err;
}

static int wifi_hal_nl80211_attach(struct wifi_softc *sc)
{
	struct netlink_ctx *nl_ctx = &sc->nl_ctx;
	struct nl_sock *sock;
	int err;

	nl_ctx->sock = nl_socket_alloc();
	if (!nl_ctx->sock) {
		hal_err(HAL_DBG_WIFI, "failed to alloc netlink socket\n");
		return -ENOMEM;
	}
	sock = nl_ctx->sock;
	err = genl_connect(sock);
	if (err) {
		hal_err(HAL_DBG_WIFI, "failed to connect to genl\n");
		goto out;
	}

	nl_ctx->nl80211_id = -1;
	nl_ctx->nl80211_id = genl_ctrl_resolve(sock, "nl80211");
	if (nl_ctx->nl80211_id < 0) {
		hal_err(HAL_DBG_WIFI, "nl80211 ctrl resolve failed\n");
		err = -EINVAL;
		goto out;
	}

	return 0;

out:
	nl_socket_free(sock);
	return err;
}

static void wifi_hal_nl80211_dettach(struct wifi_softc *sc)
{
	struct netlink_ctx *nl_ctx = &sc->nl_ctx;
	nl_socket_free(nl_ctx->sock);
	nl_cb_put(nl_ctx->if_cb );
	nl_cb_put(nl_ctx->link_info_cb);
}

__attribute__((unused)) int wifi_hal_nl80211_tx_frame(struct netlink_ctx *nl_ctx, void *action_frame, size_t len, int index)
{
	char cmd = NL80211_CMD_FRAME;
	struct nl_msg *nl_msg = nullptr;
	struct nl_cb *s_cb;
	int err = -1;

	s_cb = nl_cb_alloc(NL_CB_DEBUG);
	if (!s_cb) {
		hal_err(HAL_DBG_WIFI, "failed to allocate nl callback\n");
		err = -ENOMEM;
		goto nla_put_failure;
	}
	nl_socket_set_cb(nl_ctx->sock, s_cb);

	nl_msg = nlmsg_alloc();
	if (!nl_msg) {
		hal_err(HAL_DBG_WIFI, "failed to allocate netlink message\n");
		err = -ENOMEM;
		goto nla_put_failure;
	}

	genlmsg_put(nl_msg, 0, 0, nl_ctx->nl80211_id, 0, 0, cmd, 0);
	NLA_PUT_U32(nl_msg, NL80211_ATTR_IFINDEX, nl_ctx->ifindex[index]);
	NLA_PUT(nl_msg, NL80211_ATTR_FRAME, len, action_frame);
	NLA_PUT_FLAG(nl_msg, NL80211_ATTR_DONT_WAIT_FOR_ACK);
	/* To Do: populate NL80211_ATTR_WIPHY_FREQ from mesh link*/
	err = nl_send_auto_complete(nl_ctx->sock, nl_msg);
	if (err < 0)
		goto nla_put_failure;

	nl_wait_for_ack(nl_ctx->sock);
	nlmsg_free(nl_msg);
	return 0;

nla_put_failure:
	nl_cb_put(s_cb);
	nlmsg_free(nl_msg);
	return err;
}

static int wifi_hal_get_interface(struct netlink_ctx *nl_ctx)
{
	struct nl_msg* if_get_msg = nlmsg_alloc();
	int err = 0;
	if (!if_get_msg) {
		hal_err(HAL_DBG_WIFI, "failed to allocate if get NL  message.\n");
		return -ENOMEM;
	}
	genlmsg_put(if_get_msg,
			NL_AUTO_PORT,
			NL_AUTO_SEQ,
			nl_ctx->nl80211_id,
			0,
			NLM_F_DUMP,
			NL80211_CMD_GET_INTERFACE,
			0);
	nl_ctx->if_cb_err = 1;
	err = nl_send_auto(nl_ctx->sock, if_get_msg);
	if (err<0) {
		hal_err(HAL_DBG_WIFI, "nl_send_auto failed");
		return -1;
	}

	while (nl_ctx->if_cb_err > 0)
	{
		nl_recvmsgs(nl_ctx->sock, nl_ctx->if_cb);
	}
	nlmsg_free(if_get_msg);

	if (!nl_ctx->ifindex[WIFI_RADIO_0])
		return -EINVAL;

	return 0;
}

static int wifi_hal_get_stainfo(struct netlink_ctx *nl_ctx, int index)
{
	struct nl_msg* sta_info_msg = nlmsg_alloc();
	int err = 0;

	if (!sta_info_msg) {
		hal_err(HAL_DBG_WIFI, "failed to allocate sta info  NL  message.\n");
		return -ENOMEM;
	}
	genlmsg_put(sta_info_msg,
			NL_AUTO_PORT,
			NL_AUTO_SEQ,
			nl_ctx->nl80211_id,
			0,
			NLM_F_DUMP,
			NL80211_CMD_GET_STATION,
			0);

	nl_ctx->linkinfo_cb_err = 1;
	nla_put_u32(sta_info_msg, NL80211_ATTR_IFINDEX, nl_ctx->ifindex[index]);
	err = nl_send_auto(nl_ctx->sock, sta_info_msg);
	if (err<0) {
		hal_err(HAL_DBG_WIFI, "nl_send_auto failed");
		return -1;
	}

	while (nl_ctx->linkinfo_cb_err > 0)
	{
		nl_recvmsgs(nl_ctx->sock, nl_ctx->link_info_cb);
	}
	nlmsg_free(sta_info_msg);

	return 0;
}

static int wifi_hal_set_txpower(struct netlink_ctx *nl_ctx, int index, struct wifi_config *config)
{
	struct nl_msg* msg = nlmsg_alloc();
	int ret = 0;
	enum nl80211_commands command = NL80211_CMD_SET_WIPHY;

	if (!msg) {
		hal_err(HAL_DBG_WIFI, "failed to allocate NL80211 message.\n");
		return -ENOMEM;
	}

    /* Create the message, so it will send a command to the nl80211 interface. */
	genlmsg_put(msg, 0, 0, genl_ctrl_resolve(nl_ctx->sock, "nl80211"), 0, 0, command, 0);

	/* Add specific attributes to change the frequency of the device. */
	NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, if_nametoindex(nl_ctx->ifname[index]));
	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_TX_POWER_SETTING, NL80211_TX_POWER_LIMITED);
	NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_TX_POWER_LEVEL, config->tx_power * 100); // dbm to mdBm

	/* Finally send it and receive the amount of bytes sent. */
	ret = nl_send_auto(nl_ctx->sock, msg);
	if (ret<0)
		goto nla_put_failure;

	nlmsg_free(msg);
	return EXIT_SUCCESS;

nla_put_failure:
	nlmsg_free(msg);
	return -EXIT_FAILURE;
}

static int wifi_hal_get_hal_version(char *version)
{
	snprintf(version, 32, "%d.%d", WIFI_RADIO_HAL_MAJOR_VERSION, WIFI_RADIO_HAL_MINOR_VERSION);

	return 0;
}

int wifi_hal_get_iface_name(struct radio_context *ctx, char *name, int radio_index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	memcpy(name, sc->nl_ctx.ifname[radio_index], RADIO_IFNAME_SIZE);

	return 0;
}

static int wifi_hal_get_txrate (struct radio_context *ctx, int radio_index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	wifi_hal_get_interface(&sc->nl_ctx);
	wifi_hal_get_stainfo(&sc->nl_ctx, radio_index);

	return sc->txrate[radio_index];
}

static int wifi_hal_get_rxrate (struct radio_context *ctx, int radio_index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	wifi_hal_get_interface(&sc->nl_ctx);
	wifi_hal_get_stainfo(&sc->nl_ctx, radio_index);

	return sc->rxrate[radio_index];
}

static int wifi_hal_wpa_mesh_attach(struct wifi_softc *sc, int index)
{
	struct wpa_ctrl_ctx *ctx = &sc->wpa_ctx[index];
	char sock_path[SOCKET_PATH_LENGTH] = {0};
	int len = 0;

	len += snprintf(sock_path, SOCKET_PATH_LENGTH, "%s", WIFI_HAL_WPA_SOCK_PATH);
	snprintf(sock_path + len, SOCKET_PATH_LENGTH, "%s", (const char *)(sc->nl_ctx.ifname[index]));

	ctx->mesh_ctrl = wpa_ctrl_open2(sock_path, client_socket_dir[index]);
	if (!ctx->mesh_ctrl) {
		hal_err(HAL_DBG_WIFI, "Couldn't open '%s'\n", sock_path);
		return -1;
	}

	return 0;
}

static int wifi_hal_wpa_attach(struct wifi_softc *sc, int index)
{
	struct wpa_ctrl_ctx *ctx = &sc->wpa_ctx[index];
	char sock_path[SOCKET_PATH_LENGTH] = {0};
	int len = 0;

	/* Initialise even not used */
	ctx->mesh_ctrl = (wpa_ctrl *) nullptr;

	len += snprintf(sock_path, SOCKET_PATH_LENGTH, WIFI_HAL_WPA_SOCK_PATH);
	len += snprintf(sock_path + len, SOCKET_PATH_LENGTH, "%s", (const char *)(sc->nl_ctx.ifname[index]));
	ctx->ctrl = wpa_ctrl_open2(sock_path, client_socket_dir[index]);
	if (!ctx->ctrl) {
		hal_err(HAL_DBG_WIFI, "Couldn't open '%s'\n", sock_path);
		return -1;
	}
	ctx->fd = wpa_ctrl_get_fd(ctx->ctrl);

	ctx->monitor = wpa_ctrl_open2(sock_path, client_socket_dir[index]);
	if (!ctx->monitor) {
		hal_err(HAL_DBG_WIFI, "Couldn't open '%s'\n", sock_path);
		wpa_ctrl_close(ctx->ctrl);
		return -1;
	}

	if (wpa_ctrl_attach(ctx->monitor) < 0) {
		hal_err(HAL_DBG_WIFI, "wpa_ctrl monitor attach failed");
		wpa_ctrl_close(ctx->monitor);
		wpa_ctrl_close(ctx->ctrl);
		return -1;
	}

	return 0;
}

static void wifi_hal_wpa_dettach(struct wifi_softc *sc, int index)
{
	struct wpa_ctrl_ctx *ctx = &sc->wpa_ctx[index];

	wpa_ctrl_close(ctx->monitor);
	wpa_ctrl_close(ctx->ctrl);
	wpa_ctrl_close(ctx->mesh_ctrl);
}

int create_default_wpa_config()
{
	FILE *fd;

	fd = fopen(WPA_SUPPLICANT_DEFAULT_CONFIG, "w");
	if (!fd) {
		return -ENOMEM;
	}

	fprintf(fd, "ctrl_interface=DIR=/var/run/wpa_supplicant\n");
	fclose(fd);

	return 0;
}

static int wifi_hal_start_wpa_dummy_config(struct radio_context *ctx, int radio_index)
{
	char *cmd_buf = nullptr;
	char resp_buf[RESP_BUFFER_SIZE] = {0};
	int ret = 0;
	int str_len = 0;
	int len = sizeof(resp_buf) - 1;

	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	ret = create_default_wpa_config();
	if (ret) {
		hal_err(HAL_DBG_WIFI, "failed to create default wpa configuration\n");
		return -1;
	}

#if 0
	asprintf(&cmd_buf, (const char*) "iw dev %s del 2>/dev/null", MESH_NAME);
	ret = wifi_hal_run_sys_cmd(cmd_buf, resp_buf, len);
	if (ret) {
		hal_warn(HAL_DBG_WIFI, "failed to delete stale mesh interface\n");
	}
	safe_free(cmd_buf);
#endif

	ret = system("pkill wpa_supplicant");
	if (ret) {
		hal_warn(HAL_DBG_WIFI, "failed to pkill wpa_supplicant\n");
	}

	for (int i=0; i < sc->radio_amount; i++) {

		str_len = asprintf(&cmd_buf, (const char*) "rm /var/run/wpa_supplicant/%s 2>/dev/null", sc->nl_ctx.ifname[i]);
		if (str_len)
			ret = wifi_hal_run_sys_cmd(cmd_buf, resp_buf, len);
		else
			return -1;

		if (ret) {
			hal_warn(HAL_DBG_WIFI, "failed to delete default ctrl interface\n");
		}
		free(cmd_buf);

		str_len = asprintf(&cmd_buf,
						(const char*) "wpa_supplicant -dd -Dnl80211 -B -i%s -c%s -f /tmp/wpa_default.log ",
						 sc->nl_ctx.ifname[i], WPA_SUPPLICANT_DEFAULT_CONFIG);
		if (str_len)
			ret = system(cmd_buf);
		else
			return -1;

		if (ret) {
			hal_err(HAL_DBG_WIFI, "failed to start supplicant with default conf\n");
			return -1;
		}
		free(cmd_buf);
	}
	return ret;
}

static int wifi_hal_open(struct radio_context *ctx, enum radio_type type)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
	int err = 0, ret = 0;

	err = wifi_hal_get_interface(&sc->nl_ctx);
	if (err<0) {
		hal_err(HAL_DBG_WIFI, "no wifi device");
		return -1;
	}

	ret = wifi_hal_get_phyname(sc);
	if (ret<0) {
		hal_err(HAL_DBG_WIFI, "failed to get phy");
		return -1;
	}

	for (int i = 0; i < sc->radio_amount; i++) {
		hal_info(HAL_DBG_WIFI, "wifi(%d) interface: %s , interface index = %d\n", i, sc->nl_ctx.ifname[i], sc->nl_ctx.ifindex[i]);
	}

	err = wifi_hal_start_wpa_dummy_config(ctx, type);
	if (err) {
		/* Fix Me: This is false positive error seen due to system cmd */
		hal_info(HAL_DBG_WIFI, "wifi hal default supplicant start failed\n");
	}

	for (int i = 0; i < sc->radio_amount; i++) {
		err = wifi_hal_wpa_attach(sc, i);
		if (err) {
			hal_err(HAL_DBG_WIFI, "wpa_attach failed \n");
			return err;
		}
	}

	return 0;
}

static int wifi_hal_close(struct radio_context *ctx, enum radio_type type) {
	struct wifi_softc *sc = (struct wifi_softc *) ctx->radio_private;

	for (int i = 0; i < sc->radio_amount; i++) {
		wifi_hal_wpa_dettach(sc, i);
		memset(sc->nl_ctx.ifname[i], 0, RADIO_IFNAME_SIZE);
		sc->nl_ctx.ifindex[i] = -1;
	}

	return 0;
}

static int wifi_hal_get_mac_addr(struct radio_context *ctx, char *mac_addr, int radio_index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	get_mac_addr(sc, mac_addr, radio_index);
	return 0;
}

static int wifi_hal_send_wpa_command(struct wpa_ctrl_ctx *ctx, int index, const char *cmd, char *resp, size_t *resp_size)
{
	int ret;

	if (!ctx[index].monitor) {
		hal_err(HAL_DBG_WIFI, "ctrl socket not connected '%s' and cmd drooped:%s\n", WIFI_HAL_WPA_SOCK_PATH, cmd);
		return -1;
	}

	if (cmd)
		ret = wpa_ctrl_request(ctx[index].ctrl, cmd, strlen(cmd), resp, resp_size, nullptr);
	else {
		hal_err(HAL_DBG_WIFI, "nullptr error");
		return -1;
	}

	if (ret == -2) {
		hal_err(HAL_DBG_WIFI, "'%s' command timed out.\n", cmd);
		return -2;
	} else if (ret < 0 || strncmp(resp, "FAIL", 4) == 0) {
		return -1;
	}

	if (1) {
		resp[*resp_size] = '\0';
		hal_debug(HAL_DBG_WIFI, "%s:%s\n", cmd, resp);
		if (*resp_size > 0 && resp[*resp_size - 1] != '\n')
			printf("\n");
	}

	return 0;
}

static int wifi_hal_send_wpa_mesh_command(struct wpa_ctrl_ctx *ctx, int index, const char *cmd, char *resp, size_t *resp_size)
{
	int ret;

	if (cmd) {
		if (!ctx[index].mesh_ctrl) {
			hal_err(HAL_DBG_WIFI, "ctrl socket not connected '%s' and cmd dropped:%s\n", WIFI_HAL_WPA_SOCK_PATH, cmd);
			return -1;
		}
		ret = wpa_ctrl_request(ctx[index].mesh_ctrl, cmd, strlen(cmd), resp, (size_t *) resp_size, nullptr);
	} else {
		hal_err(HAL_DBG_WIFI, "nullptr error");
		return -1;
	}

	if (ret == -2) {
		hal_err(HAL_DBG_WIFI, "'%s' command timed out.\n", cmd);
		return -2;
	} else if (ret < 0 || strncmp(resp, "FAIL", 4) == 0) {
		return -1;
	}

	if (1) {
		resp[*resp_size] = '\0';
		hal_debug(HAL_DBG_WIFI, "%s:%s\n", cmd, resp);
		if (*resp_size > 0 && resp[*resp_size - 1] != '\n')
			hal_debug(HAL_DBG_WIFI, "\n");
	}

	return 0;
}

int wifi_hal_trigger_scan(struct wifi_softc *sc, int index)
{
	char buf[CMD_BUFFER_SIZE];
	size_t len = 0;

	len = sizeof(buf) - 1;
	wifi_hal_send_wpa_command(sc->wpa_ctx, index, "SCAN", buf, &len);
	if (len)
		hal_info(HAL_DBG_WIFI, "SCAN TRIGGER:%s\n", buf);

	return 0;
}

static int wifi_hal_get_scan_results(struct radio_context *ctx, char *results, int index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
	char buf[4096];
	size_t len = 0;

	len = sizeof(buf) - 1;
	wifi_hal_trigger_scan(sc, index);
	wifi_hal_send_wpa_command(sc->wpa_ctx, index, "SCAN_RESULTS", buf, &len);
	if (len && debug)
		hal_info(HAL_DBG_WIFI, "SCAN RESULTS:%s\n", buf);

	memcpy(results, buf, len);

	return 0;
}

static int wifi_hal_connect_ap(struct radio_context *ctx, int index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
	struct wifi_config *config = (struct wifi_config *)ctx->config[index];
	char *cmd_buf = nullptr;
	char resp_buf[RESP_BUFFER_SIZE] = {0};
	char nw_id[6] = {0};
	size_t len = sizeof(resp_buf) - 1;
	int ret = 0;
	int str_len = 0;

	str_len = asprintf(&cmd_buf, (const char*) "REMOVE_NETWORK all");
	if (str_len)
		ret = wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret < 0) {
		hal_err(HAL_DBG_WIFI, "Fail to remove existing NW\n");
		return ret;
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "ADD_NETWORK");
	if (str_len)
		ret = wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret < 0) {
        hal_err(HAL_DBG_WIFI, "Fail to add NW\n");
        return ret;
	} else { /* TODO own function? */
		if (strlen(resp_buf) < sizeof(nw_id) - 1) {
			strncpy(nw_id, resp_buf, sizeof(nw_id));
			if (replace_line_change(nw_id, sizeof(nw_id)) < 0){
				hal_err(HAL_DBG_WIFI, "Failed to get NWID\n");
				return -1;
			}
		} else
			return -1;
		hal_info(HAL_DBG_WIFI, "NWID %s\n", nw_id);
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "%s%s%s\"%s\"", "SET_NETWORK ", nw_id, " ssid ", config->ssid);
	if (str_len)
		ret = wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
		hal_err(HAL_DBG_WIFI, "Fail to set ssid\n");
		return ret;
	} else {
		free(cmd_buf);

		str_len = asprintf(&cmd_buf, (const char*) "%s%s%s\"%s\"", "SET_NETWORK ", nw_id, " psk ", config->key);
		if (str_len)
			wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
		else
			return -1;

		if (strncmp(resp_buf, "OK", 2) == 0) {
			str_len = asprintf(&cmd_buf, (const char*) "%s%s", "ENABLE_NETWORK ", nw_id);
			if (str_len)
				wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
			else
				return -1;
		} else {
			hal_err(HAL_DBG_WIFI, "failed to enable network\n");
			return -1;
		}
	}
	free(cmd_buf);

	return 0;
}

static int wifi_hal_create_ap(struct radio_context *ctx, int index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
	struct wifi_config *config = (struct wifi_config *)ctx->config[index];
	char *cmd_buf = nullptr;
	char resp_buf[RESP_BUFFER_SIZE] = {0};
	char nw_id[6] = {0};
	size_t len = sizeof(resp_buf) - 1;
	int ret = 0;
	int str_len = 0;


	str_len = asprintf(&cmd_buf, (const char*) "REMOVE_NETWORK all");
	if (str_len)
		ret = wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret < 0) {
		hal_err(HAL_DBG_WIFI, "Fail to remove existing NW\n");
		return ret;
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "ADD_NETWORK");
	if (str_len)
		ret = wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret < 0) {
		hal_err(HAL_DBG_WIFI, "Fail to add NW\n");
		return ret;
	} else { /* TODO own function? */
		if (strlen(resp_buf) < sizeof(nw_id) - 1) {
			strncpy(nw_id, resp_buf, sizeof(nw_id));
			if (replace_line_change(nw_id, sizeof(nw_id)) < 0){
				hal_err(HAL_DBG_WIFI, "Failed to get NWID\n");
				return -1;
			}
		} else
			return -1;
		hal_info(HAL_DBG_WIFI, "NWID %s\n", nw_id);
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "SET_NETWORK %s mode 2", nw_id);
	if (str_len)
		ret = wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
		hal_err(HAL_DBG_WIFI, "failed to set mesh mode\n");
		return -1;
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "SET_NETWORK %s ssid \"%s\"", nw_id, config->ssid);
	if (str_len)
		ret = wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
		hal_err(HAL_DBG_WIFI, "failed to set mesh ssid\n");
		return -1;
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "SET_NETWORK %s frequency %s", nw_id, config->freq);
	if (str_len)
		ret = wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
		hal_err(HAL_DBG_WIFI, "failed to set mesh freq\n");
		return -1;
	}
	free(cmd_buf);

	// todo not possible with wpa_cli?
	str_len = asprintf(&cmd_buf, (const char*) "iw reg set %s", config->country);
	if (str_len)
		ret = system(cmd_buf);
	else
		return -1;

	if (ret) {
		hal_err(HAL_DBG_WIFI, "failed to set mesh country\n");
		return -1;
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "SET_NETWORK %s key_mgmt WPA-PSK", nw_id);
	if (str_len)
		ret = wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
		hal_err(HAL_DBG_WIFI, "failed to set mesh key mgmt\n");
		return -1;
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "SET_NETWORK %s psk \"%s\"", nw_id, config->key);
	if (str_len)
		ret = wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
		hal_err(HAL_DBG_WIFI, "failed to set mesh psk\n");
		return -1;
	}
	free(cmd_buf);



	str_len = asprintf(&cmd_buf, (const char*) "ENABLE_NETWORK %s", nw_id);
	if (str_len)
		ret = wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
			hal_err(HAL_DBG_WIFI, "failed to enable network\n");
		return -1;
	}
	free(cmd_buf);

	return 0;
}

static int wifi_hal_join_mesh(struct radio_context *ctx, int index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
	struct netlink_ctx *nl_ctx = &sc->nl_ctx;
	struct wifi_config *config = (struct wifi_config *)ctx->config[index];
	char *cmd_buf = nullptr;
	char resp_buf[RESP_BUFFER_SIZE] = {0};
	char nw_id[6] = {0};
	size_t len = sizeof(resp_buf) - 1;
	int ret = 0;
	int str_len = 0;

    hal_info(HAL_DBG_WIFI, "wifi_hal_join_mesh\n");

	/* To do: Back up  AP config and Add support for SAP + MESH later */
    ret = wifi_hal_wpa_mesh_attach(sc, index);
	if (ret) {
		hal_err(HAL_DBG_WIFI, "wpa_supplicant mesh attach failed\n");
		return -1;
	}

	str_len = asprintf(&cmd_buf, (const char*) "REMOVE_NETWORK all");
	if (str_len)
		ret = wifi_hal_send_wpa_command(sc->wpa_ctx, index, cmd_buf, resp_buf, (size_t *)&len);
	else
		return -1;

	if (ret < 0) {
		/* To Do: remove this once concurrency supported */
		hal_warn(HAL_DBG_WIFI, "Fail to remove existing NW\n");
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "ADD_NETWORK");
	if (str_len)
		ret = wifi_hal_send_wpa_mesh_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret < 0) {
		hal_err(HAL_DBG_WIFI, "mesh add network failed\n");
		return -1;
	} else { /* TODO own function? */
		if (strlen(resp_buf) < sizeof(nw_id) - 1) {
			strncpy(nw_id, resp_buf, sizeof(nw_id));
			if (replace_line_change(nw_id, sizeof(nw_id)) < 0){
				hal_err(HAL_DBG_WIFI, "Failed to get NWID\n");
				return -1;
			}
		} else
			return -1;
		hal_info(HAL_DBG_WIFI, "NWID %s\n", nw_id);
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "SET_NETWORK %s mode 5", nw_id);
	if (str_len)
		ret = wifi_hal_send_wpa_mesh_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
		hal_err(HAL_DBG_WIFI, "failed to set mesh mode\n");
		return -1;
	}
	free(cmd_buf);

	// todo not possible with wpa_cli?
	str_len = asprintf(&cmd_buf, (const char*) "iw reg set %s", config->country);
	if (str_len)
		ret = system(cmd_buf);
	else
		return -1;

	if (ret) {
		hal_err(HAL_DBG_WIFI, "failed to set mesh country\n");
		return -1;
	}
	free(cmd_buf);

	// set bandwidth
	ret = wifi_debugfs_write(sc, "chanbw", config->bw, index);
	if (ret) {
		hal_warn(HAL_DBG_WIFI, "failed to set bandwidth, card not supporting?\n");
	}

	ret = wifi_hal_set_txpower(nl_ctx, index, config);
	if (ret<0)
		hal_warn(HAL_DBG_WIFI, "failed to set tx_power, card not supporting?\n");

	str_len = asprintf(&cmd_buf, (const char*) "SET_NETWORK %s ssid \"%s\"", nw_id, config->ssid);
	if (str_len)
		ret = wifi_hal_send_wpa_mesh_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
		hal_err(HAL_DBG_WIFI, "failed to set mesh ssid\n");
		return -1;
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "SET_NETWORK %s frequency %s", nw_id, config->freq);
	if (str_len)
		ret = wifi_hal_send_wpa_mesh_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
		hal_err(HAL_DBG_WIFI, "failed to set mesh freq\n");
		return -1;
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "SET_NETWORK %s key_mgmt %s", nw_id, config->enc);
	if (str_len)
		ret = wifi_hal_send_wpa_mesh_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
		hal_err(HAL_DBG_WIFI, "failed to set mesh key mgmt\n");
		return -1;
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "SET_NETWORK %s psk \"%s\"", nw_id, config->key);
	if (str_len)
		ret = wifi_hal_send_wpa_mesh_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
		hal_err(HAL_DBG_WIFI, "failed to set mesh psk\n");
		return -1;
	}
	free(cmd_buf);

	str_len = asprintf(&cmd_buf, (const char*) "MESH_GROUP_ADD %s", nw_id);
	if (str_len)
		ret = wifi_hal_send_wpa_mesh_command(sc->wpa_ctx, index, cmd_buf, resp_buf, &len);
	else
		return -1;

	if (ret || strncmp(resp_buf, "OK", 2) != 0) {
		hal_err(HAL_DBG_WIFI, "failed to enable mesh\n");
		return -1;
	}
	free(cmd_buf);

	hal_info(HAL_DBG_WIFI, "successfully enabled mesh ess%s on mesh vif\n", config->ssid);
	return 0;
}

int wifi_hal_get_fw_stats(struct radio_context *ctx, char *buf, int buf_size, int index)
{
	int ret;
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
	ret = wifi_get_fw_stats(sc, buf, buf_size, index);
	if (ret)
		return -1;

	return 0;
}


int wifi_hal_capture_spectral_scan(struct radio_context *ctx, int index)
{
	int ret;
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	ret = wifi_capture_spectral_scan(sc, index);
	if (ret)
		return -1;

	return 0;
}

static int wifi_hal_ctrl_recv(struct wifi_softc *sc, int index, char *reply, size_t *reply_len)
{
	struct wpa_ctrl_ctx *ctx = (wpa_ctrl_ctx *)sc->wpa_ctx;
	int res, i;
	struct pollfd fds[WIFI_RADIO_MAX+1];
	int wpa_fd;

	for (i=0; i < WIFI_RADIO_MAX; i++) {
		if (!ctx[i].ctrl) {
			hal_err(HAL_DBG_WIFI, "ctrl socket not opened '%s'\n", WIFI_HAL_WPA_SOCK_PATH);
			return -1;
		}
	}

	memset(fds, 0, (WIFI_RADIO_MAX+1) * sizeof(struct pollfd));
	fds[0].fd = STDIN_FILENO;
	fds[0].events |= POLLIN;
	fds[0].revents = 0;

	for (i=1; i < WIFI_RADIO_MAX+1; i++) {
		wpa_fd = wpa_ctrl_get_fd(ctx[i-1].monitor);
		fds[i].fd = wpa_fd;
		fds[i].events |= POLLIN;
		fds[i].revents = 0;
	}

	res = poll(fds, sc->radio_amount+1, -1);
	if (res < 0) {
		hal_err(HAL_DBG_WIFI, "poll failed = %d", res);
		return res;
	}

	for (i=0; i < sc->radio_amount; i++) {
		if (wpa_ctrl_pending(ctx[i].monitor)>0) {
			hal_info(HAL_DBG_WIFI, "--- wpa_socket receive %d - %s ---\n", i, sc->nl_ctx.ifname[i]);
			res = wpa_ctrl_recv(ctx[i].monitor, reply, reply_len);
			return res;
		}
	}

	hal_err(HAL_DBG_WIFI, "sockets terminated already!\n");
	return -1;
}

static int wifi_hal_wait_on_event(struct wifi_softc *sc, int index, char *buf, size_t buflen)
{
	struct wpa_ctrl_ctx *ctx = (wpa_ctrl_ctx *)sc->wpa_ctx;
	size_t nread = buflen - 1;
	int result, i;

	for (i=0; i < WIFI_RADIO_MAX; i++) {
		if (!ctx[i].monitor) {
			hal_err(HAL_DBG_WIFI, "monitor Connection not opened\n");
			strncpy(buf, WPA_EVENT_TERMINATING " - connection closed", buflen - 1);
			buf[buflen - 1] = '\0';
			return (int) strlen(buf);
		}
	}

	/* To DO: Pass valid index during concurency */
	result = wifi_hal_ctrl_recv(sc, index, buf, &nread);
	if (result < 0) {
		hal_err(HAL_DBG_WIFI, "wifi_ctrl_recv failed: %s\n", strerror(errno));
		strncpy(buf, WPA_EVENT_TERMINATING " - recv error\n", buflen-1);
		buf[buflen-1] = '\0';
		return (int)strlen(buf);
	}

	buf[nread] = '\0';
	hal_info(HAL_DBG_WIFI, "WiFi HAL: wait_for_event: result=%d nread=%ld string=\"%s\"\n", result, nread, buf);
	/* Check for EOF on the socket */
	if (result == 0 && nread == 0) {
		hal_info(HAL_DBG_WIFI, "got EOF on monitor socket\n");
		strncpy(buf, WPA_EVENT_TERMINATING " - signal 0 received", buflen-1);
		buf[buflen-1] = '\0';
		return (int)strlen(buf);
	}
	/* strip verbose info from event */
	if (buf[0] == '<') {
		char *match = strchr(buf, '>');
		if (match != nullptr) {
			nread -= (match+1-buf);
			memmove(buf, match+1, nread+1);
		}
	}

	return (int)nread;
}

static int str_starts(const char *str, const char *start)
{
    return strncmp(str, start, strlen(start)) == 0;
}

static wifi_SystemEvent wifi_hal_map_wpa_event_to_state(char *event, int len) {

    if (str_starts(event, WPA_EVENT_DISCONNECTED)) {
        return DISCONNECTED_EVENT;
    } else if (str_starts(event, AP_EVENT_ENABLED)) {
        return AP_ENABLED_EVENT;
    } else if (str_starts(event, MESH_GROUP_STARTED)) {
        return MESH_GROUP_STARTED_EVENT;
    } else if (str_starts(event, MESH_PEER_CONNECTED)) {
        return CONNECTED_EVENT;
    } else if (str_starts(event, MESH_PEER_DISCONNECTED)) {
        return DISCONNECTED_EVENT;
//    } else if (str_starts(event, WPA_EVENT_CHANNEL_SWITCH_STARTED)) {
//        return error_event;
//    } else if (str_starts(event, AP_EVENT_DISABLED)) {
//        return error_event;
//    } else if (str_starts(event, MESH_GROUP_REMOVED)) {
//        return error_event;
//     } else if (str_starts(event, P2P_EVENT_GROUP_STARTED)) {
//        return error_event;
//    } else if (str_starts(event, P2P_EVENT_GROUP_REMOVED)) {
//        return error_event;
//    } else if (str_starts(event, P2P_EVENT_CROSS_CONNECT_ENABLE)) {
//        return error_event;
//    } else if (str_starts(event, P2P_EVENT_CROSS_CONNECT_DISABLE)) {
//        return error_event;
//    } else if (str_starts(event, P2P_EVENT_GO_NEG_FAILURE)) {
//        return error_event;
//    } else if (str_starts(event, WPS_EVENT_SUCCESS)) {
//        return error_event;
//    } else if (str_starts(event, WPS_EVENT_ACTIVE)) {
//        return error_event;
//    } else if (str_starts(event, WPS_EVENT_TIMEOUT)) {
//        return error_event;
//    } else if (str_starts(event, WPS_EVENT_FAIL)) {
//        return error_event;
//    } else if (str_starts(event, AP_STA_CONNECTED)) {
//        return error_event;
//    } else if (str_starts(event, AP_STA_DISCONNECTED)) {
//        return error_event;
//    } else if (str_starts(event, ESS_DISASSOC_IMMINENT)) {
//        return error_event;
//    } else if (str_starts(event, HS20_SUBSCRIPTION_REMEDIATION)) {
//        return error_event;
//    } else if (str_starts(event, HS20_DEAUTH_IMMINENT_NOTICE)) {
//        return error_event;
//    } else if (str_starts(event, HS20_T_C_ACCEPTANCE)) {
//        return error_event;
//    } else if (str_starts(event, DPP_EVENT_CONF_RECEIVED)) {
//        return error_event;
//    } else if (str_starts(event, DPP_EVENT_CONFOBJ_AKM)) {
//        return error_event;
//    } else if (str_starts(event, DPP_EVENT_CONFOBJ_SSID)) {
//        return error_event;
//    } else if (str_starts(event, DPP_EVENT_CONNECTOR)) {
//        return error_event;
//    } else if (str_starts(event, DPP_EVENT_CONFOBJ_PASS)) {
//        return error_event;
//    } else if (str_starts(event, DPP_EVENT_CONFOBJ_PSK)) {
//        return error_event;
//    } else if (str_starts(event, DPP_EVENT_C_SIGN_KEY)) {
//        return error_event;
//    } else if (str_starts(event, DPP_EVENT_NET_ACCESS_KEY)) {
//        return error_event;
    } else if (str_starts(event, WPA_EVENT_TERMINATING)) {
        return TERMINATE_EVENT;
    }
    return NO_EVENT;
}

static wifi_state connected_handler(struct radio_context *ctx, int index) {
    struct radio_generic_func *radio_ops;
    radio_ops = ctx->cmn.rd_func;

    hal_info(HAL_DBG_WIFI, "RSSI:%d dbm\n", (int8_t)radio_ops->radio_get_rssi(ctx, 1));
    return CONNECTED_STATE;
}

static wifi_state disconnected_handler(struct radio_context *ctx, int index) {
	struct radio_generic_func *radio_ops;
	char scan_results[4096] = {0};

	radio_ops = ctx->cmn.rd_func;

	/* for test purposes */
	hal_info(HAL_DBG_WIFI, "RSSI:%d dbm\n", (int8_t)radio_ops->radio_get_rssi(ctx, 1));
	radio_ops->radio_get_scan_results(ctx, scan_results, index);
	hal_info(HAL_DBG_WIFI, "SCAN RESULTS %s\n", scan_results);

    return DISCONNECTED_STATE;
}

static wifi_state init_handler(struct radio_context *ctx, int index) {
	struct radio_generic_func *radio_ops;
	struct wifi_config *config;
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	config = (struct wifi_config *)ctx->config[index];
	radio_ops = ctx->cmn.rd_func;

	if(wifi_debugfs_init(sc, index) != 0) {
		hal_err(HAL_DBG_WIFI, "debugfs initialisation failed!");
	}

	if (strncmp(config->mode, "ap", 2) == 0) {
		radio_ops->radio_create_ap(ctx, index);
	} else 	if (strncmp(config->mode, "sta", 3) == 0) {
		radio_ops->radio_connect_ap(ctx, index);
	} else if (strncmp(config->mode, "mesh", 4) == 0) {
		radio_ops->radio_join_mesh(ctx, index);
	} else {
		return UNKNOWN_STATE;
	}

	/* to get all wifi radios initialised */
	if (index < sc->radio_amount-1)
		return INIT_STATE;

	return IF_UP_STATE;
}

//wifi state machine definition TODO
static wifi_StateMachine wifi_asStateMachine[] =
{    // from          // event trigger    // event handler
    {INIT_STATE,         STARTUP_EVENT,      init_handler},
    {DISCONNECTED_STATE, CONNECTED_EVENT,    disconnected_handler},
    {CONNECTED_STATE,    DISCONNECTED_EVENT, connected_handler},
	{UNKNOWN_STATE,      NO_EVENT,           nullptr} // Don't remove this line
};

static void wifi_events(struct radio_context *ctx)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
	char resp_buf[RESP_BUFFER_SIZE] = {0};
	size_t len = sizeof(resp_buf) - 1;
	size_t nread = 0;
	bool loop = true;
	wifi_SystemEvent eNewEvent;

	int index = -1; // TODO used for initialisation loop

	sc->state = INIT_STATE;
	eNewEvent = STARTUP_EVENT;

	while (loop) {
		hal_info(HAL_DBG_WIFI, "EventState: %d\n", sc->state);
		if (sc->state != INIT_STATE) {
			// read next events
			nread = wifi_hal_wait_on_event(sc, index, resp_buf, len);
			eNewEvent = wifi_hal_map_wpa_event_to_state(resp_buf, nread);
		} else {
			index++;
			eNewEvent = STARTUP_EVENT;
		}

        if (eNewEvent == NO_EVENT) {
            continue;
        }

        if ((sc->state < LAST_STATE) && (eNewEvent < LAST_EVENT)) {
			int i = 0;
			// search from StateMachine
			while (wifi_asStateMachine[i].StateMachineEventHandler != nullptr) {
				if ((wifi_asStateMachine[i].StateMachineEvent == eNewEvent) &&   // is supported event in StateMachine
					(wifi_asStateMachine[i].StateMachine == sc->state)) {       // state transition is defined
					break;
				}
				i++;
			}
			if (wifi_asStateMachine[i].StateMachineEventHandler != nullptr) {
				sc->state = (*wifi_asStateMachine[i].StateMachineEventHandler)(ctx, index);
			}
			else
				hal_warn(HAL_DBG_WIFI, "Not defined state state!!  event=%d\n", eNewEvent);
		} else {
            hal_warn(HAL_DBG_WIFI, "Wifi state machine unknown event!!  event=%d\n", eNewEvent);
		}
    }
}

/* To Do: Check connection state */
static int wifi_hal_get_rssi (struct radio_context *ctx, int radio_index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	wifi_hal_get_interface(&sc->nl_ctx);
	wifi_hal_get_stainfo(&sc->nl_ctx, radio_index);

	return sc->signal[radio_index];
}

static struct radio_generic_func wifi_hal_ops = {
	.open = wifi_hal_open,
	.close = wifi_hal_close,
	.radio_get_hal_version = wifi_hal_get_hal_version,
	.radio_initialize = nullptr,
	.radio_wait_for_driver_ready = nullptr,
	.radio_cleanup = nullptr,
	.radio_event_loop = wifi_events,
	.radio_create_config = nullptr,
	.radio_enable = nullptr,
	.get_no_of_radio = nullptr,
	.radio_get_iface_name = wifi_hal_get_iface_name,
	.radio_get_supported_freq_band = nullptr,
	.radio_get_status = nullptr,
	.radio_get_feature_status = nullptr,
	.radio_get_supported_channels = nullptr,
	.radio_get_operating_channel = nullptr,
	.radio_get_mac_address = wifi_hal_get_mac_addr,
	.radio_get_rssi = wifi_hal_get_rssi,
	.radio_get_txrate = wifi_hal_get_txrate,
	.radio_get_rxrate = wifi_hal_get_rxrate,
	.radio_get_scan_results = wifi_hal_get_scan_results,
	.radio_connect_ap = wifi_hal_connect_ap,
	.radio_create_ap = wifi_hal_create_ap,
	.radio_join_mesh = wifi_hal_join_mesh,
	.radio_connect = nullptr, //modem interface
	.radio_get_fw_stats = wifi_hal_get_fw_stats,
	.radio_capture_spectral_data = wifi_hal_capture_spectral_scan,
};

__attribute__((unused)) int wifi_hal_register_ops(struct radio_context *ctx)
{
	ctx->cmn.rd_func = &wifi_hal_ops;

	return 0;
}

struct radio_context*  wifi_hal_attach()
{
	struct radio_context *ctx = nullptr;
	struct wifi_softc *sc = nullptr;
	int err = 0;

	ctx = (struct radio_context *)malloc(sizeof(struct radio_context));
	if (!ctx) {
		hal_err(HAL_DBG_WIFI, "failed to allocate radio hal ctx\n");
		return nullptr;
	}
	sc = (struct wifi_softc *)malloc(sizeof(struct wifi_softc));
	if (!sc) {
		hal_err(HAL_DBG_WIFI, "failed to allocate wifi softc ctx\n");
		err =  -ENOMEM;
		goto sc_alloc_failure;
	}

	for (int i = 0; i < WIFI_RADIO_MAX; i++) {
		sc->nl_ctx.ifname[i][0] = '\0';
		sc->nl_ctx.ifindex[i] = -1;
	}
	sc->radio_amount = 0;

	ctx->radio_private = (void*)sc;
	err = wifi_hal_nl80211_attach(sc);
	if (err) {
		hal_err(HAL_DBG_WIFI, "failed to attach with nl80211\n");
		goto nl_attach_failure;
	}

	err = wifi_hal_register_nl_cb(sc);
	if (err) {
		hal_err(HAL_DBG_WIFI, "failed to register nl80211 callback\n");
		goto nl_cb_attach_failure;
	}
	ctx->cmn.rd_func = &wifi_hal_ops;
	hal_info(HAL_DBG_WIFI, "WiFi HAL attach completed\n");

	return ctx;

nl_cb_attach_failure:
	wifi_hal_nl80211_dettach(sc);
nl_attach_failure:
	free(sc);
sc_alloc_failure:
	free(ctx);
	return nullptr;
}

int wifi_hal_dettach(struct radio_context *ctx)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	wifi_hal_nl80211_dettach(sc);

    free(sc);
	for (int i=0; i<RADIO_MAX_AMOUNT; i++)
		free(ctx->config[i]);
	free(ctx);

	hal_info(HAL_DBG_WIFI, "WiFi HAL detach completed\n");
	return 0;
}

