/* InputLeap -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <string>

namespace inputleap {

// Observes selection only; this is not an acknowledgement from the foreground
// IME. Callers serialize following keys while waiting and retain a hard limit.
class OSXInputSourceTransition {
public:
    enum class Result { Waiting, Changed, TimedOut };
    static constexpr double timeout = 0.300;
    static constexpr double stableInterval = 0.020;

    void begin(std::string source, double now);
    Result observe(const std::string& source, double now);
    const std::string& candidate() const { return candidate_; }

private:
    std::string before_;
    std::string candidate_;
    double started_ = 0;
    double changed_ = 0;
};

} // namespace inputleap
