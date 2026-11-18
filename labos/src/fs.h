// Files next to labos on the boot volume (the USB stick, or the FAT directory of QEMU).
#ifndef LABOS_FS_H
#define LABOS_FS_H

#include "efi.h"

// Reads a whole file; path like "labos/manual.txt". Returns 0 when it is missing. Free with fs_release
void *fs_read(const char *path, usize *size);
void fs_release(void *data, usize size);

// Writes a whole file, replacing an old one and creating the directories on the way; 1 on success
int fs_write(const char *path, const void *data, usize size);

#endif
