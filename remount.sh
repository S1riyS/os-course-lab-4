sudo umount /mnt/vt
sudo rmmod vtfs
sudo make
sudo insmod vtfs.ko storage_type=ram # Storage types: "ram" and "net"
sudo mount -t vtfs "REMOUNT" /mnt/vt