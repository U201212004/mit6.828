/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_TRAP_H
#define JOS_KERN_TRAP_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/trap.h>
#include <inc/mmu.h>

/* The kernel's interrupt descriptor table */
extern struct Gatedesc idt[];
extern struct Pseudodesc idt_pd;

void trap_init(void);
void trap_init_percpu(void);
void print_regs(struct PushRegs *regs);
void print_trapframe(struct Trapframe *tf);
void page_fault_handler(struct Trapframe *);
void backtrace(struct Trapframe *);


void divide_entry();
void debug_entry();
void nmi_entry();
void brkpt_entry();
void oflow_entry();
void bound_entry();
void device_entry();
void illop_entry();
void dblflt_entry();
void tss_entry();
void segnp_entry();
void stack_entry();
void gpflt_entry();
void pgflt_entry();
void fperr_entry();
void align_entry();
void mchk_entry();
void simderr_entry();
void syscall_entry();

//声明中断处理函数
void timer_entry();
void kbd_entry();
void serial_entry();
void spurious_entry();
void ide_entry();
void error_entry();

#endif /* JOS_KERN_TRAP_H */
