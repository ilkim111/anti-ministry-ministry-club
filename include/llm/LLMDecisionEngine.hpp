#pragma once
#include "ActionSchema.hpp"
#include "SessionMemory.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>

struct LLMConfig {
    std::string anthropicApiKey;
    std::string anthropicModel  = "claude-haiku-4-5-20251001";
    std::string openaiApiKey;
    std::string openaiModel     = "gpt-4o";
    std::string ollamaHost      = "http://localhost:11434";
    std::string ollamaModel     = "llama3:8b";
    bool        useFallback     = true;   // fall back to Ollama if Anthropic fails
    bool        ollamaPrimary   = false;  // use Ollama as primary (fully local mode)
    int         maxTokens       = 1024;
    float       temperature     = 0.3f;   // low temp for consistent decisions
    int         timeoutMs       = 5000;
};

class LLMDecisionEngine {
public:
    explicit LLMDecisionEngine(const LLMConfig& config);
    ~LLMDecisionEngine();

    // Main decision call — given mix state, returns actions
    std::vector<MixAction> decideMixActions(
        const nlohmann::json& mixState,
        const nlohmann::json& sessionContext);

    // Raw call for discovery review and other non-standard uses
    // maxTokensOverride: if > 0, overrides the configured maxTokens for this call
    std::string callRaw(const std::string& systemPrompt,
                        const std::string& userMessage,
                        int maxTokensOverride = 0);

    // Stats
    int totalCalls() const { return totalCalls_; }
    int failedCalls() const { return failedCalls_; }
    float avgLatencyMs() const;

private:
    std::string callAnthropic(const std::string& systemPrompt,
                               const std::string& userMessage);
    std::string callOpenAI(const std::string& systemPrompt,
                            const std::string& userMessage);
    std::string callOllama(const std::string& systemPrompt,
                            const std::string& userMessage);

    std::string buildMixSystemPrompt() const;
    std::vector<MixAction> parseActions(const std::string& response);

    LLMConfig config_;
    int  totalCalls_  = 0;
    int  failedCalls_ = 0;
    float totalLatencyMs_ = 0;
};
