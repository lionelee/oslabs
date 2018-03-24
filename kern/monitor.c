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
	{ "time", "Display running time (in clocks cycles) of the command", mon_time}
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

	/*cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");*/

	int x = 1, y = 3, z = 4;
	cprintf("x %d, y %x, z %d\n", x, y, z);

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
