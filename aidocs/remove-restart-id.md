# Plan: remove the `reset id=` idempotency id; clients use `ts`

**Status:** Implemented in **winject-radio-esp32** (firmware, host tests, `docs/mplane.md`). **winject-radio-realtek** still needs the vendored m-plane sync and backend/settings cleanup below.

## Goal

1. `reset` no longer takes `id=<u8>`. The radios stop storing and comparing
   reset ids.
2. A client that is unsure whether a `reset` went through (no reply, timeout)
   checks the radio's uptime (`ts` in `tx_info` / `rx_info`) instead of resending
   with the same id.
3. Same change in winject-radio-realtek and winject-radio-esp32 (they share the
   m-plane command code).

winject-l3 (the manager, the only m-plane client) is assumed to be updated
already. It sends plain `reset` and confirms through `ts`. This plan covers
only the radios.

Today:

- `mplane_commands::cmd_reset` (`winject-radio-esp32/src/winject-esp32/mplane/mplane_commands.cpp:269`,
  vendored into `winject-radio-realtek/src/vendor/mplane/`)
  parses `id=`, calls `device_backend::accept_reset_id()`, and replies
  `OK id=<u8>` or `NOK EALREADY` for a repeated id.
- ESP32: `settings::store_reset_id()` stores the id in NVS key `rst_id`
  (`winject-radio-esp32/src/winject-esp32/settings.cpp:16,201`), so a repeat is rejected even
  after the reboot.
- Realtek: `Settings::accept_reset_id()` writes `state.dir/reset_id`
  (`winject-radio-realtek/src/radio/Settings.cpp:221`) but never reads it back. The comparison is
  only in memory, so it is lost on the `execv` that the reset triggers
  (`winject-radio-realtek/docs/mplane.md:104`). This means the id has never protected a retransmit
  across a reset on Realtek.

## Why `ts` is enough

`ts` is uptime in µs, and it starts again from about 0 on every reset:

| Radio | `uptime_us()` | Restarts on `reset` |
|---|---|---|
| ESP32 | `esp_timer_get_time()` (`device_backend_esp.cpp:143`) | yes, `esp_restart()` |
| Realtek | `CLOCK_MONOTONIC` minus `start_us_` (`RealtekBackends.cpp:89`). `start_us_` is set in `App::bring_up()` (`App.cpp:184`) | yes, `execv` reruns `bring_up()` |

If a client sends `reset` at time `T0`, then later reads `ts` at time `T1`:

- `ts < (T1 - T0)` → the radio restarted after the request. Treat the reset as
  done.
- `ts >= (T1 - T0)` → the radio has been up since before the request, so the
  reset did not happen. Sending it again is safe.

This also covers a radio that restarted for another reason (a crash or a
watchdog) after the request. The client wanted a fresh radio and has one, so
treating that as done is correct.

## Client contract

The radio-side changes rely on this behaviour from winject-l3. It is listed
here as the reference for what the radios must provide:

- Before sending `reset`, the client notes `t0`. If no reply arrives, it
  polls `tx_info` until the radio answers, then compares `ts` with the time
  elapsed since `t0`. It does not resend unless `ts` shows that the radio was
  not restarted.
- So the radios must keep two things true: `ts` restarts on every `reset`, and
  `tx_info` works as soon as the m-plane accepts commands after boot. Both are
  true today, and this plan does not change either one.

## Wire change

| Before | After |
|---|---|
| `reset [mode=…] [id=<u8>]` | `reset [mode=…]` |
| `OK id=<u8>` / `NOK EALREADY` | `OK` |

The manager no longer sends `id=`, so no compatibility window is needed.
`id=` is removed outright. It is now an unknown key, so the radio replies
`NOK EINVAL`, the same as for any other unknown `reset` argument.

## Changes

### Shared m-plane code (edit in winject-radio-esp32, re-vendor into realtek)

`src/winject-esp32/mplane/mplane_commands.cpp` and
`src/vendor/mplane/mplane_commands.cpp` are identical. Realtek vendors the
esp32 copy at `src/vendor/mplane/VERSION`
(`3dbab50c2598102d4cd322f653bb5ec58b491dce`).

- `cmd_reset`: remove the `reset_id` optional, the `accept_reset_id` call,
  and the `OK id=` reply. Remove `"id"` from `k_keys` (`mplane_commands.cpp:244`),
  so `parse_args` rejects `id=` with `EINVAL`.
- `mplane_backend.h:105-106`: remove `accept_reset_id()` from
  `device_backend`.
- Then copy the files into `winject-radio-realtek/src/vendor/mplane/` and
  bump `VERSION`.

### winject-radio-esp32

- `device_backend_esp.{h,cpp}:41`: remove `accept_reset_id`.
- `settings.{h,cpp}`: remove `reset_id_is_duplicate`, `store_reset_id`,
  `last_reset_id_*`, and `k_reset_id_key`, and stop reading `rst_id` at load
  (`settings.cpp:153`). An existing `rst_id` NVS entry does no harm. Erase it
  once at boot if a clean NVS matters.
- Tests `src/host_test/mplane_commands_test.cpp`: remove the fake's
  `accept_reset_id` (55-62) and `last_reset_id_*` (116-117), and replace the
  cases at 381-383 and 621 (`cmd:8 reset id=9`) with plain `reset`.
- Docs `docs/mplane.md:35,51,82`: remove `reset id=` from the examples and
  from the `EALREADY` row.

### winject-radio-realtek

- `RealtekBackends.{h,cpp}:41`: remove `accept_reset_id`.
- `Settings.{h,cpp}`: remove `accept_reset_id`, `reset_id_matches`,
  `reset_id_`, and `reset_path()`. Delete a stale `state.dir/reset_id` at
  startup, or leave it.
- Tests `src/test/mplane_commands_test.cpp`: remove the fake's
  `accept_reset_id` and `last_reset_id_*`, and the cases at 385-386.
- Docs: `docs/mplane.md:37,104`, `docs/implementation.md:261` (reset row),
  `docs/winject.md:95,220` (`reset_id` in `state.dir`). Add one line under
  `reset` saying that clients confirm a reset with `ts`.

#### Realtek: is "reopen the device and reset the timer" needed?

No. The current `execv` reset already gives the `ts` behaviour this plan
needs. `start_us_` is set again in `bring_up()`, so `ts` restarts at about 0,
the same as on ESP32. The only Realtek work is the id removal above.

An in-process restart (stop the m-plane and data plane, close and reopen the
interface, call `bring_up()` again, reset `start_us_`) would avoid the
`exec`. However, it would have to tear down and rebuild everything that `execv`
resets for free: sockets, the BPF filter, the reactor, the threads, and the
re-read of the config file. Each of those is a place for a leak or stale
state. Only do this if `execv` causes a real problem, such as a supervisor
treating it as a crash. It is a separate change.

## Test plan

1. Unit tests in both radio repos, as listed above. Add a case for
   `reset id=3` → `NOK EINVAL`.
2. On hardware, for each radio: run `tx_info` and note `ts`. Run `reset`, then
   `tx_info` until it replies: `ts` is small and the counters are 0.
3. With the updated winject-l3, drop the radio's `OK` reply to `reset`
   (firewall the reply). The manager confirms through `ts` and does not reset
   twice.
