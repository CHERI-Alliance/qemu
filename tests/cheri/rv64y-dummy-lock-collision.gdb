# Note: This test is fully AI-generated (prompted to generate adversarial
# regression tests exposing MTTCG capability tag memory bugs).
#
# Rule: get_dummy_locks() indexes dummy_tag_table strictly by the page
# offset, write-acquires each distinct dummy lock at most once (even when
# both granules hash to the same one), and releasing both returned locks
# leaves every lock unlocked.

set direct-call-timeout 10

break main
run

python
import cheri_gdb
import gdb

get_sym = cheri_gdb.require_symbol("get_dummy_locks")
free_sym = cheri_gdb.require_symbol("cheri_tag_free_lock",
                                    "cheri_tag_writer_release")

TAG_LOCK_NONE = (1 << 64) - 1
pair_buf = int(gdb.parse_and_eval("(uintptr_t)malloc(2 * sizeof(void *))"))


def read_ptr(addr):
    val = int(gdb.parse_and_eval(f"*(uintptr_t *)0x{addr:x}"))
    return val & ((1 << 64) - 1)


def write_ptr(addr, val):
    gdb.execute(f"set var *(uintptr_t *)0x{addr:x} = 0x{val:x}ULL")


def is_locked(lock_addr):
    try:
        expr = f"((CheriTagBlock *)0x{lock_addr:x})->seqlock"
        seq = int(gdb.parse_and_eval(expr))
    except gdb.error as exc:
        cheri_gdb.fail(f"CheriTagBlock.seqlock unavailable ({exc})")
    return (seq & 1) != 0


def check_dummy_pair(vaddr):
    write_ptr(pair_buf, TAG_LOCK_NONE)
    write_ptr(pair_buf + 8, TAG_LOCK_NONE)
    gdb.execute(f"call (void){get_sym}(0x{vaddr:x}ULL, "
                f"(void **)0x{pair_buf:x}, (void **)0x{pair_buf + 8:x})")
    low = read_ptr(pair_buf)
    high = read_ptr(pair_buf + 8)
    if (low in (0, TAG_LOCK_NONE)) and (high in (0, TAG_LOCK_NONE)):
        print(f"FAIL: get_dummy_locks(0x{vaddr:x}) took no lock")
        gdb.execute("quit 1")
    for ptr in (low, high):
        if ptr not in (0, TAG_LOCK_NONE) and not is_locked(ptr):
            print(f"FAIL: lock 0x{ptr:x} not write-locked")
            gdb.execute("quit 1")
    gdb.execute(f"call (void){free_sym}((void *)0x{low:x})")
    gdb.execute(f"call (void){free_sym}((void *)0x{high:x})")
    for ptr in (low, high):
        if ptr not in (0, TAG_LOCK_NONE) and is_locked(ptr):
            print(f"FAIL: lock 0x{ptr:x} still locked after release")
            gdb.execute("quit 1")
    return low, high


low1, high1 = check_dummy_pair(0x1108)
if low1 in (0, TAG_LOCK_NONE) or high1 in (0, TAG_LOCK_NONE) or low1 == high1:
    print("FAIL: expected two distinct dummy locks for 0x1108")
    gdb.execute("quit 1")

low2, high2 = check_dummy_pair(0x2108)
if (low1, high1) != (low2, high2):
    print("FAIL: virtual aliases 0x1108 and 0x2108 returned different locks")
    gdb.execute("quit 1")

check_dummy_pair(0x7fffffff8)
print("PASS: get_dummy_locks() handled all pairs")
gdb.execute("delete")
end

continue
quit $_exitcode
