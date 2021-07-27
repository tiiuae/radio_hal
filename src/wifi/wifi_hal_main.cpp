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

static int nl80211_attach(struct netlink_ctx *nl_ctx)
{
	struct nl_sock *sock = nl_ctx->sock;
	int err;

	sock = nl_socket_alloc();
	if (sock == NULL) {
		printf("failed to alloc netlink socket\n");
		return -ENOMEM;
	}

	err = genl_connect(sock);
	if (err) {
		printf("failed to connect to genl\n");
		goto out;
	}

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

static void nl80211_dettach(struct netlink_ctx *nl_ctx)
{
	nl_socket_free(nl_ctx->sock);
}

int nl80211_tx_frame(struct netlink_ctx *nl_ctx, void *action_frame, size_t len)
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


int wifi_hal_attach(struct radio_context *ctx)
{
	struct wifi_sotftc *sc = NULL;
	int err = 0;

	ctx = (struct radio_context *)malloc(sizeof(struct radio_context));
	if (!ctx) {
		printf("failed to allocate radio hal ctx");
		return -ENOMEM;
	}

	sc = (struct wifi_sotftc *)malloc(sizeof(struct wifi_sotftc));
	if (!sc) {
		printf("failed to allocate wifi softc ctx");
		err =  -ENOMEM;
		goto sc_alloc_failure;
	}

	ctx->radio_private = (void*)sc;

	return 0;

sc_alloc_failure:
	free(ctx);
	return err;
}

int wifi_hal_dettach(struct radio_context *ctx)
{
	struct wifi_sotftc *sc = (struct wifi_sotftc *)ctx->radio_private;
	int err = 0;

	if (sc)
		free(sc);

	if (ctx)
		free(ctx);

	return err;
}
