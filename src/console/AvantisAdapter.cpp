#include "console/AvantisAdapter.hpp"
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

AvantisAdapter::AvantisAdapter() = default;

AvantisAdapter::~AvantisAdapter() {
    disconnect();
}

bool AvantisAdapter::connect(const std::string& ip, int port) {
    ip_   = ip;
    port_ = port > 0 ? port : 51325;

    sockFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFd_ < 0) {
        spdlog::error("Avantis: failed to create TCP socket");
        return false;
    }

    // Set receive timeout
    struct timeval tv{};
    tv.tv_sec  = 5;
    tv.tv_usec = 0;
    setsockopt(sockFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port_);
    addr.sin_addr.s_addr = inet_addr(ip_.c_str());

    if (::connect(sockFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        spdlog::error("Avantis: failed to connect to {}:{}", ip_, port_);
        ::close(sockFd_);
        sockFd_ = -1;
        return false;
    }

    connected_ = true;
    running_   = true;
    lastKeepalive_ = std::chrono::steady_clock::now();

    recvThread_ = std::thread(&AvantisAdapter::receiveLoop, this);

    spdlog::info("Avantis: connected to {}:{}", ip_, port_);
    if (onConnectionChange) onConnectionChange(true);
    return true;
}

void AvantisAdapter::disconnect() {
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

bool AvantisAdapter::isConnected() const {
    return connected_;
}

ConsoleCapabilities AvantisAdapter::capabilities() const {
    return {
        .model       = "Avantis",
        .firmware     = "",
        .channelCount = 64,
        .busCount     = 24,
        .matrixCount  = 0,
        .dcaCount     = 24,
        .fxSlots      = 12,
        .eqBands      = 4,
        .hasMotorizedFaders = true,
        .hasDynamicEq       = true,
        .hasMultibandComp   = false,
        .meterUpdateRateMs  = 50
    };
}

void AvantisAdapter::requestFullSync() {
    // A&H Avantis uses a binary TCP protocol
    // Request all channel parameters in bulk
    spdlog::info("Avantis: requesting full state sync");

    for (int ch = 1; ch <= 64; ch++) {
        // Request name
        sendCommand(0x01, buildSetParam(ch, paramToAvantisId(ChannelParam::Name), 0));
        // Request fader
        sendCommand(0x01, buildSetParam(ch, paramToAvantisId(ChannelParam::Fader), 0));
        // Request mute
        sendCommand(0x01, buildSetParam(ch, paramToAvantisId(ChannelParam::Mute), 0));
    }

    for (int bus = 1; bus <= 24; bus++) {
        sendCommand(0x01, buildSetParam(bus, 0x0100, 0));  // name
        sendCommand(0x01, buildSetParam(bus, 0x0101, 0));  // fader
    }
}

void AvantisAdapter::setChannelParam(int ch, ChannelParam param, float value) {
    uint16_t paramId = paramToAvantisId(param);
    sendCommand(0x02, buildSetParam(ch, paramId, value));
}

void AvantisAdapter::setChannelParam(int ch, ChannelParam param, bool value) {
    uint16_t paramId = paramToAvantisId(param);
    sendCommand(0x02, buildSetParam(ch, paramId, value ? 1.0f : 0.0f));
}

void AvantisAdapter::setChannelParam(int ch, ChannelParam param, const std::string& /*value*/) {
    // Avantis name setting uses a different message format
    if (param == ChannelParam::Name) {
        spdlog::warn("Avantis: name setting not yet implemented");
    }
}

void AvantisAdapter::setSendLevel(int ch, int bus, float value) {
    // Avantis: send level paramId = 0x0200 + bus offset
    uint16_t paramId = 0x0200 + static_cast<uint16_t>(bus - 1);
    sendCommand(0x02, buildSetParam(ch, paramId, value));
}

void AvantisAdapter::setBusParam(int bus, BusParam param, float value) {
    uint16_t paramId = 0;
    switch (param) {
        case BusParam::Fader: paramId = 0x0101; break;
        case BusParam::Pan:   paramId = 0x0103; break;
        default: return;
    }
    sendCommand(0x02, buildSetParam(bus, paramId, value));
}

void AvantisAdapter::subscribeMeter(int /*refreshMs*/) {
    metering_ = true;
    // Avantis: subscribe to meter data via command 0x10
    std::vector<uint8_t> payload = {0x01};  // subscribe flag
    sendCommand(0x10, payload);
}

void AvantisAdapter::unsubscribeMeter() {
    metering_ = false;
    std::vector<uint8_t> payload = {0x00};  // unsubscribe
    sendCommand(0x10, payload);
}

void AvantisAdapter::tick() {
    if (!connected_) return;
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastKeepalive_).count();
    if (ms > 5000) {
        sendKeepalive();
        lastKeepalive_ = now;
    }
}

// ── Private ──────────────────────────────────────────────────────────────

void AvantisAdapter::sendCommand(uint16_t msgType, const std::vector<uint8_t>& payload) {
    if (sockFd_ < 0) return;

    // A&H protocol: [length:2][msgType:2][payload:N]
    uint16_t totalLen = static_cast<uint16_t>(4 + payload.size());
    std::vector<uint8_t> msg(totalLen);

    uint16_t netLen  = htons(totalLen);
    uint16_t netType = htons(msgType);
    memcpy(msg.data(), &netLen, 2);
    memcpy(msg.data() + 2, &netType, 2);
    if (!payload.empty())
        memcpy(msg.data() + 4, payload.data(), payload.size());

    ::send(sockFd_, msg.data(), msg.size(), 0);
}

std::vector<uint8_t> AvantisAdapter::buildSetParam(int ch, uint16_t paramId, float value) {
    std::vector<uint8_t> payload(8);
    uint16_t netCh    = htons(static_cast<uint16_t>(ch));
    uint16_t netParam = htons(paramId);
    uint32_t bits;
    memcpy(&bits, &value, 4);
    bits = htonl(bits);

    memcpy(payload.data(), &netCh, 2);
    memcpy(payload.data() + 2, &netParam, 2);
    memcpy(payload.data() + 4, &bits, 4);
    return payload;
}

uint16_t AvantisAdapter::paramToAvantisId(ChannelParam param) const {
    // A&H parameter ID mapping (approximate)
    switch (param) {
        case ChannelParam::Fader:        return 0x0001;
        case ChannelParam::Mute:         return 0x0002;
        case ChannelParam::Pan:          return 0x0003;
        case ChannelParam::Name:         return 0x0004;
        case ChannelParam::Gain:         return 0x0010;
        case ChannelParam::PhantomPower: return 0x0011;
        case ChannelParam::PhaseInvert:  return 0x0012;
        case ChannelParam::HighPassFreq: return 0x0020;
        case ChannelParam::HighPassOn:   return 0x0021;
        case ChannelParam::EqOn:         return 0x0030;
        case ChannelParam::EqBand1Freq:  return 0x0031;
        case ChannelParam::EqBand1Gain:  return 0x0032;
        case ChannelParam::EqBand1Q:     return 0x0033;
        case ChannelParam::CompThreshold:return 0x0040;
        case ChannelParam::CompRatio:    return 0x0041;
        case ChannelParam::CompAttack:   return 0x0042;
        case ChannelParam::CompRelease:  return 0x0043;
        case ChannelParam::CompOn:       return 0x0044;
        case ChannelParam::GateThreshold:return 0x0050;
        case ChannelParam::GateOn:       return 0x0054;
        default:                         return 0xFFFF;
    }
}

void AvantisAdapter::receiveLoop() {
    uint8_t buf[4096];
    while (running_) {
        ssize_t n = ::recv(sockFd_, buf, sizeof(buf), 0);
        if (n > 0) {
            parseMessage(buf, n);
        } else if (n == 0) {
            spdlog::warn("Avantis: connection closed by remote");
            connected_ = false;
            break;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            spdlog::warn("Avantis: receive error: {}", strerror(errno));
            connected_ = false;
            break;
        }
    }
}

void AvantisAdapter::parseMessage(const uint8_t* data, size_t len) {
    // Parse A&H binary protocol responses
    if (len < 4) return;

    uint16_t netType;
    memcpy(&netType, data + 2, 2);
    uint16_t msgType = ntohs(netType);

    if (msgType == 0x02 && len >= 12) {
        // Parameter update response
        uint16_t netCh, netParam;
        memcpy(&netCh, data + 4, 2);
        memcpy(&netParam, data + 6, 2);
        int ch         = ntohs(netCh);
        uint16_t param = ntohs(netParam);

        uint32_t bits;
        memcpy(&bits, data + 8, 4);
        bits = ntohl(bits);
        float value;
        memcpy(&value, &bits, 4);

        ParameterUpdate update{};
        update.target = ParameterUpdate::Target::Channel;
        update.index  = ch;
        update.value  = value;

        // Reverse-map paramId to ChannelParam
        // (simplified — full mapping would cover all IDs)
        switch (param) {
            case 0x0001: update.param = ChannelParam::Fader; break;
            case 0x0002: update.param = ChannelParam::Mute;  break;
            case 0x0003: update.param = ChannelParam::Pan;   break;
            case 0x0010: update.param = ChannelParam::Gain;  break;
            default: return;
        }

        if (onParameterUpdate)
            onParameterUpdate(update);
    } else if (msgType == 0x10) {
        // Meter data
        size_t offset = 4;
        int ch = 1;
        while (offset + 4 <= len && ch <= 64) {
            uint32_t bits;
            memcpy(&bits, data + offset, 4);
            bits = ntohl(bits);
            float level;
            memcpy(&level, &bits, 4);
            offset += 4;

            float dbfs = (level > 0.0001f) ? 20.0f * log10f(level) : -96.0f;
            if (onMeterUpdate)
                onMeterUpdate(ch, dbfs, dbfs);
            ch++;
        }
    }
}

void AvantisAdapter::sendKeepalive() {
    sendCommand(0x00, {});  // Heartbeat
}
