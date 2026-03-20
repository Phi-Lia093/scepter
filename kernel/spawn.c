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
    
    printk("[SPAWN] Memory layout:\n");
    printk("[SPAWN]   Code:  0x%08x - 0x%08x\n", task->mm.code_start, task->mm.code_end);
    printk("[SPAWN]   Heap:  0x%08x - 0x%08x\n", task->mm.brk_start, task->mm.brk_end);
    printk("[SPAWN]   Stack: 0x%08x - 0x%08x\n", task->mm.stack_start, task->mm.stack_end);
    
    /* Set up initial CPU context on kernel stack */
    uint32_t *kstack = (uint32_t*)(task->kernel_esp);
    
    /* Build fake switch_to frame: [ret_addr] [regs] [eflags] */
    kstack--; *kstack = (uint32_t)&&enter_userspace;  /* Return address */
    kstack--; *kstack = 0;  /* EDI */
    kstack--; *kstack = 0;  /* ESI */
    kstack--; *kstack = 0;  /* EBP */
    kstack--; *kstack = 0;  /* ESP (ignored) */
    kstack--; *kstack = 0;  /* EBX */
    kstack--; *kstack = 0;  /* EDX */
    kstack--; *kstack = 0;  /* ECX */
    kstack--; *kstack = 0;  /* EAX */
    kstack--; *kstack = 0x202;  /* EFLAGS (IF set) */
    
    task->kernel_esp = (uint32_t)kstack;
    
    /* Set TSS.esp0 for syscalls */
    extern tss_entry_t tss;
    tss.esp0 = task->kernel_stack + 8192;
    
    /* Add to scheduler */
    task->state = TASK_READY;
    add_task(task);
    
    printk("[SPAWN] Init process created: PID %u, entry=0x%08x\n\n",
           task->pid, USER_TEXT_START);
    
    return 0;

enter_userspace:
    /* This runs when init is first scheduled */
    printk("[SPAWN] First schedule to PID %u, entering userspace\n", current->pid);
    
    /* Calculate CR3 */
    uint32_t cr3 = VIRT_TO_PHYS((uint32_t)&current->mm.pgdir[0]);
    
    /* Enter Ring 3 */
    __asm__ volatile (
        "cli\n"
        
        /* Load task's CR3 */
        "movl %0, %%eax\n"
        "movl %%eax, %%cr3\n"
        
        /* Set up user segments */
        "movw $0x23, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        
        /* Build IRET frame */
        "pushl $0x23\n"        /* SS */
        "pushl %2\n"           /* ESP (user stack top) */
        "pushfl\n"
        "popl %%eax\n"
        "orl $0x200, %%eax\n"  /* Enable interrupts */
        "pushl %%eax\n"        /* EFLAGS */
        "pushl $0x1B\n"        /* CS */
        "pushl %1\n"           /* EIP */
        
        "iret\n"
        :
        : "r"(cr3),
          "r"(USER_TEXT_START),
          "r"(USER_STACK_TOP)
        : "eax"
    );
    
    /* Should never return */
    while(1);
}