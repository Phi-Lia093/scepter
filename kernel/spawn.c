/* ============================================================================
 * Process Spawning - Create init process with proper memory management
 * ============================================================================ */

#include "kernel/exec.h"
#include "kernel/sched.h"
#include "kernel/cpu.h"
#include "mm/pgtable.h"
#include "mm/buddy.h"
#include "mm/mm.h"
#include "fs/fs.h"
#include "lib/printk.h"
#include "lib/string.h"

/* ============================================================================
 * Helper: Map user page
 * ============================================================================ */

static int map_user_page(task_struct_t *task, uint32_t vaddr, uint32_t phys, uint32_t flags)
{
    /* Get page directory index (0-767 for user space) */
    uint32_t pdi = vaddr >> 22;
    
    if (pdi >= 768) {
        printk("[SPAWN] ERROR: Trying to map kernel space 0x%08x\n", vaddr);
        return -1;
    }
    
    /* Check if page table exists */
    if (!task->mm.page_tables[pdi]) {
        /* Allocate new page table (from direct-mapped region) */
        uint32_t *pt = (uint32_t*)page_alloc(PAGE_SIZE);
        if (!pt) {
            printk("[SPAWN] Failed to allocate page table\n");
            return -1;
        }
        
        /* Clear page table */
        memset(pt, 0, PAGE_SIZE);
        
        /* Store in mm structure */
        task->mm.page_tables[pdi] = pt;
        
        /* Install in page directory */
        uint32_t pt_phys = VIRT_TO_PHYS((uint32_t)pt);
        task->mm.pgdir[pdi] = pt_phys | 0x7;  /* Present | RW | User */
        
        printk("[SPAWN] Allocated page table %u at virt=%p phys=0x%08x\n", 
               pdi, pt, pt_phys);
    }
    
    /* Get page table and index */
    uint32_t *pt = task->mm.page_tables[pdi];
    uint32_t pti = (vaddr >> 12) & 0x3FF;
    
    /* Install PTE */
    pt[pti] = phys | flags;
    
    return 0;
}

/* ============================================================================
 * Spawn Init Process
 * ============================================================================ */

int spawn_init(const char *path)
{
    extern task_struct_t *current;
    
    printk("\n[SPAWN] Creating init process from: %s\n", path);
    printk("[SPAWN] current=%p, current->pid=%u, current->next_fd=%d\n", 
           current, current->pid, current->next_fd);
    printk("[SPAWN] current->files=%p (next=%p, prev=%p)\n",
           &current->files, current->files.next, current->files.prev);
    
    /* Allocate task structure (from direct-mapped kernel memory) */
    task_struct_t *task = alloc_task();
    if (!task) {
        printk("[SPAWN] Failed to allocate task\n");
        return -1;
    }
    
    /* Set task properties */
    task->ppid = 0;
    strncpy(task->name, "init", sizeof(task->name) - 1);
    task->cwd[0] = '/';
    task->cwd[1] = '\0';
    
    printk("[SPAWN] Task PID %u allocated\n", task->pid);
    
    /* Open binary file */
    int fd = fs_open(path, O_RDONLY);
    if (fd < 0) {
        printk("[SPAWN] Failed to open file: %s\n", path);
        free_task(task);
        return -1;
    }
    
    /* Get file size */
    int file_size_tmp = fs_seek(fd, 0, SEEK_END);
    if (file_size_tmp <= 0) {
        printk("[SPAWN] Invalid file size: %d\n", file_size_tmp);
        fs_close(fd);
        free_task(task);
        return -1;
    }
    fs_seek(fd, 0, SEEK_SET);
    
    uint32_t file_size = (uint32_t)file_size_tmp;
    printk("[SPAWN] Binary size: %u bytes\n", file_size);
    
    /* Calculate pages needed */
    uint32_t num_pages = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    printk("[SPAWN] Loading %u pages into user space\n", num_pages);
    
    /* Load binary into user space (non-premapped region) */
    uint32_t bytes_loaded = 0;
    for (uint32_t i = 0; i < num_pages; i++) {
        /* Allocate physical page (from direct-mapped region) */
        void *page_virt = page_alloc(PAGE_SIZE);
        if (!page_virt) {
            printk("[SPAWN] Failed to allocate page\n");
            fs_close(fd);
            free_task(task);
            return -1;
        }
        
        /* Read binary data into page */
        uint32_t bytes_to_read = PAGE_SIZE;
        if (bytes_loaded + bytes_to_read > file_size) {
            bytes_to_read = file_size - bytes_loaded;
        }
        
        char *page_buf = (char*)page_virt;
        uint32_t offset = 0;
        while (offset < bytes_to_read) {
            int n = fs_read(fd, page_buf + offset, bytes_to_read - offset);
            if (n <= 0) break;
            offset += n;
        }
        bytes_loaded += offset;
        
        /* Zero rest of page */
        if (offset < PAGE_SIZE) {
            memset(page_buf + offset, 0, PAGE_SIZE - offset);
        }
        
        /* Map into user space (non-premapped region) */
        uint32_t vaddr = USER_TEXT_START + (i * PAGE_SIZE);
        uint32_t phys = VIRT_TO_PHYS((uint32_t)page_virt);
        
        if (map_user_page(task, vaddr, phys, 0x7) < 0) {  /* P | RW | U */
            printk("[SPAWN] Failed to map page\n");
            fs_close(fd);
            free_task(task);
            return -1;
        }
        
        printk("[SPAWN] Mapped page %u: vaddr=0x%08x phys=0x%08x (%u bytes)\n",
               i, vaddr, phys, offset);
    }
    
    fs_close(fd);
    
    /* Update memory regions */
    task->mm.code_end = USER_TEXT_START + file_size;
    task->mm.brk_start = (task->mm.code_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    task->mm.brk_end = task->mm.brk_start;
    
    /* Allocate user stack (1 page for now) */
    void *stack_page = page_alloc(PAGE_SIZE);
    if (!stack_page) {
        printk("[SPAWN] Failed to allocate stack page\n");
        free_task(task);
        return -1;
    }
    
    /* Map stack at top of user space (just below kernel at 0xC0000000) */
    uint32_t stack_vaddr = USER_STACK_TOP - PAGE_SIZE;
    uint32_t stack_phys = VIRT_TO_PHYS((uint32_t)stack_page);
    
    if (map_user_page(task, stack_vaddr, stack_phys, 0x7) < 0) {
        printk("[SPAWN] Failed to map stack page\n");
        free_task(task);
        return -1;
    }
    
    printk("[SPAWN] Mapped stack: vaddr=0x%08x phys=0x%08x\n", stack_vaddr, stack_phys);
    
    printk("[SPAWN] Memory layout:\n");
    printk("[SPAWN]   Code:  0x%08x - 0x%08x\n", task->mm.code_start, task->mm.code_end);
    printk("[SPAWN]   Heap:  0x%08x - 0x%08x\n", task->mm.brk_start, task->mm.brk_end);
    printk("[SPAWN]   Stack: 0x%08x - 0x%08x\n", task->mm.stack_start, task->mm.stack_end);
    
    /* Set up initial kernel stack for this task.
     *
     * switch_to() does: popa, popfl, ret
     * Stack must be laid out so that:
     *   ESP+0  = EDI  (popa reads first)
     *   ESP+4  = ESI
     *   ESP+8  = EBP
     *   ESP+12 = dummy (popa skips ESP)
     *   ESP+16 = EBX
     *   ESP+20 = EDX
     *   ESP+24 = ECX
     *   ESP+28 = EAX  (popa reads last, ESP now +32)
     *   ESP+32 = EFLAGS  (popfl reads, ESP now +36)
     *   ESP+36 = first_entry_trampoline  (ret jumps here, ESP +40)
     *   ESP+40 = EIP=USER_TEXT_START  \
     *   ESP+44 = CS=0x1B               |  IRET frame
     *   ESP+48 = EFLAGS=0x202          |
     *   ESP+52 = ESP=USER_STACK_TOP    |
     *   ESP+56 = SS=0x23              /
     *
     * Since kstack-- goes to lower addresses, push HIGH-address items first:
     */
    extern void first_entry_trampoline(void);
    
    uint32_t *kstack = (uint32_t *)(task->kernel_esp);
    
    /* IRET frame (highest address = pushed first) */
    kstack--; *kstack = 0x23;            /* SS            ESP+56 */
    kstack--; *kstack = USER_STACK_TOP;  /* user ESP      ESP+52 */
    kstack--; *kstack = 0x202;           /* EFLAGS (iret) ESP+48 */
    kstack--; *kstack = 0x1B;            /* CS            ESP+44 */
    kstack--; *kstack = USER_TEXT_START; /* EIP           ESP+40 */
    
    /* Return address for switch_to's ret */
    kstack--; *kstack = (uint32_t)first_entry_trampoline; /* ESP+36 */
    
    /* EFLAGS for switch_to's popfl */
    kstack--; *kstack = 0x202;           /* EFLAGS        ESP+32 */
    
    /* popa frame: push in REVERSE order (EAX first, EDI last) */
    kstack--; *kstack = 0;  /* EAX   ESP+28 */
    kstack--; *kstack = 0;  /* ECX   ESP+24 */
    kstack--; *kstack = 0;  /* EDX   ESP+20 */
    kstack--; *kstack = 0;  /* EBX   ESP+16 */
    kstack--; *kstack = 0;  /* dummy ESP+12 */
    kstack--; *kstack = 0;  /* EBP   ESP+8  */
    kstack--; *kstack = 0;  /* ESI   ESP+4  */
    kstack--; *kstack = 0;  /* EDI   ESP+0  (popa reads this first!) */
    
    task->kernel_esp = (uint32_t)kstack;
    
    /* Set TSS.esp0 to top of kernel stack (for ring3→ring0 transitions) */
    extern tss_entry_t tss;
    tss.esp0 = task->kernel_stack + 8192;
    
    /* Add to scheduler */
    task->state = TASK_READY;
    add_task(task);
    
    printk("[SPAWN] Init process created: PID %u, entry=0x%08x, CR3=0x%08x\n\n",
           task->pid, USER_TEXT_START,
           VIRT_TO_PHYS((uint32_t)&task->mm.pgdir[0]));
    
    return 0;
}
