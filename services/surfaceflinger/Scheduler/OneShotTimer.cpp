/*
 * Copyright 2018 The Android Open Source Project
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

#include "OneShotTimer.h"
#include <utils/Log.h>
#include <utils/Timers.h>
#include <chrono>
#include <sstream>
#include <thread>

namespace {
using namespace std::chrono_literals;

constexpr int64_t kNsToSeconds = std::chrono::duration_cast<std::chrono::nanoseconds>(1s).count();

// The syscall interface uses a pair of integers for the timestamp. The first
// (tv_sec) is the whole count of seconds. The second (tv_nsec) is the
// nanosecond part of the count. This function takes care of translation.
void calculateTimeoutTime(std::chrono::nanoseconds timestamp, timespec* spec) {
    const nsecs_t timeout = systemTime(CLOCK_MONOTONIC) + timestamp.count();
    spec->tv_sec = static_cast<__kernel_time_t>(timeout / kNsToSeconds);
    spec->tv_nsec = timeout % kNsToSeconds;
}
} // namespace

namespace android {
namespace scheduler {

OneShotTimer::OneShotTimer(std::string name, const Interval& interval,
                           const ResetCallback& resetCallback,
                           const TimeoutCallback& timeoutCallback, std::unique_ptr<Clock> clock)
      : mClock(std::move(clock)),
        mName(std::move(name)),
        mInterval(interval),
        mResetCallback(resetCallback),
        mTimeoutCallback(timeoutCallback) {
    mLastResetTime = std::chrono::steady_clock::time_point::min();
    LOG_ALWAYS_FATAL_IF(!mClock, "Clock must not be provided");
}

OneShotTimer::~OneShotTimer() {
    stop();
}

void OneShotTimer::start() {
    int result = sem_init(&mSemaphore, 0, 0);
    LOG_ALWAYS_FATAL_IF(result, "sem_init failed");

    if (!mThread.joinable()) {
        // Only create thread if it has not been created.
        mThread = std::thread(&OneShotTimer::loop, this);
    }
}

void OneShotTimer::stop() {
    mStopTriggered = true;
    int result = sem_post(&mSemaphore);
    LOG_ALWAYS_FATAL_IF(result, "sem_post failed");

    if (mThread.joinable()) {
        mThread.join();
        result = sem_destroy(&mSemaphore);
        LOG_ALWAYS_FATAL_IF(result, "sem_destroy failed");
    }
}

void OneShotTimer::loop() {
    if (pthread_setname_np(pthread_self(), mName.c_str())) {
        ALOGW("Failed to set thread name on dispatch thread");
    }

    TimerState state = TimerState::RESET;
    while (true) {
        bool triggerReset = false;
        bool triggerTimeout = false;

        state = checkForResetAndStop(state);
        if (state == TimerState::STOPPED) {
            break;
        }

        if (state == TimerState::IDLE) {
            int result = sem_wait(&mSemaphore);
            if (result && errno != EINTR) {
                std::stringstream ss;
                ss << "sem_wait failed (" << errno << ")";
                LOG_ALWAYS_FATAL("%s", ss.str().c_str());
            }
            continue;
        }

        if (state == TimerState::RESET) {
            triggerReset = true;
        }

        if (triggerReset && mResetCallback) {
            mResetCallback();
        }

        state = checkForResetAndStop(state);
        if (state == TimerState::STOPPED) {
            break;
        }

        auto triggerTime = mClock->now() + mInterval.load();
        state = TimerState::WAITING;
        while (true) {
            if (mPaused) {
                mWaiting = true;
                int result = sem_wait(&mSemaphore);
                if (result && errno != EINTR) {
                    std::stringstream ss;
                    ss << "sem_wait failed (" << errno << ")";
                    LOG_ALWAYS_FATAL("%s", ss.str().c_str());
                }

                mWaiting = false;
                state = checkForResetAndStop(state);
                if (state == TimerState::STOPPED) {
                    break;
                }
            }
            // Wait until triggerTime time to check if we need to reset or drop into the idle state.
            if (const auto triggerInterval = triggerTime - mClock->now(); triggerInterval > 0ns) {
                mWaiting = true;
                struct timespec ts;
                calculateTimeoutTime(triggerInterval, &ts);
                int result = sem_clockwait(&mSemaphore, CLOCK_MONOTONIC, &ts);
                if (result && errno != ETIMEDOUT && errno != EINTR) {
                    std::stringstream ss;
                    ss << "sem_clockwait failed (" << errno << ")";
                    LOG_ALWAYS_FATAL("%s", ss.str().c_str());
                }
            }

            mWaiting = false;
            state = checkForResetAndStop(state);
            if (state == TimerState::STOPPED) {
                break;
            }

            if (!mPaused && state == TimerState::WAITING && (triggerTime - mClock->now()) <= 0ns) {
                triggerTimeout = true;
                state = TimerState::IDLE;
                break;
            }

            if (state == TimerState::RESET) {
                triggerTime = mLastResetTime.load() + mInterval.load();
                state = TimerState::WAITING;
            }
        }

        if (triggerTimeout && mTimeoutCallback) {
            mTimeoutCallback();
        }
    }
}

OneShotTimer::TimerState OneShotTimer::checkForResetAndStop(TimerState state) {
    // Stop takes precedence of the reset.
    if (mStopTriggered.exchange(false)) {
        return TimerState::STOPPED;
    }
    // If the state was stopped, the thread was joined, and we cannot reset
    // the timer anymore.
    if (state != TimerState::STOPPED && mResetTriggered.exchange(false)) {
        return TimerState::RESET;
    }
    return state;
}

void OneShotTimer::reset() {
    mLastResetTime = mClock->now();
    mResetTriggered = true;
    // If mWaiting is true, then we are guaranteed to be in a block where we are waiting on
    // mSemaphore for a timeout, rather than idling. So we can avoid a sem_post call since we can
    // just check that we triggered a reset on timeout.
    if (!mWaiting) {
        LOG_ALWAYS_FATAL_IF(sem_post(&mSemaphore), "sem_post failed");
    }
}

void OneShotTimer::pause() {
    mPaused = true;
}

void OneShotTimer::resume() {
    if (mPaused.exchange(false)) {
        LOG_ALWAYS_FATAL_IF(sem_post(&mSemaphore), "sem_post failed");
    }
}

} // namespace scheduler
} // namespace android
