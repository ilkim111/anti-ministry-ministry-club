#include "llm/LLMDecisionEngine.hpp"
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <chrono>

LLMDecisionEngine::LLMDecisionEngine(const LLMConfig& config)
    : config_(config) {}

LLMDecisionEngine::~LLMDecisionEngine() = default;

std::vector<MixAction> LLMDecisionEngine::decideMixActions(
    const nlohmann::json& mixState,
    const nlohmann::json& sessionContext)
{
    nlohmann::json prompt = {
        {"mix_state",       mixState},
        {"recent_history",  sessionContext}
    };

    std::string systemPrompt = buildMixSystemPrompt();
    std::string response = callRaw(systemPrompt, prompt.dump());

    return parseActions(response);
}

std::string LLMDecisionEngine::callRaw(const std::string& systemPrompt,
                                         const std::string& userMessage)
{
    totalCalls_++;
    auto start = std::chrono::steady_clock::now();

    std::string response;
    bool success = false;

    if (config_.ollamaPrimary) {
        // Ollama-primary mode: try Ollama first, fall back to Anthropic
        try {
            response = callOllama(systemPrompt, userMessage);
            success = true;
        } catch (const std::exception& e) {
            spdlog::warn("Ollama call failed: {}", e.what());
        }

        if (!success && !config_.anthropicApiKey.empty()) {
            try {
                response = callAnthropic(systemPrompt, userMessage);
                success = true;
            } catch (const std::exception& e) {
                spdlog::error("Anthropic fallback also failed: {}", e.what());
            }
        }
    } else {
        // Default: try Anthropic first, fall back to Ollama
        if (!config_.anthropicApiKey.empty()) {
            try {
                response = callAnthropic(systemPrompt, userMessage);
                success = true;
            } catch (const std::exception& e) {
                spdlog::warn("Anthropic call failed: {}", e.what());
            }
        }

        if (!success && config_.useFallback) {
            try {
                response = callOllama(systemPrompt, userMessage);
                success = true;
            } catch (const std::exception& e) {
                spdlog::error("Ollama fallback also failed: {}", e.what());
            }
        }
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    totalLatencyMs_ += elapsed;

    if (!success) {
        failedCalls_++;
        spdlog::error("All LLM backends failed — returning empty response");
        return "{}";
    }

    spdlog::debug("LLM response in {}ms ({} chars)", elapsed, response.size());
    return response;
}

std::string LLMDecisionEngine::callAnthropic(
    const std::string& systemPrompt,
    const std::string& userMessage)
{
    httplib::Client cli("https://api.anthropic.com");
    cli.set_connection_timeout(config_.timeoutMs / 1000,
                                (config_.timeoutMs % 1000) * 1000);
    cli.set_read_timeout(config_.timeoutMs / 1000,
                          (config_.timeoutMs % 1000) * 1000);

    nlohmann::json body = {
        {"model",       config_.anthropicModel},
        {"max_tokens",  config_.maxTokens},
        {"temperature", config_.temperature},
        {"system",      systemPrompt},
        {"messages", {{
            {"role", "user"},
            {"content", userMessage}
        }}}
    };

    httplib::Headers headers = {
        {"x-api-key",         config_.anthropicApiKey},
        {"anthropic-version", "2023-06-01"},
        {"content-type",      "application/json"}
    };

    auto res = cli.Post("/v1/messages", headers,
                         body.dump(), "application/json");

    if (!res || res->status != 200) {
        int status = res ? res->status : 0;
        std::string errBody = res ? res->body : "no response";
        throw std::runtime_error(
            "Anthropic API error " + std::to_string(status) +
            ": " + errBody.substr(0, 200));
    }

    auto j = nlohmann::json::parse(res->body);
    if (j.contains("content") && !j["content"].empty())
        return j["content"][0]["text"].get<std::string>();

    return res->body;
}

std::string LLMDecisionEngine::callOllama(
    const std::string& systemPrompt,
    const std::string& userMessage)
{
    // Parse host:port from ollamaHost
    std::string host = config_.ollamaHost;

    httplib::Client cli(host);
    cli.set_connection_timeout(config_.timeoutMs / 1000,
                                (config_.timeoutMs % 1000) * 1000);
    cli.set_read_timeout(30, 0);  // Ollama can be slow

    nlohmann::json body = {
        {"model",   config_.ollamaModel},
        {"stream",  false},
        {"system",  systemPrompt},
        {"prompt",  userMessage},
        {"options", {
            {"temperature", config_.temperature},
            {"num_predict", config_.maxTokens}
        }}
    };

    auto res = cli.Post("/api/generate", body.dump(), "application/json");

    if (!res || res->status != 200) {
        int status = res ? res->status : 0;
        throw std::runtime_error(
            "Ollama API error " + std::to_string(status));
    }

    auto j = nlohmann::json::parse(res->body);
    return j.value("response", "");
}

std::string LLMDecisionEngine::buildMixSystemPrompt() const {
    return R"(You are an expert live sound engineer AI assistant.
You are given the current state of a live mixing console and recent history.
Analyse the mix and suggest specific, safe adjustments.

RULES:
- Never change faders by more than 6dB in a single step
- Never boost EQ by more than 3dB in a single step — cuts are safer than boosts
- For feedback risks, suggest CUTS, never boosts
- Always prioritize vocal clarity
- Lead vocals should sit 4-6dB above backing vocals in the mix
- If something sounds fine, respond with no_action
- Kick and bass should not mask each other — use HPF separation or EQ notching
- Be conservative — small changes that compound over time
- CRITICAL: If "engineer_instructions" are present in the mix state, those are
  direct instructions from the human engineer. Follow them. They take priority
  over your own analysis. If the engineer says "leave the drums alone", do not
  suggest any drum changes. If the engineer says "more vocals", prioritize that.

Respond with a JSON array of actions:
[
  {
    "action": "set_fader|set_pan|set_eq|set_comp|set_gate|set_hpf|set_send|mute|unmute|no_action|observation",
    "channel": 1,
    "role": "Kick",
    "value": 0.75,
    "value2": 0.0,
    "value3": 1.0,
    "band": 1,
    "aux": 0,
    "urgency": "immediate|fast|normal|low",
    "reason": "brief explanation"
  }
]

For set_eq: value=frequency_hz, value2=gain_db, value3=q_factor, band=1-6
For set_comp: value=threshold_db, value2=ratio
For set_hpf: value=frequency_hz
For set_fader: value=0.0-1.0 normalized)";
}

std::vector<MixAction> LLMDecisionEngine::parseActions(
    const std::string& response)
{
    std::vector<MixAction> actions;

    try {
        // Try to find JSON array in response
        auto start = response.find('[');
        auto end   = response.rfind(']');
        if (start == std::string::npos || end == std::string::npos) {
            spdlog::warn("LLM response contains no JSON array");
            return actions;
        }

        auto j = nlohmann::json::parse(
            response.substr(start, end - start + 1));

        for (auto& item : j) {
            actions.push_back(MixAction::fromJson(item));
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse LLM actions: {}", e.what());
    }

    return actions;
}

float LLMDecisionEngine::avgLatencyMs() const {
    return totalCalls_ > 0 ? totalLatencyMs_ / totalCalls_ : 0;
}
