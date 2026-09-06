#include "FatFS.h"

#include <string.h>

#include "ch32h4_fatfs_disk.h"

/* This core's convention, from cores/ch32h4/FS.cpp: filesystem diagnostics are
 * compiled out unless the build asks for them, because a printf in the
 * filesystem path would pull stdio into every sketch that opens a file. Build
 * with -DFS_DEBUG to see them. The machine-readable half is
 * ch32h4_fatfs_last_error(), which is always available. */
#ifndef DEBUGV
#ifdef FS_DEBUG
#define DEBUGV(fmt, ...)  ::printf(fmt, ##__VA_ARGS__)
#else
#define DEBUGV(...)       do { } while (0)
#endif
#endif

/* The filesystem partition, from the linker script. The array form rather than
 * `extern uint8_t _FS_start;` and taking its address: both work, and this one
 * cannot be read as a variable by mistake. */
extern "C" char _FS_start[];
extern "C" char _FS_end[];

/* FatFs volume 0. Volume 1 is the SD card, which libraries/SDFS mounts; see
 * ch32h4_fatfs_disk.h for the numbering. */
#define FLASH_VOL "0:"

static uint32_t partitionBase() {
    return (uint32_t)(uintptr_t)_FS_start;
}

static uint32_t partitionSize() {
    return (uint32_t)(_FS_end - _FS_start);
}

/* ---- the disk driver for volume 0 --------------------------------------- */

/* The mounted instance's translation layer, for the C callbacks below and for
 * the block API FatFSUSB uses. There is one flash volume, so one pointer
 * rather than a lookup. */
static CH32H4FTLFlash *s_flash = nullptr;
static SPIFTL *s_ftl = nullptr;
static ch32h4_fatfs_error s_err = CH32H4_FATFS_OK;

/* How many times FatFs has asked us to flush, and how many of those the
 * translation layer accepted. Exposed because "the file vanished after a
 * reboot" has two very different causes -- FatFs never asking, or the layer
 * failing to write -- and they look identical from outside. */
static uint32_t s_syncs = 0;
static uint32_t s_syncFails = 0;

/* Whether the translation layer's map has been restored from flash in this
 * boot. start() RELOADS the map, so calling it on a layer that is already
 * running throws away every mapping made since the last persist -- which is
 * exactly what begin()-after-format() used to do, losing the volume f_mkfs
 * had just written. Once per boot is both sufficient and necessary. */
static bool s_ftlStarted = false;

/* The last few sectors written, for diagnosis. "The file is gone after a
 * remount" cannot distinguish FatFs never writing the directory from the
 * layer below losing it, and those need opposite fixes. */
#define WLOG_N 24
static uint32_t s_wlog[WLOG_N];
static uint32_t s_wlogCount = 0;

static DSTATUS flash_status(void) {
    return s_ftl ? 0 : STA_NOINIT;
}

static DSTATUS flash_initialize(void) {
    return flash_status();
}

static DRESULT flash_read(BYTE *buff, LBA_t sector, UINT count) {
    if (!s_ftl) {
        return RES_NOTRDY;
    }
    for (UINT i = 0; i < count; i++) {
        if (!s_ftl->read((int)(sector + i), buff + i * 512)) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

#if FF_FS_READONLY == 0

static DRESULT flash_write(const BYTE *buff, LBA_t sector, UINT count) {
    if (!s_ftl) {
        return RES_NOTRDY;
    }
    for (UINT i = 0; i < count; i++) {
        /* The FIRST N, not the last: what a format writes matters more than
           what trails it, and sector 0 is the one in question. */
        if (s_wlogCount < WLOG_N) {
            s_wlog[s_wlogCount] = (uint32_t)(sector + i);
        }
        s_wlogCount++;
        if (!s_ftl->write((int)(sector + i), buff + i * 512)) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

#endif

static DRESULT flash_ioctl(BYTE cmd, void *buff) {
    if (!s_ftl) {
        return RES_NOTRDY;
    }
    switch (cmd) {
        case CTRL_SYNC: {
            /* Push the mapping to flash. Without this the filesystem is
             * intact until the first power cycle and then is not, which is
             * the worst possible time to find out. */
            s_syncs++;
            const bool ok = s_ftl->persist();
            if (!ok) {
                s_syncFails++;
            }
            return ok ? RES_OK : RES_ERROR;
        }

        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = (LBA_t)s_ftl->lbaCount();
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD *)buff = 512;
            return RES_OK;

        case GET_BLOCK_SIZE:
            /* In sectors. The translation layer already spreads writes, so
             * there is nothing here for f_mkfs to usefully align to. */
            *(DWORD *)buff = 1;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}

static const ch32h4_fatfs_disk_ops s_flash_ops = {
    flash_status,
    flash_initialize,
    flash_read,
#if FF_FS_READONLY == 0
    flash_write,
#else
    NULL,
#endif
    flash_ioctl
};

/* ---- paths -------------------------------------------------------------- */

/* As in SDFS: normalise, and carry the volume prefix, so this is the single
 * point where a sketch-visible path becomes a FatFs one.
 *
 * Idempotent, and it has to be -- a Dir hands its entries back through the
 * public open(), and those have already been through here. Without this a
 * listing would open "0:/0:/log/x". */
static String fixPath(const char *path) {
    if (!path || !path[0]) {
        return String(FLASH_VOL "/");
    }
    if (!strncmp(path, FLASH_VOL, sizeof(FLASH_VOL) - 1)) {
        return String(path);
    }
    if (path[0] == '/') {
        return String(FLASH_VOL) + path;
    }
    return String(FLASH_VOL "/") + path;
}

static bool fatOk(FRESULT r) {
    return r == FR_OK;
}

/* ---- File --------------------------------------------------------------- */

class FatFSFileImpl : public FileImpl {
public:
    FatFSFileImpl(FatFSImpl *fs, const char *name, FIL fil, bool writable)
        : _fs(fs), _fil(fil), _opened(true), _writable(writable) {
        _name = strdup(name);
    }

    ~FatFSFileImpl() override {
        close();
        free(_name);
    }

    size_t write(const uint8_t *buf, size_t size) override {
        if (!_opened || !_writable) {
            return 0;
        }
        UINT written = 0;
        if (f_write(&_fil, buf, (UINT)size, &written) != FR_OK) {
            return 0;
        }
        return written;
    }

    /* int, not size_t: the FS API returns a negative value for an error and
     * zero for end of file, and those have to stay distinguishable. */
    int read(uint8_t *buf, size_t size) override {
        if (!_opened) {
            return -1;
        }
        UINT got = 0;
        if (f_read(&_fil, buf, (UINT)size, &got) != FR_OK) {
            return -1;
        }
        return (int)got;
    }

    void flush() override {
        if (_opened && _writable) {
            f_sync(&_fil);
        }
    }

    bool seek(uint32_t pos, SeekMode mode) override {
        if (!_opened) {
            return false;
        }
        FSIZE_t target;
        switch (mode) {
            case SeekSet: target = pos; break;
            case SeekCur: target = f_tell(&_fil) + pos; break;
            case SeekEnd: target = f_size(&_fil) - pos; break;
            default: return false;
        }
        return f_lseek(&_fil, target) == FR_OK && f_tell(&_fil) == target;
    }

    size_t position() const override {
        return _opened ? (size_t)f_tell(&_fil) : 0;
    }

    size_t size() const override {
        return _opened ? (size_t)f_size(&_fil) : 0;
    }

    bool truncate(uint32_t size) override {
        if (!_opened || !_writable) {
            return false;
        }
        return f_lseek(&_fil, size) == FR_OK && f_truncate(&_fil) == FR_OK;
    }

    void close() override {
        if (_opened) {
            f_close(&_fil);
            _opened = false;
        }
    }

    const char *fullName() const override { return _name; }

    const char *name() const override {
        /* The Arduino File::name() has meant the basename since the SD library
         * shipped, and sketches print it in directory listings. */
        const char *slash = strrchr(_name, '/');
        return slash ? slash + 1 : _name;
    }

    bool isFile() const override { return _opened; }
    bool isDirectory() const override { return false; }

    time_t getLastWrite() override {
        FILINFO fno;
        if (f_stat(_name, &fno) != FR_OK) {
            return 0;
        }
        return fatTime(fno.fdate, fno.ftime);
    }

    time_t getCreationTime() override {
        /* FAT keeps a creation time, but FatFs' FILINFO does not carry it, so
         * reporting the write time would be a quiet lie. Zero says "unknown",
         * which is what the API means by it. */
        return 0;
    }

    static time_t fatTime(WORD fdate, WORD ftime) {
        struct tm tmv = {};
        tmv.tm_year = ((fdate >> 9) & 0x7F) + 80;   /* FAT epoch is 1980 */
        tmv.tm_mon = ((fdate >> 5) & 0x0F) - 1;
        tmv.tm_mday = fdate & 0x1F;
        tmv.tm_hour = (ftime >> 11) & 0x1F;
        tmv.tm_min = (ftime >> 5) & 0x3F;
        tmv.tm_sec = (ftime & 0x1F) * 2;            /* two-second resolution */
        tmv.tm_isdst = -1;
        return mktime(&tmv);
    }

protected:
    FatFSImpl *_fs;
    mutable FIL _fil;
    char *_name = nullptr;
    bool _opened = false;
    bool _writable = false;
};

/* ---- Dir ---------------------------------------------------------------- */

class FatFSDirImpl : public DirImpl {
public:
    FatFSDirImpl(FatFSImpl *fs, const String &pattern, const String &path)
        : _fs(fs), _pattern(pattern), _path(path) { }

    ~FatFSDirImpl() override {
        if (_opened) {
            f_closedir(&_dir);
        }
    }

    FileImplPtr openFile(OpenMode openMode, AccessMode accessMode) override {
        if (!_valid) {
            return FileImplPtr();
        }
        return _fs->open(joined().c_str(), openMode, accessMode);
    }

    const char *fileName() override { return _valid ? _fno.fname : ""; }
    size_t fileSize() override { return _valid ? (size_t)_fno.fsize : 0; }

    time_t fileTime() override {
        return _valid ? FatFSFileImpl::fatTime(_fno.fdate, _fno.ftime) : 0;
    }
    time_t fileCreationTime() override { return 0; }

    bool isFile() const override {
        return _valid && !(_fno.fattrib & AM_DIR);
    }
    bool isDirectory() const override {
        return _valid && (_fno.fattrib & AM_DIR);
    }

    bool next() override {
        if (!_opened) {
            if (f_opendir(&_dir, _path.c_str()) != FR_OK) {
                return false;
            }
            _opened = true;
        }
        for (;;) {
            if (f_readdir(&_dir, &_fno) != FR_OK || _fno.fname[0] == 0) {
                _valid = false;
                return false;
            }
            /* The FS API's Dir is a prefix match, not a glob: openDir("/log")
             * lists everything under it. An empty pattern matches all. */
            if (_pattern.length() == 0
                || !strncmp(_fno.fname, _pattern.c_str(), _pattern.length())) {
                _valid = true;
                return true;
            }
        }
    }

    bool rewind() override {
        if (_opened) {
            f_closedir(&_dir);
            _opened = false;
        }
        _valid = false;
        return true;
    }

private:
    String joined() const {
        String s = _path;
        if (!s.endsWith("/")) {
            s += "/";
        }
        s += _fno.fname;
        return s;
    }

    FatFSImpl *_fs;
    String _pattern;
    String _path;
    DIR _dir = {};
    FILINFO _fno = {};
    bool _opened = false;
    bool _valid = false;
};

/* ---- FS ----------------------------------------------------------------- */

bool FatFSImpl::setConfig(const FSConfig &cfg) {
    if (cfg._type != FatFSConfig::FSId) {
        return false;
    }
    if (_mounted) {
        return false;
    }
    _autoFormat = cfg._autoFormat;
    return true;
}

SPIFTL *ch32h4_fatfs_ftl() {
    /* The single instance. See the note in FatFS.h: a second one over the same
     * partition would hold its own map and overwrite this one's data. */
    if (!s_flash) {
        s_flash = new CH32H4FTLFlash(partitionBase(), partitionSize());
        s_ftl = new SPIFTL(s_flash, (int)s_flash->ebBytes());
    }
    return s_ftl;
}

bool FatFSImpl::startFTL() {
    if (s_ftlStarted) {
        return true;
    }
    /* start() restores the mapping from flash, or formats the FTL if there is
     * nothing there to restore. It does not create a FAT volume -- that is
     * f_mkfs's job.
     *
     * ONCE PER BOOT. See s_ftlStarted: a second call reloads the map and
     * discards anything mapped since the last persist. */
    /* Cleared for the duration: start() rebuilds the map in place, and the
       block API above must refuse while it is half-built. */
    s_ftlStarted = false;
    s_ftlStarted = ch32h4_fatfs_ftl()->start();
    return s_ftlStarted;
}

bool FatFSImpl::begin() {
    if (_mounted) {
        return true;
    }
    s_err = CH32H4_FATFS_OK;

    const uint32_t len = partitionSize();
    if (len < MIN_PARTITION) {
        s_err = CH32H4_FATFS_ERR_TOO_SMALL;
        DEBUGV("FatFS: the filesystem partition is %lu bytes; FatFS needs at "
               "least %lu. Raise it with the board's filesystem-size menu "
               "(board_build.filesystem_size under PlatformIO).\n",
               (unsigned long)len, (unsigned long)MIN_PARTITION);
        return false;
    }

    if (!startFTL()) {
        s_err = CH32H4_FATFS_ERR_FTL;
        DEBUGV("FatFS: the flash translation layer would not start.\n");
        return false;
    }
    ch32h4_fatfs_register_disk(CH32H4_FATFS_PDRV_FLASH, &s_flash_ops);

    if (FR_OK == f_mount(&_fs, FLASH_VOL, 1)) {
        _mounted = true;
        return true;
    }

    if (!_autoFormat) {
        s_err = CH32H4_FATFS_ERR_NO_VOLUME;
        DEBUGV("FatFS: no FAT volume in the flash partition, and autoFormat "
               "is off.\n");
        return false;
    }

    /* The partition belongs to whichever sketch is flashed, so this reformats
     * rather than refusing -- a sketch that called begin() has said which
     * filesystem it wants. Loud about it, because what is being discarded may
     * be a LittleFS somebody put files in. */
    DEBUGV("FatFS: no usable FAT volume in the flash partition; formatting. "
           "Anything that was here, including a LittleFS, is gone.\n");
    if (!format()) {
        s_err = CH32H4_FATFS_ERR_FORMAT_FAILED;
        return false;
    }
    _mounted = (FR_OK == f_mount(&_fs, FLASH_VOL, 1));
    if (!_mounted) {
        s_err = CH32H4_FATFS_ERR_FORMAT_FAILED;
    }
    return _mounted;
}

void FatFSImpl::end() {
    if (!_mounted) {
        return;
    }
    f_mount(nullptr, FLASH_VOL, 0);
    if (s_ftl) {
        /* The mapping, not just the file data. f_mount's unmount syncs FatFs'
         * caches through CTRL_SYNC; this covers the case where nothing was
         * dirty enough to trigger one. */
        s_ftl->persist();
    }
    ch32h4_fatfs_register_disk(CH32H4_FATFS_PDRV_FLASH, nullptr);
    _mounted = false;
}

bool FatFSImpl::format() {
    if (partitionSize() < MIN_PARTITION) {
        s_err = CH32H4_FATFS_ERR_TOO_SMALL;
        return false;
    }
    ch32h4_fatfs_ftl();
    /* The translation layer first: its metadata is what f_mkfs's sectors land
     * on, and writing a FAT onto a stale mapping produces a filesystem that
     * mounts exactly once. */
    s_ftlStarted = false;   /* the map is about to be rebuilt; see above */
    if (!s_ftl->format() || !s_ftl->start()) {
        s_err = CH32H4_FATFS_ERR_FTL;
        return false;
    }
    s_ftlStarted = true;
    ch32h4_fatfs_register_disk(CH32H4_FATFS_PDRV_FLASH, &s_flash_ops);

    /* On the heap rather than the stack, as in SDFS: FF_MAX_SS is 512 now, but
     * a stack allocation sized from a config macro is the kind of thing that
     * quietly overflows when someone raises it. */
    void *work = malloc(FF_MAX_SS);
    if (!work) {
        s_err = CH32H4_FATFS_ERR_FORMAT_FAILED;
        return false;
    }

    MKFS_PARM opt = {};
    /* FM_SFD: no partition table. A device this size with its boot sector at
     * LBA 0 is what a host expects from something presenting itself as a
     * removable stick. FM_FAT rather than letting FatFs choose, because at
     * these sizes the answer is always FAT12 or FAT16 and being explicit keeps
     * the format stable across partition sizes. */
    opt.fmt = FM_FAT | FM_SFD;
    FRESULT r = f_mkfs(FLASH_VOL, &opt, work, FF_MAX_SS);
    free(work);

    if (r != FR_OK) {
        s_err = CH32H4_FATFS_ERR_FORMAT_FAILED;
        return false;
    }
    return s_ftl->persist();
}

FileImplPtr FatFSImpl::open(const char *path, OpenMode openMode,
                            AccessMode accessMode) {
    if (!_mounted || !path) {
        return FileImplPtr();
    }

    BYTE mode = 0;
    if (accessMode & AM_READ) {
        mode |= FA_READ;
    }
    if (accessMode & AM_WRITE) {
        mode |= FA_WRITE;
    }
    if (openMode & OM_CREATE) {
        mode |= FA_OPEN_ALWAYS;
    }
    if (openMode & OM_TRUNCATE) {
        mode |= FA_CREATE_ALWAYS;
    }
    if (!(openMode & (OM_CREATE | OM_TRUNCATE))) {
        mode |= FA_OPEN_EXISTING;
    }

    FIL fil = {};
    if (f_open(&fil, fixPath(path).c_str(), mode) != FR_OK) {
        return FileImplPtr();
    }
    if (openMode & OM_APPEND) {
        f_lseek(&fil, f_size(&fil));
    }
    return std::make_shared<FatFSFileImpl>(this, fixPath(path).c_str(), fil,
                                           (mode & FA_WRITE) != 0);
}

bool FatFSImpl::exists(const char *path) {
    if (!_mounted) {
        return false;
    }
    FILINFO fno;
    return f_stat(fixPath(path).c_str(), &fno) == FR_OK;
}

DirImplPtr FatFSImpl::openDir(const char *path) {
    if (!_mounted) {
        return DirImplPtr();
    }
    /* The FS API's openDir takes a prefix, and sketches pass both real
     * directories and prefixes like "/log". If it names a directory, list it;
     * otherwise split it and match on the last component. */
    String p = fixPath(path);
    FILINFO fno;
    if (f_stat(p.c_str(), &fno) == FR_OK && (fno.fattrib & AM_DIR)) {
        return std::make_shared<FatFSDirImpl>(this, "", p);
    }
    if (p == FLASH_VOL "/") {
        return std::make_shared<FatFSDirImpl>(this, "", p);
    }
    int slash = p.lastIndexOf('/');
    /* The volume's own slash, at index 2 in "0:/name", is not a directory
     * separator to split on -- and "0:" alone means FatFs' current directory
     * for the volume, not its root. Anything at or before it is the root. */
    const int volSlash = (int)sizeof(FLASH_VOL) - 1;
    String dir = slash <= volSlash ? String(FLASH_VOL "/") : p.substring(0, slash);
    String pattern = p.substring(slash + 1);
    return std::make_shared<FatFSDirImpl>(this, pattern, dir);
}

bool FatFSImpl::rename(const char *from, const char *to) {
    if (!_mounted) {
        return false;
    }
    return fatOk(f_rename(fixPath(from).c_str(), fixPath(to).c_str()));
}

bool FatFSImpl::remove(const char *path) {
    if (!_mounted) {
        return false;
    }
    return fatOk(f_unlink(fixPath(path).c_str()));
}

bool FatFSImpl::mkdir(const char *path) {
    if (!_mounted) {
        return false;
    }
    return fatOk(f_mkdir(fixPath(path).c_str()));
}

bool FatFSImpl::rmdir(const char *path) {
    if (!_mounted) {
        return false;
    }
    return fatOk(f_unlink(fixPath(path).c_str()));
}

bool FatFSImpl::stat(const char *path, FSStat *st) {
    if (!_mounted || !st) {
        return false;
    }
    FILINFO fno;
    if (f_stat(fixPath(path).c_str(), &fno) != FR_OK) {
        return false;
    }
    *st = {};
    st->size = fno.fsize;
    st->blocksize = 512;
    st->isDir = (fno.fattrib & AM_DIR) != 0;
    st->ctime = 0;
    st->atime = FatFSFileImpl::fatTime(fno.fdate, fno.ftime);
    return true;
}

bool FatFSImpl::info(FSInfo &info) {
    if (!_mounted) {
        return false;
    }
    FATFS *fs = nullptr;
    DWORD freeClusters = 0;
    if (f_getfree(FLASH_VOL, &freeClusters, &fs) != FR_OK) {
        return false;
    }
    const uint64_t clusterBytes = (uint64_t)fs->csize * 512;

    info = {};
    /* n_fatent counts the FAT entries; two are reserved, so the data area is
     * that less two clusters. */
    info.totalBytes = (uint64_t)(fs->n_fatent - 2) * clusterBytes;
    info.usedBytes = info.totalBytes - (uint64_t)freeClusters * clusterBytes;
    info.blockSize = (size_t)clusterBytes;
    info.pageSize = 512;
    info.maxOpenFiles = 0;      /* FatFs imposes none with FF_FS_LOCK == 0 */
    info.maxPathLength = FF_MAX_LFN;
    return true;
}

FS FatFS(FSImplPtr(new FatFSImpl()));

/* ---- the C API declared in FatFS.h -------------------------------------- */

extern "C" {

ch32h4_fatfs_error ch32h4_fatfs_last_error(void) {
    return s_err;
}

const char *ch32h4_fatfs_last_error_string(void) {
    switch (s_err) {
        case CH32H4_FATFS_OK:            return "ok";
        case CH32H4_FATFS_ERR_TOO_SMALL: return "partition too small, needs 256K";
        case CH32H4_FATFS_ERR_NO_VOLUME: return "no FAT volume, autoformat off";
        case CH32H4_FATFS_ERR_FORMAT_FAILED: return "format failed";
        case CH32H4_FATFS_ERR_FTL:       return "flash translation layer failed";
    }
    return "unknown";
}

void ch32h4_fatfs_write_log_reset(void) {
    s_wlogCount = 0;
}

uint32_t ch32h4_fatfs_write_count(void) {
    return s_wlogCount;
}

uint32_t ch32h4_fatfs_write_log(uint32_t i) {
    return i < WLOG_N ? s_wlog[i] : 0xFFFFFFFFu;
}

uint32_t ch32h4_fatfs_sync_count(void) {
    return s_syncs;
}

uint32_t ch32h4_fatfs_sync_failures(void) {
    return s_syncFails;
}

/* EVERY ONE OF THESE REFUSES UNTIL THE LAYER HAS STARTED, and that is not
 * belt and braces.
 *
 * lbaCount() is set in the SPIFTL constructor, so it is non-zero from the
 * moment the object exists -- before start() has rebuilt the map from flash.
 * USB mass storage asks "is there media?" during enumeration and reads the
 * moment the answer is yes, which put a host's READ(10) into a half-built map
 * while FatFS.begin() was still filling it, and hung the board on every boot.
 *
 * The guard belongs here rather than in the caller: anything reaching the
 * block layer wants the same answer, and a caller that has to remember is a
 * caller that will not. */
uint32_t ch32h4_fatfs_lba_count(void) {
    return (s_ftl && s_ftlStarted) ? (uint32_t)s_ftl->lbaCount() : 0;
}

bool ch32h4_fatfs_lba_read(uint32_t lba, void *dst) {
    return s_ftl && s_ftlStarted && s_ftl->read((int)lba, (uint8_t *)dst);
}

bool ch32h4_fatfs_lba_write(uint32_t lba, const void *src) {
    return s_ftl && s_ftlStarted
           && s_ftl->write((int)lba, (const uint8_t *)src);
}

bool ch32h4_fatfs_lba_sync(void) {
    return s_ftl && s_ftlStarted && s_ftl->persist();
}

}  // extern "C"
