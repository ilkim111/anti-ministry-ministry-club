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
                                         const std::string& userMessage,
                                         int maxTokensOverride)
{
    totalCalls_++;
    auto start = std::chrono::steady_clock::now();

    // Temporarily override maxTokens if requested
    int origMaxTokens = config_.maxTokens;
    if (maxTokensOverride > 0)
        config_.maxTokens = maxTokensOverride;

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
                spdlog::warn("Anthropic fallback failed: {}", e.what());
            }
        }

        if (!success && !config_.openaiApiKey.empty()) {
            try {
                response = callOpenAI(systemPrompt, userMessage);
                success = true;
            } catch (const std::exception& e) {
                spdlog::error("OpenAI fallback also failed: {}", e.what());
            }
        }
    } else {
        // Default: try Anthropic first, then OpenAI, then Ollama
        if (!config_.anthropicApiKey.empty()) {
            try {
                response = callAnthropic(systemPrompt, userMessage);
                success = true;
            } catch (const std::exception& e) {
                spdlog::warn("Anthropic call failed: {}", e.what());
            }
        }

        if (!success && !config_.openaiApiKey.empty()) {
            try {
                response = callOpenAI(systemPrompt, userMessage);
                success = true;
            } catch (const std::exception& e) {
                spdlog::warn("OpenAI fallback failed: {}", e.what());
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

    // Restore original maxTokens
    config_.maxTokens = origMaxTokens;

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
    cli.set_read_timeout(30, 0);  // 30s read timeout — LLM responses can be slow
    cli.enable_server_certificate_verification(false);  // macOS Homebrew OpenSSL lacks system CA bundle

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

    if (!res) {
        auto err = res.error();
        std::string errMsg;
        switch (err) {
            case httplib::Error::Connection:        errMsg = "connection failed"; break;
            case httplib::Error::ConnectionTimeout:  errMsg = "connection timeout"; break;
            case httplib::Error::Read:              errMsg = "read timeout"; break;
            case httplib::Error::Write:             errMsg = "write error"; break;
            case httplib::Error::SSLConnection:     errMsg = "SSL connection failed"; break;
            case httplib::Error::SSLServerVerification: errMsg = "SSL cert verification failed"; break;
            default:                                errMsg = "error code " + std::to_string(static_cast<int>(err)); break;
        }
        throw std::runtime_error("Anthropic request failed: " + errMsg);
    }
    if (res->status != 200) {
        throw std::runtime_error(
            "Anthropic API error " + std::to_string(res->status) +
            ": " + res->body.substr(0, 200));
    }

    auto j = nlohmann::json::parse(res->body);
    if (j.contains("content") && !j["content"].empty())
        return j["content"][0]["text"].get<std::string>();

    return res->body;
}

std::string LLMDecisionEngine::callOpenAI(
    const std::string& systemPrompt,
    const std::string& userMessage)
{
    httplib::Client cli("https://api.openai.com");
    cli.set_connection_timeout(config_.timeoutMs / 1000,
                                (config_.timeoutMs % 1000) * 1000);
    cli.set_read_timeout(30, 0);
    cli.enable_server_certificate_verification(false);

    nlohmann::json body = {
        {"model",       config_.openaiModel},
        {"max_tokens",  config_.maxTokens},
        {"temperature", config_.temperature},
        {"messages", {
            {{"role", "system"}, {"content", systemPrompt}},
            {{"role", "user"},   {"content", userMessage}}
        }}
    };

    httplib::Headers headers = {
        {"Authorization", "Bearer " + config_.openaiApiKey},
        {"content-type",  "application/json"}
    };

    auto res = cli.Post("/v1/chat/completions", headers,
                         body.dump(), "application/json");

    if (!res || res->status != 200) {
        int status = res ? res->status : 0;
        std::string errBody = res ? res->body : "no response";
        throw std::runtime_error(
            "OpenAI API error " + std::to_string(status) +
            ": " + errBody.substr(0, 200));
    }

    auto j = nlohmann::json::parse(res->body);
    if (j.contains("choices") && !j["choices"].empty())
        return j["choices"][0]["message"]["content"].get<std::string>();

    return res->body;
}

std::string LLMDecisionEngine::callOllama(
    const std::string& systemPrompt,
    const std::string& userMessage)
{
    // Parse host:port from ollamaHost
    std::string host = config_.ollamaHost;

    // Replace localhost with 127.0.0.1 to avoid IPv6 resolution issues on macOS
    {
        auto pos = host.find("://localhost");
        if (pos != std::string::npos) {
            host.replace(pos + 3, 9, "127.0.0.1");
            spdlog::debug("Ollama: resolved localhost -> {}", host);
        }
    }

    httplib::Client cli(host);
    cli.set_connection_timeout(10, 0);   // 10s — generous for localhost
    cli.set_read_timeout(120, 0);        // 120s — model cold-start can be very slow
    cli.set_write_timeout(10, 0);

    nlohmann::json body = {
        {"model",   config_.ollamaModel},
        {"stream",  false},
        {"format",  "json"},  // Force JSON output mode
        {"system",  systemPrompt},
        {"prompt",  userMessage},
        {"options", {
            {"temperature", config_.temperature},
            {"num_predict", config_.maxTokens}
        }}
    };

    spdlog::debug("Ollama: POST {} /api/generate model={}", host, config_.ollamaModel);
    auto res = cli.Post("/api/generate", body.dump(), "application/json");

    if (!res) {
        auto err = res.error();
        std::string errMsg;
        switch (err) {
            case httplib::Error::Connection:        errMsg = "connection failed"; break;
            case httplib::Error::ConnectionTimeout:  errMsg = "connection timeout"; break;
            case httplib::Error::Read:              errMsg = "read timeout"; break;
            case httplib::Error::Write:             errMsg = "write error"; break;
            default:                                errMsg = "error code " + std::to_string(static_cast<int>(err)); break;
        }
        throw std::runtime_error("Ollama request failed: " + errMsg +
            " (host=" + host + ")");
    }

    if (res->status != 200) {
        throw std::runtime_error(
            "Ollama API error " + std::to_string(res->status) +
            ": " + res->body.substr(0, 200));
    }

    auto j = nlohmann::json::parse(res->body);
    return j.value("response", "");
}

std::string LLMDecisionEngine::buildMixSystemPrompt() const {
    return R"(You are an expert live sound engineer AI assistant.
You are given the current state of a live mixing console and recent history.
Analyse the mix and suggest specific, safe adjustments.

CRITICAL OUTPUT FORMAT:
- You MUST respond with ONLY a valid JSON array. No explanations, no markdown, no text before or after.
- If no changes are needed, respond with: [{"action":"no_action","reason":"mix sounds good"}]
- NEVER respond with conversational text. ONLY output a JSON array starting with [ and ending with ].

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

GENRE PRESET:
- If "genre_preset" is present in the mix state, it contains target levels,
  EQ character, and dynamics hints for each instrument role in this genre.
- Use the "target_rms_relative" values as reference points — they indicate how
  loud each instrument should be relative to the main bus (in dB).
- Use "eq_character" hints to guide EQ decisions (e.g. "warm" = gentle high cut,
  "bright" = presence boost, "scooped" = cut mids).
- Use "dynamics_hint" to guide compression decisions (e.g. "punchy" = fast attack,
  "natural" = minimal compression, "controlled" = moderate ratio).
- These are starting points, not rigid rules. Adapt based on what actually
  sounds right in context.

ENGINEER PREFERENCES:
- If "engineer_preferences" is present, it reflects what this engineer typically
  approves vs rejects over time. Adapt your suggestions accordingly.
- If the overall approval rate is low, be more conservative.
- If "eq_tendency" says the engineer prefers cuts, favor subtractive EQ.
- If a role has a "warning" about frequent rejections, avoid touching it.
- If a role has a "preferred_fader_range", target that range.
- Preferences evolve — weight recent feedback more heavily.

AUDIO ANALYSIS:
- If "analysis_source" is "fft_audio", the mix state includes real FFT-based
  spectral analysis. Trust the "issues" array — it contains specific, actionable
  problems detected by DSP (boomy, harsh, thin, masking, clipping, feedback risk).
- If "analysis_source" is "console_meters", you only have level data from the
  console's built-in meters. Be less confident about spectral issues.

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

For set_eq: value=frequency_hz, value2=gain_db (NEGATIVE only — subtractive EQ, no boosting), value3=q_factor, band=1-4
  IMPORTANT: Always cut, never boost EQ. Use negative gain values only (e.g. -3, -6).
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

        nlohmann::json j;

        if (start != std::string::npos && end != std::string::npos && end > start) {
            // Found an array — extract and parse it
            std::string arrayStr = response.substr(start, end - start + 1);
            try {
                j = nlohmann::json::parse(arrayStr);
            } catch (...) {
                // Try to fix common issues: trailing commas
                // Remove trailing comma before ]
                std::string fixed = arrayStr;
                auto lastComma = fixed.rfind(',');
                auto lastBracket = fixed.rfind(']');
                if (lastComma != std::string::npos && lastBracket != std::string::npos &&
                    lastComma < lastBracket) {
                    // Check if only whitespace between comma and bracket
                    bool onlyWhitespace = true;
                    for (size_t i = lastComma + 1; i < lastBracket; i++) {
                        if (!isspace(fixed[i])) { onlyWhitespace = false; break; }
                    }
                    if (onlyWhitespace) {
                        fixed.erase(lastComma, 1);
                        j = nlohmann::json::parse(fixed);
                    }
                }
            }
        } else {
            // No array found — try parsing as single object and wrap in array
            try {
                j = nlohmann::json::parse(response);
                if (j.is_object()) {
                    // Check if it has an "actions" key containing an array
                    if (j.contains("actions") && j["actions"].is_array()) {
                        j = j["actions"];
                    } else if (j.contains("action")) {
                        // Single action object — wrap in array
                        j = nlohmann::json::array({j});
                    } else {
                        spdlog::warn("LLM response is not actionable: {}",
                                     response.substr(0, 300));
                        return actions;
                    }
                }
            } catch (...) {
                spdlog::warn("LLM response contains no JSON array: {}",
                             response.substr(0, 300));
                return actions;
            }
        }

        if (j.is_array()) {
            for (auto& item : j) {
                actions.push_back(MixAction::fromJson(item));
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse LLM actions: {}", e.what());
    }

    return actions;
}

float LLMDecisionEngine::avgLatencyMs() const {
    return totalCalls_ > 0 ? totalLatencyMs_ / totalCalls_ : 0;
}
