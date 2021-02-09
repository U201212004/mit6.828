// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>
#include <inc/memlayout.h>
// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
    if((err & FEC_WR) == 0 || (uvpt[PGNUM(addr)] & PTE_COW) == 0)
        panic("pgfault:it is not writable or access non-copy-on-write page\n ");
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
    envid_t envid = sys_getenvid();
    addr = ROUNDDOWN(addr, PGSIZE);
    if((r = sys_page_alloc(envid, (void *)PFTEMP, PTE_P | PTE_W | PTE_U)) < 0)
        panic("pgfault:page allocation failed %e\n", r);

    memmove(PFTEMP, addr, PGSIZE);
    if((r = sys_page_map(envid, PFTEMP, envid, addr, PTE_P|PTE_W|PTE_U)) < 0)
        panic("pgfault：page map failed %e\n", r);

    if((r = sys_page_unmap(envid, PFTEMP)) < 0)
        panic("pgfault:page unmap failed %e\n", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	void *addr;
    pte_t pte;
    int perm;

    addr = (void *)((uint32_t)pn * PGSIZE);
    pte = uvpt[pn];
    perm = PTE_P | PTE_U;
    if((pte & PTE_W) || (pte & PTE_COW))
        perm |= PTE_COW;

    if((r = sys_page_map(thisenv->env_id, addr, envid, addr, perm)) < 0){
        panic("duppage:page map failed:%e\n", r);
        return r;
    }

    if(perm & PTE_COW){
        if((r = sys_page_map(thisenv->env_id, addr, thisenv->env_id, addr, perm)) < 0){
            panic("duppage:page remap failed %e\n", r);
            return r;
        }
    }
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	extern void _pgfault_upcall(void);
    //设置缺页处理函数
    set_pgfault_handler(pgfault);
    envid_t envid;
    int r, pn;

    if((envid = sys_exofork()) < 0)
        panic("sys_exofork failed: %e\n", envid);

    if(envid == 0){
        thisenv = &envs[ENVX(sys_getenvid())];
        return 0;
    }

    //调用duppage复制映射UTEXT-USTACKTOP
    for(pn = PGNUM(UTEXT); pn < PGNUM(USTACKTOP); pn++){
        if((uvpd[pn>>10] & PTE_P) && (uvpt[pn] & PTE_P)){
            if((r = duppage(envid, pn)) < 0)
                return r;
        }
    }

    //分配一页用于子进程用户异常栈
    if((r = sys_page_alloc(envid, (void *)(UXSTACKTOP-PGSIZE), PTE_P|PTE_U|PTE_W)) < 0)
        return r;

    //set fault entrypoint
    if((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
        return r;

    if((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
        panic("sys_env_set_status failed:%e", r);
    return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
