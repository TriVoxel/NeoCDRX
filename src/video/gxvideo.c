/****************************************************************************
*   NeoCDRX
*   NeoGeo CD Emulator
*   NeoCD Redux - Copyright (C) 2007 softdev
****************************************************************************/

/****************************************************************************
* NeoCDRX
*
* GX Video — integer-scaled orthographic renderer
*
* The Neo Geo outputs 320x224 pixels.  We display at the highest integer
* multiple that fits the current EFB:
*
*   480p  efbHeight=480 : scale=2  → 640x448, centred (16px bars top/bottom)
*   480i  efbHeight=240 : scale=1  → 320x224 in EFB; GX YScale doubles to
*                                     448 visible lines (same result on screen)
*
* With CropOverscan: the leftmost 8 source pixels are skipped, giving
* 312x224 source pixels.  At 2x that is 624x448, centred horizontally
* (8px black border each side).
*
* The projection is orthographic with 1 GX unit = 1 EFB pixel, so quad
* coordinates are in screen-pixel space and there is no approximation.
****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gccore.h>
#include "neocdrx.h"

/*** External 2D Video ***/
extern u32 whichfb;
extern u32 *xfb[2];
extern GXRModeObj *vmode;

/*** 3D GX ***/
#define DEFAULT_FIFO_SIZE ( 256 * 1024 )
#define TEXSIZE ( (NEOSCR_WIDTH * NEOSCR_HEIGHT) * 2 )

static u8 gp_fifo[DEFAULT_FIFO_SIZE] ATTRIBUTE_ALIGN (32);
static u8 texturemem[TEXSIZE] ATTRIBUTE_ALIGN (32);

GXTexObj texobj;
int vwidth, vheight, oldvwidth, oldvheight;

/****************************************************************************
 * draw_init
 * Vertex format: positions are f32 XYZ (DIRECT), colours are RGBA8 (DIRECT),
 * texture coords are f32 ST (DIRECT).
 ****************************************************************************/
static void
draw_init (void)
{
  GX_ClearVtxDesc ();
  GX_SetVtxDesc (GX_VA_POS,  GX_DIRECT);
  GX_SetVtxDesc (GX_VA_CLR0, GX_DIRECT);
  GX_SetVtxDesc (GX_VA_TEX0, GX_DIRECT);

  GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_POS,  GX_POS_XYZ,   GX_F32,   0);
  GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA,  GX_RGBA8, 0);
  GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST,    GX_F32,   0);

  GX_SetNumTexGens (1);
  GX_SetTexCoordGen (GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

  GX_InvalidateTexAll ();

  GX_InitTexObj (&texobj, texturemem, vwidth, vheight, GX_TF_RGB565,
         GX_CLAMP, GX_CLAMP, GX_FALSE);
  GX_InitTexObjFilterMode (&texobj, GX_NEAR, GX_NEAR);
}

/****************************************************************************
 * draw_square
 * Orthographic projection: 1 GX unit = 1 EFB pixel.
 * Quad coordinates are computed from the current vmode and CropOverscan flag
 * so every source pixel maps to exactly one or more destination pixels.
 ****************************************************************************/
static void
draw_square (void)
{
  Mtx44 proj;
  Mtx   model;

  int efb_w = vmode->fbWidth;       /* always 640 for GC/Wii */
  int efb_h = vmode->efbHeight;     /* 480 for 480p, 240 for 480i */

  /* Integer scale: largest multiple of source dimensions that fits */
  int scale_x = efb_w / NEOSCR_WIDTH;   /* 640/320 = 2 */
  int scale_y = efb_h / NEOSCR_HEIGHT;  /* 480/224 = 2  or  240/224 = 1 */
  int scale   = (scale_x < scale_y) ? scale_x : scale_y;
  if (scale < 1) scale = 1;

  /* Source pixel region (UV-level crop) */
  int src_x0 = CropOverscan ? (8 * scale) : 0;
  int src_w  = NEOSCR_WIDTH - (src_x0 * 2);

  /* Display size in EFB pixels */
  int disp_w = src_w  * scale;
  int disp_h = NEOSCR_HEIGHT * scale;

  /* Centre in EFB */
  float x0 = (efb_w - disp_w) * 0.5f;
  float y0 = (efb_h - disp_h) * 0.5f;
  float x1 = x0 + disp_w;
  float y1 = y0 + disp_h;

  /* UV coordinates */
  float uv_l = (float)src_x0 / (float)NEOSCR_WIDTH;
  float uv_r = (float)1 - uv_l;

  /* Ortho projection: maps (0,0)..(efb_w,efb_h) to clip space */
  guOrtho (proj, 0.0f, (f32)efb_h, 0.0f, (f32)efb_w, 0.0f, 1.0f);
  GX_LoadProjectionMtx (proj, GX_ORTHOGRAPHIC);

  guMtxIdentity (model);
  GX_LoadPosMtxImm (model, GX_PNMTX0);

  GX_Begin (GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32 (x0, y0, 0.0f);  GX_Color1u32 (0xFFFFFFFF);  GX_TexCoord2f32 (uv_l, 0.0f);
    GX_Position3f32 (x1, y0, 0.0f);  GX_Color1u32 (0xFFFFFFFF);  GX_TexCoord2f32 (uv_r, 0.0f);
    GX_Position3f32 (x1, y1, 0.0f);  GX_Color1u32 (0xFFFFFFFF);  GX_TexCoord2f32 (uv_r, 1.0f);
    GX_Position3f32 (x0, y1, 0.0f);  GX_Color1u32 (0xFFFFFFFF);  GX_TexCoord2f32 (uv_l, 1.0f);
  GX_End ();
}

/****************************************************************************
 * StartGX
 ****************************************************************************/
void
StartGX (void)
{
  GXColor gxbackground = { 0, 0, 0, 0xff };

  memset (&gp_fifo, 0, DEFAULT_FIFO_SIZE);

  GX_Init (&gp_fifo, DEFAULT_FIFO_SIZE);
  GX_SetCopyClear (gxbackground, 0x00ffffff);

  GX_SetViewport (0, 0, vmode->fbWidth, vmode->efbHeight, 0, 1);
  GX_SetDispCopyYScale ((f32) vmode->xfbHeight / (f32) vmode->efbHeight);
  GX_SetScissor (0, 0, vmode->fbWidth, vmode->efbHeight);
  GX_SetDispCopySrc (0, 0, vmode->fbWidth, vmode->efbHeight);
  GX_SetDispCopyDst (vmode->fbWidth, vmode->xfbHeight);
  GX_SetCopyFilter (vmode->aa, vmode->sample_pattern, GX_TRUE, vmode->vfilter);
  GX_SetFieldMode (vmode->field_rendering,
                   ((vmode->viHeight == 2 * vmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));

  GX_SetPixelFmt (GX_PF_RGB8_Z24, GX_ZC_LINEAR);
  GX_SetCullMode (GX_CULL_NONE);
  GX_CopyDisp (xfb[whichfb ^ 1], GX_TRUE);
  GX_SetDispCopyGamma (GX_GM_1_0);

  memset (texturemem, 0, TEXSIZE);
  vwidth  = 100;
  vheight = 100;
}

/****************************************************************************
 * Update Video
 ****************************************************************************/
void
update_video (int width, int height, char *vbuffer)
{
  int h, w;
  long long int *dst  = (long long int *) texturemem;
  long long int *src1 = (long long int *) vbuffer;
  long long int *src2 = (long long int *) (vbuffer + 640);
  long long int *src3 = (long long int *) (vbuffer + 1280);
  long long int *src4 = (long long int *) (vbuffer + 1920);

  vwidth  = 320;
  vheight = 224;

  whichfb ^= 1;

  if ((oldvheight != vheight) || (oldvwidth != vwidth))
    {
      oldvwidth  = vwidth;
      oldvheight = vheight;
      draw_init ();

      GX_SetViewport (0, 0, vmode->fbWidth, vmode->efbHeight, 0, 1);
    }

  GX_InvVtxCache ();
  GX_InvalidateTexAll ();
  GX_SetTevOp (GX_TEVSTAGE0, GX_DECAL);
  GX_SetTevOrder (GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

  for (h = 0; h < vheight; h += 4)
    {
      for (w = 0; w < 80; w++)
        {
          *dst++ = *src1++;
          *dst++ = *src2++;
          *dst++ = *src3++;
          *dst++ = *src4++;
        }
      src1 += 240;
      src2 += 240;
      src3 += 240;
      src4 += 240;
    }

  DCFlushRange (texturemem, TEXSIZE);

  GX_SetNumChans (1);
  GX_LoadTexObj (&texobj, GX_TEXMAP0);

  draw_square ();

  GX_DrawDone ();

  GX_SetZMode (GX_TRUE, GX_LEQUAL, GX_TRUE);
  GX_SetColorUpdate (GX_TRUE);
  GX_CopyDisp (xfb[whichfb], GX_TRUE);
  GX_Flush ();

  VIDEO_SetNextFramebuffer (xfb[whichfb]);
  VIDEO_Flush ();

  VIDEO_WaitVSync ();
}
