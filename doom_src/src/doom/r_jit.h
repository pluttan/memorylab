//
// r_jit.h - Self-modifying code for DOOM rendering
// Comparison: branching vs JIT-patched code
//

#ifndef R_JIT_H
#define R_JIT_H

#include <stdint.h>
#include <stdbool.h>

// Statistics - now tracks per-frame timing
typedef struct {
    uint64_t jit_calls;        // Total R_DrawColumn calls in JIT mode
    uint64_t branch_calls;     // Total R_DrawColumn calls in branching mode
    uint64_t jit_frames;       // Number of frames in JIT mode
    uint64_t branch_frames;    // Number of frames in branching mode
    double jit_time_ms;        // Total time in JIT mode (ms)
    double branch_time_ms;     // Total time in branching mode (ms)
} jit_stats_t;

extern jit_stats_t jit_stats;
extern bool jit_mode_enabled;    // true = JIT, false = branching
extern uint64_t jit_frame_calls; // Increment this in R_DrawColumn

// Initialize JIT system
void R_JIT_Init(void);

// Shutdown JIT system  
void R_JIT_Shutdown(void);

// Toggle mode
void R_JIT_Toggle(void);

// Toggle auto-switch mode (every 1 second)
void R_JIT_ToggleAutoSwitch(void);

// Print statistics
void R_JIT_PrintStats(void);

// Call at start/end of frame to measure render time
void R_JIT_FrameStart(void);
void R_JIT_FrameEnd(void);

// JIT-compiled column drawer function type
// Parameters: dest, source, colormap, count, fracstep, frac
typedef void (*jit_draw_column_fn)(void *dest, const void *source, 
                                    const void *colormap, int count,
                                    int fracstep, int frac);

// Generate JIT code for current colormap (call when colormap changes)
void R_JIT_GenerateDrawColumn(const void *colormap);

// Get the JIT-compiled function (NULL if not available)
jit_draw_column_fn R_JIT_GetDrawColumn(void);

#endif // R_JIT_H
