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

// Pure-helper tests only: no TIS/CF/preferences/sleep/event calls.
// Including only the pure header proves the policy can compile and run
// without any macOS input/event framework.
#include "platform/OSXInputSourceSelection.h"

#include <gtest/gtest.h>

namespace inputleap {
namespace {

const std::string kSystem = "org.youknowone.inputmethod.Gureum.system";
const std::string kQwerty = "org.youknowone.inputmethod.Gureum.qwerty";
const std::string kDvorak = "org.youknowone.inputmethod.Gureum.dvorak";
const std::string kColemak = "org.youknowone.inputmethod.Gureum.colemak";
const std::string kHan2 = "org.youknowone.inputmethod.Gureum.han2";
const std::string kHan390 = "org.youknowone.inputmethod.Gureum.han390";
const std::string kHan3Final = "org.youknowone.inputmethod.Gureum.han3final";
const std::string kAbc = "com.apple.keylayout.ABC";
const std::string kAppleKorean = "com.apple.inputmethod.Korean.2SetKorean";

OSXInputSourceCandidate on(const std::string& id)
{
    return OSXInputSourceCandidate{id, true, true};
}

std::vector<OSXInputSourceCandidate> fullSet()
{
    return {on(kSystem), on(kQwerty), on(kAbc), on(kHan2), on(kAppleKorean)};
}

} // namespace

TEST(OSXInputSourceSelection, qwertyPreferenceBeatsSystemAndAbc)
{
    auto s = PickOSXInputSourceTarget(fullSet(), kHan2, kQwerty, kHan2, 1);
    EXPECT_EQ(kQwerty, s.m_targetId);
    EXPECT_NE(std::string::npos, s.m_reason.find("preference"));
    EXPECT_NE(std::string::npos, s.m_reason.find(kQwerty));
}

TEST(OSXInputSourceSelection, qwertyDisabledFallsBackWithDisabledReason)
{
    std::vector<OSXInputSourceCandidate> c = {
        on(kSystem), {kQwerty, false, true}, on(kAbc), on(kHan2),
    };
    auto s = PickOSXInputSourceTarget(c, kHan2, kQwerty, kHan2, 1);
    EXPECT_EQ(kSystem, s.m_targetId);
    EXPECT_NE(std::string::npos, s.m_reason.find("fallback"));
    EXPECT_NE(std::string::npos, s.m_reason.find("disabled"));
    EXPECT_NE(std::string::npos, s.m_reason.find(kQwerty));
}

TEST(OSXInputSourceSelection, qwertyDisabledAbcRemainsWhenSystemMissing)
{
    // Current behavior worth documenting: with qwerty disabled and no other
    // Roman fallback usable except ABC, ABC is still selected.
    std::vector<OSXInputSourceCandidate> c = {
        {kQwerty, false, true}, on(kAbc), on(kHan2),
    };
    auto s = PickOSXInputSourceTarget(c, kHan2, kQwerty, kHan2, 1);
    EXPECT_EQ(kAbc, s.m_targetId);
    EXPECT_NE(std::string::npos, s.m_reason.find("disabled"));
}

TEST(OSXInputSourceSelection, nonselectablePreferenceFallsBack)
{
    std::vector<OSXInputSourceCandidate> c = {
        on(kSystem), {kQwerty, true, false}, on(kAbc), on(kHan2),
    };
    auto s = PickOSXInputSourceTarget(c, kHan2, kQwerty, kHan2, 1);
    EXPECT_EQ(kSystem, s.m_targetId);
    EXPECT_NE(std::string::npos, s.m_reason.find("nonselectable"));
}

TEST(OSXInputSourceSelection, unregisteredPreferenceFallsBack)
{
    auto s = PickOSXInputSourceTarget(
        fullSet(), kHan2, "com.example.Roman", kHan2, 1);
    EXPECT_EQ(kSystem, s.m_targetId);
    EXPECT_NE(std::string::npos, s.m_reason.find("unregistered"));
}

TEST(OSXInputSourceSelection, wrongDirectionPreferenceFallsBack)
{
    // Roman slot holds a Hangul ID: rejected without activation.
    auto s = PickOSXInputSourceTarget(fullSet(), kHan2, kHan2, kHan2, 1);
    EXPECT_EQ(kSystem, s.m_targetId);
    EXPECT_NE(std::string::npos, s.m_reason.find("wrong-direction"));
    // Mirror: Hangul slot holds a Roman ID.
    auto t = PickOSXInputSourceTarget(fullSet(), kAbc, kQwerty, kQwerty, 1);
    EXPECT_EQ(kHan2, t.m_targetId);
    EXPECT_NE(std::string::npos, t.m_reason.find("wrong-direction"));
}

TEST(OSXInputSourceSelection, englishPreferencesSystemDvorakColemak)
{
    for (const auto& pref : {kSystem, kDvorak, kColemak}) {
        std::vector<OSXInputSourceCandidate> c = {
            on(kSystem), on(kQwerty), on(kAbc), on(pref), on(kHan2),
        };
        auto s = PickOSXInputSourceTarget(c, kHan2, pref, kHan2, 1);
        EXPECT_EQ(pref, s.m_targetId) << pref;
    }
}

TEST(OSXInputSourceSelection, nonHan2HangulPreferenceHonored)
{
    std::vector<OSXInputSourceCandidate> c = {
        on(kAbc), on(kHan2), on(kHan390), on(kHan3Final),
    };
    auto s = PickOSXInputSourceTarget(c, kAbc, kQwerty, kHan390, 1);
    EXPECT_EQ(kHan390, s.m_targetId);
    auto t = PickOSXInputSourceTarget(c, kAbc, kQwerty, kHan3Final, 1);
    EXPECT_EQ(kHan3Final, t.m_targetId);
}

TEST(OSXInputSourceSelection, AppleSourceIsNotAcceptedAsGureumPreference)
{
    auto s = PickOSXInputSourceTarget(
        fullSet(), kQwerty, kQwerty, kAppleKorean, 1);
    EXPECT_EQ(kHan2, s.m_targetId);
    EXPECT_NE(std::string::npos, s.m_reason.find("unregistered"));
}

TEST(OSXInputSourceSelection, gureumHangulPreferenceFromNonHangulCurrent)
{
    // Current is a Roman source, so the Hangul preference wins over the
    // historical han2-first fallback.
    std::vector<OSXInputSourceCandidate> c = {
        on(kQwerty), on(kAbc), on(kHan2), on(kHan390),
    };
    auto s = PickOSXInputSourceTarget(c, kQwerty, kQwerty, kHan390, 1);
    EXPECT_EQ(kHan390, s.m_targetId);
}

TEST(OSXInputSourceSelection, emptyPreferenceKeepsHistoricalFallback)
{
    auto s = PickOSXInputSourceTarget(fullSet(), kHan2, "", "", 1);
    EXPECT_EQ(kSystem, s.m_targetId);
    EXPECT_NE(std::string::npos, s.m_reason.find("pref-empty"));
    auto t = PickOSXInputSourceTarget(fullSet(), kAbc, "", "", 1);
    EXPECT_EQ(kHan2, t.m_targetId);
}

TEST(OSXInputSourceSelection, emptyCandidatesReturnsEmpty)
{
    auto s = PickOSXInputSourceTarget({}, kHan2, kQwerty, kHan2, 1);
    EXPECT_TRUE(s.m_targetId.empty());
    EXPECT_NE(std::string::npos, s.m_reason.find("empty"));
}

TEST(OSXInputSourceSelection, allDisabledReturnsEmpty)
{
    std::vector<OSXInputSourceCandidate> c = {
        {kQwerty, false, true}, {kHan2, true, false},
    };
    auto s = PickOSXInputSourceTarget(c, kHan2, kQwerty, kHan2, 1);
    EXPECT_TRUE(s.m_targetId.empty());
}

TEST(OSXInputSourceSelection, currentMissingCyclesFromStart)
{
    std::vector<OSXInputSourceCandidate> c = {on(kAbc), on(kHan2), on(kQwerty)};
    auto s = PickOSXInputSourceTarget(c, "com.example.Missing", "", "", -1);
    EXPECT_FALSE(s.m_targetId.empty());
    EXPECT_NE(std::string::npos, s.m_reason.find("current-missing"));
    auto fwd = PickOSXInputSourceTarget(
        c, "com.example.Missing", kQwerty, kHan2, 1);
    EXPECT_EQ(kHan2, fwd.m_targetId);
    EXPECT_NE(std::string::npos, fwd.m_reason.find("current-missing"));
}

TEST(OSXInputSourceSelection, duplicatesAreSafe)
{
    std::vector<OSXInputSourceCandidate> c = {
        on(kHan2), on(kQwerty), on(kQwerty), on(kSystem), on(kAbc),
    };
    auto s = PickOSXInputSourceTarget(c, kHan2, kQwerty, kHan2, 1);
    EXPECT_EQ(kQwerty, s.m_targetId);
}

TEST(OSXInputSourceSelection, sameReselectionIsDiagnosable)
{
    std::vector<OSXInputSourceCandidate> c = {on(kQwerty)};
    auto s = PickOSXInputSourceTarget(c, kQwerty, kHan2, kHan2, 1);
    EXPECT_EQ(kQwerty, s.m_targetId);
    EXPECT_NE(std::string::npos, s.m_reason.find("same"));
}

TEST(OSXInputSourceSelection, forwardAndReverseRegression)
{
    // Forward from Hangul prefers Roman; reverse preserves pure cycling.
    std::vector<OSXInputSourceCandidate> c = {on(kAbc), on(kHan2), on(kQwerty)};
    auto fwd = PickOSXInputSourceTarget(c, kHan2, "", "", 1);
    EXPECT_EQ(kSystem == fwd.m_targetId || kAbc == fwd.m_targetId, true);
    auto rev = PickOSXInputSourceTarget(c, kHan2, kQwerty, kHan2, -1);
    EXPECT_EQ(kAbc, rev.m_targetId);
    EXPECT_NE(std::string::npos, rev.m_reason.find("reverse-cycle"));
}

TEST(OSXInputSourceSelection, AppleSourcesSwitchBothDirections)
{
    const std::vector<OSXInputSourceCandidate> c = {
        on(kAbc), on(kAppleKorean),
    };

    auto toHangul = PickOSXInputSourceTarget(c, kAbc, "", "", 1);
    EXPECT_EQ(kAppleKorean, toHangul.m_targetId);
    EXPECT_NE(std::string::npos, toHangul.m_reason.find("hangul"));

    auto toRoman = PickOSXInputSourceTarget(c, kAppleKorean, "", "", 1);
    EXPECT_EQ(kAbc, toRoman.m_targetId);
    EXPECT_NE(std::string::npos, toRoman.m_reason.find("roman"));
}

TEST(OSXInputSourceSelection, exactMatchNotPrefix)
{
    EXPECT_TRUE(OSXInputSourceIsHangul(kHan2));
    EXPECT_TRUE(OSXInputSourceIsHangul(kHan390));
    EXPECT_TRUE(OSXInputSourceIsRoman(kQwerty));
    EXPECT_TRUE(OSXInputSourceIsRoman(kSystem));
    EXPECT_FALSE(OSXInputSourceIsHangul(kQwerty));
    EXPECT_FALSE(OSXInputSourceIsRoman(kHan2));
    EXPECT_FALSE(OSXInputSourceIsRegistered(
        "org.youknowone.inputmethod.Gureum.han3"));
    // Arbitrary prefix must not classify.
    EXPECT_FALSE(OSXInputSourceIsRegistered("org.youknowone.inputmethod.Gureum"));
    EXPECT_FALSE(
        OSXInputSourceIsRegistered("org.youknowone.inputmethod.Gureum.evil"));
}

TEST(OSXInputSourceSelection, pureHelperHasNoSideEffectsOnInputs)
{
    auto c = fullSet();
    auto copy = c;
    auto s1 = PickOSXInputSourceTarget(c, kHan2, kQwerty, kHan2, 1);
    auto s2 = PickOSXInputSourceTarget(c, kHan2, kQwerty, kHan2, 1);
    EXPECT_EQ(s1.m_targetId, s2.m_targetId);
    EXPECT_EQ(s1.m_reason, s2.m_reason);
    ASSERT_EQ(copy.size(), c.size());
    for (size_t i = 0; i < copy.size(); ++i) {
        EXPECT_EQ(copy[i].m_id, c[i].m_id);
    }
    // No OS state exists to mutate: determinism + input preservation is the
    // observable contract. This translation unit links without Carbon/TIS.
}

} // namespace inputleap
