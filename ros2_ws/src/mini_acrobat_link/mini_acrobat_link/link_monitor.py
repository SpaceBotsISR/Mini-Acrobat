"""link_monitor: publishes the drone's network status on /link/status.

Wi-Fi from `iw dev <if> link` (falls back to /proc/net/wireless for the signal
if `iw` is missing), addresses from `ip -j -4 addr`, Tailscale from the
tailscale0 address. Read-only: it never changes the network.

    ros2 run mini_acrobat_link link_monitor --ros-args -p interface:=wlan0
"""
import shutil
import socket
import subprocess

import rclpy
from mini_acrobat_interfaces.msg import LinkStatus
from rclpy.node import Node
from rclpy.qos import QoSDurabilityPolicy, QoSProfile

from mini_acrobat_link import parsing


def _run(cmd, timeout=0.5):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                              check=False).stdout
    except (OSError, subprocess.TimeoutExpired):
        return ''


class LinkMonitor(Node):

    def __init__(self):
        super().__init__('link_monitor')
        self.interface = self.declare_parameter('interface', 'wlan0').value
        rate = float(self.declare_parameter('rate_hz', 1.0).value)
        self.iw = shutil.which('iw')
        if self.iw is None:
            self.get_logger().warn(
                '`iw` not found (sudo apt install iw): only the signal level will be reported')
        # Transient local: a late subscriber (MAgcs reconnecting) gets the last value at once.
        qos = QoSProfile(depth=1, durability=QoSDurabilityPolicy.TRANSIENT_LOCAL)
        self.pub = self.create_publisher(LinkStatus, '/link/status', qos)
        self.hostname = socket.gethostname()
        self.timer = self.create_timer(1.0 / max(rate, 0.1), self.tick)
        self.get_logger().info(f'publishing /link/status for {self.interface} at {rate:.1f} Hz')

    def tick(self):
        msg = LinkStatus()
        msg.hostname = self.hostname
        msg.wifi_interface = self.interface

        if self.iw:
            info = parsing.parse_iw_link(_run([self.iw, 'dev', self.interface, 'link']))
        else:
            info = {}
            try:
                with open('/proc/net/wireless', encoding='ascii') as f:
                    level = parsing.parse_proc_net_wireless(f.read(), self.interface)
            except OSError:
                level = None
            if level is not None:
                info = {'connected': True, 'rssi_dbm': level}

        msg.wifi_connected = bool(info.get('connected', False))
        msg.ssid = info.get('ssid', '')
        msg.bssid = info.get('bssid', '')
        msg.rssi_dbm = int(info.get('rssi_dbm', 0))
        msg.frequency_mhz = int(info.get('frequency_mhz', 0))
        msg.tx_bitrate_mbps = float(info.get('tx_bitrate_mbps', 0.0))
        msg.rx_bitrate_mbps = float(info.get('rx_bitrate_mbps', 0.0))

        msg.addresses = parsing.parse_ip_addr_json(_run(['ip', '-j', '-4', 'addr', 'show']))
        msg.tailscale_up, msg.tailscale_ip = parsing.tailscale_ip(msg.addresses)
        self.pub.publish(msg)


def main(args=None):
    rclpy.init(args=args)
    node = LinkMonitor()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
