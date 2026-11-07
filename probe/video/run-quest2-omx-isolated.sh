#!/system/bin/sh
# Private process only. Do not register a service, bind a global library, or
# restart Venus/PAS. The donor's failures must be returned, never simulated.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here"
sha256sum -c SHA256SUMS
unset LD_PRELOAD
export LD_LIBRARY_PATH="$here/lib64:/vendor/lib64:/system/lib64"
exec "$here/quest2-omx-probe" "$here/lib64/libOmxCore.so" "$@"
