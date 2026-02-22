/****************************************************************************
* NeoCDRX
*
* GUI File Selector
****************************************************************************/

#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zlib.h>
#include "neocdrx.h"
#include "iso9660.h"
#include <ogc/dvd.h>
#include "backdrop.h"
#include "banner.h"

#ifdef HW_RVL
#include <wiiuse/wpad.h>
#include <di/di.h>
#endif

/*** GC 2D Video ***/
extern unsigned int *xfb[2];
extern int whichfb;
extern GXRModeObj *vmode;

/*** libOGC Default Font ***/
extern u8 console_font_8x16[];

static u32 fgcolour = COLOR_WHITE;
static u32 bgcolour = COLOR_BLACK;
int bg_transparent = 0;  /* when 1, background pixels in drawcharw are skipped (backdrop shows through) */
int bg_rainbow = 0;      /* when 1, background pixels use the scrolling rainbow gradient */

/* Rainbow gradient state — precomputed YCbCr lookup, one entry per pixel column (0-639) */
static u8  rain_y[640];
static u8  rain_cb[640];
static u8  rain_cr[640];
static int rainbow_phase = 0;   /* current horizontal scroll offset, 0-639 */

/* Precompute the ROYGBIVIBGYOR gradient into the rain_* tables.
 * The spectrum spans 0-639px: R at edges, V at center (x=319/320).
 * Each half has 6 segments across 7 colour stops. */
static void init_rainbow(void)
{
  /* 7 stops: R O Y G B I V */
  static const int stops[7][3] = {
    {180,  10,  10},   /* R */
    {240, 140,   0},   /* O */
    {180, 180,   0},   /* Y */
    { 10, 180,  10},   /* G */
    { 10,  10, 180},   /* B */
    { 75,   0, 130},   /* I */
    {110,   0, 175},   /* V */
  };

  int i;
  for (i = 0; i < 640; i++) {
    /* Map pixel to a position in [0, 6] along the half-spectrum.
     * First half (0-319): R->V.  Second half (320-639): V->R (mirror). */
    float pos;
    if (i < 320)
      pos = i * 6.0f / 320.0f;
    else
      pos = (639 - i) * 6.0f / 320.0f;

    int s0 = (int)pos;
    int s1 = s0 + 1;
    if (s1 > 6) s1 = 6;
    float t = pos - s0;

    float r = stops[s0][0] + t * (stops[s1][0] - stops[s0][0]);
    float g = stops[s0][1] + t * (stops[s1][1] - stops[s0][1]);
    float b = stops[s0][2] + t * (stops[s1][2] - stops[s0][2]);

    /* BT.601 limited-range RGB -> YCbCr */
    rain_y[i]  = (u8)( 0.257f*r + 0.504f*g + 0.098f*b + 16.5f);
    rain_cb[i] = (u8)(-0.148f*r - 0.291f*g + 0.439f*b + 128.5f);
    rain_cr[i] = (u8)( 0.439f*r - 0.368f*g - 0.071f*b + 128.5f);
  }
}
static unsigned char background[1280 * 480] ATTRIBUTE_ALIGN (32);
static unsigned char bannerunc[banner_WIDTH * banner_HEIGHT * 2] ATTRIBUTE_ALIGN (32);
static void unpack (void);

unsigned short SaveDevice = 1;            // 0=MemCard, 1=SD/ODE
unsigned char DefaultLoadDevice = 0;      // 0=None,1=SD,2=USB,3=IDE-EXI,4=WKF,5=DVD
unsigned char MenuTrigger = 3;            // 0=L,1=R,2=L+R,3=C-left,4=C-right
unsigned char VideoMode = 0;              // 0=Auto,1=480p,2=480i
unsigned char SkipBios = 0;               // 0=False, 1=True
unsigned char CropOverscan = 1;           // 0=False, 1=True
unsigned char FilterMode = 1;             // 0=Nearest, 1=Bilinear

/* Prefs file path — tried bare (GC/ODE) then sd: prefix (Wii) */
#define PREFS_PATH_A  "/NeoCDRX/NeoCDRXprefs.bin"
#define PREFS_PATH_B  "sd:/NeoCDRX/NeoCDRXprefs.bin"

typedef struct { unsigned char SaveDevice; unsigned char DefaultLoadDevice; unsigned char neogeo_region; unsigned char MenuTrigger; unsigned char VideoMode; unsigned char SkipBios; unsigned char CropOverscan; unsigned char FilterMode; } NeoPrefs;

void save_prefs(void)
{
  NeoPrefs p;
  p.SaveDevice = (unsigned char)SaveDevice;
  p.DefaultLoadDevice = DefaultLoadDevice;
  p.neogeo_region = (unsigned char)neogeo_region;
  p.MenuTrigger = MenuTrigger;
  p.VideoMode = VideoMode;
  p.SkipBios = SkipBios;
  p.CropOverscan = CropOverscan;
  p.FilterMode = FilterMode;

  /* Try subdirectory paths first, then fall back to root of the filesystem.
   * Do NOT call mkdir() — the devkitPPC newlib stub crashes on GC when the
   * underlying fatfs driver doesn't support it via the syscall layer. */
  FILE *fp = fopen(PREFS_PATH_A, "wb");
  if (!fp) fp = fopen(PREFS_PATH_B, "wb");
  if (!fp) fp = fopen("NeoCDRXprefs.bin", "wb");   /* root fallback */
  if (!fp) return;
  fwrite(&p, 1, sizeof(p), fp);
  fclose(fp);
}

void load_prefs(void)
{
  NeoPrefs p;
  FILE *fp = fopen(PREFS_PATH_A, "rb");
  if (!fp) fp = fopen(PREFS_PATH_B, "rb");
  if (!fp) return;
  if (fread(&p, 1, sizeof(p), fp) == sizeof(p)) {
    SaveDevice = p.SaveDevice < 2 ? p.SaveDevice : 1;
    DefaultLoadDevice = p.DefaultLoadDevice < 6 ? p.DefaultLoadDevice : 0;
    neogeo_region = p.neogeo_region < 3 ? p.neogeo_region : 0;
    MenuTrigger = p.MenuTrigger < 5 ? p.MenuTrigger : 3;
    VideoMode = p.VideoMode < 3 ? p.VideoMode : 0;
    SkipBios = p.SkipBios < 2 ? p.SkipBios : 0;
    CropOverscan = p.CropOverscan < 2 ? p.CropOverscan : 1;
    FilterMode = p.FilterMode < 2 ? p.FilterMode : 1;
  }
  fclose(fp);
}

int use_SD  = 0;
int use_USB = 0;
int use_IDE = 0;
int use_WKF = 0;
int use_DVD = 0;

int mega = 0;

/****************************************************************************
* plotpixel
****************************************************************************/
static void
plotpixel (int x, int y)
{
  u32 pixel;

  pixel = xfb[whichfb][(y * 320) + (x >> 1)];

  if (x & 1)
    xfb[whichfb][(y * 320) + (x >> 1)] =
      (pixel & 0xffff00ff) | (COLOR_WHITE & 0xff00);
  else
    xfb[whichfb][(y * 320) + (x >> 1)] =
      (COLOR_WHITE & 0xffff00ff) | (pixel & 0xff00);
}

/****************************************************************************
* roughcircle
****************************************************************************/
static void
roughcircle (int cx, int cy, int x, int y)
{
  if (x == 0)
    {
      plotpixel (cx, cy + y);			/*** Anti ***/
      plotpixel (cx, cy - y);
      plotpixel (cx + y, cy);
      plotpixel (cx - y, cy);
    }
  else
    {
      if (x == y)
	{
	  plotpixel (cx + x, cy + y);		/*** Anti ***/
	  plotpixel (cx - x, cy + y);
	  plotpixel (cx + x, cy - y);
	  plotpixel (cx - x, cy - y);
	}
  else
	{
	  if (x < y)
		{
		  plotpixel (cx + x, cy + y);	/*** Anti ***/
		  plotpixel (cx - x, cy + y);
		  plotpixel (cx + x, cy - y);
		  plotpixel (cx - x, cy - y);
		  plotpixel (cx + y, cy + x);
		  plotpixel (cx - y, cy + x);
		  plotpixel (cx + y, cy - x);
		  plotpixel (cx - y, cy - x);
		}

	}
    }
}

/****************************************************************************
* circle
****************************************************************************/
void
circle (int cx, int cy, int radius)
{
  int x = 0;
  int y = radius;
  int p = (5 - radius * 4) / 4;

  roughcircle (cx, cy, x, y);

  while (x < y)
    {
      x++;
      if (p < 0)
	p += (x << 1) + 1;
      else
	{
	  y--;
	  p += ((x - y) << 1) + 1;
	}
      roughcircle (cx, cy, x, y);
    }
}

/* Draw a rainbow-gradient bar with padding and straight-cut beveled corners.
 * x, y: top-left in screen pixels (x should be even).
 * w: total width including padding, h: height in pixels.
 * bevel: corner chamfer size in pixels.
 * The gradient is a cross-section of the 640px-wide scrolling rainbow. */
void draw_rainbow_bar(int x, int y, int w, int h)
{
  int bevel = 4;
  int row, col;
  for (row = 0; row < h; row++) {
    /* Straight bevel: cut pixels from each corner proportional to distance
     * from the nearest horizontal edge. */
    int cut = 0;
    if (row < bevel)        cut = bevel - row;
    else if (row >= h - bevel) cut = bevel - (h - 1 - row);

    int x0 = x + cut;
    int x1 = x + w - cut;
    /* Align left edge to even pixel (YUY2 word boundary) */
    x0 = (x0 + 1) & ~1;

    int fb_y = y + row;
    for (col = x0; col < x1; col += 2) {
      int px0 = (col     + rainbow_phase) % 640;
      int px1 = (col + 1 + rainbow_phase) % 640;
      u8 Y0 = rain_y[px0], Y1 = rain_y[px1];
      u8 Cb = (u8)(((int)rain_cb[px0] + (int)rain_cb[px1]) >> 1);
      u8 Cr = (u8)(((int)rain_cr[px0] + (int)rain_cr[px1]) >> 1);
      u32 word = ((u32)Y0 << 24) | ((u32)Cb << 16) | ((u32)Y1 << 8) | (u32)Cr;
      xfb[whichfb][(fb_y * 320) + (col >> 1)] = word;
    }
  }
}


static void
drawchar (int x, int y, char c)
{
  int yy, xx;
  u32 colour[2];
  int offset;
  u8 bits;

  offset = (y * 320) + (x >> 1);

  for (yy = 0; yy < 16; yy++)
    {
      bits = console_font_8x16[((c << 4) + yy)];

      for (xx = 0; xx < 4; xx++)
	{
	  if (bits & 0x80)
		colour[0] = fgcolour;
	  else
		colour[0] = bgcolour;

	  if (bits & 0x40)
		colour[1] = fgcolour;
	  else
		colour[1] = bgcolour;

	  xfb[whichfb][offset + xx] =
		(colour[0] & 0xffff00ff) | (colour[1] & 0xff00);

	  bits <<= 2;
	}

      offset += 320;
    }
}

/****************************************************************************
* drawcharw
****************************************************************************/
static void
drawcharw (int x, int y, char c)
{
  int yy, xx;
  int offset;
  int bits;

  offset = (y * 320) + (x >> 1);

  for (yy = 0; yy < 16; yy++)
    {
      bits = console_font_8x16[((c << 4) + yy)];

      for (xx = 0; xx < 8; xx++)
	{
	  if (bits & 0x80)
		xfb[whichfb][offset + xx] = xfb[whichfb][offset + 320 + xx] =
		  fgcolour;
	  else if (bg_rainbow) {
		/* Two pixels per word: px0 at x+xx*2, px1 at x+xx*2+1.
		 * Chroma is averaged (YUY2 4:2:2). Scroll via rainbow_phase. */
		int px0 = (x + xx * 2 + rainbow_phase) % 640;
		int px1 = (x + xx * 2 + 1 + rainbow_phase) % 640;
		u8 Y0 = rain_y[px0], Y1 = rain_y[px1];
		u8 Cb = (u8)(((int)rain_cb[px0] + rain_cb[px1]) >> 1);
		u8 Cr = (u8)(((int)rain_cr[px0] + rain_cr[px1]) >> 1);
		u32 w = ((u32)Y0 << 24) | ((u32)Cb << 16) | ((u32)Y1 << 8) | Cr;
		xfb[whichfb][offset + xx] = xfb[whichfb][offset + 320 + xx] = w;
	  } else if (!bg_transparent)
		xfb[whichfb][offset + xx] = xfb[whichfb][offset + 320 + xx] =
		  bgcolour;

	  bits <<= 1;
	}

      offset += 640;
    }
}

/* drawcharw_clipped — like drawcharw (TXT_DOUBLE, double-height) but only writes
 * pixel word-pairs whose left screen pixel falls within [clip_x0, clip_x1).
 * x, clip_x0 and clip_x1 must all be even (YUY2 word-boundary aligned). */
static void
drawcharw_clipped (int x, int y, char c, int clip_x0, int clip_x1)
{
    int yy, xx;
    int offset = (y * 320) + (x >> 1);
    int bits;

    for (yy = 0; yy < 16; yy++)
    {
        bits = console_font_8x16[((unsigned char)c << 4) + yy];
        for (xx = 0; xx < 8; xx++)
        {
            int px = x + (xx << 1);   /* left screen pixel of this word pair */
            if (px >= clip_x0 && px < clip_x1)
            {
                if (bits & 0x80)
                    xfb[whichfb][offset + xx] = xfb[whichfb][offset + 320 + xx] = fgcolour;
                else if (bg_rainbow)
                {
                    int px0 = (px     + rainbow_phase) % 640;
                    int px1 = (px + 1 + rainbow_phase) % 640;
                    u8 Y0 = rain_y[px0], Y1 = rain_y[px1];
                    u8 Cb = (u8)(((int)rain_cb[px0] + (int)rain_cb[px1]) >> 1);
                    u8 Cr = (u8)(((int)rain_cr[px0] + (int)rain_cr[px1]) >> 1);
                    u32 w = ((u32)Y0 << 24) | ((u32)Cb << 16) | ((u32)Y1 << 8) | Cr;
                    xfb[whichfb][offset + xx] = xfb[whichfb][offset + 320 + xx] = w;
                }
                else if (!bg_transparent)
                    xfb[whichfb][offset + xx] = xfb[whichfb][offset + 320 + xx] = bgcolour;
                /* bg_transparent: leave existing pixel (rainbow bar shows through) */
            }
            /* outside clip region: leave existing pixel untouched */
            bits <<= 1;
        }
        offset += 640;
    }
}

/* gprint_clipped — render a string in TXT_DOUBLE mode, clipping to [clip_x0, clip_x1).
 * x, clip_x0 and clip_x1 must be even.
 * Characters entirely left of clip_x0 are skipped; iteration stops at clip_x1. */
void
gprint_clipped (int x, int y, char *text, int clip_x0, int clip_x1)
{
    int n = strlen (text);
    int i;
    int cx = x;
    for (i = 0; i < n; i++, cx += 16)
    {
        if (cx >= clip_x1) break;           /* rest of string is off the right edge */
        if (cx + 16 <= clip_x0) continue;  /* this char is entirely off the left edge */
        drawcharw_clipped (cx, y, text[i], clip_x0, clip_x1);
    }
}

/****************************************************************************
* gprint
****************************************************************************/
void
gprint (int x, int y, char *text, int mode)
{
  int n;
  int i;

  n = strlen (text);
  if (!n)
    return;

  if (mode != TXT_DOUBLE)
    {
      for (i = 0; i < n; i++, x += 8)
	  drawchar (x, y, text[i]);
    }
  else
    {
      for (i = 0; i < n; i++, x += 16)
	  drawcharw (x, y, text[i]);
    }
}

/****************************************************************************
* DrawScreen
****************************************************************************/
void
DrawScreen (void)
{
  static int inited = 0;
  static int rainbow_tick = 0;

  if (!inited)
    {
      unpack ();
      init_rainbow ();
      inited = 1;
    }

  /* Advance rainbow scroll one pixel every 2 frames (~30px/sec at 60fps) */
  if (++rainbow_tick >= 1) {
    rainbow_tick = 0;
    rainbow_phase = (rainbow_phase + 3) % 640;
  }

  VIDEO_WaitVSync ();

  whichfb ^= 1;
  memcpy (xfb[whichfb], background, 1280 * 480);
}

/****************************************************************************
* ShowScreen
****************************************************************************/
void
ShowScreen (void)
{
  VIDEO_SetNextFramebuffer (xfb[whichfb]);
  VIDEO_Flush ();
  VIDEO_WaitVSync ();
}

/****************************************************************************
* setfgcolour
****************************************************************************/
void
setfgcolour (u32 colour)
{
  fgcolour = colour;
}

/****************************************************************************
* setbgcolour
****************************************************************************/
void
setbgcolour (u32 colour)
{
  bgcolour = colour;
}

/****************************************************************************
* WaitButtonA
****************************************************************************/
void
WaitButtonA (void)
{
  short joy;
  joy = getMenuButtons();

  while (!(joy & PAD_BUTTON_A)) joy = getMenuButtons();
    VIDEO_WaitVSync ();

  while (joy & PAD_BUTTON_A) joy = getMenuButtons();
    VIDEO_WaitVSync ();
}

/****************************************************************************
* ActionScreen
****************************************************************************/
void
ActionScreen (char *msg)
{
  int n;
  char pressa[] = "Press A to continue";

  DrawScreen ();

  n = strlen (msg);
  fgcolour = COLOR_WHITE;
  bgcolour = BMPANE;
  bg_transparent = 1;

  gprint ((640 - (n * 16)) >> 1, 248, msg, TXT_DOUBLE);

  gprint (168, 288, pressa, TXT_DOUBLE);
  bg_transparent = 0;

  ShowScreen ();

  WaitButtonA ();
}

/****************************************************************************
* InfoScreen
****************************************************************************/
void
InfoScreen (char *msg)
{
  int n;

  DrawScreen ();

  n = strlen (msg);
  fgcolour = COLOR_WHITE;
  bgcolour = BMPANE;
  bg_transparent = 1;

  gprint ((640 - (n * 16)) >> 1, 264, msg, TXT_DOUBLE);

  bg_transparent = 0;
  ShowScreen ();
}

/****************************************************************************
* Credits
****************************************************************************/
int credits()
{
int quit = 0;
int ret = 0;
short joy;

char Title[]   = "CREDITS";
char Coder[]   = "Coding: NiuuS";
char Thanks[]  = "Thanks to:";
char Softdev[] = "Softdev and his Neo-CD Redux (GCN) (2007)"; 
char Coders1[] = "Wiimpathy / Jacobeian for NeoCD-Wii (2011)";
char Coders2[] = "infact for Neo-CD Redux (2011)";
char Coders3[] = "megalomaniac for Neo-CD Redux Unofficial (2013-2016)";
char Coders4[] = "TriVoxel for ODE support and rainbow vibes (2026)";
char Fun[]     = "GIGA POWER!";
char iosVersion[20] = {0};
char appVersion[20]= "NeoCD-RX v1.1.00";

#ifdef HW_RVL
	sprintf(iosVersion, "IOS : %d", IOS_GetVersion());
#endif

  DrawScreen ();
  
  fgcolour = COLOR_BLACK;
  bgcolour = BMPANE;

  bg_transparent = 1;
  gprint (250, 160, Title, TXT_DOUBLE);
  gprint (60, 200, Coder, 3);
  gprint (60, 230, Thanks, 3);
  gprint (60, 260, Softdev, 3);
  gprint (60, 280, Coders1, 3);
  gprint (60, 300, Coders2, 3);
  gprint (60, 320, Coders3, 3);
  gprint (60, 340, Coders4, 3);
  gprint (60, 360, Fun, 3);
  gprint (510, 390, iosVersion, 0);
  gprint (60, 390, appVersion, 0);
  bg_transparent = 0;

  ShowScreen ();

  while (quit == 0)
  {
    joy = getMenuButtons();

    if (joy & (PAD_BUTTON_A | PAD_BUTTON_B))
    { 
      quit = 1;
      ret = -1;
    }

  }
  return ret;
}

/****************************************************************************
* unpack
****************************************************************************/
static void
unpack (void)
{
  unsigned long inbytes, outbytes;

  inbytes = backdrop_COMPRESSED;
  outbytes = backdrop_RAW;

  uncompress (background, &outbytes, backdrop, inbytes);

  inbytes = banner_COMPRESSED;
  outbytes = banner_RAW;

  uncompress (bannerunc, &outbytes, banner, inbytes);
}

/****************************************************************************
* LoadingScreen
****************************************************************************/
void
LoadingScreen (char *msg)
{
  int n;

  VIDEO_WaitVSync ();

  whichfb ^= 1;

  n = strlen (msg);
  fgcolour = COLOR_WHITE;
  bgcolour = COLOR_BLACK;
  bg_transparent = 1;

  gprint ((640 - (n * 16)) >> 1, 410, msg, TXT_DOUBLE);
  bg_transparent = 0;

  ShowScreen ();
}

/****************************************************************************
* draw_menu
****************************************************************************/
char menutitle[60] = { "" };
int menu = 0;

static void draw_menu(char items[][22], int maxitems, int minitems, int selected)
{
   int i;
   int j;
   /* Center vertically within the backdrop menu box (263px tall, starting at y=157) */
   if (mega == 1) j = 162;
   else j = 158 + (263 - ((maxitems - minitems) * 32)) / 2;
   int n;
   char msg[] = "";
   n = strlen (msg);


   DrawScreen ();
   
   for( i = minitems; i < maxitems; i++ )
   {
      if ( i == selected )
      {
         int text_x = ( 640 - ( strlen(items[i]) << 4 )) >> 1;
         int bar_x  = text_x - 4;
         int bar_w  = (strlen(items[i]) << 4) + 8;
         draw_rainbow_bar(bar_x, j, bar_w, 32);
         setfgcolour (COLOR_WHITE);
         bg_transparent = 1;
         gprint( text_x, j, items[i], TXT_DOUBLE);
         bg_transparent = 0;
      }
      else
      {
         setfgcolour (COLOR_BLACK);
         bg_transparent = 1;
         gprint( ( 640 - ( strlen(items[i]) << 4 )) >> 1, j, items[i], TXT_DOUBLE);
         bg_transparent = 0;
      }
      j += 32;
   }
   
   if (mega == 0) {
      setfgcolour (COLOR_WHITE);
      setbgcolour (BMPANE);
      gprint ((640 - (n * 16)) >> 1, 162, msg, TXT_DOUBLE);
   }

   ShowScreen();
}

/****************************************************************************
* do_menu
****************************************************************************/
int DoMenu (char items[][22], int maxitems, int minitems)
{
  int quit = 0;
  int ret = 0;
  short joy;

  while (quit == 0)
  {
    draw_menu (&items[0], maxitems, minitems, menu);


    joy = getMenuButtons();

    if (joy & PAD_BUTTON_UP)
    {
      menu--;
      if (menu < minitems) menu = maxitems - 1;
    }

    if (joy & PAD_BUTTON_DOWN)
    {
      menu++;
      if (menu == maxitems) menu = minitems;
    }

    if (joy & PAD_BUTTON_A)
    {
      quit = 1;
      ret = menu;
    }

    if (joy & PAD_BUTTON_B)
    {
      quit = 1;
      ret = -1;
    }
  }
  return ret;
}

/****************************************************************************
* Save_menu - a friendly way to allow user to select location for RXsave.bin
****************************************************************************/
int ChooseMemCard (void)
{
  char titles[6][36];
  int i;
  int quit = 0;
  short joy;

  strncpy(titles[0], "Settings save file not found.",    32);
  strncpy(titles[1], "Please choose a save location by", 34);
  strncpy(titles[2], "pressing one of these buttons:",   32);
  strncpy(titles[3], "",                                  4);
  strncpy(titles[4], "A - SD/ODE",                       12);
  strncpy(titles[5], "B - Memory Card",                  20);

  DrawScreen ();

  fgcolour = COLOR_BLACK;
  bgcolour = BMPANE;
  bg_transparent = 1;

  for (i = 0; i < 6; i++) gprint ((640 - (strlen (titles[i]) * 16)) >> 1, 192 + (i * 32), titles[i], TXT_DOUBLE);
  bg_transparent = 0;

  ShowScreen ();

  while (!quit)
  {
    joy = getMenuButtons();
    if (joy & PAD_BUTTON_A) {
      SaveDevice = 1;  /* SD / ODE — both use same sd:/gcode: path */
      quit = 1;
    }
    if (joy & PAD_BUTTON_B) {
      SaveDevice = 0;             /* Memory Card */
      quit = 1;
    }
  }

  return 1;
}

/****************************************************************************
* Options menu
****************************************************************************/

/* Label for MenuTrigger value */
static const char *menu_trigger_label(unsigned char v)
{
  switch (v) {
    case 0: return "L";
    case 1: return "R";
    case 2: return "L+R";
    case 3: return "C-Left";
    case 4: return "C-Right";
    default: return "C-Right";
  }
}

/* -------------------------------------------------------------------------
 * Video mode helpers
 * ---------------------------------------------------------------------- */

/* Replicates the auto-detection logic from main() startup */
static GXRModeObj *get_auto_vmode(void)
{
#ifdef HW_RVL
  return VIDEO_GetPreferredMode(NULL);
#else
  extern char IPLInfo[256];
  if (VIDEO_HaveComponentCable()) {
    if (strstr(IPLInfo, "PAL") != NULL) return &TVEurgb60Hz480Prog;
    return &TVNtsc480Prog;
  } else {
    if (strstr(IPLInfo, "PAL") != NULL) return &TVEurgb60Hz480IntDf;
    if (strstr(IPLInfo, "NTSC") != NULL) return &TVNtsc480IntDf;
    return VIDEO_GetPreferredMode(NULL);
  }
#endif
}

/* Returns the GXRModeObj for a VideoMode value */
GXRModeObj *vmode_for_setting(unsigned char setting)
{
  switch (setting) {
    case 1: /* 480p */
#ifdef HW_RVL
      return &TVNtsc480Prog;
#else
      if (VIDEO_HaveComponentCable()) {
        extern char IPLInfo[256];
        if (strstr(IPLInfo, "PAL") != NULL) return &TVEurgb60Hz480Prog;
        return &TVNtsc480Prog;
      }
      return get_auto_vmode(); /* no component cable, fall back */
#endif
    case 2: /* 480i */
#ifdef HW_RVL
      return &TVNtsc480IntDf;
#else
      {
        extern char IPLInfo[256];
        if (strstr(IPLInfo, "PAL") != NULL) return &TVEurgb60Hz480IntDf;
        return &TVNtsc480IntDf;
      }
#endif
    default: /* Auto */
      return get_auto_vmode();
  }
}

/* Apply a video mode — reconfigures VI and clears framebuffers */
static void apply_vmode(GXRModeObj *mode)
{
  vmode = mode;
  VIDEO_Configure(vmode);
  VIDEO_ClearFrameBuffer(vmode, xfb[0], COLOR_BLACK);
  VIDEO_ClearFrameBuffer(vmode, xfb[1], COLOR_BLACK);
  VIDEO_SetNextFramebuffer(xfb[0]);
  VIDEO_Flush();
  VIDEO_WaitVSync();
  VIDEO_WaitVSync();
}

static const char *vmode_label(unsigned char v)
{
  switch (v) {
    case 0: return "Auto";
    case 1: return "480p";
    case 2: return "480i";
    default: return "Auto";
  }
}

/* Show a "Keep this video mode? Reverting in N seconds" confirm dialog.
 * Returns 1 if user chose Keep, 0 if Revert (or timer expired). */
static int confirm_vmode(void)
{
  const int TOTAL_SECONDS = 5;
  const int FRAMES_PER_SEC = 60;

  int selected = 0; /* 0=Revert (default), 1=Keep */
  int frames = TOTAL_SECONDS * FRAMES_PER_SEC;
  char countdown[40];
  char keep_item[22];
  char revert_item[22];

  /* Drain any buttons still held from the options menu before starting */
  while (PAD_ButtonsHeld(0)) VIDEO_WaitVSync();

  /* Edge-detection state — initialised fresh each call (not static) */
  u32 last_buttons = 0;

  while (frames > 0)
  {
    int sec_remaining = (frames + FRAMES_PER_SEC - 1) / FRAMES_PER_SEC;

    DrawScreen();
    fgcolour = COLOR_BLACK;
    bgcolour = BMPANE;
    bg_transparent = 1;

    gprint((640 - (21 * 16)) >> 1, 220, (char *)"Keep this video mode?", TXT_DOUBLE);

    snprintf(countdown, sizeof(countdown), "Reverting in %d second%s",
             sec_remaining, sec_remaining == 1 ? " " : "s");
    gprint((640 - (strlen(countdown) * 16)) >> 1, 258, countdown, TXT_DOUBLE);

    /* Draw two buttons: Revert | Keep */
    strncpy(revert_item, "Revert", 21);
    strncpy(keep_item,   " Keep ", 21);

    if (selected == 0) {
      draw_rainbow_bar(178 - 4, 320, 96 + 8, 32);
      setfgcolour(COLOR_WHITE);
      bg_transparent = 1;
      gprint(178, 320, revert_item, TXT_DOUBLE);
      bg_transparent = 0;
      setfgcolour(COLOR_BLACK);
      bg_transparent = 1;
      gprint(370, 320, keep_item, TXT_DOUBLE);
      bg_transparent = 0;
    } else {
      setfgcolour(COLOR_BLACK);
      bg_transparent = 1;
      gprint(178, 320, revert_item, TXT_DOUBLE);
      bg_transparent = 0;
      draw_rainbow_bar(370 - 4, 320, 96 + 8, 32);
      setfgcolour(COLOR_WHITE);
      bg_transparent = 1;
      gprint(370, 320, keep_item, TXT_DOUBLE);
      bg_transparent = 0;
    }

    setfgcolour(COLOR_WHITE);
    setbgcolour(BMPANE);

    ShowScreen();

    u32 held = PAD_ButtonsHeld(0);
    u32 pressed = held & ~last_buttons;
    last_buttons = held;

    if (pressed & PAD_BUTTON_LEFT)  selected = 0;
    if (pressed & PAD_BUTTON_RIGHT) selected = 1;

    if (pressed & PAD_BUTTON_A)
      return selected; /* 1=Keep, 0=Revert */
    if (pressed & PAD_BUTTON_B)
      return 0; /* Revert */

    frames--;
  }

  return 0; /* Timer expired — revert */
}

/* Label for DefaultLoadDevice value */
static const char *load_device_label(unsigned char v)
{
  switch (v) {
    case 0: return "None";
    case 1: return "SD/ODE";
    case 2: return "USB";
    case 3: return "IDE-EXI";
    case 4: return "WKF";
    case 5: return "DVD";
    default: return "None";
  }
}

/****************************************************************************
* Audio menu
****************************************************************************/
int audiomenu()
{
  int prevmenu = menu;
  int quit = 0;
  int ret;
  char buf[22];
  int count = 5;
  static char items[5][22] = {
    { "SFX Volume:       1.0" },
    { "MP3 Volume:       1.0" },
    { "Low Gain:         1.0" },
    { "Mid Gain:         1.0" },
    { "High Gain:        1.0" }

  };
  static float opts[5] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

  menu = 0;

  while (quit == 0)
  {
    sprintf(items[0],"SFX Volume       %1.1f",opts[0]);
    sprintf(items[1],"MP3 Volume       %1.1f",opts[1]);
    sprintf(items[2],"Low Gain         %1.1f",opts[2]);
    sprintf(items[3],"Mid Gain         %1.1f",opts[3]);
    sprintf(items[4],"High Gain        %1.1f",opts[4]);

    ret = DoMenu (&items[0], count, 0);
    switch (ret)
    {
      case -1:
        quit = 1;
        break;
      default:
          opts[menu-0] += 0.1f;
          if ( opts[menu-0] > 2.0f ) opts[menu-0] = 1.0f;
          strcpy(buf, items[menu]);
          buf[18]=0;
          sprintf(items[menu],"%s%1.1f", buf, opts[menu-0]);
         break;
    }
    if (dirsel_back_to_main) { dirsel_back_to_main = 0; quit = 1; }
  }
  mixer_set( opts[0], opts[1], opts[2], opts[3], opts[4]);
  menu = prevmenu;
  return 0;
}

/****************************************************************************
* Graphics menu
****************************************************************************/
int graphicsmenu() {
  int prevmenu = menu;
  int quit = 0;
  int ret;
  int count = 3;
  char items[3][22];

  /* Track VideoMode on entry so we can detect changes on exit */
  unsigned char entry_video_mode = VideoMode;

  menu = 0;

  while (quit == 0)
    {
      snprintf(items[1], 22, "Crop Overscan:%7s", CropOverscan ? "True" : "False");
      snprintf(items[0], 22, "Filter Mode:%9s", FilterMode ? "Bilinear" : "Nearest");
      snprintf(items[2], 22, "Video Mode:   %7s", vmode_label(VideoMode));

      ret = DoMenu (&items[0], count, 0);
      switch (ret)
      {
        case 0:   // Filter mode
          FilterMode = !FilterMode;
          break;
        case 1:   // Crop Overscan
          CropOverscan = !CropOverscan;
          break;
        case 2:   // Video Mode — cycle only, apply on exit
          VideoMode++;
          if (VideoMode > 2) VideoMode = 0;
          break;
        case -1:
          quit = 1;
          break;
      }
    }

  /* If VideoMode changed, apply and show confirm dialog */
  if (VideoMode != entry_video_mode)
  {
    GXRModeObj *old_vmode = vmode;
    unsigned char old_setting = entry_video_mode;

    apply_vmode(vmode_for_setting(VideoMode));

    if (!(confirm_vmode()))
    {
      VideoMode = old_setting;
      apply_vmode(old_vmode);
    }
  }
  save_prefs();
  menu = prevmenu;
  return 0;
}

int optionmenu() {
  int prevmenu = menu;
  int quit = 0;
  int ret;
  int num_save_devices = 2;
  int count = 7;
  char items[7][22];

  menu = 0;

  while (quit == 0)
    {
    if      (neogeo_region == 0) sprintf(items[0], "Region:         JAPAN");
    else if (neogeo_region == 1) sprintf(items[0], "Region:           USA");
    else                         sprintf(items[0], "Region:        EUROPE");
    if (SaveDevice == 0)         sprintf(items[1], "Save Device: MEM Card");
    else                         sprintf(items[1], "Save Device:   SD/ODE");

    snprintf(items[2], 22, "Load Device: %8s", load_device_label(DefaultLoadDevice));
    snprintf(items[3], 22, "Menu Toggle: %8s", menu_trigger_label(MenuTrigger));
    snprintf(items[4], 22, "Skip BIOS:   %8s", SkipBios ? "True" : "False");
    snprintf(items[5], 22, "FX/Music Equalizer  >");
    snprintf(items[6], 22, "Graphics Settings   >");

    ret = DoMenu (&items[0], count, 0);
    switch (ret)
      {
      case 0:   // BIOS Region
        neogeo_region++;
        if (neogeo_region > 2) neogeo_region = 0;
        break;

      case 1:   // Save Device
        SaveDevice++;
        if (SaveDevice >= num_save_devices) SaveDevice = 0;
        break;

      case 2:   // Default Load Device
      {
#ifdef HW_RVL
        DefaultLoadDevice++;
        if      (DefaultLoadDevice > 3) DefaultLoadDevice = 0;
#else
        if      (DefaultLoadDevice == 0) DefaultLoadDevice = 1;
        else if (DefaultLoadDevice == 1) DefaultLoadDevice = 3;
        else if (DefaultLoadDevice == 3) DefaultLoadDevice = 4;
        else if (DefaultLoadDevice == 4) DefaultLoadDevice = 5;
        else                             DefaultLoadDevice = 0;
#endif
        break;
      }

      case 3:   // Menu Toggle
        MenuTrigger++;
        if (MenuTrigger > 4) MenuTrigger = 0;
        break;

      case 4:   // Skip BIOS
        SkipBios = !SkipBios;
        break;

      case 5:   // FX / Music Equalizer
        audiomenu();
        break;

      case 6:
        graphicsmenu();
        break;

      case -1:
        quit = 1;
        break;
    }
  }
  save_prefs();
  menu = prevmenu;
  return 0;
}

/****************************************************************************
 * Load menu
 *
 ****************************************************************************/
static u8 load_menu = 0;

int loadmenu ()
{
  int prevmenu = menu;
  int ret,count;
  int quit = 0;

  /* If a default load device is set, skip the picker and go straight to it */
  if (DefaultLoadDevice != 0)
  {
    use_SD = use_USB = use_IDE = use_WKF = use_DVD = 0;
    switch (DefaultLoadDevice)
    {
      case 1: use_SD  = 1; break;
      case 2: use_USB = 1; break;
      case 3: use_IDE = 1; break;
      case 4: use_WKF = 1; break;
      case 5: use_DVD = 1; break;
    }
    InfoScreen((char *) "Mounting media");
    if (DefaultLoadDevice == 5)
      DVD_SetHandler();
    else
      SD_SetHandler();
    GEN_mount();
    if (have_ROM == 1) return 1;
    if (dirsel_back_to_main) { dirsel_back_to_main = 0; return 0; }
    /* Mount failed — fall through to the normal picker */
  }

#ifdef HW_RVL
  count = 6;
  char item[6][22] = {
    {"Load from SD"},
    {"Load from USB"},
    {"Load from IDE-EXI"},
    {"Load from DVD"},
    {"Stop DVD Motor"},
    {"Go Back"}
  };
#else
  DVD_Init();
  int ode_detected = is_ode();

  count = ode_detected ? 4 : 6;
  char item[6][22];
  if (ode_detected) {
    strncpy(item[0], "Load from SD",      21);
    strncpy(item[1], "Load from IDE-EXI", 21);
    strncpy(item[2], "Load from WKF",     21);
    strncpy(item[3], "Go Back",           21);
  } else {
    strncpy(item[0], "Load from SD",      21);
    strncpy(item[1], "Load from IDE-EXI", 21);
    strncpy(item[2], "Load from WKF",     21);
    strncpy(item[3], "Load from DVD",     21);
    strncpy(item[4], "Stop DVD Motor",    21);
    strncpy(item[5], "Go Back",           21);
  }
#endif

  menu = load_menu;

  while (quit == 0)
  {
     use_SD  = 0;
     use_USB = 0;
     use_IDE = 0;
     use_WKF = 0;
     use_DVD = 0;
     ret = DoMenu (&item[0], count, 0);

#ifndef HW_RVL
     if (ode_detected) {
       switch (ret)
       {
         case -1:
         case 3:
           quit = 1;
           break;
         case 0:
           use_SD = 1;
           InfoScreen((char *) "Mounting media");
           SD_SetHandler();
           GEN_mount();
           if (have_ROM == 1) return 1;
           break;
         case 1:
           use_IDE = 1;
           InfoScreen((char *) "Mounting media");
           SD_SetHandler();
           GEN_mount();
           if (have_ROM == 1) return 1;
           break;
         case 2:
           use_WKF = 1;
           InfoScreen((char *) "Mounting media");
           SD_SetHandler();
           GEN_mount();
           if (have_ROM == 1) return 1;
           break;
       }
       if (dirsel_back_to_main) { dirsel_back_to_main = 0; quit = 1; }
       continue;
     }
#endif

     switch (ret)
     {
        case -1:
        case 5:
           quit = 1;
           break;

#ifdef HW_RVL
        case 1:
           use_USB = 1;
           InfoScreen((char *) "Mounting media");
           SD_SetHandler();
           GEN_mount();
           if (have_ROM == 1) return 1;
           break;

        case 2:
#else
        case 2:
           use_WKF = 1;
           InfoScreen((char *) "Mounting media");
           SD_SetHandler();
           GEN_mount();
           if (have_ROM == 1) return 1;
           break;

        case 1:
#endif
           use_IDE = 1;
           InfoScreen((char *) "Mounting media");
           SD_SetHandler();
           GEN_mount();
           if (have_ROM == 1) return 1;
           break;

        case 3:
           use_DVD = 1;
           InfoScreen((char *) "Mounting media");
           DVD_SetHandler();
           GEN_mount();
           if (have_ROM == 1) return 1;
           break;

        case 4:
           InfoScreen((char *) "Stopping DVD drive...");
           dvd_motor_off();
           break;

        default:
           use_SD  = 1;
           InfoScreen((char *) "Mounting media");
           SD_SetHandler();
           GEN_mount();
           if (have_ROM == 1) return 1;
           break;
     }
     if (dirsel_back_to_main) { dirsel_back_to_main = 0; quit = 1; }
  }

  menu = prevmenu;
  return 0;
}

/****************************************************************************
 * Main Menu
 *
 ****************************************************************************/

int load_mainmenu()
{
  s8 ret;
  u8 quit = 0;
  if (have_ROM) {
    menu = 0;
  }
  else {
    menu = 2;
  }

  /* Build menu dynamically — Play/Reset only shown when a game is loaded */
  char items[6][22];
  u8 count;

  VIDEO_Configure (vmode);
  VIDEO_ClearFrameBuffer(vmode, xfb[whichfb], COLOR_BLACK);
  VIDEO_Flush();
  VIDEO_WaitVSync();
  VIDEO_WaitVSync();

  while (quit == 0)
  {
    strncpy(items[0], "Resume Game",   21);
    strncpy(items[1], "Reset Game",    21);
    /*                "Load/Change Game" */
    strncpy(items[3], "Settings",      21);
    strncpy(items[4], "Credits",       21);
    strncpy(items[5], "Exit",          21);
    count = 6;

    if (have_ROM) {
      strncpy(items[2], "Change Game",   21);
      ret = DoMenu (&items[0], count, 0);
    }
    else {
      strncpy(items[2], "Load Game",     21);
      ret = DoMenu (&items[0], count, 2);
    }

    switch (ret)
    {
      case -1:
      case  0:
        ret = 0;
        quit = 1;
        break;

      case 1:
        neogeo_reset();
        YM2610_sh_reset();
        ret = 0;
        quit = 1;
        break;

      case 2:
      {
        if (have_ROM)
        {
          {
            int saved_have_ROM = have_ROM;
            int changed = loadmenu();
            if (changed)
              quit = 1;      /* new game selected — exit main menu and let emulator restart */
            else
              have_ROM = saved_have_ROM; /* backed out — restore so Resume/Reset remain available */
            break;
          }
        }
        else
        {
          quit = loadmenu();
          break;
        }
      }

      case 3:
        optionmenu();
        break;

      case 4:
        credits();
        break;

      case 5:
        VIDEO_ClearFrameBuffer(vmode, xfb[whichfb], COLOR_BLACK);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        neogeocd_exit();
        break;
    }
  }

  while(PAD_ButtonsHeld(0)) VIDEO_WaitVSync();
#ifdef HW_RVL
  while(WPAD_ButtonsHeld(0)) WPAD_ScanPads();
#endif

  return ret;
}

/****************************************************************************
* bannerscreen
****************************************************************************/
void
bannerscreen (void)
{
  int y, x, j;
  int offset;
  int *bb = (int *) bannerunc;

  whichfb ^= 1;
  offset = (200 * 320) + 40;
  VIDEO_ClearFrameBuffer (vmode, xfb[whichfb], COLOR_BLACK);

  for (y = 0, j = 0; y < banner_HEIGHT; y++)
    {
      for (x = 0; x < (banner_WIDTH >> 1); x++)
		xfb[whichfb][offset + x] = bb[j++];

      offset += 320;
    }

  VIDEO_SetNextFramebuffer (xfb[whichfb]);
  VIDEO_Flush ();
  VIDEO_WaitVSync ();
}
