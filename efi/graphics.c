#ifdef SUPPORT_GRAPHICS

#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/misc.h>

#include <term.h>
#include <shared.h>
#include <graphics.h>

#include "graphics.h"

int foreground = 0x00ffffff, background = 0; 
int graphics_inited = 0;

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



extern struct graphics_backend uga_backend;
extern struct graphics_backend eg_backend;

static struct graphics_backend *backends[] = {
    &eg_backend,
    &uga_backend,
    NULL
};
static struct graphics_backend *backend;

struct graphics {
    int scroll;
    int fontx;
    int fonty;

    int standard_color;
    int normal_color;
    int highlight_color;
    int current_color;
    color_state color_state;

    char splashpath[64];
    struct xpm *splashimage;

    unsigned short *text;
};

int
graphics_alloc_text_buf(void)
{
    if (backend) {
        struct graphics *graphics = backend->graphics;
        position_t screen_size;

        if (graphics->text) {
            grub_free(graphics->text);
            graphics->text = NULL;
        }
        graphics_get_screen_rowscols(&screen_size);
        graphics->text = grub_malloc(screen_size.x
                                     * screen_size.y
                                     * sizeof (graphics->text[0]));
        if (graphics->text) {
            int x, y;
            for (y = 0; y < screen_size.y; y++)
                for (x = 0; x < screen_size.x; x++)
                    graphics->text[y * screen_size.x + x] = ' ';
            return 1;
        }
    }
    return 0;
}

unsigned short *
graphics_get_text_buf(void)
{
    return backend ? backend->graphics->text : NULL;
    return NULL;
}

int
graphics_get_screen_rowscols(position_t *size)
{
#if 0
    if (backend && backend->get_screen_size) {
        backend->get_screen_size(backend, size);
        size->x >>= 3;
        size->y >>= 4;
        return 1;
    }
    size->x = -1;
    size->y = -1;
    return 0;
#else
    size->x = 80;
    size->y = 30;
    return 1;
#endif
}

int
graphics_get_scroll(void)
{
    return backend ? backend->graphics->scroll : 1;
}

void
graphics_set_palette(int idx, int red, int green, int blue)
{
    if (backend && backend->set_palette) {
        backend->set_palette(backend, idx, red, green, blue);
	backend->set_palette(backend, 0, 0x00, 0x00, 0x00);
	backend->set_palette(backend, 15, 0xff, 0xff, 0xff);
	backend->set_palette(backend, 16, 0x00, 0x00, 0x00);
    }
}

static char splashpath[64] = "";

static void
graphics_set_splash_helper(void)
{
    if (backend) {
        struct xpm *xpm = NULL;

        if (backend->graphics->splashimage)
            xpm_free(backend->graphics->splashimage);

        if (splashpath[0])
            xpm = xpm_open(splashpath);

        if (xpm) {
            backend->graphics->splashimage = xpm;
	    grub_strcpy(backend->graphics->splashpath, splashpath);
        } else {
            backend->graphics->splashimage = NULL;
            backend->graphics->splashpath[0] = '\0';
        }
        backend->reset_screen_geometry(backend);
    }
}

void
graphics_set_splash(char *s)
{
    if (s && s[0] != '\0')
        grub_strcpy(splashpath, s);
    else
        splashpath[0] = '\0';
    graphics_set_splash_helper();
}

char *
graphics_get_splash(void)
{
    if (backend)
        return backend->graphics->splashpath;
    if (splashpath[0] != '\0')
        return splashpath;
    return NULL;
}

struct xpm *
graphics_get_splash_xpm(void)
{
    if (backend)
        return backend->graphics->splashimage;
    return NULL;
}

void
graphics_putchar(int ch)
{
    struct graphics *graphics;
    position_t screensz;
    int offset;

    void *old_term = current_term;

    if (!backend)
        return;

    graphics = backend->graphics;
    graphics_get_screen_rowscols(&screensz);

    ch &= 0xff;

    graphics_cursor(0);

    if (ch == '\n') {
        if (graphics->fonty + 1 < screensz.y) {
            graphics_setxy(graphics->fontx, graphics->fonty + 1);
        } else
            graphics_scroll();
        graphics_cursor(1);
        return;
    } else if (ch == '\r') {
        graphics_setxy(0, graphics->fonty);
        graphics_cursor(1);
        return;
    }

    graphics_cursor(0);

    offset = graphics->fontx + graphics->fonty * screensz.x;
    graphics->text[offset] = ch;
    graphics->text[offset] &= 0x00ff;
    if (graphics->current_color & 0xf0)
        graphics->text[offset] |= 0x100;

    graphics_cursor(0);

    if ((graphics->fontx + 1) >= screensz.x) {
        graphics_setxy(0, graphics->fonty);
        if (graphics->fonty + 1 < screensz.y)
            graphics_setxy(0, graphics->fonty + 1);
        else
            graphics_scroll();
    } else {
        graphics_setxy(graphics->fontx + 1, graphics->fonty);
    }

    graphics_cursor(1);
}

void
graphics_set_font_position(position_t *pos)
{
    if (backend) {
        backend->graphics->fontx = pos->x;
        backend->graphics->fonty = pos->y;
    }
}

void
graphics_get_font_position(position_t *pos)
{
    if (backend) {
        pos->x = backend->graphics->fontx;
        pos->y = backend->graphics->fonty;
        return;
    }
    pos->x = -1;
    pos->y = -1;
}

void
graphics_get_font_size(position_t *size)
{
    size->x = 8;
    size->y = 16;
}

void
graphics_setxy(int x, int y)
{
    if (!backend)
        return;

    if (backend->setxy) {
        position_t pos = {x, y};
        backend->setxy(backend, &pos);
        return;
    }
}

int
graphics_getxy(void)
{
    int ret = 0;
    if (backend) {
        if (backend->getxy) {
            position_t pos;
            backend->getxy(backend, &pos);
            ret = ((pos.x & 0xff) << 8) | (pos.y & 0xff);
        } else {
            ret = ((backend->graphics->fontx & 0xff) << 8) | (backend->graphics->fonty & 0xff);
        }
    }
    return ret;
}

void
graphics_scroll (void)
{
    struct graphics *graphics;
    position_t screensz;
    int i;
    unsigned short *text;
    int linesz;

    if (!backend)
        return;

    graphics = backend->graphics;

    /* we don't want to scroll recursively... that would be bad */
    if (!graphics->scroll)
        return;
    graphics->scroll = 0;
    graphics_get_screen_rowscols(&screensz);

    text = graphics_get_text_buf();
    linesz = screensz.x * sizeof (text[0]);
    for (i = 1; i < screensz.y; i++) {
        unsigned short *prev = &text[(i-1) * screensz.x];
        unsigned short *this = prev + screensz.x;

        memmove(prev, this, linesz);
        prev = this;
        this += screensz.x;
    }
    graphics_setxy(0, screensz.y - 1);
    for (i = 0; i < screensz.x; i++) {
        graphics_putchar(' ');
        //text[(screensz.y-1) + i] = ' ';
    }

    graphics_clbl(0, 0, screensz.x, screensz.y, 1);
    graphics_setxy(0, screensz.y - 1);
    graphics->scroll = 1;
}

void
graphics_cursor(int set)
{
    if (backend && backend->cursor)
        backend->cursor(backend, set);
}

void
graphics_gotoxy(int x, int y)
{
    position_t pos = {x,y};
    if (backend) {
        if (backend->gotoxy) {
            backend->gotoxy(backend, &pos);
        } else {
            graphics_cursor(0);
            graphics_setxy(x, y);
            graphics_cursor(1);
        }
    }
}

void
graphics_cls(void)
{
    int x = 0, y = 0, xy;
    unsigned short *text;
    position_t screensz;

    graphics_get_screen_rowscols(&screensz);
    text = graphics_get_text_buf();
    for (y = 0; y < screensz.y; y++)
            for (x = 0; x < screensz.x; x++)
                text[x + y * screensz.x] = ' ';

    if (backend) {
        //int xy = graphics_getxy();

        graphics_clbl(0, 0, screensz.x, screensz.y, 0);
        //graphics_gotoxy(xy & 0xf0 >> 8, xy & 0xf);
        graphics_gotoxy(0, 0);
    }
}

void
graphics_clbl(int col, int row, int width, int height, int draw_text)
{
    if (backend) {
        int x = 0, y = 0, xy;
        unsigned short *text;
        position_t screensz;

        graphics_get_screen_rowscols(&screensz);

#if 0
        xy = graphics_getxy();
#endif
        if (backend->clbl) {
            backend->clbl(backend, col, row, width, height, draw_text);
        } else {
            for (y = 0; y < screensz.y; y++) {
                for (x = 0; x < screensz.x; x++) {
                    graphics_setxy(x, y);
                    graphics_cursor(0);
                }
            }
        }
#if 0
        graphics_gotoxy(xy >> 8, xy & 0xf);
#endif
    }
}

void
graphics_setcolorstate (color_state state)
{
    struct graphics *graphics;
    if (!backend)
        return;

    graphics = backend->graphics;
    
    switch (state) {
        case COLOR_STATE_STANDARD:
            graphics->current_color = graphics->standard_color;
            break;
        case COLOR_STATE_NORMAL:
            graphics->current_color = graphics->normal_color;
            break;
        case COLOR_STATE_HIGHLIGHT:
            graphics->current_color = graphics->highlight_color;
            break;
        default:
            graphics->current_color = graphics->standard_color;
            break;
    }

    graphics->color_state = state;
}

void
graphics_setcolor (int normal_color, int highlight_color)
{
    backend->graphics->normal_color = normal_color;
    backend->graphics->highlight_color = highlight_color;

    graphics_setcolorstate (backend->graphics->color_state);
}

int 
graphics_setcursor(int on)
{
    /* FIXME: we don't have a cursor in graphics */
    return 0;
}

int
graphics_init(void)
{
    struct graphics *graphics;
    int i;

    if (graphics_inited)
        return 1;

    if (backend) {
        if (backend->enable(backend)) {
            graphics_inited = 1;
            graphics_set_splash_helper();
            return 1;
        }
        return 0;
    }

    graphics = grub_malloc(sizeof (*graphics));
    if (!graphics)
        return 0;

    grub_memset(graphics, '\0', sizeof (*graphics));

    graphics->scroll = 1;
    graphics->fontx = 0;
    graphics->fonty = 0;

    graphics->standard_color = A_NORMAL;
    graphics->normal_color = A_NORMAL;
    graphics->highlight_color = A_REVERSE;
    graphics->current_color = A_NORMAL;
    graphics->color_state = COLOR_STATE_STANDARD;

    grub_memset(graphics->splashpath, '\0', sizeof (graphics->splashpath));

    for(i = 0; backends[i] != NULL; i++) {
        backend = backends[i];
        backend->graphics = graphics;
        if (backend->enable(backend)) {
            graphics_inited = 1;
            graphics_set_splash_helper();
            return 1;
        }
        backend->graphics = NULL;
    }
    grub_free(graphics->text);
    grub_free(graphics);
    return 0;
}

void
graphics_end(void)
{
    if (backend && graphics_inited) {
        graphics_inited = 0;
        backend->disable(backend);
    }
}

#endif /* SUPPORT_GRAPHICS */
