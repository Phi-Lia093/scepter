# Architecture Layer

`arch/` holds everything that is specific to a CPU architecture.  The
rest of the kernel (kernel/, mm/, driver/, fs/, net/, lib/) only talks to
the architecture through the arch-neutral API described below.

## Layout

    arch/
    ├── Makefile            # dispatches to arch/$(ARCH)
    ├── README.md
    └── i386/
        ├── arch.mk         # arch-specific toolchain flags (ARCH_CFLAGS/LDFLAGS)
        ├── Makefile        # builds arch/i386 objects into build/
        ├── linker.ld       # arch-specific link script (higher-half layout)
        ├── include/        # the arch-abstract API surface, included as <arch/*.h>
        │   ├── io.h        #   port I/O: inb/outb/inw/outw/inl/outl/io_wait
        │   ├── cpu.h       #   cli/sti/irq_save/irq_restore/hlt,
        │   │               #   arch_cpu_init(), arch_set_kernel_stack(),
        │   │               #   switch_to(), enter_userspace(), first_entry_trampoline()
        │   ├── irq.h       #   interrupt_init/eoi/enable/disable, irq_init/register/dispatch
        │   ├── paging.h    #   KERNEL_VMA, PHYS_TO_VIRT/VIRT_TO_PHYS, map_page/unmap/
        │   │               #   flush_tlb/get_pte/create_user_pgdir, arch_kernel_pgdir()
        │   ├── mem.h       #   arch_mem_detect_kb()
        │   ├── timer.h     #   arch_timer_init(hz), arch_timer_get_ticks(), timer_wq
        │   ├── abi.h       #   registers_t (trap frame), user-space memory layout
        │   └── uaccess.h   #   valid_user_pointer/copy_from_user/copy_to_user
        ├── boot.s          #   multiboot header + early paging + _start
        ├── context.s       #   switch_to / enter_userspace / first_entry_trampoline
        ├── isr.s           #   exception / IRQ / syscall stubs
        ├── cpu.c           #   GDT/TSS/IDT setup, kernel page table
        ├── irq.c           #   interrupt controller manager (APIC/PIC)
        ├── pic.c lapic.c ioapic.c
        ├── paging.c        #   page-table primitives
        ├── mem.c           #   physical memory detection
        ├── timer.c         #   system timer (PIT) + tick handler
        ├── trap.c          #   exception entry: panic_isr, register dump, ISR14 glue
        └── uaccess.c       #   user-space pointer validation / copy helpers

## The API contract

The *active arch* provides the `arch/*.h` headers (resolved at build time
via `-I arch/$(ARCH)/include`).  Generic code includes `<arch/io.h>`,
`<arch/cpu.h>`, `<arch/irq.h>`, `<arch/paging.h>`, `<arch/mem.h>`,
`<arch/timer.h>`, `<arch/abi.h>`, `<arch/uaccess.h>` and must never
include arch-private headers (gdt.h, msr.h, pic.h, lapic.h, ioapic.h) or
depend on x86-specific register layouts outside those headers.

## Adding a new architecture

To support a new architecture `foo`:

1. Create `arch/foo/` with the same `arch.mk`, `Makefile`, `linker.ld`,
   `boot.*`, `context.*`, `isr.*`, and source layout as `arch/i386/`.
2. Provide `arch/foo/include/{io,cpu,irq,paging,mem,timer,abi,uaccess}.h`
   implementing the same API contract.
3. Build with `make ARCH=foo`.
