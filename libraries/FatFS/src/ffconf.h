/* FatFs R0.16 configuration for this core.
 *
 * Only the options that differ from ChaN's defaults, or that are worth
 * justifying, carry a comment. The rest are the stock values.
 */
#define FFCONF_DEF 80386   /* R0.16 -- must match FF_DEFINED in ff.h */

/*---------------------------------------------------------------------------/
/ Function Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_READONLY  0
#define FF_FS_MINIMIZE  0

/* Arduino's File is a Stream, so gets()/putc()/printf() on it are expected to
 * work. That is what FF_USE_STRFUNC provides. */
#define FF_USE_STRFUNC  1
#define FF_PRINT_LLI    1
#define FF_PRINT_FLOAT  0   /* pulls in the float formatter; Print already has one */
#define FF_STRF_ENCODE  0

/* f_mkfs, so a sketch can format a card that arrives blank. Its work buffer is
 * passed in by the caller, so enabling it costs code and no RAM. */
#define FF_USE_MKFS     1

#define FF_USE_FASTSEEK 0
#define FF_USE_EXPAND   0
#define FF_USE_CHMOD    1
#define FF_USE_LABEL    1
#define FF_USE_FORWARD  0

/*---------------------------------------------------------------------------/
/ Namespace and Locale
/---------------------------------------------------------------------------*/

/* 437 (US) rather than 0 (all pages). 0 compiles every conversion table in
 * ffunicode.c, which is 10,000 lines of them, and requires f_setcp() to be
 * called before anything works. One page is the right trade for a card in a
 * dev board. */
#define FF_CODE_PAGE    437

/* Long filenames, with a static working buffer.
 *
 * 1 rather than 2 or 3: 2 puts the 255-character buffer on the stack, which is
 * 512 bytes of it per call, and 3 puts it on the heap and needs ff_memalloc
 * from ffsystem.c. Static is the right choice for a single-threaded core and
 * makes FF_FS_REENTRANT unnecessary. */
#define FF_USE_LFN      1
#define FF_MAX_LFN      255

/* 0 = ANSI/OEM strings, so paths are plain char* the way every Arduino sketch
 * writes them. Switching this to 1 would make them UTF-16 and break every
 * existing sketch's F("/data.txt"). */
#define FF_LFN_UNICODE  0
#define FF_LFN_BUF      255
#define FF_SFN_BUF      12
#define FF_FS_RPATH     2   /* f_chdir/f_getcwd, which SDFS uses for openDir */

/*---------------------------------------------------------------------------/
/ Drive/Volume Configurations
/---------------------------------------------------------------------------*/

/* Two: volume 0 is the internal flash (libraries/FatFS), volume 1 is the SD
 * card (libraries/SDFS). Both are 512-byte-sector devices -- the flash gets
 * there through a translation layer -- so FF_MIN_SS and FF_MAX_SS stay equal
 * and FatFs keeps its fixed-sector fast paths.
 *
 * Mounting one costs nothing for the other: the volume table is two pointers,
 * and each filesystem registers its disk driver only when it starts. */
#define FF_VOLUMES      2
#define FF_STR_VOLUME_ID 0
#define FF_MULTI_PARTITION 0

/* 0 = ask the disk layer via disk_ioctl(GET_SECTOR_SIZE). The card is always
 * 512, but going through the ioctl keeps the block driver the single place
 * that states it. */
#define FF_MIN_SS       512
#define FF_MAX_SS       512

#define FF_LBA64        0
#define FF_MIN_GPT      0x10000000
#define FF_USE_TRIM     0

/*---------------------------------------------------------------------------/
/ System Configurations
/---------------------------------------------------------------------------*/

#define FF_FS_TINY      0
#define FF_FS_EXFAT     0   /* patent-encumbered; SDXC cards must be reformatted */
#define FF_FS_NORTC     0   /* get_fattime() is supplied, see ch32h4_diskio.c */
#define FF_NORTC_MON    1
#define FF_NORTC_MDAY   1
#define FF_NORTC_YEAR   2026

#define FF_FS_NOFSINFO  0
#define FF_FS_LOCK      0

/* Not reentrant, and it does not need to be: there is one core running
 * sketches and no RTOS. Enabling it would want ff_mutex_* from ffsystem.c. */
#define FF_FS_REENTRANT 0
#define FF_FS_TIMEOUT   1000
