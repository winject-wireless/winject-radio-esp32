# Radio management plane (m-plane)

The **m-plane** is a text command protocol over **UDP** on the radio's Ethernet IPv4 address. It configures the network, WiFi PHY, boot-time tuning, saved settings slots, and built-in Ethernet/WiFi traffic tests, and triggers reboots.

Implementation:

| Layer | Files |
|-------|-------|
| UDP socket, reply buffering | `src/winject-esp32/console.cpp` |
| Parsing, dispatch, reply format (host unit tests in `src/host_test/`) | `src/winject-esp32/mplane/mplane_commands.cpp`, `mplane_args.cpp`, `mplane_reply.cpp`, `mplane_req_id.cpp` |
| Backend interfaces | `src/winject-esp32/mplane/mplane_backend.h` |
| Firmware backends | `mplane/device_backend_esp.cpp`, `radio_backend_esp.cpp`, `test_backend_esp.cpp` |

The **d-plane** (frame inject and RX forward) has fixed UDP ports and is not configured through the m-plane; see [D-plane](#d-plane).

How the radio behind these commands works (inject/RX paths, queues, counters): [winject.md](./winject.md).

## Transport

| Item | Value |
|------|--------|
| Protocol | UDP/IPv4 |
| Port | **22** (`CONTROL_CONSOLE_PORT` in `src/winject-esp32/config.h`) |
| Bind | `0.0.0.0:22` while Ethernet has an IPv4 address (checked every 250 ms; closed when the address is lost) |
| Request size | Up to 1499 bytes per datagram; longer datagrams are truncated |
| Reply size | Up to 16384 bytes per datagram; longer replies continue in further datagrams |
| Reply routing | `sendto` back to the request's source IP and port |

A request datagram holds one or more newline-separated command lines; each line gets its own reply line(s), all returned together. No line terminator is required. Leading/trailing blanks and CR are ignored; empty lines and lines starting with `#` produce no reply.

Command names, aliases, and argument keys are matched **case-insensitively**. Arguments are `key=value` tokens separated by spaces or tabs; unknown or repeated keys are rejected with `NOK EINVAL`.

### Request correlation (`cmd:<u8>`)

**winject-manager** (and other L3 clients) prefix each forwarded line with **`cmd:<u8>`** so replies can be paired with the request. This id is only for request/response correlation; idempotent TX test commands still use **`id=<u8>`** in the command body (`test_ether_tx id=…`, etc.). Clients confirm a successful `reset` by comparing `ts` in `tx_info` / `rx_info` with elapsed time since the request.

| Direction | Wire format | Typical log |
|-----------|-------------|-------------|
| Client → radio | `cmd:<u8> <m-plane line>` | `<< cmd:<u8> …` |
| Radio → client (success) | `OK:<u8> …` on every reply line for that command | `>> OK:<u8> …` |
| Radio → client (failure) | `NOK:<u8> <code or message>` | `>> NOK:<u8> …` |

Examples:

| Request | Reply |
|---------|--------|
| `cmd:3 ping` | `OK:3 pong` |
| `cmd:4 save 1` | `OK:4` |
| `cmd:5 radio_tx channel=6` | `OK:5 radio_tx channel=6 tx_power=20 modulation=DSS_1M_L cca=true` |
| `cmd:6 bogus` | `NOK:6 ENOSYS` |
| `cmd:7 reset` | `OK:7` (then reboot) |
| `cmd:8 radio_caps_info` | `OK:8 radio_caps_info fcs=SIGNAL` |
| `cmd:9 tx_info` | `OK:9 tx_info tx_queue_sz=0 in_flight=0 …` (info replies get `OK:<u8> ` prepended) |

Lines without the `cmd:` prefix behave as before (no correlation fields). A malformed `cmd:` prefix (non-numeric, above 255, or with no command after it) replies an uncorrelated `NOK EINVAL`.

### Value formats

| Type | Format |
|------|--------|
| `<bool>` | `1` `true` `on` `yes` / `0` `false` `off` `no` |
| `<mac>` | `aa:bb:cc:dd:ee:ff`, `aa-bb-cc-dd-ee-ff`, or `aabbccddeeff` (hex, any case); printed lowercase with `:` |
| `<ip>` | dotted IPv4 `a.b.c.d` |
| `<u8>`, `<u16>` | unsigned decimal |

## Replies

| Reply | Meaning |
|-------|---------|
| `OK` | Success, no body |
| `OK <command> key=value ...` | Success; echoes the resulting state (`radio_caps_info`, `network`, `radio_tx`, …) |
| `<command> key=value ...` | Info reply of a query (`tx_info`, `rx_info`, `radio_tx_info`, `*_stat`) |
| `pong` | Reply to `ping` |
| `NOK <code>` | Failure, see below |
| `OK:<u8> …` / `NOK:<u8> …` | Correlated reply for a `cmd:<u8>` request (see above) |

| Code | Meaning |
|------|---------|
| `EINVAL` | Malformed command, unknown/repeated key, value out of range, or unsupported `radio_tx` combination |
| `ENOSYS` | Unknown command (including `radio_caps_info` on firmware older than this capability; hosts treat that as `fcs=ACTUAL` on the d-plane) |
| `ENODEV` | Radio commands, `test_wifi_rx`, and `test_wifi_tx` while the radio is down; every radio and test command in OTA mode |
| `EALREADY` | A TX test of the same kind is already running |
| `ESTALE` | TX test `id` repeats the last accepted start, or a stop `id` does not match the running test |
| `ENOENT` | `load` of an empty slot |
| `EIO` | NVS or driver failure, `load` of a corrupt or invalid slot, a test socket that cannot bind, or a test task that cannot start |

## Modes

| Mode | Behavior |
|------|----------|
| `WINJECT` (default) | Network, m-plane, HTTP OTA, WiFi radio, and d-plane |
| `OTA` | Network, m-plane, and HTTP OTA only. Radio, d-plane, and test commands reply `NOK ENODEV`; `save`/`load` still keep the radio settings of a slot |

The mode is persisted by `reset mode=...` and applies from the next boot. Firmware update is HTTP on port **80** (`GET /`, `POST /update`) in both modes.

## Commands

`help` lists every command with its alias and usage, plus the valid modulation names when the radio is up.

### Device

| Command | Alias | Arguments | Reply |
|---------|-------|-----------|-------|
| `help` | `?` | | usage lines |
| `ping` | `p` | | `pong` |
| `reset` | `r` | `[mode=WINJECT\|OTA]` | `OK`, then reboots (~200 ms later). With `mode`, persists the boot mode first |
| `save` | | `<slot 0-9>` | `OK`; stores network, radio (incl. `rx_filter_addr3`), and tune, and makes the slot current |
| `load` | | `<slot 0-9>` | `OK`; applies radio immediately, network right after the reply, tune on the next boot, and makes the slot current. `NOK ENOENT` if empty, `NOK EIO` if unreadable |
| `network` | `sn` | `ip=<ip>[/<prefix>] type=dhcp\|static timeout=<0-65535>` | `OK network ip=<ip>/<prefix> type=<type> timeout=<s>` |
| `tune_param` | `tp` | `set_eth_dma_burst_len=<1\|2\|4\|8\|16\|32>` | `OK tune_param set_eth_dma_burst_len=<n>` |
| `tune_tx_param` | `ttp` | `eth_rx_ring_sz=<u8> tx_queue_sz=<u8> wifi_tx_ring_sz=<u8>` | `OK tune_tx_param eth_rx_ring_sz=<n> tx_queue_sz=<n> wifi_tx_ring_sz=<n>` |
| `tune_rx_param` | `trp` | `eth_tx_ring_sz=<u8> rx_queue_sz=<u8> wifi_rx_ring_sz=<u8>` | `OK tune_rx_param eth_tx_ring_sz=<n> rx_queue_sz=<n> wifi_rx_ring_sz=<n>` |

All `key=value` arguments are optional: omitted keys keep their value, and no arguments at all just prints the current state.

**Boot settings.** At boot the radio applies the current slot (the last one saved or loaded); an empty or unreadable slot falls back to the defaults below. Changes made with `network`, `radio_tx`, `rx_filter_addr3`, or the tune commands are not persisted until `save`.

**Network.** `network` applies immediately, so the radio may move to a new address right after replying. With `type=dhcp`, `ip/prefix` is the static fallback used after `timeout` seconds without a lease; `timeout=0` keeps waiting for DHCP. With `type=static`, `ip/prefix` is used directly. `ip=` without `/<prefix>` keeps the current prefix (1–32). The address must be a unicast host address (not 0/8, 127/8, multicast/reserved; for prefixes up to /30 not the network or broadcast address). Defaults: `type=dhcp ip=192.168.32.1/24 timeout=5`.

**Tune.** Values are checked when set and take effect on the next boot, so the usual sequence is `tune_* ...`, `save <slot>`, `reset`. The commands report the configured values, not the running ones.

**Reset.** After `OK`, the firmware reboots in about 200 ms. There is no idempotency `id=` on `reset`; `id=` is an unknown key and replies `NOK EINVAL`. If a client sends `reset` and never sees `OK` (timeout or lost reply), it should not blindly resend: note the time `t0`, poll `tx_info` or `rx_info` until the radio answers, then compare `ts` (uptime in µs, near zero after every boot) with elapsed time since `t0`. If `ts` is less than that elapsed interval, the radio restarted after the request (from this `reset`, a watchdog, or a crash) and the client can treat the reboot as done; if `ts` is still large, the radio was up the whole time and sending `reset` again is safe. **winject-manager** uses this rule instead of a stored reset id.

| Key | Range | Default | Effect |
|-----|-------|---------|--------|
| `set_eth_dma_burst_len` | 1, 2, 4, 8, 16, 32 | 32 | EMAC DMA burst length (beats) |
| `eth_rx_ring_sz` | built value only (28) | 28 | EMAC RX descriptors; compiled into ESP-IDF (`CONFIG_ETH_DMA_RX_BUFFER_NUM`) |
| `eth_tx_ring_sz` | built value only (16) | 16 | EMAC TX descriptors (`CONFIG_ETH_DMA_TX_BUFFER_NUM`) |
| `tx_queue_sz` | 1-64 | 20 | Inject queue between Ethernet RX and WiFi TX |
| `rx_queue_sz` | 1-32 | 8 | WiFi RX queue and packet pool (one 1504-byte buffer each) |
| `wifi_tx_ring_sz` | 1-128 | 16 | WiFi driver dynamic TX buffers |
| `wifi_rx_ring_sz` | 8-128 | 32 | WiFi driver dynamic RX buffers (minimum is the static RX buffer count) |

### Radio

| Command | Alias | Arguments | Reply |
|---------|-------|-----------|-------|
| `tx_info` | `ti` | | `tx_info tx_queue_sz=<n> in_flight=<n> dropped_invalid_frame=<u32> dropped_tx_queue=<u32> dropped_wifi=<u32> ether_pkt=<u32> air_pkt=<u32> ts=<u64>` |
| `rx_info` | `ri` | | `rx_info rx_queue_sz=<n> dropped_filter_mismatched=<u32> dropped_rx_queue=<u32> dropped_no_peer=<u32> dropped_send_failed=<u32> ether_pkt=<u32> air_pkt=<u32> ts=<u64>` |
| `radio_tx` | `rt` | `channel=<1-14> tx_power=<2-20> modulation=<name> cca=<bool>` | `OK radio_tx channel=<n> tx_power=<dBm> modulation=<name> cca=<bool>` |
| `radio_tx_info` | `rti` | | `radio_tx channel=... tx_power=... modulation=... cca=...` and, once a frame was received, `radio_rx rssi=<dBm>` |
| `radio_caps_info` | `rci` | (none) | `OK radio_caps_info fcs=SIGNAL\|ACTUAL` |
| `rx_filter_addr3` | `rf3` | `addr=<mac>` or `addr=` | `OK rx_filter_addr3 addr=<mac or empty>` |

`radio_caps_info` is read-only: it describes the build and hardware, not a setting, and is not stored in `save`/`load` slots. Any arguments reply `NOK EINVAL`. Like other radio commands, it replies `NOK ENODEV` while the radio is down or in OTA mode. The reply is `key=value` so new capability keys can be appended later; parsers must ignore unknown keys. **winject-manager** may answer with the same reply line so one parser works against a radio or a manager.

`tx_info` and `rx_info` report **current occupancy** (`tx_queue_sz`, `in_flight`, `rx_queue_sz`) and **monotonic drop/packet counters** since boot (`dropped_*`, `ether_pkt`, `air_pkt`; `u32`, wrapping at 2³² — diff two readings to get drops over an interval). `ts` is radio uptime in µs (`esp_timer_get_time()`), one sample per reply. Configured capacities are in `tune_tx_param` / `tune_rx_param`.

**`tx_info` fields**

| Field | Meaning |
|---|---|
| `tx_queue_sz`, `in_flight` | Current inject-queue occupancy and frames submitted to the driver awaiting TX-done (at most 4) |
| `dropped_invalid_frame` | Inject datagrams hijacked from Ethernet whose MPDU length was outside 24–1500 bytes, plus frames the TX task rejects for size |
| `dropped_tx_queue` | Frames dropped because the inject queue was full |
| `dropped_wifi` | Frames lost in the WiFi driver (inject refused after retries, stall guard reset, or TX-done failure) |
| `ether_pkt` | Inject frames taken on Ethernet (UDP :9000 hijack match), counted before the length check |
| `air_pkt` | Frames reported sent (`TX-done` with success) |
| `ts` | Uptime in µs when the reply was built |

**`rx_info` fields**

| Field | Meaning |
|---|---|
| `rx_queue_sz` | Frames waiting to be forwarded to the host |
| `dropped_filter_mismatched` | Promiscuous callbacks not forwarded (wrong type, A-MPDU, length, or Addr3 filter) |
| `dropped_rx_queue` | Addr3-matched frames dropped because the RX queue or pool was full |
| `dropped_no_peer` | Frames drained while no host had registered on UDP :9210 |
| `dropped_send_failed` | Forward attempts where `sendto` failed |
| `ether_pkt` | Frames successfully sent to the registered host |
| `air_pkt` | Every frame the promiscuous filter delivers (data, misc, and FCS-failed frames on the channel, including other networks' traffic) |
| `ts` | Uptime in µs when the reply was built |

See [more-metrics.md](./more-metrics.md) for counter placement and bench verification.

`radio_tx` accepts any subset of its keys. Modulation names are case-insensitive and echoed in upper case; `help` lists them. Channel 14 allows only the 802.11b rates (`DSS_*`, `CCK_*`); an unknown name or an unsupported channel/modulation combination replies `NOK EINVAL`, and a driver failure `NOK EIO` (channel and modulation are rolled back). At `OFDM_48M`/`OFDM_54M` the radio transmits at most 13 dBm, but `tx_power` still reports the requested value. Defaults: `channel=1 tx_power=20 modulation=DSS_1M_L cca=true`.

`rx_filter_addr3 addr=<mac>` forwards only received frames whose addr3 equals `<mac>` (full 6-byte match). `addr=` (empty) clears the filter: **forward all** received MPDUs on the d-plane path. With no argument it prints the current filter.

### Tests

Traffic generators and counters for link bring-up and throughput checks. Every test command replies `NOK ENODEV` in OTA mode; while the radio is down, the Ethernet tests still work and `test_wifi_rx` / `test_wifi_tx` reply `NOK ENODEV`.

| Command | Alias | Arguments | Reply |
|---------|-------|-----------|-------|
| `test_ether_rx` | `ter` | `port=<port>` (0 closes) | `OK ether_rx port=<port>` |
| `test_ether_tx` | `tet` | `id=<u8> host=<ip> port=<port> mtu=<1-1472> count=<u16> [rate=<kbps>]` | `OK` |
| `test_ether_rx_stat` | `ters` | `[clear=<bool>]` | `test_ether_rx_stat pkt=<n> byt=<n>` |
| `test_wifi_rx` | `twr` | `[addr1=<mac>] [addr2=<mac>] [addr3=<mac>]` | `OK` |
| `test_wifi_tx` | `twt` | `[id=<u8>] [addr1=<mac>] [addr2=<mac>] [addr3=<mac>] mtu=<24-1500> count=<u16> [rate=<kbps>]` | `OK` |
| `test_wifi_rx_stat` | `twrs` | `[clear=<bool>]` | `test_wifi_rx_stat pkt=<n> byt=<n> fec_error_pkt=<n>` |

**Ethernet RX.** `test_ether_rx port=<p>` binds a UDP socket and counts every datagram and its payload bytes. Ports 22, 80, 9000, and 9210 are refused (`NOK EINVAL`).

**Ethernet TX.** `test_ether_tx` sends `count` UDP datagrams of `mtu` payload bytes to `host:port` in the background. The first 4 payload bytes are a little-endian sequence number. `rate` (0–1000000 kbit/s, counted on payload bytes) paces the sends; `rate=0` or omitted sends as fast as possible.

**WiFi RX.** `test_wifi_rx` counts received frames whose addresses match every given address; omitted addresses match anything, and no addresses at all stops counting. `byt` counts MPDU bytes without the FCS; `fec_error_pkt` counts frames the hardware flagged (`rx_state != 0`). Counting is independent of d-plane forwarding, which continues as normal.

**WiFi TX.** `test_wifi_tx` queues `count` data frames of `mtu` bytes (24-byte header included, zero-filled body, sequence number = frame index) on the inject path, paced like `test_ether_tx`. Omitted addresses default to broadcast (addr1), the radio's own STA MAC (addr2), and `00:00:00:00:00:00` (addr3). Frames share the TX queue with d-plane inject; the generator waits while that queue is full instead of dropping.

**Start/stop.** `count=0` stops a running TX test (`id` must then match the running test, or be omitted); with no test running it replies `OK`. Only `id` and `count` are needed to stop. A start replies `NOK EALREADY` while a test of the same kind is running, and `NOK ESTALE` if `id` equals the last accepted start, so a retransmitted request does not start a second run. `id` is required for `test_ether_tx` and optional for `test_wifi_tx`.

## D-plane

| Direction | Port | Behavior |
|-----------|------|----------|
| Inject (host -> air) | UDP **9000** (`DPLANE_INJECT_PORT`) | Each datagram is one MPDU (24–1472 bytes over Ethernet, one unfragmented UDP datagram on a 1500-byte MTU link; header included, no FCS), sent unicast to the radio's own MAC and IPv4 address. IP fragments are not reassembled. The frame is taken straight from the Ethernet RX path, never by the IP stack |
| Forward (air -> host) | UDP **9210** (`DPLANE_RX_PORT`) | Any datagram sent to this port registers its source as the forward peer; the most recent sender wins. Each accepted received frame is sent as the MPDU followed by a **4-byte trailer** whose meaning is given by `radio_caps_info fcs=` (query once per radio; `NOK ENOSYS` means legacy `ACTUAL`). Layout is always `MPDU \|\| 4 bytes`; only the trailer semantics differ. In both modes, FAIL frames are still forwarded so the receiver can count them; strip the trailer before using the MPDU |

| `fcs=` | Trailing 4 bytes | Receiver check |
|--------|------------------|----------------|
| `ACTUAL` | On-air FCS, CRC-32 little-endian, as received | `crc32(MPDU) == trailer` → pass |
| `SIGNAL` | Hardware integrity verdict: `00 00 00 00` = PASS, `FF FF FF FF` = FAIL | `trailer == 0` → pass; anything else → fail |

In `SIGNAL` mode the radio writes all-zero for PASS and all-ones (`0xFFFFFFFF`) for FAIL. Receivers treat **any non-zero** trailer as FAIL so a future reason code cannot be mistaken for PASS.

**ESP32 (this firmware):** `fcs=SIGNAL`. Espressif strips the on-air FCS from the promiscuous payload ([esp-idf#6473](https://github.com/espressif/esp-idf/issues/6473)); PASS means `rx_ctrl.rx_state == 0`. **Radios that deliver the real FCS** (for example Realtek with radiotap `F_FCS`) use `fcs=ACTUAL`.

Received frames are accepted per `rx_filter_addr3`. Inject frames that arrive while the inject queue is full are dropped. See [winject.md](./winject.md) for both paths in detail.

## Example session

Set `RADIO` to the radio's Ethernet address (DHCP lease or the fallback `192.168.32.1`).

```bash
mp() { echo -n "${*:2}" | nc -u -w1 "$1" 22; }
RADIO=192.168.32.1

mp $RADIO ping
mp $RADIO radio_caps_info
mp $RADIO radio_tx channel=6 modulation=OFDM_24M tx_power=15
mp $RADIO rx_filter_addr3 addr=ca:fe:ba:be:00:01
mp $RADIO tune_tx_param tx_queue_sz=32
mp $RADIO network ip=10.0.0.20/24 type=static   # radio moves to 10.0.0.20 now
RADIO=10.0.0.20
mp $RADIO save 1
mp $RADIO reset

# Air link check between two radios on the same channel (A sends, B counts)
A=<ip-of-A> B=<ip-of-B>
mp $B test_wifi_rx addr3=ca:fe:ba:be:00:02
mp $A test_wifi_tx id=1 addr3=ca:fe:ba:be:00:02 mtu=1500 count=1000 rate=5000
mp $B test_wifi_rx_stat clear=1
```
