/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006  Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <grub/misc.h>
#include <grub/types.h>
#include <grub/cpu/linux.h>
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/misc.h>

#include "switch.h"

#include <shared.h>

#define grub_file_size()    filemax

#define NEXT_MEMORY_DESCRIPTOR(desc, size)      \
  ((grub_efi_memory_descriptor_t *) ((char *) (desc) + (size)))

#define PTR_HI(x) ((grub_uint32_t) ((unsigned long long)((unsigned long)(x)) >> 32))

static unsigned long linux_mem_size;
static int loaded;
static void *real_mode_mem;
static void *prot_mode_mem;
static void *initrd_mem;
static void *mmap_buf;
static grub_efi_uintn_t real_mode_pages;
static grub_efi_uintn_t prot_mode_pages;
static grub_efi_uintn_t initrd_pages;
static grub_efi_uintn_t mmap_pages;
static grub_efi_guid_t graphics_output_guid = GRUB_EFI_GRAPHICS_OUTPUT_GUID;

static inline grub_size_t
page_align (grub_size_t size)
{
  return (size + (1 << 12) - 1) & (~((1 << 12) - 1));
}

/* Find the optimal number of pages for the memory map. Is it better to
   move this code to efimm.c?  */
static grub_efi_uintn_t
find_mmap_size (void)
{
  static grub_efi_uintn_t mmap_size = 0;

  if (mmap_size != 0)
    return mmap_size;

  mmap_size = (1 << 12);
  while (1)
    {
      int ret;
      grub_efi_memory_descriptor_t *mmap;
      grub_efi_uintn_t desc_size;

      mmap = grub_malloc (mmap_size);
      if (! mmap)
	return 0;

      ret = grub_efi_get_memory_map (&mmap_size, mmap, 0, &desc_size, 0);
      grub_free (mmap);

      if (ret < 0)
	grub_fatal ("cannot get memory map");
      else if (ret > 0)
	break;

      mmap_size += (1 << 12);
    }

  /* Increase the size a bit for safety, because GRUB allocates more on
     later, and EFI itself may allocate more.  */
  mmap_size += (1 << 11);

  return page_align (mmap_size);
}

static void
free_pages (void)
{
  if (real_mode_mem)
    {
      grub_efi_free_pages ((grub_addr_t) real_mode_mem, real_mode_pages);
      real_mode_mem = 0;
    }

  if (prot_mode_mem)
    {
      grub_efi_free_pages ((grub_addr_t) prot_mode_mem, prot_mode_pages);
      prot_mode_mem = 0;
    }

  if (initrd_mem)
    {
      grub_efi_free_pages ((grub_addr_t) initrd_mem, initrd_pages);
      initrd_mem = 0;
    }

  if (mmap_buf)
    {
      grub_efi_free_pages ((grub_addr_t) mmap_buf, mmap_pages);
      mmap_buf = 0;
    }
}

/* Allocate pages for the real mode code and the protected mode code
   for linux as well as a memory map buffer.  */
static int
allocate_pages (grub_size_t real_size, grub_size_t prot_size)
{
  grub_efi_uintn_t desc_size;
  grub_efi_memory_descriptor_t *mmap, *mmap_end;
  grub_efi_uintn_t mmap_size, tmp_mmap_size;
  grub_efi_memory_descriptor_t *desc;
  grub_efi_physical_address_t addr;

  /* Make sure that each size is aligned to a page boundary.  */
  real_size = page_align (real_size + SECTOR_SIZE);
  prot_size = page_align (prot_size);
  mmap_size = find_mmap_size ();

  grub_dprintf ("linux", "real_size = %x, prot_size = %x, mmap_size = %x\n",
		(unsigned int) real_size, (unsigned int) prot_size,
		(unsigned int) mmap_size);

  /* Calculate the number of pages; Combine the real mode code with
     the memory map buffer for simplicity.  */
  real_mode_pages = (real_size >> 12);
  prot_mode_pages = (prot_size >> 12);
  mmap_pages = (mmap_size >> 12);

  /* Initialize the memory pointers with NULL for convenience.  */
  real_mode_mem = 0;
  prot_mode_mem = 0;
  mmap_buf = 0;

  /* Read the memory map temporarily, to find free space.  */
  mmap = grub_malloc (mmap_size);
  if (! mmap)
    {
      errnum = ERR_UNRECOGNIZED;
      return 0;
    }

  tmp_mmap_size = mmap_size;
  if (grub_efi_get_memory_map (&tmp_mmap_size, mmap, 0, &desc_size, 0) <= 0)
    grub_fatal ("cannot get memory map");

  addr = 0;
  mmap_end = NEXT_MEMORY_DESCRIPTOR (mmap, tmp_mmap_size);
  /* First, find free pages for the real mode code
     and the memory map buffer.  */
  for (desc = mmap;
       desc < mmap_end;
       desc = NEXT_MEMORY_DESCRIPTOR (desc, desc_size))
    {
      /* Probably it is better to put the real mode code in the traditional
	 space for safety.  */
      if (desc->type == GRUB_EFI_CONVENTIONAL_MEMORY
	  && desc->physical_start <= 0x90000
	  && desc->num_pages >= real_mode_pages)
	{
	  grub_efi_physical_address_t physical_end;

          physical_end = desc->physical_start + (desc->num_pages << 12);
          if (physical_end > 0x90000)
            physical_end = 0x90000;

          grub_dprintf ("linux", "physical_start = %x, physical_end = %x\n",
                        (unsigned) desc->physical_start,
                        (unsigned) physical_end);
          addr = physical_end - real_size - mmap_size;
          if (addr < 0x10000)
            continue;

          grub_dprintf ("linux", "trying to allocate %u pages at %x\n",
                        (unsigned) real_mode_pages, (unsigned) addr);
          real_mode_mem = grub_efi_allocate_pages (addr, real_mode_pages);
          if (! real_mode_mem)
            grub_fatal ("cannot allocate pages");

          desc->num_pages -= real_mode_pages;
          break;
	}
    }

  if (! real_mode_mem)
    {
      grub_printf ("cannot allocate real mode pages");
      errnum = ERR_WONT_FIT;
      goto fail;
    }

  mmap_buf = grub_efi_allocate_pages (0, mmap_pages);
  if (! mmap_buf)
    {
      grub_printf("cannot allocate efi mmap pages");
      errnum = ERR_WONT_FIT;
      goto fail;
    }

  /* Next, find free pages for the protected mode code.  */
  /* XXX what happens if anything is using this address?  */
  prot_mode_mem = grub_efi_allocate_pages (0x100000, prot_mode_pages);
  if (! prot_mode_mem)
    {
      errnum = ERR_WONT_FIT;
      grub_printf ("cannot allocate protected mode pages");
      goto fail;
    }

  grub_free (mmap);
  return 1;

 fail:
  grub_free (mmap);
  free_pages ();
  return 0;
}

static void
find_bits (unsigned long mask, unsigned char *first,
	   unsigned char* len)
{
  unsigned char bit_pos = 0, bit_len = 0;
  *first =0;
  *len = 0;
  if (mask == 0)
    return;
  while (! (mask & 0x1)) {
    mask = mask >> 1;
    bit_pos++;
  }
  while (mask & 0x1) {
    mask = mask >> 1;
    bit_len++;
  }
  *first = bit_pos;
  *len = bit_len;
}

static grub_uint16_t
grub_console_getwh (void)
{
  grub_efi_simple_text_output_interface_t *o;
  grub_efi_uintn_t columns, rows;

  o = grub_efi_system_table->con_out;
  if (Call_Service_4 (o->query_mode , o, o->mode->mode, &columns, &rows)
      != GRUB_EFI_SUCCESS)
    {
      /* Why does this fail?  */
      columns = 80;
      rows = 25;
    }

  return ((columns << 8) | rows);
}

static void
set_video_params (struct linux_kernel_params *params)
{
  grub_efi_uintn_t size;
  grub_efi_graphics_output_t *gop_intf = NULL;
  grub_efi_graphics_output_mode_information_t *gop_info = NULL;
  grub_efi_graphics_output_mode_t *gop_mode = NULL;
  grub_efi_status_t efi_status = GRUB_EFI_SUCCESS;

  gop_intf = grub_efi_locate_protocol (&graphics_output_guid, NULL);
  if (gop_intf == NULL)
    goto fallback;
  gop_mode = gop_intf->mode;
  efi_status = Call_Service_4 (gop_intf->query_mode,
			       gop_intf, gop_mode->mode, &size, &gop_info);
  if (efi_status != GRUB_EFI_SUCCESS)
    goto fallback;

  /* No VBE on EFI.  */
  params->lfb_width = gop_info->horizontal_resolution;
  params->lfb_height = gop_info->vertical_resolution;
  params->lfb_base = gop_mode->frame_buffer_base;
  params->lfb_size = gop_mode->frame_buffer_size;
  params->lfb_pages = 1;
  params->vesapm_segment = 0;
  params->vesapm_offset = 0;
  params->vesa_attrib = 0;
  if (gop_info->pixel_format == GRUB_EFI_PIXEL_RGBR_8BIT_PER_COLOR)
    {
      params->lfb_depth = 32;
      params->red_mask_size = 8;
      params->red_field_pos = 0;
      params->green_mask_size = 8;
      params->green_field_pos = 8;
      params->blue_mask_size = 8;
      params->blue_field_pos = 16;
      params->reserved_mask_size = 8;
      params->reserved_field_pos = 24;
      params->lfb_line_len = gop_info->pixels_per_scan_line * 4;
    }
  else if (gop_info->pixel_format == GRUB_EFI_PIXEL_BGRR_8BIT_PER_COLOR)
    {
      params->lfb_depth = 32;
      params->red_mask_size = 8;
      params->red_field_pos = 16;
      params->green_mask_size = 8;
      params->green_field_pos = 8;
      params->blue_mask_size = 8;
      params->blue_field_pos = 0;
      params->reserved_mask_size = 8;
      params->reserved_field_pos = 24;
      params->lfb_line_len = gop_info->pixels_per_scan_line * 4;
    }
  else if (gop_info->pixel_format == GRUB_EFI_PIXEL_BIT_MASK)
    {
      find_bits (gop_info->pixel_information.red_mask,
		 &params->red_field_pos, &params->red_mask_size);
      find_bits (gop_info->pixel_information.green_mask,
		 &params->green_field_pos, &params->green_mask_size);
      find_bits (gop_info->pixel_information.blue_mask,
		 &params->blue_field_pos, &params->blue_mask_size);
      find_bits (gop_info->pixel_information.reserved_mask,
		 &params->reserved_field_pos, &params->reserved_mask_size);
      params->lfb_depth = params->red_mask_size + params->green_mask_size +
	params->blue_mask_size + params->reserved_mask_size;
      params->lfb_line_len = (gop_info->pixels_per_scan_line * params->lfb_depth) / 8;
    }
  else
    {
      params->lfb_depth = 4;
      params->red_mask_size = 0;
      params->red_field_pos = 0;
      params->green_mask_size = 0;
      params->green_field_pos = 0;
      params->blue_mask_size = 0;
      params->blue_field_pos = 0;
      params->reserved_mask_size = 0;
      params->reserved_field_pos = 0;
      params->lfb_line_len = params->lfb_width / 2;
    }
#if 0
  params->video_cursor_x = 0;
  params->video_cursor_y = 0;
  params->video_page = 0;
  params->video_mode = 0;
  params->video_width = 0;
  params->video_ega_bx = 0;
  params->video_height = 0;
  params->have_vga = 0x70;
  params->font_size = 0;
#else
  params->video_cursor_x = grub_efi_system_table->con_out->mode->cursor_column;
  params->video_cursor_y = grub_efi_system_table->con_out->mode->cursor_row;
  params->video_page = 0; /* ??? */
  params->video_mode = grub_efi_system_table->con_out->mode->mode;
  params->video_width = (grub_console_getwh () >> 8);
  params->video_ega_bx = 0;
  params->video_height = (grub_console_getwh () & 0xff);
  params->have_vga = 0;
  params->font_size = 16; /* XXX */
#endif
  return;

 fallback:
  params->video_cursor_x = grub_efi_system_table->con_out->mode->cursor_column;
  params->video_cursor_y = grub_efi_system_table->con_out->mode->cursor_row;
  params->video_page = 0; /* ??? */
  params->video_mode = grub_efi_system_table->con_out->mode->mode;
  params->video_width = (grub_console_getwh () >> 8);
  params->video_ega_bx = 0;
  params->video_height = (grub_console_getwh () & 0xff);
  params->have_vga = 0;
  params->font_size = 16; /* XXX */

  /* No VBE.  */
  params->lfb_width = 0;
  params->lfb_height = 0;
  params->lfb_depth = 0;
  params->lfb_base = 0;
  params->lfb_size = 0;
  params->lfb_line_len = 0;
  params->red_mask_size = 0;
  params->red_field_pos = 0;
  params->green_mask_size = 0;
  params->green_field_pos = 0;
  params->blue_mask_size = 0;
  params->blue_field_pos = 0;
  params->reserved_mask_size = 0;
  params->reserved_field_pos = 0;
  params->vesapm_segment = 0;
  params->vesapm_offset = 0;
  params->lfb_pages = 0;
  params->vesa_attrib = 0;
}

/* do some funky stuff, then boot linux */
void
linux_boot (void)
{
  grub_printf ("zImage is not supported under EFI.\n");
  for (;;);
}

void
big_linux_boot (void)
{
  struct linux_kernel_params *params;
  struct grub_linux_kernel_header *lh;
  grub_efi_uintn_t mmap_size;
  grub_efi_uintn_t map_key;
  grub_efi_uintn_t desc_size;
  grub_efi_uint32_t desc_version;
  int e820_nr_map;

  params = real_mode_mem;

  set_video_params (params);

  mmap_size = find_mmap_size ();
  if (grub_efi_get_memory_map (&mmap_size, mmap_buf, &map_key,
			       &desc_size, &desc_version) <= 0)
    grub_fatal ("cannot get memory map");

  /* Pass e820 memmap. */
  e820_map_from_efi_map ((struct e820_entry *) params->e820_map, &e820_nr_map,
			 mmap_buf, desc_size, mmap_size);
  params->e820_nr_map = e820_nr_map;

  if (! grub_efi_exit_boot_services (map_key))
    grub_fatal ("cannot exit boot services");

  /* Note that no boot services are available from here.  */
  memcpy ((void *) 0x700, switch_image, switch_size);

  lh = &params->hdr;
  /* Pass EFI parameters.  */
  if (grub_le_to_cpu16 (lh->version) >= 0x0206) {
    params->version_0206.efi_mem_desc_size = desc_size;
    params->version_0206.efi_mem_desc_version = desc_version;
    params->version_0206.efi_mmap = (grub_uint32_t) (unsigned long) mmap_buf;
    params->version_0206.efi_mmap_size = mmap_size;
  } else if (grub_le_to_cpu16 (lh->version) >= 0x0204) {
    params->version_0204.efi_mem_desc_size = desc_size;
    params->version_0204.efi_mem_desc_version = desc_version;
    params->version_0204.efi_mmap = (grub_uint32_t) (unsigned long) mmap_buf;
    params->version_0204.efi_mmap_size = mmap_size;
  } else /* dunno */ {
    params->dunno.efi_mem_desc_size = desc_size;
    params->dunno.efi_mem_desc_version = desc_version;
    params->dunno.efi_mmap = (grub_uint32_t) (unsigned long) mmap_buf;
    params->dunno.efi_mmap_size = mmap_size;
    params->dunno.efi_mmap_hi = PTR_HI(mmap_buf);
  }

#ifdef __x86_64__
  /* Pass parameters.  */
  asm volatile ("mov %0, %%rsi" : : "m" (real_mode_mem));
  asm volatile ("movl %0, %%ebx" : : "m" (params->hdr.code32_start));

  /* Enter Linux, switch from 64-bit long mode
   * to 32-bit protect mode, this code end address
   * must not exceed 0x1000, because linux kernel bootstrap
   * code will flush this area
   */
  asm volatile ( "mov $0x700, %%rdi" : :);
  asm volatile ( "jmp *%%rdi" : :);
#else
  /* Pass parameters.  */
  asm volatile ("mov %0, %%esi" : : "m" (real_mode_mem));
  asm volatile ("movl %0, %%ebx" : : "m" (params->hdr.code32_start));

  /* Enter Linux, switch from 64-bit long mode
   * to 32-bit protect mode, this code end address
   * must not exceed 0x1000, because linux kernel bootstrap
   * code will flush this area
   */
  asm volatile ( "mov $0x700, %%edi" : :);
  asm volatile ( "jmp *%%edi" : :);
#endif

  /* Never reach here.  */
  for (;;);
}

int
grub_load_linux (char *kernel, char *arg)
{
  struct grub_linux_kernel_header *lh;
  struct linux_kernel_params *params;
  static struct linux_kernel_params params_buf;
  grub_uint8_t setup_sects;
  grub_size_t real_size, prot_size;
  grub_ssize_t len;
  char *dest;

  if (kernel == NULL)
    {
      errnum = ERR_BAD_FILENAME;
      grub_printf ("no kernel specified");
      goto fail1;
    }

  if (! grub_open (kernel))
    goto fail1;

  if (grub_read ((char *) &params_buf, sizeof (params_buf))
      != sizeof (params_buf))
    {
      errnum = ERR_EXEC_FORMAT;
      grub_printf ("cannot read the linux header");
      goto fail;
    }

  lh = &params_buf.hdr;

  if (lh->boot_flag != grub_cpu_to_le16 (0xaa55))
    {
      errnum = ERR_EXEC_FORMAT;
      grub_printf ("invalid magic number: %x", lh->boot_flag);
      goto fail;
    }

  /* EFI support is quite new, so reject old versions.  */
  if (lh->header != grub_cpu_to_le32 (GRUB_LINUX_MAGIC_SIGNATURE)
      || grub_le_to_cpu16 (lh->version) < 0x0203)
    {
      errnum = ERR_EXEC_FORMAT;
      grub_printf ("too old version");
      goto fail;
    }

  /* I'm not sure how to support zImage on EFI.  */
  if (! (lh->loadflags & GRUB_LINUX_FLAG_BIG_KERNEL))
    {
      errnum = ERR_EXEC_FORMAT;
      grub_printf ("zImage is not supported");
      goto fail;
    }

  setup_sects = lh->setup_sects;

  real_size = 0x1000 + grub_strlen(arg);
  prot_size = grub_file_size () - (setup_sects << SECTOR_BITS) - SECTOR_SIZE;

  if (! allocate_pages (real_size, prot_size))
    goto fail;

  /* XXX Linux assumes that only elilo can boot Linux on EFI!!!  */
  lh->type_of_loader = 0x50;

  lh->cmd_line_ptr = (grub_uint32_t) (unsigned long) real_mode_mem + 0x1000;
  lh->ramdisk_image = 0;
  lh->ramdisk_size = 0;

  grub_memset(real_mode_mem, 0, real_size);

  params = (struct linux_kernel_params *) real_mode_mem;

  grub_memmove(&params->hdr, lh, 0x202 + lh->jump_off - 0x1f1);

  params->cl_magic = GRUB_LINUX_CL_MAGIC;
  params->cl_offset = 0x1000;

  /* These are not needed to be precise, because Linux uses these values
     only to raise an error when the decompression code cannot find good
     space.  */
  params->ext_mem = ((32 * 0x100000) >> 10);
  params->alt_mem = ((32 * 0x100000) >> 10);

  /* No APM on EFI.  */
  params->apm_version = 0;
  params->apm_code_segment = 0;
  params->apm_entry = 0;
  params->apm_16bit_code_segment = 0;
  params->apm_data_segment = 0;
  params->apm_flags = 0;
  params->apm_code_len = 0;
  params->apm_data_len = 0;

  /* XXX is there any way to use SpeedStep on EFI?  */
  params->ist_signature = 0;
  params->ist_command = 0;
  params->ist_event = 0;
  params->ist_perf_level = 0;

  /* Let the kernel probe the information.  */
  grub_memset (params->hd0_drive_info, 0, sizeof (params->hd0_drive_info));
  grub_memset (params->hd1_drive_info, 0, sizeof (params->hd1_drive_info));

  /* No MCA on EFI.  */
  params->rom_config_len = 0;

  if (grub_le_to_cpu16 (lh->version) >= 0x0206) {
    params->version_0206.efi_system_table = \
                        (grub_uint32_t) (unsigned long) grub_efi_system_table;
  } else if (grub_le_to_cpu16 (lh->version) >= 0x0204) {
    grub_memcpy(&params->version_0204.efi_signature, "EFIL", 4);
    params->version_0204.efi_system_table = \
                        (grub_uint32_t) (unsigned long) grub_efi_system_table;
  } else /* dunno */ {
    params->dunno.efi_signature = GRUB_LINUX_EFI_SIGNATURE_X64;
    params->dunno.efi_system_table = \
                        (grub_uint32_t) (unsigned long) grub_efi_system_table;
    params->dunno.efi_system_table_hi = PTR_HI(grub_efi_system_table);
  }
  /* The other EFI parameters are filled when booting.  */

  /* No EDD */
  params->eddbuf_entries = 0;
  params->edd_mbr_sig_buf_entries = 0;

  /* XXX there is no way to know if the kernel really supports EFI.  */
  grub_printf ("[Linux-EFI, setup=0x%x, size=0x%x]\n", (unsigned int)real_size,
	       (unsigned int)prot_size);

  /* Check the mem= option to limit memory used for initrd.  */
  {
    char *mem;

    mem = grub_strstr (arg, "mem=");
    if (mem)
      {
	char *value = mem + 4;

	safe_parse_maxulong (&value, &linux_mem_size);
	switch (errnum)
	  {
	  case ERR_NUMBER_OVERFLOW:
	    /* If an overflow occurs, use the maximum address for
	       initrd instead. This is good, because MAXINT is
	       greater than LINUX_INITRD_MAX_ADDRESS.  */
	    linux_mem_size = LINUX_INITRD_MAX_ADDRESS;
	    errnum = ERR_NONE;
	    break;

	  case ERR_NONE:
	    {
	      int shift = 0;

	      switch (grub_tolower (*value))
		{
		case 'g':
		  shift += 10;
		case 'm':
		  shift += 10;
		case 'k':
		  shift += 10;
		default:
		  break;
		}

	      /* Check an overflow.  */
	      if (linux_mem_size > (~0UL >> shift))
		linux_mem_size = 0;
	      else
		linux_mem_size <<= shift;
	    }
	    break;

	  default:
	    linux_mem_size = 0;
	    errnum = ERR_NONE;
	    break;
	  }
      }
    else
      linux_mem_size = 0;
  }

  dest = grub_stpcpy ((char *) real_mode_mem + 0x1000, skip_to(0, arg));

  grub_seek ((setup_sects << SECTOR_BITS) + SECTOR_SIZE);
  len = prot_size;
  if (grub_read ((char *) GRUB_LINUX_BZIMAGE_ADDR, len) != len)
    grub_printf ("Couldn't read file");

  if (errnum == ERR_NONE)
    {
      loaded = 1;
    }

 fail:

  grub_close ();

 fail1:

  if (errnum != ERR_NONE)
    {
      loaded = 0;
    }
  return errnum ? KERNEL_TYPE_NONE : KERNEL_TYPE_BIG_LINUX;
}

int
grub_load_initrd (char *initrd)
{
  grub_ssize_t size;
  grub_addr_t addr_min, addr_max;
  grub_addr_t addr;
  grub_efi_uintn_t mmap_size;
  grub_efi_memory_descriptor_t *desc;
  grub_efi_memory_descriptor_t tdesc;
  grub_efi_uintn_t desc_size;
  struct linux_kernel_params *params;

  if (initrd == NULL)
    {
      errnum = ERR_BAD_FILENAME;
      grub_printf ("No module specified");
      goto fail1;
    }

  if (! loaded)
    {
      errnum = ERR_UNRECOGNIZED;
      grub_printf ("You need to load the kernel first.");
      goto fail1;
    }

  if (! grub_open (initrd))
    goto fail1;

  size = grub_file_size ();
  initrd_pages = (page_align (size) >> 12);

  params = (struct linux_kernel_params *) real_mode_mem;
  grub_dprintf(__func__, "initrd_pages: %lu\n", initrd_pages);

  addr_max = grub_cpu_to_le32 (params->hdr.initrd_addr_max);
  if (linux_mem_size != 0 && linux_mem_size < addr_max)
    addr_max = linux_mem_size;
  addr_max &= ~((1 << 12)-1);

  /* Linux 2.3.xx has a bug in the memory range check, so avoid
     the last page.
     Linux 2.2.xx has a bug in the memory range check, which is
     worse than that of Linux 2.3.xx, so avoid the last 64kb.  */
  //addr_max -= 0x10000;

  /* Usually, the compression ratio is about 50%.  */
  addr_min = (grub_addr_t) prot_mode_mem + ((prot_mode_pages * 3) << 12);
  grub_dprintf(__func__, "prot_mode_mem=%p prot_mode_pages=%lu\n", prot_mode_mem, prot_mode_pages);

  /* Find the highest address to put the initrd.  */
  mmap_size = find_mmap_size ();
  grub_dprintf(__func__, "addr_min: 0x%lx addr_max: 0x%lx mmap_size: %lu\n", addr_min, addr_max, mmap_size);
  if (grub_efi_get_memory_map (&mmap_size, mmap_buf, 0, &desc_size, 0) <= 0)
    grub_fatal ("cannot get memory map");

  addr = 0;
  for (desc = mmap_buf;
       desc < NEXT_MEMORY_DESCRIPTOR (mmap_buf, mmap_size);
       desc = NEXT_MEMORY_DESCRIPTOR (desc, desc_size))
    {
      if (desc->type != GRUB_EFI_CONVENTIONAL_MEMORY)
        continue;
      memcpy(&tdesc, desc, sizeof (tdesc));
      if (tdesc.physical_start < addr_min
              && tdesc.num_pages > ((addr_min - tdesc.physical_start) >> 12))
        {
          tdesc.num_pages -= ((addr_min - tdesc.physical_start) >> 12);
          tdesc.physical_start = addr_min;
        }

      grub_dprintf(__func__, "desc = {type=%d,ps=0x%llx,vs=0x%llx,sz=%llu,attr=%llu}\n", desc->type, (unsigned long long)desc->physical_start, (unsigned long long)desc->virtual_start, (unsigned long long)desc->num_pages, (unsigned long long)desc->attribute);
      if (tdesc.physical_start >= addr_min
	  && tdesc.physical_start + page_align (size) <= addr_max
	  && tdesc.num_pages >= initrd_pages)
	{
	  grub_efi_physical_address_t physical_end;

	  physical_end = tdesc.physical_start + (tdesc.num_pages << 12);
	  if (physical_end > addr_max)
	    physical_end = addr_max;

	  if (physical_end <= 0x7fffffffUL && physical_end > addr)
	    addr = physical_end - page_align (size);
	}
    }

  if (addr == 0)
    {
      errnum = ERR_UNRECOGNIZED;
      grub_printf ("no free pages available");
      goto fail;
    }

  initrd_mem = grub_efi_allocate_pages (addr, initrd_pages);
  if (! initrd_mem)
    grub_fatal ("cannot allocate pages: %x@%x", (unsigned)initrd_pages,
		(unsigned)addr);

  if (grub_read (initrd_mem, size) != size)
    {
      grub_printf ("Couldn't read file");
      goto fail;
    }

  grub_printf ("   [Initrd, addr=0x%x, size=0x%x]\n", (unsigned int) addr,
	       (unsigned int) size);

  params->hdr.ramdisk_image = addr;
  params->hdr.ramdisk_size = size;
  params->hdr.root_dev = 0x0100; /* XXX */

 fail:
  grub_close ();
 fail1:
  return !errnum;
}
