/****************************************************************************
* ISO 9660 - libogc DISC_INTERFACE wrapper
*
* Replaces the original raw hardware register-based ISO9660 parser with
* libogc's ISO9660_Mount abstraction. This allows the DVD path to work
* correctly with ODEs (GCLoader, cubeODE) as well as real drives.
*
* Based on DVD mounting technique from Genesis-Plus-GX (Eke-Eke).
****************************************************************************/

#include <gccore.h>
#include <ogcsys.h>
#include <ogc/dvd.h>
/* ISO9660_Mount/Unmount — the system <iso9660.h> is shadowed by the project's
 * own src/fileio/iso9660.h, so we declare these directly instead of including. */
bool ISO9660_Mount(const char *name, DISC_INTERFACE *iface);
bool ISO9660_Unmount(const char *name);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/dir.h>
#include <sys/stat.h>

#ifdef HW_RVL
#include <di/di.h>
#endif

#include "iso9660.h"

/* DVD DISC_INTERFACE — libogc provides __io_gcdvd / __io_wiidvd */
#ifdef HW_RVL
static DISC_INTERFACE *dvd_disc = (DISC_INTERFACE *)&__io_wiidvd;
#else
static DISC_INTERFACE *dvd_disc = (DISC_INTERFACE *)&__io_gcdvd;
#endif

static int dvdInited  = 0;
static int dvdMounted = 0;

/****************************************************************************
* is_ode
*
* Detect whether the drive is an ODE (GCLoader, cubeODE, etc.) or a real
* optical drive. After DVD_Init, real Panasonic/Philips/Sanyo drives identify
* themselves via the version register (offset 0x24 / word 9). They return
* non-zero drive codes; ODEs return 0x00000000.
*
* Returns 1 if an ODE is detected, 0 for a real drive or unknown.
*
* Only meaningful on GC (non-HW_RVL); on Wii DI is used instead.
****************************************************************************/
#ifndef HW_RVL
int is_ode(void)
{
    volatile unsigned long *dvd_reg = (volatile unsigned long *)0xCC006000;
    unsigned long drive_code;

    /* Drive version is in the high byte of register 9 (byte offset 0x24).
     * Real drives: Panasonic=0x20xxxxxx, Philips=0x10xxxxxx, Sanyo=0x30xxxxxx
     * GCLoader / cubeODE: returns 0x00000000 */
    drive_code = dvd_reg[9] >> 24;
    return (drive_code == 0x00) ? 1 : 0;
}
#else
int is_ode(void) { return 0; }
#endif

/*
 * Persistent storage for subdirectory names returned by GetSubDirectories.
 * Pointers into this array are stored in dirbuffer and read back by DirSelector.
 */
#define MAX_SUBDIRS  512
#define MAX_DIRNAME  256
static char subdirnames[MAX_SUBDIRS][MAX_DIRNAME];

#ifndef HW_RVL
/*
 * The libogc GC DVD DISC_INTERFACE has broken startup/isInserted stubs.
 * Patch them at runtime — technique taken from Genesis-Plus-GX.
 */
static bool patchedStartup(DISC_INTERFACE *disc)
{
    (void)disc;
    DVD_Mount();
    return true;
}

static bool patchedIsInserted(DISC_INTERFACE *disc)
{
    (void)disc;
    return true;
}
#endif /* !HW_RVL */

/****************************************************************************
* mount_image
*
* Initialise the DVD interface and mount the disc via libogc ISO9660.
* Returns 1 on success, 0 on failure.
****************************************************************************/
int mount_image(void)
{
    if (!dvdInited)
    {
#ifdef HW_RVL
        DI_Init();
#else
        DVD_Init();
        /* Patch the broken GC stubs so ISO9660_Mount can work */
        *(FN_MEDIUM_STARTUP    *)&dvd_disc->startup    = patchedStartup;
        *(FN_MEDIUM_ISINSERTED *)&dvd_disc->isInserted = patchedIsInserted;
#endif
        dvdInited = 1;
    }

    /* Unmount any existing mount first */
    if (dvdMounted)
    {
        ISO9660_Unmount("dvd:");
        dvdMounted = 0;
    }

    /* Check disc presence */
    if (!dvd_disc->isInserted(dvd_disc))
    {
        return 0;
    }

    /* Mount via libogc — works with real drives and ODEs alike */
    if (!ISO9660_Mount("dvd", dvd_disc))
        return 0;

    dvdMounted = 1;
    return 1;
}

/****************************************************************************
* unmount_image
****************************************************************************/
void unmount_image(void)
{
    if (dvdMounted)
    {
        ISO9660_Unmount("dvd:");
        dvdMounted = 0;
    }
}

/****************************************************************************
* dvd_motor_off
*
* Stop the disc motor on real drives. On ODEs (GCLoader etc.) this is
* effectively a no-op since there is no physical drive motor.
****************************************************************************/
void dvd_motor_off(void)
{
#ifndef HW_RVL
    /* Send motor-off command directly via DVD register interface.
     * DVD_StopMotor() does not exist in libogc2. */
    volatile unsigned long *dvd = (volatile unsigned long *)0xCC006000;
    dvd[0] = 0x2e;
    dvd[1] = 0;
    dvd[2] = 0xe3000000;
    dvd[3] = 0; dvd[4] = 0; dvd[5] = 0; dvd[6] = 0;
    dvd[7] = 1;
    while (dvd[7] & 1) usleep(10);
#else
    DI_StopMotor();
#endif
}

/****************************************************************************
* GetSubDirectories
*
* List subdirectories of 'dir' (e.g. "/" or "/GAME") into buf using the
* same packed format expected by DirSelector:
*
*   buf[0..3]   = int count of entries
*   buf[4..7]   = char* pointer to name of entry 0
*   buf[8..11]  = char* pointer to name of entry 1
*   ...
*
* Pointers point into the persistent subdirnames[][] array.
****************************************************************************/
void GetSubDirectories(char *dir, char *buf, int len)
{
    char fullpath[512];
    DIR *d;
    struct dirent *entry;
    int count = 0;
    int ofs   = 4; /* first 4 bytes reserved for count */

    memset(buf, 0, len);

    /* Build "dvd:<dir>" — dir always starts with '/' */
    snprintf(fullpath, sizeof(fullpath), "dvd:%s", dir);

    /* Strip trailing slash unless it is the mount root ("dvd:/") */
    int plen = strlen(fullpath);
    if (plen > 5 && fullpath[plen - 1] == '/')
        fullpath[plen - 1] = '\0';

    d = opendir(fullpath);
    if (!d)
    {
        memcpy(buf, &count, 4);
        return;
    }

    while ((entry = readdir(d)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0)  continue;
        if (strcmp(entry->d_name, "..") == 0) continue;

        /* libogc ISO9660 readdir often returns DT_UNKNOWN — use stat() to
         * determine if the entry is a directory when d_type is not set. */
        bool is_dir = (entry->d_type == DT_DIR);
        if (!is_dir && entry->d_type == DT_UNKNOWN)
        {
            char entrypath[640];
            struct stat st;
            snprintf(entrypath, sizeof(entrypath), "%s/%s", fullpath, entry->d_name);
            if (stat(entrypath, &st) == 0)
                is_dir = S_ISDIR(st.st_mode);
        }
        if (!is_dir) continue;

        if (count >= MAX_SUBDIRS) break;
        if (ofs + 4 > len) break;

        strncpy(subdirnames[count], entry->d_name, MAX_DIRNAME - 1);
        subdirnames[count][MAX_DIRNAME - 1] = '\0';

        /* Store pointer value in buf (matches original iso9660.c format) */
        char *ptr = subdirnames[count];
        memcpy(buf + ofs, &ptr, 4);
        ofs += 4;
        count++;
    }

    closedir(d);
    memcpy(buf, &count, 4);
}

/****************************************************************************
* FindFile / gcsim64
*
* Retained as stubs for ABI compatibility. dvdfileio.c has been rewritten
* to use standard fopen/fread via the dvd: mount point and no longer calls
* either of these functions.
****************************************************************************/
DIRENTRY *FindFile(char *filename)
{
    (void)filename;
    return NULL;
}

int gcsim64(void *buffer, long long int offset, int length)
{
    (void)buffer;
    (void)offset;
    (void)length;
    return 0;
}
