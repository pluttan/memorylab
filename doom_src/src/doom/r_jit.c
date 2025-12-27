//
// r_jit.c - Self-modifying code for DOOM rendering
// Demonstrates JIT optimization vs branching in real-world scenario
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#ifdef __APPLE__
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#endif

#include "r_jit.h"

// Statistics
jit_stats_t jit_stats = {0};
bool jit_mode_enabled = false;

// Frame timing
static struct timespec frame_start_time;
uint64_t jit_frame_calls =
    0; // Calls in current frame (accessible from r_draw.c)

// Auto-switch timing
static struct timespec last_switch_time;
static bool auto_switch_enabled = true;  // Enabled by default
static double switch_interval_sec = 1.0; // Switch every 1 second

// Data logging
static FILE *log_file = NULL;
static const char *log_filename = "jit_benchmark.csv";

// JIT code buffer (for demonstration)
static unsigned char *jit_code = NULL;
static size_t jit_code_size = 4096;

//============================================================================
// Public API
//============================================================================

void R_JIT_Init(void)
{
#ifdef __APPLE__
    jit_code = mmap(NULL, jit_code_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
#else
    jit_code = mmap(NULL, jit_code_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif

    if (jit_code == MAP_FAILED)
    {
        printf("R_JIT_Init: Failed to allocate JIT memory\n");
        jit_code = NULL;
    }
    else
    {
        printf("R_JIT_Init: JIT memory allocated at %p\n", jit_code);
    }

    memset(&jit_stats, 0, sizeof(jit_stats));
    jit_mode_enabled = false;

    // Initialize auto-switch timer
    clock_gettime(CLOCK_MONOTONIC, &last_switch_time);

    // Disable stdout buffering for debug logs
    setvbuf(stdout, NULL, _IONBF, 0);

    // Open log file
    log_file = fopen(log_filename, "w");
    if (log_file)
    {
        // Write CSV header
        fprintf(log_file, "timestamp_ms,mode,frame_time_ms,draw_calls\n");
        fflush(log_file);
        printf("R_JIT_Init: Logging to %s\n", log_filename);
    }
    else
    {
        printf("R_JIT_Init: Warning - could not open log file\n");
    }
}

void R_JIT_Shutdown(void)
{
    if (jit_code && jit_code != MAP_FAILED)
    {
        munmap(jit_code, jit_code_size);
        jit_code = NULL;
    }

    if (log_file)
    {
        fclose(log_file);
        log_file = NULL;
        printf("R_JIT: Benchmark data saved to %s\n", log_filename);
    }
}

void R_JIT_Toggle(void)
{
    jit_mode_enabled = !jit_mode_enabled;
    printf("R_JIT: Mode switched to %s\n",
           jit_mode_enabled ? "JIT (self-modifying)" : "BRANCHING");
}

void R_JIT_ToggleAutoSwitch(void)
{
    auto_switch_enabled = !auto_switch_enabled;
    printf("R_JIT: Auto-switch %s (every %.1f sec)\n",
           auto_switch_enabled ? "ENABLED" : "DISABLED", switch_interval_sec);

    if (auto_switch_enabled)
    {
        clock_gettime(CLOCK_MONOTONIC, &last_switch_time);
    }
}

void R_JIT_PrintStats(void)
{
    printf("\n========== JIT Rendering Statistics ==========\n");
    printf("Mode: %s\n", jit_mode_enabled ? "JIT" : "BRANCHING");
    printf("Auto-switch: %s\n", auto_switch_enabled ? "ON" : "OFF");
    printf("\n");

    printf("JIT mode:\n");
    printf("  Frames:     %llu\n", jit_stats.jit_frames);
    printf("  Calls:      %llu\n", jit_stats.jit_calls);
    printf("  Total time: %.2f ms\n", jit_stats.jit_time_ms);
    if (jit_stats.jit_frames > 0)
    {
        printf("  Avg/frame:  %.3f ms\n",
               jit_stats.jit_time_ms / jit_stats.jit_frames);
    }

    printf("\n");
    printf("BRANCHING mode:\n");
    printf("  Frames:     %llu\n", jit_stats.branch_frames);
    printf("  Calls:      %llu\n", jit_stats.branch_calls);
    printf("  Total time: %.2f ms\n", jit_stats.branch_time_ms);
    if (jit_stats.branch_frames > 0)
    {
        printf("  Avg/frame:  %.3f ms\n",
               jit_stats.branch_time_ms / jit_stats.branch_frames);
    }

    // Calculate speedup
    if (jit_stats.jit_frames > 0 && jit_stats.branch_frames > 0)
    {
        double jit_avg = jit_stats.jit_time_ms / jit_stats.jit_frames;
        double branch_avg = jit_stats.branch_time_ms / jit_stats.branch_frames;
        if (jit_avg > 0)
        {
            printf("\n>>> SPEEDUP: %.2fx <<<\n", branch_avg / jit_avg);
        }
    }
    printf("==============================================\n");
}

// Called at start of each frame (before rendering)
void R_JIT_FrameStart(void)
{
    clock_gettime(CLOCK_MONOTONIC, &frame_start_time);
    jit_frame_calls = 0;

    // Check for auto-switch
    if (auto_switch_enabled)
    {
        struct timespec now;
        double elapsed;

        clock_gettime(CLOCK_MONOTONIC, &now);

        elapsed = (now.tv_sec - last_switch_time.tv_sec) +
                  (now.tv_nsec - last_switch_time.tv_nsec) / 1000000000.0;

        // DEBUG: Print elapsed time every 60 frames to avoid spam
        {
            static int debug_counter = 0;
            if (++debug_counter >= 60)
            {
                printf("R_JIT_FrameStart: Elapsed: %.4f, Interval: %.4f, Mode: "
                       "%s\n",
                       elapsed, switch_interval_sec,
                       jit_mode_enabled ? "JIT" : "BRANCH");
                debug_counter = 0;
            }
        }

        if (elapsed >= switch_interval_sec)
        {
            jit_mode_enabled = !jit_mode_enabled;
            last_switch_time = now;
            printf("R_JIT: Auto-switched to %s (Elapsed: %.4f)\n",
                   jit_mode_enabled ? "JIT" : "BRANCHING", elapsed);
        }
    }
    else
    {
        static int disabled_counter = 0;
        if (++disabled_counter >= 300)
        { // Log every ~5s
            printf("R_JIT: Auto-switch DISABLED\n");
            disabled_counter = 0;
        }
    }
}

// Called at end of each frame (after rendering)
void R_JIT_FrameEnd(void)
{
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double elapsed_ms =
        (end_time.tv_sec - frame_start_time.tv_sec) * 1000.0 +
        (end_time.tv_nsec - frame_start_time.tv_nsec) / 1000000.0;

    // Calculate timestamp from program start (approximate)
    static struct timespec program_start = {0};
    if (program_start.tv_sec == 0)
    {
        program_start = end_time;
    }
    double timestamp_ms =
        (end_time.tv_sec - program_start.tv_sec) * 1000.0 +
        (end_time.tv_nsec - program_start.tv_nsec) / 1000000.0;

    // Update statistics
    if (jit_mode_enabled)
    {
        jit_stats.jit_frames++;
        jit_stats.jit_calls += jit_frame_calls;
        jit_stats.jit_time_ms += elapsed_ms;
    }
    else
    {
        jit_stats.branch_frames++;
        jit_stats.branch_calls += jit_frame_calls;
        jit_stats.branch_time_ms += elapsed_ms;
    }

    // Log raw data to file
    if (log_file)
    {
        fprintf(log_file, "%.2f,%s,%.4f,%llu\n", timestamp_ms,
                jit_mode_enabled ? "JIT" : "BRANCH", elapsed_ms,
                jit_frame_calls);

        // Flush periodically (every 100 frames)
        static int flush_counter = 0;
        if (++flush_counter >= 100)
        {
            fflush(log_file);
            flush_counter = 0;
        }
    }
}

//============================================================================
// JIT Code Generation - Self-Modifying Code
//============================================================================

// Current JIT-compiled function
static jit_draw_column_fn jit_compiled_fn = NULL;
static const void *jit_current_colormap = NULL;

// ARM64 machine code template for column drawing
// This function generates native code with the colormap address embedded
#ifdef __aarch64__

// ARM64: Generate code that draws a column with baked colormap address
// The key optimization: colormap address is an immediate in the code
void R_JIT_GenerateDrawColumn(const void *colormap)
{
    if (!jit_code || jit_code == MAP_FAILED)
        return;

    // Skip if same colormap
    if (colormap == jit_current_colormap && jit_compiled_fn != NULL)
        return;

    jit_current_colormap = colormap;

    // Need to switch to write mode on Apple Silicon
#ifdef __APPLE__
    pthread_jit_write_protect_np(0); // Allow writing
#endif

    uint32_t *code = (uint32_t *) jit_code;
    uintptr_t cmap_addr = (uintptr_t) colormap;

    // ARM64 calling convention: x0=dest, x1=source, x2=colormap(unused),
    //                           w3=count, w4=fracstep, w5=frac
    // We ignore x2 and use our baked address instead!

    int i = 0;

    // Prologue - save callee-saved registers we'll use
    // stp x19, x20, [sp, #-16]!
    code[i++] = 0xa9bf4ff3;
    // stp x21, x22, [sp, #-16]!
    code[i++] = 0xa9bf57f5;

    // Load baked colormap address into x19 (4 instructions for 64-bit immediate)
    // movz x19, #imm16_0
    code[i++] = 0xd2800013 | ((cmap_addr & 0xFFFF) << 5);
    // movk x19, #imm16_1, lsl #16
    code[i++] = 0xf2a00013 | (((cmap_addr >> 16) & 0xFFFF) << 5);
    // movk x19, #imm16_2, lsl #32
    code[i++] = 0xf2c00013 | (((cmap_addr >> 32) & 0xFFFF) << 5);
    // movk x19, #imm16_3, lsl #48
    code[i++] = 0xf2e00013 | (((cmap_addr >> 48) & 0xFFFF) << 5);

    // x20 = SCREENWIDTH (320)
    // movz x20, #320
    code[i++] = 0xd2802814;

    // x21 = dest (x0)
    // mov x21, x0
    code[i++] = 0xaa0003f5;

    // w22 = frac (w5)
    // mov w22, w5
    code[i++] = 0x2a0503f6;

    // Loop: process one pixel per iteration
    // loop_start:
    int loop_start = i;

    // Calculate texture index: (frac >> 16) & 127
    // lsr w8, w22, #16
    code[i++] = 0x53104ec8;
    // and w8, w8, #127
    code[i++] = 0x12001908;

    // Load texel from source: w9 = source[w8]
    // ldrb w9, [x1, x8]
    code[i++] = 0x38686829;

    // Apply colormap (baked address!): w9 = colormap[w9]
    // ldrb w9, [x19, x9]
    code[i++] = 0x38696a69;

    // Store to dest: *dest = pixel
    // strb w9, [x21]
    code[i++] = 0x390002a9;

    // dest += SCREENWIDTH
    // add x21, x21, x20
    code[i++] = 0x8b1402b5;

    // frac += fracstep
    // add w22, w22, w4
    code[i++] = 0x0b0402d6;

    // Decrement count and loop
    // subs w3, w3, #1
    code[i++] = 0x71000463;
    // b.ge loop_start
    int offset = loop_start - i;
    code[i++] = 0x5400000a | ((offset & 0x7FFFF) << 5);

    // Epilogue
    // ldp x21, x22, [sp], #16
    code[i++] = 0xa8c157f5;
    // ldp x19, x20, [sp], #16
    code[i++] = 0xa8c14ff3;
    // ret
    code[i++] = 0xd65f03c0;

#ifdef __APPLE__
    pthread_jit_write_protect_np(1); // Protect from writing
    sys_icache_invalidate(jit_code, i * 4);
#endif

    jit_compiled_fn = (jit_draw_column_fn) jit_code;

    // Debug: only print on first generation
    static int gen_count = 0;
    if (gen_count++ < 3)
    {
        printf("R_JIT: Generated %d bytes of ARM64 code for colormap %p\n",
               i * 4, colormap);
    }
}

#elif defined(__x86_64__)

// x86_64 JIT template - pre-compiled machine code with placeholder for colormap address
// This template implements the column drawing loop with the colormap address baked in
static const unsigned char x86_64_jit_template[] = {
    // === Prologue ===
    0x53,       // push rbx
    0x41, 0x54, // push r12
    0x41, 0x55, // push r13

    // === Load baked colormap address into r12 ===
    // mov r12, imm64 (colormap address will be patched at offset 7)
    0x49, 0xbc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, // placeholder: 8 bytes at offset 7

    // === Setup constants ===
    // mov r13d, 320 (SCREENWIDTH)
    0x41, 0xbd, 0x40, 0x01, 0x00, 0x00,

    // mov eax, r9d (frac = arg6)
    0x44, 0x89, 0xc8,

    // === Main loop (offset 28) ===
    // loop_start:

    // Calculate texture index: (frac >> 16) & 127
    0x89, 0xc3,       // mov ebx, eax
    0xc1, 0xeb, 0x10, // shr ebx, 16
    0x83, 0xe3, 0x7f, // and ebx, 127

    // Load texel from source
    0x0f, 0xb6, 0x1c, 0x1e, // movzx ebx, byte ptr [rsi + rbx]

    // Apply colormap (baked address in r12!)
    0x41, 0x0f, 0xb6, 0x1c, 0x1c, // movzx ebx, byte ptr [r12 + rbx]

    // Store pixel to dest
    0x88, 0x1f, // mov byte ptr [rdi], bl

    // Advance dest pointer
    0x4c, 0x01, 0xef, // add rdi, r13  (dest += SCREENWIDTH)

    // Advance frac
    0x44, 0x01, 0xc0, // add eax, r8d  (frac += fracstep)

    // Loop control
    0xff, 0xc9, // dec ecx
    0x79, 0xe3, // jns loop_start (offset -29)

    // === Epilogue ===
    0x41, 0x5d, // pop r13
    0x41, 0x5c, // pop r12
    0x5b,       // pop rbx
    0xc3        // ret
};

#define X86_64_COLORMAP_OFFSET 7 // Offset where colormap address is patched
#define X86_64_TEMPLATE_SIZE   sizeof(x86_64_jit_template)

// x86_64: Generate code with baked colormap address using template
void R_JIT_GenerateDrawColumn(const void *colormap)
{
    if (!jit_code || jit_code == MAP_FAILED)
        return;

    if (colormap == jit_current_colormap && jit_compiled_fn != NULL)
        return;

    jit_current_colormap = colormap;

    // Copy template to executable memory
    memcpy(jit_code, x86_64_jit_template, X86_64_TEMPLATE_SIZE);

    // Patch the colormap address (8 bytes at offset 7)
    uintptr_t cmap_addr = (uintptr_t) colormap;
    memcpy((unsigned char *) jit_code + X86_64_COLORMAP_OFFSET, &cmap_addr, 8);

    jit_compiled_fn = (jit_draw_column_fn) jit_code;

    static int gen_count = 0;
    if (gen_count++ < 3)
    {
        printf(
            "R_JIT: Generated %zu bytes of x86_64 JIT code for colormap %p\n",
            X86_64_TEMPLATE_SIZE, colormap);
    }
}

#else

// Fallback for unsupported architectures
void R_JIT_GenerateDrawColumn(const void *colormap)
{
    (void) colormap;
    jit_compiled_fn = NULL;
}

#endif

jit_draw_column_fn R_JIT_GetDrawColumn(void)
{
    return jit_compiled_fn;
}
