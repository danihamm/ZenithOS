#!/bin/bash
# Sets up a bridge + TAP device so QEMU guests appear on the local LAN.
# Requires root. Idempotent — safe to run multiple times.

set -e

BRIDGE=br0
TAP=tap0
PHYS=enp0s31f6
USER=$(logname 2>/dev/null || echo "${SUDO_USER:-daniel-hammer}")

# Check if physical interface is already in the bridge (properly)
PHYS_MASTER=$(ip -j link show dev "$PHYS" 2>/dev/null | grep -oP '"master"\s*:\s*"\K[^"]+' || true)

if [ "$PHYS_MASTER" = "$BRIDGE" ] && ip link show "$TAP" &>/dev/null; then
    echo "Bridge $BRIDGE with $PHYS and $TAP already set up."
    exit 0
fi

echo "Creating bridge $BRIDGE..."
ip link add name "$BRIDGE" type bridge 2>/dev/null || true
ip link set "$BRIDGE" up

# Tell NetworkManager to leave our interfaces alone (if NM is running)
if command -v nmcli &>/dev/null; then
    nmcli device set "$PHYS" managed no 2>/dev/null || true
    nmcli device set "$BRIDGE" managed no 2>/dev/null || true
fi

# Move physical interface into bridge (if not already)
if [ "$PHYS_MASTER" != "$BRIDGE" ]; then
    echo "Moving $PHYS into bridge $BRIDGE..."

    # Grab current IP config before moving
    ADDR=$(ip -4 addr show dev "$PHYS" | grep -oP 'inet \K[\d.]+/\d+' | head -1)
    GW=$(ip route show default dev "$PHYS" | grep -oP 'via \K[\d.]+' | head -1)

    ip addr flush dev "$PHYS"
    ip link set "$PHYS" master "$BRIDGE"

    # Apply IP config to bridge
    if [ -n "$ADDR" ]; then
        ip addr add "$ADDR" dev "$BRIDGE" 2>/dev/null || true
        echo "Assigned $ADDR to $BRIDGE"
    fi
    if [ -n "$GW" ]; then
        ip route add default via "$GW" dev "$BRIDGE" 2>/dev/null || true
        echo "Set default gateway $GW via $BRIDGE"
    fi
else
    echo "$PHYS already in $BRIDGE"
fi

# Create TAP device (if not already)
if ! ip link show "$TAP" &>/dev/null; then
    echo "Creating TAP $TAP for user $USER..."
    ip tuntap add dev "$TAP" mode tap user "$USER"
    ip link set "$TAP" master "$BRIDGE"
    ip link set "$TAP" up
else
    echo "TAP $TAP already exists"
    ip link set "$TAP" up 2>/dev/null || true
fi

if command -v nmcli &>/dev/null; then
    nmcli device set "$TAP" managed no 2>/dev/null || true
fi

echo "Network bridge setup complete: $PHYS -> $BRIDGE <- $TAP"
ip -4 addr show dev "$BRIDGE" | head -3
