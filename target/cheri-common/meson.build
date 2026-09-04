# cheri-tcg-op-ldst.c implements the "_with_checked_addr" TCG load/store/
# atomic entry points that tcg/tcg-op.h redirects the standard
# tcg_gen_qemu_ld/st/atomic/cmpxchg calls to, for every target (CHERI and
# non-CHERI alike) -- so unlike the rest of this directory, it is not gated
# on TARGET_CHERI: it must be compiled per target for every target, since
# non-CHERI target code reaches it through that redirect too. See the
# comment at the top of the file for why it cannot live in the shared
# tcg_ss library alongside the rest of tcg/tcg-op-ldst.c.
specific_ss.add(files(
  'cheri-tcg-op-ldst.c',
))

specific_ss.add(when: 'TARGET_CHERI', if_true: files(
  'cheri_gdbstub.c',
  'cheri_tagmem.c',
  'op_helper_cheri_common.c',
))
