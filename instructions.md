# Instructions
We are in the cheri qemu repository. Currently in the process of merging
upstream commits to bring the repository inline with qemu 8.0
During this process we have hit an issue. The latest commit causes a bunch of
tests to fail. This appears to be related to the lifecycle of TCG variables.

Diagnose the issue and propose a fix which maintains the intent of the commit
(i.e. fix the CHERI issue, do not drop the commit).

Do not make any changes outside the repository and do not change git commits.

build cab be done simply running 'ninja' inside the build folder.
Test which demonstrates the problem van be run with './build/qemu-system-mips64cheri128 -cpu BERI -kernel ~/cheri/cheritest/obj/128/test_cp2_cloadtags.elf 
-serial none -monitor none -nographic -m 2048M  -M malta -d op,op_opt,cpu -D tcg_debug.log
'
It will dump the log of tcg operations into tcg_debug.log.


