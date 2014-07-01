/*  (c) 2011 Thomas Schoebel-Theuer / 1&1 Internet AG */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include "light_strategy.h"
#include "../xio_bricks/xio_net.h"

static
char *_xio_translate_hostname(const char *name)
{
	struct mars_global *global = mars_global;
	char *res = brick_strdup(name);
	struct mars_dent *test;
	char *tmp;

	if (unlikely(!global))
		goto done;

	for (tmp = res; *tmp; tmp++) {
		if (*tmp == ':') {
			*tmp = '\0';
			break;
		}
	}

	tmp = path_make("/mars/ips/ip-%s", res);
	if (unlikely(!tmp))
		goto done;

	test = mars_find_dent(global, tmp);
	if (test && test->link_val) {
		XIO_DBG("'%s' => '%s'\n", tmp, test->link_val);
		brick_string_free(res);
		res = brick_strdup(test->link_val);
	} else {
		XIO_DBG("no translation for '%s'\n", tmp);
	}
	brick_string_free(tmp);

done:
	return res;
}

int xio_send_dent_list(struct xio_socket *sock, struct list_head *anchor)
{
	struct list_head *tmp;
	struct mars_dent *dent;
	int status = 0;

	for (tmp = anchor->next; tmp != anchor; tmp = tmp->next) {
		dent = container_of(tmp, struct mars_dent, dent_link);
		status = xio_send_struct(sock, dent, mars_dent_meta);
		if (status < 0)
			break;
	}
	if (status >= 0) { /*  send EOR */
		status = xio_send_struct(sock, NULL, mars_dent_meta);
	}
	return status;
}
EXPORT_SYMBOL_GPL(xio_send_dent_list);

int xio_recv_dent_list(struct xio_socket *sock, struct list_head *anchor)
{
	int status;

	for (;;) {
		struct mars_dent *dent = brick_zmem_alloc(sizeof(struct mars_dent));

		INIT_LIST_HEAD(&dent->dent_link);
		INIT_LIST_HEAD(&dent->brick_list);

		status = xio_recv_struct(sock, dent, mars_dent_meta);
		if (status <= 0) {
			xio_free_dent(dent);
			goto done;
		}
		list_add_tail(&dent->dent_link, anchor);
	}
done:
	return status;
}
EXPORT_SYMBOL_GPL(xio_recv_dent_list);

/***************** module init stuff ************************/

int __init init_sy_net(void)
{
	XIO_INF("init_sy_net()\n");
	xio_translate_hostname = _xio_translate_hostname;
	return 0;
}

void exit_sy_net(void)
{
	XIO_INF("exit_sy_net()\n");
}
