/*	$NetBSD: bus_space.c,v 1.13 2023/12/08 01:38:20 thorpej Exp $ 	*/

/*
 * Copyright (c) 1996, 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: bus_space.c,v 1.13 2023/12/08 01:38:20 thorpej Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

void
mipsco_bus_space_init(bus_space_tag_t bst, const char *name, paddr_t paddr, vaddr_t vaddr, bus_addr_t start, bus_size_t size)
{
	bst->bs_name = name;
	bst->bs_spare = NULL;
	bst->bs_start = start;
	bst->bs_size = size;
	bst->bs_pbase = paddr;
	bst->bs_vbase = vaddr;
	bst->bs_compose_handle = mipsco_bus_space_compose_handle;
	bst->bs_dispose_handle = mipsco_bus_space_dispose_handle;
	bst->bs_paddr = mipsco_bus_space_paddr;
	bst->bs_map = mipsco_bus_space_map;
	bst->bs_unmap = mipsco_bus_space_unmap;
	bst->bs_subregion = mipsco_bus_space_subregion;
	bst->bs_mmap = mipsco_bus_space_mmap;
	bst->bs_alloc = mipsco_bus_space_alloc;
	bst->bs_free = mipsco_bus_space_free;
	bst->bs_aux = NULL;
	bst->bs_bswap = 0; /* No byte swap for stream methods */
	mipsco_bus_space_set_aligned_stride(bst, 0);
}

void
mipsco_bus_space_set_aligned_stride(bus_space_tag_t bst, unsigned int shift)
	/* shift:		 log2(alignment) */
{
	bst->bs_stride = shift;

	if (shift == 2) {		/* XXX Assumes Big Endian & 4B */
	    bst->bs_offset_1 = 3;
	    bst->bs_offset_2 = 2;
	} else {
	    bst->bs_offset_1 = 0;
	    bst->bs_offset_2 = 0;
	}
	bst->bs_offset_4 = 0;
	bst->bs_offset_8 = 0;
}

int
mipsco_bus_space_compose_handle(bus_space_tag_t bst, bus_addr_t addr, bus_size_t size, int flags, bus_space_handle_t *bshp)
{
	bus_space_handle_t bsh = bst->bs_vbase +
	    ((addr - bst->bs_start) << bst->bs_stride);

	/*
	 * Since all buses can be linearly mappable, we don't have to check
	 * BUS_SPACE_MAP_LINEAR and BUS_SPACE_MAP_PREFETCHABLE.
	 */
	if ((flags & BUS_SPACE_MAP_CACHEABLE) == 0) {
		*bshp = bsh;
		return (0);
	}
	if (bsh < MIPS_KSEG1_START) /* KUSEG or KSEG0 */
		panic("mipsco_bus_space_compose_handle: bad address 0x%x", bsh);
	if (bsh < MIPS_KSEG2_START) { /* KSEG1 */
		*bshp = MIPS_PHYS_TO_KSEG0(MIPS_KSEG1_TO_PHYS(bsh));
		return (0);
	}
	/*
	 * KSEG2:
	 * Do not make the page cacheable in this case, since:
	 * - the page which this bus_space belongs might include
	 *   other bus_spaces.
	 * or
	 * - this bus might be mapped by wired TLB, in that case,
	 *   we cannot manupulate cacheable attribute with page granularity.
	 */
#ifdef DIAGNOSTIC
	printf("mipsco_bus_space_compose_handle: ignore cacheable 0x%x\n", bsh);
#endif
	*bshp = bsh;
	return (0);
}

int
mipsco_bus_space_dispose_handle(bus_space_tag_t bst, bus_space_handle_t bsh, bus_size_t size)
{
	return (0);
}

int
mipsco_bus_space_paddr(bus_space_tag_t bst, bus_space_handle_t bsh, paddr_t *pap)
{
	if (bsh < MIPS_KSEG0_START) /* KUSEG */
		panic("mipsco_bus_space_paddr(%p): bad address", (void *)bsh);
	else if (bsh < MIPS_KSEG1_START) /* KSEG0 */
		*pap = MIPS_KSEG0_TO_PHYS(bsh);
	else if (bsh < MIPS_KSEG2_START) /* KSEG1 */
		*pap = MIPS_KSEG1_TO_PHYS(bsh);
	else { /* KSEG2 */
		/*
		 * Since this region may be mapped by wired TLB,
		 * kvtophys() is not always available.
		 */
		*pap = bst->bs_pbase + (bsh - bst->bs_vbase);
	}
	return (0);
}

int
mipsco_bus_space_map(bus_space_tag_t bst, bus_addr_t addr, bus_size_t size, int flags, bus_space_handle_t *bshp)
{

	if (addr < bst->bs_start || addr + size > bst->bs_start + bst->bs_size)
		return (EINVAL);

	return (bus_space_compose_handle(bst, addr, size, flags, bshp));
}

void
mipsco_bus_space_unmap(bus_space_tag_t bst, bus_space_handle_t bsh, bus_size_t size)
{
	bus_space_dispose_handle(bst, bsh, size);
}

int
mipsco_bus_space_subregion(bus_space_tag_t bst, bus_space_handle_t bsh, bus_size_t offset, bus_size_t size, bus_space_handle_t *nbshp)
{
	*nbshp = bsh + (offset << bst->bs_stride);
	return (0);
}

paddr_t
mipsco_bus_space_mmap(bus_space_tag_t bst, bus_addr_t addr, off_t off, int prot, int flags)
{

	/*
	 * XXX We do not disallow mmap'ing of I/O space here,
	 * XXX which we should be doing.
	 */

	if (addr < bst->bs_start ||
	    (addr + off) >= (bst->bs_start + bst->bs_size))
		return (-1);

	return (mips_btop(bst->bs_pbase + (addr - bst->bs_start) + off));
}

int
mipsco_bus_space_alloc(bus_space_tag_t bst, bus_addr_t start, bus_addr_t end, bus_size_t size, bus_size_t align, bus_size_t boundary, int flags, bus_addr_t *addrp, bus_space_handle_t *bshp)
{

	panic("bus_space_alloc() not implemented");
}
