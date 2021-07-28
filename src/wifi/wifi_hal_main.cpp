#include "radio_hal.h"
#include "wifi_hal.h"
#include <netlink/attr.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/handlers.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <linux/nl80211.h>

static int wifi_hal_nl_finish_handler(struct nl_msg *msg, void *arg)
{
	int *err = (int *)arg;

	*err = 0;
	return NL_SKIP;
}

static int wifi_hal_ifname_resp_hdlr(struct nl_msg *msg, void *arg)
{
	struct netlink_ctx *nl_ctx = (struct netlink_ctx *)arg;
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

static int wifi_hal_register_nl_cb(struct netlink_ctx *nl_ctx)
{
	nl_ctx->if_cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!nl_ctx->if_cb)
	{
		printf("failed to allocate if NL callback.\n");
		return -ENOMEM;
	}

	nl_cb_set(nl_ctx->if_cb, NL_CB_VALID , NL_CB_CUSTOM, wifi_hal_ifname_resp_hdlr, nl_ctx);
	nl_cb_set(nl_ctx->if_cb, NL_CB_FINISH, NL_CB_CUSTOM, wifi_hal_nl_finish_handler, &(nl_ctx->if_cb_err));

	return 0;
}

static int wifi_hal_nl80211_attach(struct netlink_ctx *nl_ctx)
{
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

static void wifi_hal_nl80211_dettach(struct netlink_ctx *nl_ctx)
{
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

int wifi_hal_attach(struct radio_context *ctx)
{
	struct wifi_sotftc *sc = NULL;
	int err = 0;

	ctx = (struct radio_context *)malloc(sizeof(struct radio_context));
	if (!ctx) {
		printf("failed to allocate radio hal ctx\n");
		return -ENOMEM;
	}

	sc = (struct wifi_sotftc *)malloc(sizeof(struct wifi_sotftc));
	if (!sc) {
		printf("failed to allocate wifi softc ctx\n");
		err =  -ENOMEM;
		goto sc_alloc_failure;
	}

	ctx->radio_private = (void*)sc;
	err = wifi_hal_nl80211_attach(&sc->nl_ctx);
	if (err) {
		printf("failed to attach with nl80211\n");
		goto nl_attach_failure;
	}

	err = wifi_hal_register_nl_cb(&sc->nl_ctx);
	if (err) {
		printf("failed to register nl80211 callback\n");
		goto nl_cb_attach_failure;
	}

	printf("WiFi HAL attach completed\n");

	return 0;

nl_cb_attach_failure:
	wifi_hal_nl80211_dettach(&sc->nl_ctx);
nl_attach_failure:
	free(sc);
sc_alloc_failure:
	free(ctx);
	return err;
}

int wifi_hal_dettach(struct radio_context *ctx)
{
	struct wifi_sotftc *sc = (struct wifi_sotftc *)ctx->radio_private;
	int err = 0;

	wifi_hal_nl80211_dettach(&sc->nl_ctx);
	if (sc)
		free(sc);

	if (ctx)
		free(ctx);

	printf("WiFi HAL dettach completed\n");
	return err;
}
