# winject-radio-esp32

ESP32 **winject radio** firmware for the Wireless-Tag **WT32-ETH01** (ESP32 + LAN8720 Ethernet) and compatible **lan-module** boards (RMII REF_CLK on GPIO17). Built with PlatformIO and **ESP-IDF**.

Ethernet UDP carries the management plane and L3 inject/monitor paths; WiFi runs raw 802.11 monitor and inject. WiFi driver and the inject task run on **CPU0**. Ethernet, lwIP, the UDP console, and HTTP OTA run on **CPU1**.

## Layout

```
src/winject-esp32/          Firmware: console, upstream, settings, OTA, board config
src/winject-esp32/radio/    WiFi radio and 802.11 frame wrap/unwrap
src/winject-esp32/netmgr/   Ethernet PHY, DHCP client, IP manager
src/bfc-esp32/              ESP32 BFC subset (FreeRTOS / lwIP)
sdkconfig.defaults          Shared IDF options
sdkconfig.wt32-eth01        Per-board overrides (WT32-ETH01)
sdkconfig.lan-module        Per-board overrides (GPIO17 REF_CLK)
partitions.csv              Two OTA app slots
platformio.ini              Board, framework, and serial settings
```

Host **winject-manager** and L3 tests live in the sibling repo [**winject-l3**](https://github.com/winject-wireless/winject-l3). Set `WINJECT_L3_ROOT` if it is not next to this tree. Bench scripts under `scripts/` use `ensure_manager.sh` to find it.

## Documentation

- [docs/winject.md](docs/winject.md) — radio architecture: inject/RX paths, tasks, radio config
- [docs/mplane.md](docs/mplane.md) — UDP control plane (port 22)

## Build and flash

The WT32-ETH01 has no USB port. Use a **3.3 V** USB-UART adapter on `TXD` / `RXD` / `GND`. To flash, hold `BOOT` (IO0) low while resetting or powering on.

```bash
pio run -e wt32-eth01          # or -e lan-module
pio run -e wt32-eth01 -t upload
pio device monitor
```

Set `upload_port` in `platformio.ini` only if auto-detect picks the wrong serial device.

## Network

Default mode is **`dhcp`**: DHCP client, then static fallback `192.168.32.1/24` if there is no lease within **5 s** (`NETWORK_DHCP_TIMEOUT_S_DEFAULT`). The radio does **not** run a DHCP server.

If the link was down and the radio had fallen back to the static address, plugging the cable back in restarts DHCP; the fallback address may be unavailable for up to `timeout_s` while the client retries.

PHY defaults match WT32-ETH01 (LAN8720 addr `1`, MDC `23`, MDIO `18`, power `16`). RMII REF_CLK: `wt32-eth01` uses external clock on GPIO0; `lan-module` uses ESP32 clock out on GPIO17.

## Ports

| Service | Port |
|---------|------|
| m-plane (UDP console) | 22 |
| d-plane (inject, peer registration, forward) | 9000 |
| HTTP OTA | 80 |

## Security

- Use an **isolated bench LAN** for development.
- The m-plane, d-plane, and HTTP `/update` accept any source; keep radios on an isolated LAN.
- **OTA rollback** requires a **full serial flash** (bootloader + partition table + app) once per unit. OTA alone does not replace the bootloader; units that never get that flash cannot roll back.
- Signed apps (`SECURE_SIGNED_APPS_NO_SECURE_BOOT`) are not enabled yet.

## Host unit tests

```bash
cmake -S src/host_test -B build_host_test
cmake --build build_host_test -j
ctest --test-dir build_host_test
```
