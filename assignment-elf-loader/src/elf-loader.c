// SPDX-License-Identifier: BSD-3-Clause

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <elf.h>
#include <string.h>
#include <sys/resource.h>

void *map_elf(const char *filename)
{
	// This part helps you store the content of the ELF file inside the buffer.
	struct stat st;
	void *file;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("open");
		exit(1);
	}

	fstat(fd, &st);

	file = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (file == MAP_FAILED) {
		perror("mmap");
		close(fd);
		exit(1);
	}

	return file;
}

void load_and_run(const char *filename, int argc, char **argv, char **envp)
{
	// Contents of the ELF file are in the buffer: elf_contents[x] is the x-th byte of the ELF file.
	void *elf_contents = map_elf(filename);

	/**
	 * TODO: ELF Header Validation
	 * Validate ELF magic bytes - "Not a valid ELF file" + exit code 3 if invalid.
	 * Validate ELF class is 64-bit (ELFCLASS64) - "Not a 64-bit ELF" + exit code 4 if invalid.
	 */
	if (memcmp(elf_contents, ELFMAG, SELFMAG) != 0) {
		fprintf(stderr, "Not a valid ELF file\n");
		exit(3);
	}
	if (((unsigned char *)elf_contents)[EI_CLASS] != ELFCLASS64) {
		fprintf(stderr, "Not a 64-bit ELF\n");
		exit(4);
	}

	/**
	 * TODO: Load PT_LOAD segments
	 * For minimal syscall-only binaries.
	 * For each PT_LOAD segment:
	 * - Map the segments in memory. Permissions can be RWX for now.
	 */

	// ELF header
	Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf_contents;

	// Program Header Table
	Elf64_Phdr *phdr = (Elf64_Phdr *)((char *)elf_contents + ehdr->e_phoff);

	uintptr_t load_base = 0;

	// check if it is PIE executable
	if (ehdr->e_type == ET_DYN) {
		// calculate load base
		uintptr_t vaddr1 = -1; // maximum virtual address
		uintptr_t vaddr2 = 0; // minimum virtual address

		for (int i = 0; i < ehdr->e_phnum; i++) {
			// only for PT_LOAD segmets
			if (phdr[i].p_type == PT_LOAD) {
				// update minimum virtual address
				if (phdr[i].p_vaddr < vaddr1)
					vaddr1 = phdr[i].p_vaddr;
				// update maximum virtual address
				if (vaddr2 < (phdr[i].p_vaddr + phdr[i].p_memsz))
					vaddr2 = phdr[i].p_vaddr + phdr[i].p_memsz;
			}
		}
		size_t base_size = vaddr2 - vaddr1;

		base_size = (base_size + sysconf(_SC_PAGESIZE) - 1) & ~(sysconf(_SC_PAGESIZE) - 1);
		// reserve memory at random address
		void *base = mmap(NULL, base_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (base == MAP_FAILED) {
			perror("base mmap fail");
			exit(1);
		}
		// if non-PIE load_base = 0
		load_base = (uintptr_t)base - vaddr1;
	}

	for (int i = 0; i < ehdr->e_phnum; i++) {
		// only load type PT_LOAD segmets
		if (phdr[i].p_type == PT_LOAD) {
			size_t page_size = sysconf(_SC_PAGESIZE);

			uintptr_t map_start = (uintptr_t)(phdr[i].p_vaddr + load_base) & ~(page_size - 1);
			uintptr_t map_end = ((uintptr_t)(phdr[i].p_vaddr + load_base) + phdr[i].p_memsz + page_size - 1) & ~(page_size - 1);

			size_t size = map_end - map_start;

			// map a new memory segment with RXW permissions
			void *segment = mmap((void *)map_start, size, PROT_READ | PROT_WRITE | PROT_EXEC,
						MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

			if (segment == MAP_FAILED) {
				perror("mmap fail");
				exit(1);
			}

			// copy from file into the mapped memory
			if (phdr[i].p_filesz > 0)
				memcpy((void *)(phdr[i].p_vaddr + load_base), (void *)((char *)elf_contents + phdr[i].p_offset), phdr[i].p_filesz);
			// fill with 0 the remaining memory in the segment (BSS)
			if (phdr[i].p_memsz >  phdr[i].p_filesz)
				memset((void *)(phdr[i].p_vaddr + load_base + phdr[i].p_filesz), 0, phdr[i].p_memsz - phdr[i].p_filesz);
		}
	}
	/**
	 * TODO: Load Memory Regions with Correct Permissions
	 * For each PT_LOAD segment:
	 *	- Set memory permissions according to program header p_flags (PF_R, PF_W, PF_X).
	 *	- Use mprotect() or map with the correct permissions directly using mmap().
	 */
	for (int i = 0; i < ehdr->e_phnum; i++) {
		// only PT_LOAD type segments
		if (phdr[i].p_type == PT_LOAD) {
			size_t page_size = sysconf(_SC_PAGESIZE);

			uintptr_t map_start = (uintptr_t)(phdr[i].p_vaddr + load_base) & ~(page_size - 1);
			uintptr_t map_end = ((uintptr_t)(phdr[i].p_vaddr + load_base) + phdr[i].p_memsz + page_size - 1) & ~(page_size - 1);

			size_t size = map_end - map_start;

			// set permissions according to p_flags
			int prot = 0;

			if (phdr[i].p_flags & PF_R)
				prot = prot | PROT_READ;
			if (phdr[i].p_flags & PF_W)
				prot = prot | PROT_WRITE;
			if (phdr[i].p_flags & PF_X)
				prot = prot | PROT_EXEC;
			int ok = mprotect((void *)map_start, size, prot);

			if (ok != 0) {
				perror("mprotect fail");
				exit(1);
			}
		}
	}

	/**
	 * TODO: Support Static Non-PIE Binaries with libc
	 * Must set up a valid process stack, including:
	 *	- argc, argv, envp
	 *	- auxv vector (with entries like AT_PHDR, AT_PHENT, AT_PHNUM, etc.)
	 * Note: Beware of the AT_RANDOM, AT_PHDR entries, the application will crash if you do not set them up properly.
	 */
	struct rlimit limit;
	int ok = getrlimit(RLIMIT_STACK, &limit);

	if (ok != 0) {
		perror("getrlimit fail");
		exit(1);
	}
	// getting the maximum allowed stack size
	size_t stack_size = limit.rlim_cur;

	// map memory for stack
	void *stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (stack == MAP_FAILED) {
		perror("mmap stack fail");
		exit(1);
	}
	// top of stack
	void *sp = stack + stack_size;
	uintptr_t stack_top = (uintptr_t)sp;

	// align 16 bytes
	stack_top = stack_top & ~0b1111;

	#define PUSH_LEVEL(value) do { \
		stack_top = stack_top - sizeof(uintptr_t); \
		*(uintptr_t *)stack_top = (uintptr_t)value; \
	} while (0)

	// generate 16 random bytes
	char random[16];
	FILE *fin = fopen("/dev/urandom", "rb");

	if (!fin) {
		perror("open random fail");
		exit(1);
	}
	if (fread(random, 1, 16, fin) != 16) {
		perror("fread random");
		exit(1);
	}
	fclose(fin);
	// copy random bytes onto stack
	stack_top = stack_top - 16;
	memcpy((void *)stack_top, random, 16);
	uintptr_t random_addr = stack_top;

	// copy argv strings on stack
	uintptr_t argv_addr[argc];

	for (int i = argc - 1; i >= 0; i--) {
		size_t length = strlen(argv[i]) + 1;

		stack_top = stack_top - length;
		memcpy((void *)stack_top, argv[i], length);
		argv_addr[i] = stack_top;
	}

	// store execfn address
	uintptr_t execfn_addr = argv_addr[0];

	// copy envp strings onto stack
	int envp_size = 0;

	while (envp[envp_size])
		envp_size++;

	uintptr_t envp_addr[envp_size];

	for (int i = envp_size - 1; i >= 0; i--) {
		size_t length = strlen(envp[i]) + 1;

		stack_top = stack_top - length;
		memcpy((void *)stack_top, envp[i], length);
		envp_addr[i] = stack_top;
	}

	// align 16 bytes
	stack_top = stack_top & ~0b1111;

	// get phdr address
	uintptr_t phdr_addr;

	for (int i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type == PT_LOAD) {
			phdr_addr = phdr[i].p_vaddr - phdr[i].p_offset + ehdr->e_phoff + load_base;
			break;
		}
	}

	// push NULL for end of auxv
	PUSH_LEVEL(0);
	PUSH_LEVEL(AT_NULL);

	// push pointer to executable filename
	PUSH_LEVEL(execfn_addr);
	PUSH_LEVEL(AT_EXECFN);

	// push 16 random bytes
	PUSH_LEVEL(random_addr);
	PUSH_LEVEL(AT_RANDOM);

	// push page size
	PUSH_LEVEL(sysconf(_SC_PAGESIZE));
	PUSH_LEVEL(AT_PAGESZ);

	// push entry point
	PUSH_LEVEL(ehdr->e_entry + load_base);
	PUSH_LEVEL(AT_ENTRY);

	// push count of program headers
	PUSH_LEVEL(ehdr->e_phnum);
	PUSH_LEVEL(AT_PHNUM);

	// push size of program header
	PUSH_LEVEL(sizeof(Elf64_Phdr));
	PUSH_LEVEL(AT_PHENT);

	// push address of program headers
	PUSH_LEVEL(phdr_addr);
	PUSH_LEVEL(AT_PHDR);

	// only if PIE executable
	if (ehdr->e_type == ET_DYN) {
		PUSH_LEVEL(load_base);
		PUSH_LEVEL(AT_BASE);
	}

	// push NULL for end of envp
	PUSH_LEVEL(0);
	// push envp on stack
	for (int i = envp_size - 1; i >= 0; i--)
		PUSH_LEVEL(envp_addr[i]);

	// push NULL for end of argv
	PUSH_LEVEL(0);
	// push argv on stack
	for (int i = argc - 1; i >= 0; i--)
		PUSH_LEVEL(argv_addr[i]);

	// push argc on stack
	PUSH_LEVEL(argc);

	// sp points to stack_top
	sp = (void *)stack_top;

	/**
	 * TODO: Support Static PIE Executables
	 * Map PT_LOAD segments at a random load base.
	 * Adjust virtual addresses of segments and entry point by load_base.
	 * Stack setup (argc, argv, envp, auxv) same as above.
	 */

	// TODO: Set the entry point and the stack pointer
	void (*entry)() = (void (*)())(ehdr->e_entry + load_base);

	// Transfer control
	__asm__ __volatile__(
			"mov %0, %%rsp\n"
			"xor %%rbp, %%rbp\n"
			"jmp *%1\n"
			:
			: "r"(sp), "r"(entry)
			: "memory"
			);
}

int main(int argc, char **argv, char **envp)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <static-elf-binary>\n", argv[0]);
		exit(1);
	}

	load_and_run(argv[1], argc - 1, &argv[1], envp);
	return 0;
}
