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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301, USA.
 */

#include <config.h>
#include <grub/efi/efi.h>
#include <grub/efi/time.h>
#include <grub/efi/misc.h>
#include <grub/misc.h>

#include <shared.h>

unsigned long install_partition = 0x20000;
unsigned long boot_drive = 0x80;
int saved_entryno = 0;
char version_string[] = VERSION;
char config_file[128] = "/boot/grub/menu.lst";
unsigned short io_map[IO_MAP_SIZE];
struct apm_info apm_bios_info;
/* The key map.  */
unsigned short bios_key_map[KEY_MAP_SIZE + 1];
unsigned short ascii_key_map[KEY_MAP_SIZE + 1];

void
grub_efi_init (void)
{
  /* Initialize the memory management system.  */
  grub_efi_mm_init ();
}

void
grub_efi_fini (void)
{
  grub_efi_mm_fini ();
}

void *
grub_malloc (unsigned long size)
{
  return grub_efi_allocate_pool (size);
}

void
grub_free (void *p)
{
  grub_efi_free_pool (p);
}

char *
grub_stpcpy (char *dest, const char *src)
{
  char *d = dest;
  const char *s = src;

  do
    *d++ = *s;
  while (*s++ != '\0');

  return d - 1;
}

void
grub_fatal (const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  grub_vsprintf (NULL, fmt, ap);
  va_end (ap);

  grub_exit ();
}

void
grub_real_dprintf (const char *file, const int line, const char *condition,
		   const char *fmt, ...)
{
  va_list args;

  if (! debug)
    return;

  grub_printf ("%s:%d: ", file, line);
  va_start (args, fmt);
  grub_vsprintf (0, fmt, args);
  va_end (args);
}

/* Convert UTF-16 to UTF-8.  */
grub_uint8_t *
grub_utf16_to_utf8 (grub_uint8_t *dest, grub_uint16_t *src,
		    grub_size_t size)
{
  grub_uint32_t code_high = 0;

  while (size--)
    {
      grub_uint32_t code = *src++;

      if (code_high)
	{
	  if (code >= 0xDC00 && code <= 0xDFFF)
	    {
	      /* Surrogate pair.  */
	      code = ((code_high - 0xD800) << 12) + (code - 0xDC00) + 0x10000;

	      *dest++ = (code >> 18) | 0xF0;
	      *dest++ = ((code >> 12) & 0x3F) | 0x80;
	      *dest++ = ((code >> 6) & 0x3F) | 0x80;
	      *dest++ = (code & 0x3F) | 0x80;
	    }
	  else
	    {
	      /* Error...  */
	      *dest++ = '?';
	    }

	  code_high = 0;
	}
      else
	{
	  if (code <= 0x007F)
	    *dest++ = code;
	  else if (code <= 0x07FF)
	    {
	      *dest++ = (code >> 6) | 0xC0;
	      *dest++ = (code & 0x3F) | 0x80;
	    }
	  else if (code >= 0xD800 && code <= 0xDBFF)
	    {
	      code_high = code;
	      continue;
	    }
	  else if (code >= 0xDC00 && code <= 0xDFFF)
	    {
	      /* Error... */
	      *dest++ = '?';
	    }
	  else
	    {
	      *dest++ = (code >> 16) | 0xE0;
	      *dest++ = ((code >> 12) & 0x3F) | 0x80;
	      *dest++ = (code & 0x3F) | 0x80;
	    }
	}
    }

  return dest;
}

/* low-level timing info */
int
getrtsecs (void)
{
  return grub_get_rtc() / GRUB_TICKS_PER_SECOND;
}

void
grub_reboot (void)
{
  grub_efi_runtime_services_t *r;

  r = grub_efi_system_table->runtime_services;
  Call_Service_4 (r->reset_system, GRUB_EFI_RESET_COLD,
		  GRUB_EFI_SUCCESS, 0, NULL);
  for (;;);
}

void
grub_halt (int no_apm)
{
  grub_efi_runtime_services_t *r;

  r = grub_efi_system_table->runtime_services;
  Call_Service_4 (r->reset_system, GRUB_EFI_RESET_SHUTDOWN,
		  GRUB_EFI_SUCCESS, 0, NULL);
  for (;;);
}

void
stop (void)
{
  grub_exit ();
}

/* booting a multiboot executable */
void
multi_boot (int start, int mb_info)
{
  stop ();
}

/* sets it to linear or wired A20 operation */
void
gateA20 (int linear)
{
}

/* Set up the int15 handler.  */
void
set_int15_handler (void)
{
}

/* Restore the original int15 handler.  */
void
unset_int15_handler (void)
{
}

/* Copy MAP to the drive map and set up the int13 handler.  */
void
set_int13_handler (unsigned short *map)
{
}

/* Get the ROM configuration table.  */
unsigned long
get_rom_config_table (void)
{
  return 0;
}

/* Get APM BIOS information.  */
void
get_apm_info (void)
{
  /* Nothing to do in the simulator.  */
}

void
stop_floppy (void)
{
  /* NOTUSED */
}

int
safe_parse_maxulong (char **str_ptr, unsigned long *myulong_ptr)
{
  char *ptr = *str_ptr;
  unsigned long myulong = 0;
  unsigned int mult = 10, found = 0;

  /*
   *  Is this a hex number?
   */
  if (*ptr == '0' && tolower (*(ptr + 1)) == 'x')
    {
      ptr += 2;
      mult = 16;
    }

  while (1)
    {
      /* A bit tricky. This below makes use of the equivalence:
	 (A >= B && A <= C) <=> ((A - B) <= (C - B))
	 when C > B and A is unsigned.  */
      unsigned int digit;

      digit = tolower (*ptr) - '0';
      if (digit > 9)
	{
	  digit -= 'a' - '0';
	  if (mult == 10 || digit > 5)
	    break;
	  digit += 10;
	}

      found = 1;
      if (myulong > ((~0UL - digit) / mult))
	{
	  errnum = ERR_NUMBER_OVERFLOW;
	  return 0;
	}
      myulong = (myulong * mult) + digit;
      ptr++;
    }

  if (!found)
    {
      errnum = ERR_NUMBER_PARSING;
      return 0;
    }

  *str_ptr = ptr;
  *myulong_ptr = myulong;

  return 1;
}

char *
grub_strchr (const char *s, int c)
{
  while (*s)
    {
      if (*s == c)
	return (char *) s;
      s++;
    }

  return 0;
}

char *
grub_strrchr (const char *s, int c)
{
  char *p = 0;

  while (*s)
    {
      if (*s == c)
	p = (char *) s;
      s++;
    }

  return p;
}

int
currticks (void)
{
  return grub_get_rtc ();
}
