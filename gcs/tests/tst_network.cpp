// Parsers behind the connection window / dock: tailscale CLI output,
// /proc/net/wireless, /link/status, connection modes.
#include <QJsonArray>
#include <QtTest>

#include "core/ConnectionTarget.hpp"
#include "core/LinkStatus.hpp"
#include "core/Tailscale.hpp"
#include "platform/WifiInfo.hpp"

using namespace magcs;

namespace {

// Trimmed `tailscale status --json` (Tailscale 1.7x/1.8x layout).
const char* kStatusJson = R"({
  "Version": "1.80.0",
  "BackendState": "Running",
  "TailscaleIPs": ["100.64.0.10", "fd7a:115c:a1e0::a"],
  "Self": {
    "HostName": "Franciscos-MacBook-Pro", "DNSName": "franciscos-macbook-pro.tail1234.ts.net.",
    "OS": "macOS", "TailscaleIPs": ["100.64.0.10", "fd7a:115c:a1e0::a"], "Online": true
  },
  "MagicDNSSuffix": "tail1234.ts.net",
  "CurrentTailnet": {"Name": "isr-team", "MagicDNSSuffix": "tail1234.ts.net"},
  "Peer": {
    "nodekey:aaa": {
      "HostName": "old-laptop", "DNSName": "old-laptop.tail1234.ts.net.", "OS": "windows",
      "TailscaleIPs": ["100.64.0.30"], "Online": false, "Active": false, "CurAddr": "", "Relay": "mad"
    },
    "nodekey:bbb": {
      "HostName": "mini-acrobat", "DNSName": "mini-acrobat.tail1234.ts.net.", "OS": "linux",
      "TailscaleIPs": ["fd7a:115c:a1e0::b", "100.101.102.103"], "Online": true, "Active": true,
      "CurAddr": "192.168.1.20:41641", "Relay": "mad"
    },
    "nodekey:ccc": {
      "HostName": "lab-pc", "DNSName": "lab-pc.tail1234.ts.net.", "OS": "linux",
      "TailscaleIPs": ["100.64.0.40"], "Online": true, "Active": true, "CurAddr": "", "Relay": "fra"
    }
  }
})";

}  // namespace

class TstNetwork : public QObject {
  Q_OBJECT
private slots:
  void tailscaleStatus()
  {
    const tailscale::Status s = tailscale::parseStatus(kStatusJson);
    QVERIFY(s.ok);
    QVERIFY(s.running());
    QCOMPARE(s.tailnet, QStringLiteral("isr-team"));
    QCOMPARE(s.self.shortName(), QStringLiteral("franciscos-macbook-pro"));
    QCOMPARE(s.self.ipv4(), QStringLiteral("100.64.0.10"));
    QCOMPARE(s.peers.size(), 3);
    // online first, alphabetical
    QCOMPARE(s.peers.at(0).hostName, QStringLiteral("lab-pc"));
    QCOMPARE(s.peers.at(1).hostName, QStringLiteral("mini-acrobat"));
    QCOMPARE(s.peers.at(2).hostName, QStringLiteral("old-laptop"));

    const tailscale::Node& drone = s.peers.at(1);
    QCOMPARE(drone.ipv4(), QStringLiteral("100.101.102.103"));  // IPv4 even if listed 2nd
    QCOMPARE(drone.dnsName, QStringLiteral("mini-acrobat.tail1234.ts.net"));
    QVERIFY(drone.isDirect());
    QCOMPARE(drone.connectionText(), QStringLiteral("direct (192.168.1.20:41641)"));
    QCOMPARE(s.peers.at(0).connectionText(), QStringLiteral("relay DERP(fra)"));
    QCOMPARE(s.peers.at(2).connectionText(), QStringLiteral("offline"));
  }

  void tailscaleFindPeer()
  {
    const tailscale::Status s = tailscale::parseStatus(kStatusJson);
    for (const char* key : {"mini-acrobat", "MINI-ACROBAT", "mini-acrobat.tail1234.ts.net",
                            "mini-acrobat.tail1234.ts.net.", "100.101.102.103", " mini-acrobat "})
    {
      const tailscale::Node* n = s.findPeer(QString::fromLatin1(key));
      QVERIFY2(n, key);
      QCOMPARE(n->hostName, QStringLiteral("mini-acrobat"));
    }
    QVERIFY(!s.findPeer("nope"));
    QVERIFY(!s.findPeer(""));
  }

  void tailscaleStopped()
  {
    const tailscale::Status s =
      tailscale::parseStatus(R"({"BackendState":"Stopped","Self":{},"Peer":null})");
    QVERIFY(s.ok);
    QVERIFY(!s.running());
    QVERIFY(s.peers.isEmpty());
  }

  void tailscaleGarbage()
  {
    const tailscale::Status s = tailscale::parseStatus("failed to connect to local tailscaled");
    QVERIFY(!s.ok);
    QVERIFY(!s.error.isEmpty());
  }

  void tailscalePing()
  {
    tailscale::Ping p = tailscale::parsePing(
      "pong from mini-acrobat (100.101.102.103) via 192.168.1.20:41641 in 4ms\n");
    QVERIFY(p.ok);
    QCOMPARE(p.peer, QStringLiteral("mini-acrobat"));
    QCOMPARE(p.ip, QStringLiteral("100.101.102.103"));
    QCOMPARE(p.via, QStringLiteral("192.168.1.20:41641"));
    QCOMPARE(p.ms, 4.0);
    QVERIFY(!p.relayed());
    QCOMPARE(p.text(), QStringLiteral("direct via 192.168.1.20:41641, 4.0 ms"));

    p = tailscale::parsePing("pong from mini-acrobat (100.101.102.103) via DERP(mad) in 48ms\n");
    QVERIFY(p.ok);
    QVERIFY(p.relayed());
    QCOMPARE(p.ms, 48.0);

    p = tailscale::parsePing("timeout waiting for ping reply\n");
    QVERIFY(!p.ok);
    QCOMPARE(p.error, QStringLiteral("timeout waiting for ping reply"));
  }

  void procNetWireless()
  {
    const WifiInfo w = parseProcNetWireless(
      "Inter-| sta-|   Quality        |   Discarded packets               | Missed | WE\n"
      " face | tus | link level noise |  nwid  crypt   frag  retry   misc | beacon | 22\n"
      " wlan0: 0000   54.  -56.  -256        0      0      0      0      0        0\n");
    QVERIFY(w.available);
    QCOMPARE(w.interfaceName, QStringLiteral("wlan0"));
    QCOMPARE(w.rssiDbm, -56);
    QCOMPARE(w.noiseDbm, 0);  // -256 = not reported

    const WifiInfo none = parseProcNetWireless(
      "Inter-| sta-|   Quality        |\n face | tus | link level noise |\n");
    QVERIFY(!none.available);
  }

  void rssiBars()
  {
    QCOMPARE(rssiToBars(0), 0);
    QCOMPARE(rssiToBars(-40), 4);
    QCOMPARE(rssiToBars(-55), 4);
    QCOMPARE(rssiToBars(-60), 3);
    QCOMPARE(rssiToBars(-70), 2);
    QCOMPARE(rssiToBars(-78), 1);
    QCOMPARE(rssiToBars(-90), 0);
    QCOMPARE(rssiQuality(0), QStringLiteral("unknown"));
    QCOMPARE(rssiQuality(-90), QStringLiteral("very weak"));
  }

  void linkStatus()
  {
    QJsonObject msg{
      {"hostname", "mini-acrobat"}, {"wifi_interface", "wlan0"}, {"wifi_connected", true},
      {"ssid", "iPhone de Francisco"}, {"bssid", "aa:bb:cc:dd:ee:ff"}, {"rssi_dbm", -61},
      {"noise_dbm", 0}, {"frequency_mhz", 2437}, {"tx_bitrate_mbps", 72.2},
      {"rx_bitrate_mbps", 65.0},
      {"addresses", QJsonArray{"wlan0 172.20.10.3/28", "tailscale0 100.101.102.103/32"}},
      {"tailscale_up", true}, {"tailscale_ip", "100.101.102.103"}};
    const DroneLinkStatus s = DroneLinkStatus::fromJson(msg);
    QVERIFY(s.valid);
    QVERIFY(s.wifiConnected);
    QCOMPARE(s.rssiDbm, -61);
    QCOMPARE(s.frequencyMhz, 2437);
    QCOMPARE(s.addresses.size(), 2);
    QCOMPARE(s.tailscaleIp, QStringLiteral("100.101.102.103"));
    QVERIFY(!DroneLinkStatus::fromJson({}).valid);
  }

  void connectionModes()
  {
    using M = ConnectionTarget::Mode;
    for (M m : {M::Tailscale, M::Manual, M::Simulator}) {
      M back = M::Tailscale;
      QVERIFY(ConnectionTarget::modeFromKey(ConnectionTarget::modeKey(m), &back));
      QCOMPARE(back, m);
    }
    M m = M::Manual;
    QVERIFY(ConnectionTarget::modeFromKey("TailScale", &m));
    QCOMPARE(m, M::Tailscale);
    QVERIFY(!ConnectionTarget::modeFromKey("bluetooth", &m));

    ConnectionTarget t;
    t.mode = M::Simulator;
    t.host = "ignored";
    QCOMPARE(t.url(), QStringLiteral("ws://localhost:9090"));
    t.mode = M::Tailscale;
    t.host = " mini-acrobat ";
    t.port = 9091;
    QCOMPARE(t.url(), QStringLiteral("ws://mini-acrobat:9091"));
  }
};

QTEST_GUILESS_MAIN(TstNetwork)
#include "tst_network.moc"
