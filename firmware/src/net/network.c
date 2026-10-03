#include "network.h"

#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_ip.h>

LOG_MODULE_REGISTER(termlet_network);

#define NETWORK_READY BIT(0)

static void log_address(struct net_if *iface) {
	char addr[NET_IPV4_ADDR_LEN];
	struct net_in_addr *ip;

	ip = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	if (ip == NULL)
		return;

	if (net_addr_ntop(AF_INET, ip, addr, sizeof(addr)) != NULL)
		LOG_INF("IPv4 ready: %s", addr);
}

static void start_dhcp(struct termlet_network *network) {
	LOG_INF("Starting DHCPv4 on interface %d",
	        net_if_get_by_iface(network->iface));

	net_dhcpv4_start(network->iface);
}

static void iface_event_handler(struct net_mgmt_event_callback *cb,
                                uint64_t event,
                                struct net_if *iface) {
	struct termlet_network *network =
	    CONTAINER_OF(cb, struct termlet_network, iface_cb);

	if (iface != network->iface)
		return;

	switch (event) {
		case NET_EVENT_IF_UP:
			LOG_INF("Network interface up");
			start_dhcp(network);
			break;

		case NET_EVENT_IF_DOWN:
			LOG_INF("Network interface down");
			k_event_clear(&network->events, NETWORK_READY);
			break;
	}
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t event,
                               struct net_if *iface) {
	struct termlet_network *network =
	    CONTAINER_OF(cb, struct termlet_network, ipv4_cb);

	if (iface != network->iface)
		return;

	switch (event) {
		case NET_EVENT_IPV4_DHCP_BOUND:
			log_address(iface);
			k_event_post(&network->events, NETWORK_READY);
			break;

		case NET_EVENT_IPV4_ADDR_DEL:
		case NET_EVENT_IPV4_DHCP_STOP:
			k_event_clear(&network->events, NETWORK_READY);
			LOG_INF("IPv4 unavailable");
			break;
	}
}

int termlet_network_init(struct termlet_network *network) {
	if (network == NULL)
		return -EINVAL;

	network->iface = net_if_get_default();
	if (network->iface == NULL)
		return -ENODEV;

	k_event_init(&network->events);

	net_mgmt_init_event_callback(
	    &network->iface_cb,
	    iface_event_handler,
	    NET_EVENT_IF_UP |
	        NET_EVENT_IF_DOWN);

	net_mgmt_init_event_callback(
	    &network->ipv4_cb,
	    ipv4_event_handler,
	    NET_EVENT_IPV4_DHCP_BOUND |
	        NET_EVENT_IPV4_ADDR_DEL |
	        NET_EVENT_IPV4_DHCP_STOP);

	net_mgmt_add_event_callback(&network->iface_cb);
	net_mgmt_add_event_callback(&network->ipv4_cb);

	LOG_INF("Using interface %d, currently %s",
	        net_if_get_by_iface(network->iface),
	        net_if_is_up(network->iface) ? "up" : "down");

	/*
	 * NET_EVENT_IF_UP may already have occurred before our callbacks
	 * were registered.
	 */
	if (net_if_is_up(network->iface))
		start_dhcp(network);

	return 0;
}

bool termlet_network_is_ready(struct termlet_network *network) {
	return (k_event_test(&network->events, NETWORK_READY) &
	        NETWORK_READY) != 0;
}

void termlet_network_wait_ready(struct termlet_network *network) {
	(void) k_event_wait(&network->events,
	                    NETWORK_READY,
	                    false,
	                    K_FOREVER);
}
