/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <attestation/HmacKeyManager.h>
#include <gtest/gtest.h>
#include <input/Input.h>

namespace android {

static KeyEvent getKeyEventWithFlags(int32_t flags) {
    KeyEvent event;
    event.initialize(InputEvent::nextId(), /*deviceId=*/2, AINPUT_SOURCE_GAMEPAD,
                     ui::LogicalDisplayId::DEFAULT, INVALID_HMAC, AKEY_EVENT_ACTION_DOWN, flags,
                     AKEYCODE_BUTTON_X, /*scanCode=*/121, AMETA_ALT_ON, /*repeatCount=*/1,
                     /*downTime=*/1000, /*eventTime=*/2000);
    return event;
}

static MotionEvent getMotionEventWithFlags(int32_t flags) {
    MotionEvent event;
    constexpr size_t pointerCount = 1;
    PointerProperties pointerProperties[pointerCount];
    PointerCoords pointerCoords[pointerCount];
    for (size_t i = 0; i < pointerCount; i++) {
        pointerProperties[i].clear();
        pointerProperties[i].id = i;
        pointerCoords[i].clear();
    }

    ui::Transform transform;
    transform.set({2, 0, 4, 0, 3, 5, 0, 0, 1});
    ui::Transform identity;
    event.initialize(InputEvent::nextId(), /*deviceId=*/0, AINPUT_SOURCE_MOUSE,
                     ui::LogicalDisplayId::DEFAULT, INVALID_HMAC, AMOTION_EVENT_ACTION_DOWN,
                     /*actionButton=*/0, flags, AMOTION_EVENT_EDGE_FLAG_NONE, AMETA_NONE,
                     /*buttonState=*/0, MotionClassification::NONE, transform, /*xPrecision=*/0.1,
                     /*yPrecision=*/0.2,
                     /*xCursorPosition=*/280, /*yCursorPosition=*/540, identity, /*downTime=*/100,
                     /*eventTime=*/200, pointerCount, pointerProperties, pointerCoords);
    return event;
}

TEST(VerifiedKeyEventTest, ConvertKeyEventToVerifiedKeyEvent) {
    KeyEvent event = getKeyEventWithFlags(0);
    VerifiedKeyEvent verified = verifiedKeyEventFromKeyEvent(event);

    ASSERT_EQ(VerifiedInputEvent::Type::KEY, verified.type);

    ASSERT_EQ(event.getDeviceId(), verified.deviceId);
    ASSERT_EQ(event.getEventTime(), verified.eventTimeNanos);
    ASSERT_EQ(event.getSource(), verified.source);
    ASSERT_EQ(event.getDisplayId(), verified.displayId);

    ASSERT_EQ(event.getAction(), verified.action);
    ASSERT_EQ(event.getDownTime(), verified.downTimeNanos);
    ASSERT_EQ(event.getFlags() & VERIFIED_KEY_EVENT_FLAGS, verified.flags);
    ASSERT_EQ(event.getKeyCode(), verified.keyCode);
    ASSERT_EQ(event.getScanCode(), verified.scanCode);
    ASSERT_EQ(event.getMetaState(), verified.metaState);
    ASSERT_EQ(event.getRepeatCount(), verified.repeatCount);
}

TEST(VerifiedKeyEventTest, VerifiedKeyEventContainsOnlyVerifiedFlags) {
    KeyEvent event = getKeyEventWithFlags(AKEY_EVENT_FLAG_CANCELED | AKEY_EVENT_FLAG_FALLBACK);
    VerifiedKeyEvent verified = verifiedKeyEventFromKeyEvent(event);
    ASSERT_EQ(AKEY_EVENT_FLAG_CANCELED, verified.flags);
}

TEST(VerifiedKeyEventTest, VerifiedKeyEventDoesNotContainUnverifiedFlags) {
    KeyEvent event = getKeyEventWithFlags(AKEY_EVENT_FLAG_EDITOR_ACTION);
    VerifiedKeyEvent verified = verifiedKeyEventFromKeyEvent(event);
    ASSERT_EQ(0, verified.flags);
}

TEST(VerifiedMotionEventTest, ConvertMotionEventToVerifiedMotionEvent) {
    MotionEvent event = getMotionEventWithFlags(0);
    VerifiedMotionEvent verified = verifiedMotionEventFromMotionEvent(event);

    ASSERT_EQ(VerifiedInputEvent::Type::MOTION, verified.type);

    ASSERT_EQ(event.getDeviceId(), verified.deviceId);
    ASSERT_EQ(event.getEventTime(), verified.eventTimeNanos);
    ASSERT_EQ(event.getSource(), verified.source);
    ASSERT_EQ(event.getDisplayId(), verified.displayId);

    ASSERT_EQ(event.getRawX(0), verified.rawX);
    ASSERT_EQ(event.getRawY(0), verified.rawY);
    ASSERT_EQ(event.getAction(), verified.actionMasked);
    ASSERT_EQ(event.getDownTime(), verified.downTimeNanos);
    ASSERT_EQ(event.getFlags() & VERIFIED_MOTION_EVENT_FLAGS, verified.flags);
    ASSERT_EQ(event.getMetaState(), verified.metaState);
    ASSERT_EQ(event.getButtonState(), verified.buttonState);
}

TEST(VerifiedMotionEventTest, VerifiedMotionEventContainsOnlyVerifiedFlags) {
    MotionEvent event = getMotionEventWithFlags(AMOTION_EVENT_FLAG_WINDOW_IS_OBSCURED |
                                                AMOTION_EVENT_FLAG_IS_GENERATED_GESTURE);
    VerifiedMotionEvent verified = verifiedMotionEventFromMotionEvent(event);
    ASSERT_EQ(AMOTION_EVENT_FLAG_WINDOW_IS_OBSCURED, verified.flags);
}

TEST(VerifiedMotionEventTest, VerifiedMotionEventDoesNotContainUnverifiedFlags) {
    MotionEvent event = getMotionEventWithFlags(AMOTION_EVENT_FLAG_TAINTED);
    VerifiedMotionEvent verified = verifiedMotionEventFromMotionEvent(event);
    ASSERT_EQ(0, verified.flags);
}

} // namespace android
