#include "cd_device.hpp"
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cstdint>
#include <linux/cdrom.h>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>

namespace {
struct ScopedFd {
    int value;
    ~ScopedFd() { close(value); }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    explicit ScopedFd(int fd) : value(fd) {}
};
const char* disc_status_name(int status) {
    switch (status) {
    case CDS_NO_INFO: return "NO_INFO";
    case CDS_AUDIO: return "AUDIO";
    case CDS_MIXED: return "MIXED";
    case CDS_DATA_1: return "DATA_1";
    case CDS_DATA_2: return "DATA_2";
    case CDS_XA_2_1: return "XA_2_1";
    case CDS_XA_2_2: return "XA_2_2";
    default: return "UNKNOWN";
    }
}
const char* drive_status_name(int status) {
    switch (status) {
    case CDS_NO_INFO: return "NO_INFO";
    case CDS_NO_DISC: return "NO_DISC";
    case CDS_TRAY_OPEN: return "TRAY_OPEN";
    case CDS_DRIVE_NOT_READY: return "NOT_READY";
    case CDS_DISC_OK: return "DISC_OK";
    default: return "UNKNOWN";
    }
}
}

CdMediaSnapshot read_cd_media(const std::string& device) {
    // O_NONBLOCK permits opening an empty drive for status ioctls.
    // It does not guarantee that the ioctl itself completes without waiting.
    const int fd = open(device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "open " + device);
    const ScopedFd guard(fd);
    const int status = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
    const int error = errno;
    if (status < 0)
        throw std::system_error(error, std::generic_category(), "CDROM_DRIVE_STATUS " + device);
    if (status != CDS_DISC_OK) {
        MediaObservation observation = MediaObservation::unknown;
        if (status == CDS_TRAY_OPEN) observation = MediaObservation::tray_open;
        else if (status == CDS_NO_DISC) observation = MediaObservation::no_disc;
        else if (status == CDS_DRIVE_NOT_READY) observation = MediaObservation::not_ready;
        return {observation, status, std::nullopt};
    }
    const int disc = ioctl(fd, CDROM_DISC_STATUS, 0);
    if (disc < 0)
        throw std::system_error(errno, std::generic_category(), "CDROM_DISC_STATUS " + device);
    MediaObservation observation = MediaObservation::unsupported_disc;
    if (disc == CDS_AUDIO) observation = MediaObservation::audio_disc;
    else if (disc == CDS_NO_INFO) observation = MediaObservation::unknown;
    return {observation, status, disc};
}

void probe_cd_media(const std::string& device) {
    const auto snapshot = read_cd_media(device);
    std::cout << "cd: device=" << device << " drive_status="
              << drive_status_name(snapshot.drive_status) << " raw=" << snapshot.drive_status
              << '\n' << std::flush;
    if (!snapshot.disc_status) {
        std::cout << "cd: disc_status=NOT_QUERIED (drive not ready)\n";
        return;
    }
    const int disc = *snapshot.disc_status;
    std::cout << "cd: disc_status=" << disc_status_name(disc) << " raw=" << disc << '\n';
}


DiscToc read_cd_toc(const std::string& device) {
    const int fd = open(device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "open " + device);
    const ScopedFd guard(fd);
    const int status = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
    if (status < 0)
        throw std::system_error(errno, std::generic_category(), "CDROM_DRIVE_STATUS " + device);
    if (status != CDS_DISC_OK) throw std::runtime_error("TOC requires a ready disc");
    const int disc = ioctl(fd, CDROM_DISC_STATUS, 0);
    if (disc < 0)
        throw std::system_error(errno, std::generic_category(), "CDROM_DISC_STATUS " + device);
    if (disc != CDS_AUDIO) throw std::runtime_error("TOC diagnostic currently requires an audio-only CD");
    cdrom_tochdr header{};
    if (ioctl(fd, CDROMREADTOCHDR, &header) < 0)
        throw std::system_error(errno, std::generic_category(), "CDROMREADTOCHDR " + device);
    const int first = header.cdth_trk0;
    const int last = header.cdth_trk1;
    if (first < 1 || last > 99 || first > last)
        throw std::runtime_error("invalid TOC track range");
    std::vector<std::int32_t> starts;
    std::int32_t leadout = 0;
    for (int track = first; track <= last + 1; ++track) {
        cdrom_tocentry entry{};
        entry.cdte_track = track <= last ? track : CDROM_LEADOUT;
        entry.cdte_format = CDROM_LBA;
        if (ioctl(fd, CDROMREADTOCENTRY, &entry) < 0)
            throw std::system_error(errno, std::generic_category(),
                                    "CDROMREADTOCENTRY track=" + std::to_string(entry.cdte_track));
        if (entry.cdte_format != CDROM_LBA || entry.cdte_addr.lba < 0)
            throw std::runtime_error("unsupported TOC address");
        if (track <= last && (entry.cdte_ctrl & CDROM_DATA_TRACK))
            throw std::runtime_error("unexpected data track in audio-only TOC");
        if (track <= last) starts.push_back(entry.cdte_addr.lba);
        else leadout = entry.cdte_addr.lba;
    }
    return make_audio_toc(first, starts, leadout);
}

void eject_cd(const std::string& device) {
    const int fd = open(device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "open " + device);
    const ScopedFd guard(fd);
    if (ioctl(fd, CDROM_LOCKDOOR, 0) < 0)
        throw std::system_error(errno, std::generic_category(), "CDROM_LOCKDOOR unlock " + device);
    if (ioctl(fd, CDROMEJECT, 0) < 0) {
        const auto error = errno;
        // Restore the appliance-style lock when the tray did not open.
        (void)ioctl(fd, CDROM_LOCKDOOR, 1);
        throw std::system_error(error, std::generic_category(), "CDROMEJECT " + device);
    }
}

void probe_cd_toc(const std::string& device) {
    const auto toc = read_cd_toc(device);
    // Buffer the report until all entries have been read and validated.
    std::ostringstream report;
    report << "cd: TOC first=" << toc.tracks.front().number << " last=" << toc.tracks.back().number
           << " tracks=" << toc.tracks.size() << "\n";
    for (const auto& track : toc.tracks) {
        const auto frames = track.length_frames;
        report << "track=" << track.number << " type=AUDIO start_lba="
               << track.start_lba << " frames=" << frames
               << " duration=" << frames / (cd_frames_per_second * 60) << ':' << std::setfill('0')
               << std::setw(2) << (frames / cd_frames_per_second) % 60 << ':'
               << std::setw(2) << frames % cd_frames_per_second << "\n";
    }
    report << "leadout_lba=" << toc.leadout_lba
           << " span_frames=" << toc.span_frames() << "\n";
    std::cout << report.str();
}
