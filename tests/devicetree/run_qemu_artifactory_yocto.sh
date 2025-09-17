artifactory_path="../../../artifactory"
machine="vcu118"
artifactory_url_base="https://artifactory.codasip.com/ui/native/yocto/codasip-poky/2.0.0-pre/20250313:1917"

fw_dynamic="fw_dynamic.bin"
kernel_image="Image--6.10.0+git0+16bf052b01-r0-hobgoblin-vcu118-R2.0.0-pre.bin"
wic_image="core-image-minimal-hobgoblin-$machine.sdcard-R2.0.0-pre.wic"

files=(
    "$fw_dynamic"
    "$kernel_image"
    "$wic_image"
)

for filename in "${files[@]}"; do
    local_path="$artifactory_path/hobgoblin-$machine/$filename"
    remote_url="$artifactory_url_base/$filename"
    if [ ! -f "$local_path" ]; then
        echo "Missing $local_path; downloading..."
        mkdir -p "$(dirname "$local_path")"
        curl -fLo "$local_path" "$remote_url"
        if [ $? -ne 0 ]; then
            echo "Failed to download $remote_url" >&2
            exit 1
        fi
    fi
done

qemu-system-riscv64cheri \
  -nographic -m 1G -smp 1 \
  -machine virt \
  -bios "$artifactory_path/hobgoblin-$machine/$fw_dynamic" \
  -kernel "$artifactory_path/hobgoblin-$machine/$kernel_image" \
  -drive file="$artifactory_path/hobgoblin-$machine/$wic_image",id=virtio0,format=raw \
  -device virtio-blk-device,drive=virtio0 \
  -append "root=/dev/vda2 console=ttyS0" \
  -device virtio-net-device,bus=virtio-mmio-bus.0,netdev=net0 \
  -netdev user,id=net0,net=192.168.76.0/24,dhcpstart=192.168.76.9 \
  -dtb qemu-system-riscv64cheri-v2-vcu118.dtb
