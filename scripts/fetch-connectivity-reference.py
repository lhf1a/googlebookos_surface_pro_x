#!/usr/bin/env python3
"""Fetch public AOSP sources used to review the generic HAL before deployment."""
from pathlib import Path
from urllib.request import urlopen
import base64

out=Path(__file__).resolve().parents[1]/'out/connectivity/reference'
out.mkdir(parents=True,exist_ok=True)
base='https://android.googlesource.com/'
paths={
    'goldfish-interface.cpp': 'device/generic/goldfish/+/37eb7d78f252d851dc5021d0d24d72c262049cc5/wifi/wifi_hal/interface.cpp',
    'goldfish-halstate.cpp': 'device/generic/goldfish/+/37eb7d78f252d851dc5021d0d24d72c262049cc5/wifi/wifi_hal/halstate.cpp',
    'goldfish-info.cpp': 'device/generic/goldfish/+/37eb7d78f252d851dc5021d0d24d72c262049cc5/wifi/wifi_hal/info.cpp',
    'bt-hci.rs': 'device/google/cuttlefish/+/refs/heads/main/guest/hals/bluetooth/src/hci.rs',
    'bt-main.rs': 'device/google/cuttlefish/+/refs/heads/main/guest/hals/bluetooth/src/main.rs',
}
for name,path in paths.items():
    try:
        with urlopen(base+path+'?format=TEXT',timeout=30) as response: data=base64.b64decode(response.read())
        (out/name).write_bytes(data)
        print(name,len(data))
    except Exception as e: print(name,type(e).__name__,str(e))
