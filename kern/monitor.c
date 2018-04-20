// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>
#include <kern/env.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line
#define uint64_t unsigned long long


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display a listing of function call frames", mon_backtrace},
	{ "time", "Display running time (in clocks cycles) of the command", mon_time},
	{ "showmappings", "Display the physical page mappings and corresponding permission bits", mon_showmappings},
	{ "chperm", "Set, clear or change the permissions of any mapping", mon_chperm},
	{ "dumpcont", "Dump the contents of a given virtual/physical address memory range", mon_dumpcont},
	{ "c", "Continue execution from the current location", mon_continue },
  { "si", "Execute the code instruction by instruction", mon_stepinto },
  { "x", "Dispaly the memory", mon_display }
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-entry+1023)/1024);
	return 0;
}

// Lab1 only
// read the pointer to the retaddr on the stack
static uint32_t
read_pretaddr() {
    uint32_t pretaddr;
    __asm __volatile("leal 4(%%ebp), %0" : "=r" (pretaddr));
    return pretaddr;
}

// void do_overflow(void){
// 	cprintf("Overflow success\n");
// }
//
// void start_overflow(void){
// 	char str[256]={};
// 	int nstr = 0;
// 	char *pret_addr;
//
// 	pret_addr = (char*)read_pretaddr();
// 	int i = 0;
// 	for(; i < 256; ++i){
// 		str[i] = 'h';
// 		if(i%2)
// 			str[i]='a';
// 	}
// 	void (*do_overflow_t)();
// 	do_overflow_t = do_overflow;
// 	uint32_t ret_addr = (uint32_t)do_overflow_t + 3;
//
// 	uint32_t ret_type_0 = ret_addr & 0xff;
// 	uint32_t ret_type_1 = (ret_addr>>8) & 0xff;
// 	uint32_t ret_type_2 = (ret_addr>>16) & 0xff;
// 	uint32_t ret_type_3 = (ret_addr>>24) & 0xff;
// 	str[ret_type_0] = '\0';
// 	cprintf("%s%n\n",str,pret_addr);
// 	str[ret_type_0] = 'h';
// 	str[ret_type_1] = '\0';
// 	cprintf("%s%n\n",str,pret_addr+1);
// 	str[ret_type_1] = 'h';
// 	str[ret_type_2] = '\0';
// 	cprintf("%s%n\n",str,pret_addr+2);
// 	str[ret_type_2] = 'h';
// 	str[ret_type_3] = '\0';
// 	cprintf("%s%n\n",str,pret_addr+3);
// }

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	cprintf("Stack backtrace:\n");
	uint32_t ebp = read_ebp();

	while(ebp!=0){
		uint32_t* pebp = (uint32_t*)ebp;
		uint32_t eip = *(pebp+1);
		cprintf("  eip %08x  ebp %08x  args %08x %08x %08x %08x %08x\n", eip, ebp, *(pebp+2), *(pebp+3), *(pebp+4), *(pebp+5), *(pebp+6));
		struct Eipdebuginfo info;
		debuginfo_eip(eip,&info);
		cprintf("\t%s:%d: ", info.eip_file, info.eip_line);
		for(int i = 0; i < info.eip_fn_namelen; ++i){
			cprintf("%c", info.eip_fn_name[i]);
		}
		cprintf("+%d\n", eip - (uint32_t)info.eip_fn_addr);
		ebp = *pebp;
	}

	cprintf("Backtrace success\n");
	return 0;
}

int
mon_time(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 2){
		cprintf("Usage: time [command]\n");
		return 0;
	}
	uint32_t b_high = 0, b_low = 0, i = 0;
	uint32_t e_high = 0, e_low = 0;
	--argc;
	++argv;

	// Lookup and invoke the command
	for (i = 0; i < NCOMMANDS; ++i) {
		if (strcmp(argv[0], commands[i].name) == 0) {
			__asm __volatile("rdtsc" : "=d"(b_high),"=a"(b_low));
			commands[i].func(argc, argv, tf);
			__asm __volatile("rdtsc" : "=d"(e_high),"=a"(e_low));
			break;
		}
	}

	if (i < NCOMMANDS) {
		uint64_t begin =  (((uint64_t)b_high)<<32) | ((uint64_t)b_low);
		uint64_t end =  (((uint64_t)e_high)<<32) | ((uint64_t)e_low);
		cprintf("%s cycles: %lld\n", argv[0], end - begin);
	}
	else {
		cprintf("Unknown command '%s'\n", argv[0]);
	}

	return 0;
}

uint32_t
hex2int(char* hex) {
	uint32_t res = 0;
	if(hex[0]!= '0' || hex[1] != 'x')return -1;
	char* buf = hex + 2;
	int cnt = 0;
	while (*buf != '\0') {
		char c = *buf;
		if (c >= 'a') c = c -'a' + 10;
		else if (c >= 'A') c = c -'A' + 10;
		else c = c - '0';
		res = res * 16 + c ;
		++buf;
		++cnt;
	}
	if(cnt > 8) return -1;
	return res;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 3){
		cprintf("Usage: showmappings [begin_addr] [end_addr]\n");
		return 0;
	}
	uint32_t begin_page = hex2int(argv[1]);
	uint32_t end_page = hex2int(argv[2]);
	cprintf("ba: %08x\t ea: %08x", begin_page, end_page);
	if(begin_page == -1 || end_page == -1){
		cprintf("Error: invalid virtual/linear address\n");
		return 0;
	}
	cprintf("|  virtaddr  |  physaddr  | P | W | U | PWT | PCD | A | D | PS | G |\n");
	cprintf("|------------------------------------------------------------------|\n");
	begin_page = PGNUM(begin_page)<<PTXSHIFT;
	end_page = PGNUM(end_page)<<PTXSHIFT;
	while(begin_page <= end_page){
		pte_t* pte = pgdir_walk(kern_pgdir, (void *)begin_page, 0);
		if(pte == NULL || !(*pte & PTE_P)){
			cprintf("| 0x%08x |                    Not Exist                        |\n", begin_page);
			begin_page += PGSIZE;
		}else{
			cprintf("| 0x%08x | 0x%08x ", begin_page, PGNUM(*pte)<<PTXSHIFT);
			cprintf("| %1d | %1d | %1d |  %1d  |  %1d  | %1d | %1d | %1d  | %1d |\n",
							(*pte&PTE_P)?1:0, (*pte&PTE_W)?1:0, (*pte&PTE_U)?1:0, (*pte&PTE_PWT)?1:0, (*pte&PTE_PCD)?1:0,
							(*pte&PTE_A)?1:0, (*pte&PTE_D)?1:0, (*pte&PTE_PS)?1:0, (*pte&PTE_G)?1:0);
			if(*pte & PTE_PS) begin_page += PTSIZE;
			else begin_page += PGSIZE;
		}
		cprintf("|------------------------------------------------------------------|\n");
	}
	return 0;
}

int
mon_chperm(int argc, char **argv, struct Trapframe *tf)
{
	if(argc < 3){
		cprintf("Usage: chperm [addr] [+/-][permission(W/U/PWT/PCD/A/D/G)]...\n");
		return 0;
	}
	uint32_t addr = hex2int(argv[1]);
	pte_t *pte = pgdir_walk(kern_pgdir, (void *)addr, 0);
	if(pte == NULL || !(*pte & PTE_P)){
		cprintf("Error: page of addr 0x%08x not exist\n", addr);
		return 0;
	}
	for(int i = 2; i < argc; ++i){
			if (strcmp(argv[i] + 1, "W") == 0) {
					if(argv[i][0]=='+') *pte |= PTE_W;
					else if(argv[i][0]=='-') *pte &= ~PTE_W;
			} else if (strcmp(argv[i] + 1, "U") == 0) {
					if(argv[i][0]=='+') *pte |= PTE_U;
					else if(argv[i][0]=='-') *pte &= ~PTE_U;
			} else if (strcmp(argv[i] + 1, "PWT") == 0) {
					if(argv[i][0]=='+') *pte |= PTE_PWT;
					else if(argv[i][0]=='-') *pte &= ~PTE_PWT;
			} else if (strcmp(argv[i] + 1, "PCD") == 0) {
					if(argv[i][0]=='+') *pte |= PTE_PCD;
					else if(argv[i][0]=='-') *pte &= ~PTE_PCD;
			} else if (strcmp(argv[i] + 1, "A") == 0) {
					if(argv[i][0]=='+') *pte |= PTE_A;
					else if(argv[i][0]=='-') *pte &= ~PTE_A;
			} else if (strcmp(argv[i] + 1, "D") == 0) {
					if(argv[i][0]=='+') *pte |= PTE_D;
					else if(argv[i][0]=='-') *pte &= ~PTE_D;
			} else if (strcmp(argv[i] + 1, "G") == 0) {
					if(argv[i][0]=='+') *pte |= PTE_G;
					else if(argv[i][0]=='-') *pte &= ~PTE_G;
			}
	}
	return 0;
}

int
mon_dumpcont(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 4){
dcusage:
		cprintf("Usage: dumpcont [-(v/p)] [begin_addr] [end_addr]\n");
		return 0;
	}
	int type = 0;
	if (strcmp(argv[1], "-v") == 0) {
		type = 0;
  }else if (strcmp(argv[1], "-p") == 0) {
    type = 1;
  }else goto dcusage;
	uint32_t cur_addr = hex2int(argv[2]);
	uint32_t end_addr = hex2int(argv[3]);
	if(cur_addr == -1 || end_addr == -1){
		cprintf("Error: invalid %s address\n", type?"physical":"virtual");
		return 0;
	}
	uint32_t cnt = 0;
  while(cur_addr <= end_addr){
    if (cnt % 4 == 0) cprintf("\n0x%x:", cur_addr);
    if (type) {
			cprintf(" %02x", ((unsigned)(*(char*)(cur_addr + KERNBASE))) & 0xff);
    }else {
			cprintf(" %02x", ((unsigned)(*(char*)cur_addr)) & 0xff);
    }
    ++cnt;
		++cur_addr;
  }
  cprintf("\n");
  return 0;
}

int
mon_continue(int argc, char **argv, struct Trapframe *tf)
{
	if(argc > 1){
		cprintf("Usage: c\n");
		return 0;
	}
	tf->tf_eflags &= ~FL_TF;
	env_run(curenv);
  return 0;
}

int
mon_stepinto(int argc, char **argv, struct Trapframe *tf)
{
	if(argc > 1){
		cprintf("Usage: si\n");
		return 0;
	}
  tf->tf_eflags |= FL_TF;
  uint32_t eip = tf->tf_eip;
  cprintf("tf_eip=%08x\n", eip);
  struct Eipdebuginfo info;
  debuginfo_eip(eip, &info);
  cprintf("%s:%d: ", info.eip_file, info.eip_line);
  for (int i = 0; i < info.eip_fn_namelen; ++i) {
    cprintf("%c", info.eip_fn_name[i]);
  }
  cprintf("+%d\n", eip - (uint32_t)info.eip_fn_addr);
	env_run(curenv);
  return 0;
}

int
mon_display(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 2){
		cprintf("Usage: x [addr]\n");
		return 0;
	}
	uint32_t addr = hex2int(argv[1]);
	if(addr == -1){
		cprintf("Error: invalid address\n");
		return 0;
	}
  cprintf("%u\n", *(uint32_t*)addr);
  return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
