/****************************************************************************
* NeoGeo Directory Selector
*
* As there is no 'ROM' to speak of, use the directory as the starting point
****************************************************************************/

#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neocdrx.h"

#ifdef HW_RVL
#include <wiiuse/wpad.h>
#endif

#define PAGE_SIZE       8
#define MAX_DISP_CHARS  32          /* visible character columns in the browser */
#define CLIP_X0         64          /* left edge of display area (pixels, must be even) */
#define CLIP_X1         (CLIP_X0 + MAX_DISP_CHARS * 16)   /* 576 */

/* Scroll speed in pixels per frame.  Tune this value freely.
 *   0.4  ≈ 24 px/sec at 60fps (default)
 *   0.5  ≈ 30 px/sec
 *   1.0  = 60 px/sec
 * Sub-1 values work correctly: the float accumulates every frame and the
 * visible position updates every ceil(1/SCROLL_SPEED) frames. */
#define SCROLL_SPEED        2.0f
#define SCROLL_HOLD_FRAMES  60

/* Scroll animation states */
enum {
    SANIM_SCROLL_FWD = 0,   /* scrolling toward end of string */
    SANIM_WAIT_END,          /* holding at end before scrolling back */
    SANIM_SCROLL_BACK,       /* scrolling back toward beginning */
    SANIM_WAIT_START         /* holding at beginning before scrolling forward again */
};

char basedir[1024];
char scratchdir[1024];
char megadir[1040];   /* 1024 (basedir max) + len("/IPL.TXT") + null */
char dirbuffer[0x10000] ATTRIBUTE_ALIGN (32);

char root_dir[10];
int dirsel_back_to_main = 0;  /* set to 1 to signal caller to return to main menu */

/****************************************************************************
* DrawDirSelector
*
* Scroll animation lives entirely in static locals so it persists between
* frames.  It resets automatically when the cursor moves to a new item.
*
* Selected items:  display full string, start scrolling immediately at
*                  SCROLL_SPEED px/frame, bounce at both ends.
* Unselected items: display up to MAX_DISP_CHARS characters, no truncation indicator.
****************************************************************************/
static void
DrawDirSelector (int maxfile, int menupos, int currsel)
{
    static int   last_sel    = -1;
    static int   sa_state    = SANIM_SCROLL_FWD;
    static float sa_pixel    = 0.0f;   /* accumulated pixel scroll offset */
    static int   sa_hold     = 0;      /* hold timer (frames) */

    int  i;
    int  j = 158;
    int *p = (int *) dirbuffer;
    char *m;
    char  display[MAX_DISP_CHARS + 1];

    /* ── Reset scroll state when cursor moves ── */
    if (currsel != last_sel)
    {
        last_sel = currsel;
        sa_state = SANIM_SCROLL_FWD;
        sa_pixel = 0.0f;
        sa_hold  = 0;
    }

    /* ── Advance bounce scroll for the selected item ── */
    if (currsel >= 0 && currsel < maxfile)
    {
        m = (char *) p[currsel + 1];
        int sel_len       = (int) strlen (m);
        float max_px      = (float)((sel_len - MAX_DISP_CHARS) * 16);

        if (sel_len > MAX_DISP_CHARS && max_px > 0.0f)
        {
            switch (sa_state)
            {
                case SANIM_SCROLL_FWD:
                    sa_pixel += SCROLL_SPEED;
                    if (sa_pixel >= max_px)
                    {
                        sa_pixel = max_px;
                        sa_state = SANIM_WAIT_END;
                        sa_hold  = 0;
                    }
                    break;

                case SANIM_WAIT_END:
                    if (++sa_hold >= SCROLL_HOLD_FRAMES)
                    {
                        sa_state = SANIM_SCROLL_BACK;
                        sa_hold  = 0;
                    }
                    break;

                case SANIM_SCROLL_BACK:
                    sa_pixel -= SCROLL_SPEED;
                    if (sa_pixel <= 0.0f)
                    {
                        sa_pixel = 0.0f;
                        sa_state = SANIM_WAIT_START;
                        sa_hold  = 0;
                    }
                    break;

                case SANIM_WAIT_START:
                    if (++sa_hold >= SCROLL_HOLD_FRAMES)
                    {
                        sa_state = SANIM_SCROLL_FWD;
                        sa_hold  = 0;
                    }
                    break;
            }
        }
    }

    /* ── Draw ── */
    DrawScreen ();

    for (i = menupos; i < (menupos + PAGE_SIZE) && (i < maxfile); i++)
    {
        m = (char *) p[i + 1];

        if (i == currsel)
        {
            /* Selected: draw full string with smooth pixel-scroll clipping */

            /* Snap to 1px granularity.  sub_px is 0..15, x_start may be odd —
             * gprint_clipped's per-word clip check handles misaligned starts
             * correctly so odd values are safe. */
            int render_px = (int) sa_pixel;
            int char_skip = render_px / 16;
            int sub_px    = render_px % 16;
            int x_start   = CLIP_X0 - sub_px;

            /* Rainbow bar at fixed 32-char width */
            draw_rainbow_bar (CLIP_X0 - 4, j, MAX_DISP_CHARS * 16 + 8, 32, 4);
            setfgcolour (COLOR_WHITE);
            bg_transparent = 1;
            gprint_clipped (x_start, j, m + char_skip, CLIP_X0, CLIP_X1);
            bg_transparent = 0;
        }
        else
        {
            /* Unselected: show up to MAX_DISP_CHARS characters, no indicator */
            strncpy (display, m, MAX_DISP_CHARS);
            display[MAX_DISP_CHARS] = '\0';

            setfgcolour (COLOR_BLACK);
            bg_transparent = 1;
            gprint (CLIP_X0, j, display, TXT_DOUBLE);
            bg_transparent = 0;
        }

        j += 32;
    }

    ShowScreen ();
}

/****************************************************************************
* clamp_menupos — ensure menupos gives a full page (or as full as possible)
* with currsel visible.  Cursor stays at currsel; viewport is positioned so
* the cursor is at the top, unless that would leave blank rows at the bottom.
****************************************************************************/
static int
clamp_menupos (int currsel, int maxfile)
{
    int mp = currsel;
    if (mp + PAGE_SIZE > maxfile)
        mp = maxfile - PAGE_SIZE;
    if (mp < 0)
        mp = 0;
    return mp;
}

/****************************************************************************
* DirSelector
*
* A             — Enter directory / auto-load if IPL.TXT found
* X             — Force-load selected directory
* B             — Go up one directory; quit when at entry directory
* Z             — Quit unconditionally to main menu
* UP / DOWN     — Move cursor one item (carousel wrap)
* L / D-Left   — Jump up PAGE_SIZE items (clamp at top, no wrap)
* R / D-Right  — Jump down PAGE_SIZE items (clamp at bottom, no wrap)
****************************************************************************/
void
DirSelector (void)
{
    int  *p = (int *) dirbuffer;
    char *m;
    int   maxfile, i;
    int   currsel = 0;
    int   menupos = 0;
    short joy;
    int   quit = 0;
    char  entry_basedir[1024];
    GENFILE fp;

    dirsel_back_to_main = 0;
    strcpy (entry_basedir, basedir);

    maxfile = p[0];

    while (!quit)
    {
        DrawDirSelector (maxfile, menupos, currsel);

        joy = getMenuButtons ();

        /* ── DOWN: move cursor down one, wrap from bottom to top ── */
        if (joy & PAD_BUTTON_DOWN)
        {
            currsel++;
            if (currsel >= maxfile)
            {
                /* wrap: jump to top */
                currsel = 0;
                menupos = 0;
            }
            else if (currsel >= menupos + PAGE_SIZE)
            {
                /* scroll viewport down by one */
                menupos++;
            }
        }

        /* ── UP: move cursor up one, wrap from top to bottom ── */
        if (joy & PAD_BUTTON_UP)
        {
            currsel--;
            if (currsel < 0)
            {
                /* wrap: jump to bottom */
                currsel = maxfile - 1;
                menupos = (maxfile > PAGE_SIZE) ? maxfile - PAGE_SIZE : 0;
            }
            else if (currsel < menupos)
            {
                /* scroll viewport up by one */
                menupos--;
            }
        }

        /* ── L / D-Left: jump up PAGE_SIZE, clamp at top ── */
        if ((joy & PAD_TRIGGER_L) || (joy & PAD_BUTTON_LEFT))
        {
            currsel -= PAGE_SIZE;
            if (currsel < 0) currsel = 0;
            menupos = clamp_menupos (currsel, maxfile);
        }

        /* ── R / D-Right: jump down PAGE_SIZE, clamp at bottom ── */
        if ((joy & PAD_TRIGGER_R) || (joy & PAD_BUTTON_RIGHT))
        {
            currsel += PAGE_SIZE;
            if (currsel >= maxfile) currsel = maxfile > 0 ? maxfile - 1 : 0;
            menupos = clamp_menupos (currsel, maxfile);
        }

        /* ── A: enter directory / auto-load ── */
        if (joy & PAD_BUTTON_A)
        {
            if (maxfile == 0)
            {
                /* No subdirectories — try basedir directly (game at root) */
                sprintf (megadir, "%s/IPL.TXT", basedir);
                fp = GEN_fopen (megadir, "rb");
                if (fp) { have_ROM = 1; quit = 1; }
            }
            else
            {
                strcpy (scratchdir, basedir);
                if (scratchdir[strlen (scratchdir) - 1] != '/')
                    strcat (scratchdir, "/");

                m = (char *) p[currsel + 1];
                if (m == NULL) { break; }
                strcat (scratchdir, m);

                if (GEN_getdir (scratchdir))
                {
                    maxfile = p[0];
                    currsel = menupos = 0;
                    strcpy (basedir, scratchdir);
                }
                else
                    GEN_getdir (basedir);

                /* Auto-load if IPL.TXT present */
                sprintf (megadir, "%s/IPL.TXT", basedir);
                fp = GEN_fopen (megadir, "rb");
                if (fp) { have_ROM = 1; quit = 1; }
            }
        }

        /* ── Z: unconditionally quit to main menu ── */
        if (joy & PAD_TRIGGER_Z)
        {
            dirsel_back_to_main = 1;
            have_ROM = 0;
            quit = 1;
        }
        /* ── B: go up a directory, or quit when at entry directory ── */
        else if (joy & PAD_BUTTON_B)
        {
            if (strcmp (basedir, entry_basedir) != 0)
            {
                /* Inside a subdirectory — navigate up without quitting */
                strcpy (scratchdir, basedir);
                int slen = (int) strlen (scratchdir);
                if (slen > 0 && scratchdir[slen - 1] == '/')
                    scratchdir[--slen] = 0;

                for (i = slen - 1; i >= 0; i--)
                {
                    if (scratchdir[i] == '/')
                    {
                        if (i == 0) strcpy (scratchdir, "/");
                        else        scratchdir[i] = 0;

                        if (strcmp (scratchdir, root_dir) == 0)
                            sprintf (scratchdir, "%s/", root_dir);

                        if (GEN_getdir (scratchdir))
                        {
                            maxfile = p[0];
                            currsel = menupos = 0;
                            strcpy (basedir, scratchdir);
                        }
                        break;
                    }
                }
            }
            else
            {
                /* At entry directory — quit the browser */
                have_ROM = 0;
                if (DefaultLoadDevice != 0)
                    dirsel_back_to_main = 1;
                quit = 1;
            }
        }

        /* ── X: force-load selected directory ── */
        if (joy & PAD_BUTTON_X)
        {
            if (basedir[strlen (basedir) - 1] != '/')
                strcat (basedir, "/");
            if (maxfile > 0)
            {
                m = (char *) p[currsel + 1];
                strcat (basedir, m);
            }
            have_ROM = 1;
            quit = 1;
        }
    }

    /* Drain any still-held buttons */
    while (PAD_ButtonsHeld (0)) VIDEO_WaitVSync ();
#ifdef HW_RVL
    while (WPAD_ButtonsHeld (0)) WPAD_ScanPads ();
#endif
}
