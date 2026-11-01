#!/usr/bin/env bash
# Prepares the machine for benchmark runs. Run it once after every boot, it needs root.
#
# Pins the uncore clock (the ring and L3 cache between the cores) to its maximum. It otherwise
# follows system activity, and with it the cost of moving a cache line between the engine and the
# threads reading its queues. Keeps the CPUs that aren't isolated out of the deep idle states (see
# below). Then checks the rest of the setup in results.md and warns about what differs. The CPU
# numbers assume the i7-14650HX the benchmark defaults are made for.
#
# Usage:   sudo prepare-machine.sh          pin the uncore clock, no deep idle, check the setup
#          sudo prepare-machine.sh --undo   restore the uncore clock's default minimum, allow
#                                           deep idle again
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

# No idle state deeper than C1 on the CPUs that aren't isolated (the OS, the loggers). Their timer
# ticks come at the same moment every 1 ms, and their wake-ups from the deep states stalled every
# busy core for a few us. Without them the exchange benchmark's response p99.9 went from 29.8 to
# 14.7 us, the engine's worst case from 29 to 9 us. The isolated CPUs get no ticks and are left as
# they are. Costs some power while those CPUs idle.
isolated=" "
for r in $(tr ',' ' ' < /sys/devices/system/cpu/isolated); do
    if [[ $r == *-* ]]; then isolated+="$(seq -s ' ' "${r%-*}" "${r#*-}") "; else isolated+="$r "; fi
done
changed=()
for c in /sys/devices/system/cpu/cpu[0-9]*; do
    n=${c##*cpu}
    [[ $isolated == *" $n "* ]] && continue
    for s in "$c"/cpuidle/state[2-9]; do
        [[ -d $s ]] || continue
        if [[ $undo == yes ]]; then echo 0 > "$s/disable"; else echo 1 > "$s/disable"; fi
    done
    changed+=("$n")
done
if (( ${#changed[@]} )); then
    names=$(cat /sys/devices/system/cpu/cpu"${changed[0]}"/cpuidle/state[2-9]/name 2> /dev/null | tr '\n' ' ' || true)
    echo "idle states ${names:-none }$([[ $undo == yes ]] && echo allowed || echo disabled) on CPUs ${changed[*]}"
fi
[[ $undo == yes ]] && exit 0

# Checks only, nothing below changes the machine.
warn() { echo "WARNING: $*" >&2; }
for f in isolated nohz_full; do
    cpus=$(cat "/sys/devices/system/cpu/$f")
    [[ $cpus == 2-15 ]] || warn "$f CPUs are '$cpus', expected 2-15 (boot parameters, see results.md)"
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
