/* ============================================================================
 * C Runtime Startup (x86_64)
 *
 * The kernel sets RSP to point at the initial stack block it built in
 * setup_initial_stack():
 *   [rsp+0]             argc
 *   [rsp+8]             argv[0..argc-1], NULL
 *   [rsp+8+8*(argc+1)]  envp[0..envc-1], NULL
 * The actual strings are packed above the pointer block (higher addresses).
 * ============================================================================ */

.section .text
.global _start

_start:
    /* Zero the frame pointer (marks the outermost stack frame). */
    xorq %rbp, %rbp

    /* Parse the initial stack: rdi=argc, rsi=argv, rdx=envp */
    movq  (%rsp), %rdi            /* rdi = argc          */
    leaq  8(%rsp), %rsi           /* rsi = argv          */
    leaq  16(%rsp,%rdi,8), %rdx   /* rdx = envp          */

    /* Record the environment for getenv()/setenv() */
    movq  %rdx, environ(%rip)

    /* 16-byte align the stack (SysV ABI) */
    andq  $-16, %rsp

    /* call main(int argc, char *argv[], char *envp[]) */
    call  main

    /* main() returned: exit with its return value */
    movq  %rax, %rdi              /* exit status        */
    movl  $1, %eax                /* SYS_EXIT           */
    syscall

    /* Should never reach here */
.hang:
    hlt
    jmp .hang
