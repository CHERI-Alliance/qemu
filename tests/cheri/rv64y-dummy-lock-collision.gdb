# Note: This test is fully AI-generated (prompted to generate adversarial
# regression tests exposing MTTCG capability tag memory bugs).
#
# Rule: get_dummy_locks() indexes dummy_tag_table strictly by the page
# offset, write-acquires each distinct dummy lock at most once (even when
# both granules hash to the same one), and releasing both returned locks
# with cheri_tag_free_lock() leaves every lock unlocked.
#
# 0x1108 and 0x2108 have the same page offset (0x108) on different virtual
# pages and must acquire the exact same pair of dummy locks.
# 0x7fffffff8 spans granules 0x7fffffff and 0x80000000; if a page-boundary
# hash maps both to the same dummy lock, get_dummy_locks() must deduplicate
# them rather than self-deadlocking.

set direct-call-timeout 10

break main
run
python
if gdb.lookup_global_symbol("get_dummy_locks") is None:
    print("SKIP: get_dummy_locks not present in this build")
    gdb.execute("quit 77")
end
set $p = (void **)malloc(2 * sizeof(void *))
set $none = (void *)-1

define check_held
  if $arg0 != $none && !(*(unsigned long *)$arg0 & 1)
    printf "FAIL: lock %p not write-locked\n", $arg0
    quit 1
  end
end

define check_free
  if $arg0 != $none && (*(unsigned long *)$arg0 & 1)
    printf "FAIL: lock %p state 0x%lx after release\n", $arg0, \
      *(unsigned long *)$arg0
    quit 1
  end
end

define dummy_pair
  set $p[0] = $none
  set $p[1] = $none
  printf "get_dummy_locks(0x%lx)\n", $arg0
  call get_dummy_locks($arg0, $p, $p + 1)
  set $low = $p[0]
  set $high = $p[1]
  if $low == $none && $high == $none
    printf "FAIL: no lock taken\n"
    quit 1
  end
  check_held $low
  check_held $high
  call cheri_tag_free_lock($low)
  call cheri_tag_free_lock($high)
  check_free $low
  check_free $high
end

dummy_pair 0x1108
if $low == $none || $high == $none || $low == $high
  printf "FAIL: expected two distinct dummy locks\n"
  quit 1
end
set $low_p1 = $low
set $high_p1 = $high
dummy_pair 0x2108
if $low != $low_p1 || $high != $high_p1
  printf "FAIL: virtual aliases 0x1108 and 0x2108 returned different locks\n"
  quit 1
end
dummy_pair 0x7fffffff8
printf "PASS: get_dummy_locks() handled all pairs\n"

delete
continue
quit $_exitcode
