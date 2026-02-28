#include "console/X32Adapter.hpp"
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>

X32Adapter::X32Adapter() = default;

X32Adapter::~X32Adapter() {
    disconnect();
}

bool X32Adapter::connect(const std::string& ip, int port) {
    ip_   = ip;
    port_ = port > 0 ? port : 10023;

    sockFd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockFd_ < 0) {
        spdlog::error("X32: failed to create UDP socket");
        return false;
    }

    // Set receive timeout
    struct timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 100000;  // 100ms
    setsockopt(sockFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port_);
    addr.sin_addr.s_addr = inet_addr(ip_.c_str());

    if (::connect(sockFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        spdlog::error("X32: failed to connect to {}:{}", ip_, port_);
        ::close(sockFd_);
        sockFd_ = -1;
        return false;
    }

    connected_ = true;
    running_   = true;
    lastKeepalive_ = std::chrono::steady_clock::now();

    // Start receive thread
    recvThread_ = std::thread(&X32Adapter::receiveLoop, this);

    // Send initial /xinfo to verify connection
    sendOscQuery("/xinfo");

    spdlog::info("X32: connected to {}:{}", ip_, port_);

    if (onConnectionChange) onConnectionChange(true);
    return true;
}

void X32Adapter::disconnect() {
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

bool X32Adapter::isConnected() const {
    return connected_;
}

ConsoleCapabilities X32Adapter::capabilities() const {
    return {
        .model       = "X32",
        .firmware     = "",
        .channelCount = 32,
        .busCount     = 16,
        .matrixCount  = 6,
        .dcaCount     = 8,
        .fxSlots      = 8,
        .eqBands      = 4,
        .hasMotorizedFaders = true,
        .hasDynamicEq       = false,
        .hasMultibandComp   = false,
        .meterUpdateRateMs  = 50
    };
}

void X32Adapter::requestFullSync() {
    // X32 supports /‐‐‐info for bulk dump, but the standard approach
    // is to request each channel's parameters individually.
    // Using /xremote first to establish subscription, then querying.
    sendOscQuery("/xremote");

    for (int ch = 1; ch <= 32; ch++) {
        // Request channel config block
        sendOscQuery(channelPath(ch, "/config/name"));
        sendOscQuery(channelPath(ch, "/mix/fader"));
        sendOscQuery(channelPath(ch, "/mix/on"));
        sendOscQuery(channelPath(ch, "/mix/pan"));
        sendOscQuery(channelPath(ch, "/preamp/trim"));
        sendOscQuery(channelPath(ch, "/preamp/hpon"));
        sendOscQuery(channelPath(ch, "/preamp/hpf"));

        // EQ
        for (int b = 1; b <= 4; b++) {
            std::string prefix = "/eq/" + std::to_string(b);
            sendOscQuery(channelPath(ch, prefix + "/f"));
            sendOscQuery(channelPath(ch, prefix + "/g"));
            sendOscQuery(channelPath(ch, prefix + "/q"));
        }

        // Dynamics
        sendOscQuery(channelPath(ch, "/dyn/thr"));
        sendOscQuery(channelPath(ch, "/dyn/ratio"));
        sendOscQuery(channelPath(ch, "/dyn/attack"));
        sendOscQuery(channelPath(ch, "/dyn/release"));
        sendOscQuery(channelPath(ch, "/dyn/on"));

        // Gate
        sendOscQuery(channelPath(ch, "/gate/thr"));
        sendOscQuery(channelPath(ch, "/gate/range"));
        sendOscQuery(channelPath(ch, "/gate/on"));
    }

    // Buses
    for (int bus = 1; bus <= 16; bus++) {
        sendOscQuery(busPath(bus, "/config/name"));
        sendOscQuery(busPath(bus, "/mix/fader"));
        sendOscQuery(busPath(bus, "/mix/on"));
    }
}

void X32Adapter::setChannelParam(int ch, ChannelParam param, float value) {
    switch (param) {
        case ChannelParam::Fader:
            sendOsc(channelPath(ch, "/mix/fader"), value);
            break;
        case ChannelParam::Pan:
            sendOsc(channelPath(ch, "/mix/pan"), value);
            break;
        case ChannelParam::Gain:
            sendOsc(channelPath(ch, "/preamp/trim"), value);
            break;
        case ChannelParam::HighPassFreq:
            sendOsc(channelPath(ch, "/preamp/hpf"), value);
            break;
        case ChannelParam::EqBand1Freq:
            sendOsc(channelPath(ch, "/eq/1/f"), value);
            break;
        case ChannelParam::EqBand1Gain:
            sendOsc(channelPath(ch, "/eq/1/g"), value);
            break;
        case ChannelParam::EqBand1Q:
            sendOsc(channelPath(ch, "/eq/1/q"), value);
            break;
        case ChannelParam::EqBand2Freq:
            sendOsc(channelPath(ch, "/eq/2/f"), value);
            break;
        case ChannelParam::EqBand2Gain:
            sendOsc(channelPath(ch, "/eq/2/g"), value);
            break;
        case ChannelParam::EqBand2Q:
            sendOsc(channelPath(ch, "/eq/2/q"), value);
            break;
        case ChannelParam::EqBand3Freq:
            sendOsc(channelPath(ch, "/eq/3/f"), value);
            break;
        case ChannelParam::EqBand3Gain:
            sendOsc(channelPath(ch, "/eq/3/g"), value);
            break;
        case ChannelParam::EqBand3Q:
            sendOsc(channelPath(ch, "/eq/3/q"), value);
            break;
        case ChannelParam::EqBand4Freq:
            sendOsc(channelPath(ch, "/eq/4/f"), value);
            break;
        case ChannelParam::EqBand4Gain:
            sendOsc(channelPath(ch, "/eq/4/g"), value);
            break;
        case ChannelParam::EqBand4Q:
            sendOsc(channelPath(ch, "/eq/4/q"), value);
            break;
        case ChannelParam::CompThreshold:
            sendOsc(channelPath(ch, "/dyn/thr"), value);
            break;
        case ChannelParam::CompRatio:
            sendOsc(channelPath(ch, "/dyn/ratio"), value);
            break;
        case ChannelParam::CompAttack:
            sendOsc(channelPath(ch, "/dyn/attack"), value);
            break;
        case ChannelParam::CompRelease:
            sendOsc(channelPath(ch, "/dyn/release"), value);
            break;
        case ChannelParam::CompMakeup:
            sendOsc(channelPath(ch, "/dyn/mgain"), value);
            break;
        case ChannelParam::GateThreshold:
            sendOsc(channelPath(ch, "/gate/thr"), value);
            break;
        case ChannelParam::GateRange:
            sendOsc(channelPath(ch, "/gate/range"), value);
            break;
        default:
            spdlog::warn("X32: unhandled float param for ch{}", ch);
            break;
    }
}

void X32Adapter::setChannelParam(int ch, ChannelParam param, bool value) {
    int intVal = value ? 1 : 0;
    switch (param) {
        case ChannelParam::Mute:
            // X32: mix/on is inverted — ON=1 means unmuted
            sendOsc(channelPath(ch, "/mix/on"), value ? 1 : 0);
            break;
        case ChannelParam::EqOn:
            sendOsc(channelPath(ch, "/eq/on"), intVal);
            break;
        case ChannelParam::CompOn:
            sendOsc(channelPath(ch, "/dyn/on"), intVal);
            break;
        case ChannelParam::GateOn:
            sendOsc(channelPath(ch, "/gate/on"), intVal);
            break;
        case ChannelParam::HighPassOn:
            sendOsc(channelPath(ch, "/preamp/hpon"), intVal);
            break;
        default:
            spdlog::warn("X32: unhandled bool param for ch{}", ch);
            break;
    }
}

void X32Adapter::setChannelParam(int ch, ChannelParam param, const std::string& value) {
    if (param == ChannelParam::Name) {
        sendOsc(channelPath(ch, "/config/name"), value);
    }
}

void X32Adapter::setSendLevel(int ch, int bus, float value) {
    // X32 send path: /ch/XX/mix/YY/level
    char path[64];
    snprintf(path, sizeof(path), "/ch/%02d/mix/%02d/level", ch, bus);
    sendOsc(path, value);
}

void X32Adapter::setBusParam(int bus, BusParam param, float value) {
    switch (param) {
        case BusParam::Fader:
            sendOsc(busPath(bus, "/mix/fader"), value);
            break;
        case BusParam::Pan:
            sendOsc(busPath(bus, "/mix/pan"), value);
            break;
        default:
            break;
    }
}

void X32Adapter::subscribeMeter(int refreshMs) {
    metering_ = true;
    meterRefreshMs_ = refreshMs;
    // X32 meter subscription: /meters /0 (inputs)
    // Need to renew every 10 seconds
    renewMeterSubscription();
}

void X32Adapter::unsubscribeMeter() {
    metering_ = false;
}

void X32Adapter::tick() {
    if (!connected_) return;

    auto now = std::chrono::steady_clock::now();
    auto msSinceKeepalive = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastKeepalive_).count();

    // X32 requires /xremote every 10 seconds to maintain subscription
    if (msSinceKeepalive > 8000) {
        sendKeepalive();
        lastKeepalive_ = now;
    }

    // Renew meter subscription every 9 seconds
    if (metering_) {
        auto msSinceMeter = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastMeterRenew_).count();
        if (msSinceMeter > 9000) {
            renewMeterSubscription();
            lastMeterRenew_ = now;
        }
    }
}

// ── Private ──────────────────────────────────────────────────────────────

std::string X32Adapter::channelPath(int ch, const std::string& suffix) {
    char buf[64];
    snprintf(buf, sizeof(buf), "/ch/%02d", ch);
    return std::string(buf) + suffix;
}

std::string X32Adapter::busPath(int bus, const std::string& suffix) {
    char buf[64];
    snprintf(buf, sizeof(buf), "/bus/%02d", bus);
    return std::string(buf) + suffix;
}

void X32Adapter::sendOsc(const std::string& address, float value) {
    // OSC message: address string (padded to 4-byte boundary)
    // + type tag ",f\0\0" + float32 big-endian
    std::vector<uint8_t> msg;

    // Address string (null terminated, padded to 4 bytes)
    for (char c : address) msg.push_back(c);
    msg.push_back(0);
    while (msg.size() % 4 != 0) msg.push_back(0);

    // Type tag
    msg.push_back(',');
    msg.push_back('f');
    msg.push_back(0);
    msg.push_back(0);

    // Float value (big-endian)
    uint32_t bits;
    memcpy(&bits, &value, 4);
    bits = htonl(bits);
    auto* p = reinterpret_cast<uint8_t*>(&bits);
    msg.insert(msg.end(), p, p + 4);

    sendRaw(msg);
}

void X32Adapter::sendOsc(const std::string& address, int value) {
    std::vector<uint8_t> msg;
    for (char c : address) msg.push_back(c);
    msg.push_back(0);
    while (msg.size() % 4 != 0) msg.push_back(0);

    msg.push_back(',');
    msg.push_back('i');
    msg.push_back(0);
    msg.push_back(0);

    uint32_t val = htonl(static_cast<uint32_t>(value));
    auto* p = reinterpret_cast<uint8_t*>(&val);
    msg.insert(msg.end(), p, p + 4);

    sendRaw(msg);
}

void X32Adapter::sendOsc(const std::string& address, const std::string& value) {
    std::vector<uint8_t> msg;
    for (char c : address) msg.push_back(c);
    msg.push_back(0);
    while (msg.size() % 4 != 0) msg.push_back(0);

    msg.push_back(',');
    msg.push_back('s');
    msg.push_back(0);
    msg.push_back(0);

    for (char c : value) msg.push_back(c);
    msg.push_back(0);
    while (msg.size() % 4 != 0) msg.push_back(0);

    sendRaw(msg);
}

void X32Adapter::sendOscQuery(const std::string& address) {
    // Query message — no args, just the address
    std::vector<uint8_t> msg;
    for (char c : address) msg.push_back(c);
    msg.push_back(0);
    while (msg.size() % 4 != 0) msg.push_back(0);
    sendRaw(msg);
}

void X32Adapter::sendRaw(const std::vector<uint8_t>& data) {
    if (sockFd_ < 0) return;
    ::send(sockFd_, data.data(), data.size(), 0);
}

void X32Adapter::receiveLoop() {
    uint8_t buf[4096];
    while (running_) {
        ssize_t n = ::recv(sockFd_, buf, sizeof(buf), 0);
        if (n > 0) {
            parseOscMessage(buf, n);
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            spdlog::warn("X32: receive error: {}", strerror(errno));
            connected_ = false;
            if (onConnectionChange) onConnectionChange(false);
            break;
        }
    }
}

void X32Adapter::parseOscMessage(const uint8_t* data, size_t len) {
    if (len < 4) return;

    // Check for meter blob
    if (len > 4 && data[0] == '/' && memcmp(data, "/meters", 7) == 0) {
        handleMeterMessage(data, len);
        return;
    }

    // Extract OSC address
    std::string address(reinterpret_cast<const char*>(data));
    size_t addrLen = address.size() + 1;
    while (addrLen % 4 != 0) addrLen++;

    if (addrLen >= len) return;

    // Find type tag
    if (data[addrLen] != ',') return;
    char typeTag = data[addrLen + 1];
    size_t dataOffset = addrLen + 4;  // skip ",X\0\0"

    ParamValue value;
    if (typeTag == 'f' && dataOffset + 4 <= len) {
        uint32_t bits;
        memcpy(&bits, data + dataOffset, 4);
        bits = ntohl(bits);
        float f;
        memcpy(&f, &bits, 4);
        value = f;
    } else if (typeTag == 'i' && dataOffset + 4 <= len) {
        uint32_t bits;
        memcpy(&bits, data + dataOffset, 4);
        int32_t i = static_cast<int32_t>(ntohl(bits));
        value = (i != 0);
    } else if (typeTag == 's') {
        std::string s(reinterpret_cast<const char*>(data + dataOffset));
        value = s;
    } else {
        return;
    }

    handleParameterMessage(address, value);
}

void X32Adapter::handleParameterMessage(const std::string& address, const ParamValue& value) {
    // Parse X32 OSC address into ParameterUpdate
    // Examples: /ch/01/mix/fader, /ch/01/config/name, /bus/03/mix/fader

    ParameterUpdate update{};

    if (address.substr(0, 4) == "/ch/") {
        update.target = ParameterUpdate::Target::Channel;
        update.index  = std::stoi(address.substr(4, 2));
        std::string path = address.substr(6);

        if (path == "/mix/fader")       { update.param = ChannelParam::Fader;   }
        else if (path == "/mix/on")     { update.param = ChannelParam::Mute; /* inverted */ }
        else if (path == "/mix/pan")    { update.param = ChannelParam::Pan;     }
        else if (path == "/config/name"){
            update.param = ChannelParam::Name;
            if (std::holds_alternative<std::string>(value))
                update.strValue = std::get<std::string>(value);
        }
        else if (path == "/preamp/trim")  { update.param = ChannelParam::Gain; }
        else if (path == "/preamp/hpf")   { update.param = ChannelParam::HighPassFreq; }
        else if (path == "/preamp/hpon")  { update.param = ChannelParam::HighPassOn; }
        else if (path == "/eq/1/f") { update.param = ChannelParam::EqBand1Freq; }
        else if (path == "/eq/1/g") { update.param = ChannelParam::EqBand1Gain; }
        else if (path == "/eq/1/q") { update.param = ChannelParam::EqBand1Q; }
        else if (path == "/eq/2/f") { update.param = ChannelParam::EqBand2Freq; }
        else if (path == "/eq/2/g") { update.param = ChannelParam::EqBand2Gain; }
        else if (path == "/eq/2/q") { update.param = ChannelParam::EqBand2Q; }
        else if (path == "/eq/3/f") { update.param = ChannelParam::EqBand3Freq; }
        else if (path == "/eq/3/g") { update.param = ChannelParam::EqBand3Gain; }
        else if (path == "/eq/3/q") { update.param = ChannelParam::EqBand3Q; }
        else if (path == "/eq/4/f") { update.param = ChannelParam::EqBand4Freq; }
        else if (path == "/eq/4/g") { update.param = ChannelParam::EqBand4Gain; }
        else if (path == "/eq/4/q") { update.param = ChannelParam::EqBand4Q; }
        else if (path == "/dyn/thr")     { update.param = ChannelParam::CompThreshold; }
        else if (path == "/dyn/ratio")   { update.param = ChannelParam::CompRatio; }
        else if (path == "/dyn/attack")  { update.param = ChannelParam::CompAttack; }
        else if (path == "/dyn/release") { update.param = ChannelParam::CompRelease; }
        else if (path == "/dyn/on")      { update.param = ChannelParam::CompOn; }
        else if (path == "/gate/thr")    { update.param = ChannelParam::GateThreshold; }
        else if (path == "/gate/range")  { update.param = ChannelParam::GateRange; }
        else if (path == "/gate/on")     { update.param = ChannelParam::GateOn; }
        else return;

        update.value = value;
    } else if (address.substr(0, 5) == "/bus/") {
        update.target = ParameterUpdate::Target::Bus;
        update.index  = std::stoi(address.substr(5, 2));
        std::string path = address.substr(7);

        if (path == "/mix/fader")        { update.param = ChannelParam::Fader; }
        else if (path == "/mix/on")      { update.param = ChannelParam::Mute;  }
        else if (path == "/config/name") {
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

void X32Adapter::handleMeterMessage(const uint8_t* data, size_t len) {
    // X32 meter blob format: /meters followed by blob of float32 values
    // First 32 floats are input channel meters (0.0–1.0 normalized)
    size_t offset = 0;
    // Skip address
    while (offset < len && data[offset] != 0) offset++;
    offset++;
    while (offset % 4 != 0) offset++;
    // Skip type tag
    if (offset >= len || data[offset] != ',') return;
    offset += 4;
    // Skip blob size
    if (offset + 4 > len) return;
    uint32_t blobSize;
    memcpy(&blobSize, data + offset, 4);
    blobSize = ntohl(blobSize);
    offset += 4;

    int channels = std::min((int)(blobSize / 4), 32);
    for (int ch = 0; ch < channels && offset + 4 <= len; ch++) {
        uint32_t bits;
        memcpy(&bits, data + offset, 4);
        bits = ntohl(bits);
        float level;
        memcpy(&level, &bits, 4);
        offset += 4;

        // Convert 0.0–1.0 to dBFS (X32 uses roughly log scale)
        float dbfs = (level > 0.0001f) ? 20.0f * log10f(level) : -96.0f;

        if (onMeterUpdate)
            onMeterUpdate(ch + 1, dbfs, dbfs);  // RMS and peak approximation
    }
}

void X32Adapter::sendKeepalive() {
    sendOscQuery("/xremote");
}

void X32Adapter::renewMeterSubscription() {
    // X32: /meters /0 requests input meters
    sendOscQuery("/meters");
    lastMeterRenew_ = std::chrono::steady_clock::now();
}

std::vector<uint8_t> X32Adapter::buildOscMessage(
    const std::string& /*address*/, const std::vector<ParamValue>& /*args*/) {
    // Generic builder — not used directly, using typed sendOsc overloads instead
    return {};
}
