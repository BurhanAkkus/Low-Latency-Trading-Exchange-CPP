#!/usr/bin/env bash
# Prepares the machine for benchmark runs. Run it once after every boot, it needs root.
#
# Pins the uncore clock (the ring and L3 cache between the cores) to its maximum. It otherwise
# follows system activity, and with it the cost of moving a cache line between the engine and the
# threads reading its queues. Then checks the rest of the setup in results.md and warns about what
# differs. The CPU numbers assume the i7-14650HX the benchmark defaults are made for.
#
# Usage:   sudo prepare-machine.sh          pin the uncore clock, check the setup
#          sudo prepare-machine.sh --undo   restore the uncore clock's default minimum
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "needs root: sudo $0 $*" >&2
    exit 1
fi
undo=no
[[ ${1:-} == --undo ]] && undo=yes

for d in /sys/devices/system/cpu/intel_uncore_frequency/package_*_die_*; do
    if [[ ! -d $d ]]; then
        echo "WARNING: no intel_uncore_frequency driver, the uncore clock is not pinned" >&2
        break
    fi
    if [[ $undo == yes ]]; then
        cat "$d/initial_min_freq_khz" > "$d/min_freq_khz"
    else
        cat "$d/max_freq_khz" > "$d/min_freq_khz"
    fi
    echo "$(basename "$d"): uncore $(($(cat "$d/min_freq_khz") / 1000))-$(($(cat "$d/max_freq_khz") / 1000)) MHz"
done
[[ $undo == yes ]] && exit 0

# Checks only, nothing below changes the machine.
warn() { echo "WARNING: $*" >&2; }
for f in isolated nohz_full; do
    cpus=$(cat "/sys/devices/system/cpu/$f")
    [[ $cpus == 4-13 ]] || warn "$f CPUs are '$cpus', expected 4-13 (boot parameters, see results.md)"
done
ac=0
for p in /sys/class/power_supply/*; do
    [[ $(cat "$p/type") == Mains ]] && ac=$(cat "$p/online")
done
[[ $ac == 1 ]] || warn "not on AC power, the clock speed differs on battery"
if command -v powerprofilesctl > /dev/null; then
    profile=$(powerprofilesctl get 2> /dev/null || echo unknown)
    [[ $profile == performance ]] || warn "power profile is '$profile', expected performance"
fi
[[ $(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2> /dev/null || echo 0) == 0 ]] || warn "turbo is off"
echo "done"
