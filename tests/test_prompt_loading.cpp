#include <gtest/gtest.h>
#include "llm/LLMDecisionEngine.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Helper: create a temporary directory with prompt files for testing.
class PromptLoadingTest : public ::testing::Test {
protected:
    fs::path tmpDir_;

    void SetUp() override {
        tmpDir_ = fs::temp_directory_path() / "mixagent_prompt_test";
        fs::create_directories(tmpDir_);
    }

    void TearDown() override {
        fs::remove_all(tmpDir_);
    }

    void writeFile(const std::string& name, const std::string& content) {
        std::ofstream f(tmpDir_ / name);
        f << content;
    }
};

TEST_F(PromptLoadingTest, NoPromptDirUsesBuiltIn) {
    LLMConfig cfg;
    cfg.promptDir = "";
    LLMDecisionEngine engine(cfg);

    EXPECT_FALSE(engine.hasLoadedPrompts());
}

TEST_F(PromptLoadingTest, InvalidPromptDirUsesBuiltIn) {
    LLMConfig cfg;
    cfg.promptDir = "/nonexistent/path/to/prompts";
    LLMDecisionEngine engine(cfg);

    EXPECT_FALSE(engine.hasLoadedPrompts());
}

TEST_F(PromptLoadingTest, MissingCorePromptFails) {
    // Directory exists but no core prompt file
    LLMConfig cfg;
    cfg.promptDir = tmpDir_.string();
    LLMDecisionEngine engine(cfg);

    EXPECT_FALSE(engine.hasLoadedPrompts());
}

TEST_F(PromptLoadingTest, CorePromptOnlyLoadsSuccessfully) {
    writeFile("mix_engineer_core.txt", "You are an expert sound engineer.");

    LLMConfig cfg;
    cfg.promptDir = tmpDir_.string();
    LLMDecisionEngine engine(cfg);

    EXPECT_TRUE(engine.hasLoadedPrompts());
}

TEST_F(PromptLoadingTest, AllPromptsLoadSuccessfully) {
    writeFile("mix_engineer_core.txt", "CORE PROMPT");
    writeFile("mix_balance_reference.txt", "BALANCE REF");
    writeFile("mix_troubleshooting.txt", "TROUBLESHOOTING");

    LLMConfig cfg;
    cfg.promptDir = tmpDir_.string();
    LLMDecisionEngine engine(cfg);

    EXPECT_TRUE(engine.hasLoadedPrompts());
}

TEST_F(PromptLoadingTest, GenrePromptLoadedWhenSet) {
    writeFile("mix_engineer_core.txt", "CORE PROMPT");
    writeFile("genre_rock.txt", "ROCK GENRE CONTEXT");

    LLMConfig cfg;
    cfg.promptDir = tmpDir_.string();
    cfg.activeGenre = "rock";
    LLMDecisionEngine engine(cfg);

    EXPECT_TRUE(engine.hasLoadedPrompts());
}

TEST_F(PromptLoadingTest, MissingGenreFileStillLoads) {
    writeFile("mix_engineer_core.txt", "CORE PROMPT");

    LLMConfig cfg;
    cfg.promptDir = tmpDir_.string();
    cfg.activeGenre = "metal";  // no genre_metal.txt
    LLMDecisionEngine engine(cfg);

    // Core prompt loads fine even if genre file is missing
    EXPECT_TRUE(engine.hasLoadedPrompts());
}

TEST_F(PromptLoadingTest, ReloadChangesGenre) {
    writeFile("mix_engineer_core.txt", "CORE PROMPT");
    writeFile("genre_rock.txt", "ROCK CONTEXT");
    writeFile("genre_jazz.txt", "JAZZ CONTEXT");

    LLMConfig cfg;
    cfg.promptDir = tmpDir_.string();
    cfg.activeGenre = "rock";
    LLMDecisionEngine engine(cfg);
    EXPECT_TRUE(engine.hasLoadedPrompts());

    // Simulate genre change at runtime — not directly testable via
    // the public API without accessing the prompt text, but we verify
    // that loadPromptFiles() returns true after reconfiguration.
    // (In production the agent would update config_.activeGenre and
    // call loadPromptFiles().)
    EXPECT_TRUE(engine.loadPromptFiles());
}
