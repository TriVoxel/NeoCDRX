/****************************************************************************
*   NeoCDRX
*   NeoGeo CD Emulator
*   NeoCD Redux - Copyright (C) 2007 softdev
*
*   DVD File I/O — rewritten to use standard POSIX fopen/fread via the
*   libogc ISO9660 "dvd:" mount point. This replaces the original raw
*   sector-read approach (FindFile + gcsim64) and allows the DVD path to
*   work correctly with ODEs (GCLoader, cubeODE) as well as real drives.
****************************************************************************/

#include <gccore.h>
#include <ogcsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "neocdrx.h"
#include "fileio.h"
#include "iso9660.h"
#include "dirsel.h"

#ifdef HW_RVL
#include <di/di.h>
#endif

#define MAXFILES 32

GENFILEINFO fileinfo[MAXFILES];
static GENHANDLER dvdhandler;

/* One FILE* per open handle — indexed by the same slot as fileinfo[] */
static FILE *dvd_fps[MAXFILES];

/****************************************************************************
* DVDFindFree — return the index of an unused slot, or -1 if full
****************************************************************************/
static int DVDFindFree(void)
{
    int i;
    for (i = 0; i < MAXFILES; i++)
        if (fileinfo[i].handle == (u32)-1) return i;
    return -1;
}

/****************************************************************************
* DVDfopen
*
* Open a file on the disc. Constructs the full "dvd:<basedir>/<filename>"
* path and delegates to the standard C library — which routes through
* libogc's ISO9660 layer and works with both real drives and ODEs.
****************************************************************************/
static u32 DVDfopen(const char *filename, const char *mode)
{
    char fullpath[1024];
    int handle;
    FILE *fp;
    int size;

    /* Read-only: the NeoGeo CD filesystem is never written during emulation */
    if (strstr(mode, "w")) return 0;

    handle = DVDFindFree();
    if (handle == -1) return 0;

    /* Construct full path: basedir may or may not have a trailing slash */
    if (basedir[strlen(basedir) - 1] == '/')
        snprintf(fullpath, sizeof(fullpath), "dvd:%s%s", basedir, filename);
    else
        snprintf(fullpath, sizeof(fullpath), "dvd:%s/%s", basedir, filename);

    fp = fopen(fullpath, "rb");
    if (!fp) return 0;

    /* Measure file length */
    fseek(fp, 0, SEEK_END);
    size = (int)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    fileinfo[handle].handle           = (u32)handle;
    fileinfo[handle].mode             = GEN_MODE_READ;
    fileinfo[handle].length           = size;
    fileinfo[handle].currpos          = 0;
    fileinfo[handle].offset_on_media64 = 0; /* unused in POSIX path */
    dvd_fps[handle]                   = fp;

    return (u32)handle | 0x8000;
}

/****************************************************************************
* DVDfclose
****************************************************************************/
static int DVDfclose(u32 fp)
{
    int handle = (int)(fp & 0x7fff);
    if (fileinfo[handle].handle != (u32)-1)
    {
        if (dvd_fps[handle])
        {
            fclose(dvd_fps[handle]);
            dvd_fps[handle] = NULL;
        }
        fileinfo[handle].handle = (u32)-1;
        return 1;
    }
    return 0;
}

/****************************************************************************
* DVDfread
****************************************************************************/
static u32 DVDfread(char *buf, int block, int len, u32 fp)
{
    int handle = (int)(fp & 0x7fff);
    int bytesrequested, bytesavailable, bytestoread, bytesdone;

    if (fileinfo[handle].handle == (u32)-1 || !dvd_fps[handle]) return 0;

    bytesrequested = block * len;
    bytesavailable = fileinfo[handle].length - fileinfo[handle].currpos;
    bytestoread    = (bytesrequested <= bytesavailable) ? bytesrequested
                                                        : bytesavailable;
    if (bytestoread <= 0) return 0;

    bytesdone = (int)fread(buf, 1, (size_t)bytestoread, dvd_fps[handle]);
    fileinfo[handle].currpos += bytesdone;
    return (u32)bytesdone;
}

/****************************************************************************
* DVDfseek
****************************************************************************/
static int DVDfseek(u32 fp, int where, int whence)
{
    int handle = (int)(fp & 0x7fff);
    if (fileinfo[handle].handle == (u32)-1 || !dvd_fps[handle]) return -1;

    if (fseek(dvd_fps[handle], (long)where, whence) != 0) return 0;

    fileinfo[handle].currpos = (int)ftell(dvd_fps[handle]);
    return 1;
}

/****************************************************************************
* DVDftell
****************************************************************************/
static int DVDftell(u32 fp)
{
    int handle = (int)(fp & 0x7fff);
    if (fileinfo[handle].handle != (u32)-1 && dvd_fps[handle])
        return (int)ftell(dvd_fps[handle]);
    return 0;
}

/****************************************************************************
* DVDfcloseall
****************************************************************************/
static void DVDfcloseall(void)
{
    int i;
    for (i = 0; i < MAXFILES; i++)
    {
        if (dvd_fps[i])
        {
            fclose(dvd_fps[i]);
            dvd_fps[i] = NULL;
        }
    }
    memset(fileinfo, 0xff, sizeof(GENFILEINFO) * MAXFILES);
}

/****************************************************************************
* DVDgetdir — fill dirbuffer with subdirectories of dir; return count
****************************************************************************/
static int DVDgetdir(char *dir)
{
    int *p = (int *)dirbuffer;
    GetSubDirectories(dir, dirbuffer, 0x10000);
    return p[0];
}

/****************************************************************************
* DVDmount — initialise and mount the disc, then open the directory browser
****************************************************************************/
static void DVDmount(void)
{
    memset(basedir,    0, sizeof(basedir));
    memset(scratchdir, 0, sizeof(scratchdir));

    if (!mount_image())
    {
#ifdef HW_RVL
        /* Wii: use DI library for disc insertion detection */
        DI_Init();

        u32 val;
        DI_GetCoverRegister(&val);
        while (val & 0x1)
        {
            InfoScreen((char *)"Please insert disc !");
            DI_GetCoverRegister(&val);
        }
        DI_Mount();
        while (DI_GetStatus() & DVD_INIT)
            usleep(10);

        if (!(DI_GetStatus() & DVD_READY))
        {
            char msg[64];
            sprintf(msg, "DI Status Error: 0x%08X !\n", DI_GetStatus());
            ActionScreen(msg);
            return;
        }

        if (!mount_image())
        {
            ActionScreen((char *)"Error reading disc !");
            return;
        }
#else
        ActionScreen((char *)"Error reading disc !");
        return;
#endif
    }

    /* Set device root and populate directory listing */
    strcpy(root_dir, "/");
    strcpy(basedir,  "/");

    /* Check for IPL.TXT at disc root — game files may be at root with no
     * subdirectories. Try both upper and lower case since ISO9660 may
     * return names in either form depending on the disc image. */
    {
        const char *ipl_paths[] = { "dvd:/IPL.TXT", "dvd:/ipl.txt", NULL };
        int i;
        for (i = 0; ipl_paths[i]; i++)
        {
            FILE *ipl = fopen(ipl_paths[i], "rb");
            if (ipl)
            {
                fclose(ipl);
                have_ROM = 1;
                bannerscreen();
                return;
            }
        }
    }

    if (DVDgetdir(basedir) <= 0)
    {
        /* Last resort: if stat() reports the root is accessible but has no
         * subdirs, the game files may be directly at root — treat it as
         * the game directory and let the emulator find IPL.TXT itself. */
        struct stat st;
        if (stat("dvd:/", &st) == 0)
        {
            have_ROM = 1;
            bannerscreen();
        }
        else
        {
            ActionScreen((char *)"No directories found on disc !");
        }
        return;
    }

    DirSelector();
    bannerscreen();
}

/****************************************************************************
* DVD_SetHandler — register the DVD GENHANDLER
****************************************************************************/
void DVD_SetHandler(void)
{
    memset(&dvdhandler, 0,    sizeof(GENHANDLER));
    memset(fileinfo,    0xff, sizeof(GENFILEINFO) * MAXFILES);
    memset(dvd_fps,     0,    sizeof(dvd_fps));

    dvdhandler.gen_fopen     = DVDfopen;
    dvdhandler.gen_fclose    = DVDfclose;
    dvdhandler.gen_fread     = DVDfread;
    dvdhandler.gen_fseek     = DVDfseek;
    dvdhandler.gen_ftell     = DVDftell;
    dvdhandler.gen_fcloseall = DVDfcloseall;
    dvdhandler.gen_getdir    = DVDgetdir;
    dvdhandler.gen_mount     = DVDmount;

    GEN_SetHandler(&dvdhandler);
}
