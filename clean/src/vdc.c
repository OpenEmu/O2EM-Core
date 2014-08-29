/*
 *   O2EM Free Odyssey2 / Videopac+ Emulator
 *
 *   Created by Daniel Boris <dboris@comcast.net>  (c) 1997,1998
 *
 *   Developed by Andre de la Rocha   <adlroc@users.sourceforge.net>
 *             Arlindo M. de Oliveira <dgtec@users.sourceforge.net>
 *
 *   http://o2em.sourceforge.net
 *
 *
 *
 *   O2 Video Display Controller emulation
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "types.h"
#include "vmachine.h"
#include "config.h"
#include "keyboard.h"
#include "cset.h"
#include "timefunc.h"
#include "cpu.h"
#include "vpp.h"
#include "vdc.h"
#include "allegro.h"
#include "audio.h"
#include "voice.h"

#define COL_SP0   0x01
#define COL_SP1   0x02
#define COL_SP2   0x04
#define COL_SP3   0x08
#define COL_VGRID 0x10
#define COL_HGRID 0x20
#define COL_VPP   0x40
#define COL_CHAR  0x80

#define X_START   8
#define Y_START   24

static long colortable[2][16] = {
    /* O2 palette */
    { 0x000000, 0x0e3dd4, 0x00981b, 0x00bbd9, 0xc70008, 0xcc16b3, 0x9d8710,
      0xe1dee1, 0x5f6e6b, 0x6aa1ff, 0x3df07a, 0x31ffff, 0xff4255, 0xff98ff,
      0xd9ad5d, 0xffffff },
    /* VP+ G7400 palette */
    { 0x000000, 0x0000b6, 0x00b600, 0x00b6b6, 0xb60000, 0xb600b6, 0xb6b600,
      0xb6b6b6, 0x494949, 0x4949ff, 0x49ff49, 0x49ffff, 0xff4949, 0xff49ff,
      0xffff49, 0xffffff }
};

/* Collision buffer */
static Byte *col = NULL;

static PALETTE colors, oldcol;

/* The pointer to the graphics buffer */
static Byte *vscreen = NULL;

static BITMAP *bitmap, *bitmap_cache;
static int cached_lines[MAXLINES];

Byte coltab[256];

long clip_low;
long clip_high;

int show_fps = 0;

int wsize;

static void create_cmap(void);
static void draw_char(Byte ypos, Byte xpos, Byte chr, Byte col);
static void draw_quad(Byte ypos, Byte xpos, Byte cp0l, Byte cp0h, Byte cp1l,
        Byte cp1h, Byte cp2l, Byte cp2h, Byte cp3l, Byte cp3h);
static void draw_grid(void);
INLINE void mputvid(unsigned int ad, unsigned int len, Byte d, Byte c);

void draw_region(void)
{
    int i;
    if (regionoff == 0xffff) {
        i = (master_clk / (LINECNT - 1) - 5);
    } else {
        i = (master_clk / 22 + regionoff);
    }
    i = (snapline(i, VDCwrite[0xA0], 0));
    if (app_data.crc == 0xA7344D1F) {
        i = (master_clk / 22 + regionoff) + 6;
        i = (snapline(i, VDCwrite[0xA0], 0) + 6);
    }/*Atlantis*/

    if (app_data.crc == 0xD0BC4EE6) {
        i = (master_clk / 24 + regionoff) - 6;
        i = (snapline(i, VDCwrite[0xA0], 0) + 7);
    }/*Frogger*/

    if (app_data.crc == 0x26517E77) {
        i = (master_clk / 22 + regionoff);
        i = (snapline(i, VDCwrite[0xA0], 0) - 5);
    }/*Comando Noturno*/

    if (app_data.crc == 0xA57E1724) {
        i = (master_clk / (LINECNT - 1) - 5);
        i = (snapline(i, VDCwrite[0xA0], 0) - 3);
    }/*Catch the ball*/

    if (i < 0)
        i = 0;
    clip_low = last_line * (long) BMPW;
    clip_high = i * (long) BMPW;
    if (clip_high > BMPW * BMPH)
        clip_high = BMPW * BMPH;
    if (clip_low < 0)
        clip_low = 0;
    if (clip_low < clip_high)
        draw_display();
    last_line = i;
}

static void create_cmap(void)
{
    int i;

    /* Initialise parts of the colors array */
    for (i = 0; i < 16; i++) {
        /* Use the color values from the color table */
        colors[i + 32].r = colors[i].r = (colortable[app_data.vpp ? 1 : 0][i]
                & 0xff0000) >> 18;
        colors[i + 32].g = colors[i].g = (colortable[app_data.vpp ? 1 : 0][i]
                & 0x00ff00) >> 10;
        colors[i + 32].b = colors[i].b = (colortable[app_data.vpp ? 1 : 0][i]
                & 0x0000ff) >> 2;
    }

    for (i = 16; i < 32; i++) {
        /* Half-bright colors for the 50% scanlines */
        colors[i + 32].r = colors[i].r = colors[i - 16].r / 2;
        colors[i + 32].g = colors[i].g = colors[i - 16].g / 2;
        colors[i + 32].b = colors[i].b = colors[i - 16].b / 2;
    }

    for (i = 64; i < 256; i++)
        colors[i].r = colors[i].g = colors[i].b = 0;
}

void grmode(void)
{
    set_color_depth(8);
    wsize = app_data.wsize;
    if (app_data.fullscreen) {
        if (app_data.scanlines) {
            wsize = 2;
            if (set_gfx_mode(GFX_AUTODETECT_FULLSCREEN, 640, 480, 0, 0)) {
                wsize = 1;
                if (set_gfx_mode(GFX_AUTODETECT_FULLSCREEN, 320, 240, 0, 0)) {
                    fprintf(stderr, "Error: could not create screen.\n");
                    exit(EXIT_FAILURE);
                }
            }
        } else {
#ifdef ALLEGRO_DOS
            wsize = 1;
            if (set_gfx_mode(GFX_AUTODETECT_FULLSCREEN, 320, 240, 0, 0)) {
                wsize = 2;
                if (set_gfx_mode(GFX_AUTODETECT_FULLSCREEN, 640, 480, 0, 0)) {
                    fprintf(stderr,"Error: could not create screen.\n");
                    exit(EXIT_FAILURE);
                }
            }
#else
            wsize = 2;
            if (set_gfx_mode(GFX_AUTODETECT_FULLSCREEN, 640, 480, 0, 0)) {
                wsize = 1;
                if (set_gfx_mode(GFX_AUTODETECT_FULLSCREEN, 320, 240, 0, 0)) {
                    fprintf(stderr, "Error: could not create screen.\n");
                    exit(EXIT_FAILURE);
                }
            }
#endif
        }
    } else {
        if (set_gfx_mode(GFX_AUTODETECT_WINDOWED, WNDW * wsize, WNDH * wsize, 0,
                0)) {
            wsize = 2;
            if (set_gfx_mode(GFX_AUTODETECT_WINDOWED, WNDW * 2, WNDH * 2, 0,
                    0)) {
                if (set_gfx_mode(GFX_AUTODETECT, WNDW * 2, WNDH * 2, 0, 0)) {
                    fprintf(stderr, "Error: could not create window.\n");
                    exit(EXIT_FAILURE);
                }
            }
#ifndef ALLEGRO_DOS
            printf("Could not set the requested window size\n");
#endif
        }
    }

    if ((app_data.scanlines) && (wsize == 1)) {
#ifndef ALLEGRO_DOS
        printf("Could not set scanlines\n");
#endif
    }
    set_palette(colors);
    set_window_title(app_data.window_title);
    clearscr();
    set_display_switch_mode(SWITCH_PAUSE);
}

void set_textmode(void)
{
    set_palette(oldcol);
    set_gfx_mode(GFX_TEXT, 0, 0, 0, 0);
    if (new_int) {
        Set_Old_Int9();
    }
}

void clearscr(void)
{
    acquire_screen();
    clear(screen);
    release_screen();
    clear(bitmap_cache);
}

INLINE void mputvid(unsigned int ad, unsigned int len, Byte d, Byte c)
{
    if ((ad > (unsigned long) clip_low) && (ad < (unsigned long) clip_high)) {
        unsigned int i;
        if (((len & 3) == 0) && (sizeof(unsigned long) == 4)) {
            unsigned long dddd = (((unsigned long) d) & 0xff)
                    | ((((unsigned long) d) & 0xff) << 8)
                    | ((((unsigned long) d) & 0xff) << 16)
                    | ((((unsigned long) d) & 0xff) << 24);
            unsigned long cccc = (((unsigned long) c) & 0xff)
                    | ((((unsigned long) c) & 0xff) << 8)
                    | ((((unsigned long) c) & 0xff) << 16)
                    | ((((unsigned long) c) & 0xff) << 24);
            for (i = 0; i < len >> 2; i++) {
                *((unsigned long*) (vscreen + ad)) = dddd;
                cccc |= *((unsigned long*) (col + ad));
                *((unsigned long*) (col + ad)) = cccc;
                coltab[c] |= ((cccc | (cccc >> 8) | (cccc >> 16) | (cccc >> 24))
                        & 0xff);
                ad += 4;
            }
        } else {
            for (i = 0; i < len; i++) {
                vscreen[ad] = d;
                col[ad] |= c;
                coltab[c] |= col[ad++];
            }
        }
    }
}

static void draw_grid(void)
{
    unsigned int pnt, pn1;
    Byte mask, d;
    int j, i, x, w;
    Byte color;

    if (VDCwrite[0xA0] & 0x40) {
        for (j = 0; j < 9; j++) {
            pnt = (((j * 24) + 24) * BMPW);
            for (i = 0; i < 9; i++) {
                pn1 = pnt + (i * 32) + 20;
                color = ColorVector[j * 24 + 24];
                mputvid(
                        pn1,
                        4,
                        (color & 0x07) | ((color & 0x40) >> 3)
                                | (color & 0x80 ? 0 : 8), COL_HGRID);
                color = ColorVector[j * 24 + 25];
                mputvid(
                        pn1 + BMPW,
                        4,
                        (color & 0x07) | ((color & 0x40) >> 3)
                                | (color & 0x80 ? 0 : 8), COL_HGRID);
                color = ColorVector[j * 24 + 26];
                mputvid(
                        pn1 + BMPW * 2,
                        4,
                        (color & 0x07) | ((color & 0x40) >> 3)
                                | (color & 0x80 ? 0 : 8), COL_HGRID);
            }
        }
    }

    mask = 0x01;
    for (j = 0; j < 9; j++) {
        pnt = (((j * 24) + 24) * BMPW);
        for (i = 0; i < 9; i++) {
            pn1 = pnt + (i * 32) + 20;
            if ((pn1 + BMPW * 3 >= (unsigned long) clip_low)
                    && (pn1 <= (unsigned long) clip_high)) {
                d = VDCwrite[0xC0 + i];
                if (j == 8) {
                    d = VDCwrite[0xD0 + i];
                    mask = 1;
                }
                if (d & mask) {
                    color = ColorVector[j * 24 + 24];
                    mputvid(
                            pn1,
                            36,
                            (color & 0x07) | ((color & 0x40) >> 3)
                                    | (color & 0x80 ? 0 : 8), COL_HGRID);
                    color = ColorVector[j * 24 + 25];
                    mputvid(
                            pn1 + BMPW,
                            36,
                            (color & 0x07) | ((color & 0x40) >> 3)
                                    | (color & 0x80 ? 0 : 8), COL_HGRID);
                    color = ColorVector[j * 24 + 26];
                    mputvid(
                            pn1 + BMPW * 2,
                            36,
                            (color & 0x07) | ((color & 0x40) >> 3)
                                    | (color & 0x80 ? 0 : 8), COL_HGRID);
                }
            }
        }
        mask = mask << 1;
    }

    mask = 0x01;
    w = 4;
    if (VDCwrite[0xA0] & 0x80) {
        w = 32;
    }
    for (j = 0; j < 10; j++) {
        pnt = (j * 32);
        mask = 0x01;
        d = VDCwrite[0xE0 + j];
        for (x = 0; x < 8; x++) {
            pn1 = pnt + (((x * 24) + 24) * BMPW) + 20;
            if (d & mask) {
                for (i = 0; i < 24; i++) {
                    if ((pn1 >= (unsigned long) clip_low)
                            && (pn1 <= (unsigned long) clip_high)) {
                        color = ColorVector[x * 24 + 24 + i];
                        mputvid(
                                pn1,
                                w,
                                (color & 0x07) | ((color & 0x40) >> 3)
                                        | (color & 0x80 ? 0 : 8), COL_VGRID);
                    }
                    pn1 += BMPW;
                }
            }
            mask = mask << 1;
        }
    }
}

void finish_display(void)
{
    int x, y, sn;
    static int cache_counter = 0;

    vpp_finish_bmp(vscreen, 9, 5, BMPW - 9, BMPH - 5, bitmap->w, bitmap->h);

    if (show_fps) {
        static long last = -1, index = 0, curr = 0, t = 0;
        if (last < 0)
            last = gettimeticks();
        index = (index + 1) % 200;
        if (!index) {
            t = gettimeticks();
            curr = t - last;
            last = t;
        }
        if (curr) {
            textprintf_ex(bitmap, font, 20, 4, 7, 0, "FPS :%3d",
                    (int) ((200.0 * TICKSPERSEC) / curr + 0.5));
        }
    }

    for (y = 0; y < bitmap->h; y++) {
        cached_lines[y] = !memcmp(bitmap_cache->line[y], bitmap->line[y], bitmap->w);
        if (!cached_lines[y])
            memcpy(bitmap_cache->line[y], bitmap->line[y], bitmap->w);
    }

    for (y = 0; y < 10; y++)
        cached_lines[(y + cache_counter) % bitmap->h] = 0;
    cache_counter = (cache_counter + 10) % bitmap->h;

    acquire_screen();

    sn = ((wsize > 1) && (app_data.scanlines)) ? 1 : 0;

    for (y = 0; y < WNDH; y++) {
        if (!cached_lines[y + 2])
            stretch_blit(bitmap, screen, 7, 2 + y, WNDW, 1, 0, y * wsize,
                    WNDW * wsize, wsize - sn);
    }

    if (sn) {
        for (y = 0; y < WNDH; y++) {
            if (!cached_lines[y + 2]) {
                for (x = 0; x < bitmap->w; x++)
                    bitmap->line[y + 2][x] += 16;
                stretch_blit(bitmap, screen, 7, 2 + y, WNDW, 1, 0,
                        (y + 1) * wsize - 1, WNDW * wsize, 1);
                memcpy(bitmap->line[y + 2], bitmap_cache->line[y + 2], bitmap->w);
            }
        }
    }
    release_screen();
}

void clear_collision(void)
{
    load_colplus(col);
    coltab[0x01] = coltab[0x02] = 0;
    coltab[0x04] = coltab[0x08] = 0;
    coltab[0x10] = coltab[0x20] = 0;
    coltab[0x40] = coltab[0x80] = 0;
}

void draw_display(void)
{
    int i, j, x, sm, t;
    Byte y, b, d1, cl, c;

    unsigned int pnt, pnt2;

    for (i = clip_low / BMPW; i < clip_high / BMPW; i++)
        memset(
                vscreen + i * BMPW,
                ((ColorVector[i] & 0x38) >> 3)
                        | (ColorVector[i] & 0x80 ? 0 : 8), BMPW);

    if (VDCwrite[0xA0] & 0x08)
        draw_grid();

    if (useforen && (!(VDCwrite[0xA0] & 0x20)))
        return;

    for (i = 0x10; i < 0x40; i += 4)
        draw_char(VDCwrite[i], VDCwrite[i + 1], VDCwrite[i + 2],
                VDCwrite[i + 3]);

    /* draw quads, position mapping happens in ext_write (vmachine.c)*/
    for (i = 0x40; i < 0x80; i += 0x10)
        draw_quad(VDCwrite[i], VDCwrite[i + 1], VDCwrite[i + 2],
                VDCwrite[i + 3], VDCwrite[i + 6], VDCwrite[i + 7],
                VDCwrite[i + 10], VDCwrite[i + 11], VDCwrite[i + 14],
                VDCwrite[i + 15]);

    c = 8;
    for (i = 12; i >= 0; i -= 4) {
        pnt2 = 0x80 + (i * 2);
        y = VDCwrite[i];
        x = VDCwrite[i + 1] - 8;
        t = VDCwrite[i + 2];
        cl = ((t & 0x38) >> 3);
        cl = ((cl & 2) | ((cl & 1) << 2) | ((cl & 4) >> 2)) + 8;
        /*174*/
        if ((x < 164) && (y > 0) && (y < 232)) {
            pnt = y * BMPW + (x * 2) + 20 + sproff;
            if (t & 4) {
                if ((pnt + BMPW * 32 >= (unsigned long) clip_low)
                        && (pnt <= (unsigned long) clip_high)) {
                    for (j = 0; j < 8; j++) {
                        sm = (((j % 2 == 0) && (((t >> 1) & 1) != (t & 1)))
                                || ((j % 2 == 1) && (t & 1))) ? 1 : 0;
                        d1 = VDCwrite[pnt2++];
                        for (b = 0; b < 8; b++) {
                            if (d1 & 0x01) {
                                if ((x + b + sm < 159) && (y + j < 247)) {
                                    mputvid(sm + pnt, 4, cl, c);
                                    mputvid(sm + pnt + BMPW, 4, cl, c);
                                    mputvid(sm + pnt + 2 * BMPW, 4, cl, c);
                                    mputvid(sm + pnt + 3 * BMPW, 4, cl, c);
                                }
                            }
                            pnt += 4;
                            d1 = d1 >> 1;
                        }
                        pnt += BMPW * 4 - 32;
                    }
                }
            } else {
                if ((pnt + BMPW * 16 >= (unsigned long) clip_low)
                        && (pnt <= (unsigned long) clip_high)) {
                    for (j = 0; j < 8; j++) {
                        sm = (((j % 2 == 0) && (((t >> 1) & 1) != (t & 1)))
                                || ((j % 2 == 1) && (t & 1))) ? 1 : 0;
                        d1 = VDCwrite[pnt2++];
                        for (b = 0; b < 8; b++) {
                            if (d1 & 0x01) {
                                if ((x + b + sm < 160) && (y + j < 249)) {
                                    mputvid(sm + pnt, 2, cl, c);
                                    mputvid(sm + pnt + BMPW, 2, cl, c);
                                }
                            }
                            pnt += 2;
                            d1 = d1 >> 1;
                        }
                        pnt += BMPW * 2 - 16;
                    }
                }
            }
        }
        c = c >> 1;
    }
}

void draw_char(Byte ypos, Byte xpos, Byte chr, Byte col)
{
    int j, c;
    Byte cl, d1;
    int y, b, n;
    unsigned int pnt;

    y = (ypos & 0xFE);
    pnt = y * BMPW + ((xpos - 8) * 2) + 20;

    ypos = ypos >> 1;
    n = 8 - (ypos % 8) - (chr % 8);
    if (n < 3)
        n = n + 7;

    if ((pnt + BMPW * 2 * n >= (unsigned long) clip_low)
            && (pnt <= (unsigned long) clip_high)) {

        c = (int) chr + ypos;
        if (col & 0x01)
            c += 256;
        if (c > 511)
            c = c - 512;

        cl = ((col & 0x0E) >> 1);
        cl = ((cl & 2) | ((cl & 1) << 2) | ((cl & 4) >> 2)) + 8;

        if ((y > 0) && (y < 232) && (xpos < 157)) {
            for (j = 0; j < n; j++) {
                d1 = cset[c + j];
                for (b = 0; b < 8; b++) {
                    if (d1 & 0x80) {
                        if ((xpos - 8 + b < 160) && (y + j < 240)) {
                            mputvid(pnt, 2, cl, COL_CHAR);
                            mputvid(pnt + BMPW, 2, cl, COL_CHAR);
                        }
                    }
                    pnt += 2;
                    d1 = d1 << 1;
                }
                pnt += BMPW * 2 - 16;
            }
        }
    }
}

/* This quad drawing routine can display the quad cut off effect used in KTAA.
 * It needs more testing with other games, especially the clipping.
 * This code is quite slow and needs a rewrite by somebody with more experience
 * than I (sgust) have */

void draw_quad(Byte ypos, Byte xpos, Byte cp0l, Byte cp0h, Byte cp1l, Byte cp1h,
        Byte cp2l, Byte cp2h, Byte cp3l, Byte cp3h)
{
    /* char set pointers */
    int chp[4];
    /* colors */
    Byte col[4];
    /* pointer into screen bitmap */
    unsigned int pnt;
    /* offset into current line */
    unsigned int off;
    /* loop variables */
    int i, j, lines;

    /* get screen bitmap position of quad */
    pnt = (ypos & 0xfe) * BMPW + ((xpos - 8) * 2) + 20;
    /* abort drawing if completely below the bottom clip */
    if (pnt > (unsigned long) clip_high)
        return;
    /* extract and convert char-set offsets */
    chp[0] = cp0l | ((cp0h & 1) << 8);
    chp[1] = cp1l | ((cp1h & 1) << 8);
    chp[2] = cp2l | ((cp2h & 1) << 8);
    chp[3] = cp3l | ((cp3h & 1) << 8);
    for (i = 0; i < 4; i++)
        chp[i] = (chp[i] + (ypos >> 1)) & 0x1ff;
    lines = 8 - (chp[3] + 1) % 8;
    /* abort drawing if completely over the top clip */
    if (pnt + BMPW * 2 * lines < (unsigned long) clip_low)
        return;
    /* extract and convert color information */
    col[0] = (cp0h & 0xe) >> 1;
    col[1] = (cp1h & 0xe) >> 1;
    col[2] = (cp2h & 0xe) >> 1;
    col[3] = (cp3h & 0xe) >> 1;
    for (i = 0; i < 4; i++)
        col[i] = ((col[i] & 2) | ((col[i] & 1) << 2) | ((col[i] & 4) >> 2)) + 8;
    /* now draw the quad line by line controlled by the last quad */
    while (lines-- > 0) {
        off = 0;
        /* draw all 4 sub-quads */
        for (i = 0; i < 4; i++) {
            /* draw sub-quad pixel by pixel, but stay in same line */
            for (j = 0; j < 8; j++) {
                if ((cset[chp[i]] & (1 << (7 - j))) && (off < BMPW)) {
                    mputvid(pnt + off, 2, col[i], COL_CHAR);
                    mputvid(pnt + off + BMPW, 2, col[i], COL_CHAR);
                }
                /* next pixel */
                off += 2;
            }
            /* space between sub-quads */
            off += 16;
        }
        /* advance char-set pointers */
        for (i = 0; i < 4; i++)
            chp[i] = (chp[i] + 1) & 0x1ff;
        /* advance screen bitmap pointer */
        pnt += BMPW * 2;
    }
}

void close_display(void)
{
    free(vscreen);
    free(col);
}

void window_close_hook(void)
{
    key_debug = 0;
    key_done = 1;

    // FIXME : Quit program in a clean way
    //finish_display();
    //close_display();
    //allegro_exit();
}
static void txtmsg(int x, int y, int c, const char *s)
{
    textout_centre_ex(bitmap, font, s, x + 1, y + 1, makecol(255, 0, 0),
            makecol(0, 0, 0));
    textout_centre_ex(bitmap, font, s, x, y, c, makecol(0, 0, 0));
}
void display_bg(void)
{
    rectfill(bitmap, 20, 72, 311, 172, 9 + 32);
    line(bitmap, 20, 72, 311, 72, 15 + 32);
    line(bitmap, 20, 72, 20, 172, 15 + 32);
    line(bitmap, 21, 172, 311, 172, 1 + 32);
    line(bitmap, 311, 172, 311, 72, 1 + 32);
}

void abaut(void)
{
    char *ver;
    char exitstr[80];
    int i = 0;
    while (syskeys[1] != keybtab[i].keybcode)
        i++;

    strcpy(exitstr, "Press ");
    strcat(exitstr, keybtab[i].keybname);
    strcat(exitstr, " to continue");

#if defined(ALLEGRO_WINDOWS)
    ver = "Windows version";
#elif defined(ALLEGRO_DOS)
    ver = "DOS version";
#elif defined(ALLEGRO_LINUX)
    ver = "Linux version";
#elif defined(ALLEGRO_BEOS)
    ver = "BEOS version";
#elif defined(ALLEGRO_QNX)
    ver = "QNX version";
#elif defined(ALLEGRO_UNIX)
    ver = "UNIX version";
#elif defined(ALLEGRO_MPW)
    ver = "MacOS version";
#else
    ver = "Unknown platform";
#endif
    display_bg();
    txtmsg(166, 76, 15 + 32, "O2EM v" O2EM_VERSION "  " RELEASE_DATE);
    txtmsg(166, 90, 15 + 32, "Free Odyssey2 / VP+ Emulator");
    txtmsg(166, 104, 15 + 32, ver);
    txtmsg(166, 118, 15 + 32, "Developed by Andre de la Rocha");
    txtmsg(168, 132, 15 + 32, "and Arlindo M. de Oliveira");
    txtmsg(166, 148, 15 + 32, "Copyright 1996/1998 by Daniel Boris");
    txtmsg(166, 162, 15 + 32, exitstr);
    finish_display();
}
void display_msg(char *msg, int waits)
{
    mute_audio();
    mute_voice();
    rectfill(bitmap, 60, 72, 271, 90, 9 + 32);
    line(bitmap, 60, 72, 271, 72, 15 + 32);
    line(bitmap, 60, 72, 60, 90, 15 + 32);
    line(bitmap, 61, 90, 271, 90, 1 + 32);
    line(bitmap, 271, 90, 271, 72, 1 + 32);
    txtmsg(166, 76, 15 + 32, msg);
    finish_display();
    rest(waits * 100);
    init_sound_stream();
}
void init_display(void)
{
    get_palette(oldcol);
    create_cmap();
    bitmap = create_bitmap(BMPW, BMPH);
    bitmap_cache = create_bitmap(BMPW, BMPH);
    if ((!bitmap) || (!bitmap_cache)) {
        fprintf(stderr, "Could not allocate memory for screen buffer.\n");
        exit(EXIT_FAILURE);
    }
    vscreen = (Byte *) bitmap->dat;
    clear(bitmap);
    clear(bitmap_cache);
    col = (Byte *) malloc(BMPW * BMPH);
    if (!col) {
        fprintf(stderr, "Could not allocate memory for collision buffer.\n");
        free(vscreen);
        exit(EXIT_FAILURE);
    }
    memset(col, 0, BMPW * BMPH);
    if (!app_data.debug) {
        grmode();
        init_keyboard();
    }
    set_close_button_callback(window_close_hook);

}
/*************************** Help*/
void help(void)
{
    int yet_another_index = 40;
    int index = 0;
    int counter = 0;
    int way = 0;
    static char hl[96][70] = {
        "-wsize=n", " Window size (1-4)", "",
        "-fullscreen", " Full screen mode", "",
        "-help", " This instructions", "",
        "-scanlines", " Enable scanlines", "",
        "-nosound", " Turn off sound emulation", "",
        "-novoice", " Turn off voice emulation", "",
        "-svolume=n", " Set sound volume (0-100)", "",
        "-vvolume=n", " Set voice volume (0-100)", "",
        "-filter", " Enable low-pass audio filter", "",
        "-debug", " Start the emulator in ", " debug mode", "",
        "-speed=n", " Relative speed", " (100 = original)", "",
        "-nolimit", " Turn off speed limiter", "",
        "-bios=file", " Set the O2 bios file name/dir", "",
        "-biosdir=path", " Set the O2 bios path ", " (default= bios/ )", "",
        "-romdir=path", " Set the O2 roms Path ", " (default= roms/ )", "",
        "-scshot=file", " Set the screenshot file ", " name/template", "",
        "-euro", " Use European timing /", " 50Hz mode", "",
        "-exrom", " Use special 3K program/", " 1K data ROM mode", "",
        "-3k", " Use 3K rom mapping mode", "",
        "-s<n>=mode/keys", " Define stick n mode/keys (n=1-2)", "",
        "-c52", " Start the emulator with ", " french Odyssey 2 BIOS", "",
        "-g7400", " Start the emulator ", " with VP+ BIOS", "",
        "-jopac", " Start the emulator with ", " french VP+ bios", "",
        "-scoretype=m", " Set Scoretype to m (see manual)", "",
        "-scoreadr=n", " Set Scoreaddress to n ", " (decimal value)", "",
        "-scorefile=file", " Set Output-Scorefile to ", " file (highscore.txt)", "",
        "-score=n", " Set Highscore to n", "",
        "-savefile=file", " Load/Save State ", " from/to file"
    };

    textout_ex(bitmap, font, "O2EM v" O2EM_VERSION " Help", 26, 16, 2, 0);
    textout_ex(bitmap, font, "Use: o2em <file> [options]", 26, 26, 2, 0);
    rect(bitmap, 20, 12, 315, 228, 6);
    line(bitmap, 20, 35, 315, 35, 6);
    for (index = 0; index < 16; index++) {
        textout_ex(bitmap, font, hl[counter + index], 26, yet_another_index, 4, makecol(0, 0, 0));
        yet_another_index += 12;
    }
    yet_another_index = 40;
    finish_display();
    do {
        rest(25);
        if (NeedsPoll)
            poll_keyboard();
        if (key[KEY_DOWN]) {
            counter++;
            if (counter >= 80)
                counter = 80;
            rectfill(bitmap, 25, 36, 306, 227, 0);
            for (index = 0; index < 16; index++) {
                textout_ex(bitmap, font, hl[counter + index], 26, yet_another_index, 4,
                        makecol(0, 0, 0));
                yet_another_index += 12;
            }
            yet_another_index = 40;
            for (way = 0; way < 8; way++)
                finish_display();
        }
        if (key[KEY_UP]) {
            counter--;
            if (counter <= 0)
                counter = 0;
            rectfill(bitmap, 25, 36, 306, 227, 0);
            for (index = 0; index < 16; index++) {
                textout_ex(bitmap, font, hl[counter + index], 26, yet_another_index, 4,
                        makecol(0, 0, 0));
                yet_another_index += 12;
            }
            yet_another_index = 40;
            for (way = 0; way < 8; way++)
                finish_display();
        }

    } while (!key[KEY_ESC]);
    do {
        rest(5);
        if (NeedsPoll)
            poll_keyboard();
    } while (key[KEY_ESC]);
    return;
}

