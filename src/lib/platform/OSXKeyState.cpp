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

#include "platform/OSXKeyState.h"
#include "platform/OSXInputSourceSelection.h"
#include "platform/OSXUchrKeyResource.h"
#include "platform/OSXMediaKeySupport.h"
#include "arch/Arch.h"
#include "base/Log.h"
#include "base/Time.h"

#include <Carbon/Carbon.h>
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <IOKit/hidsystem/IOHIDLib.h>

namespace inputleap {

static const std::uint32_t s_brightnessUp = 144;
static const std::uint32_t s_brightnessDown = 145;
static const std::uint32_t s_missionControlVK = 160;
static const std::uint32_t s_launchpadVK = 131;

static const std::uint32_t s_osxNumLock = 1 << 16;

static const std::uint32_t s_int4VK = 0x8a; // international4
static const std::uint32_t s_int5VK = 0x8b; // international5

template <typename Func>
static auto runOnMainThread(Func&& func) -> decltype(func())
{
    if (pthread_main_np()) {
        return func();
    }
    __block decltype(func()) result;
    dispatch_sync(dispatch_get_main_queue(), ^{
        result = func();
    });
    return result;
}

static std::string getInputSourceString(TISInputSourceRef source, CFStringRef property)
{
    if (source == nullptr) {
        return "<none>";
    }

    CFStringRef value = runOnMainThread([&]() {
        return static_cast<CFStringRef>(TISGetInputSourceProperty(source, property));
    });
    if (value == nullptr) {
        return "<unknown>";
    }

    char buffer[512];
    if (!CFStringGetCString(value, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        return "<unprintable>";
    }
    return buffer;
}

// A content-free snapshot for diagnosing input-source transitions. The
// selected input source, its input mode, and the active keyboard layout are
// separate pieces of state on macOS; a successful TISSelectInputSource call
// does not by itself prove that the foreground app's IMK session is ready.
static std::string getCurrentInputSourceState()
{
    TISInputSourceRef inputSource = runOnMainThread([]() {
        return TISCopyCurrentKeyboardInputSource();
    });
    TISInputSourceRef keyboardLayout = runOnMainThread([]() {
        return TISCopyCurrentKeyboardLayoutInputSource();
    });

    const std::string inputId = getInputSourceString(
        inputSource, kTISPropertyInputSourceID);
    const std::string inputMode = getInputSourceString(
        inputSource, kTISPropertyInputModeID);
    const std::string layoutId = getInputSourceString(
        keyboardLayout, kTISPropertyInputSourceID);

    if (inputSource != nullptr) {
        CFRelease(inputSource);
    }
    if (keyboardLayout != nullptr) {
        CFRelease(keyboardLayout);
    }

    return "input=" + inputId + " mode=" + inputMode + " layout=" + layoutId;
}

struct KeyEntry {
public:
    KeyID                m_keyID;
    std::uint32_t m_virtualKey;
};

// Read-only Gureum preference lookup. Never writes settings. A missing key,
// a non-string value, or an empty string yields "" and must not crash.
static std::string readGureumPreference(CFStringRef key)
{
    CFStringRef appId = CFSTR("org.youknowone.Gureum");
    CFPropertyListRef value = CFPreferencesCopyValue(
        key, appId, kCFPreferencesCurrentUser, kCFPreferencesAnyHost);
    if (value == nullptr) {
        return "";
    }
    std::string result;
    if (CFGetTypeID(value) == CFStringGetTypeID()) {
        char buffer[512];
        if (CFStringGetCString(static_cast<CFStringRef>(value), buffer,
                               sizeof(buffer), kCFStringEncodingUTF8)) {
            result = buffer;
        }
    }
    CFRelease(value);
    return result;
}
static const KeyEntry    s_controlKeys[] = {
    // cursor keys.  if we don't do this we'll may still get these from
    // the keyboard resource but they may not correspond to the arrow
    // keys.
    { kKeyLeft,        kVK_LeftArrow },
    { kKeyRight,        kVK_RightArrow },
    { kKeyUp,        kVK_UpArrow },
    { kKeyDown,        kVK_DownArrow },
    { kKeyHome,        kVK_Home },
    { kKeyEnd,        kVK_End },
    { kKeyPageUp,        kVK_PageUp },
    { kKeyPageDown,        kVK_PageDown },
    { kKeyInsert,        kVK_Help }, // Mac Keyboards have 'Help' on 'Insert'

    // function keys
    { kKeyF1,        kVK_F1 },
    { kKeyF2,        kVK_F2 },
    { kKeyF3,        kVK_F3 },
    { kKeyF4,        kVK_F4 },
    { kKeyF5,        kVK_F5 },
    { kKeyF6,        kVK_F6 },
    { kKeyF7,        kVK_F7 },
    { kKeyF8,        kVK_F8 },
    { kKeyF9,        kVK_F9 },
    { kKeyF10,        kVK_F10 },
    { kKeyF11,        kVK_F11 },
    { kKeyF12,        kVK_F12 },
    { kKeyF13,        kVK_F13 },
    { kKeyF14,        kVK_F14 },
    { kKeyF15,        kVK_F15 },
    { kKeyF16,        kVK_F16 },
    { kKeyF17,        kVK_F17 },
    { kKeyF18,        kVK_F18 },
    { kKeyF19,        kVK_F19 },
    { kKeyF20,        kVK_F20 },

    { kKeyKP_0,        kVK_ANSI_Keypad0 },
    { kKeyKP_1,        kVK_ANSI_Keypad1 },
    { kKeyKP_2,        kVK_ANSI_Keypad2 },
    { kKeyKP_3,        kVK_ANSI_Keypad3 },
    { kKeyKP_4,        kVK_ANSI_Keypad4 },
    { kKeyKP_5,        kVK_ANSI_Keypad5 },
    { kKeyKP_6,        kVK_ANSI_Keypad6 },
    { kKeyKP_7,        kVK_ANSI_Keypad7 },
    { kKeyKP_8,        kVK_ANSI_Keypad8 },
    { kKeyKP_9,        kVK_ANSI_Keypad9 },
    { kKeyKP_Decimal,    kVK_ANSI_KeypadDecimal },
    { kKeyKP_Equal,        kVK_ANSI_KeypadEquals },
    { kKeyKP_Multiply,    kVK_ANSI_KeypadMultiply },
    { kKeyKP_Add,        kVK_ANSI_KeypadPlus },
    { kKeyKP_Divide,    kVK_ANSI_KeypadDivide },
    { kKeyKP_Subtract,    kVK_ANSI_KeypadMinus },
    { kKeyKP_Enter,        kVK_ANSI_KeypadEnter },

    // virtual key 110 is fn+enter and i have no idea what that's supposed
    // to map to.  also the enter key with numlock on is a modifier but i
    // don't know which.

    // modifier keys. Support for right side keys is a later addition in macOS.
    { kKeyShift_L,                 kVK_Shift }, // 56
    { kKeyShift_R,            kVK_RightShift }, // 60
    { kKeyControl_L,             kVK_Control }, // 59
    { kKeyControl_R,        kVK_RightControl }, // 62
    { kKeyAlt_L,                  kVK_Option }, // 58
    { kKeyAlt_R,             kVK_RightOption }, // 61
    { kKeySuper_L,               kVK_Command }, // 55
    { kKeySuper_R,          kVK_RightCommand }, // 54
    { kKeyMeta_L,                kVK_Command }, // 55
    { kKeyMeta_R,           kVK_RightCommand }, // 54

    // toggle modifiers
    { kKeyNumLock,      kVK_ANSI_KeypadClear },
    { kKeyCapsLock,             kVK_CapsLock },

    { kKeyMissionControl, s_missionControlVK },
    { kKeyLaunchpad, s_launchpadVK },
    { kKeyBrightnessUp,  s_brightnessUp },
    { kKeyBrightnessDown, s_brightnessDown },

    // JIS keyboards only
    { kKeyEisuToggle, kVK_JIS_Eisu },
    { kKeyKana, kVK_JIS_Kana },
    { kKeyMuhenkan, s_int5VK },
};


//
// OSXKeyState
//

OSXKeyState::OSXKeyState(IEventQueue* events) :
    KeyState(events)
{
    init();
}

OSXKeyState::OSXKeyState(IEventQueue* events, inputleap::KeyMap& keyMap) :
    KeyState(events, keyMap)
{
    init();
}

OSXKeyState::~OSXKeyState()
{
}

void
OSXKeyState::init()
{
    m_deadKeyState = 0;
    m_pressedModifiers.clear();

    // build virtual key map
    for (size_t i = 0; i < sizeof(s_controlKeys) / sizeof(s_controlKeys[0]);
        ++i) {

        m_virtualKeyMap[s_controlKeys[i].m_virtualKey] =
            s_controlKeys[i].m_keyID;
    }
}

KeyModifierMask OSXKeyState::mapModifiersFromOSX(std::uint32_t mask) const
{
    KeyModifierMask outMask = 0;
    if ((mask & kCGEventFlagMaskShift) != 0) {
        outMask |= KeyModifierShift;
    }
    if ((mask & kCGEventFlagMaskControl) != 0) {
        outMask |= KeyModifierControl;
    }
    if ((mask & kCGEventFlagMaskAlternate) != 0) {
        outMask |= KeyModifierAlt;
    }
    if ((mask & kCGEventFlagMaskCommand) != 0) {
        outMask |= KeyModifierSuper;
    }
    if ((mask & kCGEventFlagMaskAlphaShift) != 0) {
        outMask |= KeyModifierCapsLock;
    }
    if ((mask & kCGEventFlagMaskNumericPad) != 0) {
        outMask |= KeyModifierNumLock;
    }

    LOG_DEBUG1("mask=%04x outMask=%04x", mask, outMask);
    return outMask;
}

KeyModifierMask OSXKeyState::mapModifiersToCarbon(std::uint32_t mask) const
{
    KeyModifierMask outMask = 0;
    if ((mask & kCGEventFlagMaskShift) != 0) {
        outMask |= shiftKey;
    }
    if ((mask & kCGEventFlagMaskControl) != 0) {
        outMask |= controlKey;
    }
    if ((mask & kCGEventFlagMaskCommand) != 0) {
        outMask |= cmdKey;
    }
    if ((mask & kCGEventFlagMaskAlternate) != 0) {
        outMask |= optionKey;
    }
    if ((mask & kCGEventFlagMaskAlphaShift) != 0) {
        outMask |= alphaLock;
    }
    if ((mask & kCGEventFlagMaskNumericPad) != 0) {
        outMask |= s_osxNumLock;
    }

    return outMask;
}

KeyButton
OSXKeyState::mapKeyFromEvent(KeyIDs& ids,
                KeyModifierMask* maskOut, CGEventRef event) const
{
    ids.clear();

    // map modifier key
    if (maskOut != nullptr) {
        KeyModifierMask activeMask = getActiveModifiers();
        activeMask &= ~KeyModifierAltGr;
        *maskOut    = activeMask;
    }

    // get virtual key
    std::uint32_t vkCode = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

    // handle up events
    std::uint32_t eventKind = CGEventGetType(event);
    if (eventKind == kCGEventKeyUp) {
        // the id isn't used.  we just need the same button we used on
        // the key press.  note that we don't use or reset the dead key
        // state;  up events should not affect the dead key state.
        ids.push_back(kKeyNone);
        return mapVirtualKeyToKeyButton(vkCode);
    }

    // check for special keys
    auto i = m_virtualKeyMap.find(vkCode);
    if (i != m_virtualKeyMap.end()) {
        m_deadKeyState = 0;
        ids.push_back(i->second);
        return mapVirtualKeyToKeyButton(vkCode);
    }

    // get keyboard info
    TISInputSourceRef currentKeyboardLayout = runOnMainThread([]() {
        return TISCopyCurrentKeyboardLayoutInputSource();
    });

    if (currentKeyboardLayout == nullptr) {
        return kKeyNone;
    }

    // get the event modifiers and remove the command and control
    // keys.  note if we used them though.
    // UCKeyTranslate expects old-style Carbon modifiers, so convert.
    std::uint32_t modifiers;
    modifiers = mapModifiersToCarbon(CGEventGetFlags(event));
    static const std::uint32_t s_commandModifiers =
        cmdKey | controlKey | rightControlKey;
    bool isCommand = ((modifiers & s_commandModifiers) != 0);
    modifiers &= ~s_commandModifiers;

    // if we've used a command key then we want the glyph produced without
    // the option key (i.e. the base glyph).
    //if (isCommand) {
        modifiers &= ~optionKey;
    //}

    // choose action
    std::uint16_t action;
    if (eventKind==kCGEventKeyDown) {
        action = kUCKeyActionDown;
    }
    else if (CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat)==1) {
        action = kUCKeyActionAutoKey;
    }
    else {
        return 0;
    }

    // translate via uchr resource
    CFDataRef ref = runOnMainThread([&]() {
        return (CFDataRef)TISGetInputSourceProperty(currentKeyboardLayout, kTISPropertyUnicodeKeyLayoutData);
    });
    const UCKeyboardLayout* layout = (const UCKeyboardLayout*) CFDataGetBytePtr(ref);
    const bool layoutValid = (layout != nullptr);

    if (layoutValid) {
        // translate key
        UniCharCount count;
        UniChar chars[2];
        LOG_DEBUG2("modifiers: %08x", modifiers & 0xffu);
        OSStatus status = UCKeyTranslate(layout,
                            vkCode & 0xffu, action,
                            (modifiers >> 8) & 0xffu,
                            LMGetKbdType(), 0, &m_deadKeyState,
                            sizeof(chars) / sizeof(chars[0]), &count, chars);

        // get the characters
        if (status == 0) {
            if (count != 0 || m_deadKeyState == 0) {
                m_deadKeyState = 0;
                for (UniCharCount i = 0; i < count; ++i) {
                    ids.push_back(IOSXKeyResource::unicharToKeyID(chars[i]));
                }
                adjustAltGrModifier(ids, maskOut, isCommand);
                return mapVirtualKeyToKeyButton(vkCode);
            }
            return 0;
        }
    }

    return 0;
}

bool
OSXKeyState::fakeCtrlAltDel()
{
    // pass keys through unchanged
    return false;
}

bool
OSXKeyState::fakeMediaKey(KeyID id)
{
    return fakeNativeMediaKey(id);;
}

CGEventFlags
OSXKeyState::getModifierStateAsOSXFlags()
{
    CGEventFlags modifiers = CGEventFlags(0);

    if (m_pressedModifiers.count(kVK_Shift) || m_pressedModifiers.count(kVK_RightShift)) {
        modifiers |= CGEventFlags(kCGEventFlagMaskShift);
    }

    if (m_pressedModifiers.count(kVK_Control) || m_pressedModifiers.count(kVK_RightControl)) {
        modifiers |= CGEventFlags(kCGEventFlagMaskControl);
    }

    if (m_pressedModifiers.count(kVK_Option) || m_pressedModifiers.count(kVK_RightOption)) {
        modifiers |= CGEventFlags(kCGEventFlagMaskAlternate);
    }

    if (m_pressedModifiers.count(kVK_Command) || m_pressedModifiers.count(kVK_RightCommand)) {
        modifiers |= CGEventFlags(kCGEventFlagMaskCommand);
    }

    if (m_pressedModifiers.count(kVK_CapsLock)) {
        modifiers |= CGEventFlags(kCGEventFlagMaskAlphaShift);
    }

    return modifiers;
}

KeyModifierMask
OSXKeyState::pollActiveModifiers() const
{
    // falsely assumed that the mask returned by GetCurrentKeyModifiers()
    // was the same as a CGEventFlags (which is what mapModifiersFromOSX
    // expects). patch by Marc
    std::uint32_t mask = GetCurrentKeyModifiers();
    KeyModifierMask outMask = 0;

    if ((mask & shiftKey) != 0) {
        outMask |= KeyModifierShift;
    }
    if ((mask & controlKey) != 0) {
        outMask |= KeyModifierControl;
    }
    if ((mask & optionKey) != 0) {
        outMask |= KeyModifierAlt;
    }
    if ((mask & cmdKey) != 0) {
        outMask |= KeyModifierSuper;
    }
    if ((mask & alphaLock) != 0) {
        outMask |= KeyModifierCapsLock;
    }
    if ((mask & s_osxNumLock) != 0) {
        outMask |= KeyModifierNumLock;
    }

    LOG_DEBUG1("mask=%04x outMask=%04x", mask, outMask);
    return outMask;
}

std::int32_t OSXKeyState::pollActiveGroup() const
{
    TISInputSourceRef inputSource = runOnMainThread([]() {
        return TISCopyCurrentKeyboardLayoutInputSource();
    });
    const std::string id = getInputSourceString(
        inputSource, kTISPropertyInputSourceID);

    auto i = m_groupMap.find(id);
    if (i != m_groupMap.end()) {
        LOG_DEBUG1("active macOS keyboard layout id=%s group=%d",
                   id.c_str(), i->second);
        CFRelease(inputSource);
        return i->second;
    }

    LOG_DEBUG1("can't map active macOS keyboard layout id=%s, use first group",
               id.c_str());
    if (inputSource != nullptr) {
        CFRelease(inputSource);
    }

    return 0;
}

void
OSXKeyState::pollPressedKeys(KeyButtonSet& pressedKeys) const
{
    ::KeyMap km;
    GetKeys(km);
    const std::uint8_t* m = reinterpret_cast<const std::uint8_t*>(km);
    for (std::uint32_t i = 0; i < 16; ++i) {
        for (std::uint32_t j = 0; j < 8; ++j) {
            if ((m[i] & (1u << j)) != 0) {
                pressedKeys.insert(mapVirtualKeyToKeyButton(8 * i + j));
            }
        }
    }
}

void
OSXKeyState::getKeyMap(inputleap::KeyMap& keyMap)
{
    // update keyboard groups
    if (getGroups(m_groups)) {
        m_groupMap.clear();
        std::int32_t numGroups = (std::int32_t)m_groups.size();
        for (std::int32_t g = 0; g < numGroups; ++g) {
            const std::string id = getInputSourceString(
                m_groups[g], kTISPropertyInputSourceID);
            m_groupMap[id] = g;
            LOG_DEBUG1("macOS input source group=%d id=%s name=%s",
                       g, id.c_str(),
                       getInputSourceString(
                           m_groups[g], kTISPropertyLocalizedName).c_str());
        }
    }

    std::uint32_t keyboardType = LMGetKbdType();
    for (std::int32_t g = 0, n = (std::int32_t)m_groups.size(); g < n; ++g) {
        // add special keys
        getKeyMapForSpecialKeys(keyMap, g);

        const void* resource;
        bool layoutValid = false;

        // add regular keys
        // try uchr resource first
        CFDataRef resourceRef = runOnMainThread([&]() {
            return (CFDataRef)TISGetInputSourceProperty(m_groups[g], kTISPropertyUnicodeKeyLayoutData);
        });

        layoutValid = resourceRef != nullptr;
        if (layoutValid)
            resource = CFDataGetBytePtr(resourceRef);

        if (layoutValid) {
            OSXUchrKeyResource uchr(resource, keyboardType);
            if (uchr.isValid()) {
                LOG_DEBUG1("using uchr resource for group %d", g);
                getKeyMap(keyMap, g, uchr);
                continue;
            }
        }

        LOG_DEBUG1("no keyboard resource for group %d", g);
    }
}

static io_connect_t getEventDriver(void)
{
    static mach_port_t sEventDrvrRef = 0;
    mach_port_t masterPort, service, iter;
    kern_return_t kr;

    if (!sEventDrvrRef) {
        // Get master device port
        kr = IOMasterPort(bootstrap_port, &masterPort);
        assert(KERN_SUCCESS == kr);

        kr = IOServiceGetMatchingServices(masterPort,
                IOServiceMatching(kIOHIDSystemClass), &iter);
        assert(KERN_SUCCESS == kr);

        service = IOIteratorNext(iter);
        assert(service);

        kr = IOServiceOpen(service, mach_task_self(),
                kIOHIDParamConnectType, &sEventDrvrRef);
        assert(KERN_SUCCESS == kr);

        IOObjectRelease(service);
        IOObjectRelease(iter);
    }

    return sEventDrvrRef;
}

static std::uint16_t mapVirtualKeyToCharCode(std::uint8_t vk, bool shift)
{
    switch (vk) {
    case kVK_ANSI_A: return shift ? 'A' : 'a';
    case kVK_ANSI_B: return shift ? 'B' : 'b';
    case kVK_ANSI_C: return shift ? 'C' : 'c';
    case kVK_ANSI_D: return shift ? 'D' : 'd';
    case kVK_ANSI_E: return shift ? 'E' : 'e';
    case kVK_ANSI_F: return shift ? 'F' : 'f';
    case kVK_ANSI_G: return shift ? 'G' : 'g';
    case kVK_ANSI_H: return shift ? 'H' : 'h';
    case kVK_ANSI_I: return shift ? 'I' : 'i';
    case kVK_ANSI_J: return shift ? 'J' : 'j';
    case kVK_ANSI_K: return shift ? 'K' : 'k';
    case kVK_ANSI_L: return shift ? 'L' : 'l';
    case kVK_ANSI_M: return shift ? 'M' : 'm';
    case kVK_ANSI_N: return shift ? 'N' : 'n';
    case kVK_ANSI_O: return shift ? 'O' : 'o';
    case kVK_ANSI_P: return shift ? 'P' : 'p';
    case kVK_ANSI_Q: return shift ? 'Q' : 'q';
    case kVK_ANSI_R: return shift ? 'R' : 'r';
    case kVK_ANSI_S: return shift ? 'S' : 's';
    case kVK_ANSI_T: return shift ? 'T' : 't';
    case kVK_ANSI_U: return shift ? 'U' : 'u';
    case kVK_ANSI_V: return shift ? 'V' : 'v';
    case kVK_ANSI_W: return shift ? 'W' : 'w';
    case kVK_ANSI_X: return shift ? 'X' : 'x';
    case kVK_ANSI_Y: return shift ? 'Y' : 'y';
    case kVK_ANSI_Z: return shift ? 'Z' : 'z';

    case kVK_ANSI_1: return shift ? '!' : '1';
    case kVK_ANSI_2: return shift ? '@' : '2';
    case kVK_ANSI_3: return shift ? '#' : '3';
    case kVK_ANSI_4: return shift ? '$' : '4';
    case kVK_ANSI_5: return shift ? '%' : '5';
    case kVK_ANSI_6: return shift ? '^' : '6';
    case kVK_ANSI_7: return shift ? '&' : '7';
    case kVK_ANSI_8: return shift ? '*' : '8';
    case kVK_ANSI_9: return shift ? '(' : '9';
    case kVK_ANSI_0: return shift ? ')' : '0';

    case kVK_ANSI_Equal: return shift ? '+' : '=';
    case kVK_ANSI_Minus: return shift ? '_' : '-';
    case kVK_ANSI_RightBracket: return shift ? '}' : ']';
    case kVK_ANSI_LeftBracket: return shift ? '{' : '[';
    case kVK_ANSI_Quote: return shift ? '"' : '\'';
    case kVK_ANSI_Semicolon: return shift ? ':' : ';';
    case kVK_ANSI_Backslash: return shift ? '|' : '\\';
    case kVK_ANSI_Comma: return shift ? '<' : ',';
    case kVK_ANSI_Slash: return shift ? '?' : '/';
    case kVK_ANSI_Period: return shift ? '>' : '.';
    case kVK_ANSI_Grave: return shift ? '~' : '`';
    case kVK_Space: return ' ';
    case kVK_Return: return '\r';
    case kVK_Tab: return '\t';
    case kVK_Delete: return '\b';
    default: return 0;
    }
}

void OSXKeyState::postHIDVirtualKey(const std::uint8_t virtualKeyCode, const bool postDown)
{
    switch (virtualKeyCode)
    {
    case kVK_Shift:
    case kVK_RightShift:
    case kVK_Command:
    case kVK_RightCommand:
    case kVK_Option:
    case kVK_RightOption:
    case kVK_Control:
    case kVK_RightControl:
    case kVK_CapsLock:
        if (postDown) {
            m_pressedModifiers.insert(virtualKeyCode);
        }
        else {
            m_pressedModifiers.erase(virtualKeyCode);
        }
        break;
    }

    postKeyboardEvent(virtualKeyCode, postDown, getModifierStateAsOSXFlags());
}

void OSXKeyState::postKeyboardEvent(std::uint8_t virtualKeyCode, bool postDown,
                                   CGEventFlags flags)
{
    CGEventRef quartzEvent = CGEventCreateKeyboardEvent(nullptr, virtualKeyCode, postDown);
    if (quartzEvent != nullptr) {
        CGEventSetFlags(quartzEvent, flags);
        CGEventPost(kCGHIDEventTap, quartzEvent);
        CFRelease(quartzEvent);
    }
}

void OSXKeyState::postBackspace(int count)
{
    for (int i = 0; i < count; ++i) {
        postHIDVirtualKey(kVK_Delete, true);
        postHIDVirtualKey(kVK_Delete, false);
    }
}

std::string OSXKeyState::describeInputSourceState() const
{
    return getCurrentInputSourceState();
}

void OSXKeyState::waitForInputSourceSettling()
{
    // Preserve the historical interval. This is not an IME acknowledgement.
    this_thread_sleep(0.075);
}

void OSXKeyState::postUnicodeString(const std::u16string& str)
{
    for (char16_t ch : str) {
        UniChar u = static_cast<UniChar>(ch);
        CGEventRef down = CGEventCreateKeyboardEvent(nullptr, 0, true);
        if (down != nullptr) {
            CGEventKeyboardSetUnicodeString(down, 1, &u);
            CGEventPost(kCGHIDEventTap, down);
            CFRelease(down);
        }
        CGEventRef up = CGEventCreateKeyboardEvent(nullptr, 0, false);
        if (up != nullptr) {
            CGEventKeyboardSetUnicodeString(up, 1, &u);
            CGEventPost(kCGHIDEventTap, up);
            CFRelease(up);
        }
    }
}

void
OSXKeyState::fakeKey(const Keystroke& keystroke)
{
    switch (keystroke.m_type) {
    case Keystroke::kButton: {

        KeyButton button = keystroke.m_data.m_button.m_button;
        bool keyDown = keystroke.m_data.m_button.m_press;
        bool repeat = keystroke.m_data.m_button.m_repeat;
        CGKeyCode virtualKey = mapKeyButtonToVirtualKey(button);

        LOG_DEBUG1(
            "  button=0x%04x virtualKey=0x%04x keyDown=%s",
            button, virtualKey, keyDown ? "down" : "up");

        // F19 is the semantic target for the Windows Right Alt tap. Select
        // the opposite language family through the generic policy below:
        // Hangul -> Roman and Roman -> Hangul. Do not hard-code a Gureum mode
        // here; Apple Korean/ABC are the supported baseline and Gureum is an
        // optional compatible target.
        if (virtualKey == kVK_F19) {
            if (keyDown && !repeat) {
                LOG_INFO("F19 transition begin modifiers=0x%08x pressedModifiers=%zu state=%s",
                         static_cast<unsigned int>(getModifierStateAsOSXFlags()),
                         m_pressedModifiers.size(),
                         describeInputSourceState().c_str());
                if (cycleInputSource(1)) {
                    LOG_INFO("F19 transition selected bidirectional source state=%s (foreground IME state unverified)",
                             describeInputSourceState().c_str());
                    waitForInputSourceSettling();
                }
                else {
                    LOG_WARN("F19 transition failed state=%s", describeInputSourceState().c_str());
                }
            }
            break;
        }

        postHIDVirtualKey(virtualKey, keyDown);

        break;
    }

    case Keystroke::kGroup: {
        std::int32_t group = keystroke.m_data.m_group.m_group;
        if (keystroke.m_data.m_group.m_absolute) {
            LOG_DEBUG1("  keyboard layout override group=%d restore=%s", group,
                       keystroke.m_data.m_group.m_restore ? "yes" : "no");
            setGroup(group);
        }
        else {
            LOG_DEBUG1("  input source %+d", group);
            cycleInputSource(group);
        }
        break;
    }
    }
}

void OSXKeyState::getKeyMapForSpecialKeys(inputleap::KeyMap& keyMap, std::int32_t group) const
{
    // special keys are insensitive to modifers and none are dead keys
    inputleap::KeyMap::KeyItem item;
    for (size_t i = 0; i < sizeof(s_controlKeys) /
                                sizeof(s_controlKeys[0]); ++i) {
        const KeyEntry& entry = s_controlKeys[i];
        item.m_id        = entry.m_keyID;
        item.m_group     = group;
        item.m_button    = mapVirtualKeyToKeyButton(entry.m_virtualKey);
        item.m_required  = 0;
        item.m_sensitive = 0;
        item.m_dead      = false;
        item.m_client    = 0;
        inputleap::KeyMap::initModifierKey(item);
        keyMap.addKeyEntry(item);

        if (item.m_lock) {
            // all locking keys are half duplex on OS X
            keyMap.addHalfDuplexButton(item.m_button);
        }
    }

    // note:  we don't special case the number pad keys.  querying the
    // mac keyboard returns the non-keypad version of those keys but
    // a KeyState always provides a mapping from keypad keys to
    // non-keypad keys so we'll be able to generate the characters
    // anyway.
}

bool OSXKeyState::getKeyMap(inputleap::KeyMap& keyMap, std::int32_t group,
                            const IOSXKeyResource& r) const
{
    if (!r.isValid()) {
        return false;
    }

    // space for all possible modifier combinations
    std::vector<bool> modifiers(r.getNumModifierCombinations());

    // make space for the keys that any single button can synthesize
    std::vector<std::pair<KeyID, bool> > buttonKeys(r.getNumTables());

    // iterate over each button
    inputleap::KeyMap::KeyItem item;
    for (std::uint32_t i = 0; i < r.getNumButtons(); ++i) {
        item.m_button = mapVirtualKeyToKeyButton(i);

        // the KeyIDs we've already handled
        std::set<KeyID> keys;

        // convert the entry in each table for this button to a KeyID
        for (std::uint32_t j = 0; j < r.getNumTables(); ++j) {
            buttonKeys[j].first  = r.getKey(j, i);
            buttonKeys[j].second = inputleap::KeyMap::isDeadKey(buttonKeys[j].first);
        }

        // iterate over each character table
        for (std::uint32_t j = 0; j < r.getNumTables(); ++j) {
            // get the KeyID for the button/table
            KeyID id = buttonKeys[j].first;
            if (id == kKeyNone) {
                continue;
            }

            // if we've already handled the KeyID in the table then
            // move on to the next table
            if (keys.count(id) > 0) {
                continue;
            }
            keys.insert(id);

            // prepare item.  the client state is 1 for dead keys.
            item.m_id     = id;
            item.m_group  = group;
            item.m_dead   = buttonKeys[j].second;
            item.m_client = buttonKeys[j].second ? 1 : 0;
            inputleap::KeyMap::initModifierKey(item);
            if (item.m_lock) {
                // all locking keys are half duplex on OS X
                keyMap.addHalfDuplexButton(i);
            }

            // collect the tables that map to the same KeyID.  we know it
            // can't be any earlier tables because of the check above.
            std::set<std::uint8_t> tables;
            tables.insert(static_cast<std::uint8_t>(j));
            for (std::uint32_t k = j + 1; k < r.getNumTables(); ++k) {
                if (buttonKeys[k].first == id) {
                    tables.insert(static_cast<std::uint8_t>(k));
                }
            }

            // collect the modifier combinations that map to any of the
            // tables we just collected
            for (std::uint32_t k = 0; k < r.getNumModifierCombinations(); ++k) {
                modifiers[k] = (tables.count(r.getTableForModifier(k)) > 0);
            }

            // figure out which modifiers the key is sensitive to.  the
            // key is insensitive to a modifier if for every modifier mask
            // with the modifier bit unset in the modifiers we also find
            // the same mask with the bit set.
            //
            // we ignore a few modifiers that we know aren't important
            // for generating characters.  in fact, we want to ignore any
            // characters generated by the control key.  we don't map
            // those and instead expect the control modifier plus a key.
            std::uint32_t sensitive = 0;
            for (std::uint32_t k = 0; (1u << k) <
                                r.getNumModifierCombinations(); ++k) {
                std::uint32_t bit = (1u << k);
                if ((bit << 8) == cmdKey ||
                    (bit << 8) == controlKey ||
                    (bit << 8) == rightControlKey) {
                    continue;
                }
                for (std::uint32_t m = 0; m < r.getNumModifierCombinations(); ++m) {
                    if (modifiers[m] != modifiers[m ^ bit]) {
                        sensitive |= bit;
                        break;
                    }
                }
            }

            // find each required modifier mask.  the key can be synthesized
            // using any of the masks.
            std::set<std::uint32_t> required;
            for (std::uint32_t k = 0; k < r.getNumModifierCombinations(); ++k) {
                if ((k & sensitive) == k && modifiers[k & sensitive]) {
                    required.insert(k);
                }
            }

            // now add a key entry for each key/required modifier pair.
            item.m_sensitive = mapModifiersFromOSX(sensitive << 16);
            for (auto k = required.begin(); k != required.end(); ++k) {
                item.m_required = mapModifiersFromOSX(*k << 16);
                keyMap.addKeyEntry(item);
            }
        }
    }

    return true;
}

bool OSXKeyState::map_hot_key_to_mac(KeyID key, KeyModifierMask mask,
                                     std::uint32_t &macVirtualKey,
                                     std::uint32_t &macModifierMask) const
{
    // look up button for key
    KeyButton button = getButton(key, pollActiveGroup());
    if (button == 0 && key != kKeyNone) {
        return false;
    }
    macVirtualKey = mapKeyButtonToVirtualKey(button);

    // calculate modifier mask
    macModifierMask = 0;
    if ((mask & KeyModifierShift) != 0) {
        macModifierMask |= shiftKey;
    }
    if ((mask & KeyModifierControl) != 0) {
        macModifierMask |= controlKey;
    }
    if ((mask & KeyModifierAlt) != 0) {
        macModifierMask |= cmdKey;
    }
    if ((mask & KeyModifierSuper) != 0) {
        macModifierMask |= optionKey;
    }
    if ((mask & KeyModifierCapsLock) != 0) {
        macModifierMask |= alphaLock;
    }
    if ((mask & KeyModifierNumLock) != 0) {
        macModifierMask |= s_osxNumLock;
    }

    return true;
}

void OSXKeyState::handleModifierKeys(const EventTarget* target, std::uint32_t virtualKey,
                                     KeyModifierMask oldMask, KeyModifierMask newMask)
{
    // compute changed modifiers
    KeyModifierMask changed = (oldMask ^ newMask);

    // synthesize changed modifier keys
    if ((changed & KeyModifierShift) != 0) {
        handleModifierKey(target, virtualKey, m_virtualKeyMap.find(virtualKey)->second,
                            (newMask & KeyModifierShift) != 0, newMask);
    }
    if ((changed & KeyModifierControl) != 0) {
        handleModifierKey(target, virtualKey, m_virtualKeyMap.find(virtualKey)->second,
                            (newMask & KeyModifierControl) != 0, newMask);
    }
    if ((changed & KeyModifierAlt) != 0) {
        handleModifierKey(target, virtualKey, m_virtualKeyMap.find(virtualKey)->second,
                            (newMask & KeyModifierAlt) != 0, newMask);
    }
    if ((changed & KeyModifierSuper) != 0) {
        handleModifierKey(target, virtualKey, m_virtualKeyMap.find(virtualKey)->second,
                            (newMask & KeyModifierSuper) != 0, newMask);
    }
    if ((changed & KeyModifierCapsLock) != 0) {
        handleModifierKey(target, virtualKey, m_virtualKeyMap.find(virtualKey)->second,
                            (newMask & KeyModifierCapsLock) != 0, newMask);
    }
    if ((changed & KeyModifierNumLock) != 0) {
        handleModifierKey(target, virtualKey, m_virtualKeyMap.find(virtualKey)->second,
                            (newMask & KeyModifierNumLock) != 0, newMask);
    }
}

void OSXKeyState::handleModifierKey(const EventTarget* target, std::uint32_t virtualKey, KeyID id,
                                    bool down, KeyModifierMask newMask)
{
    KeyButton button = mapVirtualKeyToKeyButton(virtualKey);
    onKey(button, down, newMask);
    sendKeyEvent(target, down, false, id, newMask, 0, button);
}

bool
OSXKeyState::getGroups(GroupList& groups) const
{
    CFIndex n;
    bool gotLayouts = false;

    // get number of layouts
    CFStringRef keys[] = { kTISPropertyInputSourceCategory };
    CFStringRef values[] = { kTISCategoryKeyboardInputSource };
    CFDictionaryRef dict = CFDictionaryCreate(nullptr, (const void **)keys, (const void **)values, 1, nullptr, nullptr);
    CFArrayRef kbds = runOnMainThread([&]() {
        return TISCreateInputSourceList(dict, false);
    });
    n = CFArrayGetCount(kbds);
    gotLayouts = (n != 0);

    if (!gotLayouts) {
        LOG_DEBUG1("can't get keyboard layouts");
        return false;
    }

    // get each layout
    groups.clear();
    for (CFIndex i = 0; i < n; ++i) {
        TISInputSourceRef keyboardLayout =
            (TISInputSourceRef)CFArrayGetValueAtIndex(kbds, i);

        const bool hasKeyLayout = runOnMainThread([&]() {
            return TISGetInputSourceProperty(keyboardLayout, kTISPropertyUnicodeKeyLayoutData) != nullptr;
        });
        LOG_DEBUG1("macOS keyboard layout candidate id=%s name=%s keymap=%s",
                   getInputSourceString(
                       keyboardLayout, kTISPropertyInputSourceID).c_str(),
                   getInputSourceString(
                       keyboardLayout, kTISPropertyLocalizedName).c_str(),
                   hasKeyLayout ? "yes" : "no");

        if (hasKeyLayout) {
            groups.push_back(keyboardLayout);
        }
    }
    return true;
}

void OSXKeyState::setGroup(std::int32_t group)
{
    if (group < 0 || group >= static_cast<std::int32_t>(m_groups.size())) {
        LOG_ERR("invalid macOS input source group=%d count=%d",
                group, static_cast<int>(m_groups.size()));
        return;
    }

    TISInputSourceRef target = m_groups[group];
    const std::string targetId = getInputSourceString(
        target, kTISPropertyInputSourceID);
    const OSStatus status = runOnMainThread([&]() {
        return TISSetInputMethodKeyboardLayoutOverride(target);
    });

    if (status != noErr) {
        LOG_WARN("macOS keyboard layout override failed group=%d target=%s status=%d",
                 group, targetId.c_str(), static_cast<int>(status));
        return;
    }

    LOG_DEBUG1("set macOS keyboard layout group=%d target=%s",
               group, targetId.c_str());
}

bool OSXKeyState::cycleInputSource(std::int32_t offset)
{
    CFStringRef keys[] = { kTISPropertyInputSourceCategory };
    CFStringRef values[] = { kTISCategoryKeyboardInputSource };
    CFDictionaryRef filter = CFDictionaryCreate(
        nullptr, reinterpret_cast<const void **>(keys),
        reinterpret_cast<const void **>(values), 1, nullptr, nullptr);
    CFArrayRef sources = runOnMainThread([&]() {
        return TISCreateInputSourceList(filter, false);
    });
    CFRelease(filter);

    if (sources == nullptr) {
        LOG_WARN("could not enumerate macOS input sources");
        return false;
    }

    // Full candidate list (with enabled/selectable flags) for the pure
    // policy helper; TIS refs are kept separately for the single final
    // TISSelectInputSource call.
    std::vector<OSXInputSourceCandidate> candidates;
    std::vector<TISInputSourceRef> selectableSources;
    const CFIndex count = CFArrayGetCount(sources);
    for (CFIndex i = 0; i < count; ++i) {
        TISInputSourceRef source = static_cast<TISInputSourceRef>(
            const_cast<void *>(CFArrayGetValueAtIndex(sources, i)));
        const auto sourceFlags = runOnMainThread([&]() {
            const CFBooleanRef selectCapable = static_cast<CFBooleanRef>(
                TISGetInputSourceProperty(
                    source, kTISPropertyInputSourceIsSelectCapable));
            const CFBooleanRef enabled = static_cast<CFBooleanRef>(
                TISGetInputSourceProperty(
                    source, kTISPropertyInputSourceIsEnabled));
            return std::make_pair(
                selectCapable == kCFBooleanTrue,
                enabled == kCFBooleanTrue);
        });
        const std::string sourceId = getInputSourceString(
            source, kTISPropertyInputSourceID);
        candidates.push_back(
            OSXInputSourceCandidate{sourceId, sourceFlags.second, sourceFlags.first});
        if (sourceFlags.first && sourceFlags.second) {
            selectableSources.push_back(source);
        }

        LOG_DEBUG1("macOS input source candidate id=%s selectable=%s enabled=%s",
                   sourceId.c_str(),
                   sourceFlags.first ? "yes" : "no",
                   sourceFlags.second ? "yes" : "no");
    }

    if (selectableSources.empty()) {
        LOG_WARN("no selectable macOS input sources");
        CFRelease(sources);
        return false;
    }

    TISInputSourceRef current = runOnMainThread([]() {
        return TISCopyCurrentKeyboardInputSource();
    });
    const std::string currentId = getInputSourceString(
        current, kTISPropertyInputSourceID);
    const std::string stateBefore = getCurrentInputSourceState();

    // Read-only Gureum preferences. Missing keys, non-string values, and
    // empty strings all degrade to "" (historical fallback order). No writes.
    const std::string preferredRomanId =
        readGureumPreference(CFSTR("LastRomanInputMode"));
    const std::string preferredHangulId =
        readGureumPreference(CFSTR("LastHangulInputMode"));
    LOG_DEBUG1("Gureum preferences roman=%s hangul=%s",
               preferredRomanId.empty() ? "<empty>" : preferredRomanId.c_str(),
               preferredHangulId.empty() ? "<empty>" : preferredHangulId.c_str());

    // Pure policy: no TIS/CF/sleep/event calls inside.
    const OSXInputSourceSelection selection = PickOSXInputSourceTarget(
        candidates, currentId, preferredRomanId, preferredHangulId, offset);
    if (selection.m_targetId.empty()) {
        LOG_WARN("no macOS input source target reason=%s", selection.m_reason.c_str());
        if (current != nullptr) {
            CFRelease(current);
        }
        CFRelease(sources);
        return false;
    }

    TISInputSourceRef target = nullptr;
    for (TISInputSourceRef source : selectableSources) {
        if (getInputSourceString(source, kTISPropertyInputSourceID) ==
            selection.m_targetId) {
            target = source;
            break;
        }
    }
    if (target == nullptr) {
        // Defensive: the pure helper only returns enabled+selectable IDs, so
        // this path is unreachable unless enumeration raced. Do not chain to
        // another source and do not write settings.
        LOG_WARN("macOS input source target not selectable target=%s reason=%s",
                 selection.m_targetId.c_str(), selection.m_reason.c_str());
        if (current != nullptr) {
            CFRelease(current);
        }
        CFRelease(sources);
        return false;
    }
    const std::string targetId = selection.m_targetId;
    LOG_DEBUG1("selecting macOS input source current=%s target=%s reason=%s",
               currentId.c_str(), targetId.c_str(), selection.m_reason.c_str());
    // Exactly one selection call on the final target.
    const OSStatus status = runOnMainThread([&]() {
        return TISSelectInputSource(target);
    });

    LOG_DEBUG1("cycle macOS input source offset=%+d current=%s target=%s status=%d reason=%s before=%s afterCall=%s",
               offset, currentId.c_str(), targetId.c_str(),
               static_cast<int>(status), selection.m_reason.c_str(),
               stateBefore.c_str(), getCurrentInputSourceState().c_str());

    if (current != nullptr) {
        CFRelease(current);
    }
    CFRelease(sources);

    if (status != noErr) {
        // Selection failure returns without chaining or settings writes.
        return false;
    }

    // TISSelectInputSource returns before activation is observable. Confirm
    // the requested source instead of letting the next remote character race.
    for (int attempt = 0; attempt < 8; ++attempt) {
        TISInputSourceRef active = runOnMainThread([]() {
            return TISCopyCurrentKeyboardInputSource();
        });
        const std::string activeId = getInputSourceString(
            active, kTISPropertyInputSourceID);
        if (active != nullptr) {
            CFRelease(active);
        }
        if (activeId == targetId) {
            LOG_DEBUG1("macOS input source activated target=%s state=%s",
                       targetId.c_str(), getCurrentInputSourceState().c_str());
            return true;
        }
        this_thread_sleep(0.025);
    }

    LOG_WARN("macOS input source did not activate target=%s state=%s",
             targetId.c_str(), getCurrentInputSourceState().c_str());
    return false;
}

void
OSXKeyState::checkKeyboardLayout()
{
    // XXX -- should call this when notified that groups have changed.
    // if no notification for that then we should poll.
    GroupList groups;
    if (getGroups(groups) && groups != m_groups) {
        updateKeyMap();
        updateKeyState();
    }
}

void
OSXKeyState::adjustAltGrModifier(const KeyIDs& ids,
                KeyModifierMask* mask, bool isCommand) const
{
    if (!isCommand) {
        for (auto i = ids.begin(); i != ids.end(); ++i) {
            KeyID id = *i;
            if (id != kKeyNone &&
                ((id < 0xe000u || id > 0xefffu) ||
                (id >= kKeyKP_Equal && id <= kKeyKP_9))) {
                *mask |= KeyModifierAltGr;
                return;
            }
        }
    }
}

KeyButton OSXKeyState::mapVirtualKeyToKeyButton(std::uint32_t keyCode)
{
    // 'A' maps to 0 so shift every id
    return static_cast<KeyButton>(keyCode + KeyButtonOffset);
}

std::uint32_t OSXKeyState::mapKeyButtonToVirtualKey(KeyButton keyButton)
{
    return static_cast<std::uint32_t>(keyButton - KeyButtonOffset);
}

} // namespace inputleap
