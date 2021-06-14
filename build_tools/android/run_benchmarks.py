#!/usr/bin/env python3
# Copyright 2021 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Runs all matched benchmark suites on an Android device.

This script probes the Android phone via `adb` and uses the device information
to filter and run suitable benchmarks on it.

It expects that `adb` is installed, and there is an `iree-benchmark-module`
tool cross-compiled towards Android. It also expects the benchmark artifacts
are generated by building the `iree-benchmark-suites` target in the following
directory structure:

<root-build-dir>/benchmark_suites
└── <benchmark-category> (e.g., TensorFlow)
    ├── <benchmark-suite> (e.g., MobileBertSquad-fp32)
    │   ├── <benchmark-case> (e.g., iree-vulkan__GPU-Mali-Valhall__kernel-execution)
    │   │   └── flagfile
    │   ├── ...
    │   │   └── flagfile
    │   └── <benchmark_case>
    │       └── flagfile
    └── vmfb
        ├── compiled-<sha1>.vmfb
        ├── ...
        └── compiled-<sha1>.vmfb

Example usages:
  python3 run_benchmarks.py \
    --benchmark_tool=/path/to/android/target/iree-benchmark_module \
    /path/to/host/build/dir
"""

import argparse
import json
import os
import re
import subprocess

from typing import Any, Dict, Sequence, Tuple

from common.benchmark_description import (AndroidDeviceInfo, BenchmarkInfo,
                                          BenchmarkResults, get_output)

# All benchmarks' relative path against root build directory.
BENCHMARK_SUITE_REL_PATH = "benchmark_suites"
# VMFB files' relative path against a benchmark category directory.
VMFB_REL_PATH = "vmfb"

# The flagfile's filename for compiled benchmark artifacts.
MODEL_FLAGFILE_NAME = "flagfile"

# Root directory to perform benchmarks in on the Android device.
ANDROID_TMP_DIR = "/data/local/tmp/iree-benchmarks"

BENCHMARK_REPETITIONS = 10

# A map from Android CPU ABI to IREE's benchmark target architecture.
CPU_ABI_TO_TARGET_ARCH_MAP = {
    "arm64-v8a": "cpu-arm64-v8a",
}

# A map from Android GPU name to IREE's benchmark target architecture.
GPU_NAME_TO_TARGET_ARCH_MAP = {
    "adreno-640": "gpu-adreno",
    "adreno-650": "gpu-adreno",
    "adreno-660": "gpu-adreno",
    "mali-g77": "gpu-mali-valhall",
    "mali-g78": "gpu-mali-valhall",
}


def get_git_commit_hash(commit: str) -> str:
  return get_output(['git', 'rev-parse', commit],
                    cwd=os.path.dirname(os.path.realpath(__file__)))


def adb_push_to_tmp_dir(content: str,
                        relative_dir: str,
                        verbose: bool = False) -> str:
  """Pushes content onto the Android device.

  Args:
  - content: the full path to the source file.
  - relative_dir: the directory to push to; relative to ANDROID_TMP_DIR.

  Returns:
  - The full path to the content on the Android device.
  """
  filename = os.path.basename(content)
  android_path = os.path.join(ANDROID_TMP_DIR, relative_dir, filename)
  get_output(
      ["adb", "push", os.path.abspath(content), android_path], verbose=verbose)
  return android_path


def adb_execute_in_dir(cmd_args: Sequence[str],
                       relative_dir: str,
                       verbose: bool = False) -> str:
  """Executes command with adb shell in a directory.

  Args:
  - cmd_args: a list containing the command to execute and its parameters
  - relative_dir: the directory to execute the command in; relative to
    ANDROID_TMP_DIR.

  Returns:
  - A string for the command output.
  """
  cmd = ["adb", "shell"]
  cmd.extend(["cd", f"{ANDROID_TMP_DIR}/{relative_dir}"])
  cmd.append("&&")
  cmd.extend(cmd_args)

  return get_output(cmd, verbose=verbose)


def compose_benchmark_info_object(device_info: AndroidDeviceInfo,
                                  benchmark_category_dir: str,
                                  benchmark_case_dir: str) -> BenchmarkInfo:
  """Creates an BenchmarkInfo object to describe the benchmark.

  Args:
  - device_info: an AndroidDeviceInfo object.
  - benchmark_category_dir: the directory to a specific benchmark category.
  - benchmark_case_dir: a directory containing the benchmark case.

  Returns:
  - A BenchmarkInfo object.
  """
  # Extract the model name from the directory path. This uses the relative
  # path under the root model directory. If there are multiple segments,
  # additional ones will be placed in parentheses.
  model_name = os.path.relpath(benchmark_case_dir, benchmark_category_dir)
  # Now we have <model-name>/.../<iree-driver>__<target-arch>__<bench_mode>,
  # Remove the last segment.
  model_name = os.path.dirname(model_name)
  main, rest = os.path.split(model_name)
  if main:
    # Tags coming from directory structure.
    model_name = main
    model_tags = [re.sub(r"\W+", "-", rest)]
  else:
    # Tags coming from the name itself.
    model_name, rest = rest.split("-", 1)
    model_tags = rest.split(",")

  # Extract benchmark info from the directory path following convention:
  #   <iree-driver>__<target-architecture>__<benchmark_mode>
  root_immediate_dir = os.path.basename(benchmark_case_dir)
  iree_driver, target_arch, bench_mode = root_immediate_dir.split("__")

  return BenchmarkInfo(model_name=model_name,
                       model_tags=model_tags,
                       model_source="TensorFlow",
                       bench_mode=bench_mode.split(","),
                       runner=iree_driver,
                       device_info=device_info)


def filter_benchmarks_for_category(benchmark_category_dir: str,
                                   cpu_target_arch: str,
                                   gpu_target_arch: str,
                                   verbose: bool = False) -> Sequence[str]:
  """Filters benchmarks in a specific category for the given device.

  Args:
  - benchmark_category_dir: the directory to a specific benchmark category.
  - cpu_target_arch: CPU target architecture.
  - gpu_target_arch: GPU target architecture.

  Returns:
  - A list containing all matched benchmark cases' directories.
  """
  matched_benchmarks = []

  # Go over all benchmarks in the model directory to find those matching the
  # current Android device's CPU/GPU architecture.
  for root, dirs, _ in os.walk(benchmark_category_dir):
    # Take the immediate directory name and try to see if it contains compiled
    # models and flagfiles. This relies on the following directory naming
    # convention:
    #   <iree-driver>__<target-architecture>__<benchmark_mode>
    root_immediate_dir = os.path.basename(root)
    segments = root_immediate_dir.split("__")
    if len(segments) != 3 or not segments[0].startswith("iree-"):
      continue

    iree_driver, target_arch, bench_mode = segments
    target_arch = target_arch.lower()
    # We can choose this benchmark if it matches the CPU/GPU architecture.
    should_choose = (target_arch == cpu_target_arch or
                     target_arch == gpu_target_arch)
    if should_choose:
      matched_benchmarks.append(root)

    if verbose:
      print(f"dir: {root}")
      print(f"  iree_driver: {iree_driver}")
      print(f"  target_arch: {target_arch}")
      print(f"  bench_mode: {bench_mode}")
      print(f"  chosen: {should_choose}")

  return matched_benchmarks


def run_benchmarks_for_category(
    device_info: AndroidDeviceInfo,
    benchmark_category_dir: str,
    benchmark_case_dirs: Sequence[str],
    benchmark_tool: str,
    verbose: bool = False
) -> Sequence[Tuple[BenchmarkInfo, Dict[str, Any], Dict[str, Any]]]:
  """Runs all benchmarks on the Android device and reports results.

  Args:
  - device_info: an AndroidDeviceInfo object.
  - benchmark_category_dir: the directory to a specific benchmark category.
  - benchmark_case_dirs: a list of benchmark case directories.
  - benchmark_tool: the path to the benchmark tool.

  Returns:
  - A list containing (BenchmarkInfo, context, results) tuples.
  """
  # Push the benchmark vmfb and tool files to the Android device first.
  adb_push_to_tmp_dir(os.path.join(benchmark_category_dir, VMFB_REL_PATH),
                      relative_dir=os.path.basename(benchmark_category_dir),
                      verbose=verbose)
  android_tool_path = adb_push_to_tmp_dir(benchmark_tool,
                                          relative_dir="tools",
                                          verbose=verbose)

  results = []

  # Push all model artifacts to the device and run them.
  root_benchmark_dir = os.path.dirname(benchmark_category_dir)
  for benchmark_case_dir in benchmark_case_dirs:
    benchmark_info = compose_benchmark_info_object(device_info,
                                                   benchmark_category_dir,
                                                   benchmark_case_dir)
    print(f"--> benchmark: {benchmark_info} <--")
    android_relative_dir = os.path.relpath(benchmark_case_dir,
                                           root_benchmark_dir)
    adb_push_to_tmp_dir(os.path.join(benchmark_case_dir, MODEL_FLAGFILE_NAME),
                        android_relative_dir,
                        verbose=verbose)

    cmd = [
        "taskset",
        benchmark_info.deduce_taskset(),
        android_tool_path,
        f"--flagfile={MODEL_FLAGFILE_NAME}",
        f"--benchmark_repetitions={BENCHMARK_REPETITIONS}",
        "--benchmark_format=json",
    ]
    resultjson = adb_execute_in_dir(cmd, android_relative_dir, verbose=verbose)

    print(resultjson)
    resultjson = json.loads(resultjson)

    for previous_result in results:
      if previous_result[0] == benchmark_info:
        raise ValueError(f"Duplicated benchmark: {benchmark_info}")

    results.append(
        (benchmark_info, resultjson["context"], resultjson["benchmarks"]))

  return results


def filter_and_run_benchmarks(device_info: AndroidDeviceInfo,
                              root_build_dir: str,
                              benchmark_tool: str,
                              verbose: bool = False) -> BenchmarkResults:
  """Filters and runs benchmarks in all categories for the given device.

  Args:
  - device_info: an AndroidDeviceInfo object.
  - root_build_dir: the root build directory.
  - benchmark_tool: the path to the benchmark tool.
  """
  cpu_target_arch = CPU_ABI_TO_TARGET_ARCH_MAP[device_info.cpu_abi.lower()]
  gpu_target_arch = GPU_NAME_TO_TARGET_ARCH_MAP[device_info.gpu_name.lower()]

  root_benchmark_dir = os.path.join(root_build_dir, BENCHMARK_SUITE_REL_PATH)

  results = BenchmarkResults()

  for directory in os.listdir(root_benchmark_dir):
    benchmark_category_dir = os.path.join(root_benchmark_dir, directory)
    matched_benchmarks = filter_benchmarks_for_category(
        benchmark_category_dir=benchmark_category_dir,
        cpu_target_arch=cpu_target_arch,
        gpu_target_arch=gpu_target_arch,
        verbose=verbose)
    run_results = run_benchmarks_for_category(
        device_info=device_info,
        benchmark_category_dir=benchmark_category_dir,
        benchmark_case_dirs=matched_benchmarks,
        benchmark_tool=benchmark_tool,
        verbose=verbose)
    for info, context, runs in run_results:
      results.append_one_benchmark(info, context, runs)

  # Attach commit information.
  results.set_commit(get_git_commit_hash("HEAD"))

  return results


def parse_arguments():
  """Parses command-line options."""

  def check_dir_path(path):
    if os.path.isdir(path):
      return path
    else:
      raise argparse.ArgumentTypeError(path)

  def check_exe_path(path):
    if os.access(path, os.X_OK):
      return path
    else:
      raise argparse.ArgumentTypeError(f"'{path}' is not an executable")

  parser = argparse.ArgumentParser()
  parser.add_argument(
      "build_dir",
      metavar="<build-dir>",
      type=check_dir_path,
      help="Path to the build directory containing benchmark suites")
  parser.add_argument("--benchmark_tool",
                      type=check_exe_path,
                      default=None,
                      help="Path to the iree-benchmark-module tool (default to "
                      "iree/tools/iree-benchmark-module under <build-dir>)")
  parser.add_argument("-o",
                      dest="output",
                      default=None,
                      help="Path to the ouput file")
  parser.add_argument(
      "--no-clean",
      action="store_true",
      help=
      "Do not clean up the temporary directory used for benchmarking on the Android device"
  )
  parser.add_argument("--verbose",
                      action="store_true",
                      help="Print internal information during execution")

  args = parser.parse_args()

  if args.benchmark_tool is None:
    args.benchmark_tool = os.path.join(args.build_dir, "iree", "tools",
                                       "iree-benchmark-module")

  return args


def main(args):
  device_info = AndroidDeviceInfo.from_adb()
  if args.verbose:
    print(device_info)

  if device_info.cpu_abi.lower() not in CPU_ABI_TO_TARGET_ARCH_MAP:
    raise ValueError(f"Unrecognized CPU ABI: '{device_info.cpu_abi}'; "
                     "need to update the map")
  if device_info.gpu_name.lower() not in GPU_NAME_TO_TARGET_ARCH_MAP:
    raise ValueError(f"Unrecognized GPU name: '{device_info.gpu_name}'; "
                     "need to update the map")

  # Clear the benchmark directory on the Android device first just in case
  # there are leftovers from manual or failed runs.
  get_output(["adb", "shell", "rm", "-rf", ANDROID_TMP_DIR],
             verbose=args.verbose)

  results = filter_and_run_benchmarks(device_info, args.build_dir,
                                      args.benchmark_tool, args.verbose)

  if args.output is not None:
    with open(args.output, "w") as f:
      f.write(results.to_json_str())
  if args.verbose:
    print(results.commit)
    print(results.benchmarks)

  if not args.no_clean:
    # Clear the benchmark directory on the Android device.
    get_output(["adb", "shell", "rm", "-rf", ANDROID_TMP_DIR],
               verbose=args.verbose)


if __name__ == "__main__":
  main(parse_arguments())
