#!/bin/busybox sh
# Bounded read-only sample of an explicitly identified decoder service process.
set -eu
pid=$1
case "$pid" in ''|*[!0-9]*) exit 2;; esac
test "$(readlink /proc/$pid/exe)" = /vendor/bin/sq2-c2-experimental
sleep 7
n=0
while [ "$n" -lt 85 ] && [ -d "/proc/$pid/task" ]; do
 for task in /proc/$pid/task/*; do
  stack=$(cat "$task/stack" 2>/dev/null || true)
  case "$stack" in *vdec*|*vb2*|*iommu*|*dma_buf*|*__fput*)
   echo "SAMPLE=$n TASK=${task##*/} UPTIME=$(cat /proc/uptime)"
   printf '%s\n' "$stack"
   ;;
  esac
 done
 n=$((n+1))
 sleep 0.2
done
echo STACK_SAMPLE_END
