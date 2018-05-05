// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

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
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.

	if((err & FEC_WR) == 0)
		panic("pgfault: access is not a write\n");

	if((vpd[PDX(addr)] & PTE_P) == 0 || (vpt[PGNUM(addr)] & (PTE_P|PTE_COW)) != (PTE_P|PTE_COW))
	 	panic("pgfault: access not a copy-on-write page\n");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.

	r = sys_page_alloc(0, PFTEMP, PTE_U | PTE_W | PTE_P);
	if (r < 0) panic("pgfault: sys_page_alloc: %e", r);

	addr = ROUNDDOWN(addr, PGSIZE);
	memmove(PFTEMP, addr, PGSIZE);
	r = sys_page_map(0, PFTEMP, 0, addr, PTE_U | PTE_W | PTE_P);
	if (r < 0) panic("pgfault: sys_page_map: %e", r);
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

	void* addr = (void*)(pn*PGSIZE);
	if(addr < (void*)UTOP && vpd[PDX(addr)] & PTE_P && vpt[pn] & PTE_P){
		if (vpt[pn] & (PTE_W | PTE_COW)) {
			r = sys_page_map(0, addr, envid, addr, PTE_U | PTE_P | PTE_COW);
			if (r < 0) return r;

			r = sys_page_map(0, addr, 0, addr, PTE_U | PTE_P | PTE_COW);
			if (r < 0) return r;
		} else {
			r = sys_page_map(0, addr, envid, addr, PTE_U | PTE_P);
			if (r < 0) return r;
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
//   Use vpd, vpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	set_pgfault_handler(pgfault);
	envid_t child_envid = sys_exofork();
	if(child_envid < 0){
		panic("fork: sys_exofork: %e", child_envid);
	}
	else if(child_envid == 0){
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	int r = 0;
	unsigned ep = UTOP/PGSIZE, uxsp = UXSTACKTOP/PGSIZE-1;
	for(unsigned i = 0; i < ep; ++i) {
		if (i != uxsp){
			if ((vpd[i/NPTENTRIES] & PTE_P) != 0 && (vpt[i] & (PTE_U |PTE_P)) != 0) {
				r = duppage(child_envid, i);
				if(r < 0) panic("fork: duppage: %e\n", r);
			}
		}
	}

	r = sys_page_alloc(child_envid, (void *)(UXSTACKTOP - PGSIZE), PTE_W | PTE_U | PTE_P);
  if (r < 0) panic("fork: sys_page_alloc: %e\n", r);

	extern void _pgfault_upcall(void);
  r = sys_env_set_pgfault_upcall(child_envid, _pgfault_upcall);
	if (r < 0) panic("fork: sys_env_set_pgfault_upcall: %e\n", r);

	r = sys_env_set_status(child_envid,ENV_RUNNABLE);
	if (r < 0) panic("fork: sys_env_set_status: %e\n", r);
	return child_envid;
}

// Challenge!
int
sfork(void)
{
	set_pgfault_handler(pgfault);
	thisenv = NULL;
	envid_t child_envid = sys_exofork();
	if(child_envid < 0)
		panic("fork: sys_exofork: %e", child_envid);
	else if(child_envid == 0)
		return 0;

	int r = 0;
	void* addr = NULL;
	unsigned ep = UTOP/PGSIZE, uxsp = UXSTACKTOP/PGSIZE-1, usp = USTACKTOP/PGSIZE-1;
	for(unsigned i = 0; i < ep; ++i) {
		if (i != uxsp && i != usp){
			if ((vpd[i/NPTENTRIES] & PTE_P) != 0 && (vpt[i] & (PTE_U |PTE_P)) != 0) {
				addr = (void*)(i * PGSIZE);
				r = sys_page_map(0, addr, child_envid, addr, PTE_P | PTE_W | PTE_U);
				if(r < 0) panic("fork: sys_page_map: %e\n", r);
			}
		}
	}
	r = duppage(child_envid, usp);
	if (r < 0) panic("fork: duppage: %e\n", r);

	r = sys_page_alloc(child_envid, (void *)(UXSTACKTOP - PGSIZE), PTE_W | PTE_U | PTE_P);
  if (r < 0) panic("fork: sys_page_alloc: %e\n", r);

	extern void _pgfault_upcall(void);
  r = sys_env_set_pgfault_upcall(child_envid, _pgfault_upcall);
	if (r < 0) panic("fork: sys_env_set_pgfault_upcall: %e\n", r);

	r = sys_env_set_status(child_envid,ENV_RUNNABLE);
	if (r < 0) panic("fork: sys_env_set_status: %e\n", r);
	return child_envid;
}
