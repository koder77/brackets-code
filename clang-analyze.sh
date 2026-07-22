#!/bin/bash
clang-19 --analyze \
  -Xanalyzer -analyzer-checker=alpha.security.taint \
  -Xanalyzer -analyzer-checker=alpha.security.ArrayBoundV2 \
  -Xanalyzer -analyzer-checker=security \
  -Xanalyzer -analyzer-output=text \
  brackets-code.c embed.c learn.c dsl.c
