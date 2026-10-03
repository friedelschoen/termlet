#pragma once

#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>

struct termlet_network {
	struct net_if *iface;
	struct net_mgmt_event_callback iface_cb;
	struct net_mgmt_event_callback ipv4_cb;
	struct k_event events;
};

int termlet_network_init(struct termlet_network *network);

bool termlet_network_is_ready(struct termlet_network *network);

/*
 * Block until IPv4 connectivity is available.
 */
void termlet_network_wait_ready(struct termlet_network *network);
