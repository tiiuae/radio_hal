#include <unistd.h>
#include <stdint.h>
#include "radio_hal.h"
#include "wifi_hal.h"
#include <netlink/attr.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/handlers.h>
#include <netlink/msg.h>
#include <netlink/genl/family.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <linux/if_ether.h>
#include <linux/nl80211.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "wifi_frame_helper.h"
#include "wpa_socket/wpa_ctrl.h"

#define WIFI_RADIO_HAL_MAJOR_VERSION 1
#define WIFI_RADIO_HAL_MINOR_VERSION 0
#define WIFI_HAL_WPA_SOCK_PATH "/var/run/wpa_supplicant/"
static const char *client_socket_dir = NULL;
static int debug;

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
                        sprintf(mac_addr+l, "%02x", arg[i]);
                        l += 2;
                } else {
                        sprintf(mac_addr+l, ":%02x", arg[i]);
                        l += 3;
                }
        }
}

int wifi_hal_run_sys_cmd(char *cmd, char *resp_buf, int resp_size)
{
    FILE *f;
    char *buf = resp_buf;
    int size=resp_size, resp_buf_bytes=0, readbytes=0;

    if((f = popen(cmd, "r")) == NULL) {
        printf("popen %s error\n", cmd);
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

        fgets(buf,resp_buf_bytes,f);
        readbytes=strlen(buf);
        if(!readbytes)
	    break;

        size-=readbytes;
        buf += readbytes;
    }
    pclose(f);
    resp_buf[resp_size-1]=0;

    if (debug)
	printf("sys cmd:%s resp:%s\n", cmd, resp_buf);

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

int wifi_hal_frequency_to_channel(int freq)
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

static void get_mac_addr(struct wifi_softc *sc, char *mac_addr)
{
	struct ifreq if_req;
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	unsigned char *mac;

	if_req.ifr_addr.sa_family = AF_INET;
	strncpy(if_req.ifr_name, sc->nl_ctx.ifname, RADIO_IFNAME_SIZE);
	ioctl(fd, SIOCGIFHWADDR, &if_req);
	close(fd);
	mac = (unsigned char*)if_req.ifr_hwaddr.sa_data;
	wifi_hal_mac_addr_n2a(sc->mac_addr, mac);
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
		  NULL);

	if (tb_msg[NL80211_ATTR_IFNAME]) {
		strcpy(nl_ctx->ifname, nla_get_string(tb_msg[NL80211_ATTR_IFNAME]));
	}

	if (tb_msg[NL80211_ATTR_IFINDEX]) {
		(nl_ctx->ifindex = nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]));
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
	static struct nla_policy sta_info[NL80211_STA_INFO_MAX + 1] = {0};
	static struct nla_policy rate_info[NL80211_RATE_INFO_MAX + 1] = {0};

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
            NULL);

	if (!tb_msg[NL80211_ATTR_STA_INFO]) {
		printf("failed to parse sta info\n");
		return NL_SKIP;
	}

	if (nla_parse_nested(sinfo, NL80211_STA_INFO_MAX,
		tb_msg[NL80211_ATTR_STA_INFO], sta_info)) {
		printf("failed to parse nested sta info\n");
		return NL_SKIP;
	}

	if (sinfo[NL80211_STA_INFO_SIGNAL]) {
		sc->signal = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]);
	}

	if (sinfo[NL80211_STA_INFO_TX_BITRATE])
	{
		if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX,
			sinfo[NL80211_STA_INFO_TX_BITRATE], rate_info))
		{
			printf("failed to parse nested rate attributes!\n");
		}
		else {
			if (rinfo[NL80211_RATE_INFO_BITRATE]) {
				sc->txrate = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE])/10;
			}
		}
	}

	if (sinfo[NL80211_STA_INFO_RX_BITRATE])
	{
		if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX,
			sinfo[NL80211_STA_INFO_RX_BITRATE], rate_info))
		{
			printf("failed to parse nested rate attributes!\n");
		}
		else {
			if (rinfo[NL80211_RATE_INFO_BITRATE]) {
				sc->rxrate = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE])/10;
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
		printf("failed to allocate if NL callback.\n");
		return -ENOMEM;
	}

	nl_ctx->link_info_cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!nl_ctx->link_info_cb)
	{
		printf("failed to allocate link info NL callback.\n");
		return -ENOMEM;
	}

	nl_cb_set(nl_ctx->if_cb, NL_CB_VALID , NL_CB_CUSTOM, wifi_hal_ifname_resp_hdlr, sc);
	nl_cb_set(nl_ctx->if_cb, NL_CB_FINISH, NL_CB_CUSTOM, wifi_hal_nl_finish_handler, &(nl_ctx->if_cb_err));
	nl_cb_set(nl_ctx->link_info_cb, NL_CB_VALID , NL_CB_CUSTOM, wifi_hal_connection_info_hdlr, sc);
	nl_cb_set(nl_ctx->link_info_cb, NL_CB_FINISH, NL_CB_CUSTOM, wifi_hal_nl_finish_handler, &(nl_ctx->linkinfo_cb_err));

	return 0;
}

static int wifi_hal_switch_channel(struct netlink_ctx *nl_ctx, char *channel,
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
	int i;
	char *end;
        int err = 0;
        struct nl_msg* ch_sw_msg = nlmsg_alloc();
	int chan;

        if (!ch_sw_msg) {
                printf("failed to allocate ch switch NL message.\n");
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

	chan = strtoul(channel, &end, 10);
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
	err = nl_send_auto(nl_ctx->sock, ch_sw_msg);
	while (nl_ctx->csa_cb_err > 0)
	{
		nl_recvmsgs(nl_ctx->sock, nl_ctx->gen_nl_cb);
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
		printf("failed to alloc netlink socket\n");
		return -ENOMEM;
	}
	sock = nl_ctx->sock;
	err = genl_connect(sock);
	if (err) {
		printf("failed to connect to genl\n");
		goto out;
	}

	nl_ctx->nl80211_id = -1;
	nl_ctx->nl80211_id = genl_ctrl_resolve(sock, "nl80211");
	if (nl_ctx->nl80211_id < 0) {
		printf("nl80211 ctrl resolve failed\n");
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
}

int wifi_hal_nl80211_tx_frame(struct netlink_ctx *nl_ctx, void *action_frame, size_t len)
{
	char cmd = NL80211_CMD_FRAME;
	struct nl_msg *nl_msg = NULL;
	struct nl_cb *s_cb;
	int err = -1;

	s_cb = nl_cb_alloc(NL_CB_DEBUG);
	if (!s_cb) {
		printf("failed to allocate nl callback\n");
		err = -ENOMEM;
		goto nla_put_failure;
	}
	nl_socket_set_cb(nl_ctx->sock, s_cb);

	nl_msg = nlmsg_alloc();
	if (!nl_msg) {
		printf("failed to allocate netlink message\n");
		err = -ENOMEM;
		goto nla_put_failure;
	}

	genlmsg_put(nl_msg, 0, 0, nl_ctx->nl80211_id, 0, 0, cmd, 0);
	NLA_PUT_U32(nl_msg, NL80211_ATTR_IFINDEX, nl_ctx->ifindex);
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
		printf("failed to allocate if get NL  message.\n");
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
	while (nl_ctx->if_cb_err > 0)
	{
		nl_recvmsgs(nl_ctx->sock, nl_ctx->if_cb);
	}
	nlmsg_free(if_get_msg);

	if (nl_ctx->ifindex < 0)
		return -EINVAL;

	return 0;
}

static int wifi_hal_get_stainfo(struct netlink_ctx *nl_ctx)
{
	struct nl_msg* sta_info_msg = nlmsg_alloc();
	int err = 0;

	if (!sta_info_msg) {
		printf("failed to allocate sta info  NL  message.\n");
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
	nla_put_u32(sta_info_msg, NL80211_ATTR_IFINDEX, nl_ctx->ifindex);
	err = nl_send_auto(nl_ctx->sock, sta_info_msg);
	while (nl_ctx->linkinfo_cb_err > 0)
	{
		nl_recvmsgs(nl_ctx->sock, nl_ctx->link_info_cb);
	}
	nlmsg_free(sta_info_msg);

	return 0;
}

static int wifi_hal_get_hal_version(char *version)
{
	snprintf(version, 32, "%d.%d", WIFI_RADIO_HAL_MAJOR_VERSION, WIFI_RADIO_HAL_MINOR_VERSION);

	return 0;
}

int wifi_hal_get_iface_name(struct radio_context *ctx, char *name, int radio_index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	memcpy(name, sc->nl_ctx.ifname, RADIO_IFNAME_SIZE);

	return 0;
}



static int wifi_hal_get_txrate (struct radio_context *ctx, int radio_index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
	
	wifi_hal_get_interface(&sc->nl_ctx);
	wifi_hal_get_stainfo(&sc->nl_ctx);

	return sc->txrate;
}

static int wifi_hal_get_rxrate (struct radio_context *ctx, int radio_index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
	
	wifi_hal_get_interface(&sc->nl_ctx);
	wifi_hal_get_stainfo(&sc->nl_ctx);

	return sc->rxrate;
}

static int wifi_hal_wpa_mesh_attach(struct wifi_softc *sc)
{
	struct wpa_ctrl_ctx *ctx = &sc->wpa_ctx;
	char sock_path[64] = {0};
	int len = 0;

	len += sprintf(sock_path, WIFI_HAL_WPA_SOCK_PATH);
	/* To do: remove harcoded path */
	len += sprintf(sock_path + len, (const char *)("mesh0"));
	ctx->mesh_ctrl = wpa_ctrl_open2(sock_path, client_socket_dir);
	if (!ctx->mesh_ctrl) {
		printf("Couldn't open '%s'\n", sock_path);
		return -1;
	}

	return 0;
}

static int wifi_hal_wpa_attach(struct wifi_softc *sc)
{
	struct wpa_ctrl_ctx *ctx = &sc->wpa_ctx;
	char sock_path[64] = {0};
	int len = 0;

	len += sprintf(sock_path, WIFI_HAL_WPA_SOCK_PATH);
	len += sprintf(sock_path + len, (const char *)(sc->nl_ctx.ifname));
	ctx->ctrl = wpa_ctrl_open2(sock_path, client_socket_dir);
	if (!ctx->ctrl) {
		printf("Couldn't open '%s'\n", sock_path);
		return -1;
	}
	ctx->fd = wpa_ctrl_get_fd(ctx->ctrl);

	ctx->monitor = wpa_ctrl_open2(sock_path, client_socket_dir);
	if (!ctx->monitor) {
		printf("Couldn't open '%s'\n", sock_path);
		wpa_ctrl_close(ctx->ctrl);
		return -1;
	}

	if (wpa_ctrl_attach(ctx->monitor) < 0) {
		printf("wpa_ctrl monitor attach failed");
		wpa_ctrl_close(ctx->monitor);
		wpa_ctrl_close(ctx->ctrl);
		return -1;
	}


	return 0;
}

static void wifi_hal_wpa_dettach(struct wifi_softc *sc)
{
	struct wpa_ctrl_ctx *ctx = &sc->wpa_ctx;

	wpa_ctrl_close(ctx->monitor);
	wpa_ctrl_close(ctx->ctrl);
}

static int wifi_hal_open(struct radio_context *ctx, enum radio_type type)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
	int err;

	err = wifi_hal_get_interface(&sc->nl_ctx);
	if (err) {
		printf("failed to get interface \n");
		return err;
	}

	printf("wifi interface: %s , interface index = %d\n", sc->nl_ctx.ifname, sc->nl_ctx.ifindex);

	err = wifi_hal_wpa_attach(sc);
	if (err) {
		printf("wpa_attach failed \n");
		return err;
	}

	return 0;
}

static int wifi_hal_close(struct radio_context *ctx, enum radio_type type)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	wifi_hal_wpa_dettach(sc);

	memset(sc->nl_ctx.ifname, 0,  RADIO_IFNAME_SIZE);
	sc->nl_ctx.ifindex = 0;

	return 0;
}

static int wifi_hal_get_mac_addr(struct radio_context *ctx, char *mac_addr, int radio_index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	get_mac_addr(sc, mac_addr);
	return 0;
}

static int wifi_hal_send_wpa_command(struct wpa_ctrl_ctx *ctx, int index, const char *cmd, char *resp, size_t *resp_size)
{
	int ret;

	if (!ctx->ctrl) {
		printf("ctrl socket not connected '%s' and cmd drooped:%s\n", WIFI_HAL_WPA_SOCK_PATH, cmd);
		return -1;
	}

	ret = wpa_ctrl_request(ctx->ctrl, cmd, strlen(cmd), resp, resp_size, NULL);
	if (ret == -2) {
		printf("'%s' command timed out.\n", cmd);
		return -2;
	} else if (ret < 0 || strncmp(resp, "FAIL", 4) == 0) {
		return -1;
	}

        if (debug) {
                resp[*resp_size] = '\0';
                printf("%s:%s\n", cmd, resp);
                if (*resp_size > 0 && resp[*resp_size - 1] != '\n')
                        printf("\n");
        }

	return 0;
}

static int wifi_hal_send_wpa_mesh_command(struct wpa_ctrl_ctx *ctx, int index, const char *cmd, char *resp, size_t *resp_size)
{
	int ret;

	if (!ctx->mesh_ctrl) {
		printf("ctrl socket not connected '%s' and cmd drooped:%s\n", WIFI_HAL_WPA_SOCK_PATH, cmd);
		return -1;
	}

	ret = wpa_ctrl_request(ctx->mesh_ctrl, cmd, strlen(cmd), resp, resp_size, NULL);
	if (ret == -2) {
		printf("'%s' command timed out.\n", cmd);
		return -2;
	} else if (ret < 0 || strncmp(resp, "FAIL", 4) == 0) {
		return -1;
	}

        if (debug) {
                resp[*resp_size] = '\0';
                printf("%s:%s\n", cmd, resp);
                if (*resp_size > 0 && resp[*resp_size - 1] != '\n')
                        printf("\n");
        }

	return 0;
}

static int wifi_hal_trigger_scan(struct wifi_softc *sc)
{
        char buf[2048];
        size_t len = 0;

	len = sizeof(buf) - 1;
        wifi_hal_send_wpa_command(&sc->wpa_ctx, 0, "SCAN", buf, &len);
        if (len)
                printf("SCAN TRIGGER:%s\n", buf);

	return 0;
}

static int wifi_hal_get_scan_results(struct radio_context *ctx, char *results)
{
        struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
        char buf[2048];
        size_t len = 0;

	len = sizeof(buf) - 1;
	wifi_hal_trigger_scan(sc);
        wifi_hal_send_wpa_command(&sc->wpa_ctx, 0, "SCAN_RESULTS", buf, &len);
        if (len && debug)
                printf("SCAN RESULTS:%s\n", buf);

	memcpy(results, buf, len);

	return 0;
}

static int wifi_hal_connect_ap(struct radio_context *ctx, char *ssid, char *psk)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
        char cmd_buf[2048] = {0};
        char resp_buf[2048] = {0};
	char nw_id[6] = {0};
        size_t len = 0;
	int ret = 0;

	len = sizeof(cmd_buf) - 1;
	strcpy(cmd_buf, "REMOVE_NETWORK all");

        ret = wifi_hal_send_wpa_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
        if (ret < 0) {
		printf("Fail to remove existing NW\n");
		return ret;
	}

	len = sizeof(cmd_buf) - 1;
	strcpy(cmd_buf, "ADD_NETWORK");

        ret = wifi_hal_send_wpa_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
        if (ret < 0) {
		printf("Fail to add NW\n");
		return ret;
	}

	if (len) {
		/* To Do: Add check */
		strncpy(nw_id, resp_buf, 6);
	}
	printf("NW ID:%s\n", nw_id);
	memset(cmd_buf, 0, 2048);
	len = 0;
	strcat(cmd_buf, "SET_NETWORK ");
	strncat(cmd_buf, nw_id, strlen(nw_id) - 1);
	strcat(cmd_buf, " ssid ");
	strncat(cmd_buf, ssid, strlen(ssid));
	printf("CMD:%s\n", cmd_buf);
	len = sizeof(cmd_buf) - 1;
        wifi_hal_send_wpa_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
        if (ret < 0) {
		printf("Fail to add NW\n");
		return ret;
	}

        if (len) {
		if (strncmp(resp_buf, "OK", 2) == 0) {
			memset(cmd_buf, 0, 2048);
			snprintf(cmd_buf, 2048, "%s%s%s%s", "SET_NETWORK ", nw_id, " psk ", psk);
			printf("CMD:%s\n", cmd_buf);
			len = sizeof(cmd_buf) - 1;
			wifi_hal_send_wpa_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
		} else {
			printf("failed to set psk\n");
			return -1;
		}
		if (strncmp(resp_buf, "OK", 2) == 0) {
			memset(cmd_buf, 0, 2048);
			snprintf(cmd_buf, 2048, "%s%s", "ENABLE_NETWORK ", nw_id);
			printf("CMD:%s\n", cmd_buf);
			len = sizeof(cmd_buf) - 1;
			wifi_hal_send_wpa_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
		} else {
			printf("failed to enable network\n");
			return -1;
		}
	}

	return 0;
}

static int wifi_hal_get_phyname(struct wifi_softc *sc, char *cmd, char *resp_buf, int resp_size)
{
	int ret;

        sprintf(cmd, "echo $(iw dev %s info | awk '/wiphy/ {printf \"phy\" $2}')", sc->nl_ctx.ifname);
        ret = wifi_hal_run_sys_cmd(cmd, resp_buf, resp_size);
        if (ret) {
                printf("failed to kill supplicant\n");
                return -1;
        }

	return 0;
}

static int wifi_hal_join_mesh(struct radio_context *ctx, char *ssid, char *psk, char *freq)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
        char cmd_buf[2048] = {0};
        char resp_buf[2048] = {0};
	char nw_id[6] = {0};
        size_t len = 0;
	int ret = 0;

	/* To do: Back up  AP config and Add support for SAP + MESH later */

	/* restart wpa_supplicant on mesh interface */
	memset(cmd_buf, 0, 2048);
	sprintf(cmd_buf, "killall wpa_supplicant");
	ret = system(cmd_buf);
	if (ret) {
		printf("failed to kill supplicant\n");
		return -1;
	}

	if (!(strncmp(sc->nl_ctx.ifname, "mesh0", 5) == 0)) {
		memset(cmd_buf, 0, 2048);
		sprintf(cmd_buf, "iw dev %s del", sc->nl_ctx.ifname);
		len = sizeof(cmd_buf) - 1;
		ret = wifi_hal_run_sys_cmd(cmd_buf, resp_buf, len);
		if (ret) {
			printf("failed to delete default managed interface\n");
			return -1;
		}
	}

	system("sleep 1");
	wifi_hal_get_phyname(sc, cmd_buf, resp_buf, len);
	if (!(strncmp(sc->nl_ctx.ifname, "mesh0", 5) == 0)) {
		memset(cmd_buf, 0, 2048);
		/* To Do: fix phyname with resp buffer(TBD) */
		sprintf(cmd_buf, "iw phy phy0 interface add mesh0 type mp");
		len = sizeof(cmd_buf) - 1;
		ret = wifi_hal_run_sys_cmd(cmd_buf, resp_buf, len);
		if (ret) {
			printf("failed to add mesh0 if\n");
			return -1;
		}

#ifdef CONFIG_PROVISON_MACADDR
		/* To Do: Check if we need to provison later with SAP + STA */
		memset(cmd_buf, 0, 2048);
		sprintf(cmd_buf, "ifconfig mesh0 hw ether 00:11:22:33:44:55");
		len = sizeof(cmd_buf) - 1;
		ret = wifi_hal_run_sys_cmd(cmd_buf, resp_buf, len);
		if (ret) {
			printf("failed to set mac addr\n");
			return -1;
		}
#endif
		memset(cmd_buf, 0, 2048);
		sprintf(cmd_buf, "ifconfig mesh0 up");
		len = sizeof(cmd_buf) - 1;
		ret = wifi_hal_run_sys_cmd(cmd_buf, resp_buf, len);
		if (ret) {
			printf("failed to up mesh0 if\n");
			return -1;
		}
	}

	memset(cmd_buf, 0, 2048);
	sprintf(cmd_buf, "rm /var/run/wpa_supplicant/mesh0");
	ret = wifi_hal_run_sys_cmd(cmd_buf, resp_buf, len);
	if (ret) {
		printf("failed to remove supplicant socket\n");
	}

	memset(cmd_buf, 0, 2048);
	sprintf(cmd_buf, "wpa_supplicant -B -imesh0 -c/etc/wpa_supplicant/wpa_supplicant-mesh.conf");
	len = sizeof(cmd_buf) - 1;
	ret = system(cmd_buf);
	//ret = execlp("wpa_supplicant", "wpa_supplicant", "-Dnl80211", "-B", "-imesh0", "-c/etc/wpa_supplicant/wpa_supplicant-mesh.conf", "-dd", NULL);
	if (ret) {
		printf("failed to start supplicant in mesh0 interface\n");
		return -1;
	}

	system("sleep 1");

	ret = wifi_hal_wpa_mesh_attach(sc);
	if (ret) {
		printf("wpa_suplicant mesh attach failed\n");
		return -1;
	}

	len = sizeof(cmd_buf) - 1;
	strcpy(cmd_buf, "REMOVE_NETWORK all");

        ret = wifi_hal_send_wpa_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
        if (ret < 0) {
		/* To Do: remove this once concurrency supported */
		printf("Fail to remove existing NW\n");
	}

	memset(cmd_buf, 0, 2048);
	sprintf(cmd_buf, "ADD_NETWORK");
	len = sizeof(cmd_buf) - 1;
	ret = wifi_hal_send_wpa_mesh_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
	if (ret < 0) {
		printf("mesh0 add network failed\n");
		return -1;
	} else {
		/* To Do: Add check */
		strncpy(nw_id, resp_buf, 6);
	}

	memset(cmd_buf, 0, 2048);
	sprintf(cmd_buf, "SET_NETWORK %s mode 5", nw_id);
	len = sizeof(cmd_buf) - 1;
	ret = wifi_hal_send_wpa_mesh_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
	if (ret || !strncmp(resp_buf, "OK", 2) == 0) {
		printf("failed to set mesh mode\n");
		return -1;
	}

	memset(cmd_buf, 0, 2048);
	sprintf(cmd_buf, "SET_NETWORK %s ssid %s", nw_id, ssid);
	len = sizeof(cmd_buf) - 1;
	ret = wifi_hal_send_wpa_mesh_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
	if (ret || !strncmp(resp_buf, "OK", 2) == 0) {
		printf("failed to set mesh ssid\n");
		return -1;
	}

	memset(cmd_buf, 0, 2048);
	sprintf(cmd_buf, "SET_NETWORK %s frequency %s", nw_id, freq);
	len = sizeof(cmd_buf) - 1;
	ret = wifi_hal_send_wpa_mesh_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
	if (ret || !strncmp(resp_buf, "OK", 2) == 0) {
		printf("failed to set mesh freq\n");
		return -1;
	}

	memset(cmd_buf, 0, 2048);
	sprintf(cmd_buf, "SET_NETWORK %s key_mgmt SAE", nw_id);
	len = sizeof(cmd_buf) - 1;
	ret = wifi_hal_send_wpa_mesh_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
	if (ret || !strncmp(resp_buf, "OK", 2) == 0) {
		printf("failed to set mesh key mgmt\n");
		return -1;
	}

	memset(cmd_buf, 0, 2048);
	sprintf(cmd_buf, "SET_NETWORK %s psk %s", nw_id, psk);
	len = sizeof(cmd_buf) - 1;
	ret = wifi_hal_send_wpa_mesh_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
	if (ret || !strncmp(resp_buf, "OK", 2) == 0) {
		printf("failed to set mesh psk\n");
		return -1;
	}

	memset(cmd_buf, 0, 2048);
	sprintf(cmd_buf, "MESH_GROUP_ADD %s", nw_id);
	len = sizeof(cmd_buf) - 1;
	ret = wifi_hal_send_wpa_mesh_command(&sc->wpa_ctx, 0, cmd_buf, resp_buf, &len);
	if (ret || !strncmp(resp_buf, "OK", 2) == 0) {
		printf("failed to enable mesh0\n");
		return -1;
	}

	return 0;
}

static int wifi_hal_ctrl_recv(struct wpa_ctrl_ctx *ctx, int index, char *reply, size_t *reply_len)
{
	int res;
	struct pollfd fds[2];
	int wpa_fd;

	if (!ctx->ctrl) {
		printf("ctrl socket noy opened '%s'\n", WIFI_HAL_WPA_SOCK_PATH);
		return -1;
	}

	wpa_fd = wpa_ctrl_get_fd(ctx->ctrl);
	memset(fds, 0, 2 * sizeof(struct pollfd));
	fds[0].fd = STDIN_FILENO;
	fds[0].events |= POLLIN;
	fds[0].revents = 0;
	fds[1].fd = wpa_fd;
	fds[1].events |= POLLIN;
	fds[1].revents = 0;
	res = poll(fds, 2, -1);
	if (res < 0) {
		printf("poll failed = %d", res);
		return res;
	}

	if (fds[0].revents & POLLIN) {
		return wpa_ctrl_recv(ctx->monitor, reply, reply_len);
	} else {
		printf("socket terminated already!");
		return -1;
	}

	return 0;
}

static int wifi_hal_wait_on_event(struct wpa_ctrl_ctx *ctx, int index, char *buf, size_t buflen)
{
	size_t nread = buflen - 1;
	int result;

	if (!ctx->monitor)
	{
		printf("monitor Connection not opened\n");
		strncpy(buf, WPA_EVENT_TERMINATING " - connection closed", buflen-1);
		buf[buflen-1] = '\0';
		return strlen(buf);
	}

	/* To DO: Pass valid index during concurency */
	result = wifi_hal_ctrl_recv(ctx, 0, buf, &nread);
	if (result < 0) {
		printf("wifi_ctrl_recv failed: %s\n", strerror(errno));
		strncpy(buf, WPA_EVENT_TERMINATING " - recv error", buflen-1);
		buf[buflen-1] = '\0';
		return strlen(buf);
	}

	buf[nread] = '\0';
	printf("WiFi HAL: wait_for_event: result=%d nread=%d string=\"%s\"\n", result, nread, buf);
	/* Check for EOF on the socket */
	if (result == 0 && nread == 0) {
		printf("got EOF on monitor socket\n");
		strncpy(buf, WPA_EVENT_TERMINATING " - signal 0 received", buflen-1);
		buf[buflen-1] = '\0';
		return strlen(buf);
	}
	/* strip verbose info from event */
	if (buf[0] == '<') {
	char *match = strchr(buf, '>');
		if (match != NULL) {
		nread -= (match+1-buf);
		memmove(buf, match+1, nread+1);
		}
	}

	return nread;
}


/* To Do: Check connection state */
static int wifi_hal_get_rssi (struct radio_context *ctx, int radio_index)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;

	wifi_hal_get_interface(&sc->nl_ctx);
	wifi_hal_get_stainfo(&sc->nl_ctx);

	return sc->signal;
}

static struct radio_generic_func wifi_hal_ops = {
	.open = wifi_hal_open,
	.close = wifi_hal_close,
	.radio_get_hal_version = wifi_hal_get_hal_version,
	.radio_initialize = NULL,
	.radio_wait_for_driver_ready = NULL,
	.radio_cleanup = NULL,
	.radio_event_loop = NULL,
	.radio_create_config = NULL,
	.radio_enable = NULL,
	.get_no_of_radio = NULL,
	.radio_get_iface_name = wifi_hal_get_iface_name,
	.radio_get_supported_freq_band = NULL,
	.radio_get_status = NULL,
	.radio_get_feature_status = NULL,
	.radio_get_supported_channels = NULL,
	.radio_get_operating_channel = NULL,
	.radio_get_mac_address = wifi_hal_get_mac_addr,
	.radio_get_rssi = wifi_hal_get_rssi,
	.radio_get_txrate = wifi_hal_get_txrate,
	.radio_get_rxrate = wifi_hal_get_rxrate,
	.radio_get_scan_results = wifi_hal_get_scan_results,
	.radio_connect_ap = wifi_hal_connect_ap,
	.radio_join_mesh = wifi_hal_join_mesh,
};

int wifi_hal_register_ops(struct radio_context *ctx)
{
	ctx->cmn.rd_func = &wifi_hal_ops;

	return 0;
}

struct radio_context*  wifi_hal_attach()
{
	struct radio_context *ctx = NULL;
	struct wifi_softc *sc = NULL;
	int err = 0;

	ctx = (struct radio_context *)malloc(sizeof(struct radio_context));
	if (!ctx) {
		printf("failed to allocate radio hal ctx\n");
		return NULL;
	}
	sc = (struct wifi_softc *)malloc(sizeof(struct wifi_softc));
	if (!sc) {
		printf("failed to allocate wifi softc ctx\n");
		err =  -ENOMEM;
		goto sc_alloc_failure;
	}

	ctx->radio_private = (void*)sc;
	err = wifi_hal_nl80211_attach(sc);
	if (err) {
		printf("failed to attach with nl80211\n");
		goto nl_attach_failure;
	}

	err = wifi_hal_register_nl_cb(sc);
	if (err) {
		printf("failed to register nl80211 callback\n");
		goto nl_cb_attach_failure;
	}
	ctx->cmn.rd_func = &wifi_hal_ops;
	printf("WiFi HAL attach completed\n");

	return ctx;

nl_cb_attach_failure:
	wifi_hal_nl80211_dettach(sc);
nl_attach_failure:
	free(sc);
sc_alloc_failure:
	free(ctx);
	return NULL;
}

int wifi_hal_dettach(struct radio_context *ctx)
{
	struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
	int err = 0;

	wifi_hal_nl80211_dettach(sc);
	if (sc)
		free(sc);

	if (ctx)
		free(ctx);

	printf("WiFi HAL dettach completed\n");
	return err;
}

