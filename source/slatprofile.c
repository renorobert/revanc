/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include "cache.h"
#include "paging.h"
#include "profile.h"
#include "slatprofile.h"
#include "shuffle.h"

#define PAGE_SHIFT  12
#define PAGE_SIZE   (1 << PAGE_SHIFT)
#define PFN_PRESENT (1ull << 63)
#define PFN_PFN     ((1ull << 55) - 1)

#define PML4_IDX(v) ((v >> 39) & 0x1FF)
#define PDPT_IDX(v) ((v >> 30) & 0x1FF)
#define PD_IDX(v)   ((v >> 21) & 0x1FF)
#define PT_IDX(v)   ((v >> 12) & 0x1FF)

#define PTE_PHYS_MASK 0xffffffff000

typedef uint64_t gpa_t;
typedef uint64_t gfn_t;
typedef void    *gva_t;

struct info {
	unsigned long pid;      // input
	unsigned long cr3;      // output
} info;

int mem, pagemap, cr3;
uint64_t gpa;
FILE *res;

static FILE *vfopenf(const char *fname, const char *mode, va_list ap)
{
	FILE *f;
	char *s;

	if (vasprintf(&s, fname, ap) < 0)
		return NULL;

	f = fopen(s, mode);
	free(s);

	return f;
}

static FILE *fopenf(const char *fname, const char *mode, ...)
{
	FILE *f;
	va_list ap;

	va_start(ap, mode);
	f = vfopenf(fname, mode, ap);
	va_end(ap);

	return f;
}

uint32_t page_offset(unsigned long addr) 
{

	return addr & ((1 << PAGE_SHIFT) - 1);
}

gfn_t gva_to_gfn(gva_t addr) 
{

	off_t off;
	uint64_t pte, pfn;

	if (!pagemap)
		pagemap = open("/proc/self/pagemap", O_RDONLY);

	if (pagemap < 0)
		errx(EXIT_FAILURE, "open pagemap");

	off = ((uintptr_t)addr >> 9) & ~7;

	if (lseek(pagemap, off, SEEK_SET) != off)
		errx(EXIT_FAILURE, "lseek");

	if (read(pagemap, &pte, 8) != 8)
		errx(EXIT_FAILURE, "read");

	if (!(pte & PFN_PRESENT))
		return (gfn_t)-1;

	pfn = pte & PFN_PFN;

	return pfn;
}

uint64_t gva_to_gpa(uint64_t addr) 
{

	gfn_t gfn = gva_to_gfn((gva_t)addr);

	assert(gfn != (gfn_t)-1);

	return (gfn << PAGE_SHIFT) | page_offset((unsigned long)addr);
}

unsigned long *map_phy_address(off_t address, size_t size)
{
	unsigned long *map;

	if (!mem)
		mem = open("/dev/mem", O_RDWR | O_SYNC);

	map = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
			mem, address);
	return map;
}

void get_page_info(unsigned long cr3, unsigned long gva)
{
	unsigned long  *pml4_map, *pdpt_map, *pd_map, *pt_map;
	unsigned long  pml4e, pdpte, pde, pte;

	pml4_map =  map_phy_address(cr3, PAGE_SIZE);
	pml4e = pml4_map[PML4_IDX(gva)] & PTE_PHYS_MASK;
	fprintf(res, "gPML4E,0x%lx\n", pml4e);
	munmap(pml4_map, PAGE_SIZE);

	pdpt_map =  map_phy_address(pml4e, PAGE_SIZE);
	pdpte = pdpt_map[PDPT_IDX(gva)] & PTE_PHYS_MASK;
	fprintf(res, "gPDPTE,0x%lx\n", pdpte);
	munmap(pdpt_map, PAGE_SIZE);

	pd_map =  map_phy_address(pdpte, PAGE_SIZE);
	pde = pd_map[PD_IDX(gva)] & PTE_PHYS_MASK;
	fprintf(res, "gPDE,0x%lx\n", pde);
	munmap(pd_map, PAGE_SIZE);

	pt_map =  map_phy_address(pde, PAGE_SIZE);
	pte = pt_map[PT_IDX(gva)] & PTE_PHYS_MASK;
	fprintf(res, "gPTE,0x%lx\n", pte);
	munmap(pt_map, PAGE_SIZE);
	
	assert(gpa == pte);
}

unsigned long get_cr3(void)
{
	struct info info = {0};

	if (!cr3)
		cr3 = open("/dev/cr3",O_RDWR);

	if(cr3 < 0)
		errx(EXIT_FAILURE, "[!] Error opening /dev/cr3");

	info.pid = getpid();
	ioctl(cr3, 0, &info);

	return info.cr3;
}

void close_files(void)
{
	close(pagemap);
	close(mem);
	close(cr3);
}

static void slat_profile_cache_lines(struct cache *cache,
		struct page_level *level, size_t page_level, size_t *cache_lines,
		size_t ncache_lines, size_t nrounds, volatile char *page)
{
	volatile char *p;
	uint64_t timing;
	size_t cache_line;
	size_t i, j, k;
	unsigned long cr3_reg;

	/* Get physical address from pagemap for validation */
	p = page;
	profile_access(p);
	gpa = gva_to_gpa((uint64_t)p);

	printf("Dumping page table information...\n");

	fprintf(res, "gVA,0x%lx\n", (uint64_t)p);

	cr3_reg = get_cr3();
	fprintf(res, "gCR3,0x%lx\n", cr3_reg);

	get_page_info(cr3_reg, (unsigned long)p);
	close_files();

	printf("Profiling cache...\n");
	fflush(stdout);

	for (i = 0; i < ncache_lines; ++i) {

		cache_line = cache_lines[i];
		p = page + cache_line * cache->line_size;

		/* warmup */
		for (k = 0; k < 32; k++) {
			evict_cache_line(cache, level->table_size, cache_line, page_level);
			profile_access(p);
		}

		for (j = 0; j < nrounds; ++j) {
			evict_cache_line(cache, level->table_size, cache_line, page_level);
			timing = profile_access(p);
			fprintf(res, "%ld,%lu\n", i, timing);
		}
	}
}

void slat_profile_page_table(struct cache *cache,
		struct page_level *level, size_t n, size_t ncache_lines, size_t nrounds,
		volatile char *target)
{
	volatile char *page;
	size_t *cache_lines;
	size_t j;

	if (!(cache_lines = malloc(ncache_lines * sizeof *cache_lines)))
		return;

	generate_indicies(cache_lines, ncache_lines);

	page = target;

	for (j = 0; j < level->npages; ++j) {
		slat_profile_cache_lines(cache, level, n,
				cache_lines, ncache_lines, nrounds, page);
		break;
	}
}

void slat_profile_page_tables(
		struct cache *cache,
		struct page_format *fmt,
		size_t nrounds,
		volatile void *target,
		const char *output_dir)
{
	struct page_level *level;
	size_t ncache_lines;
	size_t i;

	if (!(res = fopenf("%s/slat-timings.csv", "w", output_dir)))
		return;

	for (i = 0, level = fmt->levels; i < fmt->nlevels; ++i, ++level) {

		ncache_lines = level->table_size / cache->line_size;

		slat_profile_page_table(cache, level, i, ncache_lines,
				nrounds, target);

		break;
	}

	printf("Check slat-timings.csv for results!\n\n");
	fclose(res);
}
