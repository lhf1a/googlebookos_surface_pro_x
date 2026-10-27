#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build/tools-test2
O="$ROOT/out/test2/services"
mkdir -p "$B/services" "$O"
cd "$B/services"
for p in qrtr tqftpserv pd-mapper rmtfs; do cp -a "$ROOT/upstream/$p" ./; done
CC=aarch64-linux-gnu-gcc
$CC -static -O2 -Iqrtr/lib qrtr/lib/logging.c qrtr/src/{addr,ns,map,hash,waiter,util}.c -o "$O/qrtr-ns"
for f in logging qrtr qmi; do $CC -O2 -Iqrtr/lib -Iqrtr/src -c "qrtr/lib/$f.c" -o "$f.o"; done
aarch64-linux-gnu-ar rcs libqrtr.a logging.o qrtr.o qmi.o
$CC -static -O2 -Iqrtr/lib tqftpserv/{translate,tqftpserv}.c libqrtr.a -o "$O/tqftpserv"
LIBS="$B/arm64-libs/usr/lib/aarch64-linux-gnu"
$CC -static -O2 -Iqrtr/lib -I"$B/arm64-libs/usr/include" pd-mapper/{pd-mapper,assoc,json,servreg_loc,lzma_decomp}.c libqrtr.a "$LIBS/liblzma.a" -pthread -o "$O/pd-mapper"
$CC -O2 -Iqrtr/lib -I"$B/arm64-libs/usr/include" rmtfs/{qmi_rmtfs,rmtfs,rproc,sharedmem,storage,util}.c libqrtr.a -L"$LIBS" -Wl,-rpath-link,"$LIBS" -ludev -pthread -o "$O/rmtfs"
find "$O" -maxdepth 1 -type f -exec aarch64-linux-gnu-strip {} +
mkdir -p "$O/lib"
for f in ld-linux-aarch64.so.1 libc.so.6 libudev.so.1 libcap.so.2; do cp -L "$LIBS/$f" "$O/lib/$f"; done
aarch64-linux-gnu-readelf -d "$O/rmtfs" | grep NEEDED
qemu-aarch64 -L "$O" "$O/rmtfs" -? || [[ $? -eq 1 ]]
cd "$B"
echo '912ea06f74e30a8e36fbb68064d6cdff218d8d591db0fc5d75dee6c81ac7fc0a  '"$ROOT/downloads/wpa_supplicant-2.11.tar.gz" | sha256sum -c -
tar -xf "$ROOT/downloads/wpa_supplicant-2.11.tar.gz"
cd wpa_supplicant-2.11/wpa_supplicant
cat > .config <<EOF
CONFIG_DRIVER_NL80211=y
CONFIG_LIBNL32=y
CONFIG_CTRL_IFACE=y
CONFIG_BACKEND=file
CONFIG_TLS=internal
CONFIG_INTERNAL_LIBTOMMATH=y
CONFIG_IEEE80211W=y
CONFIG_DEBUG_FILE=y
CONFIG_NO_WPA_PASSPHRASE=y
EOF
PKG_CONFIG_LIBDIR="$B/install/lib/pkgconfig" make -j16 CC="$CC" LDFLAGS="-static -L$B/install/lib" wpa_supplicant wpa_cli
aarch64-linux-gnu-strip wpa_supplicant wpa_cli
cp wpa_supplicant wpa_cli "$O/"
qemu-aarch64 "$O/wpa_supplicant" -v
