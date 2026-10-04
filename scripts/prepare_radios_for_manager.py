#!/usr/bin/env python3
"""Configure radios for manager bw_test.

Always applied: stop any test_wifi_tx run, rx_filter_addr3 for --domain.
Optional (only if flagged): --channel, --modulation, --power, --cca/--no-cca.
D-plane port is fixed on the radio (UDP 9000 for inject, registration, and forward); the manager
registers itself as the forward peer.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent / "tools"
sys.path.insert(0, str(TOOLS))

import bw_test as bw  # noqa: E402

DEFAULT_DOMAIN = bw.DEFAULT_DOMAIN


def main() -> int:
    p = argparse.ArgumentParser(description="prepare radios for manager bw_test")
    p.add_argument("--a", default="192.168.253.11")
    p.add_argument("--b", default="192.168.253.12")
    p.add_argument(
        "--domain",
        default=DEFAULT_DOMAIN,
        help=f"air domain forwarded by both radios (addr3 CA:FE:BA:BE:<domain>), "
        f"hex 1..ffff (default {DEFAULT_DOMAIN})",
    )
    p.add_argument(
        "--any-domain",
        action="store_true",
        help="clear rx_filter_addr3 instead (forward any CA:FE:BA:BE addr3)",
    )
    p.add_argument(
        "--channel",
        type=int,
        default=None,
        help=f"radio_tx channel {bw.CHANNEL_MIN}-{bw.CHANNEL_MAX} "
        f"(omit to keep existing; 14 is 802.11b-only)",
    )
    p.add_argument(
        "--modulation",
        default=None,
        help="radio_tx modulation (omit to keep existing; DSSS/CCK required on channel 14)",
    )
    p.add_argument(
        "--power",
        type=int,
        default=None,
        help="radio_tx tx_power in dBm (omit to keep existing)",
    )
    p.add_argument("--verbose", action="store_true")
    p.add_argument(
        "--cca",
        action=argparse.BooleanOptionalAction,
        default=None,
        help="radio_tx cca; omit to keep existing",
    )
    args = p.parse_args()
    if args.channel is not None and not bw.channel_ok(args.channel):
        raise SystemExit(f"--channel must be {bw.CHANNEL_MIN}-{bw.CHANNEL_MAX}")
    if args.modulation is not None and not bw.modulation_ok_for_channel(
        args.modulation, args.channel
    ):
        raise SystemExit(
            "channel 14 rejects OFDM/MCS; use a DSSS/CCK --modulation "
            f"(got {args.modulation})"
        )
    quiet = not args.verbose
    domain = None if args.any_domain else bw.fmt_domain(args.domain)

    def configure_radio(ip: str) -> bool:
        cmds = ["test_wifi_tx count=0", bw.rx_filter_cmd(domain)]
        phy = bw.radio_tx_cmd(args.channel, args.modulation, args.cca, args.power)
        if phy is not None:
            cmds.append(phy)
        try:
            replies = bw.console(ip, cmds, quiet=quiet, timeout=5)
        except OSError as err:
            print(f"{ip}: console failed: {err}")
            return False
        if not bw.replies_ok(replies):
            print(f"{ip}: configure failed")
            return False
        return True

    ok_a = configure_radio(args.a)
    ok_b = configure_radio(args.b)
    if not (ok_a and ok_b):
        return 1
    ch = "unchanged" if args.channel is None else str(args.channel)
    mod = "unchanged" if args.modulation is None else args.modulation
    pwr = "unchanged" if args.power is None else str(args.power)
    if args.cca is None:
        cca = "unchanged"
    else:
        cca = "enabled" if args.cca else "disabled"
    print(
        f"ok domain={domain or 'any'} channel={ch} modulation={mod} power={pwr} "
        f"cca={cca} dplane={bw.DPLANE_PORT}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
