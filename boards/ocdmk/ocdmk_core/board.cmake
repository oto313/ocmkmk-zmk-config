# SPDX-License-Identifier: MIT

board_runner_args(jlink "--device=nRF52833_xxAA")

include(${ZEPHYR_BASE}/boards/common/uf2.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)

