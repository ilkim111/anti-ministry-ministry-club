#include "ui/TerminalUI.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

TerminalUI::TerminalUI(const Dependencies& deps)
    : deps_(deps) {}

TerminalUI::~TerminalUI() = default;

std::string TerminalUI::renderFrame() const {
    std::ostringstream out;

    out << "╔══════════════════════════════════════════════════════════════╗\n";
    out << "║  MixAgent — Live Channel Map                               ║\n";
    out << "╠══════════════════════════════════════════════════════════════╣\n";

    if (!deps_.channelMap) return out.str();

    for (auto& p : deps_.channelMap->all()) {
        if (p.consoleName.empty() && !p.fingerprint.hasSignal)
            continue;

        auto snap = deps_.model->channel(p.index);

        out << "║ " << std::setw(2) << p.index << " "
            << std::setw(12) << std::left
            << (p.consoleName.empty() ? "(unnamed)" : p.consoleName)
            << " " << std::setw(16) << std::left << roleToString(p.role)
            << " " << meterBar(snap.rmsDB, 20)
            << " " << faderBar(snap.fader, 8);

        if (p.stereoPair)
            out << " L/R=" << *p.stereoPair;

        out << " ║\n";
    }

    out << "╠══════════════════════════════════════════════════════════════╣\n";

    // LLM stats
    if (deps_.llm) {
        out << "║ LLM: " << deps_.llm->totalCalls() << " calls, "
            << deps_.llm->failedCalls() << " failed, "
            << std::fixed << std::setprecision(0)
            << deps_.llm->avgLatencyMs() << "ms avg";
        if (deps_.memory)
            out << " | Memory: " << deps_.memory->size() << " entries";
        out << std::string(20, ' ') << "║\n";
    }

    out << "╚══════════════════════════════════════════════════════════════╝\n";
    return out.str();
}

std::string TerminalUI::formatChannelStrip(int ch) const {
    auto p = deps_.channelMap->getProfile(ch);
    auto snap = deps_.model->channel(ch);

    std::ostringstream out;
    out << "ch" << std::setw(2) << ch << " "
        << std::setw(12) << std::left << p.consoleName
        << " [" << roleToString(p.role) << "]"
        << " fader=" << std::fixed << std::setprecision(2) << snap.fader
        << " rms=" << std::setprecision(1) << snap.rmsDB << "dB"
        << " peak=" << snap.peakDB << "dB";

    if (snap.muted) out << " MUTED";
    if (p.stereoPair) out << " pair=ch" << *p.stereoPair;

    return out.str();
}

std::string TerminalUI::formatMeterBridge() const {
    std::ostringstream out;
    for (auto& p : deps_.channelMap->active()) {
        auto snap = deps_.model->channel(p.index);
        out << std::setw(2) << p.index << ":"
            << meterBar(snap.rmsDB, 20) << " "
            << std::fixed << std::setprecision(0) << snap.rmsDB << "\n";
    }
    return out.str();
}

std::string TerminalUI::meterBar(float dbFS, int width) {
    // Map -96..0 dBFS to 0..width
    float norm = std::clamp((dbFS + 96.0f) / 96.0f, 0.0f, 1.0f);
    int filled = static_cast<int>(norm * width);

    std::string bar = "[";
    for (int i = 0; i < width; i++) {
        if (i < filled) {
            if (i > width * 0.9f)      bar += '#';  // red zone
            else if (i > width * 0.7f) bar += '=';  // yellow zone
            else                        bar += '-';  // green zone
        } else {
            bar += ' ';
        }
    }
    bar += "]";
    return bar;
}

std::string TerminalUI::faderBar(float norm, int width) {
    int pos = static_cast<int>(norm * width);
    std::string bar;
    for (int i = 0; i < width; i++)
        bar += (i == pos) ? '|' : '.';
    return bar;
}
