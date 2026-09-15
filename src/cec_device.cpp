#include "cec_device.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/cec.h>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>

namespace {
template<class T> void checked_ioctl(int fd, unsigned long request, T& value) {
    if (ioctl(fd, request, &value) < 0)
        throw std::system_error(errno, std::generic_category(), "CEC ioctl");
}
}
CecDevice::CecDevice(std::string path) : path_(std::move(path)) {}
CecDevice::~CecDevice() { if (fd_ >= 0) close(fd_); }
void CecDevice::status(const std::string& message) {
    if (message != last_status_) {
        std::cout << "cec: " << message << '\n' << std::flush;
        last_status_ = message;
    }
}
void CecDevice::update() {
    if (fd_ < 0) {
        fd_ = open(path_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) {
            if (errno == ENOENT) { status("waiting for " + path_); return; }
            throw std::system_error(errno, std::generic_category(), "open " + path_);
        }
        cec_caps caps{};
        checked_ioctl(fd_, CEC_ADAP_G_CAPS, caps);
        if (!(caps.capabilities & CEC_CAP_LOG_ADDRS) || caps.available_log_addrs < 1)
            throw std::runtime_error("CEC adapter cannot configure logical addresses");
        status(std::string("opened ") + path_ + " driver=" + caps.driver);
    }
    __u16 physical{};
    checked_ioctl(fd_, CEC_ADAP_G_PHYS_ADDR, physical);
    cec_log_addrs addresses{};
    checked_ioctl(fd_, CEC_ADAP_G_LOG_ADDRS, addresses);
    if (addresses.num_log_addrs != 0 &&
        (addresses.num_log_addrs != 1 ||
         addresses.log_addr_type[0] != CEC_LOG_ADDR_TYPE_PLAYBACK ||
         addresses.primary_device_type[0] != CEC_OP_PRIM_DEVTYPE_PLAYBACK ||
         std::strncmp(addresses.osd_name, "PiCDPlayer", sizeof(addresses.osd_name)) != 0))
        throw std::runtime_error("CEC has another configuration; refusing to replace it");
    if (physical == CEC_PHYS_ADDR_INVALID) {
        status("waiting for valid physical address (f.f.f.f)");
        return;
    }
    std::ostringstream description;
    description << "physical=" << std::hex << ((physical >> 12) & 15) << '.'
                << ((physical >> 8) & 15) << '.' << ((physical >> 4) & 15)
                << '.' << (physical & 15);
    if (addresses.num_log_addrs == 0) {
        addresses.cec_version = CEC_OP_CEC_VERSION_2_0;
        addresses.vendor_id = CEC_VENDOR_ID_NONE;
        addresses.num_log_addrs = 1;
        std::strcpy(addresses.osd_name, "PiCDPlayer");
        addresses.primary_device_type[0] = CEC_OP_PRIM_DEVTYPE_PLAYBACK;
        addresses.log_addr_type[0] = CEC_LOG_ADDR_TYPE_PLAYBACK;
        addresses.all_device_types[0] = CEC_OP_ALL_DEVTYPE_PLAYBACK;
        checked_ioctl(fd_, CEC_ADAP_S_LOG_ADDRS, addresses);
        requested_ = true;
        status(description.str() + " playback claim requested");
        return;
    }
    if (!(addresses.log_addr_mask & CEC_LOG_ADDR_MASK_PLAYBACK)) {
        status(description.str() + " waiting for playback logical address");
        return;
    }
    description << " logical=" << std::dec << unsigned(addresses.log_addr[0])
                << " name=PiCDPlayer " << (requested_ ? "claim confirmed" : "existing registration reused");
    status(description.str());
}
