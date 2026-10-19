# SPDX-License-Identifier: Apache-2.0
# Wallfree-Anwendung fuer die Steuerung (nur bauen, nicht flashen)
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/..)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wallfree)
target_sources(app PRIVATE
  src/main.c
  src/hw_stm32.c
  src/hw_gpio.c
  src/evse_task.c
  src/shell_evse.c
  src/storage.c
)
