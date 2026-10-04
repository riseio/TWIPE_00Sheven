import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile

def main():
    parser = argparse.ArgumentParser()
    for name in ('root', 'output', 'compiler', 'cpu-generator', 'rsp-generator'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--runtime-library', action='append', default=[])
    args = parser.parse_args()
    root, output = Path(args.root), Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    cpu = root / 'build/generated/RecompiledFuncs'
    bodies = '\n'.join(p.read_text() for p in sorted(cpu.glob('funcs_*.c')))
    game_names = sorted(set(re.findall(r'RECOMP_FUNC void (\w+)\(', bodies)))
    called_names = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', bodies))
    if not game_names:
        raise RuntimeError('No game function metadata')
    function_header = (cpu / 'funcs.h').read_text()
    declarations = set(re.findall(r'void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\);', function_header))
    if not set(game_names) <= declarations:
        raise RuntimeError('Generated declaration/definition mismatch')
    header_text = (root / 'include/twine_recomp.h').read_text()
    aliases = dict(re.findall(r'^#define (\w+) (\w+)$', header_text, re.M))
    host_names = {aliases.get(name, name) for name in declarations - set(game_names)}
    prototypes = {name: ('void', ['uint8_t*', 'recomp_context*']) for name in host_names}

    for result, name, parameters in re.findall(
            r'\b(void|uint32_t|int|float)\s+(\w+)\s*\(([^;{}]*)\)\s*;', header_text):
        if name not in called_names:
            continue
        types = []
        for parameter in parameters.split(','):
            parameter = ' '.join(parameter.split())
            if parameter in ('', 'void'):
                continue
            match = re.fullmatch(r'((?:const\s+)?\w+\s*\*?)\s+\w+', parameter)
            types.append(match[1].strip() if match else parameter)
        prototypes[name] = (result, types)
    core_imports = {
        'cop0_status_write': ('void', ['recomp_context*', 'gpr']),
        'cop0_status_read': ('gpr', ['recomp_context*']),
        'switch_error': ('void', ['const char*', 'uint32_t', 'uint32_t']),
        'do_break': ('void', ['uint32_t']),
        'get_function': ('recomp_func_t*', ['int32_t']),
        'recomp_syscall_handler': ('void', ['uint8_t*', 'recomp_context*', 'int32_t']),
        'pause_self': ('void', ['uint8_t*']),
    }
    prototypes.update({name: prototype for name, prototype in core_imports.items()
                       if name != 'recomp_syscall_handler' or
                       name in called_names})
    imports = sorted(prototypes)
    missing_hooks = {name for name in called_names if (name.startswith('twine_') or name.endswith('_recomp'))
                     and name not in prototypes and name not in game_names}
    if missing_hooks:
        raise RuntimeError('Missing typed runtime hook declarations: ' + ', '.join(sorted(missing_hooks)))
    kit = output / 'kit'
    if kit.exists():
        if kit.is_symlink() or kit.resolve().parent != output.resolve():
            raise RuntimeError('Unsafe compilation kit staging directory')
        shutil.rmtree(kit)
    kit.mkdir(exist_ok=True)
    include = kit / 'include'
    include.mkdir(exist_ok=True)
    copies = {
        'recomp.h': 'lib/N64ModernRuntime/N64Recomp/include/recomp.h',
        'twine_recomp.h': 'include/twine_recomp.h',
        'local_aot_abi.h': 'include/local_aot_abi.h',
        'librecomp/sections.h': 'lib/N64ModernRuntime/librecomp/include/librecomp/sections.h',
        'librecomp/rsp.hpp': 'lib/N64ModernRuntime/librecomp/include/librecomp/rsp.hpp',
        'librecomp/rsp_vu.hpp': 'lib/N64ModernRuntime/librecomp/include/librecomp/rsp_vu.hpp',
        'librecomp/rsp_vu_impl.hpp': 'lib/N64ModernRuntime/librecomp/include/librecomp/rsp_vu_impl.hpp',
        'ultramodern/ultra64.h': 'lib/N64ModernRuntime/ultramodern/include/ultramodern/ultra64.h',
    }
    for destination, source in copies.items():
        path = include / destination
        path.parent.mkdir(parents=True, exist_ok=True)
        text = (root / source).read_text()
        if destination == 'recomp.h':
            before = 'extern int32_t* section_addresses;'
            if text.count(before) != 1:
                raise RuntimeError('Overlay address ABI changed')
            text = text.replace(before, 'extern int32_t** twine_section_addresses;\n#define section_addresses (*twine_section_addresses)')
        if destination == 'librecomp/rsp.hpp':
            for before, after in (
                ('extern uint8_t dmem[];', 'extern uint8_t* dmem;'),
                ('extern uint16_t rspReciprocals[512];', 'extern uint16_t* rspReciprocals;'),
                ('extern uint16_t rspInverseSquareRoots[512];', 'extern uint16_t* rspInverseSquareRoots;'),
            ):
                if text.count(before) != 1:
                    raise RuntimeError('RSP header ABI changed')
                text = text.replace(before, after)
        path.write_text(text, encoding='utf-8', newline='\n')
    for name, source in (
        ('cpu.toml', 'config/twine.us.rev0.toml'),
        ('symbols.toml', 'config/twine.us.rev0.syms.toml'),
        ('rsp.toml', 'config/twine.audio.us.rev0.toml'),
    ):
        text = (root / source).read_text()
        replacements = {'rom_file_path': 'input.z64', 'symbols_file_path': 'symbols.toml',
                        'output_func_path': 'cpu', 'output_file_path': 'rsp.cpp'}
        for key, value in replacements.items():
            text = re.sub(r'^' + key + r' = "[^"]*"', key + ' = "' + value + '"', text, flags=re.M)
        (kit / name).write_text(text, encoding='utf-8', newline='\n')

    host_header = output / 'funcs.h'
    if not host_header.exists() or host_header.read_text() != function_header:
        host_header.write_text(function_header, encoding='utf-8', newline='\n')
    host = ['#include "twine_recomp.h"', '#include "funcs.h"',
            '#include "local_aot_abi.h"', '#include "local_aot_metadata.hpp"',
            '#include "librecomp/rsp.hpp"', '#include <stdexcept>', '#include <cstdlib>',
            'namespace twine::local_aot {',
            'TwineAotModule loaded{};',
            'static void (*const callbacks[])(void) = {']
    host += ['reinterpret_cast<void(*)(void)>(&' + name + '),' for name in imports]
    host += ['};', 'TwineAotHost host_api() { return {1, sizeof(recomp_context),',
             'offsetof(recomp_context, status_reg), sizeof(SectionTableEntry), kAbiIdentity,',
             'callbacks, sizeof(callbacks)/sizeof(callbacks[0]), dmem, rspReciprocals, rspInverseSquareRoots, &section_addresses}; }',
             '}', 'extern "C" {']
    for index, name in enumerate(game_names):
        host += [f'void {name}(uint8_t* rdram, recomp_context* ctx) {{',
                 f'if (!twine::local_aot::loaded.functions) std::abort();',
                 f'twine::local_aot::loaded.functions[{index}](rdram, ctx);', '}']
    host += ['}', 'RspExitReason twine_audio(uint8_t* rdram, uint32_t address) {',
             'if (!twine::local_aot::loaded.audio) std::abort();',
             'return static_cast<RspExitReason>(twine::local_aot::loaded.audio(rdram, address));', '}']
    (output / 'host_bindings.cpp').write_text('\n'.join(host) + '\n', encoding='utf-8', newline='\n')
    module_imports = ['#include "twine_recomp.h"', '#include "funcs.h"',
                      '#include "local_aot_abi.h"', 'const TwineAotHost* twine_aot_host;',
                      'int32_t** twine_section_addresses;']
    for index, name in enumerate(imports):
        result, types = prototypes[name]
        parameters = ', '.join(f'{t} p{i}' for i, t in enumerate(types)) or 'void'
        arguments = ', '.join(f'p{i}' for i in range(len(types)))
        pointer = f'{result}(*)({", ".join(types) or "void"})'
        prefix = '' if result == 'void' else 'return '
        module_imports += [f'{result} {name}({parameters}) {{',
                           f'{prefix}(({pointer})twine_aot_host->functions[{index}])({arguments});', '}']
    (kit / 'imports.c').write_text('\n'.join(module_imports) + '\n', encoding='utf-8', newline='\n')
    identity_inputs = header_text + json.dumps(prototypes, sort_keys=True) + '\n'.join(game_names)
    for path in sorted(include.rglob('*')):
        if path.is_file():
            identity_inputs += path.read_text()
    for name in ('cpu.toml', 'rsp.toml', 'symbols.toml'):
        identity_inputs += (kit / name).read_text()
    identity_inputs += (root / 'include/local_aot_recipe.hpp').read_text()
    identity_inputs += Path(__file__).read_text()
    for tool in (args.cpu_generator, args.rsp_generator):
        identity_inputs += hashlib.sha256(Path(tool).read_bytes()).hexdigest()
    abi_id = hashlib.sha256(identity_inputs.encode()).hexdigest()
    module = ['#include "librecomp/rsp.hpp"', '#include "local_aot_abi.h"',
              '#include "recomp_overlays.inl"', '#include <cstring>',
              'extern "C" { extern const TwineAotHost* twine_aot_host; }',
              'uint8_t* dmem;', 'uint16_t* rspReciprocals;', 'uint16_t* rspInverseSquareRoots;',
              'extern RspUcodeFunc twine_audio;',
              'static int audio(uint8_t* ram, uint32_t address) { return static_cast<int>(twine_audio(ram, address)); }',
              'static recomp_func_t* const functions[] = {']
    module += [name + ',' for name in game_names]
    module += ['};', '#if defined(_WIN32)', '#define EXPORT __declspec(dllexport)', '#else',
               '#define EXPORT __attribute__((visibility("default")))', '#endif',
               'extern "C" EXPORT int twine_local_module_init(const TwineAotHost* host, TwineAotModule* out) {',
               f'if (!host || !out || host->version != 1 || host->function_count != {len(imports)} ||',
               'host->context_size != sizeof(recomp_context) || host->context_status_offset != offsetof(recomp_context, status_reg) ||',
               'host->section_entry_size != sizeof(SectionTableEntry) ||',
               f'!host->identity || std::strcmp(host->identity, "{abi_id}") != 0) return 0;',
               'twine_aot_host = host; dmem = host->rsp_dmem; rspReciprocals = host->rsp_reciprocals;',
               'rspInverseSquareRoots = host->rsp_inverse_roots;',
               'twine_section_addresses = host->section_addresses_owner;',
               '*out = {section_table, ARRLEN(section_table), num_sections, overlay_sections_by_index,',
               'ARRLEN(overlay_sections_by_index), functions, ARRLEN(functions), audio}; return 1;', '}']
    (kit / 'module.cpp').write_text('\n'.join(module) + '\n', encoding='utf-8', newline='\n')
    generators = kit / 'generators'
    generators.mkdir(exist_ok=True)
    for argument, name in ((args.cpu_generator, 'N64Recomp'), (args.rsp_generator, 'RSPRecomp')):
        source = Path(argument)
        shutil.copy2(source, generators / (name + source.suffix))
    for library in args.runtime_library:
        source = Path(library)
        shutil.copy2(source, generators / source.name)

    compiler = Path(args.compiler)
    compiler_archive = compiler.parent / (compiler.name + '-payload.zip')
    if not compiler_archive.exists():
        pending = compiler_archive.with_suffix('.pending')
        with zipfile.ZipFile(pending, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as zipped:
            for path in sorted(p for p in compiler.rglob('*') if p.is_file()):
                info = zipfile.ZipInfo('compiler/' + path.relative_to(compiler).as_posix(), (2020, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (0o100755 if path.stat().st_mode & 0o111 else 0o100644) << 16
                zipped.writestr(info, path.read_bytes())
        pending.replace(compiler_archive)
    for dependency, source in (
        ('N64Recomp', 'lib/N64ModernRuntime/N64Recomp/LICENSE'),
        ('N64ModernRuntime', 'lib/N64ModernRuntime/COPYING'),
        ('rabbitizer', 'lib/N64ModernRuntime/N64Recomp/lib/rabbitizer/LICENSE'),
        ('fmt', 'lib/N64ModernRuntime/N64Recomp/lib/fmt/LICENSE'),
        ('ELFIO', 'lib/N64ModernRuntime/N64Recomp/lib/ELFIO/LICENSE.txt'),
        ('tomlplusplus', 'lib/N64ModernRuntime/N64Recomp/lib/tomlplusplus/LICENSE'),
    ):
        license_file = root / source
        shutil.copy2(license_file, kit / (dependency + '-LICENSE'))
    files = sorted(p for p in kit.rglob('*') if p.is_file())
    for path in files:
        relative = path.relative_to(kit).as_posix()
        if relative.startswith('cpu/') or relative in ('rsp.cpp', 'input.z64'):
            raise RuntimeError('Translated code or ROM entered distribution kit')
    archive = output / 'bundle.zip'
    shutil.copy2(compiler_archive, archive)
    with zipfile.ZipFile(archive, 'a', zipfile.ZIP_DEFLATED, compresslevel=6) as zipped:
        for path in files:
            info = zipfile.ZipInfo(path.relative_to(kit).as_posix(), (2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100755 if path.stat().st_mode & 0o111 else 0o100644) << 16
            zipped.writestr(info, path.read_bytes())
    kit_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
    sections = (cpu / 'recomp_overlays.inl').read_text()
    section_count = len(re.findall(r'\.funcs\s*=', sections))
    total_sections = re.search(r'num_sections\s*=\s*(\d+)', sections)
    overlays = re.search(r'overlay_sections_by_index\[\]\s*=\s*\{(.*?)\}', sections, re.S)
    if not section_count or not total_sections or not overlays:
        raise RuntimeError('Unsupported overlay metadata layout')
    overlay_count = len(re.findall(r'\b\d+\b', overlays[1]))
    (output / 'local_aot_metadata.hpp').write_text(
        '#pragma once\nnamespace twine::local_aot {\n'
        f'inline constexpr char kAbiIdentity[] = "{abi_id}";\n'
        f'inline constexpr char kKitIdentity[] = "{kit_hash}";\n'
        f'inline constexpr size_t kGameFunctionCount = {len(game_names)};\n'
        f'inline constexpr size_t kSectionCount = {section_count};\n'
        f'inline constexpr size_t kTotalSections = {total_sections[1]};\n'
        f'inline constexpr size_t kOverlayCount = {overlay_count};\n'
        '}\n', encoding='utf-8', newline='\n')

if __name__ == '__main__':
    main()
