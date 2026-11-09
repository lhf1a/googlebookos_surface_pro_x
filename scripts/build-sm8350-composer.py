#!/usr/bin/env python3
"""Build the complete source QTI composer against matched SDM and real platform inputs."""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MODULES = {
    'qservice': ['libqservice/' + name + '.cpp' for name in
                 ('QService', 'IQService', 'IQClient', 'IQHDMIClient')],
    'qdutils': ['libqdutils/qd_utils.cpp', 'libqdutils/display_config.cpp'],
    'histogram': ['libhistogram/histogram_collector.cpp', 'libhistogram/ringbuffer.cpp'],
    'gpu_tonemapper': ['gpu_tonemapper/' + name + '.cpp' for name in
                      ('TonemapFactory', 'glengine', 'EGLImageBuffer', 'EGLImageWrapper', 'Tonemapper')],
}
LINK_LIBS = {
    'qservice': ['binder', 'c++', 'utils', 'cutils', 'log'],
    'qdutils': ['binder', 'qservice', 'c++', 'utils', 'cutils', 'log'],
    'histogram': ['c++', 'utils', 'cutils', 'log', 'base', 'drm'],
    'gpu_tonemapper': ['c++', 'utils', 'cutils', 'log', 'ui', 'grallocutils',
                      'qdMetaData', 'EGL', 'GLESv2', 'GLESv3'],
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path):
    return json.loads(path.read_text())


def require_hash(path, expected):
    if sha(path) != expected:
        raise ValueError('Input hash mismatch: ' + str(path))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('display-build', 'allocator-command', 'dependencies', 'extra-platform', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args = argparse.Namespace(**{key: value.resolve() for key, value in vars(args).items()})
    manifest_path = args.display_build / 'BUILD.json'
    display = read_json(manifest_path)
    # A source and library pair must come from the same completed SDM build.
    for name, digest in display['inputs'].items():
        path = Path(name)
        if path.is_relative_to(args.display_build):
            require_hash(path, digest)
    for name, digest in display['sha256'].items():
        require_hash(args.display_build / name, digest)
    argv = read_json(args.allocator_command)
    values = {argv[i][2:].replace('-', '_'): Path(argv[i + 1]).resolve()
              for i in range(2, len(argv), 2)}
    refs = values['references']
    donor = values['donor'] / 'lib64'
    deps = args.dependencies
    donor_pins = read_json(ROOT / 'configs/quest2-v77-display-reference.json')['files']
    for name in ('libgrallocutils.so', 'libqdMetaData.so', 'libdisplayconfig.qti.so',
                 'vendor.qti.hardware.display.composer@3.0.so',
                 *('android.hardware.graphics.composer@2.' + v + '.so' for v in ('1', '2', '3', '4'))):
        require_hash(donor / name, donor_pins['/lib64/' + name])
    args.output.mkdir(parents=True, exist_ok=False)
    source = args.output / 'source'
    shutil.copytree(args.display_build / 'source', source)
    # The SDM-only stage omits shader text; preserve the original source's shaders.
    repositories = read_json(refs / 'repositories.json')
    reference = next(item for item in repositories if item['repo'] == 'LineageOS/android_hardware_qcom_display')
    blobs = {item['path']: item['sha'] for item in reference['tree']['tree'] if item['type'] == 'blob'}
    for name in ('forward_tonemap.inl', 'fullscreen_vertex_shader.inl', 'rgba_inverse_tonemap.inl'):
        relative = 'gpu_tonemapper/' + name
        data = (refs / 'display-source' / relative).read_bytes()
        blob = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        if blob != blobs[relative]:
            raise ValueError('Unexpected shader source: ' + name)
        (source / relative).write_bytes(data)
    allocator_pins = read_json(ROOT / 'probe/graphics/sm8350-hwc-allocator-pins.json')
    if not display.get('hwc_allocator_mapper5'):
        raise ValueError('The matched SDM build must include the mapper5 HWC allocator')
    for name, pin in allocator_pins['files'].items():
        if name.startswith('composer/') or name.startswith('sdm/include/'):
            require_hash(source / name, pin['patched'])
    importer = read_json(ROOT / 'configs/sm8350-display-reference.json')['importer_patch']
    for name, pin in importer['files'].items():
        require_hash(source / name, pin['original'])
    patch_command = ['patch', '--batch', '--fuzz=0', '-p1', '-i', str(ROOT / importer['path'])]
    subprocess.run(patch_command, cwd=source, check=True)
    for name, pin in importer['files'].items():
        require_hash(source / name, pin['patched'])
    session_init = read_json(ROOT / 'configs/sm8350-display-build.json')['hwc_session_init']
    require_hash(ROOT / session_init['patch'], session_init['patch_sha256'])
    for name, pin in session_init['files'].items():
        require_hash(source / name, pin['original'])
    session_patch_command = ['patch', '--batch', '--fuzz=0', '-p1', '-i', str(ROOT / session_init['patch'])]
    subprocess.run(session_patch_command, cwd=source, check=True)
    for name, pin in session_init['files'].items():
        require_hash(source / name, pin['patched'])
    primary_cleanup = read_json(ROOT / 'configs/sm8350-display-reference.json')['primary_cleanup_patch']
    require_hash(ROOT / primary_cleanup['patch'], primary_cleanup['sha256'])
    for item in primary_cleanup['files']:
        require_hash(source / item['path'], item['base_sha256'])
    primary_patch_command = ['patch', '--batch', '--fuzz=0', '-p1', '-i', str(ROOT / primary_cleanup['patch'])]
    subprocess.run(primary_patch_command, cwd=source, check=True)
    for item in primary_cleanup['files']:
        require_hash(source / item['path'], item['result_sha256'])
    allocator_phases = read_json(ROOT / 'configs/sm8350-display-reference.json')['allocator_phase_patch']
    require_hash(ROOT / allocator_phases['patch'], allocator_phases['sha256'])
    for item in allocator_phases['files']:
        require_hash(source / item['path'], item['base_sha256'])
    allocator_phase_command = ['patch', '--batch', '--fuzz=0', '-p1', '-i', str(ROOT / allocator_phases['patch'])]
    subprocess.run(allocator_phase_command, cwd=source, check=True)
    for item in allocator_phases['files']:
        require_hash(source / item['path'], item['result_sha256'])
    system_binder = read_json(ROOT / 'configs/sm8350-display-reference.json')['composer_system_binder_patch']
    require_hash(ROOT / system_binder['patch'], system_binder['sha256'])
    for item in system_binder['files']:
        require_hash(source / item['path'], item['base_sha256'])
    binder_patch_command = ['patch', '--batch', '--fuzz=0', '-p1', '-i', str(ROOT / system_binder['patch'])]
    subprocess.run(binder_patch_command, cwd=source, check=True)
    for item in system_binder['files']:
        require_hash(source / item['path'], item['result_sha256'])
    client_target = read_json(ROOT / 'configs/sm8350-display-reference.json')['client_target_property_patch']
    require_hash(ROOT / client_target['patch'], client_target['sha256'])
    for item in client_target['files']:
        require_hash(source / item['path'], item['base_sha256'])
    client_target_command = ['patch', '--batch', '--fuzz=0', '-p1', '-i', str(ROOT / client_target['patch'])]
    subprocess.run(client_target_command, cwd=source, check=True)
    for item in client_target['files']:
        require_hash(source / item['path'], item['result_sha256'])
    empty_layer = read_json(ROOT / 'configs/sm8350-display-reference.json')['empty_layer_output_patch']
    require_hash(ROOT / empty_layer['patch'], empty_layer['sha256'])
    for item in empty_layer['files']:
        require_hash(source / item['path'], item['base_sha256'])
    empty_layer_command = ['patch', '--batch', '--fuzz=0', '-p1', '-i', str(ROOT / empty_layer['patch'])]
    subprocess.run(empty_layer_command, cwd=source, check=True)
    for item in empty_layer['files']:
        require_hash(source / item['path'], item['result_sha256'])
    empty_flush = read_json(ROOT / 'configs/sm8350-display-reference.json')['empty_layer_flush_patch']
    require_hash(ROOT / empty_flush['patch'], empty_flush['sha256'])
    for item in empty_flush['files']:
        require_hash(source / item['path'], item['base_sha256'])
    empty_flush_command = ['patch', '--batch', '--fuzz=0', '-p1', '-i', str(ROOT / empty_flush['patch'])]
    subprocess.run(empty_flush_command, cwd=source, check=True)
    for item in empty_flush['files']:
        require_hash(source / item['path'], item['result_sha256'])
    color_matrix = read_json(ROOT / 'configs/sm8350-display-reference.json')['composer_color_matrix_patch']
    require_hash(ROOT / color_matrix['patch'], color_matrix['sha256'])
    for item in color_matrix['files']:
        require_hash(source / item['path'], item['base_sha256'])
    color_matrix_command = ['patch', '--batch', '--fuzz=0', '-p1', '-i', str(ROOT / color_matrix['patch'])]
    subprocess.run(color_matrix_command, cwd=source, check=True)
    for item in color_matrix['files']:
        require_hash(source / item['path'], item['result_sha256'])
    command_errors = read_json(ROOT / 'configs/sm8350-display-reference.json')['composer_command_errors_patch']
    require_hash(ROOT / command_errors['patch'], command_errors['sha256'])
    for item in command_errors['files']:
        require_hash(source / item['path'], item['base_sha256'])
    command_errors_command = ['patch', '--batch', '--fuzz=0', '-p1', '-i',
                              str(ROOT / command_errors['patch'])]
    subprocess.run(command_errors_command, cwd=source, check=True)
    for item in command_errors['files']:
        require_hash(source / item['path'], item['result_sha256'])
    acquire_fence = read_json(ROOT / 'configs/sm8350-display-reference.json')['composer_acquire_fence_patch']
    require_hash(ROOT / acquire_fence['patch'], acquire_fence['sha256'])
    for item in acquire_fence['files']:
        require_hash(source / item['path'], item['base_sha256'])
    acquire_fence_command = ['patch', '--batch', '--fuzz=0', '-p1', '-i',
                             str(ROOT / acquire_fence['patch'])]
    subprocess.run(acquire_fence_command, cwd=source, check=True)
    for item in acquire_fence['files']:
        require_hash(source / item['path'], item['result_sha256'])
    first = next(command for command in display['commands'] if '-c' in command)
    flags = [value.replace(str(args.display_build / 'source'), str(source))
             for value in first[:first.index('-MD')]]
    flags[flags.index('-std=c++17')] = '-std=c++20'
    flags += ['-fno-rtti', '-DLOG_TAG="SDM"', '-Wno-inconsistent-missing-override',
              '-Wno-deprecated-declarations', '-D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION',
              '-Wno-non-c-typedef-for-linkage', '-Wno-sign-compare', '-Wno-unused-but-set-parameter',
              '-Wno-unused-but-set-variable', '-DDO_NOT_CHECK_MANUAL_BINDER_INTERFACES',
              # shortcut: Android 12 FMQ requires a constant; this build targets the 4 KiB kernel.
              '-DPAGE_SIZE=4096']
    for path in (deps / 'generated', values['core_headers'].parent / 'generated',
                 deps / 'hardware-include', deps / 'hardware-legacy-include', deps / 'binder-include',
                 *(deps / 'composer-hal' / version / 'utils' for version in ('2.1', '2.2', '2.3', '2.4')),
                 values['mapper_headers'], values['common_headers'], values['binder_headers'],
                 refs / 'android_vendor_qcom_opensource_display-commonsys-intf/services',
                 *(source / name for name in ('composer', 'libqservice', 'libqdutils', 'gpu_tonemapper',
                                              'libhistogram', 'libmemutils', 'gralloc'))):
        flags += ['-I', str(path)]
    toolchains = values['binder_headers'].parents[4]
    for path in (values['core_headers'], toolchains / 'sq2-c2-core-reference/libsync/include',
                 refs / 'android_vendor_qcom_opensource_display-commonsys-intf/gralloc',
                 refs / 'android_vendor_qcom_opensource_display-commonsys-intf/libqdmetadata',
                 *(deps / ('libs-' + name + '-include') for name in
                   ('nativewindow', 'ui', 'nativebase', 'math', 'arect'))):
        flags += ['-isystem', str(path)]
    modules = dict(MODULES, composer=[str(path.relative_to(source)) for path in
                                    sorted((source / 'composer').glob('*.cpp'))])
    commands, dependencies = [], set()
    objects = {name: [] for name in modules}
    jobs = []
    for module, names in modules.items():
        for name in names:
            obj = args.output / (module + '-' + Path(name).stem + '.o')
            command = flags + ['-MD', '-MF', str(obj.with_suffix('.d')), '-c', str(source / name),
                               '-o', str(obj)]
            objects[module].append(str(obj))
            jobs.append((obj, command))
            commands.append(command)
    (args.output / 'compile-commands.json').write_text(json.dumps(commands, indent=2) + '\n')

    def compile_one(job):
        obj, command = job
        result = subprocess.run(command, text=True, capture_output=True)
        obj.with_suffix('.log').write_text(result.stdout + result.stderr)
        return obj, result.returncode

    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        results = list(pool.map(compile_one, jobs))
    failures = [obj.name for obj, code in results if code]
    if failures:
        raise RuntimeError('Compilation failed, see logs: ' + ', '.join(failures))
    for obj, _ in results:
        dependencies.update(shlex.split(obj.with_suffix('.d').read_text().split(':', 1)[1].replace('\\\n', ' ')))
    link = [flags[0], '--target=aarch64-linux-android35', '-nostdlib++', '-Wl,--no-undefined',
            '-Wl,--allow-shlib-undefined', '-Wl,-t']
    for path in (args.output, args.display_build, args.extra_platform, values['platform'],
                 values['source_libraries'].parent / 'allocator/platform', donor):
        link += ['-L', str(path)]
    outputs = []
    for module in MODULES:
        binary = args.output / ('lib' + module + '.so')
        commands.append(link + ['-shared', '-Wl,-soname,' + binary.name] + objects[module] +
                        ['-l' + name for name in LINK_LIBS[module]] + ['-o', str(binary)])
        outputs.append(binary)
    service = args.output / 'vendor.qti.hardware.display.composer-service'
    libraries = ['binder', 'hardware', 'histogram', 'utils', 'cutils', 'sync', 'c++', 'log', 'hidlbase',
                 'fmq', 'hardware_legacy', 'sdmcore', 'qservice', 'qdutils', 'qdMetaData', 'displaydebug',
                 'sdmutils', 'ui', 'grallocutils', 'gpu_tonemapper', 'EGL', 'GLESv2', 'GLESv3',
                 'displayconfig.qti', 'drm', 'android', 'dl']
    interfaces = ['vendor.qti.hardware.display.composer@3.0.so'] + [
        'android.hardware.graphics.composer@2.' + v + '.so' for v in ('1', '2', '3', '4')]
    commands.append(link + objects['composer'] + ['-l' + name for name in libraries] +
                    ['-l:' + name for name in interfaces] + ['-o', str(service)])
    outputs.append(service)
    for command, binary in zip(commands[-len(outputs):], outputs):
        result = subprocess.run(command, text=True, capture_output=True)
        (args.output / (binary.name + '.link.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('Link failed; see log: ' + binary.name)
        for name in result.stdout.splitlines():
            if Path(name).is_file():
                dependencies.add(name)
        elf = binary.read_bytes()
        if elf[:6] != b'\x7fELF\x02\x01' or int.from_bytes(elf[18:20], 'little') != 183:
            raise ValueError('Not ARM64 ELF: ' + binary.name)
    dependencies.update(map(str, (Path(__file__), manifest_path, args.allocator_command,
                                  ROOT / importer['path'], ROOT / session_init['patch'], ROOT / primary_cleanup['patch'],
                                  ROOT / allocator_phases['patch'], ROOT / system_binder['patch'],
                                  ROOT / client_target['patch'], ROOT / empty_layer['patch'], ROOT / empty_flush['patch'],
                                  ROOT / color_matrix['patch'], ROOT / command_errors['patch'], ROOT / acquire_fence['patch'],
                                  ROOT / 'configs/sm8350-display-build.json',
                                  ROOT / 'configs/sm8350-display-reference.json',
                                  ROOT / 'probe/graphics/quest2-platform-cxx.h')))
    dependencies.update(str(path) for path in deps.glob('*.json'))
    inputs = {str(Path(path).resolve()): sha(Path(path)) for path in sorted(dependencies)}
    build = {'status': 'complete-source-composer-service-linked', 'deployed': False,
             'translation_units': len(results), 'source_display_build': str(args.display_build),
             'commands': commands, 'patch_command': patch_command,
             'session_patch_command': session_patch_command, 'hwc_session_init': True,
             'primary_patch_command': primary_patch_command, 'hwc_primary_cleanup': True,
             'allocator_phase_command': allocator_phase_command, 'allocator_phases': True,
             'binder_patch_command': binder_patch_command, 'binder_driver': '/dev/binder',
             'client_target_command': client_target_command, 'client_target_property_initialized': True,
             'dpu_histogram_capability': display.get('dpu_histogram_capability', False),
             'empty_layer_command': empty_layer_command, 'empty_layer_output_initialized': True,
             'empty_flush_command': empty_flush_command, 'empty_video_flush': True,
             'dpu_flush_fence': display.get('dpu_flush_fence', False),
             'color_matrix_command': color_matrix_command, 'dataspace_matrix_initialized': True,
             'command_errors_command': command_errors_command, 'command_errors_propagated': True,
             'acquire_fence_command': acquire_fence_command, 'acquire_fence_validation': True,
             'qservice_binder_driver': '/dev/binder', 'hidl_transport': 'hwBinder', 'inputs': inputs,
             'sha256': {path.name: sha(path) for path in outputs},
             'limits': ['Runtime relocation closure and service behavior require device validation',
                        'Legacy vndbinder QService clients and enforcing SELinux deployment are not validated',
                        'Android 12 FMQ and uevent source compiled for the matching 4 KiB kernel',
                        'Private handle consumers still require donor layout validation',
                        'No scanout, secure, HDR or performance claim from successful linking']}
    (args.output / 'BUILD.json').write_text(json.dumps(build, indent=2) + '\n')
    print(json.dumps({key: build[key] for key in ('status', 'translation_units', 'sha256')}, indent=2))


if __name__ == '__main__':
    main()
