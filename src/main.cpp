#include "agent/SoundEngineerAgent.hpp"
#include "console/X32Adapter.hpp"
#include "console/WingAdapter.hpp"
#include "console/AvantisAdapter.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <fstream>
#include <memory>
#include <csignal>
#include <cstdlib>

static std::unique_ptr<SoundEngineerAgent> g_agent;

static void signalHandler(int sig) {
    spdlog::info("Received signal {} — shutting down", sig);
    if (g_agent) g_agent->stop();
}

static std::string getEnv(const std::string& key,
                           const std::string& defaultVal = "") {
    const char* val = std::getenv(key.c_str());
    return val ? val : defaultVal;
}

static void loadDotEnv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // Remove quotes
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            val = val.substr(1, val.size() - 2);
        setenv(key.c_str(), val.c_str(), 0);  // don't override existing
    }
}

int main(int argc, char* argv[]) {
    // Load .env file
    loadDotEnv(".env");

    // Setup logging
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto fileSink    = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "mixagent.log", 1048576 * 5, 3);  // 5MB, 3 files

    auto logger = std::make_shared<spdlog::logger>(
        "mixagent",
        spdlog::sinks_init_list{consoleSink, fileSink});
    spdlog::set_default_logger(logger);

    std::string logLevel = getEnv("MIXAGENT_LOG_LEVEL", "info");
    if (logLevel == "debug")      spdlog::set_level(spdlog::level::debug);
    else if (logLevel == "warn")  spdlog::set_level(spdlog::level::warn);
    else if (logLevel == "error") spdlog::set_level(spdlog::level::err);
    else                          spdlog::set_level(spdlog::level::info);

    spdlog::info("MixAgent v0.1.0 starting");

    // Load show config
    std::string configPath = "config/show.json";
    if (argc > 1) configPath = argv[1];

    nlohmann::json config;
    {
        std::ifstream f(configPath);
        if (!f.is_open()) {
            spdlog::error("Cannot open config file: {}", configPath);
            return 1;
        }
        f >> config;
    }

    spdlog::info("Loaded config: {}", configPath);

    // Create console adapter based on config
    std::string consoleType = config.value("console_type", "x32");
    std::string consoleIp   = config.value("console_ip", "192.168.1.100");
    int consolePort         = config.value("console_port", 0);

    std::unique_ptr<IConsoleAdapter> adapter;
    if (consoleType == "x32" || consoleType == "m32") {
        adapter = std::make_unique<X32Adapter>();
        if (consolePort == 0) consolePort = 10023;
    } else if (consoleType == "wing") {
        adapter = std::make_unique<WingAdapter>();
        if (consolePort == 0) consolePort = 2222;
    } else if (consoleType == "avantis") {
        adapter = std::make_unique<AvantisAdapter>();
        if (consolePort == 0) consolePort = 51325;
    } else {
        spdlog::error("Unknown console type: {}", consoleType);
        return 1;
    }

    spdlog::info("Console: {} at {}:{}", consoleType, consoleIp, consolePort);

    // Connect to console
    if (!adapter->connect(consoleIp, consolePort)) {
        spdlog::error("Failed to connect to console");
        return 1;
    }

    // LLM config
    LLMConfig llmConfig;
    llmConfig.anthropicApiKey = getEnv("ANTHROPIC_API_KEY");
    llmConfig.anthropicModel  = getEnv("MIXAGENT_MODEL",
                                        "claude-sonnet-4-20250514");
    llmConfig.ollamaHost      = getEnv("OLLAMA_HOST",
                                        "http://localhost:11434");
    llmConfig.ollamaModel     = getEnv("MIXAGENT_FALLBACK_MODEL",
                                        "llama3:8b");
    llmConfig.useFallback     = !llmConfig.ollamaHost.empty();
    llmConfig.ollamaPrimary   = config.value("ollama_primary", false);
    llmConfig.temperature     = config.value("llm_temperature", 0.3f);
    llmConfig.maxTokens       = config.value("llm_max_tokens", 1024);

    // Auto-detect: if no API key, switch to Ollama-primary
    if (llmConfig.anthropicApiKey.empty()) {
        llmConfig.ollamaPrimary = true;
        spdlog::info("No ANTHROPIC_API_KEY set — using Ollama as primary LLM");
    }

    if (llmConfig.ollamaPrimary) {
        spdlog::info("LLM mode: Ollama-primary ({})", llmConfig.ollamaModel);
    } else {
        spdlog::info("LLM mode: Anthropic-primary ({})", llmConfig.anthropicModel);
    }

    // Agent config
    AgentConfig agentConfig;
    agentConfig.dspIntervalMs  = config.value("dsp_interval_ms", 50);
    agentConfig.llmIntervalMs  = config.value("llm_interval_ms", 5000);
    agentConfig.meterRefreshMs = config.value("meter_refresh_ms", 50);
    agentConfig.headless       = config.value("headless", false);

    std::string approvalMode = config.value("approval_mode", "auto_urgent");
    if (approvalMode == "approve_all")
        agentConfig.approvalMode = ApprovalQueue::Mode::ApproveAll;
    else if (approvalMode == "auto_all")
        agentConfig.approvalMode = ApprovalQueue::Mode::AutoAll;
    else if (approvalMode == "deny_all")
        agentConfig.approvalMode = ApprovalQueue::Mode::DenyAll;
    else
        agentConfig.approvalMode = ApprovalQueue::Mode::AutoUrgent;

    // Setup signal handlers
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Create and start agent
    g_agent = std::make_unique<SoundEngineerAgent>(
        *adapter, llmConfig, agentConfig);

    if (!g_agent->start()) {
        spdlog::error("Failed to start agent");
        return 1;
    }

    spdlog::info("Agent running — press Ctrl+C to stop");

    // If headless, just block on signal
    if (agentConfig.headless) {
        while (g_agent->isRunning()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // Wait for agent to finish (UI will drive the loop in non-headless mode)
    while (g_agent->isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    g_agent.reset();
    adapter->disconnect();

    spdlog::info("MixAgent exited cleanly");
    return 0;
}
