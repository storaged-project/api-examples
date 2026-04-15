# api-examples

Examples of how the various storage APIs from the [storaged-project](https://github.com/storaged-project) can be utilized.

Most examples demonstrate the same workflow -- creating an encrypted LVM setup on two disks:

1. Wipe existing disk signatures
2. Create LVM physical volumes on two disks
3. Create a volume group combining the two PVs
4. Create a swap logical volume and run mkswap
5. Create a data logical volume and encrypt it with LUKS2
6. Format the encrypted volume with XFS
7. Clean up by removing all created devices

`demo-1-progress.c` is a standalone example demonstrating libblockdev's progress reporting by running e2fsck on an ext4 filesystem with a progress callback.

## Examples

### C

| File | Library | Description |
|------|---------|-------------|
| `demo-1-libblockdev.c` | [libblockdev](https://github.com/storaged-project/libblockdev) | Direct library calls for storage management |
| `demo-1-progress.c` | [libblockdev](https://github.com/storaged-project/libblockdev) | Progress reporting with callbacks (runs e2fsck with progress) |
| `demo-1-libudisks.c` | [libudisks](https://github.com/storaged-project/udisks) | Storage management via the UDisks2 daemon (D-Bus) |

### Python

| File | Library | Description |
|------|---------|-------------|
| `demo-1-libblockdev-python` | [libblockdev](https://github.com/storaged-project/libblockdev) | Python bindings for libblockdev via GObject introspection |
| `demo-1-blivet` | [Blivet](https://github.com/storaged-project/blivet) | High-level declarative storage configuration |
| `demo-1-udisks-dbus` | [UDisks2](https://github.com/storaged-project/udisks) | Direct D-Bus calls to the UDisks2 daemon |

## License

MIT
