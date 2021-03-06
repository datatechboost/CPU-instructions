// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cpu_instructions/x86/cpu_state.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace cpu_instructions {
namespace {

struct Int32Pair {
  int32_t a;
  int32_t b;
};

template <int alignement>
void CheckAlignement() {
  UniqueAlignedStorage<alignement, char[512]> char_array_buffer;
  EXPECT_EQ(512, (UniqueAlignedStorage<alignement, char[512]>::size));
  EXPECT_EQ(0, reinterpret_cast<std::uintptr_t>(char_array_buffer.get()) %
                   alignement);

  UniqueAlignedStorage<alignement, int32_t> int_buffer;
  EXPECT_EQ(4, (UniqueAlignedStorage<alignement, int32_t>::size));
  EXPECT_EQ(0, reinterpret_cast<std::uintptr_t>(int_buffer.get()) % alignement);

  UniqueAlignedStorage<alignement, Int32Pair> some_type_buffer;
  EXPECT_LE(8, (UniqueAlignedStorage<alignement, Int32Pair>::size));
  EXPECT_EQ(
      0, reinterpret_cast<std::uintptr_t>(some_type_buffer.get()) % alignement);
}

TEST(CpuStateTest, UniqueAlignedStorage) {
  CheckAlignement<16>();
  CheckAlignement<32>();
  CheckAlignement<64>();
  CheckAlignement<128>();
  CheckAlignement<512>();
  CheckAlignement<1024>();
}

}  // namespace
}  // namespace cpu_instructions
