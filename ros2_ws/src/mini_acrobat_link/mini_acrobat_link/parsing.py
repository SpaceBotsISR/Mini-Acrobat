"""Pure parsers for the Linux network tools (no ROS, unit-tested)."""
import ipaddress
import json
import re

TAILSCALE_NET = ipaddress.ip_network('100.64.0.0/10')


def parse_iw_link(text):
    """Parse `iw dev <if> link`.

    Connected to aa:bb:cc:dd:ee:ff (on wlan0)
            SSID: MyHotspot
            freq: 2437
            signal: -61 dBm
            rx bitrate: 65.0 MBit/s MCS 7
            tx bitrate: 72.2 MBit/s MCS 7 short GI

    Returns a dict; {'connected': False} for "Not connected.".
    """
    out = {'connected': False}
    m = re.search(r'Connected to ([0-9a-fA-F:]{17})', text)
    if not m:
        return out
    out['connected'] = True
    out['bssid'] = m.group(1).lower()
    fields = {
        'ssid': (r'^\s*SSID:\s*(.*)$', str),
        'frequency_mhz': (r'^\s*freq:\s*([0-9.]+)', lambda v: int(float(v))),
        'rssi_dbm': (r'^\s*signal:\s*(-?\d+)', int),
        'rx_bitrate_mbps': (r'^\s*rx bitrate:\s*([0-9.]+)', float),
        'tx_bitrate_mbps': (r'^\s*tx bitrate:\s*([0-9.]+)', float),
    }
    for key, (pattern, conv) in fields.items():
        fm = re.search(pattern, text, re.MULTILINE)
        if fm:
            out[key] = conv(fm.group(1).strip())
    return out


def parse_proc_net_wireless(text, interface):
    """Signal level (dBm) of `interface` from /proc/net/wireless, or None."""
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith(interface + ':'):
            continue
        parts = line.split(':', 1)[1].split()
        # status link level noise ...
        if len(parts) < 3:
            return None
        try:
            level = int(float(parts[2]))
        except ValueError:
            return None
        return level if level < 0 else None
    return None


def parse_ip_addr_json(text):
    """Parse `ip -j -4 addr show` into ['<iface> <addr>/<prefix>', ...] (no loopback)."""
    try:
        data = json.loads(text)
    except ValueError:
        return []
    out = []
    for itf in data:
        name = itf.get('ifname', '?')
        if name == 'lo' or 'LOOPBACK' in itf.get('flags', []):
            continue
        for a in itf.get('addr_info', []):
            if a.get('family') != 'inet':
                continue
            out.append(f"{name} {a.get('local')}/{a.get('prefixlen')}")
    return out


def tailscale_ip(addresses):
    """(up, ip): first tailscale0 / 100.64.0.0/10 address in parse_ip_addr_json() output."""
    for entry in addresses:
        try:
            name, cidr = entry.split(' ', 1)
            ip = ipaddress.ip_address(cidr.split('/')[0])
        except ValueError:
            continue
        if name.startswith('tailscale') or ip in TAILSCALE_NET:
            return True, str(ip)
    return False, ''
