/* efigraph.c - graphics mode support for GRUB/EFI */
/* Implemented as a terminal type by Jeremy Katz <katzj@redhat.com> based
 * on a patch by Paulo César Pereira de Andrade <pcpa@conectiva.com.br>
 */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2007 Intel Corp.
 *  Copyright (C) 2001,2002  Red Hat, Inc.
 *  Portions copyright (C) 2000  Conectiva, Inc.
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

#ifdef SUPPORT_GRAPHICS

#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/misc.h>

#include <term.h>
#include <shared.h>
#include <graphics.h>

struct grub_pixel_info
{
  char depth_bits;
  char depth_bytes;
  unsigned char red_size;
  unsigned char red_pos;
  unsigned char green_size;
  unsigned char green_pos;
  unsigned char blue_size;
  unsigned char blue_pos;
  unsigned char reserved_size;
  unsigned char reserved_pos;
  int line_length;
};
typedef struct grub_pixel_info grub_pixel_info_t;

int saved_videomode;
extern const unsigned char font8x16[];

int graphics_inited = 0;
static char splashimage[64];

/* constants to define the viewable area */
const int x0 = 0;
const int x1 = 80;
const int y0 = 0;
const int y1 = 30;

/* text buffer has to be kept around so that we can write things as we
 * scroll and the like */
unsigned short text[80 * 30];

/* why do these have to be kept here? */
int foreground = (63 << 16) | (63 << 8) | (63), background = 0, border = 0;

/* current position */
static int fontx = 0;
static int fonty = 0;

/* global state so that we don't try to recursively scroll or cursor */
static int no_scroll = 0;

/* color state */
static int graphics_standard_color = A_NORMAL;
static int graphics_normal_color = A_NORMAL;
static int graphics_highlight_color = A_REVERSE;
static int graphics_current_color = A_NORMAL;
static color_state graphics_color_state = COLOR_STATE_STANDARD;

static grub_efi_guid_t graphics_output_guid = GRUB_EFI_GRAPHICS_OUTPUT_GUID;

static grub_efi_graphics_output_t *gop_intf = NULL;
static grub_efi_graphics_output_mode_information_t gop_mode_info;
static grub_pixel_info_t gop_pixel_info;
static int gop_mode_num = -1;
static int saved_gop_mode_num = -1;

static int palette[0x11];

unsigned char *bmp_bg;

/* graphics local functions */
static void graphics_setxy (int col, int row);
static void graphics_scroll (void);

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

static int
fill_pixel_info (grub_pixel_info_t *pixel_info,
		 grub_efi_graphics_output_mode_information_t *mode_info)
{
  if (mode_info->pixel_format == GRUB_EFI_PIXEL_RGBR_8BIT_PER_COLOR)
    {
      pixel_info->depth_bits = 32;
      pixel_info->depth_bytes = 4;
      pixel_info->red_size = 8;
      pixel_info->red_pos = 0;
      pixel_info->green_size = 8;
      pixel_info->green_pos = 8;
      pixel_info->blue_size = 8;
      pixel_info->blue_pos = 16;
      pixel_info->reserved_size = 8;
      pixel_info->reserved_pos = 24;
      pixel_info->line_length = mode_info->pixels_per_scan_line * 4;
    }
  else if (mode_info->pixel_format == GRUB_EFI_PIXEL_BGRR_8BIT_PER_COLOR)
    {
      pixel_info->depth_bits = 32;
      pixel_info->depth_bytes = 4;
      pixel_info->red_size = 8;
      pixel_info->red_pos = 16;
      pixel_info->green_size = 8;
      pixel_info->green_pos = 8;
      pixel_info->blue_size = 8;
      pixel_info->blue_pos = 0;
      pixel_info->reserved_size = 8;
      pixel_info->reserved_pos = 24;
      pixel_info->line_length = mode_info->pixels_per_scan_line * 4;
    }
  else if (mode_info->pixel_format == GRUB_EFI_PIXEL_BIT_MASK)
    {
      find_bits (mode_info->pixel_information.red_mask,
		 &pixel_info->red_pos, &pixel_info->red_size);
      find_bits (mode_info->pixel_information.green_mask,
		 &pixel_info->green_pos, &pixel_info->green_size);
      find_bits (mode_info->pixel_information.blue_mask,
		 &pixel_info->blue_pos, &pixel_info->blue_size);
      find_bits (mode_info->pixel_information.reserved_mask,
		 &pixel_info->reserved_pos, &pixel_info->reserved_size);
      pixel_info->depth_bits = pixel_info->red_size + pixel_info->green_size +
	pixel_info->blue_size + pixel_info->reserved_size;
      pixel_info->depth_bytes = (pixel_info->depth_bits + 7) / 8;
      pixel_info->line_length = mode_info->pixels_per_scan_line *
	pixel_info->depth_bytes;
    }
  else
    return 0;
  return 1;
}

static int
search_videomode (void)
{
  grub_efi_graphics_output_mode_information_t *gop_info = NULL;
  grub_efi_status_t efi_status = GRUB_EFI_SUCCESS;
  grub_efi_uintn_t size;
  int i;

  if (gop_mode_num != -1)
    return 1;
  gop_intf = grub_efi_locate_protocol (&graphics_output_guid, NULL);
  if (gop_intf == NULL)
    {
      return 0;
    }
  for (i = 0; i < gop_intf->mode->max_mode; i++)
    {
      efi_status = Call_Service_4 (gop_intf->query_mode,
				   gop_intf, i, &size, &gop_info);
      if (efi_status != GRUB_EFI_SUCCESS)
	{
	  return 0;
	}
      if (gop_info->horizontal_resolution == 640 &&
	  gop_info->vertical_resolution == 480 &&
	  (gop_info->pixel_format == GRUB_EFI_PIXEL_RGBR_8BIT_PER_COLOR ||
	   gop_info->pixel_format == GRUB_EFI_PIXEL_BGRR_8BIT_PER_COLOR ||
	   gop_info->pixel_format == GRUB_EFI_PIXEL_BIT_MASK))
	{
	  if (! fill_pixel_info(&gop_pixel_info, gop_info) ||
	      gop_pixel_info.depth_bits < 8)
	    continue;
	  gop_mode_info = *gop_info;
	  gop_mode_num = i;
	  break;
	}
    }
  if (gop_mode_num == -1)
    {
      return 0;
    }

  return 1;
}

static int
efi_set_videomode (grub_efi_graphics_output_t *intf, int mode_num)
{
  grub_efi_status_t efi_status = GRUB_EFI_SUCCESS;

  efi_status = Call_Service_2 (intf->set_mode, intf,
			       mode_num);
  return efi_status == GRUB_EFI_SUCCESS ? 0 : -1;
}

int
set_videomode (int mode)
{
  if (mode)
    {
      if (!search_videomode ())
	return -1;
      if (!grub_efi_set_text_mode (0))
	{
	  return -1;
	}
      saved_gop_mode_num = gop_intf->mode->mode;
      if (efi_set_videomode (gop_intf, gop_mode_num))
	{
	  grub_efi_set_text_mode (1);
	  saved_gop_mode_num = -1;
	  return -1;
	}
      return 0;
    }
  else
    {
      if (saved_gop_mode_num != -1)
	{
	  efi_set_videomode (gop_intf, saved_gop_mode_num);
	  saved_gop_mode_num = -1;
	}
      grub_efi_set_text_mode (1);
      return 0;
    }
}

void
graphics_set_palette (int idx, int red, int green, int blue)
{
  red &= 0x3f;
  green &= 0x3f;
  blue &= 0x3f;
  red = red * ((1 << gop_pixel_info.red_size) - 1) / 0x3f;
  green = green * ((1 << gop_pixel_info.green_size) - 1) / 0x3f;
  blue = blue * ((1 << gop_pixel_info.blue_size) - 1) / 0x3f;
  palette[idx] = (red << gop_pixel_info.red_pos) |
    (green << gop_pixel_info.green_pos) | (blue << gop_pixel_info.blue_pos);
}

static void
draw_pixel (int x, int y, int clr)
{
  char *fb = (char *) gop_intf->mode->frame_buffer_base;
  char depth_bytes = gop_pixel_info.depth_bytes;
  char *pal = (char *) &palette[clr];

  fb += y * gop_pixel_info.line_length + x * depth_bytes;
  while (depth_bytes--)
    *fb++ = *pal++;
}

static void
bg_bmp_set_pixel (int x, int y, int clr)
{
  int c, pos;

  pos = (y * 640 + x) / 2;
  c = bmp_bg[pos];
  if (x & 1)
    bmp_bg[pos] = (c & 0xf) | (clr << 4);
  else
    bmp_bg[pos] = (c & 0xf0) | clr;
}

static int
bg_bmp_get_pixel (int x, int y)
{
  int c, pos;

  pos = (y * 640 + x) / 2;
  c = bmp_bg[pos];
  if (x & 1)
    return c >> 4;
  else
    return c & 0xf;
}

/* Set the splash image */
void
graphics_set_splash (char *splashfile)
{
  grub_strcpy (splashimage, splashfile);
}

/* Get the current splash image */
char *
graphics_get_splash (void)
{
  return splashimage;
}

/* Initialize a vga16 graphics display with the palette based off of
 * the image in splashimage.  If the image doesn't exist, leave graphics
 * mode.  */
int
graphics_init ()
{
  bmp_bg = grub_malloc (640 * 480 / 2);
  if (!bmp_bg)
    {
      current_term = term_table;
      return 0;
    }
  if (!read_image (splashimage))
    {
      current_term = term_table;
      return 0;
    }

  graphics_inited = 1;

  /* make sure that the highlight color is set correctly */
  graphics_highlight_color = ((graphics_normal_color >> 4) |
			      ((graphics_normal_color & 0xf) << 4));

  return 1;
}

/* Leave graphics mode */
void
graphics_end (void)
{
  if (graphics_inited)
    {
      set_videomode (0);
      grub_free (bmp_bg);
      graphics_inited = 0;
    }
}

/* Print ch on the screen.  Handle any needed scrolling or the like */
void
graphics_putchar (int ch)
{
  ch &= 0xff;

  graphics_cursor (0);

  if (ch == '\n')
    {
      if (fonty + 1 < y1)
	graphics_setxy (fontx, fonty + 1);
      else
	graphics_scroll ();
      graphics_cursor (1);
      return;
    }
  else if (ch == '\r')
    {
      graphics_setxy (x0, fonty);
      graphics_cursor (1);
      return;
    }

  graphics_cursor (0);

  text[fonty * 80 + fontx] = ch;
  text[fonty * 80 + fontx] &= 0x00ff;
  if (graphics_current_color & 0xf0)
    text[fonty * 80 + fontx] |= 0x100;

  graphics_cursor (0);

  if ((fontx + 1) >= x1)
    {
      graphics_setxy (x0, fonty);
      if (fonty + 1 < y1)
	graphics_setxy (x0, fonty + 1);
      else
	graphics_scroll ();
    }
  else
    {
      graphics_setxy (fontx + 1, fonty);
    }

  graphics_cursor (1);
}

/* get the current location of the cursor */
int
graphics_getxy (void)
{
  return (fontx << 8) | fonty;
}

void
graphics_gotoxy (int x, int y)
{
  graphics_cursor (0);

  graphics_setxy (x, y);

  graphics_cursor (1);
}

void
graphics_cls (void)
{
  int i, j, pix;

  graphics_cursor (0);
  graphics_gotoxy (x0, y0);

  for (i = 0; i < 80 * 30; i++)
    text[i] = ' ';
  graphics_cursor (1);

  for (i = 0; i < 640; i++)
    for (j = 0; j < 480; j++)
      {
	pix = bg_bmp_get_pixel (i, j);
	draw_pixel (i, j, pix);
      }
}

void
graphics_setcolorstate (color_state state)
{
  switch (state)
    {
    case COLOR_STATE_STANDARD:
      graphics_current_color = graphics_standard_color;
      break;
    case COLOR_STATE_NORMAL:
      graphics_current_color = graphics_normal_color;
      break;
    case COLOR_STATE_HIGHLIGHT:
      graphics_current_color = graphics_highlight_color;
      break;
    default:
      graphics_current_color = graphics_standard_color;
      break;
    }

  graphics_color_state = state;
}

void
graphics_setcolor (int normal_color, int highlight_color)
{
  graphics_normal_color = normal_color;
  graphics_highlight_color = highlight_color;

  graphics_setcolorstate (graphics_color_state);
}

int
graphics_setcursor (int on)
{
  /* FIXME: we don't have a cursor in graphics */
  return 0;
}

/* Open the file, and search for a valid XPM header.  Return 1 if one is found,
 * leaving the current position as the start of the next line.  Else,
 * return 0.
 */
static int
xpm_open (char *s)
{
  char buf, prev, target[] = "/* XPM */\n";
  int pos = 0;

  if (!grub_open (s))
    return 0;

  prev = '\n';
  buf = 0;
  do
    {
      if (grub_read (&buf, 1) != 1)
	{
	  grub_close ();
	  return 0;
	}
      if ((pos == 0 && prev == '\n') || pos > 0)
	{
	  if (buf == target[pos])
	    pos++;
	  else
	    pos = 0;
	}
      prev = buf;
    }
  while (target[pos]);
  return 1;
}

/* Read in the splashscreen image and set the palette up appropriately.
 * Format of splashscreen is an xpm (can be gzipped) with 16 colors and
 * 640x480. */
int
read_image (char *s)
{
  char buf[32], pal[16];
  char c, base;
  unsigned i, len, idx, colors, x, y, width, height;

  if (! xpm_open (s))
    return 0;

  saved_videomode = set_videomode (0x12);
  if (saved_videomode == -1)
    return 0;

  /* parse info */
  while (grub_read (&c, 1))
    {
      if (c == '"')
	break;
    }

  while (grub_read (&c, 1) && (c == ' ' || c == '\t'))
    ;

  i = 0;
  width = c - '0';
  while (grub_read (&c, 1))
    {
      if (c >= '0' && c <= '9')
	width = width * 10 + c - '0';
      else
	break;
    }
  while (grub_read (&c, 1) && (c == ' ' || c == '\t'))
    ;

  height = c - '0';
  while (grub_read (&c, 1))
    {
      if (c >= '0' && c <= '9')
	height = height * 10 + c - '0';
      else
	break;
    }
  while (grub_read (&c, 1) && (c == ' ' || c == '\t'))
    ;

  colors = c - '0';
  while (grub_read (&c, 1))
    {
      if (c >= '0' && c <= '9')
	colors = colors * 10 + c - '0';
      else
	break;
    }

  base = 0;
  while (grub_read (&c, 1) && c != '"')
    ;

  /* palette */
  for (i = 0, idx = 1; i < colors; i++)
    {
      len = 0;

      while (grub_read (&c, 1) && c != '"')
	;
      grub_read (&c, 1);	/* char */
      base = c;
      grub_read (buf, 4);	/* \t c # */

      while (grub_read (&c, 1) && c != '"')
	{
	  if (len < sizeof (buf))
	    buf[len++] = c;
	}

      if (len == 6 && idx < 15)
	{
	  int r = ((hex (buf[0]) << 4) | hex (buf[1])) >> 2;
	  int g = ((hex (buf[2]) << 4) | hex (buf[3])) >> 2;
	  int b = ((hex (buf[4]) << 4) | hex (buf[5])) >> 2;

	  pal[idx] = base;
	  graphics_set_palette (idx, r, g, b);
	  ++idx;
	}
    }

  graphics_set_palette (0, (background >> 16), (background >> 8), background);
  graphics_set_palette (15, (foreground >> 16), (foreground >> 8),
			foreground);
  graphics_set_palette (0x11, (border >> 16), (border >> 8), border);

  x = y = len = 0;

  /* parse xpm data */
  while (y < height)
    {
      while (1)
	{
	  if (!grub_read (&c, 1))
	    {
	      grub_close ();
	      return 0;
	    }
	  if (c == '"')
	    break;
	}

      while (grub_read (&c, 1) && c != '"')
	{
	  for (i = 1; i < 15; i++)
	    if (pal[i] == c)
	      {
		c = i;
		break;
	      }

	  draw_pixel (x, y, c);
	  bg_bmp_set_pixel (x, y, c);

	  if (++x >= 640)
	    {
	      x = 0;

	      if (y < 480)
		len += 80;
	      ++y;
	    }
	}
    }

  grub_close ();

  return 1;
}


/* Convert a character which is a hex digit to the appropriate integer */
int
hex (int v)
{
  if (v >= 'A' && v <= 'F')
    return (v - 'A' + 10);
  if (v >= 'a' && v <= 'f')
    return (v - 'a' + 10);
  return (v - '0');
}


/* move the graphics cursor location to col, row */
static void
graphics_setxy (int col, int row)
{
  if (col >= x0 && col < x1)
    {
      fontx = col;
    }
  if (row >= y0 && row < y1)
    {
      fonty = row;
    }
}

/* scroll the screen */
static void
graphics_scroll (void)
{
  int i, j;

  /* we don't want to scroll recursively... that would be bad */
  if (no_scroll)
    return;
  no_scroll = 1;

  /* move everything up a line */
  for (j = y0 + 1; j < y1; j++)
    {
      graphics_gotoxy (x0, j - 1);
      for (i = x0; i < x1; i++)
	{
	  graphics_putchar (text[j * 80 + i]);
	}
    }

  /* last line should be blank */
  graphics_gotoxy (x0, y1 - 1);
  for (i = x0; i < x1; i++)
    graphics_putchar (' ');
  graphics_setxy (x0, y1 - 1);

  no_scroll = 0;
}


void
graphics_cursor (int set)
{
  const unsigned char *pat;
  int i, j, ch, x, y, bit, invert;

  if (set && no_scroll)
    return;

  ch = text[fonty * 80 + fontx] & 0xff;
  invert = (text[fonty * 80 + fontx] & 0xff00) != 0;
  pat = font8x16 + (ch << 4);

  for (i = 0; i < 16; i++)
    {
      for (j = 0; j < 8; j++)
	{
	  y = fonty * 16 + i;
	  x = fontx * 8 + j;
	  bit = pat[i] & (1 << (7-j));
	  if (set)
	    draw_pixel (x, y, bit ? 0 : 15);
	  else if (invert)
	    draw_pixel (x, y, bit ? 15 : 0);
	  else
	    draw_pixel (x, y, bit ? 15 : bg_bmp_get_pixel (x, y));
	}
    }
}

#endif /* SUPPORT_GRAPHICS */
