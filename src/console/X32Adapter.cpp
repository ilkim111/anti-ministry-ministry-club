#include "console/X32Adapter.hpp"
#include <spdlog/spdlog.h>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cmath>
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
    // X32 has limited OSC throughput — throttle requests to avoid UDP drops.
    // Using /xremote first to establish subscription, then querying.
    sendOscQuery("/xremote");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

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
        sendOscQuery(channelPath(ch, "/eq/on"));
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

        // Throttle: ~20 queries per channel, pause between channels
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Buses
    for (int bus = 1; bus <= 16; bus++) {
        sendOscQuery(busPath(bus, "/config/name"));
        sendOscQuery(busPath(bus, "/mix/fader"));
        sendOscQuery(busPath(bus, "/mix/on"));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void X32Adapter::setChannelParam(int ch, ChannelParam param, float value) {
    // X32 OSC: ALL float parameters must be normalized [0.0, 1.0].
    // Convert human-readable values (Hz, dB, Q, ratio) to normalized floats.
    switch (param) {
        case ChannelParam::Fader:
            // Already normalized [0.0, 1.0] by ActionValidator::dbToFaderFloat
            sendOsc(channelPath(ch, "/mix/fader"), value);
            break;
        case ChannelParam::Pan:
            // Already normalized [0.0, 1.0]
            sendOsc(channelPath(ch, "/mix/pan"), value);
            break;
        case ChannelParam::Gain:
            sendOsc(channelPath(ch, "/preamp/trim"), value);
            break;
        case ChannelParam::HighPassFreq: {
            float norm = hpfFreqToNorm(value);
            spdlog::debug("X32: ch{} HPF {:.0f}Hz -> norm {:.4f}", ch, value, norm);
            sendOsc(channelPath(ch, "/preamp/hpf"), norm);
            break;
        }
        // EQ frequency — logf [20, 20000, 201]
        case ChannelParam::EqBand1Freq:
        case ChannelParam::EqBand2Freq:
        case ChannelParam::EqBand3Freq:
        case ChannelParam::EqBand4Freq: {
            float norm = eqFreqToNorm(value);
            int band = (param == ChannelParam::EqBand1Freq) ? 1 :
                       (param == ChannelParam::EqBand2Freq) ? 2 :
                       (param == ChannelParam::EqBand3Freq) ? 3 : 4;
            spdlog::debug("X32: ch{} EQ band{} freq {:.0f}Hz -> norm {:.4f}", ch, band, value, norm);
            sendOsc(channelPath(ch, "/eq/" + std::to_string(band) + "/f"), norm);
            break;
        }
        // EQ gain — linf [-15, 15, 0.250]
        case ChannelParam::EqBand1Gain:
        case ChannelParam::EqBand2Gain:
        case ChannelParam::EqBand3Gain:
        case ChannelParam::EqBand4Gain: {
            float norm = eqGainToNorm(value);
            int band = (param == ChannelParam::EqBand1Gain) ? 1 :
                       (param == ChannelParam::EqBand2Gain) ? 2 :
                       (param == ChannelParam::EqBand3Gain) ? 3 : 4;
            spdlog::debug("X32: ch{} EQ band{} gain {:.1f}dB -> norm {:.4f}", ch, band, value, norm);
            sendOsc(channelPath(ch, "/eq/" + std::to_string(band) + "/g"), norm);
            break;
        }
        // EQ Q — logf [10.0, 0.3, 72]
        case ChannelParam::EqBand1Q:
        case ChannelParam::EqBand2Q:
        case ChannelParam::EqBand3Q:
        case ChannelParam::EqBand4Q: {
            float norm = eqQToNorm(value);
            int band = (param == ChannelParam::EqBand1Q) ? 1 :
                       (param == ChannelParam::EqBand2Q) ? 2 :
                       (param == ChannelParam::EqBand3Q) ? 3 : 4;
            spdlog::debug("X32: ch{} EQ band{} Q {:.2f} -> norm {:.4f}", ch, band, value, norm);
            sendOsc(channelPath(ch, "/eq/" + std::to_string(band) + "/q"), norm);
            break;
        }
        // Compressor threshold — linf [-60, 0, 0.500]
        case ChannelParam::CompThreshold: {
            float norm = compThreshToNorm(value);
            spdlog::debug("X32: ch{} comp thresh {:.1f}dB -> norm {:.4f}", ch, value, norm);
            sendOsc(channelPath(ch, "/dyn/thr"), norm);
            break;
        }
        // Compressor ratio — enum [0..11], must be sent as int
        case ChannelParam::CompRatio: {
            int idx = compRatioToEnum(value);
            spdlog::debug("X32: ch{} comp ratio {:.1f}:1 -> enum {}", ch, value, idx);
            sendOsc(channelPath(ch, "/dyn/ratio"), idx);
            break;
        }
        case ChannelParam::CompAttack:
            sendOsc(channelPath(ch, "/dyn/attack"), value);
            break;
        case ChannelParam::CompRelease:
            sendOsc(channelPath(ch, "/dyn/release"), value);
            break;
        case ChannelParam::CompMakeup:
            sendOsc(channelPath(ch, "/dyn/mgain"), value);
            break;
        // Gate threshold — linf [-80, 0, 0.500]
        case ChannelParam::GateThreshold: {
            float norm = gateThreshToNorm(value);
            spdlog::debug("X32: ch{} gate thresh {:.1f}dB -> norm {:.4f}", ch, value, norm);
            sendOsc(channelPath(ch, "/gate/thr"), norm);
            break;
        }
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
            // X32: mix/on is inverted — ON=1 means unmuted, OFF=0 means muted
            // Mute=true → send mix/on=0 (OFF), Mute=false → send mix/on=1 (ON)
            sendOsc(channelPath(ch, "/mix/on"), value ? 0 : 1);
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

        if (path == "/mix/fader")       { update.param = ChannelParam::Fader; update.value = value; }
        else if (path == "/mix/on")     { update.param = ChannelParam::Mute; update.value = value; }
        else if (path == "/mix/pan")    { update.param = ChannelParam::Pan; update.value = value; }
        else if (path == "/config/name"){
            update.param = ChannelParam::Name;
            update.value = value;
            if (std::holds_alternative<std::string>(value))
                update.strValue = std::get<std::string>(value);
        }
        else if (path == "/preamp/trim")  { update.param = ChannelParam::Gain; update.value = value; }
        else if (path == "/preamp/hpon")  { update.param = ChannelParam::HighPassOn; update.value = value; }
        // EQ params: X32 sends normalized [0,1], convert to human-readable
        else if (path == "/eq/1/f" || path == "/eq/2/f" ||
                 path == "/eq/3/f" || path == "/eq/4/f") {
            if (path == "/eq/1/f")      update.param = ChannelParam::EqBand1Freq;
            else if (path == "/eq/2/f") update.param = ChannelParam::EqBand2Freq;
            else if (path == "/eq/3/f") update.param = ChannelParam::EqBand3Freq;
            else                        update.param = ChannelParam::EqBand4Freq;
            if (std::holds_alternative<float>(value))
                update.value = normToEqFreq(std::get<float>(value));
            else update.value = value;
        }
        else if (path == "/eq/1/g" || path == "/eq/2/g" ||
                 path == "/eq/3/g" || path == "/eq/4/g") {
            if (path == "/eq/1/g")      update.param = ChannelParam::EqBand1Gain;
            else if (path == "/eq/2/g") update.param = ChannelParam::EqBand2Gain;
            else if (path == "/eq/3/g") update.param = ChannelParam::EqBand3Gain;
            else                        update.param = ChannelParam::EqBand4Gain;
            if (std::holds_alternative<float>(value))
                update.value = normToEqGain(std::get<float>(value));
            else update.value = value;
        }
        else if (path == "/eq/1/q" || path == "/eq/2/q" ||
                 path == "/eq/3/q" || path == "/eq/4/q") {
            if (path == "/eq/1/q")      update.param = ChannelParam::EqBand1Q;
            else if (path == "/eq/2/q") update.param = ChannelParam::EqBand2Q;
            else if (path == "/eq/3/q") update.param = ChannelParam::EqBand3Q;
            else                        update.param = ChannelParam::EqBand4Q;
            if (std::holds_alternative<float>(value))
                update.value = normToEqQ(std::get<float>(value));
            else update.value = value;
        }
        // HPF: X32 sends normalized, convert to Hz
        else if (path == "/preamp/hpf") {
            update.param = ChannelParam::HighPassFreq;
            if (std::holds_alternative<float>(value))
                update.value = normToHpfFreq(std::get<float>(value));
            else update.value = value;
        }
        // Compressor/gate thresholds: X32 sends normalized, convert to dB
        else if (path == "/dyn/thr") {
            update.param = ChannelParam::CompThreshold;
            if (std::holds_alternative<float>(value))
                update.value = std::get<float>(value) * 60.0f - 60.0f;
            else update.value = value;
        }
        else if (path == "/dyn/ratio")   { update.param = ChannelParam::CompRatio; update.value = value; }
        else if (path == "/dyn/attack")  { update.param = ChannelParam::CompAttack; update.value = value; }
        else if (path == "/dyn/release") { update.param = ChannelParam::CompRelease; update.value = value; }
        else if (path == "/dyn/on")      { update.param = ChannelParam::CompOn; update.value = value; }
        else if (path == "/gate/thr") {
            update.param = ChannelParam::GateThreshold;
            if (std::holds_alternative<float>(value))
                update.value = std::get<float>(value) * 80.0f - 80.0f;
            else update.value = value;
        }
        else if (path == "/gate/range")  { update.param = ChannelParam::GateRange; update.value = value; }
        else if (path == "/gate/on")     { update.param = ChannelParam::GateOn; update.value = value; }
        else return;
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
    // X32 meter blob format: OSC address + type tag ",b\0\0" + blob
    // Blob: 4-byte big-endian size, then payload
    // Payload: 4-byte LE int32 count, then count x int16 LE meter values
    size_t offset = 0;
    // Skip address
    while (offset < len && data[offset] != 0) offset++;
    offset++;
    while (offset % 4 != 0) offset++;
    // Skip type tag
    if (offset >= len || data[offset] != ',') return;
    offset += 4;
    // Read blob size (big-endian per OSC spec)
    if (offset + 4 > len) return;
    uint32_t blobSize;
    memcpy(&blobSize, data + offset, 4);
    blobSize = ntohl(blobSize);
    offset += 4;

    if (offset + blobSize > len || blobSize < 4) return;

    // First 4 bytes of blob payload = number of float values (little-endian int32)
    uint32_t numValues;
    memcpy(&numValues, data + offset, 4);
    offset += 4;

    // X32 meter blob: native little-endian float32 values in range [0.0, 1.0]
    // (headroom allows up to ~8.0 for +18 dBFS)
    // /meters/0 layout: 32 inputs + 8 aux returns + 8 st fx returns + 16 bus masters + 6 matrixes = 70
    int channels = std::min((int)numValues, 32);
    for (int ch = 0; ch < channels && offset + 4 <= len; ch++) {
        float level;
        memcpy(&level, data + offset, 4);  // native LE float, no byte swap
        offset += 4;

        float dbfs = (level > 0.0001f) ? 20.0f * log10f(level) : -96.0f;

        if (onMeterUpdate)
            onMeterUpdate(ch + 1, dbfs, dbfs);
    }
}

void X32Adapter::sendKeepalive() {
    sendOscQuery("/xremote");
}

void X32Adapter::renewMeterSubscription() {
    // X32: /meters with string arg "/meters/0" subscribes to METERS page for ~10s
    // /meters/0 = METERS page (70 floats: 32 in + 8 aux ret + 8 fx ret + 16 bus + 6 mtx)
    // /meters/1 = channel page (96: 32 in + 32 gate + 32 dyn gain reduction)
    // /meters/2 = mix bus page (49: 16 bus + 6 mtx + 2 main LR + 1 mono + dyn GR)
    sendOsc("/meters", std::string("/meters/0"));
    lastMeterRenew_ = std::chrono::steady_clock::now();
}

// ── X32 Parameter Normalization ──────────────────────────────────────────
// X32 OSC protocol: ALL float params are normalized [0.0, 1.0].
// See "Type rules" in the X32 OSC Protocol doc, page 10-13.

// EQ frequency: logf [20, 20000, 201] — log scale 20Hz to 20kHz
float X32Adapter::eqFreqToNorm(float hz) {
    hz = std::clamp(hz, 20.0f, 20000.0f);
    return std::clamp(std::log10(hz / 20.0f) / std::log10(1000.0f), 0.0f, 1.0f);
}

float X32Adapter::normToEqFreq(float norm) {
    norm = std::clamp(norm, 0.0f, 1.0f);
    return 20.0f * std::pow(10.0f, norm * std::log10(1000.0f));
}

// EQ gain: linf [-15, 15, 0.250] — linear -15dB to +15dB
float X32Adapter::eqGainToNorm(float db) {
    db = std::clamp(db, -15.0f, 15.0f);
    return (db + 15.0f) / 30.0f;
}

float X32Adapter::normToEqGain(float norm) {
    norm = std::clamp(norm, 0.0f, 1.0f);
    return norm * 30.0f - 15.0f;
}

// EQ Q: logf [10.0, 0.3, 72] — log scale from 10.0 (tight) to 0.3 (wide)
float X32Adapter::eqQToNorm(float q) {
    q = std::clamp(q, 0.3f, 10.0f);
    // 10.0 → 0.0, 0.3 → 1.0 (log scale, inverted)
    return std::clamp(std::log10(q / 10.0f) / std::log10(0.03f), 0.0f, 1.0f);
}

float X32Adapter::normToEqQ(float norm) {
    norm = std::clamp(norm, 0.0f, 1.0f);
    return 10.0f * std::pow(10.0f, norm * std::log10(0.03f));
}

// HPF frequency: logf [20, 400, 101] — log scale 20Hz to 400Hz
float X32Adapter::hpfFreqToNorm(float hz) {
    hz = std::clamp(hz, 20.0f, 400.0f);
    return std::clamp(std::log10(hz / 20.0f) / std::log10(20.0f), 0.0f, 1.0f);
}

float X32Adapter::normToHpfFreq(float norm) {
    norm = std::clamp(norm, 0.0f, 1.0f);
    return 20.0f * std::pow(10.0f, norm * std::log10(20.0f));
}

// Compressor threshold: linf [-60, 0, 0.500] — linear -60dB to 0dB
float X32Adapter::compThreshToNorm(float db) {
    db = std::clamp(db, -60.0f, 0.0f);
    return (db + 60.0f) / 60.0f;
}

// Compressor ratio: enum [0..11] → {1.1, 1.3, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 7.0, 10, 20, 100}
int X32Adapter::compRatioToEnum(float ratio) {
    static const float ratios[] = {1.1f, 1.3f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 7.0f, 10.0f, 20.0f, 100.0f};
    int best = 0;
    float bestDiff = std::abs(ratio - ratios[0]);
    for (int i = 1; i < 12; i++) {
        float diff = std::abs(ratio - ratios[i]);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = i;
        }
    }
    return best;
}

// Gate threshold: linf [-80, 0, 0.500] — linear -80dB to 0dB
float X32Adapter::gateThreshToNorm(float db) {
    db = std::clamp(db, -80.0f, 0.0f);
    return (db + 80.0f) / 80.0f;
}

std::vector<uint8_t> X32Adapter::buildOscMessage(
    const std::string& /*address*/, const std::vector<ParamValue>& /*args*/) {
    // Generic builder — not used directly, using typed sendOsc overloads instead
    return {};
}
