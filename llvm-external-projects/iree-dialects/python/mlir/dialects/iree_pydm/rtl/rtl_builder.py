# Copyright 2021 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Python DSL for building the PyDM runtime library.

Usage:
  python -m ...rtl_builder [module, [module...]] [--verbose]

By default, this will print an MLIR module in assembly format with all imported
runtime library modules.
"""

import importlib
import argparse
import logging
import sys

from .base import (
    RtlBuilder,
    RtlModule,
)


def main():
  parser = argparse.ArgumentParser(description="Build RTL modules")
  parser.add_argument("modules",
                      metavar="M",
                      type=str,
                      nargs="*",
                      help="RTL module to build")
  parser.add_argument("--verbose", dest="verbose", action="store_true")
  parser.set_defaults(modules=(".booleans", ".numerics"), verbose=False)
  args = parser.parse_args()
  relative_package = __package__ + ".modules"

  if args.verbose:
    logging.basicConfig(level=logging.DEBUG)

  builder = RtlBuilder()
  for module_name in args.modules:
    logging.info("Loading module %s (from %s)", module_name, relative_package)
    m = importlib.import_module(module_name, package=relative_package)
    if not hasattr(m, "RTL_MODULE"):
      raise ValueError(f"Expected {module_name} to have attribute RTL_MODULE")
    rtl_module = m.RTL_MODULE
    if not isinstance(rtl_module, RtlModule):
      raise ValueError(f"Expected {module_name}.RTL_MODULE to be an RtlModule")
    logging.info("Emitting module %s", rtl_module.name)
    builder.emit_module(rtl_module)

  print(builder.root_module)


if __name__ == "__main__":
  main()
