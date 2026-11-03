#pragma once
#include <array>
#include <algorithm>
#include <cmath>

// Pure scoring state: elapsed seconds come from a monotonic clock, not frame counts.
struct PianoLesson
{
    struct Note { int midi; double at; double duration; };
    static constexpr double hitWindow = 0.22;
    static constexpr double perfectWindow = 0.10;
    static constexpr int passScore = 980;
    static constexpr std::array<Note, 14> melody {{
        {60, 2.00, .55}, {60, 2.75, .55}, {67, 3.50, .55}, {67, 4.25, .55},
        {69, 5.00, .55}, {69, 5.75, .55}, {67, 6.50, 1.20},
        {65, 8.00, .55}, {65, 8.75, .55}, {64, 9.50, .55}, {64, 10.25, .55},
        {62, 11.00, .55}, {62, 11.75, .55}, {60, 12.50, 1.20}
    }};
    std::array<int, 14> results {}; // 0 pending, -1 missed, 70 good, 100 perfect
    int points = 0, misses = 0, mistakes = 0;
    bool running = false, complete = false;

    void start() { results = {}; points = misses = mistakes = 0; running = true; complete = false; }
    void advance (double seconds)
    {
        if (! running) return;
        for (size_t i = 0; i < melody.size(); ++i)
            if (results[i] == 0 && seconds > melody[i].at + hitWindow)
            { results[i] = -1; ++misses; }
        if (seconds >= melody.back().at + melody.back().duration)
        { running = false; complete = true; }
    }
    int hit (int index, double seconds)
    {
        advance (seconds);
        if (! running) return 0;
        if (index >= 0 && index < static_cast<int> (melody.size()) && results[index] == 0
            && std::abs (seconds - melody[index].at) <= hitWindow)
        {
            const int earned = std::abs (seconds - melody[index].at) <= perfectWindow ? 100 : 70;
            results[index] = earned;
            points += earned;
            return earned;
        }
        points -= 25;
        ++mistakes;
        return 0;
    }
    int score() const { return std::max (0, points); }
    bool passed() const { return complete && score() >= passScore; }
};
