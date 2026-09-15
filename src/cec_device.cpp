#include "cec_device.hpp"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/cec.h>
#include <linux/cec-funcs.h>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <utility>

namespace {
template<class T> void checked_ioctl(int fd, unsigned long request, T& value) {
    if (ioctl(fd, request, &value) < 0)
        throw std::system_error(errno, std::generic_category(), "CEC ioctl");
}
std::string physical_address_text(__u16 physical) {
    std::ostringstream text;
    text << std::hex << ((physical >> 12) & 15) << '.' << ((physical >> 8) & 15)
         << '.' << ((physical >> 4) & 15) << '.' << (physical & 15);
    return text.str();
}
}
CecDevice::CecDevice(std::string path, bool diagnostics)
    : path_(std::move(path)), diagnostics_(diagnostics) {}
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
        if (!(caps.capabilities & CEC_CAP_LOG_ADDRS) ||
            !(caps.capabilities & CEC_CAP_TRANSMIT) || caps.available_log_addrs < 1)
            throw std::runtime_error("CEC adapter cannot configure logical addresses");
        // An opened CEC file descriptor is not a follower by default, so it
        // would never receive commands addressed to our logical address.
        // This is deliberately non-exclusive: cec-ctl may still inspect it.
        __u32 mode = CEC_MODE_INITIATOR | CEC_MODE_FOLLOWER;
        checked_ioctl(fd_, CEC_S_MODE, mode);
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
        physical_ = CEC_PHYS_ADDR_INVALID;
        logical_ = CEC_LOG_ADDR_INVALID;
        active_source_ = false;
        status("waiting for valid physical address (f.f.f.f)");
        return;
    }
    std::ostringstream description;
    description << "physical=" << physical_address_text(physical);
    if (addresses.num_log_addrs == 0) {
        physical_ = physical;
        logical_ = CEC_LOG_ADDR_INVALID;
        active_source_ = false;
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
        logical_ = CEC_LOG_ADDR_INVALID;
        active_source_ = false;
        status(description.str() + " waiting for playback logical address");
        return;
    }
    if (physical_ != CEC_PHYS_ADDR_INVALID && physical_ != physical)
        active_source_ = false;
    physical_ = physical;
    logical_ = addresses.log_addr[0];
    description << " logical=" << std::dec << unsigned(addresses.log_addr[0])
                << " name=PiCDPlayer " << (requested_ ? "claim confirmed" : "existing registration reused");
    status(description.str());
}

void CecDevice::report_active_source(const char* reason) {
    if (physical_ == CEC_PHYS_ADDR_INVALID || logical_ == CEC_LOG_ADDR_INVALID)
        throw std::runtime_error("CEC active source requested before logical address claim");
    cec_msg response{};
    cec_msg_init(&response, logical_, CEC_LOG_ADDR_BROADCAST);
    cec_msg_active_source(&response, physical_);
    if (ioctl(fd_, CEC_TRANSMIT, &response) < 0)
        throw std::system_error(errno, std::generic_category(), "CEC report active source");
    std::cout << "cec: active_source=" << physical_address_text(physical_)
              << " reason=" << reason << '\n' << std::flush;
}

void CecDevice::set_active_source(bool active, const char* reason, std::uint16_t path) {
    if (active_source_ == active) return;
    active_source_ = active;
    std::cout << "cec: active_source=" << (active ? physical_address_text(physical_) : "inactive")
              << " reason=" << reason << " path=" << physical_address_text(path) << '\n' << std::flush;
}

int CecDevice::poll_fd() const noexcept { return fd_; }

CecReceiveResult CecDevice::receive() {
    if (fd_ < 0) return {};
    cec_msg message{};
    timespec before{};
    clock_gettime(CLOCK_MONOTONIC, &before);
    if (ioctl(fd_, CEC_RECEIVE, &message) < 0) {
        if (errno == EAGAIN || errno == EINTR) return {};
        throw std::system_error(errno, std::generic_category(), "CEC receive");
    }
    timespec after{};
    clock_gettime(CLOCK_MONOTONIC, &after);
    const auto now_ns = std::uint64_t(after.tv_sec) * 1'000'000'000ULL + after.tv_nsec;
    const auto call_us = (std::int64_t(after.tv_sec) - before.tv_sec) * 1'000'000LL +
                         (std::int64_t(after.tv_nsec) - before.tv_nsec) / 1'000;
    const auto kernel_to_daemon_us = message.rx_ts && now_ns >= message.rx_ts
        ? (now_ns - message.rx_ts) / 1'000ULL : 0;
    CecReceiveResult result{true, std::nullopt};
    if (!(message.rx_status & CEC_RX_STATUS_OK) || message.len < 2)
        return result;
    if (cec_msg_opcode(&message) == CEC_MSG_GIVE_DEVICE_POWER_STATUS && message.len == 2) {
        cec_msg response{};
        cec_msg_set_reply_to(&response, &message);
        cec_msg_report_power_status(&response, CEC_OP_POWER_STATUS_ON);
        if (ioctl(fd_, CEC_TRANSMIT, &response) < 0)
            throw std::system_error(errno, std::generic_category(), "CEC report power status");
        std::cout << "cec: power_status=on source=" << unsigned(cec_msg_initiator(&message))
                  << '\n' << std::flush;
        return result;
    }
    if (cec_msg_opcode(&message) == CEC_MSG_SET_STREAM_PATH && message.len == 4) {
        __u16 requested_path{};
        cec_ops_set_stream_path(&message, &requested_path);
        set_active_source(requested_path == physical_, "set_stream_path", requested_path);
        if (active_source_) report_active_source("set_stream_path");
        return result;
    }
    if (cec_msg_opcode(&message) == CEC_MSG_ACTIVE_SOURCE && message.len == 4) {
        __u16 source_path{};
        cec_ops_active_source(&message, &source_path);
        set_active_source(source_path == physical_, "active_source", source_path);
        return result;
    }
    if (cec_msg_opcode(&message) == CEC_MSG_REQUEST_ACTIVE_SOURCE && message.len == 2) {
        if (active_source_) report_active_source("request_active_source");
        return result;
    }
    if (message.len < 3 || cec_msg_opcode(&message) != CEC_MSG_USER_CONTROL_PRESSED)
        return result;
    const auto command = cec_command_from_ui_code(message.msg[2]);
    result.command = command;
    const auto timing = diagnostics_
        ? " kernel_to_daemon_us=" + std::to_string(kernel_to_daemon_us) +
          " receive_ioctl_us=" + std::to_string(call_us)
        : "";
    if (command) {
        std::cout << "cec: command=" << cec_command_name(*command)
                  << " source=" << unsigned(cec_msg_initiator(&message)) << timing << '\n' << std::flush;
    } else {
        std::cout << "cec: ignored UI code=0x" << std::hex << unsigned(message.msg[2])
                  << std::dec << " source=" << unsigned(cec_msg_initiator(&message)) << timing << '\n' << std::flush;
    }
    return result;
}
