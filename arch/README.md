# Architecture Layer

`arch/` holds everything that is specific to a CPU architecture.  The
rest of the kernel (kernel/, mm/, driver/, fs/, net/, lib/) only talks to
the architecture through the arch-neutral API described below.

## Layout

    arch/
    ├── Makefile            # dispatches to arch/$(ARCH)
    ├── README.md
    ├── i386/               # 32-bit x86 (booted by GRUB multiboot from root.img)
    │   ├── arch.mk         # arch-specific toolchain flags (ARCH_CFLAGS/LDFLAGS)
    │   ├── Makefile        # builds arch/i386 objects into build-i386/
    │   ├── linker.ld       # arch-specific link script (higher-half 0xC0000000)
    │   ├── include/arch/   # the arch-abstract API surface, included as <arch/*.h>
    │   │   ├── io.h        #   port I/O: inb/outb/inw/outw/inl/outl/io_wait
    │   │   ├── cpu.h       #   cli/sti/irq_save/irq_restore/hlt,
    │   │   │               #   arch_cpu_init(), arch_set_kernel_stack(),
    │   │   │               #   switch_to(), enter_userspace(), first_entry_trampoline()
    │   │   ├── irq.h       #   interrupt_init/eoi/enable/disable, irq_init/register/dispatch
    │   │   ├── paging.h    #   KERNEL_VMA, PHYS_TO_VIRT/VIRT_TO_PHYS, map_page/unmap/
    │   │   │               #   flush_tlb/get_pte/create_user_pgdir, arch_kernel_pgdir()
    │   │   ├── mem.h       #   arch_mem_detect_kb()
    │   │   ├── timer.h     #   arch_timer_init(hz), arch_timer_get_ticks(), timer_wq
    │   │   ├── abi.h       #   registers_t (trap frame), user-space memory layout
    │   │   └── uaccess.h   #   valid_user_pointer/copy_from_user/copy_to_user
    │   ├── boot.s          #   multiboot header + early paging + _start
    │   ├── context.c       #   switch_to / enter_userspace / first_entry_trampoline
    │   ├── isr.s           #   exception / IRQ / syscall stubs
    │   ├── cpu.c           #   GDT/TSS/IDT setup, kernel page table
    │   ├── irq.c           #   interrupt controller manager (APIC/PIC)
    │   ├── pic.c lapic.c ioapic.c
    │   ├── paging.c        #   page-table primitives
    │   ├── mem.c           #   physical memory detection (CMOS)
    │   ├── timer.c         #   system timer (PIT) + tick handler
    │   ├── trap.c          #   exception entry: panic_isr, register dump, ISR14 glue
    │   ├── stackframe.c    #   arch_setup_first_stack() (spawn/fork initial frames)
    │   └── uaccess.c       #   user-space pointer validation / copy helpers
    └── x86_64/             # 64-bit x86 (booted by EFI GRUB multiboot2 from efi.img)
        ├── arch.mk         # -m64 -mcmodel=kernel -mno-red-zone ...
        ├── Makefile        # builds arch/x86_64 objects into build-x86_64/
        ├── linker.ld       # ELF64 higher-half 0xFFFFFFFF80000000, multiboot2 header
        ├── include/arch/   # same API surface as i386 (io/cpu/irq/paging/mem/timer/abi/uaccess)
        ├── boot.s          # multiboot2 header + 32-bit long-mode trampoline
        ├── context.s       # switch_to / enter_userspace / first_entry_trampoline
        ├── isr.s           # exception / IRQ / syscall + sysret stubs
        ├── cpu.c           # 64-bit GDT/TSS/IDT + syscall/sysret MSRs
        ├── irq.c pic.c lapic.c ioapic.c
        ├── paging.c        # 4-level (PML4/PDPT/PD/PT) paging
        ├── mem.c           # physical memory detection (multiboot2 memory map)
        ├── timer.c         # PIT + tick handler
        ├── trap.c          # exception entry + ISR14 glue
        ├── stackframe.c    # arch_setup_first_stack() (64-bit frames)
        └── uaccess.c       # page-table-walk uaccess

## The API contract

The *active arch* provides the `arch/*.h` headers (resolved at build time
via `-I arch/$(ARCH)/include`).  Generic code includes `<arch/io.h>`,
`<arch/cpu.h>`, `<arch/irq.h>`, `<arch/paging.h>`, `<arch/mem.h>`,
`<arch/timer.h>`, `<arch/abi.h>`, `<arch/uaccess.h>` and must never
include arch-private headers (gdt.h, msr.h, pic.h, lapic.h, ioapic.h) or
depend on x86-specific register layouts outside those headers.

## Building

    make ARCH=i386            # default: 32-bit kernel → build-i386/kernel.elf
    make ARCH=x86_64          # 64-bit kernel   → build-x86_64/kernel.elf
    make -C crt ARCH=i386     # 32-bit userspace → crt/build/root
    make -C crt ARCH=x86_64   # 64-bit userspace → crt/build64/root

The x86_64 userspace ABI keeps the 32-bit (x32-style) struct layouts
(timeval/timespec/stat/fb_fix_screeninfo are 32-bit) so the same
kernel↔libc byte layout works on both arches.

## Booting x86_64 (EFI)

i386 boots the classic way: GRUB multiboot from `root.img` on the first
IDE drive.  x86_64 has no VGA text mode under EFI, so it boots via
**EFI GRUB with a multiboot2 kernel**:

1. Build an EFI GRUB image (grub-mkstandalone, x86_64-efi, multiboot2
   module) and install it on an ESP: `efi.img` (GRUB + kernel64.elf).
2. `root64.img` is an ext2 disk with the 64-bit userspace (`crt/build64/root`).
3. `make ARCH=x86_64 run` boots QEMU with OVMF + both disks.

The kernel's serial console carries all early boot diagnostics (the
multiboot2 header has a serial-info tag); a framebuffer console is
available later via the VBE/GOP path.

## Adding a new architecture

To support a new architecture `foo`:

1. Create `arch/foo/` with the same `arch.mk`, `Makefile`, `linker.ld`,
   `boot.*`, `context.*`, `isr.*`, and source layout as `arch/i386/`.
2. Provide `arch/foo/include/{io,cpu,irq,paging,mem,timer,abi,uaccess}.h`
   implementing the same API contract.
3. Build with `make ARCH=foo`.
