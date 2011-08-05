// (c) 2011 Thomas Schoebel-Theuer / 1&1 Internet AG

//#define BRICK_DEBUGGING
//#define MARS_DEBUGGING
//#define IO_DEBUGGING

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include "strategy.h"
#include "../mars_net.h"

static
char *_mars_translate_hostname(const char *name)
{
	struct mars_global *global = mars_global;
	const char *res = name;
	struct mars_dent *test;
	char *tmp;

	if (unlikely(!global)) {
		goto done;
	}
	tmp = path_make("/mars/ips/ip-%s", name);
	if (unlikely(!tmp)) {
		goto done;
	}

	test = mars_find_dent(global, tmp);
	if (test && test->new_link) {
		MARS_DBG("'%s' => '%s'\n", tmp, test->new_link);
		res = test->new_link;
	}
	kfree(tmp);

done:
	return kstrdup(res, GFP_MARS);
}

int mars_send_dent_list(struct socket **sock, struct list_head *anchor)
{
	struct list_head *tmp;
	struct mars_dent *dent;
	int status = 0;
	for (tmp = anchor->next; tmp != anchor; tmp = tmp->next) {
		dent = container_of(tmp, struct mars_dent, dent_link);
		status = mars_send_struct(sock, dent, mars_dent_meta);
		if (status < 0)
			break;
	}
	if (status >= 0) { // send EOF
		status = mars_send_struct(sock, NULL, mars_dent_meta);
	}
	return status;
}
EXPORT_SYMBOL_GPL(mars_send_dent_list);

int mars_recv_dent_list(struct socket **sock, struct list_head *anchor)
{
	int status;
	for (;;) {
		struct mars_dent *dent = kzalloc(sizeof(struct mars_dent), GFP_MARS);
		if (!dent)
			return -ENOMEM;

		//MARS_IO("\n");

		status = mars_recv_struct(sock, dent, mars_dent_meta);
		if (status <= 0) {
			kfree(dent);
			goto done;
		}
		list_add_tail(&dent->dent_link, anchor);
		INIT_LIST_HEAD(&dent->brick_list);
	}
done:
	return status;
}
EXPORT_SYMBOL_GPL(mars_recv_dent_list);


////////////////// module init stuff /////////////////////////


static int __init _init_sy_net(void)
{
	MARS_INF("init_sy_net()\n");
	mars_translate_hostname = _mars_translate_hostname;
	return 0;
}

static void __exit _exit_sy_net(void)
{
	MARS_INF("exit_sy_net()\n");
}

MODULE_DESCRIPTION("MARS network infrastructure");
MODULE_AUTHOR("Thomas Schoebel-Theuer <tst@1und1.de>");
MODULE_LICENSE("GPL");

module_init(_init_sy_net);
module_exit(_exit_sy_net);