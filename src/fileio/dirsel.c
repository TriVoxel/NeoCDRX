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

#define PAGE_SIZE 8

char basedir[1024];
char scratchdir[1024];
char megadir[1024];
char dirbuffer[0x10000] ATTRIBUTE_ALIGN (32);

char root_dir[10];
int dirsel_back_to_main = 0;  /* set to 1 to signal caller to return to main menu */
/****************************************************************************
* DrawDirSelector
****************************************************************************/
static void
DrawDirSelector (int maxfile, int menupos, int currsel)
{
  int i;
  int j = 158;
  int *p = (int *) dirbuffer;
  char *m;
  char display[40];

  DrawScreen ();

  for (i = menupos; i < (menupos + PAGE_SIZE) && (i < maxfile); i++)
    {
      m = (char *) p[i + 1];
      memset (display, 0, 40);
      memcpy (display, m, 32);

      if (i == currsel)
        {
          memset (display, 0, 40);
          memcpy (display, m, 32);
          int text_len = strlen(display);
          /* Bar covers the text width + 4px padding each side */
          draw_rainbow_bar(64 - 4, j, (text_len << 4) + 8, 32, 6);
          setfgcolour (COLOR_WHITE);
          bg_transparent = 1;
          gprint (64, j, display, TXT_DOUBLE);
          bg_transparent = 0;
        }
      else
        {
          setfgcolour (COLOR_BLACK);
          bg_transparent = 1;
          gprint (64, j, display, TXT_DOUBLE);
          bg_transparent = 0;
        }

      j += 32;
    }

  ShowScreen ();
}

/****************************************************************************
* DirSelector
*
* A == Enter directory
* B == Parent directory
* X == Set directory
****************************************************************************/
void
DirSelector (void)
{
  int *p = (int *) dirbuffer;
  char *m;
  int maxfile, i;
  int currsel = 0;
  int menupos = 0;
  short joy;
  int quit = 0;
  char entry_basedir[1024];
  GENFILE fp;

  dirsel_back_to_main = 0;
  strcpy(entry_basedir, basedir);

  maxfile = p[0];

  while (!quit)
  {
     DrawDirSelector (maxfile, menupos, currsel);

     joy = getMenuButtons();

     // Scroll displayed directories
     if (joy & PAD_BUTTON_DOWN)
     {
        currsel++;
        if (currsel == maxfile)
           currsel = menupos = 0;
        if ((currsel - menupos) >= PAGE_SIZE)
           menupos += PAGE_SIZE;

     }

     if (joy & PAD_BUTTON_UP)
     {
        currsel--;
        if (currsel < 0)
        {
           currsel = maxfile - 1;
           menupos = (maxfile / PAGE_SIZE) * PAGE_SIZE;
           if (menupos >= maxfile) menupos -= PAGE_SIZE;
           if (menupos < 0) menupos = 0;
        }

        if (currsel < menupos)
           menupos -= PAGE_SIZE;

        if (menupos < 0)
           menupos = 0;

     }

     // Previous page of displayed directories
     if (joy & PAD_TRIGGER_L)
     {
        menupos -= PAGE_SIZE;
        currsel = menupos;
        if (currsel < 0)
        {
           currsel = maxfile - 1;
           menupos = currsel - PAGE_SIZE + 1;
        }

        if (menupos < 0)
           menupos = 0;

     }

     // Next page of displayed directories
     if (joy & PAD_TRIGGER_R)
     {
        menupos += PAGE_SIZE;
        currsel = menupos;
        if (currsel > maxfile)
           currsel = menupos = 0;

     }

     // Go to Next Directory
     if (joy & PAD_BUTTON_A)
     {
        if (maxfile == 0)
        {
           // No subdirectories — try using basedir directly (game at root)
           sprintf(megadir,"%s/IPL.TXT",basedir);
           fp = GEN_fopen(megadir, "rb");
           if (fp)
           {
              have_ROM = 1;
              quit = 1;
           }
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

           // if IPL.TXT found, automount directory
           sprintf(megadir,"%s/IPL.TXT",basedir);
           fp = GEN_fopen(megadir, "rb");
           if (fp)
           {
              have_ROM = 1;
              quit = 1;
           }
        }

     }

     // Z: unconditionally quit to main menu
     if (joy & PAD_TRIGGER_Z) {
        dirsel_back_to_main = 1;
        have_ROM = 0;
        quit = 1;
     }
     // B: go up a directory if in a subdirectory, otherwise quit
     else if (joy & PAD_BUTTON_B) {
        if (strcmp(basedir, entry_basedir) != 0) {
           /* In a subdirectory — go up without quitting */
           strcpy (scratchdir, basedir);
           /* strip trailing slash so parent search works correctly */
           int slen = strlen(scratchdir);
           if (slen > 0 && scratchdir[slen-1] == '/') scratchdir[--slen] = 0;
           for (i = slen - 1; i >= 0; i--)
           {
              if (scratchdir[i] == '/')
              {
                 if (i == 0) strcpy (scratchdir, "/");
                 else scratchdir[i] = 0;
                 if (strcmp (scratchdir, root_dir) == 0)
                    sprintf(scratchdir, "%s/", root_dir);
                 if (GEN_getdir (scratchdir))
                 {
                    maxfile = p[0];
                    currsel = menupos = 0;
                    strcpy (basedir, scratchdir);
                 }
                 break;
              }
           }
        } else {
           /* At base game directory — quit */
           have_ROM = 0;
           if (DefaultLoadDevice != 0)
              dirsel_back_to_main = 1; /* signal: return to main menu */
           /* else DefaultLoadDevice==0: return to drive-select picker */
           quit = 1;
        }
     }

     // LOAD Selected Directory
     if (joy & PAD_BUTTON_X)
     {
        /*** Set basedir to mount point ***/
        if (basedir[strlen (basedir) - 1] != '/')
           strcat (basedir, "/");

        if (maxfile > 0)
        {
           m = (char *) p[currsel + 1];
           strcat (basedir, m);
        }
        // if maxfile==0, use basedir as-is (game files at root)
        have_ROM = 1;
        quit = 1;
     }
  }

  /*** Remove any still held buttons — wait for retrace callback to drain ***/
  while (PAD_ButtonsHeld (0)) VIDEO_WaitVSync();
#ifdef HW_RVL
  while (WPAD_ButtonsHeld(0)) WPAD_ScanPads();
#endif
}
