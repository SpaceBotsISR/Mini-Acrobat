#include "widgets/MacNetworkBox.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "core/NetworkMonitor.hpp"
#include "widgets/LinkText.hpp"
#include "widgets/SignalBars.hpp"

namespace magcs {

MacNetworkBox::MacNetworkBox(NetworkMonitor* monitor, QWidget* parent)
: QGroupBox(tr("This Mac"), parent), m_monitor(monitor)
{
  m_bars = new SignalBars;
  m_wifi = new QLabel;
  m_wifi->setWordWrap(true);
  m_wifi->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_ips = new QLabel;
  m_ips->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_askName = new QPushButton(tr("Show network name..."));
  m_askName->setToolTip(tr("macOS only reveals the Wi-Fi name to apps with Location permission"));
  m_askName->setFlat(true);
  m_askName->setCursor(Qt::PointingHandCursor);
  m_askName->setStyleSheet("QPushButton { color: palette(link); text-align: left; padding: 0; }");

  auto* wifiRow = new QHBoxLayout;
  wifiRow->addWidget(m_bars, 0, Qt::AlignVCenter);
  wifiRow->addWidget(m_wifi, 1);

  auto* form = new QFormLayout(this);
  form->addRow(tr("Wi-Fi"), wifiRow);
  form->addRow(QString(), m_askName);
  form->addRow(tr("IPs"), m_ips);

  connect(m_askName, &QPushButton::clicked, this, []() {requestWifiNamePermission();});
  connect(m_monitor, &NetworkMonitor::localChanged, this, &MacNetworkBox::refresh);
  refresh();
}

void MacNetworkBox::refresh()
{
  const WifiInfo& w = m_monitor->wifi();
  m_bars->setRssi(w.rssiDbm);
  m_wifi->setText(linktext::macWifi(w));
  m_askName->setVisible(wifiNamePermissionSupported() && w.associated() && w.ssid.isEmpty());
  const QStringList& ips = m_monitor->localAddresses();
  m_ips->setText(ips.isEmpty() ? tr("no network") : ips.join('\n'));
}

}  // namespace magcs
