gen = [
  decodetree.process('rel6.decode', extra_args: ['--decode=decode_isa_rel6']),
  decodetree.process('msa.decode', extra_args: '--decode=decode_ase_msa'),
  decodetree.process('tx79.decode', extra_args: '--static-decode=decode_tx79'),
]

mips_tcg_ss = ss.source_set()
mips_tcg_ss.add(gen)
mips_tcg_ss.add(files(
  'dsp_helper.c',
  'exception.c',
  'fpu_helper.c',
  'ldst_helper.c',
  'lmmi_helper.c',
  'msa_helper.c',
  'msa_translate.c',
  'op_helper.c',
  'op_helper_beri.c',
  'rel6_translate.c',
  'translate.c',
  'translate_addr_const.c',
  'txx9_translate.c',
  'vr54xx_helper.c',
))
mips_tcg_ss.add(when: 'TARGET_MIPS64', if_true: files(
  'tx79_translate.c',
), if_false: files(
  'mxu_translate.c',
))

mips_ss.add(when: 'TARGET_CHERI', if_true: files('op_helper_cheri.c'))
mips_ss.add(when: ['CONFIG_TCG', 'CONFIG_TCG_LOG_INSTR'], if_true: files('op_helper_log_instr.c'))

mips_ss.add_all(when: 'CONFIG_TCG', if_true: [mips_tcg_ss])

if have_user
  subdir('user')
endif
if have_system
  subdir('sysemu')
endif
