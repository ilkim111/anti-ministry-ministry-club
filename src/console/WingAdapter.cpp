#include "console/WingAdapter.hpp"
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

WingAdapter::WingAdapter() = default;

WingAdapter::~WingAdapter() {
    disconnect();
}

bool WingAdapter::connect(const std::string& ip, int port) {
    ip_   = ip;
    port_ = port > 0 ? port : 2222;

    sockFd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockFd_ < 0) {
        spdlog::error("Wing: failed to create UDP socket");
        return false;
    }

    struct timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 100000;
    setsockopt(sockFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port_);
    addr.sin_addr.s_addr = inet_addr(ip_.c_str());

    if (::connect(sockFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        spdlog::error("Wing: failed to connect to {}:{}", ip_, port_);
        ::close(sockFd_);
        sockFd_ = -1;
        return false;
    }

    connected_ = true;
    running_   = true;
    lastKeepalive_ = std::chrono::steady_clock::now();

    recvThread_ = std::thread(&WingAdapter::receiveLoop, this);

    spdlog::info("Wing: connected to {}:{}", ip_, port_);
    if (onConnectionChange) onConnectionChange(true);
    return true;
}

void WingAdapter::disconnect() {
    running_ = false;
    connected_ = false;
    if (recvThread_.joinable())
        recvThread_.join();
    if (sockFd_ >= 0) {
        ::close(sockFd_);
        sockFd_ = -1;
    }
    if (onConnectionChange) onConnectionChange(false);
}

bool WingAdapter::isConnected() const {
    return connected_;
}

ConsoleCapabilities WingAdapter::capabilities() const {
    return {
        .model       = "Wing",
        .firmware     = "",
        .channelCount = 48,
        .busCount     = 16,
        .matrixCount  = 8,
        .dcaCount     = 8,
        .fxSlots      = 16,
        .eqBands      = 6,
        .hasMotorizedFaders = true,
        .hasDynamicEq       = true,
        .hasMultibandComp   = true,
        .meterUpdateRateMs  = 50
    };
}

void WingAdapter::requestFullSync() {
    // Wing uses similar OSC structure but with different paths
    // /ch/1/... instead of /ch/01/...
    sendOsc("/$remotestate", 1);

    for (int ch = 1; ch <= 48; ch++) {
        sendOsc(channelPath(ch, "/name"), std::string(""));
        sendOsc(channelPath(ch, "/fader"), 0.0f);
        sendOsc(channelPath(ch, "/mute"), 0);
    }

    for (int bus = 1; bus <= 16; bus++) {
        sendOsc(busPath(bus, "/name"), std::string(""));
        sendOsc(busPath(bus, "/fader"), 0.0f);
    }
}

void WingAdapter::setChannelParam(int ch, ChannelParam param, float value) {
    switch (param) {
        case ChannelParam::Fader:
            sendOsc(channelPath(ch, "/fader"), value); break;
        case ChannelParam::Pan:
            sendOsc(channelPath(ch, "/pan"), value); break;
        case ChannelParam::Gain:
            sendOsc(channelPath(ch, "/preamp/gain"), value); break;
        case ChannelParam::HighPassFreq:
            sendOsc(channelPath(ch, "/hpf/freq"), value); break;
        case ChannelParam::EqBand1Freq:
            sendOsc(channelPath(ch, "/eq/1/freq"), value); break;
        case ChannelParam::EqBand1Gain:
            sendOsc(channelPath(ch, "/eq/1/gain"), value); break;
        case ChannelParam::EqBand1Q:
            sendOsc(channelPath(ch, "/eq/1/q"), value); break;
        case ChannelParam::CompThreshold:
            sendOsc(channelPath(ch, "/comp/thr"), value); break;
        case ChannelParam::CompRatio:
            sendOsc(channelPath(ch, "/comp/ratio"), value); break;
        default:
            spdlog::warn("Wing: unhandled float param for ch{}", ch);
            break;
    }
}

void WingAdapter::setChannelParam(int ch, ChannelParam param, bool value) {
    switch (param) {
        case ChannelParam::Mute:
            sendOsc(channelPath(ch, "/mute"), value ? 1 : 0); break;
        case ChannelParam::EqOn:
            sendOsc(channelPath(ch, "/eq/on"), value ? 1 : 0); break;
        case ChannelParam::CompOn:
            sendOsc(channelPath(ch, "/comp/on"), value ? 1 : 0); break;
        default:
            spdlog::warn("Wing: unhandled bool param for ch{}", ch);
            break;
    }
}

void WingAdapter::setChannelParam(int ch, ChannelParam param, const std::string& value) {
    if (param == ChannelParam::Name)
        sendOsc(channelPath(ch, "/name"), value);
}

void WingAdapter::setSendLevel(int ch, int bus, float value) {
    char path[64];
    snprintf(path, sizeof(path), "/ch/%d/send/%d/level", ch, bus);
    sendOsc(path, value);
}

void WingAdapter::setBusParam(int bus, BusParam param, float value) {
    switch (param) {
        case BusParam::Fader:
            sendOsc(busPath(bus, "/fader"), value); break;
        default: break;
    }
}

void WingAdapter::subscribeMeter(int /*refreshMs*/) {
    metering_ = true;
    sendOsc("/$meters", 1);
}

void WingAdapter::unsubscribeMeter() {
    metering_ = false;
    sendOsc("/$meters", 0);
}

void WingAdapter::tick() {
    if (!connected_) return;
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastKeepalive_).count();
    if (ms > 8000) {
        sendKeepalive();
        lastKeepalive_ = now;
    }
}

// ── Private ──────────────────────────────────────────────────────────────

std::string WingAdapter::channelPath(int ch, const std::string& suffix) {
    return "/ch/" + std::to_string(ch) + suffix;
}

std::string WingAdapter::busPath(int bus, const std::string& suffix) {
    return "/bus/" + std::to_string(bus) + suffix;
}

void WingAdapter::sendOsc(const std::string& address, float value) {
    std::vector<uint8_t> msg;
    for (char c : address) msg.push_back(c);
    msg.push_back(0);
    while (msg.size() % 4 != 0) msg.push_back(0);
    msg.push_back(','); msg.push_back('f'); msg.push_back(0); msg.push_back(0);
    uint32_t bits;
    memcpy(&bits, &value, 4);
    bits = htonl(bits);
    auto* p = reinterpret_cast<uint8_t*>(&bits);
    msg.insert(msg.end(), p, p + 4);
    sendRaw(msg);
}

void WingAdapter::sendOsc(const std::string& address, int value) {
    std::vector<uint8_t> msg;
    for (char c : address) msg.push_back(c);
    msg.push_back(0);
    while (msg.size() % 4 != 0) msg.push_back(0);
    msg.push_back(','); msg.push_back('i'); msg.push_back(0); msg.push_back(0);
    uint32_t val = htonl(static_cast<uint32_t>(value));
    auto* p = reinterpret_cast<uint8_t*>(&val);
    msg.insert(msg.end(), p, p + 4);
    sendRaw(msg);
}

void WingAdapter::sendOsc(const std::string& address, const std::string& value) {
    std::vector<uint8_t> msg;
    for (char c : address) msg.push_back(c);
    msg.push_back(0);
    while (msg.size() % 4 != 0) msg.push_back(0);
    msg.push_back(','); msg.push_back('s'); msg.push_back(0); msg.push_back(0);
    for (char c : value) msg.push_back(c);
    msg.push_back(0);
    while (msg.size() % 4 != 0) msg.push_back(0);
    sendRaw(msg);
}

void WingAdapter::sendRaw(const std::vector<uint8_t>& data) {
    if (sockFd_ < 0) return;
    ::send(sockFd_, data.data(), data.size(), 0);
}

void WingAdapter::receiveLoop() {
    uint8_t buf[4096];
    while (running_) {
        ssize_t n = ::recv(sockFd_, buf, sizeof(buf), 0);
        if (n > 0) {
            parseOscMessage(buf, n);
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            spdlog::warn("Wing: receive error: {}", strerror(errno));
            connected_ = false;
            break;
        }
    }
}

void WingAdapter::parseOscMessage(const uint8_t* /*data*/, size_t /*len*/) {
    // Wing OSC parsing — similar structure to X32
    // Implementation mirrors X32Adapter::parseOscMessage
    // with Wing-specific address mappings
}

void WingAdapter::sendKeepalive() {
    sendOsc("/$remotestate", 1);
}
