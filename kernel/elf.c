/* ============================================================================
 * Executable Loader
 *
 * Primary format:  ELF  (ELF32 on i386, ELF64 on x86_64)
 * Legacy format:   flat (header-less RWX blob loaded at USER_TEXT_START)
 *
 * All memory mapping goes through the arch-neutral arch_mm_* API
 * (arch/paging.h) and the VMA layer, so this file is identical for every
 * architecture.  Format dispatch is done in load_binary(): files starting
 * with the ELF magic are loaded as ELF, everything else falls back to the
 * legacy flat loader.
 *
 * ELF loading policy (initial implementation):
 *   - ET_EXEC (fixed load addresses) only.
 *   - Every PT_LOAD segment is mapped eagerly, page by page.  A page that is
 *     covered by several segments (a segment-boundary page) is allocated and
 *     mapped ONCE, with the union of those segments' permissions; standalone
 *     .text/.rodata pages stay non-writable.
 *   - One VMA per segment preserves the segment's own permissions for the
 *     page-fault handler.  Overlapping VMAs are safe because
 *     arch_mm_free_user_pages clears each PTE after freeing the page.
 * ============================================================================ */

#include "kernel/elf.h"
#include "kernel/exec.h"
#include "kernel/sched.h"
#include "arch/paging.h"
#include "mm/buddy.h"
#include "mm/slab.h"
#include "mm/vma.h"
#include "fs/fs.h"
#include "lib/printk.h"
#include "lib/string.h"
#include "errno.h"

#define PAGE_SIZE 4096
#define PAGE_ALIGN_DOWN(addr) ((uintptr_t)(addr) & ~((uintptr_t)PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(addr)   (((uintptr_t)(addr) + (uintptr_t)PAGE_SIZE - 1) \
                               & ~((uintptr_t)PAGE_SIZE - 1))

/* Upper bound on the program-header count, so a hostile header cannot make
 * us read unbounded amounts of data. */
#define ELF_MAX_PHDRS 256

/* ============================================================================
 * Legacy flat image loader
 *
 * The whole file is mapped as a single RWX region at USER_TEXT_START, entry
 * == USER_TEXT_START (offset 0).  This is the historical Scepter format and
 * is kept for backward compatibility ("reserve legacy flat binary support").
 * ============================================================================ */

static int load_flat(struct task_struct *task, int fd, uint32_t file_size,
                     exec_image_t *img)
{
    uint32_t num_pages = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t bytes_loaded = 0;

    for (uint32_t i = 0; i < num_pages; i++) {
        void *page_virt = page_alloc(PAGE_SIZE);
        if (!page_virt)
            return -ENOMEM;

        uint32_t bytes_to_read = PAGE_SIZE;
        if (bytes_loaded + bytes_to_read > file_size)
            bytes_to_read = file_size - bytes_loaded;

        char *page_buf = (char *)page_virt;
        uint32_t offset = 0;
        while (offset < bytes_to_read) {
            int n = fs_read(fd, page_buf + offset, bytes_to_read - offset);
            if (n <= 0) {
                page_free(page_virt);
                return -EIO;
            }
            offset += n;
        }
        bytes_loaded += offset;

        /* Zero the tail of the last page. */
        if (offset < PAGE_SIZE)
            memset(page_buf + offset, 0, PAGE_SIZE - offset);

        uint32_t vaddr = USER_TEXT_START + (i * PAGE_SIZE);
        uint32_t phys = VIRT_TO_PHYS((uintptr_t)page_virt);
        if (arch_mm_map_user(task, vaddr, phys, 0x7) < 0) {  /* P|R/W|U/S */
            page_free(page_virt);
            return -ENOMEM;
        }
    }

    img->entry      = USER_TEXT_START;
    img->code_start = USER_TEXT_START;
    img->code_end   = USER_TEXT_START + file_size;
    img->brk_start  = PAGE_ALIGN_UP(img->code_end);
    if (img->brk_start < USER_HEAP_START)
        img->brk_start = USER_HEAP_START;

    /* Track the loaded range so the page-fault handler can enforce
     * permissions and exec/exit teardown can free the pages. */
    vma_t *vma = vma_create(USER_TEXT_START, PAGE_ALIGN_UP(img->code_end),
                            VM_READ | VM_EXEC, VMA_CODE);
    if (!vma)
        return -ENOMEM;
    vma_insert(task, vma);

    return 0;
}

/* ============================================================================
 * ELF loader
 *
 * Strategy: segments that share a 4 KB page are grouped into page-aligned
 * regions purely so each virtual page is allocated and mapped EXACTLY ONCE
 * (a boundary page between two segments must not be double-mapped).  PTE
 * permissions are the union of the segments that cover the specific page;
 * standalone .text/.rodata pages stay non-writable.
 *
 * VMAs are created per SEGMENT (not per region) so each segment's own
 * permissions are preserved for the page-fault handler.  Segment VMAs may
 * overlap on a shared boundary page; the exec/exit teardown is safe because
 * arch_mm_free_user_pages clears each PTE after freeing the page, so a page
 * is never freed twice.
 * ============================================================================ */

/* One validated PT_LOAD segment (native widths). */
typedef struct {
    uintptr_t vaddr;    /* segment virtual address                */
    uintptr_t filesz;   /* bytes backed by the file               */
    uintptr_t memsz;    /* bytes in memory (>= filesz; .bss tail) */
    uintptr_t offset;   /* file offset of the segment             */
    uint32_t  flags;    /* PF_R / PF_W / PF_X                     */
} elf_seg_t;

/* A page-aligned union of one or more adjacent/overlapping segments, used
 * only as the set of pages that must be mapped (each page exactly once). */
typedef struct {
    uintptr_t lo;       /* page-aligned start             */
    uintptr_t hi;       /* page-aligned end (exclusive)   */
} elf_region_t;

/**
 * elf_load_common - map a set of validated PT_LOAD segments.
 *
 * @param segs  Array of segments (sorted in place by vaddr)
 * @param nsegs Number of segments
 * @return 0 on success, -errno on failure
 */
static int elf_load_common(struct task_struct *task, int fd,
                           elf_seg_t *segs, uint32_t nsegs,
                           exec_image_t *img)
{
    /* Sort segments by vaddr (insertion sort; nsegs is tiny). */
    for (uint32_t i = 1; i < nsegs; i++) {
        elf_seg_t key = segs[i];
        uint32_t j = i;
        while (j > 0 && segs[j - 1].vaddr > key.vaddr) {
            segs[j] = segs[j - 1];
            j--;
        }
        segs[j] = key;
    }

    /* Merge page-aligned ranges into regions. */
    elf_region_t *regions = (elf_region_t *)kalloc(nsegs * sizeof(elf_region_t));
    if (!regions)
        return -ENOMEM;
    uint32_t nregions = 0;

    for (uint32_t i = 0; i < nsegs; i++) {
        uintptr_t r_lo = PAGE_ALIGN_DOWN(segs[i].vaddr);
        uintptr_t r_hi = PAGE_ALIGN_UP(segs[i].vaddr + segs[i].memsz);
        if (nregions > 0 && r_lo <= regions[nregions - 1].hi) {
            if (r_hi > regions[nregions - 1].hi)
                regions[nregions - 1].hi = r_hi;
        } else {
            regions[nregions].lo = r_lo;
            regions[nregions].hi = r_hi;
            nregions++;
        }
    }

    /* Map every region once: each page is allocated and mapped a single
     * time even when two segments share a page (segment boundary pages). */
    uintptr_t min_vaddr = UINTPTR_MAX;
    uintptr_t max_end   = 0;

    for (uint32_t r = 0; r < nregions; r++) {
        uintptr_t lo = regions[r].lo;
        uintptr_t hi = regions[r].hi;

        for (uintptr_t cur = lo; cur < hi; cur += PAGE_SIZE) {
            void *page_virt = page_alloc(PAGE_SIZE);
            if (!page_virt) {
                kfree(regions);
                return -ENOMEM;
            }

            /* Zero the whole page: this fills the pre-segment gap, the
             * p_filesz..p_memsz holes (.bss) and any unused tail. */
            memset(page_virt, 0, PAGE_SIZE);

            /* Copy every segment's file-backed bytes that fall in this page. */
            for (uint32_t i = 0; i < nsegs; i++) {
                elf_seg_t *s = &segs[i];
                uintptr_t s_end = s->vaddr + s->filesz;
                if (s_end <= cur || s->vaddr >= cur + PAGE_SIZE)
                    continue;   /* this segment does not reach this page */

                uintptr_t f_lo = (s->vaddr > cur) ? s->vaddr : cur;
                uintptr_t f_hi = (s_end < cur + PAGE_SIZE) ? s_end
                                                           : (cur + PAGE_SIZE);
                if (f_hi <= f_lo)
                    continue;

                char *dst = (char *)page_virt + (f_lo - cur);
                uintptr_t cnt  = f_hi - f_lo;
                uintptr_t foff = s->offset + (f_lo - s->vaddr);
                uintptr_t done = 0;
                while (done < cnt) {
                    int n = fs_pread(fd, dst + done, cnt - done,
                                     (uint32_t)(foff + done));
                    if (n <= 0) {
                        page_free(page_virt);
                        kfree(regions);
                        return -EIO;
                    }
                    done += n;
                }
            }

            /* PTE permissions = union of the segments whose memory image
             * covers THIS page.  A page truly shared by a read-only and a
             * writable segment becomes writable (as on Linux), but pages
             * only covered by .text/.rodata stay non-writable. */
            uint32_t page_flags = 0;
            for (uint32_t i = 0; i < nsegs; i++) {
                if (segs[i].vaddr < cur + PAGE_SIZE &&
                    segs[i].vaddr + segs[i].memsz > cur)
                    page_flags |= segs[i].flags;
            }
            uint32_t pte_flags = 0x5;    /* Present | User */
            if (page_flags & PF_W)
                pte_flags |= 0x2;        /* Read/Write */

            uintptr_t phys = VIRT_TO_PHYS((uintptr_t)page_virt);
            if (arch_mm_map_user(task, cur, phys, pte_flags) < 0) {
                page_free(page_virt);
                kfree(regions);
                return -ENOMEM;
            }
        }

        if (lo < min_vaddr)
            min_vaddr = lo;
        if (hi > max_end)
            max_end = hi;
    }

    kfree(regions);

    if (max_end == 0)
        return -ENOEXEC;                 /* no loadable segment */

    /* One VMA per segment, with the segment's own permissions.  Segments may
     * share a page (their VMAs can overlap there); teardown is safe because
     * arch_mm_free_user_pages clears each PTE after freeing it, so a page is
     * freed at most once even when two overlapping VMAs are walked. */
    for (uint32_t i = 0; i < nsegs; i++) {
        uint32_t vm_flags = VM_READ;
        if (segs[i].flags & PF_W)
            vm_flags |= VM_WRITE;
        if (segs[i].flags & PF_X)
            vm_flags |= VM_EXEC;

        vma_t *vma = vma_create(segs[i].vaddr, segs[i].vaddr + segs[i].memsz,
                                vm_flags, VMA_CODE);
        if (!vma)
            return -ENOMEM;
        vma_insert(task, vma);
    }

    img->code_start = min_vaddr;
    img->code_end   = max_end;
    img->brk_start  = max_end;
    if (img->brk_start < USER_HEAP_START)
        img->brk_start = USER_HEAP_START;
    return 0;
}

static int elf32_load(struct task_struct *task, int fd, uint32_t file_size,
                      exec_image_t *img)
{
    Elf32_Ehdr ehdr;

    if (fs_pread(fd, &ehdr, sizeof(ehdr), 0) != (int)sizeof(ehdr))
        return -ENOEXEC;
    if (ehdr.e_type != ET_EXEC)
        return -ENOEXEC;
    if (ehdr.e_ehsize < sizeof(ehdr))
        return -ENOEXEC;
    if (ehdr.e_phentsize < sizeof(Elf32_Phdr))
        return -ENOEXEC;
    if (ehdr.e_phnum == 0 || ehdr.e_phnum > ELF_MAX_PHDRS)
        return -ENOEXEC;
    if ((uint64_t)ehdr.e_phoff + (uint64_t)ehdr.e_phnum * ehdr.e_phentsize >
        file_size)
        return -ENOEXEC;

    elf_seg_t *segs = (elf_seg_t *)kalloc(ehdr.e_phnum * sizeof(elf_seg_t));
    if (!segs)
        return -ENOMEM;
    uint32_t nsegs = 0;

    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        Elf32_Phdr ph;
        uint32_t off = (uint32_t)((uint64_t)ehdr.e_phoff +
                                  (uint64_t)i * ehdr.e_phentsize);
        if (fs_pread(fd, &ph, sizeof(ph), off) != (int)sizeof(ph)) {
            kfree(segs);
            return -ENOEXEC;
        }

        if (ph.p_type != PT_LOAD || ph.p_memsz == 0)
            continue;

        /* Range sanity: the segment must fit in user space (clear of the
         * kernel half and the reserved user-stack region) and its file
         * image must lie inside the executable. */
        if (ph.p_filesz > ph.p_memsz ||
            (uint64_t)ph.p_vaddr + ph.p_memsz >
                (uint64_t)(USER_STACK_TOP - USER_STACK_SIZE) ||
            (uint64_t)ph.p_offset + ph.p_filesz > file_size) {
            kfree(segs);
            return -ENOEXEC;
        }

        segs[nsegs].vaddr  = ph.p_vaddr;
        segs[nsegs].filesz = ph.p_filesz;
        segs[nsegs].memsz  = ph.p_memsz;
        segs[nsegs].offset = ph.p_offset;
        segs[nsegs].flags  = ph.p_flags;
        nsegs++;
    }

    int ret = elf_load_common(task, fd, segs, nsegs, img);
    kfree(segs);

    if (ret == 0)
        img->entry = ehdr.e_entry;
    return ret;
}

static int elf64_load(struct task_struct *task, int fd, uint32_t file_size,
                      exec_image_t *img)
{
    Elf64_Ehdr ehdr;

    if (fs_pread(fd, &ehdr, sizeof(ehdr), 0) != (int)sizeof(ehdr))
        return -ENOEXEC;
    if (ehdr.e_type != ET_EXEC)
        return -ENOEXEC;
    if (ehdr.e_ehsize < sizeof(ehdr))
        return -ENOEXEC;
    if (ehdr.e_phentsize < sizeof(Elf64_Phdr))
        return -ENOEXEC;
    if (ehdr.e_phnum == 0 || ehdr.e_phnum > ELF_MAX_PHDRS)
        return -ENOEXEC;
    if (ehdr.e_phoff + (uint64_t)ehdr.e_phnum * ehdr.e_phentsize > file_size)
        return -ENOEXEC;

    elf_seg_t *segs = (elf_seg_t *)kalloc(ehdr.e_phnum * sizeof(elf_seg_t));
    if (!segs)
        return -ENOMEM;
    uint32_t nsegs = 0;

    for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr ph;
        uint32_t off = (uint32_t)(ehdr.e_phoff +
                                  (uint64_t)i * ehdr.e_phentsize);
        if (fs_pread(fd, &ph, sizeof(ph), off) != (int)sizeof(ph)) {
            kfree(segs);
            return -ENOEXEC;
        }

        if (ph.p_type != PT_LOAD || ph.p_memsz == 0)
            continue;

        /* Range sanity: the segment must fit in user space (clear of the
         * kernel half and the reserved user-stack region) and its file
         * image must lie inside the executable. */
        if (ph.p_filesz > ph.p_memsz ||
            ph.p_vaddr + ph.p_memsz >
                (uint64_t)(USER_STACK_TOP - USER_STACK_SIZE) ||
            ph.p_offset + ph.p_filesz > file_size) {
            kfree(segs);
            return -ENOEXEC;
        }

        segs[nsegs].vaddr  = (uintptr_t)ph.p_vaddr;
        segs[nsegs].filesz = (uintptr_t)ph.p_filesz;
        segs[nsegs].memsz  = (uintptr_t)ph.p_memsz;
        segs[nsegs].offset = (uintptr_t)ph.p_offset;
        segs[nsegs].flags  = ph.p_flags;
        nsegs++;
    }

    int ret = elf_load_common(task, fd, segs, nsegs, img);
    kfree(segs);

    if (ret == 0)
        img->entry = (uintptr_t)ehdr.e_entry;
    return ret;
}

/**
 * elf_check_header - validate the ELF header + program-header table of the
 * file at fd.  No memory is mapped, so this can be called before the old
 * process image is torn down.
 * @return 0 on success, -ENOEXEC on any invalid/unsupported field
 */
static int elf_check_header(int fd, uint32_t file_size)
{
    uint8_t ident[EI_NIDENT];

    if (fs_pread(fd, ident, sizeof(ident), 0) != (int)sizeof(ident))
        return -ENOEXEC;
    if (ident[EI_MAG0] != ELFMAG0 || ident[EI_MAG1] != ELFMAG1 ||
        ident[EI_MAG2] != ELFMAG2 || ident[EI_MAG3] != ELFMAG3)
        return -ENOEXEC;
    if (ident[EI_DATA] != ELFDATA2LSB)
        return -ENOEXEC;
    if (ident[EI_VERSION] != EV_CURRENT)
        return -ENOEXEC;
    if (ident[EI_CLASS] != NATIVE_ELF_CLASS) {
        printk("[EXEC] wrong ELF class %u (native is %d)\n",
               ident[EI_CLASS], NATIVE_ELF_CLASS);
        return -ENOEXEC;
    }

    if (ident[EI_CLASS] == ELFCLASS32) {
        Elf32_Ehdr ehdr;
        if (fs_pread(fd, &ehdr, sizeof(ehdr), 0) != (int)sizeof(ehdr))
            return -ENOEXEC;
        if (ehdr.e_type != ET_EXEC)
            return -ENOEXEC;
        if (ehdr.e_ehsize < sizeof(ehdr))
            return -ENOEXEC;
        if (ehdr.e_phentsize < sizeof(Elf32_Phdr))
            return -ENOEXEC;
        if (ehdr.e_phnum == 0 || ehdr.e_phnum > ELF_MAX_PHDRS)
            return -ENOEXEC;
        if ((uint64_t)ehdr.e_phoff + (uint64_t)ehdr.e_phnum * ehdr.e_phentsize >
            file_size)
            return -ENOEXEC;
    } else {                             /* ELFCLASS64 (native, see above) */
        Elf64_Ehdr ehdr;
        if (fs_pread(fd, &ehdr, sizeof(ehdr), 0) != (int)sizeof(ehdr))
            return -ENOEXEC;
        if (ehdr.e_type != ET_EXEC)
            return -ENOEXEC;
        if (ehdr.e_ehsize < sizeof(ehdr))
            return -ENOEXEC;
        if (ehdr.e_phentsize < sizeof(Elf64_Phdr))
            return -ENOEXEC;
        if (ehdr.e_phnum == 0 || ehdr.e_phnum > ELF_MAX_PHDRS)
            return -ENOEXEC;
        if (ehdr.e_phoff + (uint64_t)ehdr.e_phnum * ehdr.e_phentsize >
            file_size)
            return -ENOEXEC;
    }
    return 0;
}

static int load_elf(struct task_struct *task, int fd, uint32_t file_size,
                    exec_image_t *img)
{
    int ret = elf_check_header(fd, file_size);
    if (ret < 0)
        return ret;

    /* The class has been validated as native; dispatch on it so both the
     * 32- and 64-bit loaders are compiled on every arch. */
    if (NATIVE_ELF_CLASS == ELFCLASS32)
        return elf32_load(task, fd, file_size, img);
    else
        return elf64_load(task, fd, file_size, img);
}

/* ============================================================================
 * Format detection / dispatch
 * ============================================================================ */

/**
 * exec_format_check - cheap pre-flight check that the file at fd is a
 * loadable executable.  ELF headers are validated; legacy flat images always
 * pass (they have no header).  Call this BEFORE tearing down the old process
 * image, so a malformed/foreign executable fails without destroying the
 * caller's memory.
 * @return 0 if the file looks loadable, -errno otherwise
 */
int exec_format_check(int fd, uint32_t file_size)
{
    uint8_t magic[4];
    int n = fs_pread(fd, magic, sizeof(magic), 0);
    if (n < 0)
        return -EIO;

    if (n == 4 && magic[0] == ELFMAG0 && magic[1] == ELFMAG1 &&
        magic[2] == ELFMAG2 && magic[3] == ELFMAG3)
        return elf_check_header(fd, file_size);

    return 0;                            /* legacy flat: no header to check */
}

/**
 * load_binary - load an executable image into a task's freshly-cleared user
 * address space (the caller has already freed the old image).  Maps all
 * segments/pages, creates the VMAs and fills *img.
 * @param task       Target task (page tables already initialized)
 * @param fd         Open file descriptor, positioned at offset 0
 * @param file_size  File size in bytes
 * @param img        Out: entry + memory region bounds
 * @return 0 on success, -errno (ENOEXEC/ENOMEM/EIO) on failure
 */
int load_binary(struct task_struct *task, int fd, uint32_t file_size,
                exec_image_t *img)
{
    uint8_t magic[4];
    int n = fs_pread(fd, magic, sizeof(magic), 0);
    if (n < 0)
        return -EIO;

    if (n == 4 && magic[0] == ELFMAG0 && magic[1] == ELFMAG1 &&
        magic[2] == ELFMAG2 && magic[3] == ELFMAG3) {
        printk("[EXEC] %s: ELF image\n", task->name);
        return load_elf(task, fd, file_size, img);
    }

    printk("[EXEC] %s: flat image (legacy format)\n", task->name);
    return load_flat(task, fd, file_size, img);
}
