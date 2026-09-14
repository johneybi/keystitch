/*
 * InputLeap -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2004 Chris Schoeneman
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "platform/OSXInputSourceSelection.h"

#include <map>

namespace inputleap {

OSXInputSourceSelection PickOSXHangulReselectTarget(
    const std::vector<OSXInputSourceCandidate>& candidates)
{
    for (const auto& candidate : candidates) {
        if (candidate.m_id == "org.youknowone.inputmethod.Gureum.han2" &&
            candidate.m_enabled && candidate.m_selectable) {
            return {candidate.m_id, "experiment-explicit-han2-reselection"};
        }
    }
    return {"", "experiment-han2-unavailable-no-fallback"};
}

const char* const kOSXAppleRomanABC = "com.apple.keylayout.ABC";
const char* const kOSXAppleKorean = "com.apple.inputmethod.Korean.2SetKorean";

namespace {

const char* const kGureumRomanIds[] = {
    "org.youknowone.inputmethod.Gureum.system",
    "org.youknowone.inputmethod.Gureum.qwerty",
    "org.youknowone.inputmethod.Gureum.dvorak",
    "org.youknowone.inputmethod.Gureum.colemak",
};

// Exact Gureum Hangul IDs from Gureum's input-source registration
// (keyboardIdentifier mapping). Do not add an ID here merely because it shares
// a prefix: a preference is trusted only when it names a registered Gureum
// mode.
const char* const kGureumHangulIds[] = {
    "org.youknowone.inputmethod.Gureum.han2",
    "org.youknowone.inputmethod.Gureum.han2classic",
    "org.youknowone.inputmethod.Gureum.han3-2011",
    "org.youknowone.inputmethod.Gureum.han3-2012",
    "org.youknowone.inputmethod.Gureum.han3classic",
    "org.youknowone.inputmethod.Gureum.han3final",
    "org.youknowone.inputmethod.Gureum.han3finalnoshift",
    "org.youknowone.inputmethod.Gureum.han3layout2",
    "org.youknowone.inputmethod.Gureum.han3noshift",
    "org.youknowone.inputmethod.Gureum.han390",
    "org.youknowone.inputmethod.Gureum.hanahnmatae",
    "org.youknowone.inputmethod.Gureum.hanroman",
};

const char* const kFallbackRomanIds[] = {
    "org.youknowone.inputmethod.Gureum.system",
    "com.apple.keylayout.ABC",
};

const char* const kFallbackHangulIds[] = {
    "org.youknowone.inputmethod.Gureum.han2",
    "com.apple.inputmethod.Korean.2SetKorean",
};

bool isIn(const std::string& id, const char* const* list, std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i) {
        if (id == list[i]) {
            return true;
        }
    }
    return false;
}

} // namespace

bool OSXInputSourceIsGureumRoman(const std::string& id)
{
    return isIn(id, kGureumRomanIds, sizeof(kGureumRomanIds) / sizeof(kGureumRomanIds[0]));
}

bool OSXInputSourceIsGureumHangul(const std::string& id)
{
    return isIn(id, kGureumHangulIds, sizeof(kGureumHangulIds) / sizeof(kGureumHangulIds[0]));
}

bool OSXInputSourceIsRoman(const std::string& id)
{
    if (id == kOSXAppleRomanABC) {
        return true;
    }
    return OSXInputSourceIsGureumRoman(id);
}

bool OSXInputSourceIsHangul(const std::string& id)
{
    if (id == kOSXAppleKorean) {
        return true;
    }
    return OSXInputSourceIsGureumHangul(id);
}

bool OSXInputSourceIsRegistered(const std::string& id)
{
    return OSXInputSourceIsRoman(id) || OSXInputSourceIsHangul(id);
}

OSXInputSourceSelection PickOSXInputSourceTarget(
    const std::vector<OSXInputSourceCandidate>& candidates,
    const std::string& currentId,
    const std::string& preferredRomanId,
    const std::string& preferredHangulId,
    std::int32_t offset)
{
    // Aggregate per-ID flags so duplicates are handled explicitly and safely:
    // an ID is usable when ANY entry is enabled && selectable. First-seen
    // order is preserved for the usable list.
    std::vector<std::string> order;
    std::map<std::string, bool> usable;
    std::map<std::string, bool> seen;
    std::map<std::string, bool> anyEnabled;
    std::map<std::string, bool> anySelectable;
    for (const auto& c : candidates) {
        if (c.m_id.empty()) {
            continue;
        }
        if (!seen[c.m_id]) {
            seen[c.m_id] = true;
            order.push_back(c.m_id);
            usable[c.m_id] = false;
            anyEnabled[c.m_id] = false;
            anySelectable[c.m_id] = false;
        }
        if (c.m_enabled) {
            anyEnabled[c.m_id] = true;
        }
        if (c.m_selectable) {
            anySelectable[c.m_id] = true;
        }
        if (c.m_enabled && c.m_selectable) {
            usable[c.m_id] = true;
        }
    }

    std::vector<std::string> usableList;
    for (const auto& id : order) {
        if (usable[id]) {
            usableList.push_back(id);
        }
    }

    if (usableList.empty()) {
        if (order.empty()) {
            return {"", "empty-candidates"};
        }
        return {"", "no-enabled-selectable-candidates"};
    }

    std::int32_t currentIndex = 0;
    bool currentFound = false;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(usableList.size()); ++i) {
        if (usableList[i] == currentId) {
            currentIndex = i;
            currentFound = true;
            break;
        }
    }

    const std::string currentSuffix = currentFound ? "" : ":current-missing";
    auto sameSuffix = [](const std::string& target, const std::string& current) -> std::string {
        return target == current ? ":same" : "";
    };

    if (offset == 1) {
        const bool wantRoman = OSXInputSourceIsHangul(currentId);
        const std::string& pref = wantRoman ? preferredRomanId : preferredHangulId;
        const std::string direction = wantRoman ? "roman" : "hangul";
        std::string prefProblem;

        if (pref.empty()) {
            prefProblem = "pref-empty";
        }
        else if (wantRoman && !OSXInputSourceIsGureumRoman(pref)) {
            prefProblem = OSXInputSourceIsGureumHangul(pref)
                ? "pref-wrong-direction:" + pref
                : "pref-unregistered:" + pref;
        }
        else if (!wantRoman && !OSXInputSourceIsGureumHangul(pref)) {
            prefProblem = OSXInputSourceIsGureumRoman(pref)
                ? "pref-wrong-direction:" + pref
                : "pref-unregistered:" + pref;
        }
        else if (!seen[pref]) {
            prefProblem = "pref-not-found:" + pref;
        }
        else if (!usable[pref]) {
            // Seen but no enabled && selectable entry. Call out disabled
            // explicitly so a disabled qwerty is diagnosable.
            if (!anyEnabled[pref]) {
                prefProblem = "pref-disabled:" + pref;
            }
            else {
                prefProblem = "pref-nonselectable:" + pref;
            }
        }
        else {
            return {pref, "forward-" + direction + "-preference:" + pref +
                             currentSuffix + sameSuffix(pref, currentId)};
        }

        // Preference unusable: keep the historical fallback order exactly,
        // with no automatic activation. Fallback lists are unchanged.
        const char* const* fallbackList = wantRoman ? kFallbackRomanIds : kFallbackHangulIds;
        const std::size_t fallbackCount = wantRoman
            ? sizeof(kFallbackRomanIds) / sizeof(kFallbackRomanIds[0])
            : sizeof(kFallbackHangulIds) / sizeof(kFallbackHangulIds[0]);
        for (std::size_t i = 0; i < fallbackCount; ++i) {
            const std::string fb = fallbackList[i];
            if (usable[fb]) {
                return {fb, "forward-" + direction + "-fallback:" + fb + ":" + prefProblem +
                                 currentSuffix + sameSuffix(fb, currentId)};
            }
        }

        // Neither preference nor fallback is usable: preserve the generic
        // forward cycle from the current position.
        const std::int32_t n = static_cast<std::int32_t>(usableList.size());
        const std::int32_t targetIndex =
            ((currentIndex + offset) % n + n) % n;
        const std::string target = usableList[targetIndex];
        std::string reason = "forward-" + direction + "-cycle:" + target + ":" + prefProblem;
        reason += currentSuffix + sameSuffix(target, currentId);
        return {target, reason};
    }

    // Reverse / general-offset cycling is preserved verbatim.
    const std::int32_t n = static_cast<std::int32_t>(usableList.size());
    const std::int32_t targetIndex =
        ((currentIndex + offset) % n + n) % n;
    const std::string target = usableList[targetIndex];
    std::string reason = (offset == -1 ? "reverse-cycle:" : "cycle:") + target;
    reason += currentSuffix + sameSuffix(target, currentId);
    return {target, reason};
}

} // namespace inputleap
