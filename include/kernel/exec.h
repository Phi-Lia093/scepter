#ifndef EXEC_H
#define EXEC_H

#include <stdint.h>

/* ============================================================================
 * Flat Binary Format
 * 
 * Simple executable format with no complex segments.
 * Everything is loaded as a single RWX section at USER_BASE.
 * ============================================================================ */

/* Flat binary header (16 bytes) */
typedef struct {
    uint32_t magic;        /* 0x464C4154 "FLAT" */
    uint32_t entry;        /* Entry point offset from USER_BASE */
    uint32_t text_size;    /* Size of code/data (everything after header) */
    uint32_t reserved;     /* Reserved for future use */
} flat_header_t;

#define FLAT_MAGIC 0x464C4154  /* "FLAT" */
#define USER_BASE  0x08000000  /* Load address: 128MB */

/* ============================================================================
 * Functions
 * ============================================================================ */

/**
 * Load and execute a flat binary
 * @param path Path to binary file
 * @return 0 on success, -1 on error
 * 
 * Note: This function does NOT return on success!
 * It switches to userspace and never comes back.
 */
int exec_flat(const char *path);

#endif /* EXEC_H */