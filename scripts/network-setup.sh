#!/usr/bin/env bash
set -euo pipefail

# Network setup helper for connecting to mixing consoles.
# Most consoles expect a static IP on a specific subnet.

echo "=== MixAgent Network Setup ==="
echo ""
echo "Common console network configurations:"
echo ""
echo "  Behringer X32 / Midas M32:"
echo "    Default IP:   192.168.1.1 (or DHCP)"
echo "    OSC Port:     10023 (UDP)"
echo "    Subnet:       192.168.1.0/24"
echo ""
echo "  Behringer Wing:"
echo "    Default IP:   192.168.1.1 (or DHCP)"
echo "    OSC Port:     2222 (UDP)"
echo "    Subnet:       192.168.1.0/24"
echo ""
echo "  Allen & Heath Avantis:"
echo "    Default IP:   192.168.1.1"
echo "    TCP Port:     51325"
echo "    Subnet:       192.168.1.0/24"
echo ""

# Detect network interfaces
echo "Available network interfaces:"
ip -br addr show 2>/dev/null || ifconfig 2>/dev/null || echo "  (cannot detect)"
echo ""

IFACE="${1:-}"
STATIC_IP="${2:-192.168.1.50}"

if [ -z "$IFACE" ]; then
    echo "Usage: $0 <interface> [static_ip]"
    echo "Example: $0 eth0 192.168.1.50"
    echo ""
    echo "This will configure the interface with a static IP"
    echo "on the console's subnet."
    exit 0
fi

echo "Configuring $IFACE with IP $STATIC_IP/24..."
echo "NOTE: This requires root/sudo."
echo ""
echo "Run manually:"
echo "  sudo ip addr add $STATIC_IP/24 dev $IFACE"
echo "  sudo ip link set $IFACE up"
echo ""
echo "Or for NetworkManager:"
echo "  nmcli con add type ethernet con-name mixagent ifname $IFACE \\"
echo "    ip4 $STATIC_IP/24"
echo "  nmcli con up mixagent"
