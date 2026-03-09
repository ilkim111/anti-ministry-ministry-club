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
    port_ = port > 0 ? port : 2223;  // Wing OSC port is 2223

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

    // Query console info
    sendOscQuery("/?");

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
    // Subscribe to OSC updates first
    sendOscQuery("/*s");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Wing paths: /ch/N/param (no zero-padding, short names)
    // Fader values are in dB (-144 to +10), not 0.0-1.0
    for (int ch = 1; ch <= 48; ch++) {
        sendOscQuery(channelPath(ch, "/name"));
        sendOscQuery(channelPath(ch, "/fdr"));
        sendOscQuery(channelPath(ch, "/mute"));
        sendOscQuery(channelPath(ch, "/pan"));
        sendOscQuery(channelPath(ch, "/in/set/trim"));

        // Filter (HPF/LPF)
        sendOscQuery(channelPath(ch, "/flt/lc"));
        sendOscQuery(channelPath(ch, "/flt/lcf"));

        // Parametric EQ (up to 6 bands + L/H shelf)
        sendOscQuery(channelPath(ch, "/peq/on"));
        for (int b = 1; b <= 3; b++) {
            std::string bs = std::to_string(b);
            sendOscQuery(channelPath(ch, "/peq/" + bs + "f"));
            sendOscQuery(channelPath(ch, "/peq/" + bs + "g"));
            sendOscQuery(channelPath(ch, "/peq/" + bs + "q"));
        }

        // Main EQ
        sendOscQuery(channelPath(ch, "/eq/on"));
        for (int b = 1; b <= 4; b++) {
            std::string bs = std::to_string(b);
            sendOscQuery(channelPath(ch, "/eq/" + bs + "f"));
            sendOscQuery(channelPath(ch, "/eq/" + bs + "g"));
            sendOscQuery(channelPath(ch, "/eq/" + bs + "q"));
        }

        // Dynamics (compressor)
        sendOscQuery(channelPath(ch, "/dyn/on"));
        sendOscQuery(channelPath(ch, "/dyn/thr"));
        sendOscQuery(channelPath(ch, "/dyn/ratio"));
        sendOscQuery(channelPath(ch, "/dyn/att"));
        sendOscQuery(channelPath(ch, "/dyn/rel"));

        // Gate
        sendOscQuery(channelPath(ch, "/gate/on"));
        sendOscQuery(channelPath(ch, "/gate/thr"));
        sendOscQuery(channelPath(ch, "/gate/range"));

        // Throttle to avoid UDP drops
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Buses
    for (int bus = 1; bus <= 16; bus++) {
        sendOscQuery(busPath(bus, "/name"));
        sendOscQuery(busPath(bus, "/fdr"));
        sendOscQuery(busPath(bus, "/mute"));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void WingAdapter::setChannelParam(int ch, ChannelParam param, float value) {
    switch (param) {
        case ChannelParam::Fader:
            // Wing faders accept dB values directly (-144 to +10)
            sendOsc(channelPath(ch, "/fdr"), value);
            break;
        case ChannelParam::Pan:
            sendOsc(channelPath(ch, "/pan"), value);
            break;
        case ChannelParam::Gain:
            sendOsc(channelPath(ch, "/in/set/trim"), value);
            break;
        case ChannelParam::HighPassFreq:
            sendOsc(channelPath(ch, "/flt/lcf"), value);
            break;
        case ChannelParam::EqBand1Freq:
            sendOsc(channelPath(ch, "/eq/1f"), value); break;
        case ChannelParam::EqBand1Gain:
            sendOsc(channelPath(ch, "/eq/1g"), value); break;
        case ChannelParam::EqBand1Q:
            sendOsc(channelPath(ch, "/eq/1q"), value); break;
        case ChannelParam::EqBand2Freq:
            sendOsc(channelPath(ch, "/eq/2f"), value); break;
        case ChannelParam::EqBand2Gain:
            sendOsc(channelPath(ch, "/eq/2g"), value); break;
        case ChannelParam::EqBand2Q:
            sendOsc(channelPath(ch, "/eq/2q"), value); break;
        case ChannelParam::EqBand3Freq:
            sendOsc(channelPath(ch, "/eq/3f"), value); break;
        case ChannelParam::EqBand3Gain:
            sendOsc(channelPath(ch, "/eq/3g"), value); break;
        case ChannelParam::EqBand3Q:
            sendOsc(channelPath(ch, "/eq/3q"), value); break;
        case ChannelParam::EqBand4Freq:
            sendOsc(channelPath(ch, "/eq/4f"), value); break;
        case ChannelParam::EqBand4Gain:
            sendOsc(channelPath(ch, "/eq/4g"), value); break;
        case ChannelParam::EqBand4Q:
            sendOsc(channelPath(ch, "/eq/4q"), value); break;
        case ChannelParam::CompThreshold:
            sendOsc(channelPath(ch, "/dyn/thr"), value); break;
        case ChannelParam::CompRatio:
            sendOsc(channelPath(ch, "/dyn/ratio"), value); break;
        case ChannelParam::CompAttack:
            sendOsc(channelPath(ch, "/dyn/att"), value); break;
        case ChannelParam::CompRelease:
            sendOsc(channelPath(ch, "/dyn/rel"), value); break;
        case ChannelParam::CompMakeup:
            sendOsc(channelPath(ch, "/dyn/gain"), value); break;
        case ChannelParam::GateThreshold:
            sendOsc(channelPath(ch, "/gate/thr"), value); break;
        case ChannelParam::GateRange:
            sendOsc(channelPath(ch, "/gate/range"), value); break;
        default:
            spdlog::warn("Wing: unhandled float param for ch{}", ch);
            break;
    }
}

void WingAdapter::setChannelParam(int ch, ChannelParam param, bool value) {
    switch (param) {
        case ChannelParam::Mute:
            // Wing: mute=1 means muted (direct, not inverted like X32)
            sendOsc(channelPath(ch, "/mute"), value ? 1 : 0);
            break;
        case ChannelParam::EqOn:
            sendOsc(channelPath(ch, "/eq/on"), value ? 1 : 0);
            break;
        case ChannelParam::CompOn:
            sendOsc(channelPath(ch, "/dyn/on"), value ? 1 : 0);
            break;
        case ChannelParam::GateOn:
            sendOsc(channelPath(ch, "/gate/on"), value ? 1 : 0);
            break;
        case ChannelParam::HighPassOn:
            sendOsc(channelPath(ch, "/flt/lc"), value ? 1 : 0);
            break;
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
    // Wing send level: /ch/N/bus/M/lvl (dB value)
    char path[64];
    snprintf(path, sizeof(path), "/ch/%d/bus/%d/lvl", ch, bus);
    sendOsc(path, value);
}

void WingAdapter::setBusParam(int bus, BusParam param, float value) {
    switch (param) {
        case BusParam::Fader:
            sendOsc(busPath(bus, "/fdr"), value);
            break;
        case BusParam::Pan:
            sendOsc(busPath(bus, "/pan"), value);
            break;
        default: break;
    }
}

void WingAdapter::subscribeMeter(int /*refreshMs*/) {
    metering_ = true;
    // Wing: /*s subscribes to OSC updates for 10 seconds
    sendOscQuery("/*s");
    lastMeterRenew_ = std::chrono::steady_clock::now();
}

void WingAdapter::unsubscribeMeter() {
    metering_ = false;
}

void WingAdapter::tick() {
    if (!connected_) return;
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastKeepalive_).count();

    // Wing subscriptions timeout after 10 seconds — renew at 8s
    if (ms > 8000) {
        sendKeepalive();
        lastKeepalive_ = now;
    }

    // Renew meter/OSC subscription
    if (metering_) {
        auto msSinceMeter = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastMeterRenew_).count();
        if (msSinceMeter > 9000) {
            sendOscQuery("/*s");
            lastMeterRenew_ = now;
        }
    }
}

// ── Private ──────────────────────────────────────────────────────────────

std::string WingAdapter::channelPath(int ch, const std::string& suffix) {
    // Wing uses /ch/N (no zero-padding)
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

void WingAdapter::sendOscQuery(const std::string& address) {
    std::vector<uint8_t> msg;
    for (char c : address) msg.push_back(c);
    msg.push_back(0);
    while (msg.size() % 4 != 0) msg.push_back(0);
    sendRaw(msg);
}

void WingAdapter::sendRaw(const std::vector<uint8_t>& data) {
    if (sockFd_ < 0) return;
    ::send(sockFd_, data.data(), data.size(), 0);
}

void WingAdapter::receiveLoop() {
    uint8_t buf[32768];  // Wing max UDP packet is 32k
    while (running_) {
        ssize_t n = ::recv(sockFd_, buf, sizeof(buf), 0);
        if (n > 0) {
            parseOscMessage(buf, n);
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            spdlog::warn("Wing: receive error: {}", strerror(errno));
            connected_ = false;
            if (onConnectionChange) onConnectionChange(false);
            break;
        }
    }
}

void WingAdapter::parseOscMessage(const uint8_t* data, size_t len) {
    if (len < 4) return;

    // Extract OSC address
    std::string address(reinterpret_cast<const char*>(data));
    size_t addrLen = address.size() + 1;
    while (addrLen % 4 != 0) addrLen++;

    if (addrLen >= len) return;

    // Find type tag
    if (data[addrLen] != ',') return;
    std::string typeTags(reinterpret_cast<const char*>(data + addrLen + 1));
    size_t dataOffset = addrLen + 4;  // skip ",XXX\0" padded

    // Wing returns ,sff (string, float[0-1], float dB) for faders
    // or ,sfi (string, float[0-1], int) for mutes/enums
    // Parse the last meaningful value

    ParamValue value;
    bool hasValue = false;

    for (char tag : typeTags) {
        if (tag == 0) break;
        if (tag == 's') {
            // String arg
            if (dataOffset < len) {
                std::string s(reinterpret_cast<const char*>(data + dataOffset));
                size_t sLen = s.size() + 1;
                while (sLen % 4 != 0) sLen++;
                dataOffset += sLen;
                // For name params, use the string value
                if (address.find("/name") != std::string::npos) {
                    value = s;
                    hasValue = true;
                }
            }
        } else if (tag == 'f') {
            if (dataOffset + 4 <= len) {
                uint32_t bits;
                memcpy(&bits, data + dataOffset, 4);
                bits = ntohl(bits);
                float f;
                memcpy(&f, &bits, 4);
                dataOffset += 4;
                // Wing fader responses: ,sff — second float is the dB value
                // For other params, the float is the actual value
                value = f;
                hasValue = true;
            }
        } else if (tag == 'i') {
            if (dataOffset + 4 <= len) {
                uint32_t bits;
                memcpy(&bits, data + dataOffset, 4);
                int32_t i = static_cast<int32_t>(ntohl(bits));
                dataOffset += 4;
                value = (i != 0);
                hasValue = true;
            }
        }
    }

    if (!hasValue) return;

    handleParameterMessage(address, value);
}

void WingAdapter::handleParameterMessage(const std::string& address, const ParamValue& value) {
    ParameterUpdate update{};

    if (address.substr(0, 4) == "/ch/") {
        update.target = ParameterUpdate::Target::Channel;

        // Parse channel number — Wing uses /ch/N (no zero-padding)
        size_t slashPos = address.find('/', 4);
        if (slashPos == std::string::npos) return;
        update.index = std::stoi(address.substr(4, slashPos - 4));
        std::string path = address.substr(slashPos);

        if (path == "/fdr")              { update.param = ChannelParam::Fader; }
        else if (path == "/mute")        { update.param = ChannelParam::Mute; }
        else if (path == "/pan")         { update.param = ChannelParam::Pan; }
        else if (path == "/name") {
            update.param = ChannelParam::Name;
            if (std::holds_alternative<std::string>(value))
                update.strValue = std::get<std::string>(value);
        }
        else if (path == "/in/set/trim") { update.param = ChannelParam::Gain; }
        else if (path == "/flt/lcf")     { update.param = ChannelParam::HighPassFreq; }
        else if (path == "/flt/lc")      { update.param = ChannelParam::HighPassOn; }
        else if (path == "/eq/on")       { update.param = ChannelParam::EqOn; }
        else if (path == "/eq/1f")       { update.param = ChannelParam::EqBand1Freq; }
        else if (path == "/eq/1g")       { update.param = ChannelParam::EqBand1Gain; }
        else if (path == "/eq/1q")       { update.param = ChannelParam::EqBand1Q; }
        else if (path == "/eq/2f")       { update.param = ChannelParam::EqBand2Freq; }
        else if (path == "/eq/2g")       { update.param = ChannelParam::EqBand2Gain; }
        else if (path == "/eq/2q")       { update.param = ChannelParam::EqBand2Q; }
        else if (path == "/eq/3f")       { update.param = ChannelParam::EqBand3Freq; }
        else if (path == "/eq/3g")       { update.param = ChannelParam::EqBand3Gain; }
        else if (path == "/eq/3q")       { update.param = ChannelParam::EqBand3Q; }
        else if (path == "/eq/4f")       { update.param = ChannelParam::EqBand4Freq; }
        else if (path == "/eq/4g")       { update.param = ChannelParam::EqBand4Gain; }
        else if (path == "/eq/4q")       { update.param = ChannelParam::EqBand4Q; }
        else if (path == "/dyn/thr")     { update.param = ChannelParam::CompThreshold; }
        else if (path == "/dyn/ratio")   { update.param = ChannelParam::CompRatio; }
        else if (path == "/dyn/att")     { update.param = ChannelParam::CompAttack; }
        else if (path == "/dyn/rel")     { update.param = ChannelParam::CompRelease; }
        else if (path == "/dyn/on")      { update.param = ChannelParam::CompOn; }
        else if (path == "/gate/thr")    { update.param = ChannelParam::GateThreshold; }
        else if (path == "/gate/range")  { update.param = ChannelParam::GateRange; }
        else if (path == "/gate/on")     { update.param = ChannelParam::GateOn; }
        else return;

        update.value = value;
    } else if (address.substr(0, 5) == "/bus/") {
        update.target = ParameterUpdate::Target::Bus;

        size_t slashPos = address.find('/', 5);
        if (slashPos == std::string::npos) return;
        update.index = std::stoi(address.substr(5, slashPos - 5));
        std::string path = address.substr(slashPos);

        if (path == "/fdr")              { update.param = ChannelParam::Fader; }
        else if (path == "/mute")        { update.param = ChannelParam::Mute; }
        else if (path == "/name") {
            update.param = ChannelParam::Name;
            if (std::holds_alternative<std::string>(value))
                update.strValue = std::get<std::string>(value);
        }
        else return;

        update.value = value;
    } else {
        return;
    }

    if (onParameterUpdate)
        onParameterUpdate(update);
}

void WingAdapter::sendKeepalive() {
    // Wing: renew OSC subscription (times out after 10s)
    sendOscQuery("/*s");
}
