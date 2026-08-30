/* ============================================================================
 * Process Spawning - Create init process with proper memory management
 * ============================================================================ */

#include "kernel/exec.h"
#include "kernel/sched.h"
#include "arch/cpu.h"
#include "kernel/process.h"
#include "arch/paging.h"
#include "mm/buddy.h"
#include "mm/mm.h"
#include "mm/vma.h"
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
    
    return arch_mm_map_user(task, vaddr, phys, flags);
}

/* ============================================================================
 * Spawn Init Process
 * ============================================================================ */

int spawn_init(const char *path)
{
    
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
    task->root[0] = '/';
    task->root[1] = '\0';
    
    /* Init is root and the leader of its own session + process group. */
    task->uid  = task->euid = 0;
    task->suid = 0;
    task->gid  = task->egid = 0;
    task->sgid = 0;
    task->fsuid = 0;
    task->fsgid = 0;
    task->ngroups = 0;
    task->personality = 0;
    task->cleartid = 0;
    task->pgid = task->pid;
    task->sid  = task->pid;
    task->umask = 0;
    task->itimer_remaining = 0;
    task->itimer_interval  = 0;
    task->uticks = 0;
    task->sticks = 0;

    /* Default resource limits. */
    for (int i = 0; i < RLIM_NLIMITS; i++) {
        task->rlimit_cur[i] = RLIM_INFINITY;
        task->rlimit_max[i] = RLIM_INFINITY;
    }
    task->rlimit_cur[RLIMIT_STACK]  = RLIM_DEFAULT_STACK;
    task->rlimit_max[RLIMIT_STACK]  = RLIM_DEFAULT_STACK;
    task->rlimit_cur[RLIMIT_AS]     = RLIM_DEFAULT_AS;
    task->rlimit_max[RLIMIT_AS]     = RLIM_DEFAULT_AS;
    task->rlimit_cur[RLIMIT_NOFILE] = RLIM_DEFAULT_NOFILE;
    task->rlimit_max[RLIMIT_NOFILE] = RLIM_DEFAULT_NOFILE;
    task->rlimit_cur[RLIMIT_NPROC]  = RLIM_DEFAULT_NPROC;
    task->rlimit_max[RLIMIT_NPROC]  = RLIM_DEFAULT_NPROC;
    task->rlimit_cur[RLIMIT_MEMLOCK] = 0;
    task->rlimit_max[RLIMIT_MEMLOCK] = 0;
    
    /* Open binary file */
    int fd = fs_open(path, O_RDONLY, 0);
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
    
    /* Pre-flight format check (ELF or legacy flat). */
    if (exec_format_check(fd, file_size) < 0) {
        printk("[SPAWN] Not a loadable executable: %s\n", path);
        fs_close(fd);
        free_task(task);
        return -1;
    }
    
    /* Load the new image (ELF or legacy flat).  The loader maps all
     * pages/segments and creates the code VMAs. */
    exec_image_t img;
    if (load_binary(task, fd, file_size, &img) < 0) {
        printk("[SPAWN] Failed to load image: %s\n", path);
        fs_close(fd);
        free_task(task);
        return -1;
    }
    fs_close(fd);
    
    /* Update memory regions */
    task->mm.code_start = img.code_start;
    task->mm.code_end   = img.code_end;
    task->mm.brk_start  = img.brk_start;
    task->mm.brk_end    = img.brk_start;
    
    /* Allocate user stack (2 pages: [0xBFFFE000, 0xC0000000)) */
    void *stack_pages = page_alloc(2 * PAGE_SIZE);
    if (!stack_pages) {
        printk("[SPAWN] Failed to allocate stack pages\n");
        free_task(task);
        return -1;
    }
    
    /* Map stack at top of user space (just below kernel at 0xC0000000) */
    uint32_t stack_vaddr = USER_STACK_TOP - 2 * PAGE_SIZE;
    uint32_t stack_phys = VIRT_TO_PHYS((uintptr_t)stack_pages);
    
    if (map_user_page(task, stack_vaddr, stack_phys, 0x7) < 0 ||
        map_user_page(task, stack_vaddr + PAGE_SIZE,
                      stack_phys + PAGE_SIZE, 0x7) < 0) {
        printk("[SPAWN] Failed to map stack pages\n");
        free_task(task);
        return -1;
    }
    
    /* Build the argc/argv/envp block for init: argv={"init"},
     * envp={"PATH=/bin","HOME=/",NULL}. */
    exec_strings_t argv = { 0 };
    exec_strings_t envp = { 0 };
    strcpy(argv.data, "init");
    argv.ptrs[0] = argv.data;
    argv.count = 1;
    argv.data_len = 5;
    
    strcpy(envp.data, "PATH=/bin");
    envp.ptrs[0] = envp.data;
    envp.count = 1;
    envp.data_len = 10;
    strcpy(envp.data + envp.data_len, "HOME=/");
    envp.ptrs[1] = envp.data + envp.data_len;
    envp.count = 2;
    envp.data_len += 7;
    
    uintptr_t user_esp = USER_STACK_TOP - 4;
    if (setup_initial_stack(stack_pages, stack_vaddr, &argv, &envp,
                            &user_esp) < 0) {
        printk("[SPAWN] Failed to build initial stack\n");
        free_task(task);
        return -1;
    }
    
    /* Create VMAs for the loaded process.
     * The code VMAs (per segment / single flat region) were created by
     * load_binary(); here we only add the stack VMA. */
    
    /* Create stack VMA (read + write + grows down) */
    vma_t *stack_vma = vma_create(task->mm.stack_start, task->mm.stack_end,
                                   VM_READ | VM_WRITE | VM_GROWSDOWN, VMA_STACK);
    if (stack_vma) {
        vma_insert(task, stack_vma);
    }
    
    /* Set up initial kernel stack for this task.
     *
     * switch_to() does: popa, popfl, ret  (see arch/i386/context.s and
     * arch/i386/context.c).  The arch builds the popa/popfl frame + a
     * ring-3 IRET frame so first_entry_trampoline() can iret to init.
     */
    arch_setup_first_stack(task, img.entry, user_esp, NULL);
    
    /* Set the ring-0 stack for ring3→ring0 transitions (TSS.esp0) */
    arch_set_kernel_stack(task->kernel_stack + KERNEL_STACK_SIZE);
    
    /* Add to scheduler */
    task->state = TASK_READY;
    add_task(task);
    
    printk("[SPAWN] Init process created: PID %u, entry=0x%08x, CR3=0x%08x\n\n",
           task->pid, (uint32_t)img.entry, (uint32_t)arch_mm_get_pgd_phys(task));
    
    return 0;
}
