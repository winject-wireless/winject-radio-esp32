#!/usr/bin/env bash
# Build winject-manager from the winject-l3 checkout (sibling repo by default).
# Usage: ensure_winject_manager <firmware_repo_root> [build_manager_arm|build_manager_x86]
ensure_winject_manager() {
    local fw_root="${1:?firmware repo root required}"
    local tree="${2:-}"
    local l3="${WINJECT_L3_ROOT:-$(cd "$fw_root/../winject-l3" && pwd)}"
    if [[ ! -f "$l3/src/manager/CMakeLists.txt" ]]; then
        echo "error: winject-l3 not found at $l3 (set WINJECT_L3_ROOT)" >&2
        return 1
    fi
    # shellcheck source=/dev/null
    source "$l3/scripts/ensure_manager.sh"
    ensure_winject_manager "$l3" "$tree"
}
