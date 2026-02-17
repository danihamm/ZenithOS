#!/bin/bash
# Tears down the bridge + TAP setup and restores the physical interface.
# Requires root.

set -e

BRIDGE=br0
TAP=tap0
PHYS=enp0s31f6

if ! ip link show "$TAP" &>/dev/null; then
    echo "TAP $TAP doesn't exist, nothing to tear down."
    exit 0
fi

echo "Tearing down network bridge..."

# Grab IP config from bridge before removing
ADDR=$(ip -4 addr show dev "$BRIDGE" 2>/dev/null | grep -oP 'inet \K[\d.]+/\d+')
GW=$(ip route show default dev "$BRIDGE" 2>/dev/null | grep -oP 'via \K[\d.]+' | head -1)

# Remove TAP
ip link set "$TAP" down 2>/dev/null || true
ip link delete "$TAP" 2>/dev/null || true

# Remove physical interface from bridge and restore its config
ip link set "$PHYS" nomaster 2>/dev/null || true
ip link delete "$BRIDGE" type bridge 2>/dev/null || true

# Restore IP config to physical interface
if [ -n "$ADDR" ]; then
    ip addr add "$ADDR" dev "$PHYS" 2>/dev/null || true
fi
if [ -n "$GW" ]; then
    ip route add default via "$GW" dev "$PHYS" 2>/dev/null || true
fi

echo "Network bridge torn down, $PHYS restored."
