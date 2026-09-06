#include "SDFS.h"

#include <string.h>

#include <ch32h4_fatfs_disk.h>

/* FatFs volume 1. Volume 0 is the internal flash, which libraries/FatFS
 * mounts; see ch32h4_fatfs_disk.h for the numbering. */
#define SD_VOL "1:"

/* FatFs speaks absolute paths without a leading slash just as happily as with
 * one, but "" is not a path at all and "/" is. Normalising here means the rest
 * of this file never has to think about it, and a sketch can use either.
 *
 * It also carries the volume prefix, which makes this the single point where a
 * sketch-visible path becomes a FatFs one. Sketches never see "1:", and a
 * sketch written before there was a second volume needs no change.
 *
 * Returns a String rather than a const char * because the prefix has to be
 * stored somewhere. Callers pass .c_str(); the temporary outlives the call. */
static String fixPath(const char *path) {
    if (!path || !path[0]) {
        return String(SD_VOL "/");
    }
    /* IDEMPOTENT, and it has to be. A Dir hands its entries back through the
     * public open(), and the path it hands over has already been through here
     * -- so without this a listing would open "1:/1:/log/x" and fail. Any
     * choke point that can be re-entered has to tolerate its own output. */
    if (!strncmp(path, SD_VOL, sizeof(SD_VOL) - 1)) {
        return String(path);
    }
    if (path[0] == '/') {
        return String(SD_VOL) + path;
    }
    return String(SD_VOL "/") + path;
}

static bool fatOk(FRESULT r) {
    return r == FR_OK;
}

/* ---- File --------------------------------------------------------------- */

class SDFSFileImpl : public FileImpl {
public:
    SDFSFileImpl(SDFSImpl *fs, const char *name, FIL fil, bool writable)
        : _fs(fs), _fil(fil), _opened(true), _writable(writable) {
        _name = strdup(name);
    }

    ~SDFSFileImpl() override {
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
    SDFSImpl *_fs;
    mutable FIL _fil;
    char *_name = nullptr;
    bool _opened = false;
    bool _writable = false;
};

/* ---- Dir ---------------------------------------------------------------- */

class SDFSDirImpl : public DirImpl {
public:
    SDFSDirImpl(SDFSImpl *fs, const String &pattern, const String &path)
        : _fs(fs), _pattern(pattern), _path(path) { }

    ~SDFSDirImpl() override {
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
        return _valid ? SDFSFileImpl::fatTime(_fno.fdate, _fno.ftime) : 0;
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

    SDFSImpl *_fs;
    String _pattern;
    String _path;
    DIR _dir = {};
    FILINFO _fno = {};
    bool _opened = false;
    bool _valid = false;
};

/* ---- FS ----------------------------------------------------------------- */

bool SDFSImpl::setConfig(const FSConfig &cfg) {
    if (cfg._type != SDFSConfig::FSId) {
        return false;
    }
    if (_mounted) {
        /* Changing the bus width or clock under a mounted filesystem would
         * re-identify the card with open files still referring to it. */
        return false;
    }
    const SDFSConfig &c = static_cast<const SDFSConfig &>(cfg);
    _width = c._width;
    _freq = c._freq;
    _autoFormat = c._autoFormat;
    return true;
}

bool SDFSImpl::begin() {
    if (_mounted) {
        return true;
    }
    if (ch32h4_sd_begin(_width, _freq) != 0) {
        return false;
    }
    /* Hand this volume's disk driver to FatFs. Here rather than in a static
     * constructor so that a sketch which never mounts an SD card does not
     * link the SDMMC driver at all -- see ch32h4_fatfs_disk.h. Idempotent, so
     * a second begin() is harmless. */
    ch32h4_sd_disk_register();
    /* Mount immediately (the 1), rather than deferring to the first access.
     * A begin() that returns true and then fails on the first open, because
     * the card holds no filesystem, is the least useful possible answer. */
    FRESULT r = f_mount(&_fs, SD_VOL, 1);
    if (r != FR_OK && _autoFormat) {
        if (format()) {
            r = f_mount(&_fs, SD_VOL, 1);
        }
    }
    if (r != FR_OK) {
        ch32h4_sd_end();
        return false;
    }
    _mounted = true;
    return true;
}

void SDFSImpl::end() {
    if (!_mounted) {
        return;
    }
    f_mount(nullptr, SD_VOL, 0);
    /* Unregistered as well as unmounted: the card is powered down below, and
     * a driver left in the table would answer FatFs with a card that is no
     * longer there. */
    ch32h4_fatfs_register_disk(CH32H4_FATFS_PDRV_SD, nullptr);
    _mounted = false;
    ch32h4_sd_end();
}

bool SDFSImpl::format() {
    /* The card has to be up, but need not be mounted -- formatting an
     * unmountable card is the main reason to call this. */
    bool broughtUp = false;
    if (!ch32h4_sd_ready()) {
        if (ch32h4_sd_begin(_width, _freq) != 0) {
            return false;
        }
        broughtUp = true;
    }

    /* FatFs wants a work buffer of at least one sector. It goes on the heap
     * rather than the stack: FF_MAX_SS is 512 now, but a stack allocation
     * sized from a config macro is the kind of thing that quietly overflows
     * when someone raises it. */
    void *work = malloc(FF_MAX_SS);
    if (!work) {
        if (broughtUp) {
            ch32h4_sd_end();
        }
        return false;
    }

    MKFS_PARM opt = {};
    opt.fmt = FM_FAT32 | FM_FAT;   /* let FatFs pick by capacity */
    FRESULT r = f_mkfs(SD_VOL, &opt, work, FF_MAX_SS);
    free(work);

    if (broughtUp) {
        ch32h4_sd_end();
    }
    return r == FR_OK;
}

FileImplPtr SDFSImpl::open(const char *path, OpenMode openMode,
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
    return std::make_shared<SDFSFileImpl>(this, fixPath(path).c_str(), fil,
                                          (mode & FA_WRITE) != 0);
}

bool SDFSImpl::exists(const char *path) {
    if (!_mounted) {
        return false;
    }
    FILINFO fno;
    return f_stat(fixPath(path).c_str(), &fno) == FR_OK;
}

DirImplPtr SDFSImpl::openDir(const char *path) {
    if (!_mounted) {
        return DirImplPtr();
    }
    /* The FS API's openDir takes a prefix, and sketches pass both real
     * directories and prefixes like "/log". If it names a directory, list it;
     * otherwise split it and match on the last component. */
    String p = fixPath(path);
    FILINFO fno;
    if (f_stat(p.c_str(), &fno) == FR_OK && (fno.fattrib & AM_DIR)) {
        return std::make_shared<SDFSDirImpl>(this, "", p);
    }
    if (p == SD_VOL "/") {
        return std::make_shared<SDFSDirImpl>(this, "", p);
    }
    int slash = p.lastIndexOf('/');
    /* The volume's own slash, at index 2 in "1:/name", is not a directory
     * separator to split on -- and "1:" alone means the volume's current
     * directory to FatFs, not its root. Anything at or before it is the
     * root. */
    const int volSlash = (int)sizeof(SD_VOL) - 1;
    String dir = slash <= volSlash ? String(SD_VOL "/") : p.substring(0, slash);
    String pattern = p.substring(slash + 1);
    return std::make_shared<SDFSDirImpl>(this, pattern, dir);
}

bool SDFSImpl::rename(const char *from, const char *to) {
    if (!_mounted) {
        return false;
    }
    return fatOk(f_rename(fixPath(from).c_str(), fixPath(to).c_str()));
}

bool SDFSImpl::remove(const char *path) {
    if (!_mounted) {
        return false;
    }
    return fatOk(f_unlink(fixPath(path).c_str()));
}

bool SDFSImpl::mkdir(const char *path) {
    if (!_mounted) {
        return false;
    }
    return fatOk(f_mkdir(fixPath(path).c_str()));
}

bool SDFSImpl::rmdir(const char *path) {
    if (!_mounted) {
        return false;
    }
    return fatOk(f_unlink(fixPath(path).c_str()));
}

bool SDFSImpl::stat(const char *path, FSStat *st) {
    if (!_mounted || !st) {
        return false;
    }
    FILINFO fno;
    if (f_stat(fixPath(path).c_str(), &fno) != FR_OK) {
        return false;
    }
    *st = {};
    st->size = fno.fsize;
    st->blocksize = CH32H4_SD_BLOCK_SIZE;
    st->isDir = (fno.fattrib & AM_DIR) != 0;
    st->ctime = 0;
    st->atime = SDFSFileImpl::fatTime(fno.fdate, fno.ftime);
    return true;
}

bool SDFSImpl::info(FSInfo &info) {
    if (!_mounted) {
        return false;
    }
    FATFS *fs = nullptr;
    DWORD freeClusters = 0;
    if (f_getfree(SD_VOL, &freeClusters, &fs) != FR_OK) {
        return false;
    }
    const uint64_t clusterBytes =
        (uint64_t)fs->csize * CH32H4_SD_BLOCK_SIZE;

    info = {};
    /* n_fatent counts the FAT entries; two are reserved, so the data area is
     * that less two clusters. */
    info.totalBytes = (uint64_t)(fs->n_fatent - 2) * clusterBytes;
    info.usedBytes = info.totalBytes - (uint64_t)freeClusters * clusterBytes;
    info.blockSize = (size_t)clusterBytes;
    info.pageSize = CH32H4_SD_BLOCK_SIZE;
    info.maxOpenFiles = 0;      /* FatFs imposes none with FF_FS_LOCK == 0 */
    info.maxPathLength = FF_MAX_LFN;
    return true;
}

FS SDFS = FS(FSImplPtr(new SDFSImpl()));
