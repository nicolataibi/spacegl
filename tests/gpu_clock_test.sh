#!/bin/bash
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# gpu_clock_test.sh — tests/gpu_clock.sh, the GPU clock helpers of
# tests/gpu_golden_check.sh (2026.11.07.07), on fake sysfs trees (no GPU
# needed):
#   1. i915 (Arc A770 layout: card1 plus its connectors card1-DP-1 ...):
#      the card is found, the connectors are skipped, range/floor/ceiling
#      and the actual clock are read;
#   2. two GPUs: the one of the rendering vendor is chosen (an AMD and an
#      Intel card, hint "Intel(R) Arc(tm) A770 Graphics"), --gpu-card
#      forces the other;
#   3. xe and amdgpu layouts: clocks read (amdgpu: the level marked *);
#   4. pin: the floor goes to the top and back (i915, xe; amdgpu:
#      performance level high and back), also when the script that pinned
#      is interrupted (SIGINT: the EXIT trap restores it);
#   5. sampling: average, min, max of a clock that changes during the run,
#      the samples at 0 MHz (GPU asleep) counted apart, not averaged;
#   6. no supported card: gpu_find fails, gpu_pin refuses.
# The checks pass shell code in single quotes on purpose (it runs where the
# library is sourced); the library sets the GPU_ globals.
# shellcheck disable=SC2016,SC2034,SC1091
set -u
DIR=$(cd -- "$(dirname -- "$0")" && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_gpuclk.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT
rc=0
check() { if "${@:2}"; then echo "  ok   $1"; else echo "  FAIL $1"; rc=1; fi; }

# card <root> <name> <driver> <vendor> <device>
card() {
    mkdir -p "$1/$2" "$1/devices/$2/drivers/$3"
    mkdir -p "$1/pci/$2"
    ln -s "$1/pci/$2" "$1/$2/device"
    ln -s "$1/devices/$2/drivers/$3" "$1/pci/$2/driver"
    echo "$4" >"$1/pci/$2/vendor"
    echo "$5" >"$1/pci/$2/device"
}
i915() {   # i915 <root> <card> act min max rp0 rpn
    local c=$1/$2
    echo "$3" >"$c/gt_act_freq_mhz"; echo "$4" >"$c/gt_min_freq_mhz"; echo "$5" >"$c/gt_max_freq_mhz"
    echo "$6" >"$c/gt_RP0_freq_mhz"; echo "$7" >"$c/gt_RPn_freq_mhz"
}
# lib <root> <code> [args]: the code, with the library sourced on that tree, in a subshell
lib() {
    local root=$1 code=$2
    shift 2
    (GPU_SYSFS=$root; . "$DIR/gpu_clock.sh"; eval "$code")
}

# 1. i915, A770 layout
R1=$WORK/a770
card "$R1" card1 i915 0x8086 0x56a0
i915 "$R1" card1 1200 300 2400 2400 300
for c in card1-DP-1 card1-DP-2 card1-HDMI-A-1; do mkdir -p "$R1/$c/device/device"; done   # connectors: device/device is a directory
out=$(lib "$R1" 'gpu_find "" "Intel(R) Arc(tm) A770 Graphics (DG2)" && echo "$(basename "$GPU_CARD") $GPU_DRV $GPU_IDS $GPU_RPN $GPU_RP0 $GPU_MIN $GPU_MAX $(gpu_mhz)"' 2>&1)
check "i915: card1 found, connectors skipped, clocks read ($out)" [ "$out" = "card1 i915 8086:56a0 300 2400 300 2400 1200" ]

# 2. two GPUs, the rendering vendor wins; --gpu-card forces
R2=$WORK/two
card "$R2" card0 amdgpu 0x1002 0x73bf
printf '0: 500Mhz\n1: 1800Mhz *\n2: 2600Mhz\n' >"$R2/pci/card0/pp_dpm_sclk"
echo auto >"$R2/pci/card0/power_dpm_force_performance_level"
card "$R2" card1 i915 0x8086 0x56a0
i915 "$R2" card1 2000 300 2400 2400 300
out=$(lib "$R2" 'gpu_find "" "Intel(R) Arc(tm) A770 Graphics" && basename "$GPU_CARD"')
check "two GPUs: the Intel one for an Intel device ($out)" [ "$out" = card1 ]
out=$(lib "$R2" 'gpu_find "" "AMD Radeon RX 6900 XT" && basename "$GPU_CARD"')
check "two GPUs: the AMD one for an AMD device ($out)" [ "$out" = card0 ]
out=$(lib "$R2" 'gpu_find card0 "Intel(R) Arc(tm) A770 Graphics" && basename "$GPU_CARD"')
check "--gpu-card card0 forces it ($out)" [ "$out" = card0 ]

# 3. xe and amdgpu
R3=$WORK/xe
card "$R3" card0 xe 0x8086 0xe20b
f=$R3/pci/card0/tile0/gt0/freq0
mkdir -p "$f"
echo 2850 >"$f/act_freq"; echo 400 >"$f/min_freq"; echo 2850 >"$f/max_freq"; echo 2850 >"$f/rp0_freq"; echo 400 >"$f/rpn_freq"
out=$(lib "$R3" 'gpu_find && echo "$GPU_DRV $GPU_RPN $GPU_RP0 $(gpu_mhz)"')
check "xe: clocks read ($out)" [ "$out" = "xe 400 2850 2850" ]
out=$(lib "$R2" 'gpu_find card0 && echo "$GPU_DRV $GPU_RPN $GPU_RP0 $(gpu_mhz)"')
check "amdgpu: levels and the current one read ($out)" [ "$out" = "amdgpu 500 2600 1800" ]

# 4. pin and restore
out=$(lib "$R1" 'gpu_find && gpu_pin && cat "$GPU_CARD/gt_min_freq_mhz" && gpu_unpin && cat "$GPU_CARD/gt_min_freq_mhz"' | tr '\n' ' ')
check "i915: floor 300 -> 2400 -> 300 ($out)" [ "$out" = "2400 300 " ]
out=$(lib "$R3" 'gpu_find && gpu_pin && cat "$_MINF" && gpu_unpin && cat "$_MINF"' | tr '\n' ' ')
check "xe: floor 400 -> 2850 -> 400 ($out)" [ "$out" = "2850 400 " ]
out=$(lib "$R2" 'gpu_find card0 && gpu_pin && cat "$GPU_PIN_FILE" && f=$GPU_PIN_FILE && gpu_unpin && cat "$f"' | tr '\n' ' ')
check "amdgpu: performance level auto -> high -> auto ($out)" [ "$out" = "high auto " ]
# interrupted while pinned: the EXIT trap of the caller restores
GPU_SYSFS=$R1 bash -c '. "$1"; gpu_find; trap gpu_unpin EXIT; trap "exit 130" INT; gpu_pin; kill -INT $$; sleep 5' _ "$DIR/gpu_clock.sh"
check "i915: restored after SIGINT ($(cat "$R1/card1/gt_min_freq_mhz"))" [ "$(cat "$R1/card1/gt_min_freq_mhz")" = 300 ]

# 5. sampling a clock that moves
out=$(lib "$R1" '
    gpu_find
    gpu_sample_start "$1"
    for v in 1000 2000 2400 1600; do echo $v >"$GPU_CARD/gt_act_freq_mhz"; sleep 0.25; done
    gpu_sample_stop "$1"' "$WORK/samples")
read -r s_avg s_min s_max s_n s_idle <<<"$out"
check "sampling: min 1000, max 2400, average between, several samples, none asleep ($out)" \
    bash -c "[ $s_min = 1000 ] && [ $s_max = 2400 ] && [ $s_avg -gt 1000 ] && [ $s_avg -lt 2400 ] && [ $s_n -ge 6 ] && [ $s_idle = 0 ]"
out=$(lib "$R1" '
    gpu_find
    gpu_sample_start "$1"
    for v in 2400 0 2400 0; do echo $v >"$GPU_CARD/gt_act_freq_mhz"; sleep 0.25; done
    gpu_sample_stop "$1"' "$WORK/samples0")
read -r s_avg s_min s_max s_n s_idle <<<"$out"
check "sampling: a pinned GPU asleep half the time is 2400 MHz, about 50 % idle ($out)" \
    bash -c "[ $s_avg = 2400 ] && [ $s_min = 2400 ] && [ $s_max = 2400 ] && [ $s_idle -ge 30 ] && [ $s_idle -le 70 ]"
out=$(lib "$R1" '
    gpu_find
    gpu_sample_start "$1"
    echo 0 >"$GPU_CARD/gt_act_freq_mhz"; sleep 0.5
    gpu_sample_stop "$1"' "$WORK/samples1")
check "sampling: always asleep ($out)" bash -c '[[ "$1" == "0 0 0 "*" 100" ]]' _ "$out"

# 6. nothing supported
R6=$WORK/none
card "$R6" card0 simpledrm 0x0000 0x0000
check "no supported card: gpu_find fails" bash -c '! (GPU_SYSFS="$1"; . "$2"; gpu_find)' _ "$R6" "$DIR/gpu_clock.sh"
out=$(lib "$R6" 'gpu_find; gpu_pin; echo "rc=$? $GPU_PIN_WHY"')
check "no supported card: gpu_pin refuses ($out)" [ "$out" = "rc=1 no GPU clock interface found" ]

if [ $rc -ne 0 ]; then echo "FAIL: gpu_clock"; exit 1; fi
echo "PASS: gpu_clock"
