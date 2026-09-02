#include "LittleFS.h"

#include <string.h>

/* The partition, as the linker placed it.
 *
 * These are absolute symbols, so their ADDRESSES are the values -- taking
 * &_FS_start is the number, and reading _FS_start as a variable reads whatever
 * flash happens to be there. Getting that backwards is the classic mistake
 * with linker symbols and it produces a plausible-looking wrong address.
 *
 * Names match arduino-pico's, so its LittleFS code reads the same here. */
extern "C" {
extern uint8_t _FS_start;
extern uint8_t _FS_end;
}

uint32_t LittleFSImpl::partitionStart() const {
    return (uint32_t)(uintptr_t)&_FS_start;
}

uint32_t LittleFSImpl::partitionSize() const {
    return (uint32_t)(uintptr_t)(&_FS_end - &_FS_start);
}

/* ---- the block device --------------------------------------------------- */

int LittleFSImpl::lfs_read(const struct lfs_config *c, lfs_block_t block,
                           lfs_off_t off, void *buffer, lfs_size_t size) {
    LittleFSImpl *self = (LittleFSImpl *)c->context;
    ch32h4_flash_read(self->_start + block * c->block_size + off, buffer, size);
    return 0;
}

int LittleFSImpl::lfs_prog(const struct lfs_config *c, lfs_block_t block,
                           lfs_off_t off, const void *buffer, lfs_size_t size) {
    LittleFSImpl *self = (LittleFSImpl *)c->context;
    const uint32_t addr = self->_start + block * c->block_size + off;
    return ch32h4_flash_write(addr, buffer, size) ? 0 : LFS_ERR_IO;
}

int LittleFSImpl::lfs_erase(const struct lfs_config *c, lfs_block_t block) {
    LittleFSImpl *self = (LittleFSImpl *)c->context;
    const uint32_t addr = self->_start + block * c->block_size;
    return ch32h4_flash_erase(addr, c->block_size) ? 0 : LFS_ERR_IO;
}

int LittleFSImpl::lfs_sync(const struct lfs_config *c) {
    (void)c;
    /* Nothing is buffered below this layer: ch32h4_flash_write() programs and
     * verifies before returning. */
    return 0;
}

/* ---- mount -------------------------------------------------------------- */

bool LittleFSImpl::configure() {
    _start = partitionStart();
    _size = partitionSize();
    _block_size = ch32h4_flash_page_size();

    if (_size == 0) {
        return false;
    }
    /* Both are guaranteed by the linker script and its assertions, so a
     * failure here means someone linked with a hand-written script. Checked
     * anyway: an unaligned erase is refused by the flash driver and would
     * present as a filesystem that mounts and then cannot write. */
    if ((_start % _block_size) != 0 || (_size % _block_size) != 0) {
        return false;
    }
    if (_size / _block_size < 4) {
        return false;
    }

    memset(&_cfg, 0, sizeof(_cfg));
    _cfg.context = this;
    _cfg.read = lfs_read;
    _cfg.prog = lfs_prog;
    _cfg.erase = lfs_erase;
    _cfg.sync = lfs_sync;

    /* read_size can be anything; prog_size cannot. The flash commits a whole
     * 256-byte page buffer at a time, so that is the smallest write the driver
     * below can perform, and LittleFS must never ask for less. */
    _cfg.read_size = 4;
    _cfg.prog_size = ch32h4_flash_prog_size();
    _cfg.block_size = _block_size;
    _cfg.block_count = _size / _block_size;

    /* How many times a block is written before LittleFS moves its contents
     * elsewhere. 500 is the value LittleFS's own documentation suggests for
     * flash of this endurance; lower wears more evenly at the cost of more
     * copying. */
    _cfg.block_cycles = 500;
    _cfg.cache_size = CACHE_SIZE;
    _cfg.lookahead_size = LOOKAHEAD_SIZE;

    _cfg.read_buffer = _read_buf;
    _cfg.prog_buffer = _prog_buf;
    _cfg.lookahead_buffer = _lookahead_buf;
    return true;
}

bool LittleFSImpl::begin() {
    if (_mounted) {
        return true;
    }
    if (!configure()) {
        /* Said out loud, because the commonest cause is simply not having
         * asked for a partition, and a bare false sends people looking at
         * their wiring. */
        if (_size == 0) {
            ch32h4_console_puts(
                "LittleFS: no partition reserved. Set"
                " board_build.filesystem_size in platformio.ini, or pick a"
                " size in the Arduino IDE's Filesystem menu.\r\n");
        } else {
            ch32h4_console_puts("LittleFS: partition too small -- needs at"
                                " least four flash blocks of ");
            ch32h4_console_putu(_block_size);
            ch32h4_console_puts(" bytes.\r\n");
        }
        return false;
    }

    if (lfs_mount(&_lfs, &_cfg) == LFS_ERR_OK) {
        _mounted = true;
        return true;
    }

    /* A first boot has an unformatted partition, and that is not an error
     * worth making a sketch handle. A mount failure on a partition that HAS
     * been formatted is data loss, so autoFormat is a configurable and not a
     * silent always. */
    if (!_autoFormat) {
        ch32h4_console_puts("LittleFS: mount failed and autoFormat is off,"
                            " so the partition was left alone.\r\n");
        return false;
    }
    if (lfs_format(&_lfs, &_cfg) != LFS_ERR_OK) {
        return false;
    }
    if (lfs_mount(&_lfs, &_cfg) != LFS_ERR_OK) {
        return false;
    }
    _mounted = true;
    return true;
}

void LittleFSImpl::end() {
    if (!_mounted) {
        return;
    }
    lfs_unmount(&_lfs);
    _mounted = false;
}

bool LittleFSImpl::format() {
    /* Formatting needs the configuration but not a mount, so an unmountable
     * partition can still be recovered. */
    const bool was_mounted = _mounted;
    if (was_mounted) {
        lfs_unmount(&_lfs);
        _mounted = false;
    }
    if (!configure()) {
        return false;
    }
    if (lfs_format(&_lfs, &_cfg) != LFS_ERR_OK) {
        return false;
    }
    if (was_mounted) {
        return begin();
    }
    return true;
}

bool LittleFSImpl::setConfig(const FSConfig &cfg) {
    if (_mounted || cfg._type != LittleFSConfig::FSId) {
        return false;
    }
    _autoFormat = cfg._autoFormat;
    return true;
}

bool LittleFSImpl::gc() {
    if (!_mounted) {
        return false;
    }
    return lfs_fs_gc(&_lfs) == LFS_ERR_OK;
}

bool LittleFSImpl::info(FSInfo &info) {
    if (!_mounted) {
        return false;
    }
    const lfs_ssize_t used = lfs_fs_size(&_lfs);
    if (used < 0) {
        return false;
    }
    info.totalBytes = (size_t)_size;
    info.usedBytes = (size_t)used * _block_size;
    info.blockSize = _block_size;
    info.pageSize = _cfg.prog_size;
    /* LittleFS imposes no limit of its own on either, so these report the
     * filesystem's own maxima rather than a made-up number. */
    info.maxOpenFiles = 0;
    info.maxPathLength = LFS_NAME_MAX;
    return true;
}

/* ---- paths -------------------------------------------------------------- */

/* LittleFS has no concept of a current directory and wants paths without a
 * leading slash to mean the same thing as paths with one. The FS API hands
 * both forms through, so they are normalised here rather than in every call. */
static const char *fixPath(const char *path) {
    if (!path) {
        return "/";
    }
    while (path[0] == '/' && path[1] == '/') {
        path++;
    }
    return path;
}

bool LittleFSImpl::exists(const char *path) {
    if (!_mounted || !path) {
        return false;
    }
    struct lfs_info inf;
    return lfs_stat(&_lfs, fixPath(path), &inf) == LFS_ERR_OK;
}

bool LittleFSImpl::stat(const char *path, FSStat *st) {
    if (!_mounted || !path || !st) {
        return false;
    }
    struct lfs_info inf;
    if (lfs_stat(&_lfs, fixPath(path), &inf) != LFS_ERR_OK) {
        return false;
    }
    memset(st, 0, sizeof(*st));
    st->size = inf.size;
    st->isDir = inf.type == LFS_TYPE_DIR;
    /* LittleFS stores no timestamps of its own. Reporting zero is honest;
     * inventing time(NULL) would make every file look freshly written. */
    st->ctime = 0;
    st->atime = 0;
    return true;
}

bool LittleFSImpl::rename(const char *pathFrom, const char *pathTo) {
    if (!_mounted || !pathFrom || !pathTo) {
        return false;
    }
    return lfs_rename(&_lfs, fixPath(pathFrom), fixPath(pathTo)) == LFS_ERR_OK;
}

bool LittleFSImpl::remove(const char *path) {
    if (!_mounted || !path) {
        return false;
    }
    return lfs_remove(&_lfs, fixPath(path)) == LFS_ERR_OK;
}

bool LittleFSImpl::mkdir(const char *path) {
    if (!_mounted || !path) {
        return false;
    }
    return lfs_mkdir(&_lfs, fixPath(path)) == LFS_ERR_OK;
}

bool LittleFSImpl::rmdir(const char *path) {
    /* lfs_remove handles both; the API keeps them separate. */
    return remove(path);
}

/* ---- File --------------------------------------------------------------- */

class LittleFSFileImpl : public FileImpl {
public:
    LittleFSFileImpl(LittleFSImpl *fs, const char *name, bool writable)
        : _fs(fs), _opened(true), _writable(writable) {
        strncpy(_name, name, sizeof(_name) - 1);
        _name[sizeof(_name) - 1] = '\0';
    }

    ~LittleFSFileImpl() override {
        close();
    }

    size_t write(const uint8_t *buf, size_t size) override {
        if (!_opened || !_writable) {
            return 0;
        }
        const lfs_ssize_t n = lfs_file_write(&_fs->_lfs, &_file, buf, size);
        return n < 0 ? 0 : (size_t)n;
    }

    int read(uint8_t *buf, size_t size) override {
        if (!_opened) {
            return -1;
        }
        const lfs_ssize_t n = lfs_file_read(&_fs->_lfs, &_file, buf, size);
        return n < 0 ? -1 : (int)n;
    }

    void flush() override {
        if (_opened) {
            lfs_file_sync(&_fs->_lfs, &_file);
        }
    }

    bool seek(uint32_t pos, SeekMode mode) override {
        if (!_opened) {
            return false;
        }
        int whence = LFS_SEEK_SET;
        if (mode == SeekCur) {
            whence = LFS_SEEK_CUR;
        } else if (mode == SeekEnd) {
            whence = LFS_SEEK_END;
        }
        return lfs_file_seek(&_fs->_lfs, &_file, (lfs_soff_t)pos, whence) >= 0;
    }

    size_t position() const override {
        if (!_opened) {
            return 0;
        }
        const lfs_soff_t p = lfs_file_tell(&_fs->_lfs,
                                           const_cast<lfs_file_t *>(&_file));
        return p < 0 ? 0 : (size_t)p;
    }

    size_t size() const override {
        if (!_opened) {
            return 0;
        }
        const lfs_soff_t s = lfs_file_size(&_fs->_lfs,
                                           const_cast<lfs_file_t *>(&_file));
        return s < 0 ? 0 : (size_t)s;
    }

    bool truncate(uint32_t size) override {
        if (!_opened || !_writable) {
            return false;
        }
        return lfs_file_truncate(&_fs->_lfs, &_file, size) == LFS_ERR_OK;
    }

    void close() override {
        if (_opened) {
            lfs_file_close(&_fs->_lfs, &_file);
            _opened = false;
        }
    }

    const char *name() const override {
        const char *slash = strrchr(_name, '/');
        return slash ? slash + 1 : _name;
    }

    const char *fullName() const override { return _name; }
    bool isFile() const override { return _opened; }
    bool isDirectory() const override { return false; }

    lfs_file_t _file = {};

private:
    LittleFSImpl *_fs;
    char _name[LFS_NAME_MAX + 1] = {};
    bool _opened;
    bool _writable;
};

FileImplPtr LittleFSImpl::open(const char *path, OpenMode openMode,
                               AccessMode accessMode) {
    if (!_mounted || !path) {
        return FileImplPtr();
    }

    int flags = 0;
    if ((accessMode & AM_READ) && (accessMode & AM_WRITE)) {
        flags = LFS_O_RDWR;
    } else if (accessMode & AM_WRITE) {
        flags = LFS_O_WRONLY;
    } else {
        flags = LFS_O_RDONLY;
    }
    if (openMode & OM_CREATE) {
        flags |= LFS_O_CREAT;
    }
    if (openMode & OM_TRUNCATE) {
        flags |= LFS_O_TRUNC;
    }
    if (openMode & OM_APPEND) {
        flags |= LFS_O_APPEND;
    }

    auto file = std::make_shared<LittleFSFileImpl>(this, fixPath(path),
                                                   (accessMode & AM_WRITE) != 0);
    if (lfs_file_open(&_lfs, &file->_file, fixPath(path), flags) != LFS_ERR_OK) {
        /* The impl was constructed as "opened" so its destructor would close
         * a successful open. It never opened, so say so before dropping it --
         * otherwise the destructor closes a file handle that was never
         * initialised. */
        file->close();
        return FileImplPtr();
    }
    return file;
}

/* ---- Dir ---------------------------------------------------------------- */

class LittleFSDirImpl : public DirImpl {
public:
    LittleFSDirImpl(LittleFSImpl *fs, const char *path) : _fs(fs) {
        strncpy(_path, path, sizeof(_path) - 1);
        _path[sizeof(_path) - 1] = '\0';
        _opened = lfs_dir_open(&_fs->_lfs, &_dir, _path) == LFS_ERR_OK;
    }

    ~LittleFSDirImpl() override {
        if (_opened) {
            lfs_dir_close(&_fs->_lfs, &_dir);
        }
    }

    FileImplPtr openFile(OpenMode openMode, AccessMode accessMode) override {
        if (!_valid) {
            return FileImplPtr();
        }
        char full[LFS_NAME_MAX * 2 + 2];
        buildPath(full, sizeof(full));
        return _fs->open(full, openMode, accessMode);
    }

    const char *fileName() override { return _valid ? _info.name : ""; }
    size_t fileSize() override { return _valid ? _info.size : 0; }
    bool isFile() const override { return _valid && _info.type == LFS_TYPE_REG; }
    bool isDirectory() const override { return _valid && _info.type == LFS_TYPE_DIR; }

    bool next() override {
        if (!_opened) {
            return false;
        }
        /* "." and ".." come back from lfs_dir_read and are not files anybody
         * iterating a directory wants to see. Skipping them here keeps every
         * caller from having to. */
        for (;;) {
            const int r = lfs_dir_read(&_fs->_lfs, &_dir, &_info);
            if (r <= 0) {
                _valid = false;
                return false;
            }
            if (strcmp(_info.name, ".") != 0 && strcmp(_info.name, "..") != 0) {
                _valid = true;
                return true;
            }
        }
    }

    bool rewind() override {
        if (!_opened) {
            return false;
        }
        _valid = false;
        return lfs_dir_rewind(&_fs->_lfs, &_dir) == LFS_ERR_OK;
    }

private:
    void buildPath(char *out, size_t len) const {
        const bool slash = _path[0] && _path[strlen(_path) - 1] == '/';
        snprintf(out, len, "%s%s%s", _path, slash ? "" : "/", _info.name);
    }

    LittleFSImpl *_fs;
    lfs_dir_t _dir = {};
    struct lfs_info _info = {};
    char _path[LFS_NAME_MAX + 1] = {};
    bool _opened = false;
    bool _valid = false;
};

DirImplPtr LittleFSImpl::openDir(const char *path) {
    if (!_mounted) {
        return DirImplPtr();
    }
    return std::make_shared<LittleFSDirImpl>(this,
                                             path && path[0] ? fixPath(path) : "/");
}

FS LittleFS = FS(FSImplPtr(new LittleFSImpl()));
