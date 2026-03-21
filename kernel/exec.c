/* ============================================================================
 * Simple Binary Loader (No Headers)
 * ============================================================================ */

#include "kernel/exec.h"
#include "kernel/cpu.h"
#include "kernel/sched.h"
#include "mm/pgtable.h"
#include "mm/buddy.h"
#include "mm/mm.h"
#include "mm/vma.h"
#include "fs/fs.h"
#include "lib/printk.h"
#include "lib/string.h"

/* External: enter_userspace function from context.s */
extern void enter_userspace(uint32_t cr3, uint32_t entry);

/* ============================================================================
 * Simple Binary Loader
 * ============================================================================ */

int exec_flat(const char *path)
{
    printk("\n[EXEC] Loading binary: %s\n", path);
    
    /* Open the file */
    int fd = fs_open(path, 0);
    if (fd < 0) {
        printk("[EXEC] Failed to open file\n");
        return -1;
    }
    
    /* Get file size */
    int file_size_tmp = fs_seek(fd, 0, 2);  /* SEEK_END */
    if (file_size_tmp <= 0) {
        printk("[EXEC] Invalid file size: %d\n", file_size_tmp);
        fs_close(fd);
        return -1;
    }
    fs_seek(fd, 0, 0);  /* SEEK_SET */
    
    uint32_t file_size = (uint32_t)file_size_tmp;
    printk("[EXEC] File size: %u bytes\n", file_size);
    
    /* Create user page directory (kernel mapped as supervisor) */
    uint32_t *pgdir_phys = create_user_pgdir();
    if (!pgdir_phys) {
        printk("[EXEC] Failed to create page directory\n");
        fs_close(fd);
        return -1;
    }
    
    /* Get virtual address to access page directory */
    uint32_t *pgdir_virt = (uint32_t*)PHYS_TO_VIRT((uint32_t)pgdir_phys);
    
    /* Calculate number of pages needed */
    uint32_t num_pages = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    printk("[EXEC] Allocating %u pages\n", num_pages);
    
    /* Allocate and map pages, load binary */
    uint32_t bytes_loaded = 0;
    for (uint32_t i = 0; i < num_pages; i++) {
        /* Allocate physical page */
        void *page_virt = page_alloc(PAGE_SIZE);
        if (!page_virt) {
            printk("[EXEC] Failed to allocate page %u\n", i);
            fs_close(fd);
            return -1;
        }
        
        /* Convert to physical address */
        uint32_t page_phys = VIRT_TO_PHYS((uint32_t)page_virt);
        uint32_t vaddr = USER_BASE + (i * PAGE_SIZE);
        
        /* Map in user page directory with RWX permissions */
        if (map_page(pgdir_virt, vaddr, page_phys, 0x7) < 0) {  /* P|R/W|U/S */
            printk("[EXEC] Failed to map page %u\n", i);
            fs_close(fd);
            return -1;
        }
        
        /* Read binary content into page */
        uint32_t bytes_to_read = PAGE_SIZE;
        if (bytes_loaded + bytes_to_read > file_size) {
            bytes_to_read = file_size - bytes_loaded;
        }
        
        char *page_buf = (char*)page_virt;
        uint32_t offset = 0;
        while (offset < bytes_to_read) {
            int n = fs_read(fd, page_buf + offset, bytes_to_read - offset);
            if (n <= 0) {
                printk("[EXEC] Read error at offset %u\n", bytes_loaded + offset);
                fs_close(fd);
                return -1;
            }
            offset += n;
        }
        bytes_loaded += offset;
        
        /* Zero remaining bytes in last page */
        if (offset < PAGE_SIZE) {
            memset(page_buf + offset, 0, PAGE_SIZE - offset);
        }
        
        printk("[EXEC] Page %u: virt=0x%08x phys=0x%08x (%u bytes)\n",
               i, vaddr, page_phys, offset);
    }
    
    fs_close(fd);
    
    printk("[EXEC] Loaded %u bytes successfully\n", bytes_loaded);
    printk("[EXEC] Entry point: 0x%08x (offset 0)\n", USER_BASE);
    
    /* Allocate kernel stack for this process (8KB) */
    void *kernel_stack = page_alloc(8192);
    if (!kernel_stack) {
        printk("[EXEC] Failed to allocate kernel stack\n");
        return -1;
    }
    
    /* Set TSS.esp0 to top of kernel stack
     * This is where CPU will switch when interrupt occurs in Ring 3 */
    extern tss_entry_t tss;
    tss.esp0 = (uint32_t)kernel_stack + 8192;  /* Top of stack (grows down) */
    
    printk("[EXEC] Kernel stack: 0x%08x-0x%08x (TSS.esp0 = 0x%08x)\n",
           (uint32_t)kernel_stack, (uint32_t)kernel_stack + 8192, tss.esp0);
    
    /* Create VMAs for the loaded process */
    task_struct_t *task = current;
    
    /* Create code VMA (read + execute) */
    vma_t *code_vma = vma_create(USER_BASE, USER_BASE + file_size,
                                  VM_READ | VM_EXEC, VMA_CODE);
    if (code_vma) {
        vma_insert(task, code_vma);
        printk("[EXEC] Created code VMA: 0x%08x-0x%08x\n",
               code_vma->vm_start, code_vma->vm_end);
    }
    
    /* Create stack VMA (read + write + grows down) */
    vma_t *stack_vma = vma_create(0xBFFF0000U, 0xC0000000U,
                                   VM_READ | VM_WRITE | VM_GROWSDOWN, VMA_STACK);
    if (stack_vma) {
        vma_insert(task, stack_vma);
        printk("[EXEC] Created stack VMA: 0x%08x-0x%08x (growsdown)\n",
               stack_vma->vm_start, stack_vma->vm_end);
    }
    
    /* Update task memory regions */
    task->mm.code_start = USER_BASE;
    task->mm.code_end = USER_BASE + file_size;
    task->mm.stack_start = 0xBFFF0000U;
    task->mm.stack_end = 0xC0000000U;
    
    printk("[EXEC] VMA setup complete, dumping VMAs:\n");
    vma_dump(task);
    
    printk("[EXEC] Switching to user CR3: 0x%08x\n", (uint32_t)pgdir_phys);
    printk("[EXEC] *** Entering userspace ***\n\n");
    
    /* Jump to offset 0 (USER_BASE) */
    enter_userspace((uint32_t)pgdir_phys, USER_BASE);
    
    /* Should never reach here */
    printk("[EXEC] ERROR: Returned from userspace!\n");
    return -1;
}