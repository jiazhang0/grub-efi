/* misc.h - prototypes for misc EFI functions */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2002,2003,2005,2006  Free Software Foundation, Inc.
 *
 *  GRUB is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GRUB_EFI_MISC_HEADER
#define GRUB_EFI_MISC_HEADER	1

struct e820_entry;
void e820_map_from_efi_map (struct e820_entry *e820_map,
			    int *e820_nr_map,
			    grub_efi_memory_descriptor_t *memory_map,
			    grub_efi_uintn_t desc_size,
			    grub_efi_uintn_t memory_map_size);

#endif /* ! GRUB_EFI_MISC_HEADER */
