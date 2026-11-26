from mini_acrobat_link import parsing

IW_CONNECTED = """Connected to AA:BB:CC:DD:EE:FF (on wlan0)
\tSSID: iPhone de Francisco
\tfreq: 2437.0
\tRX: 123456 bytes (789 packets)
\tTX: 23456 bytes (123 packets)
\tsignal: -61 dBm
\trx bitrate: 65.0 MBit/s MCS 7
\ttx bitrate: 72.2 MBit/s MCS 7 short GI
\tbss flags: short-slot-time
"""

PROC = """Inter-| sta-|   Quality        |   Discarded packets               | Missed | WE
 face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon | 22
 wlan0: 0000   49.  -61.  -256        0      0      0      0      0        0
"""

IP_JSON = """[
 {"ifindex":1,"ifname":"lo","flags":["LOOPBACK","UP"],"addr_info":[
   {"family":"inet","local":"127.0.0.1","prefixlen":8}]},
 {"ifindex":3,"ifname":"wlan0","flags":["BROADCAST","UP"],"addr_info":[
   {"family":"inet","local":"172.20.10.3","prefixlen":28}]},
 {"ifindex":4,"ifname":"tailscale0","flags":["POINTOPOINT","UP"],"addr_info":[
   {"family":"inet","local":"100.101.102.103","prefixlen":32}]},
 {"ifindex":5,"ifname":"eth0","flags":["NO-CARRIER"],"addr_info":[]}
]"""


def test_iw_connected():
    r = parsing.parse_iw_link(IW_CONNECTED)
    assert r['connected']
    assert r['bssid'] == 'aa:bb:cc:dd:ee:ff'
    assert r['ssid'] == 'iPhone de Francisco'
    assert r['frequency_mhz'] == 2437
    assert r['rssi_dbm'] == -61
    assert r['rx_bitrate_mbps'] == 65.0
    assert r['tx_bitrate_mbps'] == 72.2


def test_iw_not_connected():
    assert parsing.parse_iw_link('Not connected.\n') == {'connected': False}
    assert parsing.parse_iw_link('') == {'connected': False}


def test_proc_net_wireless():
    assert parsing.parse_proc_net_wireless(PROC, 'wlan0') == -61
    assert parsing.parse_proc_net_wireless(PROC, 'wlan1') is None


def test_ip_addr_and_tailscale():
    addrs = parsing.parse_ip_addr_json(IP_JSON)
    assert addrs == ['wlan0 172.20.10.3/28', 'tailscale0 100.101.102.103/32']
    assert parsing.tailscale_ip(addrs) == (True, '100.101.102.103')
    assert parsing.tailscale_ip(['wlan0 192.168.1.5/24']) == (False, '')
    assert parsing.parse_ip_addr_json('garbage') == []
