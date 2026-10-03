#!/bin/sh
set -eu

IFACE=zeth

cleanup()
{
    echo "Cleaning up $IFACE..."

    firewall-cmd --zone=trusted --remove-interface="$IFACE" 2>/dev/null || true

    if ip link show "$IFACE" >/dev/null 2>&1; then
        ip link delete "$IFACE"
    fi
}

trap cleanup INT TERM EXIT

if ! ip link show "$IFACE" >/dev/null 2>&1; then
    ip tuntap add dev "$IFACE" mode tap user "$SUDO_USER"
fi

ip link set "$IFACE" up
ip addr flush dev "$IFACE"
ip addr add 192.0.2.1/24 dev "$IFACE"

firewall-cmd --zone=trusted --add-interface=zeth

dnsmasq \
    --interface="$IFACE" \
    --bind-interfaces \
    --dhcp-range=192.0.2.10,192.0.2.100,255.255.255.0,1h \
    --dhcp-option=3,192.0.2.1 \
    --dhcp-option=6,192.0.2.1 \
    --log-dhcp \
    --no-daemon
