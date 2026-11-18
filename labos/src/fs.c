#include "fs.h"

#include "lib.h"

#define CHUNK (4u << 20)  // some firmware reads and writes at most a few MB in one call

static const char *memchr_ascii(const char *s, char c) {
    for (; *s; s++) {
        if (*s == c) return s;
    }
    return 0;
}

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

// UEFI paths use backslashes and UCS-2; labos paths are plain ASCII
static usize to_efi_path(const char *path, usize length, char16 *out) {
    usize n = 0;
    for (usize i = 0; i < length && path[i] && n < 255; i++) out[n++] = path[i] == '/' ? '\\' : (char16)(u8)path[i];
    out[n] = 0;
    return n;
}

void *fs_read(const char *path, usize *size) {
    char16 name[256];
    to_efi_path(path, 255, name);
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

int fs_write(const char *path, const void *data, usize size) {
    efi_file *root = open_root();
    if (!root) return 0;
    char16 name[256];
    const u64 create = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE;
    // Every directory on the way, from the root
    for (const char *slash = path; (slash = (const char *)memchr_ascii(slash, '/')); slash++) {
        efi_file *dir = 0;
        to_efi_path(path, (usize)(slash - path), name);
        if (root->open(root, &dir, name, create, EFI_FILE_DIRECTORY) == EFI_SUCCESS) dir->close(dir);
    }
    to_efi_path(path, 255, name);
    efi_file *file = 0;
    // An old file is deleted first: opening it would keep its tail when the new one is shorter
    if (root->open(root, &file, name, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0) == EFI_SUCCESS) {
        file->delete_file(file);
    }
    efi_status s = root->open(root, &file, name, create, 0);
    root->close(root);
    if (s != EFI_SUCCESS) {
        log_line("fs: %s: cannot create (%lx)", path, s);
        return 0;
    }
    usize done = 0;
    while (done < size) {
        usize part = size - done < CHUNK ? size - done : CHUNK;
        if (file->write(file, &part, (const u8 *)data + done) != EFI_SUCCESS || !part) break;
        done += part;
    }
    file->flush(file);
    file->close(file);
    if (done != size) log_line("fs: %s: wrote %lu of %lu bytes", path, done, size);
    return done == size;
}
