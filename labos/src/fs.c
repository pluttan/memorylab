#include "fs.h"

#include "lib.h"

#define CHUNK (4u << 20)  // some firmware reads at most a few MB in one call

static efi_file *open_root(void) {
    efi_guid image_guid = EFI_LOADED_IMAGE_GUID, fs_guid = EFI_SIMPLE_FS_GUID;
    efi_loaded_image *image = 0;
    efi_simple_fs *fs = 0;
    efi_file *root = 0;
    if (BS->handle_protocol(IMAGE, &image_guid, (void **)&image) != EFI_SUCCESS) return 0;
    if (BS->handle_protocol(image->device_handle, &fs_guid, (void **)&fs) != EFI_SUCCESS) return 0;
    if (fs->open_volume(fs, &root) != EFI_SUCCESS) return 0;
    return root;
}

void *fs_read(const char *path, usize *size) {
    char16 name[256];
    usize n = 0;
    for (const char *p = path; *p && n < 255; p++) name[n++] = *p == '/' ? '\\' : (char16)(u8)*p;
    name[n] = 0;
    efi_file *root = open_root(), *file = 0;
    if (!root) return 0;
    efi_status s = root->open(root, &file, name, EFI_FILE_MODE_READ, 0);
    root->close(root);
    if (s != EFI_SUCCESS) {
        log_line("fs: %s: not found (%lx)", path, s);
        return 0;
    }
    u64 length = 0;
    file->set_position(file, 0xffffffffffffffffULL);  // the end of the file
    file->get_position(file, &length);
    file->set_position(file, 0);
    u8 *data = alloc_pages((length + 4095) / 4096 + 1);
    usize done = 0;
    while (data && done < length) {
        usize part = length - done < CHUNK ? length - done : CHUNK;
        if (file->read(file, &part, data + done) != EFI_SUCCESS || !part) break;
        done += part;
    }
    file->close(file);
    if (!data || done != length) {
        log_line("fs: %s: read %lu of %lu bytes", path, done, length);
        if (data) BS->free_pages((efi_phys_addr)data, (length + 4095) / 4096 + 1);
        return 0;
    }
    data[length] = 0;  // text files end with a zero byte
    *size = length;
    return data;
}

void fs_release(void *data, usize size) {
    if (data) BS->free_pages((efi_phys_addr)data, (size + 4095) / 4096 + 1);
}
