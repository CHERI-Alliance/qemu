# Version check example test
#
# Copyright (c) 2024 Codasip s.r.o
#
# Author:
#  Paul Buxton <paul.buxton@codasip.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or
# later.  See the COPYING file in the top-level directory.


from avocado_qemu import Test
from avocado_qemu import BUILD_DIR
import os
bindir='scripts/riscv_cheri/elfhome/rv64_cheri/'

class Cheri_test(Test):
    """
    :avocado: tags=arch:riscv64cheri
    """

    def add_common_args(self):
        self.vm.add_args("-semihosting","-machine","virt","-nographic")               
 
    def common_test(self, elffile,timeout = 30,exitcode=0):
        qemu_img = os.path.join(BUILD_DIR, '../'+bindir+elffile)
        self.vm.add_args('-bios',qemu_img)
        self.add_common_args()
        self.vm.launch()
        self.vm.wait(timeout=timeout)
        self.assertEqual(self.vm.exitcode(), exitcode)
        
 
    def test_semihosting_fail(self):
        self.common_test('semihost_fail.elf',exitcode=1)

    def test_acperm(self):
        self.common_test('acperm.elf')

    def test_gcbase(self):
        self.common_test('gcbase.elf')

    def test_gcperm(self):
        self.common_test('gcperm.elf')

    def test_gctag(self):
        self.common_test('gctag.elf')

    def test_cram(self):
        self.common_test('cram.elf')

    def test_cadd(self):
        self.common_test('cadd.elf')

    def test_dataExceptions(self):
        self.common_test('dataExceptions.elf')

    def test_instructionExceptions(self):
        self.common_test('instructionExceptions.elf')

    def test_jumpExceptions(self):
        self.common_test('jumpExceptions.elf')

    def test_scaddr(self):
        self.common_test('scaddr.elf')

    def test_scbnds(self):
        self.common_test('scbnds.elf')

    def test_atexit(self):
        self.common_test('newlib/atexit.x')

    def test_hsearchtest(self):
        self.common_test('newlib/hsearchtest.x')

    def test_iconvjp(self):
        self.common_test('newlib/iconvjp.x')

    def test_iconvnm(self):
        self.common_test('newlib/iconvnm.x')

    def test_iconvru(self):
        self.common_test('newlib/iconvru.x')

    def test_mempcpy1(self):
        self.common_test('newlib/memcpy-1.x',timeout=300)

    def test_memmove1(self):
        self.common_test('newlib/memmove1.x',timeout=300)

    def test_nulprintf(self):
        self.common_test('newlib/nulprintf.x')

    def test_size_max(self):
        self.common_test('newlib/size_max.x')

    def test_strcmp(self):
        self.common_test('newlib/strcmp-1.x',timeout=500)

    def test_swprintf(self):
        self.common_test('newlib/swprintf.x')

    def test_tiswctype(self):
        self.common_test('newlib/tiswctype.x')

    def test_tstring(self):
        self.common_test('newlib/tstring.x')

    def test_twctype(self):
        self.common_test('newlib/twctype.x')

    def test_twctype(self):
        self.common_test('newlib/tzset.x')

        