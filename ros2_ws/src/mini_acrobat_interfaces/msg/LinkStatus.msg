# Drone-side network status, published by mini_acrobat_link on /link/status (~1 Hz).
# Shown by MAgcs (Connection dock) and Foxglove. Unknown values are 0 / empty.
string hostname
string wifi_interface      # e.g. wlan0
bool wifi_connected        # associated to an access point
string ssid
string bssid
int16 rssi_dbm             # signal level of the associated AP; 0 = unknown
int16 noise_dbm            # 0 = unknown
uint16 frequency_mhz       # 0 = unknown
float32 tx_bitrate_mbps    # 0 = unknown
float32 rx_bitrate_mbps    # 0 = unknown
string[] addresses         # "<iface> <addr>/<prefix>", IPv4, loopback excluded
bool tailscale_up          # tailscale0 has an address in 100.64.0.0/10
string tailscale_ip
