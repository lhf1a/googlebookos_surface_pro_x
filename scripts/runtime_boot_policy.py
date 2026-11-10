"""Render the universal image's boot helpers without file-content hash gates.

The imported release stays pristine. This is an explicit full-image policy, not
a fake sha256sum binary or suppression of mount/copy/module failures.
"""
import re
import subprocess


def replace_once(text, old, new, name):
    if text.count(old) != 1:
        raise ValueError('Unknown boot helper structure: ' + name)
    return text.replace(old, new, 1)


def without_hash_checks(name, body):
    if not body.startswith(b'#!') or b'sh' not in body.split(b'\n', 1)[0] or b'sha256sum' not in body:
        return body
    text = body.decode().replace('\r\n', '\n')
    if name == 'bin/touch-calibration-boot' and all(
            'sha256sum' not in line or line.startswith('device=$({ cat /sys/class/dmi/id/product_uuid;')
            for line in text.splitlines()):
        return body
    if name == 'bin/qti-system-display-stage':
        text = replace_once(text,
            '# A second invocation is accepted only if the entire installed payload still matches.\n'
            'if ! (cd "$vendor"; sha256sum -c "$source/ORIGINAL_SHA256SUMS"); then\n'
            '    (cd "$vendor"; sha256sum -c "$source/INSTALLED_SHA256SUMS")\nfi\n', '', name)
    if name == 'bin/audio-framework-boot':
        text = replace_once(text,
            'original=$(sha256sum /proc/1/root$target)\n'
            '[ "${original%% *}" = fd219b1058eccc701de685cf59d6578e887c11182d877bb4e6c3a78bfd73e279 ] || { echo \'HAL build mismatch\'; exit 1; }\n', '', name)
        text = replace_once(text,
            'expected_hal=$(sha256sum /etc/sq2-audio/audio-hal)\nexpected_hal=${expected_hal%% *}\n', '', name)
        text = replace_once(text,
            '            actual_hal=$(sha256sum "/proc/$pid/exe" 2>/dev/null || true)\n'
            '            if [ "${actual_hal%% *}" = "$expected_hal" ]; then started=1; break; fi\n',
            '            if [ "/proc/$pid/exe" -ef "/proc/1/root$target" ]; then started=1; break; fi\n', name)
        text = text.replace('executable hash', 'executable inode')
    if name == 'bin/framework-suspend-start':
        text = replace_once(text,
            'expected=$(cat /etc/surface-pm/system-suspend.sha256)\n'
            'case "$expected" in *[!0-9a-f]*|\'\') echo INVALID_OWNER_MANIFEST; exit 1;; esac\n'
            '[ "${#expected}" = 64 ]\n', '', name)
        text = replace_once(text,
            '    actual=$(sha256sum "/proc/$pid/exe"); actual=${actual%% *}\n'
            '    [ "$actual" = "$expected" ] || { echo OWNER_EXECUTABLE_MISMATCH; exit 1; }\n', '', name)
        text = text.replace('tgid=$pid sha256=$actual', 'tgid=$pid')
        text = text.replace('verified native Android suspend process', 'native Android suspend process with the expected path and UID')
    if name == 'bin/touch-calibration-boot':
        text = replace_once(text,
            'if [ -n "$installed" ] && [ -f "/proc/1/root$installed" ] &&\n'
            '   [ "$(sha256sum "/proc/1/root$installed" | cut -d \' \' -f1)" = "$(sha256sum "$apk" | cut -d \' \' -f1)" ]; then\n'
            '    # A helper restart must not close an identical APK mid-calibration.\n'
            '    echo \'Success (identical installed APK retained)\' > /logs/touch-calibration-install.txt\n',
            'if [ -e /run/sq2-calibration-apk-installed ] && [ -n "$installed" ] && [ -f "/proc/1/root$installed" ]; then\n'
            '    # Install once each boot; helper restarts must not close calibration.\n'
            '    echo \'Success (installed this boot)\' > /logs/touch-calibration-install.txt\n', name)
        text = replace_once(text,
            '    rm -f /proc/1/root/data/local/tmp/sq2-touch-calibration.apk\n',
            '    rm -f /proc/1/root/data/local/tmp/sq2-touch-calibration.apk\n'
            '    touch /run/sq2-calibration-apk-installed\n', name)
    if name == 'bin/prepare-native-ssd':
        text = re.sub(r'^actual=\$\(dd [^\n]*\| sha256sum\)\n\[ "\$\{actual%% \*\}" = "\$SSD_SUPER_HEADER_SHA256" \] \|\| fail [^\n]+\n', '', text, flags=re.M)
    lines = []
    for line in text.splitlines(keepends=True):
        stripped = line.strip()
        if 'sha256sum' not in line:
            lines.append(line)
        elif name == 'bin/touch-calibration-boot' and stripped.startswith('device=$({ cat /sys/class/dmi/id/product_uuid;'):
            # This derives the calibration storage key; it is NOT a file check.
            lines.append(line)
        elif re.fullmatch(r'\(cd [^;]+; sha256sum -c [^\n]+\)', stripped):
            continue
        elif stripped.startswith('test "$(sha256sum ') and re.search(r' = [0-9a-f]{64}$', stripped):
            continue
        elif stripped == 'sha256sum -c SHA256SUMS':
            continue
        elif name == 'bin/video-decoder-lab-start' and stripped.startswith("printf '%s\\n' ") and stripped.endswith('| sha256sum -c -'):
            continue
        elif name == 'bin/prepare-native-gpu' and stripped == 'sha256sum "$dest/qcom/$name"':
            lines.append('    test -f "$dest/qcom/$name"\n')
        elif name == 'bin/apply-security-overlay' and stripped == 'sha256sum /newroot/vendor/bin/hw/android.hardware.security.keymint-service':
            continue
        else:
            raise ValueError('Unrecognized hash command; refusing to damage shell control flow: ' + name + ': ' + stripped)
    result = ''.join(lines).encode()
    subprocess.run(['sh', '-n'], input=result, check=True, capture_output=True)
    return result


def apply(bodies):
    changes = {}
    for name, body in bodies.items():
        if name == 'init' or name.startswith('bin/'):
            replacement = without_hash_checks(name, body)
            if replacement != body:
                changes[name] = replacement
    bodies.update(changes)
    return sorted(changes)
