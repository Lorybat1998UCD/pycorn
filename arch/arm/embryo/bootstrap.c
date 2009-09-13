/*
 * Bootstrap code
 *
 * Copyright 2008 Torne Wuff
 *
 * This file is part of Pycorn.
 *
 * Pycorn is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "bootstrap.h"
#include <string.h>

physaddr alloc_pages_zero(uint32_t bytes, uint32_t align);
physaddr get_page_table(int section_index, int skip_map);
void map_pages(virtaddr virt_start, virtaddr virt_end, physaddr phys_start);
void boot_after_mmu(int selfmap_index, uint32_t old_pde)
  __attribute__((noreturn));

void boot_start()
{
  // Work out phys->virt offset
  bootdata->phys_to_virt = (uint32_t)&_start - bootdata->rom_base;

  // Print all the info we received from the assembly entry point
  DBGSTR("Pycorn bootstrap entered\n");
  DBGINT("rom base: ", bootdata->rom_base);
  DBGINT("machine type: ", bootdata->machtype);
  DBGINT("taglist ptr: ", bootdata->taglist_ptr);
  DBGINT("phys->virt: ", bootdata->phys_to_virt);

  // Work out section sizes
  uint32_t text_size = &__text_end__ - &__text_start__;
  uint32_t data_size = &__data_end__ - &__data_start__;
  uint32_t bss_size = &__bss_end__ - &__bss_start__;
  uint32_t heap_size = &__heap_end__ - &__heap_start__;
  uint32_t stack_size = &__stack_end__ - &__stack_start__;

  // Work out where the first free page after the image is
  uint32_t img_size = text_size + data_size;
  bootdata->next_free_page = bootdata->rom_base + img_size;
  DBGINT("first free page: ", bootdata->next_free_page);

  // Allocate page directory
  DBGSTR("Allocate page directory\n");
  bootdata->page_directory = alloc_pages_zero(PAGEDIR_SIZE, PAGEDIR_SIZE);
  DBGINT("page directory: ", bootdata->page_directory);
  
  // Set MMU base address
  DBGSTR("Set MMU base address\n");
  mmu_set_base(bootdata->page_directory);

  // Allocate and map page table mappings
  DBGSTR("Allocate and map page table mappings\n");
  int ptbl_section = (virtaddr)(&__page_tbl_start__) >> SECTION_SHIFT;
  physaddr ptbl_map = get_page_table(ptbl_section, 1);
  virtaddr ptbl_address = (virtaddr)(&__page_tbl_start__)
      + (ptbl_section * PAGETABLE_SIZE);
  map_pages(ptbl_address, ptbl_address + (PAGE_SIZE * PTBLS_PER_PAGE),
      ptbl_map | 0x3f);

  // Map page directory
  DBGSTR("Map page directory\n");
  map_pages((virtaddr)&__page_dir_virt__,
      (virtaddr)(&__page_dir_virt__ + PAGEDIR_SIZE),
      bootdata->page_directory | 0x3f); // ap 3 (r/w), cache/buffer

  // Map text section of image
  DBGSTR("Map text section\n");
  map_pages((virtaddr)&__text_start__, (virtaddr)&__text_end__,
      bootdata->rom_base | 0xf); // ap 0 (rom), cache/buffer

  // Map data section of image
  DBGSTR("Map data section\n");
  physaddr data_phys = bootdata->rom_base + text_size;
  map_pages((virtaddr)&__data_start__, (virtaddr)&__data_end__,
      data_phys | 0x3f); // ap 3 (r/w), cache/buffer

  // Allocate and map bss section
  DBGSTR("Allocate and map bss\n");
  physaddr bss_phys = alloc_pages_zero(bss_size, PAGE_SIZE);
  map_pages((virtaddr)&__bss_start__, (virtaddr)&__bss_end__,
      bss_phys | 0x3f); // ap 3 (r/w), cache/buffer

  // Allocate and map heap section
  DBGSTR("Allocate and map heap\n");
  physaddr heap_phys = alloc_pages_zero(heap_size, PAGE_SIZE);
  map_pages((virtaddr)&__heap_start__, (virtaddr)&__heap_end__,
      heap_phys | 0x3f); // ap 3 (r/w), cache/buffer

  // Allocate and map heap section
  DBGSTR("Allocate and map stack\n");
  physaddr stack_phys = alloc_pages_zero(stack_size, PAGE_SIZE);
  map_pages((virtaddr)&__stack_start__, (virtaddr)&__stack_end__,
      stack_phys | 0x3f); // ap 3 (r/w), cache/buffer

  // Map debug UART
  DBGSTR("Mapping debug UART\n");
  map_pages((virtaddr)&__dbg_serial_virt__, 
      (virtaddr)(&__dbg_serial_virt__ + PAGE_SIZE),
      (physaddr)&__dbg_serial_phys__ | 0x33); // ap 3 (r/w), no cache/no buffer

  // Map boot data page
  DBGSTR("Mapping boot data\n");
  map_pages((virtaddr)&__bootdata_virt__,
      (virtaddr)(&__bootdata_virt__ + PAGE_SIZE),
      (physaddr)bootdata | 0x3f); // ap 3 (r/w), cache/buffer

  // Self-map MMU enabling code
  DBGSTR("Self-map MMU enabling code\n");
  mmu_enable_func mmu_enable_phys = &mmu_enable - bootdata->phys_to_virt;
  physaddr selfmap_addr = (physaddr)mmu_enable_phys;
  int selfmap_index = selfmap_addr >> SECTION_SHIFT;
  selfmap_addr = selfmap_index << SECTION_SHIFT;
  uint32_t *pgd = (uint32_t *)bootdata->page_directory;
  uint32_t old_pde = pgd[selfmap_index];
  pgd[selfmap_index] = selfmap_addr | 0xe; // ap 0 (rom), cache/buffer

  // Enable MMU
  DBGSTR("Enable MMU\n");
  mmu_enable_phys(selfmap_index, old_pde, &boot_after_mmu);
}

void boot_after_mmu(int selfmap_index, uint32_t old_pde)
{
  // Restore mapping overwritten by self-mapping
  uint32_t *pgd = (uint32_t *)&__page_dir_virt__;
  pgd[selfmap_index] = old_pde;
  mmu_invalidate_tlb();

  // Run the main CRT startup code
  _mainCRTStartup();
}

physaddr alloc_pages_zero(uint32_t bytes, uint32_t align)
{
  physaddr base = bootdata->next_free_page;
  align -= 1;
  base = (base + align) & ~align;
  bootdata->next_free_page = base + bytes;
  memset((void *)base, 0, bytes);
  return base;
}

physaddr get_page_table(int section_index, int skip_map)
{
  uint32_t *pgd = (uint32_t *)bootdata->page_directory;
  physaddr ptbl = (physaddr)(pgd[section_index] & 0xFFFFFC00);
  if (!ptbl)
  {
    ptbl = alloc_pages_zero(PAGE_SIZE, PAGE_SIZE);

    int start_sec = section_index & ~(PTBLS_PER_PAGE-1);
    for (int i = 0; i < 4; ++i)
      pgd[start_sec + i] = (ptbl + (i * PAGETABLE_SIZE)) | 0x1;

    int pagetable_index = section_index / PTBLS_PER_PAGE;
    virtaddr pgt_virt = (virtaddr)(&__page_tbl_start__)
        + (pagetable_index << PAGE_SHIFT);
    if (!skip_map)
      map_pages(pgt_virt, pgt_virt + PAGE_SIZE, ptbl | 0x3f);

    int index_in_page = section_index % PTBLS_PER_PAGE;
    ptbl += index_in_page * PAGETABLE_SIZE;
  }

  return ptbl;
}

void map_pages(virtaddr virt_start, virtaddr virt_end, physaddr phys_start)
{
  int bytes = virt_end - virt_start;
  int section_index = virt_start >> 20;
  int page_index = (virt_start << 12) >> 24;
  uint32_t pte = phys_start;

  for(;;)
  {
    uint32_t *pgt = (uint32_t *)get_page_table(section_index, 0);
    do
    {
      pgt[page_index++] = pte;
      pte += PAGE_SIZE;
      bytes -= PAGE_SIZE;
      if (bytes == 0)
        return;
    }
    while (page_index < 0x100);
    page_index = 0;
    section_index++;
  }
}
