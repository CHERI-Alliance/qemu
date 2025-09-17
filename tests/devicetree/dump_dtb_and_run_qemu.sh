yocto_path="../../../yocto/build"
devicetree="qemu-system-riscv64cheri-vcu118"

$yocto_path/tmp/work/x86_64-linux/qemu-cheri-system-native/6.2.0+git/build/riscv64cheri-softmmu/qemu-system-riscv64cheri    \
  -nographic -m 1G -smp 1 \
  -machine virt,dumpdtb=$devicetree.dtb \
  -bios $yocto_path/../build/tmp/deploy/images/hobgoblin-vcu118/fw_dynamic.bin \
  -kernel $yocto_path/tmp/deploy/images/hobgoblin-vcu118/Image \
  -drive file=$yocto_path/tmp/deploy/images/hobgoblin-vcu118/core-image-minimal-hobgoblin-vcu118.sdcard.wic,id=virtio0,format=raw \
  -device virtio-blk-device,drive=virtio0 \
  -append "root=/dev/vda2 console=ttyS0" \
  -device virtio-net-device,bus=virtio-mmio-bus.0,netdev=net0 

dtc -I dtb -O dts -s -o $devicetree.dts $devicetree.dtb
sed -i -e '/fw-cfg/,+5d' $devicetree.dts
dtc -I dts -O dtb -o $devicetree.dtb $devicetree.dts

$yocto_path/tmp/work/x86_64-linux/qemu-cheri-system-native/6.2.0+git/build/riscv64cheri-softmmu/qemu-system-riscv64cheri    \
  -nographic -m 1G -smp 1 \
  -machine virt \
  -bios $yocto_path/../build/tmp/deploy/images/hobgoblin-vcu118/fw_dynamic.bin \
  -kernel $yocto_path/tmp/deploy/images/hobgoblin-vcu118/Image \
  -drive file=$yocto_path/tmp/deploy/images/hobgoblin-vcu118/core-image-minimal-hobgoblin-vcu118.sdcard.wic,id=virtio0,format=raw \
  -device virtio-blk-device,drive=virtio0 \
  -append "root=/dev/vda2 console=ttyS0" \
  -device virtio-net-device,bus=virtio-mmio-bus.0,netdev=net0 \
  -netdev user,id=net0,net=192.168.76.0/24,dhcpstart=192.168.76.9 \
  -dtb $devicetree.dtb

rm -f $devicetree.dts $devicetree.dtb