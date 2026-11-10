# Bare-metal CHERI regression tests for qemu-system-riscv64y and
# qemu-system-morello. Each test is a freestanding C file linked with a tiny
# per-architecture startup stub using stock clang and ld.lld. The suite is
# skipped for an arch if no clang that can link for it is found or the
# matching emulator is not built.
#
# Note: This test suite and all tests in tests/cheri/ are AI-generated,
# prompted to generate adversarial regression tests that expose MTTCG
# capability tag memory bugs.

# Common variant sets for 'variants' below (test name suffix: QEMU args).
cheri_tcg_variants = {
  '-stcg': ['-accel', 'tcg,thread=single'],
  '-mttcg': ['-smp', '2', '-accel', 'tcg,thread=multi'],
}

cheri_smp4_variants = {
  '-stcg': ['-smp', '4', '-accel', 'tcg,thread=single'],
  '-mttcg': ['-smp', '4', '-accel', 'tcg,thread=multi'],
}

# Keys: 'name' (source is <name>.c), 'arch' ('rv64y' or 'morello'),
# optional 'qemu_args' (list), 'variants' (dict of name suffix to extra QEMU
# args, default a single unsuffixed run), 'gdb' (a gdb script that runs
# QEMU and quits with the verdict, skipped unless QEMU was built with -O0)
# and 'log' (regexes that must match the -d instr log, skipped unless QEMU
# was built with --enable-tcg-log-instr).
cheri_tests = [
  {'name': 'rv64y-tag-sanity', 'arch': 'rv64y',
   'variants': cheri_tcg_variants},
  {'name': 'morello-tag-sanity', 'arch': 'morello',
   'variants': cheri_tcg_variants},
  {'name': 'rv64y-dummy-lock-collision', 'arch': 'rv64y',
   'gdb': 'rv64y-dummy-lock-collision.gdb'},
  # MTTCG allocates tag blocks on first access, so pin single-threaded TCG.
  {'name': 'rv64y-tag-read-log', 'arch': 'rv64y',
   'qemu_args': ['-accel', 'tcg,thread=single'],
   'log': ['Cap Tag Read \\[0000000080400000/[0-9a-f]+\\] -> 0$']},
  {'name': 'rv64y-dma-tag-order', 'arch': 'rv64y',
   'gdb': 'rv64y-dma-tag-order.gdb',
   'qemu_args': ['-smp', '2', '-accel', 'tcg,thread=multi',
                 '-name', 'dma,debug-threads=on',
                 '-uuid', '11223344-5566-7788-0000-000000000000']},
  {'name': 'morello-swap-untagged-probe', 'arch': 'morello',
   'variants': cheri_tcg_variants},
  {'name': 'rv64y-seqlock-tearing', 'arch': 'rv64y',
   'variants': cheri_smp4_variants,
   'gdb_variants': {
     '-gdb': {'gdb': 'rv64y-seqlock-tearing.gdb',
              'args': ['-smp', '4', '-accel', 'tcg,thread=multi',
                       '-name', 'tearing,debug-threads=on']},
   }},
  {'name': 'rv64y-lrsc-untagged-amo', 'arch': 'rv64y',
   'qemu_args': ['-object', 'memory-backend-ram,id=shmmem,size=1M',
                 '-device', 'ivshmem-plain,memdev=shmmem,addr=0x01.0'],
   'variants': cheri_smp4_variants,
   'gdb_variants': {
     '-gdb': {'gdb': 'rv64y-lrsc-aba.gdb',
              'args': ['-smp', '4', '-accel', 'tcg,thread=multi',
                       '-name', 'lrsc,debug-threads=on']},
   }},
  {'name': 'morello-ldxr-stxr-page', 'arch': 'morello',
   'qemu_args': ['-smp', '2'],
   'variants': cheri_tcg_variants,
   'gdb_variants': {
     '-gdb': {'gdb': 'morello-ldxr-stxr-aba.gdb',
              'args': ['-accel', 'tcg,thread=multi',
                       '-name', 'ldxr,debug-threads=on']},
   }},
  {'name': 'morello-cross-page-alias', 'arch': 'morello',
   'variants': cheri_tcg_variants,
   'gdb_variants': {
     '-gdb': {'gdb': 'morello-cross-page-alias.gdb',
              'args': ['-smp', '2', '-accel', 'tcg,thread=multi',
                       '-name', 'alias,debug-threads=on']},
   }},
  {'name': 'rv64y-tagblk-alloc-size', 'arch': 'rv64y',
   'variants': cheri_tcg_variants},
  {'name': 'morello-pair-store-tag', 'arch': 'morello',
   'variants': cheri_tcg_variants},
  {'name': 'morello-ddc-wrap', 'arch': 'morello'},
]

cheri_common_cflags = ['-O2', '-ffreestanding', '-fno-builtin',
                       '-Wall', '-Wextra', '-Werror']

cheri_arches = {
  'rv64y': {
    'target': 'riscv64y-softmmu',
    'qemu': 'qemu-system-riscv64y',
    'cflags': ['--target=riscv64', '-march=rv64g',
               '-mcmodel=medany'] + cheri_common_cflags,
    'qemu_args': ['-M', 'virt', '-bios', 'none',
                  '-semihosting-config', 'enable=on'],
  },
  'morello': {
    'target': 'morello-softmmu',
    'qemu': 'qemu-system-morello',
    'cflags': ['--target=aarch64-none-elf'] + cheri_common_cflags,
    'qemu_args': ['-M', 'virt', '-cpu', 'morello',
                  '-semihosting-config', 'enable=on'],
  },
}

cheri_cc_path = get_option('rvy_test_cc')
if cheri_cc_path != ''
  cheri_cc_candidates = [cheri_cc_path]
else
  cheri_cc_candidates = []
  if host_machine.system() == 'darwin'
    brew = find_program('brew', required: false)
    if brew.found()
      brew_llvm = run_command(brew, '--prefix', 'llvm', check: false)
      if brew_llvm.returncode() == 0
        cheri_cc_candidates += [brew_llvm.stdout().strip() / 'bin' / 'clang']
      endif
    endif
  endif
  cheri_cc_candidates += ['clang']
endif
cheri_cc = find_program(cheri_cc_candidates, required: false)
cheri_src_dir = meson.current_source_dir()
cheri_depend_files = files('include/common.h', 'include/rvy.h',
                           'include/morello.h')
cheri_runner = files('run-test.py')
cheri_debug_build = get_option('optimization') == '0' and get_option('debug')
cheri_log_instr = config_host.has_key('CONFIG_TCG_LOG_INSTR')

cheri_enabled = {}
foreach arch, info : cheri_arches
  if cheri_cc.found() and info['target'] in target_dirs
    ldflags = ['-nostdlib', '-static', '-fuse-ld=lld',
               '-Wl,-T,' + (cheri_src_dir / (arch + '.ld'))]
    probe = run_command(cheri_cc, info['cflags'], ldflags,
                        cheri_src_dir / 'probe.S',
                        '-o', meson.current_build_dir() / ('probe-' + arch),
                        check: false)
    if probe.returncode() == 0
      cheri_enabled += {arch: ldflags}
    endif
  endif
endforeach

foreach t : cheri_tests
  arch = t['arch']
  if arch not in cheri_enabled
    continue
  endif
  info = cheri_arches[arch]
  elf = custom_target(t['name'] + '.elf',
    input: ['start-' + arch + '.S', t['name'] + '.c'],
    output: t['name'] + '.elf',
    command: [cheri_cc, info['cflags'], '-I' + (cheri_src_dir / 'include'),
              cheri_enabled[arch], '@INPUT@', '-o', '@OUTPUT@'],
    depend_files: cheri_depend_files + files(arch + '.ld'),
    build_by_default: false)
  emulator = emulators[info['qemu']]
  runner_args = [cheri_runner]
  if 'gdb' in t
    runner_args += ['--gdb', cheri_src_dir / t['gdb']]
    if cheri_debug_build
      runner_args += ['--debug-build']
    endif
  endif
  foreach regex : t.get('log', [])
    runner_args += ['--log', regex]
  endforeach
  if 'log' in t and cheri_log_instr
    runner_args += ['--log-instr']
  endif
  common_qemu_args = ([emulator.full_path()] + info['qemu_args'] +
                      ['-m', '128M', '-display', 'none', '-serial', 'stdio',
                       '-monitor', 'none'] + t.get('qemu_args', []))
  foreach suffix, variant_args : t.get('variants', {'': []})
    test(t['name'] + suffix, python,
         args: runner_args + common_qemu_args + variant_args + ['-kernel', elf],
         depends: [emulator, elf],
         suite: ['cheri'],
         timeout: 60)
  endforeach
  foreach suffix, gdb_info : t.get('gdb_variants', {})
    gdb_runner_args = [cheri_runner, '--gdb', cheri_src_dir / gdb_info['gdb']]
    if cheri_debug_build
      gdb_runner_args += ['--debug-build']
    endif
    test(t['name'] + suffix, python,
         args: (gdb_runner_args + common_qemu_args + gdb_info['args'] +
                ['-kernel', elf]),
         depends: [emulator, elf],
         suite: ['cheri'],
         timeout: 60)
  endforeach
endforeach
