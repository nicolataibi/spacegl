# shellcheck shell=bash
# SpaceGL - the GPU clock for tests/gpu_golden_check.sh (2026.11.07.07).
#
# Sourced, not run. A light benchmark lets the GPU stay at a low clock, a
# heavy one raises it and the clock stays up for a while: on an Arc A770 the
# GPU-driven Vulkan frame of the busy quadrant measured 1.1 ms after the old
# (heavy) CPU-driven run and 1.9 ms after the new (light) one, with the same
# code; pinned at the top clock both were 0.82 ms. These helpers read the
# GPU's actual clock while a benchmark runs and can pin its minimum to the
# top for the whole run (restored at exit).
#
# Kernel drivers: i915 and xe (Intel), amdgpu (AMD): read and pin; nvidia:
# read through nvidia-smi, no pin. Anything else: no clock data (n/a).
#
# GPU_SYSFS (default /sys/class/drm) is the DRM class directory: the test
# tests/gpu_clock_test.sh points it at a fake tree.
#
# Interface (all globals prefixed GPU_):
#   gpu_find [cardN] [vendor-hint]  choose the card; sets GPU_CARD, GPU_DRV,
#                                   GPU_PCI, GPU_IDS, GPU_RPN, GPU_RP0,
#                                   GPU_MIN, GPU_MAX (MHz, "" when unknown)
#   gpu_mhz                         the actual clock now, in MHz ("" unknown)
#   gpu_pin / gpu_unpin             minimum clock to the top / back
#   gpu_sample_start FILE, gpu_sample_stop FILE -> "avg min max n"
# The GPU_ globals are read by the script that sources this file.
# shellcheck disable=SC2034
GPU_SYSFS=${GPU_SYSFS:-/sys/class/drm}
GPU_CARD=""
GPU_DRV=""
GPU_PCI=""
GPU_IDS=""
GPU_RPN=""
GPU_RP0=""
GPU_MIN=""
GPU_MAX=""
GPU_PIN_FILE=""
GPU_PIN_OLD=""
GPU_PIN_NEW=""
GPU_SAMPLER=""
GPU_PIN_WHY=""

_gpu_read() { [ -r "$1" ] && tr -d '[:space:]' <"$1"; }

# the files of a card's clock, by driver
_gpu_files() {   # _gpu_files card drv -> sets _ACT _MINF _MAXF _RP0F _RPNF
    local c=$1
    _ACT="" _MINF="" _MAXF="" _RP0F="" _RPNF=""
    case "$2" in
        i915)
            _ACT=$c/gt_act_freq_mhz _MINF=$c/gt_min_freq_mhz _MAXF=$c/gt_max_freq_mhz
            _RP0F=$c/gt_RP0_freq_mhz _RPNF=$c/gt_RPn_freq_mhz ;;
        xe)
            local f=$c/device/tile0/gt0/freq0
            _ACT=$f/act_freq _MINF=$f/min_freq _MAXF=$f/max_freq _RP0F=$f/rp0_freq _RPNF=$f/rpn_freq ;;
    esac
}

_gpu_vendor_of_hint() {   # a device name -> PCI vendor id
    case "$1" in
        *Intel* | *Arc* | *UHD* | *Iris*) echo 0x8086 ;;
        *AMD* | *Radeon*) echo 0x1002 ;;
        *NVIDIA* | *GeForce* | *Quadro* | *RTX*) echo 0x10de ;;
    esac
}

gpu_find() {   # gpu_find [cardN] [device name of the GPU in use]
    local want=${1:-} hint_vendor best="" best_rp0=-1 c name drv vendor rp0
    hint_vendor=$(_gpu_vendor_of_hint "${2:-}")
    for c in "$GPU_SYSFS"/card*; do
        name=${c##*/}
        [[ $name =~ ^card[0-9]+$ ]] || continue   # not the connectors (card1-DP-1 ...)
        [ -n "$want" ] && [ "$name" != "$want" ] && continue
        drv=$(basename "$(readlink -f "$c/device/driver" 2>/dev/null)" 2>/dev/null)
        case "$drv" in i915 | xe | amdgpu | nvidia) ;; *) continue ;; esac
        vendor=$(_gpu_read "$c/device/vendor")
        if [ -z "$want" ] && [ -n "$hint_vendor" ] && [ -n "$vendor" ] && [ "$vendor" != "$hint_vendor" ]; then
            continue   # another vendor's GPU than the one rendering
        fi
        _gpu_files "$c" "$drv"
        rp0=$(_gpu_read "$_RP0F")
        [ -n "$rp0" ] || rp0=0
        if [ -z "$best" ] || [ "$rp0" -gt "$best_rp0" ]; then best=$c best_rp0=$rp0; fi   # the biggest GPU of that vendor
    done
    [ -n "$best" ] || return 1
    GPU_CARD=$best
    GPU_DRV=$(basename "$(readlink -f "$best/device/driver")")
    GPU_PCI=$(basename "$(readlink -f "$best/device")")
    GPU_IDS="$(_gpu_read "$best/device/vendor" | sed 's/^0x//'):$(_gpu_read "$best/device/device" | sed 's/^0x//')"
    _gpu_files "$best" "$GPU_DRV"
    GPU_RP0=$(_gpu_read "$_RP0F") GPU_RPN=$(_gpu_read "$_RPNF")
    GPU_MIN=$(_gpu_read "$_MINF") GPU_MAX=$(_gpu_read "$_MAXF")
    if [ "$GPU_DRV" = amdgpu ]; then   # pp_dpm_sclk: "0: 500Mhz", "1: 2600Mhz *"
        local levels=$best/device/pp_dpm_sclk
        GPU_RPN=$(sed -n 's/^[0-9]*: *\([0-9]*\)[Mm][Hh]z.*/\1/p' "$levels" 2>/dev/null | sort -n | head -n 1)
        GPU_RP0=$(sed -n 's/^[0-9]*: *\([0-9]*\)[Mm][Hh]z.*/\1/p' "$levels" 2>/dev/null | sort -n | tail -n 1)
        GPU_MIN=$GPU_RPN GPU_MAX=$GPU_RP0
    fi
    return 0
}

gpu_mhz() {
    case "$GPU_DRV" in
        i915 | xe) _gpu_read "$_ACT" ;;
        amdgpu) sed -n 's/^[0-9]*: *\([0-9]*\)[Mm][Hh]z *\*.*/\1/p' "$GPU_CARD/device/pp_dpm_sclk" 2>/dev/null | head -n 1 ;;
        nvidia)
            command -v nvidia-smi >/dev/null 2>&1 &&
                nvidia-smi --query-gpu=clocks.gr --format=csv,noheader,nounits 2>/dev/null | head -n 1 |
                tr -d '[:space:]' ;;
    esac
}

_gpu_write() {   # writable (root, or a test's fake tree): directly; else through sudo
    if [ -w "$1" ]; then
        printf '%s\n' "$2" >"$1"
    else
        printf '%s\n' "$2" | sudo tee "$1" >/dev/null
    fi
}

# Raise the minimum clock to the top (amdgpu: performance level "high").
# Call it in the shell that will restore (not in $(...)): it keeps the old
# value for gpu_unpin. Returns 1 with the reason in GPU_PIN_WHY when it
# cannot.
gpu_pin() {
    case "$GPU_DRV" in
        i915 | xe)
            GPU_PIN_FILE=$_MINF GPU_PIN_NEW=$GPU_RP0 ;;
        amdgpu)
            GPU_PIN_FILE=$GPU_CARD/device/power_dpm_force_performance_level GPU_PIN_NEW=high ;;
        nvidia) GPU_PIN_WHY="the nvidia driver has no clock floor in sysfs (nvidia-smi --lock-gpu-clocks does it)"; return 1 ;;
        *) GPU_PIN_WHY="no GPU clock interface found"; return 1 ;;
    esac
    if [ -z "$GPU_PIN_NEW" ] || [ ! -r "$GPU_PIN_FILE" ]; then
        GPU_PIN_WHY="$GPU_PIN_FILE not readable"
        GPU_PIN_FILE=""
        return 1
    fi
    GPU_PIN_OLD=$(_gpu_read "$GPU_PIN_FILE")
    if ! _gpu_write "$GPU_PIN_FILE" "$GPU_PIN_NEW"; then
        GPU_PIN_WHY="cannot write $GPU_PIN_FILE (root or sudo needed)"
        GPU_PIN_FILE=""
        return 1
    fi
    [ "$(_gpu_read "$GPU_PIN_FILE")" = "$GPU_PIN_NEW" ] || { GPU_PIN_WHY="the driver did not take $GPU_PIN_NEW"; gpu_unpin; return 1; }
    return 0
}

gpu_unpin() {   # idempotent: the EXIT trap calls it
    [ -n "$GPU_PIN_FILE" ] || return 0
    _gpu_write "$GPU_PIN_FILE" "$GPU_PIN_OLD" || true
    GPU_PIN_FILE=""
}

# The clock every 0.1 s while a benchmark runs.
gpu_sample_start() {
    : >"$1"
    [ -n "$GPU_DRV" ] || return 0
    (while :; do
        gpu_mhz >>"$1" 2>/dev/null && echo >>"$1"
        sleep 0.1
    done) &
    GPU_SAMPLER=$!
}

gpu_sample_stop() {   # prints "avg min max n" (or "- - - 0")
    if [ -n "$GPU_SAMPLER" ]; then
        kill "$GPU_SAMPLER" 2>/dev/null
        wait "$GPU_SAMPLER" 2>/dev/null
        GPU_SAMPLER=""
    fi
    awk '/^[0-9]+$/ { n++; s += $1; if (n == 1 || $1 < mn) mn = $1; if ($1 > mx) mx = $1 }
         END { if (n) printf "%.0f %d %d %d\n", s / n, mn, mx, n; else print "- - - 0" }' "$1"
}
