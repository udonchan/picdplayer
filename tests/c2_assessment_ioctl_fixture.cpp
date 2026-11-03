// Linux-only synthetic ioctl boundary. No ioctl is ever forwarded to the kernel.
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/cdrom.h>
#include <mutex>
#include <scsi/sg.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <thread>
#include <unistd.h>

namespace {
std::mutex log_mutex;
std::atomic<int> raw_fd{-1};
std::atomic<unsigned> reads{0};
std::atomic<unsigned> events{0};
using Close = int (*)(int);
using Stat = int (*)(const char*, struct stat*);
Stat real_stat() {
    static auto function = reinterpret_cast<Stat>(dlsym(RTLD_NEXT, "stat"));
    return function;
}
Close real_close() {
    static auto function = reinterpret_cast<Close>(dlsym(RTLD_NEXT, "close"));
    return function;
}
bool control(const char* name) {
    const char* root = std::getenv("C2_FIXTURE_ROOT");
    if (!root) return false;
    char path[4096];
    std::snprintf(path, sizeof(path), "%s/%s", root, name);
    return access(path, F_OK) == 0;
}
void event(const char* name, int fd, int value = 0) {
    std::lock_guard lock(log_mutex);
    if (++events > 256) _exit(90);
    const char* root = std::getenv("C2_FIXTURE_ROOT");
    if (!root) _exit(91);
    char path[4096];
    std::snprintf(path, sizeof(path), "%s/events", root);
    const int log = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (log < 0) _exit(92);
    char line[128];
    const int size = std::snprintf(line, sizeof(line), "%s %d %d\n", name, fd, value);
    if (write(log, line, size) != size) _exit(93);
    real_close()(log);
}
int fail(int error = ENOTTY) { errno = error; return -1; }
bool mock_fd(int fd) {
    const char* device = std::getenv("C2_FIXTURE_DEVICE");
    struct stat expected{}, actual{};
    return device && real_stat()(device, &expected) == 0 && fstat(fd, &actual) == 0 &&
        S_ISREG(expected.st_mode) && S_ISREG(actual.st_mode) &&
        actual.st_dev == expected.st_dev && actual.st_ino == expected.st_ino &&
        (fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDONLY;
}
}

// Synthetic PRESENT observation only. fstat remains native, so every mocked
// ioctl still requires an actual read-only fd on the regular fixture inode.
extern "C" int stat(const char* path, struct stat* info) noexcept {
    const int result = real_stat()(path, info);
    const char* device = std::getenv("C2_FIXTURE_DEVICE");
    if (result == 0 && device && std::getenv("C2_FIXTURE_SYSFS") &&
        std::strcmp(path, device) == 0 && S_ISREG(info->st_mode)) {
        info->st_mode = (info->st_mode & ~S_IFMT) | S_IFBLK;
        info->st_rdev = makedev(240, 254);
    }
    return result;
}

extern "C" char* realpath(const char* path, char* resolved) noexcept {
    using Realpath = char* (*)(const char*, char*);
    static auto function = reinterpret_cast<Realpath>(dlsym(RTLD_NEXT, "realpath"));
    const char* sysfs = std::getenv("C2_FIXTURE_SYSFS");
    return function(sysfs && std::strcmp(path, "/sys/dev/block/240:254") == 0 ? sysfs : path, resolved);
}

extern "C" int close(int fd) {
    int expected = fd;
    const bool raw = raw_fd.compare_exchange_strong(expected, -1);
    const int result = real_close()(fd);
    const int saved = errno;
    if (raw) event(result == 0 ? "RAW_CLOSE" : "CLOSE_FAILED", fd);
    errno = saved;
    return result;
}

extern "C" int ioctl(int fd, unsigned long request, ...) noexcept {
    if (!mock_fd(fd)) return fail();
    // Only requests with pointer arguments consume a vararg. Scalar requests
    // include int and unsigned long callers and need no argument inspection.
    switch (request) {
    case CDROM_GET_CAPABILITY: return CDC_SELECT_SPEED;
    case CDROM_DRIVE_STATUS: return CDS_DISC_OK;
    case CDROM_DISC_STATUS: return CDS_AUDIO;
    case CDROM_LOCKDOOR: case CDROMSTART: case CDROMSTOP: return 0;
    default: break;
    }
    if (request != CDROMREADTOCHDR && request != CDROMREADTOCENTRY &&
        request != CDROM_SEND_PACKET && request != SG_IO && request != CDROMREADAUDIO)
        return fail();
    va_list args;
    va_start(args, request);
    void* pointer = va_arg(args, void*);
    va_end(args);
    if (!pointer) return fail(EINVAL);
    if (request == CDROMREADTOCHDR) {
        auto* header = static_cast<cdrom_tochdr*>(pointer);
        header->cdth_trk0 = header->cdth_trk1 = 1;
        event("TOC", fd, reads.load());
        return 0;
    }
    if (request == CDROMREADTOCENTRY) {
        auto* entry = static_cast<cdrom_tocentry*>(pointer);
        if (entry->cdte_format != CDROM_LBA ||
            (entry->cdte_track != 1 && entry->cdte_track != CDROM_LEADOUT)) return fail(EINVAL);
        entry->cdte_ctrl = 0;
        entry->cdte_addr.lba = entry->cdte_track == 1 ? 0 :
            (control("changed") && reads.load() == 6 ? 76 : 75);
        if (entry->cdte_track == CDROM_LEADOUT) event("LEADOUT", fd, entry->cdte_addr.lba);
        return 0;
    }
    if (request == CDROM_SEND_PACKET) {
        auto* packet = static_cast<cdrom_generic_command*>(pointer);
        if (packet->cmd[0] != 0x46 || !packet->buffer || packet->buflen != 512 ||
            packet->data_direction != CGC_DATA_READ) return fail(EINVAL);
        std::memset(packet->buffer, 0, packet->buflen);
        packet->buffer[3] = 12; // 16-byte configuration, one valid CD Read descriptor.
        packet->buffer[9] = 0x1e;
        packet->buffer[10] = 1;
        packet->buffer[11] = 4;
        packet->buffer[12] = 2;
        event("CONFIGURATION", fd);
        return 0;
    }
    if (request == CDROMREADAUDIO) {
        auto* audio = static_cast<cdrom_read_audio*>(pointer);
        event("ORDINARY_AUDIO", fd, audio->nframes);
        if (!control("allow_audio")) return fail(EIO);
        if (!audio->buf || audio->addr_format != CDROM_LBA || audio->nframes < 1 || audio->nframes > 75)
            return fail(EINVAL);
        std::memset(audio->buf, 0, audio->nframes * 2352);
        return 0;
    }
    auto* sg = static_cast<sg_io_hdr_t*>(pointer);
    if (sg->cmdp && sg->cmdp[0] == 0x5a) {
        event("MODE_UNSUPPORTED", fd);
        return fail(ENOTSUP);
    }
    const auto* c = sg->cmdp;
    if (!c || sg->interface_id != 'S' || sg->cmd_len != 12 || c[0] != 0xbe || c[1] != 4 ||
        c[6] != 0 || c[7] != 0 || c[8] != 1 || c[9] != 0x12 || c[10] || c[11] ||
        sg->dxfer_direction != SG_DXFER_FROM_DEV || sg->dxfer_len != 2646 ||
        !sg->dxferp || sg->timeout != 1000) {
        event("INVALID_READ", fd);
        return fail(EINVAL);
    }
    const unsigned lba = (unsigned(c[2]) << 24) | (unsigned(c[3]) << 16) | (unsigned(c[4]) << 8) | c[5];
    const unsigned count = ++reads;
    if ((lba != 10 && lba != 20) || count > 6) {
        event("INVALID_READ", fd);
        return fail(EINVAL);
    }
    raw_fd = fd;
    event("READ_CD", fd, lba);
    if (control("gate")) {
        event("READ_ENTERED", fd);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!control("release")) {
            if (std::chrono::steady_clock::now() >= deadline) {
                event("GATE_EXPIRED", fd);
                return fail(ETIMEDOUT);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        event("GATE_RELEASED", fd);
    }
    if (control("late")) std::this_thread::sleep_for(std::chrono::milliseconds(4050));
    std::memset(sg->dxferp, 0, sg->dxfer_len);
    if (lba == 20) static_cast<unsigned char*>(sg->dxferp)[2352] = 1;
    sg->status = sg->host_status = sg->driver_status = 0;
    sg->info = SG_INFO_OK;
    sg->resid = control("short") ? 1 : 0;
    event("READ_RETURN", fd, sg->resid);
    return 0;
}
