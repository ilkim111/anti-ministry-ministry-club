#include "approval/ApprovalUI.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

using namespace ftxui;

ApprovalUI::ApprovalUI(ApprovalQueue& queue)
    : queue_(queue) {}

ApprovalUI::~ApprovalUI() {
    stop();
}

void ApprovalUI::addLog(const std::string& msg) {
    logs_.push_back(msg);
    if (logs_.size() > maxLogs_)
        logs_.erase(logs_.begin());
}

void ApprovalUI::addChatResponse(const std::string& msg) {
    std::lock_guard lock(chatMtx_);
    chatHistory_.push_back("agent> " + msg);
    if ((int)chatHistory_.size() > maxChatHistory_)
        chatHistory_.erase(chatHistory_.begin());
}

void ApprovalUI::setStatus(const std::string& status) {
    status_ = status;
}

void ApprovalUI::updateConnectionStatus(const ConnectionStatus& status) {
    std::lock_guard lock(connMtx_);
    connStatus_ = status;
}

void ApprovalUI::stop() {
    running_ = false;
}

void ApprovalUI::render() {
    // Single frame render for non-interactive / headless mode
    auto pending = queue_.pending();
    auto screen = Screen::Create(Dimension::Full(), Dimension::Full());

    Elements pendingElements;
    for (size_t i = 0; i < pending.size(); i++) {
        auto& qa = pending[i];
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - qa.queued).count();

        std::string urgStr;
        Color urgColor = Color::White;
        switch (qa.action.urgency) {
            case MixAction::Urgency::Immediate:
                urgStr = "IMMED"; urgColor = Color::Red; break;
            case MixAction::Urgency::Fast:
                urgStr = "FAST "; urgColor = Color::Yellow; break;
            case MixAction::Urgency::Normal:
                urgStr = "NORM "; urgColor = Color::Green; break;
            case MixAction::Urgency::Low:
                urgStr = "LOW  "; urgColor = Color::GrayDark; break;
        }

        pendingElements.push_back(
            hbox({
                text("[" + std::to_string(i + 1) + "] ") | bold,
                text(urgStr) | color(urgColor),
                text(" " + qa.action.describe()),
                text(" (" + std::to_string(elapsed) + "ms)") | dim,
            })
        );
    }

    if (pendingElements.empty())
        pendingElements.push_back(text("No pending actions") | dim | center);

    Elements logElements;
    int logStart = std::max(0, (int)logs_.size() - 15);
    for (int i = logStart; i < (int)logs_.size(); i++)
        logElements.push_back(text(logs_[i]) | dim);

    auto doc = vbox({
        hbox({
            text("MixAgent") | bold | color(Color::Cyan),
            text(" | "), text(status_) | color(Color::Green),
            filler(),
            text("Pending: " + std::to_string(pending.size())) |
                color(pending.empty() ? Color::Green : Color::Yellow),
        }) | borderLight,
        vbox({ text("Approval Queue") | bold | underlined, separator(),
               vbox(pendingElements), }) | border | flex,
        vbox({ text("Activity") | bold | underlined, separator(),
               vbox(logElements), }) | border | flex,
        hbox({ text("[a]pprove  [r]eject  [A]ll  [/]chat  [q]uit") | dim,
        }) | borderLight,
    });

    Render(screen, doc);
    screen.Print();
}

void ApprovalUI::run() {
    running_ = true;

    auto screen = ScreenInteractive::Fullscreen();

    int selectedIndex = 0;
    std::string chatInput;

    auto renderer = Renderer([&] {
        auto pending = queue_.pending();

        // ── Connection Status Bar ──────────────────────────────────
        ConnectionStatus cs;
        {
            std::lock_guard lock(connMtx_);
            cs = connStatus_;
        }

        auto statusDot = [](bool connected) {
            return connected
                ? text(" * ") | color(Color::Green) | bold
                : text(" * ") | color(Color::Red) | bold;
        };

        auto connBar = hbox({
            statusDot(cs.oscConnected),
            text("OSC") | (cs.oscConnected ? color(Color::Green) : color(Color::Red)),
            text(" " + cs.consoleType) | dim,
            text("  "),
            statusDot(cs.audioConnected),
            text("Audio") | (cs.audioConnected ? color(Color::Green) : color(Color::Red)),
            cs.audioConnected
                ? text(" " + cs.audioBackend + " " +
                       std::to_string(cs.audioChannels) + "ch/" +
                       std::to_string((int)cs.audioSampleRate) + "Hz") | dim
                : text(" off") | dim,
            text("  "),
            statusDot(cs.llmConnected),
            text("LLM") | (cs.llmConnected ? color(Color::Green) : color(Color::Red)),
        });

        // ── Header ────────────────────────────────────────────────
        std::string modeStr = (uiMode_ == UIMode::Chat) ? "CHAT" : "QUEUE";
        Color modeColor = (uiMode_ == UIMode::Chat) ? Color::Magenta : Color::Cyan;

        auto header = hbox({
            text(" MixAgent ") | bold | color(Color::Cyan) | inverted,
            text(" "),
            text(status_) | color(Color::Green),
            filler(),
            text("[" + modeStr + "]") | color(modeColor) | bold,
            text("  Queue: " + std::to_string(pending.size()) + " "),
        });

        // ── Approval Queue ────────────────────────────────────────
        Elements pendingElements;
        for (size_t i = 0; i < pending.size(); i++) {
            auto& qa = pending[i];
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - qa.queued).count();

            std::string urgStr;
            Color urgColor = Color::White;
            switch (qa.action.urgency) {
                case MixAction::Urgency::Immediate:
                    urgStr = "!!"; urgColor = Color::Red; break;
                case MixAction::Urgency::Fast:
                    urgStr = "! "; urgColor = Color::Yellow; break;
                case MixAction::Urgency::Normal:
                    urgStr = "  "; urgColor = Color::Green; break;
                case MixAction::Urgency::Low:
                    urgStr = "  "; urgColor = Color::GrayDark; break;
            }

            bool selected = ((int)i == selectedIndex) &&
                            uiMode_ == UIMode::Approval;
            auto line = hbox({
                text(selected ? "> " : "  "),
                text(urgStr) | color(urgColor),
                text(" " + qa.action.describe()),
                filler(),
                text(std::to_string(qa.timeoutMs - (int)elapsed) + "ms") | dim,
            });
            if (selected) line = line | inverted;
            pendingElements.push_back(line);
        }

        if (pendingElements.empty())
            pendingElements.push_back(text("  No pending actions") | dim);

        // ── Chat History ──────────────────────────────────────────
        Elements chatElements;
        {
            std::lock_guard lock(chatMtx_);
            int chatStart = std::max(0, (int)chatHistory_.size() - 10);
            for (int i = chatStart; i < (int)chatHistory_.size(); i++) {
                auto& line = chatHistory_[i];
                if (line.substr(0, 4) == "you>") {
                    chatElements.push_back(
                        text("  " + line) | color(Color::Yellow));
                } else {
                    chatElements.push_back(
                        text("  " + line) | color(Color::GrayLight));
                }
            }
        }
        if (chatElements.empty())
            chatElements.push_back(
                text("  Type / to chat with the agent") | dim);

        // ── Activity Log ──────────────────────────────────────────
        Elements logElements;
        int logStart = std::max(0, (int)logs_.size() - 10);
        for (int i = logStart; i < (int)logs_.size(); i++)
            logElements.push_back(text("  " + logs_[i]) | dim);

        // ── Chat input bar ────────────────────────────────────────
        Element inputBar;
        if (uiMode_ == UIMode::Chat) {
            inputBar = hbox({
                text(" > ") | bold | color(Color::Yellow),
                text(chatInput),
                text("_") | blink,
                filler(),
                text("[Enter] send  [Esc] back ") | dim,
            }) | borderLight | color(Color::Yellow);
        } else {
            inputBar = hbox({
                text(" [Enter] approve  [d] reject  [A] all  "
                     "[/] chat  [q] quit ") | dim,
            }) | borderLight;
        }

        // ── Layout: connection bar + left (queue + activity) + right (chat)
        return vbox({
            connBar | borderLight,
            header,
            separator(),
            hbox({
                // Left column: approval queue + activity log
                vbox({
                    vbox({
                        text(" Approval Queue") | bold,
                        separator(),
                        vbox(pendingElements),
                    }) | flex,
                    separator(),
                    vbox({
                        text(" Activity") | bold,
                        separator(),
                        vbox(logElements),
                    }) | flex,
                }) | flex | border,

                // Right column: chat
                vbox({
                    text(" Chat") | bold |
                        color(uiMode_ == UIMode::Chat
                              ? Color::Yellow : Color::White),
                    separator(),
                    vbox(chatElements) | flex,
                }) | size(WIDTH, EQUAL, 40) | border,
            }) | flex,
            inputBar,
        });
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        // ── Chat mode input handling ──────────────────────────────
        if (uiMode_ == UIMode::Chat) {
            if (event == Event::Escape) {
                uiMode_ = UIMode::Approval;
                return true;
            }
            if (event == Event::Return) {
                if (!chatInput.empty()) {
                    {
                        std::lock_guard lock(chatMtx_);
                        chatHistory_.push_back("you> " + chatInput);
                        if ((int)chatHistory_.size() > maxChatHistory_)
                            chatHistory_.erase(chatHistory_.begin());
                    }
                    // Fire callback to agent
                    if (onChatMessage)
                        onChatMessage(chatInput);
                    chatInput.clear();
                }
                return true;
            }
            if (event == Event::Backspace) {
                if (!chatInput.empty())
                    chatInput.pop_back();
                return true;
            }
            // Regular character input
            if (event.is_character()) {
                chatInput += event.character();
                return true;
            }
            return false;
        }

        // ── Approval mode input handling ──────────────────────────
        auto pending = queue_.pending();

        if (event == Event::Character('/')) {
            uiMode_ = UIMode::Chat;
            chatInput.clear();
            return true;
        }
        if (event == Event::ArrowUp || event == Event::Character('k')) {
            if (selectedIndex > 0) selectedIndex--;
            return true;
        }
        if (event == Event::ArrowDown || event == Event::Character('j')) {
            if (selectedIndex < (int)pending.size() - 1) selectedIndex++;
            return true;
        }
        if (event == Event::Return || event == Event::Character('a')) {
            if (!pending.empty())
                queue_.approve(selectedIndex);
            if (selectedIndex >= (int)pending.size() - 1 && selectedIndex > 0)
                selectedIndex--;
            return true;
        }
        if (event == Event::Character('d') || event == Event::Character('r')) {
            if (!pending.empty())
                queue_.reject(selectedIndex);
            if (selectedIndex >= (int)pending.size() - 1 && selectedIndex > 0)
                selectedIndex--;
            return true;
        }
        if (event == Event::Character('A')) {
            queue_.approveAll();
            selectedIndex = 0;
            return true;
        }
        if (event == Event::Character('R')) {
            queue_.rejectAll();
            selectedIndex = 0;
            return true;
        }
        if (event == Event::Character('q') || event == Event::Escape) {
            running_ = false;
            screen.Exit();
            return true;
        }
        return false;
    });

    screen.Loop(component);
}
