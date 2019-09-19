# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

include(AbseilConfigureCopts)

set(IREE_CXX_STANDARD 11)

list(APPEND IREE_COMMON_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

set(IREE_DEFAULT_COPTS "${ABSL_DEFAULT_COPTS}")
set(IREE_TEST_COPTS "${ABSL_TEST_COPTS}")
set(IREE_DEFAULT_LINKOPTS "${ABSL_DEFAULT_LINKOPTS}")
