#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/printk.h>

#ifndef CONFIG_ARM
#error This functional is used on ARM architecture only.
#endif

//#define INV_PADDR (0xFFFFFFFF)
#define INV_PADDR    0

#define _1M 0x100000
#define _1K 0x000400
#define _4K 0x001000

#define SMALL_PAGE_SIZE    0x0001000
#define LARGE_PAGE_SIZE    0x0010000
#define SECTION_SIZE       0x0100000
#define SUPER_SECTION_SIZE 0x1000000

#define BLOCK_BEGIN   {
#define BLOCK_END     }

struct mmu_regs {
    uint32_t sctlr;  /* System Control Register */
	uint32_t ttbr0, ttbr1, ttbcr;
	int initialized;
};

static inline uint32_t to_pa(void *virt_addr, uint32_t desc, uint32_t pagesize)
{
	uint32_t pbase, offset;

	offset = (uint32_t)virt_addr & (pagesize - 1);
	pbase  = desc & ~(pagesize - 1);
	return pbase + offset;
}

static inline uint32_t get_section_size(uint32_t desc)
{
	return (desc & (1 << 18)) ? SUPER_SECTION_SIZE : SECTION_SIZE;
}

static uint32_t try_translate(struct mmu_regs *mr, int table, void *virt_addr)
{
	uint32_t *pgd;
	uint32_t des1;
	uint32_t i, pa;
	void *start;
	uint32_t retval = INV_PADDR;

	if( table == 0 ) {
		uint32_t n, mask;
		n    = mr->ttbcr & 7;
		mask = (1 << (14 - n)) - 1;
		pa = mr->ttbr0 & ~mask;
	} else {
		uint32_t mask;
		mask = (1 << (14 - 0)) - 1;
		pa = mr->ttbr1 & ~mask;
	}
	pgd = __va(pa);
	if(pgd == NULL) {
		pr_debug("Cannot map translation table @ 0x%08x\n", pa);
		return INV_PADDR;
	}

	i = (uint32_t)virt_addr >> 20;
	des1 = pgd[i];

	switch( (des1 & 3) ) {
	case 0:
	case 3:
		return INV_PADDR;

	case 1: /* page table */
		BLOCK_BEGIN
		uint32_t *pgd2;
		uint32_t des2;
		int j;

		pa = des1 & ~(_1K - 1);
		pgd2 = __va(pa);
		if(pgd2 == NULL) {
			pr_debug("Cannot map table entry @ 0x%0x\n", pa);
			return INV_PADDR;
		}
		for(j = 0; j < 256; ) {
			des2 = pgd2[j];
			if( des2 & 2 ) {
				start = (void *) (i * _1M + j * SMALL_PAGE_SIZE);

				if( virt_addr >= start && virt_addr < start + SMALL_PAGE_SIZE ) {
					retval = to_pa(virt_addr, des2, SMALL_PAGE_SIZE);
					goto done;
				}
				j++;
			} else if( (des2 & 3) == 1 ) {
				start = (void *)(i * _1M + j * LARGE_PAGE_SIZE);
				if( virt_addr >= start && virt_addr < start + LARGE_PAGE_SIZE ) {
					retval = to_pa(virt_addr, des2, LARGE_PAGE_SIZE);
					goto done;
				}
				j += 16;
			}
		}
		break;
		BLOCK_END
	case 2: /* section or supper section */
		BLOCK_BEGIN
		const uint32_t section_size = get_section_size(des1);

		start  = (void *)((uint32_t)virt_addr & ~(section_size - 1));
		if(virt_addr >= start && virt_addr < start + section_size)
			retval = to_pa(virt_addr, des1, section_size);
		break;
		BLOCK_END
	}

done:
	return retval;
}

static struct mmu_regs mmu_regs;

uint32_t pdl_virt_to_phys(void *virt_addr)
{
	uint32_t phys_addr;
	if( ! mmu_regs.initialized ) {
		__asm__ __volatile__("MRC p15, 0, %0, C2, C0, 0\n\t" : "=r"(mmu_regs.ttbr0));
		__asm__ __volatile__("MRC p15, 0, %0, C2, C0, 1\n\t" : "=r"(mmu_regs.ttbr1));
		__asm__ __volatile__("MRC p15, 0, %0, C2, C0, 2\n\t" : "=r"(mmu_regs.ttbcr));
		mmu_regs.initialized = 1;
	};

	phys_addr = try_translate(&mmu_regs, 1, virt_addr);
	if(phys_addr != INV_PADDR)
		return phys_addr;
	phys_addr = try_translate(&mmu_regs, 0, virt_addr);
	return phys_addr;
}

EXPORT_SYMBOL(pdl_virt_to_phys);

