/* ============================================================================
 * Process Management - exit, fork, exec, wait
 * ============================================================================ */

#include "kernel/process.h"
#include "kernel/syscall.h"
#include "kernel/sched.h"
#include "kernel/cpu.h"
#include "mm/mm.h"
#include "mm/buddy.h"
#include "mm/slab.h"
#include "mm/pgtable.h"
#include "mm/vma.h"
#include "fs/fs.h"
#include "lib/printk.h"
#include "lib/string.h"

/* ============================================================================
 * Process Termination (exit)
 * ============================================================================ */

/**
 * sys_exit - Terminate current process
 * @param status Exit status code
 * 
 * This function never returns. The process transitions to ZOMBIE state
 * and waits to be reaped by its parent via wait().
 */
void sys_exit(int status)
{
    task_struct_t *task = current;
    
    printk("[PROCESS] PID %u exiting with status %d\n", task->pid, status);
    
    /* Store exit code */
    task->exit_code = status;
    
    /* Close all open file descriptors */
    list_head_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &task->files) {
        file_handle_t *file = list_entry(pos, file_handle_t, node);
        fs_close(file->fd);
        list_del(&file->node);
        kfree(file);
    }
    
    /* Free user memory (pages, page tables, VMAs) */
    /* Note: We keep the kernel stack and task_struct for parent to reap */
    
    /* Switch to kernel CR3 immediately - we'll map page tables as needed */
    extern uint32_t kernel_page_table;
    __asm__ volatile("mov %0, %%cr3" : : "r"(kernel_page_table));
    
    /* Free all VMAs and their pages */
    list_for_each_safe(pos, tmp, &task->mm.vma_list) {
        vma_t *vma = list_entry(pos, vma_t, list);
        
        /* Free all pages in this VMA by directly accessing physical addresses */
        for (uint32_t addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
            uint32_t pdi = addr >> 22;
            uint32_t pti = (addr >> 12) & 0x3FF;
            
            /* Page tables are stored as kernel virtual addresses (direct-mapped) */
            uint32_t *pt = task->mm.page_tables[pdi];
            if (pt) {
                uint32_t pte = pt[pti];
                
                if (pte & 0x1) {  /* Present */
                    uint32_t phys = pte & ~0xFFF;
                    void *virt = (void *)PHYS_TO_VIRT(phys);
                    page_free(virt);
                }
            }
        }
        
        vma_destroy(vma);  /* vma_destroy handles list_del internally */
    }
    
    /* Free page tables (these are already kernel virtual addresses) */
    for (int i = 0; i < 768; i++) {
        if (task->mm.page_tables[i]) {
            page_free(task->mm.page_tables[i]);
            task->mm.page_tables[i] = NULL;
        }
    }
    
    /* Reparent children to init (PID 1) or auto-reap them */
    if (!list_empty(&task->children)) {
        
        list_for_each_safe(pos, tmp, &task->children) {
            task_struct_t *child = list_entry(pos, task_struct_t, sibling);
            
            if (child->state == TASK_ZOMBIE) {
                /* Auto-reap zombie children */
                printk("[PROCESS] Auto-reaping zombie child PID %u\n", child->pid);
                list_del(&child->sibling);
                remove_task(child);
                free_task(child);
            } else {
                /* Reparent to init (if we have one) */
                list_del(&child->sibling);
                child->ppid = 1;  /* Reparent to init */
                /* Note: In full implementation, add to init's children list */
            }
        }
    }
    
    /* Transition to ZOMBIE state */
    task->state = TASK_ZOMBIE;
    
    /* Wake up parent if it's waiting */
    /* Note: Will implement wait queue later */
    
    printk("[PROCESS] PID %u is now zombie, scheduling next task\n", task->pid);
    
    /* Schedule next task (this never returns) */
    schedule();
    
    /* Should never reach here */
    while(1);
}

/* ============================================================================
 * Process Duplication (fork)
 * ============================================================================ */

/**
 * sys_fork - Create a copy of the current process
 * @param regs CPU register state from syscall entry
 * @return Child PID to parent, 0 to child, -1 on error
 */
int sys_fork(registers_t *regs)
{
    task_struct_t *parent = current;
    
    /* Allocate new task structure */
    task_struct_t *child = alloc_task();
    if (!child) {
        return -1;
    }
    
    /* Copy basic fields */
    child->ppid = parent->pid;
    strncpy(child->name, parent->name, sizeof(child->name));
    strncpy(child->cwd, parent->cwd, sizeof(child->cwd));
    child->next_fd = parent->next_fd;
    
    /* Add child to parent's children list */
    list_add_tail(&child->sibling, &parent->children);
    
    /* Duplicate file descriptors */
    list_head_t *pos;
    list_for_each(pos, &parent->files) {
        file_handle_t *pf = list_entry(pos, file_handle_t, node);
        
        /* Allocate new file handle for child */
        file_handle_t *cf = (file_handle_t *)kalloc(sizeof(file_handle_t));
        if (!cf) {
            printk("[PROCESS] Fork failed: could not duplicate file descriptor\n");
            free_task(child);
            return -1;
        }
        
        /* Copy file handle */
        memcpy(cf, pf, sizeof(file_handle_t));
        INIT_LIST_HEAD(&cf->node);
        
        /* Add to child's file list */
        list_add_tail(&cf->node, &child->files);
    }
    
    /* Duplicate memory: VMAs and page tables */
    list_for_each(pos, &parent->mm.vma_list) {
        vma_t *pvma = list_entry(pos, vma_t, list);
        
        /* Create matching VMA in child */
        vma_t *cvma = vma_create(pvma->vm_start, pvma->vm_end, 
                                 pvma->vm_flags, pvma->vm_type);
        if (!cvma) {
            printk("[PROCESS] Fork failed: could not create child VMA\n");
            free_task(child);
            return -1;
        }
        
        vma_insert(child, cvma);
        
        /* Copy all pages in this VMA */
        for (uint32_t addr = pvma->vm_start; addr < pvma->vm_end; addr += PAGE_SIZE) {
            uint32_t pdi = addr >> 22;
            uint32_t pti = (addr >> 12) & 0x3FF;
            
            /* Check if parent has this page mapped */
            if (!parent->mm.page_tables[pdi]) {
                continue;
            }
            
            uint32_t *parent_pt = parent->mm.page_tables[pdi];
            uint32_t parent_pte = parent_pt[pti];
            
            if (!(parent_pte & 0x1)) {  /* Not present */
                continue;
            }
            
            /* Allocate page table for child if needed */
            if (!child->mm.page_tables[pdi]) {
                uint32_t *pt = (uint32_t *)page_alloc(PAGE_SIZE);
                if (!pt) {
                    printk("[PROCESS] Fork failed: could not allocate page table\n");
                    free_task(child);
                    return -1;
                }
                memset(pt, 0, PAGE_SIZE);
                child->mm.page_tables[pdi] = pt;
                
                uint32_t pt_phys = VIRT_TO_PHYS((uint32_t)pt);
                child->mm.pgdir[pdi] = pt_phys | 0x7;  /* P | RW | U */
            }
            
            /* Allocate new physical page for child */
            void *child_page = page_alloc(PAGE_SIZE);
            if (!child_page) {
                printk("[PROCESS] Fork failed: could not allocate page\n");
                free_task(child);
                return -1;
            }
            
            /* Copy page content from parent */
            uint32_t parent_phys = parent_pte & ~0xFFF;
            void *parent_page = (void *)PHYS_TO_VIRT(parent_phys);
            memcpy(child_page, parent_page, PAGE_SIZE);
            
            /* Install PTE in child */
            uint32_t *child_pt = child->mm.page_tables[pdi];
            uint32_t child_phys = VIRT_TO_PHYS((uint32_t)child_page);
            uint32_t flags = parent_pte & 0xFFF;  /* Copy flags */
            child_pt[pti] = child_phys | flags;
        }
    }
    
    /* Copy memory region info */
    child->mm.code_start = parent->mm.code_start;
    child->mm.code_end = parent->mm.code_end;
    child->mm.brk_start = parent->mm.brk_start;
    child->mm.brk_end = parent->mm.brk_end;
    child->mm.stack_start = parent->mm.stack_start;
    child->mm.stack_end = parent->mm.stack_end;
    child->mm.mmap_base = parent->mm.mmap_base;
    child->mm.mmap_end = parent->mm.mmap_end;
    
    /* Set up child's kernel stack for first execution
     * When syscall returns, child should get EAX=0 (return value for child)
     * 
     * The parent is currently in syscall context. After fork returns,
     * both parent and child will return to user mode via iret.
     * 
     * We need to set up child's kernel stack to mirror parent's stack
     * so it can also iret back to user mode (to the instruction after int 0x80).
     */
    
    extern void first_entry_trampoline(void);
    
    /* Get parent's current kernel ESP - it has the syscall frame
     * When int 0x80 was invoked, the following was pushed:
     * 1. CPU pushed SS, ESP, EFLAGS, CS, EIP (if from ring 3)
     * 2. isr128 pushed: GS, FS, ES, DS, CR3
     * 3. isr128 pushed: pusha (EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX)
     * 4. isr128 pushed: 6 syscall arguments
     * 
     * We need to find the user's EIP and ESP from the IRET frame */
    
    /* For now, use a simpler approach: parent will continue execution,
     * and child will be set up to return to the same point with EAX=0.
     * 
     * The challenge is we don't have the parent's user EIP/ESP easily accessible.
     * WORKAROUND: Copy parent's entire kernel stack frame and modify EAX */
    
    uint32_t *kstack = (uint32_t *)(child->kernel_esp);
    
    /* Build child's kernel stack for switch_to + first_entry_trampoline
     * Stack grows downward, so we push in REVERSE order:
     * 
     * High address (top of stack)
     *   [IRET frame for first_entry_trampoline to use]
     *   [Return address = first_entry_trampoline]
     *   [EFLAGS for switch_to popfl]
     *   [POPA frame for switch_to]
     * Low address (ESP points here)
     *
     * switch_to will: popa, popfl, ret (to first_entry_trampoline)
     * first_entry_trampoline will: set segments, iret (to user mode)
     */
    
    /* IRET frame (top of stack, high addresses) */
    kstack--; *kstack = regs->ss;          /* SS */
    kstack--; *kstack = regs->user_esp;    /* User ESP */
    kstack--; *kstack = regs->eflags;      /* EFLAGS */
    kstack--; *kstack = regs->cs;          /* CS */
    kstack--; *kstack = regs->eip;         /* EIP */
    
    /* Return address for switch_to's ret instruction */
    kstack--; *kstack = (uint32_t)first_entry_trampoline;
    
    /* EFLAGS for switch_to's popfl (IF=0, will be enabled by iret) */
    kstack--; *kstack = 0x002;
    
    /* POPA frame for switch_to (pushed in reverse: EDI first, EAX last) */
    kstack--; *kstack = regs->edi;         /* EDI */
    kstack--; *kstack = regs->esi;         /* ESI */
    kstack--; *kstack = regs->ebp;         /* EBP */
    kstack--; *kstack = regs->esp_dummy;   /* dummy ESP (ignored by popa) */
    kstack--; *kstack = regs->ebx;         /* EBX */
    kstack--; *kstack = regs->edx;         /* EDX */
    kstack--; *kstack = regs->ecx;         /* ECX */
    kstack--; *kstack = 0;                 /* EAX = 0 (child's return value) */
    
    /* Update child's ESP to point to start of POPA frame */
    child->kernel_esp = (uint32_t)kstack;
    
    /* Add child to scheduler */
    child->state = TASK_READY;
    add_task(child);
    
    /* Return child PID to parent */
    return (int)child->pid;
}

/* ============================================================================
 * Wait for Child (wait)
 * ============================================================================ */

/**
 * sys_wait - Wait for any child process to exit
 * @param status_ptr User pointer to store exit status (can be NULL)
 * @return Child PID on success, -1 on error
 */
int sys_wait(int *status_ptr)
{
    task_struct_t *parent = current;
    
    /* Check if we have any children */
    if (list_empty(&parent->children)) {
        printk("[PROCESS] Wait: PID %u has no children\n", parent->pid);
        return -1;
    }
    
    /* Look for zombie children */
    while (1) {
        list_head_t *pos, *tmp;
        list_for_each_safe(pos, tmp, &parent->children) {
            task_struct_t *child = list_entry(pos, task_struct_t, sibling);
            
            if (child->state == TASK_ZOMBIE) {
                /* Found a zombie child! */
                int pid = child->pid;
                int status = child->exit_code;
                
                printk("[PROCESS] Wait: PID %u reaping zombie child PID %u (status=%d)\n",
                       parent->pid, pid, status);
                
                /* Return status to user if requested */
                if (status_ptr) {
                    /* TODO: Use copy_to_user for safety */
                    *status_ptr = status;
                }
                
                /* Remove from children list */
                list_del(&child->sibling);
                
                /* Remove from scheduler and free task */
                remove_task(child);
                free_task(child);
                
                return pid;
            }
        }
        
        /* No zombie children yet - block and wait */
        printk("[PROCESS] Wait: PID %u blocking (no zombie children yet)\n", 
               parent->pid);
        
        parent->state = TASK_BLOCKED;
        schedule();  /* Sleep until a child exits */
        
        /* When we wake up, loop again to check for zombies */
    }
}