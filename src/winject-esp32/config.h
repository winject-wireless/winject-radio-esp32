#ifndef WINJECT_CONFIG_H_
#define WINJECT_CONFIG_H_

#include <stdint.h>

#define DEVICE_HOSTNAME "winject-esp32"

// LAN8720 RMII wiring (PHY addr / MDC / MDIO / power shared).
// RMII REF_CLK is selected by PlatformIO env (see platformio.ini):
//   wt32-eth01  → ETH_CLK_GPIO0_IN   (external 50 MHz into GPIO0)
//   lan-module  → ETH_CLK_GPIO17_OUT (ESP32 APLL clock out on GPIO17)
#define ETH_PHY_ADDR 1
#define ETH_PHY_MDC 23
#define ETH_PHY_MDIO 18
#define ETH_PHY_POWER 16
#define ETH_CLK_GPIO0_IN 0
#define ETH_CLK_GPIO17_OUT 1
#ifndef ETH_CLK_MODE
#define ETH_CLK_MODE ETH_CLK_GPIO0_IN
#endif

// WT32-ETH01 UART2 LEDs, active-low (LED on when GPIO is 0).
// LED4 / silk TXD = IO17 = WiFi TX. LED3 / silk RXD = IO5 = WiFi RX.
// GPIO17 is also the optional RMII clock-out pin; TX LED is disabled then.
#define WIFI_RX_LED_GPIO 5
#if ETH_CLK_MODE == ETH_CLK_GPIO17_OUT
#define WIFI_TX_LED_GPIO -1
#else
#define WIFI_TX_LED_GPIO 17
#endif
#define WIFI_LED_ON 0
#define WIFI_LED_OFF 1
// Stretch must exceed indicator_led poll (10 ms) or TX/RX pulses are invisible.
#define WIFI_LED_STRETCH_US (20 * 1000u)

// UDP management plane (m-plane) console.
#define CONTROL_CONSOLE_PORT 22

// D-plane UDP port (fixed; the manager must use the same value).
// 1–23 byte payloads register the forward peer; 24–1472 byte payloads are
// MPDUs hijacked at L2 for inject; forwarded air frames are sent from this port.
#define DPLANE_PORT 9000
// Largest MPDU in one unfragmented inject datagram (1500 MTU − 20 IP − 8 UDP).
// Larger payloads are IP-fragmented and rejected by the inject hijack.
#define DPLANE_INJECT_MPDU_MAX 1472

// test_ether_tx/rx UDP payload: IPv4/UDP L2 = 14+20+8+payload must fit
// CONFIG_ETH_DMA_BUFFER_SIZE (1514).
#define ETHER_TEST_MTU_MAX 1472
// Max L2 frame for 1400 B inject UDP (bw_test / manager path).
#define WINJECT_ETH_L2_INJECT_MAX (14u + 20u + 8u + 1400u)
// 1500-byte IP MTU Ethernet frame (L2 header only; FCS not in DMA buffer).
#define WINJECT_ETH_L2_MTU_MAX (14u + 1500u)
// Must stay below CONFIG_LWIP_TCPIP_TASK_PRIO (18) or a TX flood starves
// lwIP/EMAC and exhausts RX buffers ("esp.emac: no mem for receive buffer").
// RX at 18 matches tcpip so host→device can dequeue as fast as UDP is posted.
#define ETHER_TEST_RX_TASK_PRIO 18
#define ETHER_TEST_TX_TASK_PRIO 17
#define ETHER_TEST_TASK_STACK 3072
#define ETHER_TEST_SOCK_BUF (128 * 1024)
// Frames between vTaskDelay(1) in test_ether_* / test_wifi_tx loops so IDLE1
// can pet the task WDT.
#define ETHER_TEST_YIELD_EVERY 32

// type=dhcp: if the DHCP client has no lease after timeout= seconds, apply the
// network ip= as static. 0 disables the fallback. The radio never runs a
// DHCP server.
//
// Bench (192.168.253.0/24, server .1), 2026-09-17:
//   DORA DISCOVER→ACK ≈ 36–50 ms (p50 ≈ 40 ms).
// ESP-IDF then runs DHCP ARP conflict check (acd_dhcp_check.c): two probes
// at ACD_DHCP_ARP_REPLY_TIMEOUT_MS=500 → ≈ 1000 ms before GOT_IP / has_ipv4.
// Quiet path ≈ 1.05 s, but WiFi init overlaps DHCP on boot and can push
// GOT_IP past ~1.5 s — a tight timer then kills dhcpc mid-ACD and pins
// 192.168.32.1 on the LAN (unreachable). Keep several seconds of margin.
#define NETWORK_DHCP_TIMEOUT_S_DEFAULT 5
#define ETH_FALLBACK_ADDR 192, 168, 32, 1
#define NETWORK_PREFIX_DEFAULT 24

// HTTP firmware update. GET / form, POST /update blob.
#define OTA_HTTP_PORT 80
// Pending OTA image is marked valid after this many seconds of link + IPv4.
#define OTA_VALIDATE_UPTIME_S 30

// Core split: WiFi driver + inject task on core 0; Ethernet/lwIP/console/OTA
// on core 1 (see sdkconfig.defaults).
#define WIFI_RADIO_TASK_CORE 0
#define APP_TASK_CORE 1
#define WIFI_RADIO_TASK_PRIO 20
#define NETMGR_TASK_PRIO 6
#define UPSTREAM_RX_TASK_PRIO 17
#define UPSTREAM_RX_TASK_STACK 4096
// test_wifi_tx generator: below wifi_tx and the d-plane drain.
#define WIFI_TX_TEST_TASK_PRIO 16
#define WIFI_TX_TEST_TASK_STACK 4096

// Raw 802.11 radio.
#define WIFI_RADIO_INJECT_MIN 24
#define WIFI_RADIO_INJECT_MAX 1500
#define WIFI_FCS_LEN 4
// RX pool slot: largest forwarded MPDU plus its on-air FCS.
#define WIFI_RX_PACKET_CAP (WIFI_RADIO_INJECT_MAX + WIFI_FCS_LEN)
// App queues between driver callbacks and worker tasks (tune_tx_param
// tx_queue_sz / tune_rx_param rx_queue_sz, applied at boot). The RX packet
// pool has one WIFI_RX_PACKET_CAP slot per rx_queue_sz, heap-allocated at boot.
#define WIFI_TX_QUEUE_DEFAULT 20
#define WIFI_TX_QUEUE_MAX 64
#define WIFI_RX_QUEUE_DEFAULT 8
#define WIFI_RX_QUEUE_MAX 32
// WiFi driver dynamic buffers (tune wifi_tx_ring_sz / wifi_rx_ring_sz) feed
// wifi_init_config_t at boot. Ranges follow the IDF Kconfig; the dynamic RX
// count must not be below CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM.
#define WIFI_TX_RING_MIN 1
#define WIFI_TX_RING_MAX 128
#define WIFI_RX_RING_MAX 128
// wifi_tx: EMAC/WiFi timeshare — drain a small batch then idle (see winject.md).
#define WIFI_TX_BURST_SIZE_DEFAULT 8
#define WIFI_TX_BURST_GAP_US 1000
#define WIFI_RADIO_INJECT_RETRIES 8
// Cap outstanding 802.11 TX before submitting another — avoids NO_MEM storms
// that thrash the WiFi DMA and starve EMAC RX. Do not raise without re-checking
// ETH→sut goodput (see docs/winject.md#ethwifi-tx-performance).
#define WIFI_RADIO_MAX_IN_FLIGHT 4
// NO_MEM: yield this many times before a 1-tick sleep (driver ring recovery).
// Higher helps sustain ~MCS7 30 Mbps inject without thrashing on 1 ms sleeps.
#define WIFI_RADIO_INJECT_NOMEM_YIELD 48
// Total time to retry ESP_ERR_NO_MEM on one frame before dropping it.
#define WIFI_RADIO_INJECT_NOMEM_MAX_US (50 * 1000)
// No TX-done progress for this long → reset in_flight (driver may drop silently).
#define WIFI_TX_STALL_US (100 * 1000)
#define WIFI_CHANNEL_MIN 1
#define WIFI_CHANNEL_MAX 14
#define WIFI_DEFAULT_CHANNEL 1
#define WIFI_DEFAULT_MODULATION "DSS_1M_L"
#define WIFI_DEFAULT_TX_POWER_DBM 20
#define WIFI_TX_POWER_DBM_MIN 2
#define WIFI_TX_POWER_DBM_MAX 20
// Legacy 64-QAM (48M/54M) raw inject clips above this; peer promisc RX fails
// while USB monitor still looks fine at 20 dBm.
#define WIFI_TX_POWER_64QAM_LEGACY_MAX_DBM 13

#define WIFI_HDR_LEN 24
// Winject Addr3 prefix (manager domain encoding); not used as an RX filter
// default — cleared rx_filter_addr3 forwards all frames.
#define WIFI_BSSID_PREFIX_STANDALONE 0xCA, 0xFE, 0xBA, 0xBE

#define SETTINGS_SLOT_COUNT 10
// Longest modulation name + NUL ("OFDM_MCS7_LGI").
#define SETTINGS_MODULATION_MAX 16

#endif  // WINJECT_CONFIG_H_
