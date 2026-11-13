// Minimal UEFI 2.x definitions: only the tables and protocols labos uses.
// The x86_64-unknown-windows target already uses the Microsoft x64 ABI that UEFI requires.
#ifndef LABOS_EFI_H
#define LABOS_EFI_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed int i32;
typedef signed long long i64;
typedef u64 usize;
typedef u16 char16;

typedef u64 efi_status;
typedef void *efi_handle;
typedef void *efi_event;
typedef u64 efi_phys_addr;

#define EFI_SUCCESS 0ULL
#define EFI_ERR(n) (0x8000000000000000ULL | (n))
#define EFI_NOT_READY EFI_ERR(6)
#define EFI_BUFFER_TOO_SMALL EFI_ERR(5)
#define EFI_ERROR(s) (((i64)(s)) < 0)

typedef struct {
    u32 data1;
    u16 data2;
    u16 data3;
    u8 data4[8];
} efi_guid;

typedef struct {
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 crc32;
    u32 reserved;
} efi_table_header;

// ==========================
// ===  Text console      ===
// ==========================

typedef struct {
    u16 scan_code;
    char16 unicode_char;
} efi_input_key;

typedef struct efi_simple_text_input {
    efi_status (*reset)(struct efi_simple_text_input *self, u8 extended);
    efi_status (*read_key_stroke)(struct efi_simple_text_input *self, efi_input_key *key);
    efi_event wait_for_key;
} efi_simple_text_input;

typedef struct efi_simple_text_output {
    efi_status (*reset)(struct efi_simple_text_output *self, u8 extended);
    efi_status (*output_string)(struct efi_simple_text_output *self, const char16 *text);
    void *test_string;
    void *query_mode;
    void *set_mode;
    void *set_attribute;
    efi_status (*clear_screen)(struct efi_simple_text_output *self);
    void *set_cursor_position;
    efi_status (*enable_cursor)(struct efi_simple_text_output *self, u8 visible);
    void *mode;
} efi_simple_text_output;

// ==========================
// ===  Boot services     ===
// ==========================

typedef enum {
    EFI_ALLOCATE_ANY_PAGES,
    EFI_ALLOCATE_MAX_ADDRESS,
    EFI_ALLOCATE_ADDRESS,
} efi_allocate_type;

typedef enum {
    EFI_RESERVED_MEMORY_TYPE,
    EFI_LOADER_CODE,
    EFI_LOADER_DATA,
    EFI_BOOT_SERVICES_CODE,
    EFI_BOOT_SERVICES_DATA,
    EFI_RUNTIME_SERVICES_CODE,
    EFI_RUNTIME_SERVICES_DATA,
    EFI_CONVENTIONAL_MEMORY,
} efi_memory_type;

typedef struct {
    u32 type;
    efi_phys_addr physical_start;
    u64 virtual_start;
    u64 number_of_pages;
    u64 attribute;
} efi_memory_descriptor;

#define EFI_TPL_APPLICATION 4
#define EFI_TPL_CALLBACK 8
#define EFI_TPL_NOTIFY 16

#define EFI_OPEN_PROTOCOL_GET_PROTOCOL 0x02
#define EFI_OPEN_PROTOCOL_BY_DRIVER 0x10
#define EFI_OPEN_PROTOCOL_EXCLUSIVE 0x20

typedef enum { EFI_ALL_HANDLES, EFI_BY_REGISTER_NOTIFY, EFI_BY_PROTOCOL } efi_locate_search_type;

typedef struct {
    efi_table_header hdr;
    usize (*raise_tpl)(usize new_tpl);
    void (*restore_tpl)(usize old_tpl);
    efi_status (*allocate_pages)(efi_allocate_type type, efi_memory_type mem, usize pages, efi_phys_addr *memory);
    efi_status (*free_pages)(efi_phys_addr memory, usize pages);
    efi_status (*get_memory_map)(usize *size, efi_memory_descriptor *map, usize *key, usize *desc_size, u32 *desc_version);
    efi_status (*allocate_pool)(efi_memory_type mem, usize size, void **buffer);
    efi_status (*free_pool)(void *buffer);
    void *create_event;
    void *set_timer;
    efi_status (*wait_for_event)(usize count, efi_event *events, usize *index);
    void *signal_event;
    void *close_event;
    efi_status (*check_event)(efi_event event);
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    efi_status (*handle_protocol)(efi_handle handle, const efi_guid *protocol, void **iface);
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    void *exit_boot_services;
    void *get_next_monotonic_count;
    efi_status (*stall)(usize microseconds);
    efi_status (*set_watchdog_timer)(usize timeout, u64 code, usize data_size, char16 *data);
    void *connect_controller;
    void *disconnect_controller;
    efi_status (*open_protocol)(efi_handle handle, const efi_guid *protocol, void **iface, efi_handle agent,
                                efi_handle controller, u32 attributes);
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    efi_status (*locate_handle_buffer)(efi_locate_search_type type, const efi_guid *protocol, void *key,
                                       usize *count, efi_handle **buffer);
    efi_status (*locate_protocol)(const efi_guid *protocol, void *registration, void **iface);
    void *install_multiple_protocol_interfaces;
    void *uninstall_multiple_protocol_interfaces;
    void *calculate_crc32;
    void *copy_mem;
    void *set_mem;
    void *create_event_ex;
} efi_boot_services;

typedef struct {
    efi_table_header hdr;
    void *get_time;
    void *set_time;
    void *get_wakeup_time;
    void *set_wakeup_time;
    void *set_virtual_address_map;
    void *convert_pointer;
    void *get_variable;
    void *get_next_variable_name;
    void *set_variable;
    void *get_next_high_monotonic_count;
    void (*reset_system)(u32 type, efi_status status, usize data_size, void *data);
} efi_runtime_services;

typedef struct {
    efi_table_header hdr;
    char16 *firmware_vendor;
    u32 firmware_revision;
    efi_handle console_in_handle;
    efi_simple_text_input *con_in;
    efi_handle console_out_handle;
    efi_simple_text_output *con_out;
    efi_handle standard_error_handle;
    efi_simple_text_output *std_err;
    efi_runtime_services *runtime_services;
    efi_boot_services *boot_services;
    usize number_of_table_entries;
    void *configuration_table;
} efi_system_table;

// ==========================
// ===  Graphics output   ===
// ==========================

#define EFI_GOP_GUID {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}}

typedef enum {
    EFI_PIXEL_RGB_RESERVED_8BIT,
    EFI_PIXEL_BGR_RESERVED_8BIT,
    EFI_PIXEL_BIT_MASK,
    EFI_PIXEL_BLT_ONLY,
} efi_pixel_format;

typedef struct {
    u32 version;
    u32 horizontal_resolution;
    u32 vertical_resolution;
    efi_pixel_format pixel_format;
    u32 red_mask, green_mask, blue_mask, reserved_mask;
    u32 pixels_per_scan_line;
} efi_gop_mode_info;

typedef struct {
    u32 max_mode;
    u32 mode;
    efi_gop_mode_info *info;
    usize size_of_info;
    efi_phys_addr frame_buffer_base;
    usize frame_buffer_size;
} efi_gop_mode;

typedef struct efi_gop {
    efi_status (*query_mode)(struct efi_gop *self, u32 mode, usize *size, efi_gop_mode_info **info);
    efi_status (*set_mode)(struct efi_gop *self, u32 mode);
    void *blt;
    efi_gop_mode *mode;
} efi_gop;

// ==========================
// ===  Simple network    ===
// ==========================

#define EFI_SNP_GUID {0xa19832b9, 0xac25, 0x11d3, {0x9a, 0x2d, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}}

typedef struct {
    u8 addr[32];
} efi_mac_address;

#define EFI_SNP_STOPPED 0
#define EFI_SNP_STARTED 1
#define EFI_SNP_INITIALIZED 2

#define EFI_SNP_RECEIVE_UNICAST 0x01
#define EFI_SNP_RECEIVE_MULTICAST 0x02
#define EFI_SNP_RECEIVE_BROADCAST 0x04

typedef struct {
    u32 state;
    u32 hw_address_size;
    u32 media_header_size;
    u32 max_packet_size;
    u32 nv_ram_size;
    u32 nv_ram_access_size;
    u32 receive_filter_mask;
    u32 receive_filter_setting;
    u32 max_mcast_filter_count;
    u32 mcast_filter_count;
    efi_mac_address mcast_filter[16];
    efi_mac_address current_address;
    efi_mac_address broadcast_address;
    efi_mac_address permanent_address;
    u8 if_type;
    u8 mac_address_changeable;
    u8 multiple_tx_supported;
    u8 media_present_supported;
    u8 media_present;
} efi_snp_mode;

typedef struct efi_snp {
    u64 revision;
    efi_status (*start)(struct efi_snp *self);
    efi_status (*stop)(struct efi_snp *self);
    efi_status (*initialize)(struct efi_snp *self, usize extra_rx, usize extra_tx);
    efi_status (*reset)(struct efi_snp *self, u8 extended);
    efi_status (*shutdown)(struct efi_snp *self);
    efi_status (*receive_filters)(struct efi_snp *self, u32 enable, u32 disable, u8 reset_mcast, usize mcast_count,
                                  efi_mac_address *mcast);
    void *station_address;
    void *statistics;
    void *mcast_ip_to_mac;
    void *nv_data;
    efi_status (*get_status)(struct efi_snp *self, u32 *interrupt_status, void **tx_buf);
    efi_status (*transmit)(struct efi_snp *self, usize header_size, usize buffer_size, void *buffer,
                           efi_mac_address *src, efi_mac_address *dst, u16 *protocol);
    efi_status (*receive)(struct efi_snp *self, usize *header_size, usize *buffer_size, void *buffer,
                          efi_mac_address *src, efi_mac_address *dst, u16 *protocol);
    efi_event wait_for_packet;
    efi_snp_mode *mode;
} efi_snp;

// ==========================
// ===  Files             ===
// ==========================

#define EFI_LOADED_IMAGE_GUID {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}
#define EFI_SIMPLE_FS_GUID {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}

typedef struct {
    u32 revision;
    efi_handle parent_handle;
    efi_system_table *system_table;
    efi_handle device_handle;
    void *file_path;
    void *reserved;
    u32 load_options_size;
    void *load_options;
    void *image_base;
    u64 image_size;
    u32 image_code_type, image_data_type;
    void *unload;
} efi_loaded_image;

#define EFI_FILE_MODE_READ 1ULL

typedef struct efi_file {
    u64 revision;
    efi_status (*open)(struct efi_file *self, struct efi_file **out, const char16 *name, u64 mode, u64 attributes);
    efi_status (*close)(struct efi_file *self);
    void *delete_file;
    efi_status (*read)(struct efi_file *self, usize *size, void *buffer);
    void *write;
    efi_status (*get_position)(struct efi_file *self, u64 *position);
    efi_status (*set_position)(struct efi_file *self, u64 position);
    void *get_info;
    void *set_info;
    void *flush;
} efi_file;

typedef struct efi_simple_fs {
    u64 revision;
    efi_status (*open_volume)(struct efi_simple_fs *self, efi_file **root);
} efi_simple_fs;

#endif
