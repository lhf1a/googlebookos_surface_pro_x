#!/usr/bin/env python3
"""Every unsupported Radio request gets its matching response, never a timeout."""
import re
from pathlib import Path


def methods(path):
    text = path.read_text()
    # Generated NDK declarations are one logical line; deprecation attributes
    # follow the parameter list and do not change the override signature.
    found = re.findall(r'virtual ::ndk::ScopedAStatus (\w+)\((.*?)\)(?: __attribute__\(\(.*?\)\))? = 0;', text)
    return dict(found)


def generate(directory, destination):
    lines = ['// Generated from frozen Radio V3. Do not edit.', '#pragma once', '#include <mutex>']
    for part in ('config', 'modem', 'network', 'sim', 'data', 'voice', 'messaging'):
        cls = 'Radio' + part.capitalize()
        base = directory / 'aidl/android/hardware/radio' / part
        lines += [f'#include <aidl/android/hardware/radio/{part}/Bn{cls}.h>',
                  f'#include <aidl/android/hardware/radio/{part}/I{cls}Response.h>',
                  f'#include <aidl/android/hardware/radio/{part}/I{cls}Indication.h>']
        req = methods(base / f'I{cls}.h')
        rsp = methods(base / f'I{cls}Response.h')
        ns = 'aidl::android::hardware::radio::' + part
        lines += [f'class {cls}Base : public {ns}::Bn{cls} {{', 'protected:',
                  '  std::mutex callbacksMutex;',
                  f'  std::shared_ptr<{ns}::I{cls}Response> response;',
                  f'  std::shared_ptr<{ns}::I{cls}Indication> indication;',
                  '  auto R() { std::lock_guard<std::mutex> l(callbacksMutex); return response; }',
                  '  auto I() { std::lock_guard<std::mutex> l(callbacksMutex); return indication; }',
                  'public:']
        for name, params in req.items():
            if name in ('getInterfaceVersion', 'getInterfaceHash'):
                continue
            if name == 'setResponseFunctions':
                names = re.findall(r'\b(in_\w+)\b', params)
                body = f'std::lock_guard<std::mutex> l(callbacksMutex); response={names[0]}; indication={names[1]};'
            elif name == 'responseAcknowledgement':
                body = ''  # All responses/indications use the non-ACK type.
            else:
                response = {'hangup': 'hangupConnectionResponse'}.get(name, name + 'Response')
                if response not in rsp:
                    raise RuntimeError(f'No matching response for {cls}.{name}')
                count = len(re.findall(r'\bin_\w+\b', rsp[response]))
                if 'in_serial' not in params or count < 1:
                    raise RuntimeError(f'Unrecognized response shape: {name}')
                args = 'info(in_serial, RadioError::REQUEST_NOT_SUPPORTED)' + ', {}' * (count - 1)
                body = f'if(auto r=R()) r->{response}({args});'
            lines += [f'  ::ndk::ScopedAStatus {name}({params}) override {{ {body} return ::ndk::ScopedAStatus::ok(); }}']
        lines += ['};']
    destination.write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1] / 'out/lte-radio-20261103/generated'
    generate(root / 'include', root / 'include/sq2-radio-defaults.h')
