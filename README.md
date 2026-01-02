# Rawstor Object Storage Target (OST)

## Build requirements
Example for Debian:
```bash
apt install liburing-dev libxxhash-dev
```

## Run example:
```
# ost server
./autogen.sh
./configure
make
mkdir /tmp/objects
truncate -s 2G /tmp/objects/00000000-0000-7000-8000-000100000000
./src/ost 8080 /tmp/objects/

# qemu driver
sudo modprobe vduse virtio-vdpa
# run in separate session
sudo ./storage-daemon/qemu-storage-daemon \
  --blockdev '{"node-name":"test1","driver":"rawstor","object-id":"00000000-0000-7000-8000-000100000000","cache":{"direct":true,"no-flush":false},"discard":"unmap"}' \
  --export type=vduse-blk,id=test1,node-name=test1,name=test1,num-queues=16,queue-size=128,writable=true
sudo vdpa dev add name test1 mgmtdev vduse

# Example integrity test
fio --bs=4k --iodepth=128 --numjobs=4 --rw=write --name=test --ioengine=libaio --direct=1 --verify=sha1 --do_verify=1 --group_reporting=1 --filename=/dev/vda

# when finished
sudo vdpa dev del test1
```
