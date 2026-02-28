#include "approval/ApprovalUI.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>
#include <mutex>

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

void ApprovalUI::setStatus(const std::string& status) {
    status_ = status;
}

void ApprovalUI::stop() {
    running_ = false;
}

void ApprovalUI::render() {
    // Single frame render for non-interactive mode
    auto pending = queue_.pending();

    auto screen = Screen::Create(Dimension::Full(), Dimension::Full());

    // Build approval list
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

    if (pendingElements.empty()) {
        pendingElements.push_back(
            text("No pending actions") | dim | center);
    }

    // Build log feed
    Elements logElements;
    int logStart = std::max(0, (int)logs_.size() - 15);
    for (int i = logStart; i < (int)logs_.size(); i++) {
        logElements.push_back(text(logs_[i]) | dim);
    }

    auto doc = vbox({
        // Header
        hbox({
            text("MixAgent") | bold | color(Color::Cyan),
            text(" | "),
            text(status_) | color(Color::Green),
            filler(),
            text("Pending: " + std::to_string(pending.size())) |
                color(pending.empty() ? Color::Green : Color::Yellow),
        }) | borderLight,

        // Approval queue
        vbox({
            text("Approval Queue") | bold | underlined,
            separator(),
            vbox(pendingElements),
        }) | border | flex,

        // Activity log
        vbox({
            text("Activity") | bold | underlined,
            separator(),
            vbox(logElements),
        }) | border | flex,

        // Controls
        hbox({
            text("[a]pprove  [r]eject  [A]pprove all  "
                 "[R]eject all  [q]uit") | dim,
        }) | borderLight,
    });

    Render(screen, doc);
    screen.Print();
}

void ApprovalUI::run() {
    running_ = true;

    auto screen = ScreenInteractive::Fullscreen();

    int selectedIndex = 0;

    auto renderer = Renderer([&] {
        auto pending = queue_.pending();

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

            bool selected = ((int)i == selectedIndex);
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

        Elements logElements;
        int logStart = std::max(0, (int)logs_.size() - 20);
        for (int i = logStart; i < (int)logs_.size(); i++)
            logElements.push_back(text("  " + logs_[i]) | dim);

        return vbox({
            hbox({
                text(" MixAgent ") | bold | color(Color::Cyan) | inverted,
                text(" "),
                text(status_) | color(Color::Green),
                filler(),
                text("Queue: " + std::to_string(pending.size()) + " "),
            }),
            separator(),
            vbox(pendingElements) | flex,
            separator(),
            vbox(logElements) | flex,
            separator(),
            hbox({
                text(" [Enter] approve  [d] reject  [A] all  [q] quit ") | dim,
            }),
        });
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        auto pending = queue_.pending();
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
