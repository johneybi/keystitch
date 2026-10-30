/* InputLeap -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "platform/OSXInputSourceTransition.h"

#include <utility>

namespace inputleap {

void OSXInputSourceTransition::begin(std::string source, double now)
{
    before_ = std::move(source);
    candidate_.clear();
    started_ = changed_ = now;
}

OSXInputSourceTransition::Result OSXInputSourceTransition::observe(
    const std::string& source, double now)
{
    if (now - started_ >= timeout) return Result::TimedOut;
    if (source.empty() || source == before_) {
        candidate_.clear();
        changed_ = now;
    } else if (source != candidate_) {
        candidate_ = source;
        changed_ = now;
    } else if (now - changed_ >= stableInterval) {
        return Result::Changed;
    }
    return Result::Waiting;
}

} // namespace inputleap
