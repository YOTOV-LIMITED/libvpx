/*
 *  Copyright (c) 2012 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef TEST_REGISTER_STATE_CHECK_H_
#define TEST_REGISTER_STATE_CHECK_H_

#include "third_party/googletest/src/include/gtest/gtest.h"
#include "./vpx_config.h"
#include "vpx/vpx_integer.h"

// ASM_REGISTER_STATE_CHECK(asm_function)
//   Minimally validates the environment pre & post function execution. This
//   variant should be used with assembly functions which are not expected to
//   fully restore the system state. See platform implementations of
//   RegisterStateCheck for details.
//
// API_REGISTER_STATE_CHECK(api_function)
//   Performs all the checks done by ASM_REGISTER_STATE_CHECK() and any
//   additional checks to ensure the environment is in a consistent state pre &
//   post function execution. This variant should be used with API functions.
//   See platform implementations of RegisterStateCheckXXX for details.
//

#if defined(_WIN64)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winnt.h>

namespace testing {
namespace internal {

inline bool operator==(const M128A& lhs, const M128A& rhs) {
  return (lhs.Low == rhs.Low && lhs.High == rhs.High);
}

}  // namespace internal
}  // namespace testing

namespace libvpx_test {

// Compares the state of xmm[6-15] at construction with their state at
// destruction. These registers should be preserved by the callee on
// Windows x64.
class RegisterStateCheck {
 public:
  RegisterStateCheck() { initialized_ = StoreRegisters(&pre_context_); }
  ~RegisterStateCheck() { EXPECT_TRUE(Check()); }

 private:
  static bool StoreRegisters(CONTEXT* const context) {
    const HANDLE this_thread = GetCurrentThread();
    EXPECT_TRUE(this_thread != NULL);
    context->ContextFlags = CONTEXT_FLOATING_POINT;
    const bool context_saved = GetThreadContext(this_thread, context) == TRUE;
    EXPECT_TRUE(context_saved) << "GetLastError: " << GetLastError();
    return context_saved;
  }

  // Compares the register state. Returns true if the states match.
  bool Check() const {
    if (!initialized_) return false;
    CONTEXT post_context;
    if (!StoreRegisters(&post_context)) return false;

    const M128A* xmm_pre = &pre_context_.Xmm6;
    const M128A* xmm_post = &post_context.Xmm6;
    for (int i = 6; i <= 15; ++i) {
      EXPECT_EQ(*xmm_pre, *xmm_post) << "xmm" << i << " has been modified!";
      ++xmm_pre;
      ++xmm_post;
    }
    return !testing::Test::HasNonfatalFailure();
  }

  bool initialized_;
  CONTEXT pre_context_;
};

#define ASM_REGISTER_STATE_CHECK(statement) do {  \
  libvpx_test::RegisterStateCheck reg_check;      \
  statement;                                      \
} while (false)

}  // namespace libvpx_test

#elif defined(CONFIG_SHARED) && defined(HAVE_NEON_ASM) && defined(CONFIG_VP9) \
      && !CONFIG_SHARED && HAVE_NEON_ASM && CONFIG_VP9

extern "C" {
// Save the d8-d15 registers into store.
void vpx_push_neon(int64_t *store);
}

namespace libvpx_test {

// Compares the state of d8-d15 at construction with their state at
// destruction. These registers should be preserved by the callee on
// arm platform.
class RegisterStateCheck {
 public:
  RegisterStateCheck() { initialized_ = StoreRegisters(pre_store_); }
  ~RegisterStateCheck() { EXPECT_TRUE(Check()); }

 private:
  static bool StoreRegisters(int64_t store[8]) {
    vpx_push_neon(store);
    return true;
  }

  // Compares the register state. Returns true if the states match.
  bool Check() const {
    if (!initialized_) return false;
    int64_t post_store[8];
    vpx_push_neon(post_store);
    for (int i = 0; i < 8; ++i) {
      EXPECT_EQ(pre_store_[i], post_store[i]) << "d"
          << i + 8 << " has been modified";
    }
    return !testing::Test::HasNonfatalFailure();
  }

  bool initialized_;
  int64_t pre_store_[8];
};

#define ASM_REGISTER_STATE_CHECK(statement) do {  \
  libvpx_test::RegisterStateCheck reg_check;      \
  statement;                                      \
} while (false)

}  // namespace libvpx_test

#else

namespace libvpx_test {

class RegisterStateCheck {};
#define ASM_REGISTER_STATE_CHECK(statement) statement

}  // namespace libvpx_test

#endif  // _WIN64

#if ARCH_X86 || ARCH_X86_64
#if defined(__GNUC__)

namespace libvpx_test {

// Checks the FPU tag word pre/post execution to ensure emms has been called.
class RegisterStateCheckMMX {
 public:
  RegisterStateCheckMMX() {
    __asm__ volatile("fstenv %0" : "=rm"(pre_fpu_env_));
  }
  ~RegisterStateCheckMMX() { EXPECT_TRUE(Check()); }

 private:
  // Checks the FPU tag word pre/post execution, returning false if not cleared
  // to 0xffff.
  bool Check() const {
    EXPECT_EQ(0xffff, pre_fpu_env_[4])
        << "FPU was in an inconsistent state prior to call";

    uint16_t post_fpu_env[14];
    __asm__ volatile("fstenv %0" : "=rm"(post_fpu_env));
    EXPECT_EQ(0xffff, post_fpu_env[4])
        << "FPU was left in an inconsistent state after call";
    return !testing::Test::HasNonfatalFailure();
  }

  uint16_t pre_fpu_env_[14];
};

#define API_REGISTER_STATE_CHECK(statement) do {  \
  libvpx_test::RegisterStateCheckMMX reg_check;   \
  ASM_REGISTER_STATE_CHECK(statement);            \
} while (false)

}  // namespace libvpx_test

#endif  // __GNUC__
#endif  // ARCH_X86 || ARCH_X86_64

#ifndef API_REGISTER_STATE_CHECK
#define API_REGISTER_STATE_CHECK ASM_REGISTER_STATE_CHECK
#endif

#endif  // TEST_REGISTER_STATE_CHECK_H_
