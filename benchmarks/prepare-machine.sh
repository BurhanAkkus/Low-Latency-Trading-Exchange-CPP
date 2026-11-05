#!/usr/bin/env bash
# Prepares the machine for benchmark runs. Run it once after every boot, it needs root.
#
# Pins the uncore clock (the ring and L3 cache between the cores) to its maximum. It otherwise
# follows system activity, and with it the cost of moving a cache line between the engine and the
# threads reading its queues. Keeps the CPUs that aren't isolated out of the deep idle states and
# turns off C1E promotion (both below). Reserves the 2 MB pages for the exchange's shared memory.
# Turns on Cooler Boost (below). Then checks the rest of the setup in results.md and warns about
# what differs. The CPU numbers assume the i7-14650HX the benchmark defaults are made for, Cooler
# Boost the MSI Katana 17 HX.
#
# Usage:   sudo prepare-machine.sh          pin the uncore clock, no deep idle, no C1E promotion,
#                                           huge pages, Cooler Boost, check the setup
#          sudo prepare-machine.sh --undo   restore the uncore clock's default minimum, allow
#                                           deep idle and C1E promotion again, release the huge
#                                           pages, Cooler Boost off
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

# Kernel lockdown (Secure Boot) refuses writes to MSRs and to the EC (C1E, Cooler Boost below).
lockdown=/sys/kernel/security/lockdown
locked=no
[[ -e $lockdown ]] && ! grep -q '\[none\]' "$lockdown" && locked=yes

# No C1E promotion. On this CPU intel_idle uses the firmware's idle table, whose C1 is a plain
# MWAIT, and with C1E promotion on (the BIOS sets it) the CPU turns every C1 into C1E: the core
# drops to its lowest clock (800 MHz) while it sleeps and speeds up again when it wakes. The CPUs
# that aren't isolated wake every ~1 ms (CPU 0 for the timer, the loggers' E-cores to drain their
# queues), and the busy cores stalled for 4-9 us about 250 times a second. Off: the exchange
# benchmark's response p99 went from 3.6 to 1.5 us, p99.9 from 9.2 to 1.7, the engine's p99.9 from
# 5.7 to 0.45. Bit 1 of MSR_POWER_CTL (0x1FC) on every CPU, its other bits are written back as they
# are. A reboot turns it on again.
if [[ $locked == yes ]]; then
    echo "WARNING: kernel lockdown is on (Secure Boot), C1E promotion stays on" >&2
elif ! command -v rdmsr > /dev/null || ! command -v wrmsr > /dev/null; then
    echo "WARNING: no rdmsr/wrmsr (apt install msr-tools), C1E promotion stays on" >&2
else
    modprobe msr
    values() { rdmsr -a -c 0x1fc | awk '{print $NF}' | sort -u | tr '\n' ' '; }
    old=$(values)
    cpus=0
    for d in /dev/cpu/[0-9]*/msr; do
        c=${d#/dev/cpu/}
        c=${c%/msr}
        v=$(rdmsr -p "$c" -c 0x1fc)
        if [[ $undo == yes ]]; then v=$(( v | 0x2 )); else v=$(( v & ~0x2 )); fi
        wrmsr -p "$c" 0x1fc "$v"
        cpus=$(( cpus + 1 ))
    done
    echo "C1E promotion $([[ $undo == yes ]] && echo on || echo off): MSR 0x1FC ${old}-> $(values)on $cpus CPUs"
fi

# 2 MB pages for the exchange's shared memory (common/shared-region.h): its regions are files in
# HUGE_DIR on hugetlbfs, owned by the user who ran sudo. On 4 KB pages the 255 clients' rings are
# ~8000 pages, more address translations than the TLB holds. 32 pages leave room over the order
# and market data regions (17 and 3). Both are gone after a reboot.
HUGE_DIR=/dev/hugepages/exchange
HUGE_PAGES=32
if [[ $undo == yes ]]; then
    rm -rf -- "${HUGE_DIR:?}"
    echo 0 > /proc/sys/vm/nr_hugepages
    echo "huge pages released"
elif ! mountpoint -q /dev/hugepages; then
    echo "WARNING: no hugetlbfs on /dev/hugepages, the exchange can't create its shared memory" >&2
else
    # Only more: a larger reservation is left as it is.
    (( $(cat /proc/sys/vm/nr_hugepages) >= HUGE_PAGES )) || echo "$HUGE_PAGES" > /proc/sys/vm/nr_hugepages
    reserved=$(cat /proc/sys/vm/nr_hugepages)
    # The kernel reserves fewer when free memory is too fragmented for 2 MB blocks.
    (( reserved >= HUGE_PAGES )) || echo "WARNING: only $reserved of $HUGE_PAGES huge pages reserved" >&2
    mkdir -p "$HUGE_DIR"
    chown "${SUDO_USER:-root}": "$HUGE_DIR"
    chmod 700 "$HUGE_DIR"
    echo "huge pages: $reserved of 2 MB reserved, shared memory in $HUGE_DIR"
fi

# Cooler Boost, the fans at full speed, through the laptop's embedded controller. Under Linux the
# fans otherwise stay at ~3500 rpm whatever the temperature (MSI's fan curve is applied by its
# Windows software) and the CPU reaches its 95 C throttle point. Bit 7 of EC address 0x98, for the
# Katana 17 HX's EC firmware only (EC_FIRMWARE at 0xA0, MSI's usual layout). Writing the EC needs
# ec_sys's write_support, which kernel lockdown refuses: Secure Boot has to be off. A reboot clears it.
EC_FIRMWARE=17L7EMS1.106
ec=/sys/kernel/debug/ec/ec0/io
if [[ $locked == yes ]]; then
    echo "WARNING: kernel lockdown is on (Secure Boot), Cooler Boost not set, the fans stay at ~3500 rpm" >&2
else
    modprobe -r ec_sys 2> /dev/null || true
    modprobe ec_sys write_support=1
    fw=$(dd if=$ec bs=1 skip=$((0xA0)) count=${#EC_FIRMWARE} 2> /dev/null | tr -d '\0')
    if [[ $fw != "$EC_FIRMWARE" ]]; then
        echo "WARNING: EC firmware is '$fw', not $EC_FIRMWARE: Cooler Boost not set, its address may differ" >&2
    else
        old=$(dd if=$ec bs=1 skip=$((0x98)) count=1 2> /dev/null | xxd -p)
        if [[ $undo == yes ]]; then new=$(( 0x$old & 0x7f )); else new=$(( 0x$old | 0x80 )); fi
        printf "\\x$(printf '%02x' $new)" | dd of=$ec bs=1 seek=$((0x98)) count=1 conv=notrunc 2> /dev/null
        echo "Cooler Boost $([[ $undo == yes ]] && echo off || echo on): EC 0x98 $old -> $(dd if=$ec bs=1 skip=$((0x98)) count=1 2> /dev/null | xxd -p)"
    fi
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
