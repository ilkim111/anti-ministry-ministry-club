#include <gtest/gtest.h>
#include "console/X32Adapter.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <map>

// ── Mock X32 OSC Server ──────────────────────────────────────────────────
// Listens on UDP, stores parameter state, responds to OSC queries,
// and applies OSC set messages — mimicking a real X32 console.

class MockX32Server {
public:
    MockX32Server() {
        sockFd_ = socket(AF_INET, SOCK_DGRAM, 0);
        EXPECT_GE(sockFd_, 0);

        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(0);  // OS picks a free port
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        EXPECT_EQ(bind(sockFd_, (struct sockaddr*)&addr, sizeof(addr)), 0);

        // Read back assigned port
        socklen_t addrLen = sizeof(addr);
        getsockname(sockFd_, (struct sockaddr*)&addr, &addrLen);
        port_ = ntohs(addr.sin_port);

        // Set receive timeout
        struct timeval tv{};
        tv.tv_sec  = 0;
        tv.tv_usec = 50000;  // 50ms
        setsockopt(sockFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    ~MockX32Server() { stop(); }

    int port() const { return port_; }

    void start() {
        running_ = true;
        thread_ = std::thread(&MockX32Server::loop, this);
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        if (sockFd_ >= 0) { ::close(sockFd_); sockFd_ = -1; }
    }

    // Pre-set a parameter value (float) before the test runs
    void setFloat(const std::string& address, float value) {
        std::lock_guard<std::mutex> lock(mutex_);
        floats_[address] = value;
    }

    // Pre-set a parameter value (int) before the test runs
    void setInt(const std::string& address, int value) {
        std::lock_guard<std::mutex> lock(mutex_);
        ints_[address] = value;
    }

    // Read back a float value (what the mock currently holds)
    float getFloat(const std::string& address) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = floats_.find(address);
        return (it != floats_.end()) ? it->second : -999.0f;
    }

    // Read back an int value
    int getInt(const std::string& address) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = ints_.find(address);
        return (it != ints_.end()) ? it->second : -999;
    }

    // Wait until at least one message matching this address is received
    bool waitForMessage(const std::string& address, int timeoutMs = 2000) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (received_.count(address) > 0) return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

private:
    void loop() {
        uint8_t buf[4096];
        struct sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        while (running_) {
            ssize_t n = recvfrom(sockFd_, buf, sizeof(buf), 0,
                                  (struct sockaddr*)&clientAddr, &clientLen);
            if (n <= 0) continue;

            // Parse incoming OSC message
            std::string address(reinterpret_cast<const char*>(buf));
            size_t addrLen = address.size() + 1;
            while (addrLen % 4 != 0) addrLen++;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                received_[address]++;
            }

            if (addrLen >= (size_t)n) {
                // Query (no args) — respond with current value
                sendResponse(address, clientAddr);
                continue;
            }

            // Has args — this is a SET command
            if (buf[addrLen] != ',') continue;
            char typeTag = buf[addrLen + 1];
            size_t dataOffset = addrLen + 4;

            if (typeTag == 'f' && dataOffset + 4 <= (size_t)n) {
                uint32_t bits;
                memcpy(&bits, buf + dataOffset, 4);
                bits = ntohl(bits);
                float f;
                memcpy(&f, &bits, 4);
                std::lock_guard<std::mutex> lock(mutex_);
                floats_[address] = f;
                // Echo back (X32 does this)
                sendFloatResponse(address, f, clientAddr);
            } else if (typeTag == 'i' && dataOffset + 4 <= (size_t)n) {
                uint32_t bits;
                memcpy(&bits, buf + dataOffset, 4);
                int32_t i = static_cast<int32_t>(ntohl(bits));
                std::lock_guard<std::mutex> lock(mutex_);
                ints_[address] = i;
                sendIntResponse(address, i, clientAddr);
            }
        }
    }

    void sendResponse(const std::string& address,
                       const struct sockaddr_in& dest) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check floats first, then ints
        auto itf = floats_.find(address);
        if (itf != floats_.end()) {
            sendFloatResponse(address, itf->second, dest);
            return;
        }
        auto iti = ints_.find(address);
        if (iti != ints_.end()) {
            sendIntResponse(address, iti->second, dest);
            return;
        }

        // For /xinfo, just respond with a simple string
        if (address == "/xinfo") {
            // Minimal xinfo response — X32 sends back several string args
            // but we just need something to not time out
            std::vector<uint8_t> msg;
            for (char c : address) msg.push_back(c);
            msg.push_back(0);
            while (msg.size() % 4 != 0) msg.push_back(0);
            msg.push_back(','); msg.push_back('s');
            msg.push_back(0); msg.push_back(0);
            std::string val = "MockX32";
            for (char c : val) msg.push_back(c);
            msg.push_back(0);
            while (msg.size() % 4 != 0) msg.push_back(0);
            sendto(sockFd_, msg.data(), msg.size(), 0,
                   (const struct sockaddr*)&dest, sizeof(dest));
        }
        // Unknown address — ignore (like the real X32 would for unknown queries)
    }

    void sendFloatResponse(const std::string& address, float value,
                            const struct sockaddr_in& dest) {
        std::vector<uint8_t> msg;
        for (char c : address) msg.push_back(c);
        msg.push_back(0);
        while (msg.size() % 4 != 0) msg.push_back(0);

        msg.push_back(','); msg.push_back('f');
        msg.push_back(0); msg.push_back(0);

        uint32_t bits;
        memcpy(&bits, &value, 4);
        bits = htonl(bits);
        auto* p = reinterpret_cast<uint8_t*>(&bits);
        msg.insert(msg.end(), p, p + 4);

        sendto(sockFd_, msg.data(), msg.size(), 0,
               (const struct sockaddr*)&dest, sizeof(dest));
    }

    void sendIntResponse(const std::string& address, int value,
                          const struct sockaddr_in& dest) {
        std::vector<uint8_t> msg;
        for (char c : address) msg.push_back(c);
        msg.push_back(0);
        while (msg.size() % 4 != 0) msg.push_back(0);

        msg.push_back(','); msg.push_back('i');
        msg.push_back(0); msg.push_back(0);

        uint32_t val = htonl(static_cast<uint32_t>(value));
        auto* p = reinterpret_cast<uint8_t*>(&val);
        msg.insert(msg.end(), p, p + 4);

        sendto(sockFd_, msg.data(), msg.size(), 0,
               (const struct sockaddr*)&dest, sizeof(dest));
    }

    int sockFd_ = -1;
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::map<std::string, float> floats_;
    std::map<std::string, int>   ints_;
    std::map<std::string, int>   received_;  // message receive counts
};


// ── Test Fixture ─────────────────────────────────────────────────────────

class X32AdapterE2ETest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = std::make_unique<MockX32Server>();
        server_->start();

        adapter_ = std::make_unique<X32Adapter>();

        // Capture parameter updates from the adapter
        adapter_->onParameterUpdate = [this](const ParameterUpdate& update) {
            std::lock_guard<std::mutex> lock(updateMutex_);
            lastUpdates_[{update.index, update.param}] = update;
            updateCount_++;
            updateCv_.notify_all();
        };
    }

    void TearDown() override {
        adapter_->disconnect();
        server_->stop();
    }

    bool connectAdapter() {
        return adapter_->connect("127.0.0.1", server_->port());
    }

    // Wait for a specific parameter update to arrive from the adapter's receive loop
    bool waitForUpdate(int ch, ChannelParam param, int timeoutMs = 2000) {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
        std::unique_lock<std::mutex> lock(updateMutex_);
        while (true) {
            auto key = std::make_pair(ch, param);
            if (lastUpdates_.count(key) > 0) return true;
            if (std::chrono::steady_clock::now() >= deadline) return false;
            updateCv_.wait_until(lock, deadline);
        }
    }

    ParameterUpdate getUpdate(int ch, ChannelParam param) {
        std::lock_guard<std::mutex> lock(updateMutex_);
        return lastUpdates_.at({ch, param});
    }

    void clearUpdates() {
        std::lock_guard<std::mutex> lock(updateMutex_);
        lastUpdates_.clear();
        updateCount_ = 0;
    }

    std::unique_ptr<MockX32Server> server_;
    std::unique_ptr<X32Adapter>    adapter_;

    std::mutex updateMutex_;
    std::condition_variable updateCv_;
    std::map<std::pair<int, ChannelParam>, ParameterUpdate> lastUpdates_;
    int updateCount_ = 0;
};


// ── Tests ────────────────────────────────────────────────────────────────

TEST_F(X32AdapterE2ETest, ConnectAndDisconnect) {
    ASSERT_TRUE(connectAdapter());
    EXPECT_TRUE(adapter_->isConnected());

    // Should have sent /xinfo query
    EXPECT_TRUE(server_->waitForMessage("/xinfo"));

    adapter_->disconnect();
    EXPECT_FALSE(adapter_->isConnected());
}

TEST_F(X32AdapterE2ETest, SetFaderAndReadBack) {
    // Pre-set channel 1 fader at 0.5 on the mock
    server_->setFloat("/ch/01/mix/fader", 0.5f);

    ASSERT_TRUE(connectAdapter());

    // Query the initial value — the adapter will receive the response via its receive loop
    // (requestFullSync queries all channels, but we can also just wait for the specific one)
    // Trigger a query by requesting full sync
    adapter_->requestFullSync();

    // Wait for the fader update to come back
    ASSERT_TRUE(waitForUpdate(1, ChannelParam::Fader));
    auto initial = getUpdate(1, ChannelParam::Fader);
    EXPECT_NEAR(initial.floatVal(), 0.5f, 0.001f);

    // Now set fader to a new target value
    clearUpdates();
    float targetFader = 0.75f;
    adapter_->setChannelParam(1, ChannelParam::Fader, targetFader);

    // The mock echoes back set commands, so we should get an update
    ASSERT_TRUE(waitForUpdate(1, ChannelParam::Fader));
    auto updated = getUpdate(1, ChannelParam::Fader);
    EXPECT_NEAR(updated.floatVal(), targetFader, 0.001f);

    // Verify the mock server actually stored the new value
    EXPECT_NEAR(server_->getFloat("/ch/01/mix/fader"), targetFader, 0.001f);
}

TEST_F(X32AdapterE2ETest, MuteAndUnmuteChannel) {
    // X32: mix/on=1 means unmuted, mix/on=0 means muted
    // Start with channel 5 unmuted (mix/on=1)
    server_->setInt("/ch/05/mix/on", 1);

    ASSERT_TRUE(connectAdapter());
    adapter_->requestFullSync();

    // Wait for the mute state to arrive
    ASSERT_TRUE(waitForUpdate(5, ChannelParam::Mute));
    auto initial = getUpdate(5, ChannelParam::Mute);
    // mix/on=1 → unmuted → Mute param should report as "on" (true = not muted in X32 terms)
    // The adapter receives int 1, converts to bool true
    EXPECT_TRUE(initial.boolVal());  // mix/on=1 → bool true

    // Now MUTE the channel (Mute=true → adapter sends mix/on=0)
    clearUpdates();
    adapter_->setChannelParam(5, ChannelParam::Mute, true);

    // Verify the mock received mix/on=0 (muted)
    ASSERT_TRUE(server_->waitForMessage("/ch/05/mix/on"));
    // Give the echo response time to arrive
    ASSERT_TRUE(waitForUpdate(5, ChannelParam::Mute));

    // The mock should now hold 0 for mix/on
    EXPECT_EQ(server_->getInt("/ch/05/mix/on"), 0);

    // Now UNMUTE (Mute=false → adapter sends mix/on=1)
    clearUpdates();
    adapter_->setChannelParam(5, ChannelParam::Mute, false);

    ASSERT_TRUE(waitForUpdate(5, ChannelParam::Mute));
    EXPECT_EQ(server_->getInt("/ch/05/mix/on"), 1);
}

TEST_F(X32AdapterE2ETest, SetEqBand1GainAndReadBack) {
    // Start with EQ band 1 gain at 0.0 dB (normalized: 0.5 on X32)
    server_->setFloat("/ch/03/eq/1/g", 0.5f);

    ASSERT_TRUE(connectAdapter());
    adapter_->requestFullSync();

    // Wait for EQ gain update — adapter denormalizes: 0.5 → 0.0 dB
    ASSERT_TRUE(waitForUpdate(3, ChannelParam::EqBand1Gain));
    auto initial = getUpdate(3, ChannelParam::EqBand1Gain);
    EXPECT_NEAR(initial.floatVal(), 0.0f, 0.1f);  // 0dB

    // Set a new EQ gain in dB — adapter normalizes before sending
    clearUpdates();
    float newGainDb = 3.0f;  // +3 dB
    adapter_->setChannelParam(3, ChannelParam::EqBand1Gain, newGainDb);

    ASSERT_TRUE(waitForUpdate(3, ChannelParam::EqBand1Gain));
    auto updated = getUpdate(3, ChannelParam::EqBand1Gain);
    EXPECT_NEAR(updated.floatVal(), newGainDb, 0.5f);  // read back ~3dB

    // Server should hold normalized: (3+15)/30 = 0.6
    EXPECT_NEAR(server_->getFloat("/ch/03/eq/1/g"), 0.6f, 0.001f);
}

TEST_F(X32AdapterE2ETest, SetEqBand1FreqAndReadBack) {
    // Server holds normalized 0.3 → denormalized: 20 * 10^(0.3*3) ≈ 159 Hz
    server_->setFloat("/ch/02/eq/1/f", 0.3f);

    ASSERT_TRUE(connectAdapter());
    adapter_->requestFullSync();

    ASSERT_TRUE(waitForUpdate(2, ChannelParam::EqBand1Freq));
    auto initial = getUpdate(2, ChannelParam::EqBand1Freq);
    EXPECT_NEAR(initial.floatVal(), 159.0f, 5.0f);  // ~159 Hz

    // Set freq in Hz — adapter normalizes before sending
    clearUpdates();
    float newFreqHz = 1000.0f;
    adapter_->setChannelParam(2, ChannelParam::EqBand1Freq, newFreqHz);

    ASSERT_TRUE(waitForUpdate(2, ChannelParam::EqBand1Freq));
    // Server should hold normalized: log10(1000/20)/log10(1000) ≈ 0.566
    EXPECT_NEAR(server_->getFloat("/ch/02/eq/1/f"), 0.566f, 0.01f);
}

TEST_F(X32AdapterE2ETest, SetMultipleEqBands) {
    // Set initial values for all 4 EQ bands (normalized 0.5 = 0dB)
    server_->setFloat("/ch/01/eq/1/g", 0.5f);
    server_->setFloat("/ch/01/eq/2/g", 0.5f);
    server_->setFloat("/ch/01/eq/3/g", 0.5f);
    server_->setFloat("/ch/01/eq/4/g", 0.5f);

    ASSERT_TRUE(connectAdapter());
    adapter_->requestFullSync();

    ASSERT_TRUE(waitForUpdate(1, ChannelParam::EqBand1Gain));
    ASSERT_TRUE(waitForUpdate(1, ChannelParam::EqBand2Gain));
    ASSERT_TRUE(waitForUpdate(1, ChannelParam::EqBand3Gain));
    ASSERT_TRUE(waitForUpdate(1, ChannelParam::EqBand4Gain));

    // Adjust each band to different dB values
    clearUpdates();
    adapter_->setChannelParam(1, ChannelParam::EqBand1Gain, 3.0f);    // +3dB → norm 0.6
    adapter_->setChannelParam(1, ChannelParam::EqBand2Gain, -3.0f);   // -3dB → norm 0.4
    adapter_->setChannelParam(1, ChannelParam::EqBand3Gain, 6.0f);    // +6dB → norm 0.7
    adapter_->setChannelParam(1, ChannelParam::EqBand4Gain, -4.5f);   // -4.5dB → norm 0.35

    ASSERT_TRUE(waitForUpdate(1, ChannelParam::EqBand1Gain));
    ASSERT_TRUE(waitForUpdate(1, ChannelParam::EqBand2Gain));
    ASSERT_TRUE(waitForUpdate(1, ChannelParam::EqBand3Gain));
    ASSERT_TRUE(waitForUpdate(1, ChannelParam::EqBand4Gain));

    EXPECT_NEAR(server_->getFloat("/ch/01/eq/1/g"), 0.6f, 0.001f);
    EXPECT_NEAR(server_->getFloat("/ch/01/eq/2/g"), 0.4f, 0.001f);
    EXPECT_NEAR(server_->getFloat("/ch/01/eq/3/g"), 0.7f, 0.001f);
    EXPECT_NEAR(server_->getFloat("/ch/01/eq/4/g"), 0.35f, 0.001f);
}

TEST_F(X32AdapterE2ETest, FaderMovesToSpecificTargets) {
    // Start at 0.0 (fader all the way down)
    server_->setFloat("/ch/10/mix/fader", 0.0f);

    ASSERT_TRUE(connectAdapter());
    adapter_->requestFullSync();

    ASSERT_TRUE(waitForUpdate(10, ChannelParam::Fader));
    EXPECT_NEAR(getUpdate(10, ChannelParam::Fader).floatVal(), 0.0f, 0.001f);

    // Move to unity (0.75 on X32 is roughly 0 dB)
    clearUpdates();
    adapter_->setChannelParam(10, ChannelParam::Fader, 0.75f);
    ASSERT_TRUE(waitForUpdate(10, ChannelParam::Fader));
    EXPECT_NEAR(server_->getFloat("/ch/10/mix/fader"), 0.75f, 0.001f);

    // Move to max (1.0 = +10 dB)
    clearUpdates();
    adapter_->setChannelParam(10, ChannelParam::Fader, 1.0f);
    ASSERT_TRUE(waitForUpdate(10, ChannelParam::Fader));
    EXPECT_NEAR(server_->getFloat("/ch/10/mix/fader"), 1.0f, 0.001f);

    // Move back down to -inf (0.0)
    clearUpdates();
    adapter_->setChannelParam(10, ChannelParam::Fader, 0.0f);
    ASSERT_TRUE(waitForUpdate(10, ChannelParam::Fader));
    EXPECT_NEAR(server_->getFloat("/ch/10/mix/fader"), 0.0f, 0.001f);
}

TEST_F(X32AdapterE2ETest, SetPanAndReadBack) {
    server_->setFloat("/ch/07/mix/pan", 0.5f);  // center

    ASSERT_TRUE(connectAdapter());
    adapter_->requestFullSync();

    ASSERT_TRUE(waitForUpdate(7, ChannelParam::Pan));
    EXPECT_NEAR(getUpdate(7, ChannelParam::Pan).floatVal(), 0.5f, 0.001f);

    // Pan hard left
    clearUpdates();
    adapter_->setChannelParam(7, ChannelParam::Pan, 0.0f);
    ASSERT_TRUE(waitForUpdate(7, ChannelParam::Pan));
    EXPECT_NEAR(server_->getFloat("/ch/07/mix/pan"), 0.0f, 0.001f);

    // Pan hard right
    clearUpdates();
    adapter_->setChannelParam(7, ChannelParam::Pan, 1.0f);
    ASSERT_TRUE(waitForUpdate(7, ChannelParam::Pan));
    EXPECT_NEAR(server_->getFloat("/ch/07/mix/pan"), 1.0f, 0.001f);
}

TEST_F(X32AdapterE2ETest, SetCompressorThresholdAndRatio) {
    // Threshold: normalized 0.5 → denormalized: 0.5*60-60 = -30dB
    server_->setFloat("/ch/04/dyn/thr", 0.5f);
    // Ratio: enum, X32 sends int back. Pre-set as int 5 (= ratio 3.0:1)
    server_->setInt("/ch/04/dyn/ratio", 5);

    ASSERT_TRUE(connectAdapter());
    adapter_->requestFullSync();

    ASSERT_TRUE(waitForUpdate(4, ChannelParam::CompThreshold));
    // Threshold read back should be -30dB
    EXPECT_NEAR(getUpdate(4, ChannelParam::CompThreshold).floatVal(), -30.0f, 0.5f);

    // Set threshold in dB, ratio as float value
    clearUpdates();
    adapter_->setChannelParam(4, ChannelParam::CompThreshold, -39.0f);  // → norm (−39+60)/60 = 0.35
    adapter_->setChannelParam(4, ChannelParam::CompRatio, 4.0f);        // → enum index 6

    ASSERT_TRUE(waitForUpdate(4, ChannelParam::CompThreshold));

    // Server should hold normalized threshold
    EXPECT_NEAR(server_->getFloat("/ch/04/dyn/thr"), 0.35f, 0.001f);
    // Ratio sent as int enum — index 6 = ratio 4.0:1
    ASSERT_TRUE(server_->waitForMessage("/ch/04/dyn/ratio"));
    EXPECT_EQ(server_->getInt("/ch/04/dyn/ratio"), 6);
}

TEST_F(X32AdapterE2ETest, SetHighPassFilterFreq) {
    // Normalized 0.1 → Hz: 20 * 10^(0.1 * log10(20)) ≈ 20 * 10^0.1301 ≈ 27 Hz
    server_->setFloat("/ch/01/preamp/hpf", 0.1f);

    ASSERT_TRUE(connectAdapter());
    adapter_->requestFullSync();

    ASSERT_TRUE(waitForUpdate(1, ChannelParam::HighPassFreq));
    // Read back should be ~27 Hz
    EXPECT_NEAR(getUpdate(1, ChannelParam::HighPassFreq).floatVal(), 27.0f, 3.0f);

    // Set HPF in Hz — adapter normalizes before sending
    clearUpdates();
    float hpfHz = 100.0f;
    adapter_->setChannelParam(1, ChannelParam::HighPassFreq, hpfHz);

    ASSERT_TRUE(waitForUpdate(1, ChannelParam::HighPassFreq));
    // Server should hold normalized: log10(100/20)/log10(20) ≈ 0.699/1.301 ≈ 0.537
    EXPECT_NEAR(server_->getFloat("/ch/01/preamp/hpf"), 0.537f, 0.01f);
}

TEST_F(X32AdapterE2ETest, BusFaderSetAndReadBack) {
    server_->setFloat("/bus/03/mix/fader", 0.6f);

    ASSERT_TRUE(connectAdapter());
    adapter_->requestFullSync();

    // Bus updates use the same ChannelParam::Fader but with Target::Bus
    // We need to wait for a bus update — our fixture tracks by (index, param)
    // Bus 3 will come in as index=3
    ASSERT_TRUE(waitForUpdate(3, ChannelParam::Fader, 3000));

    clearUpdates();
    adapter_->setBusParam(3, BusParam::Fader, 0.8f);

    ASSERT_TRUE(waitForUpdate(3, ChannelParam::Fader));
    EXPECT_NEAR(server_->getFloat("/bus/03/mix/fader"), 0.8f, 0.001f);
}
