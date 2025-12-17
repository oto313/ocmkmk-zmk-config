# Copyright (c) 2024 OCDMK Contributors
# SPDX-License-Identifier: MIT

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_INDICATOR_LED indicator_led.c)

