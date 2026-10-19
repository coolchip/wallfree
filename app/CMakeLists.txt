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
  src/auth.c
  src/http_api.c
  src/netcfg.c
  src/sysinfo.c
)
# Ressourcenliste des HTTP-Servers (HTTP_RESOURCE_DEFINE)
zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(NAME http_resource_desc_wallfree_http KVMA RAM_REGION GROUP RODATA_REGION)
# Weboberflaeche (SOLL-API-31): gzip-komprimiert im Flash
set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/web)
foreach(f index.html app.js style.css vendor/preact-htm.js)
  get_filename_component(n ${f} NAME)
  generate_inc_file_for_target(app ${CMAKE_CURRENT_SOURCE_DIR}/../web/${f} ${gen_dir}/${n}.gz.inc --gzip)
endforeach()
zephyr_include_directories(${ZEPHYR_BINARY_DIR}/include/generated)
# Firmwarestand fuer /system/info (git describe, sonst "unbekannt")
execute_process(COMMAND git describe --always --dirty
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE WALLFREE_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(WALLFREE_VERSION)
  target_compile_definitions(app PRIVATE WALLFREE_VERSION="${WALLFREE_VERSION}")
endif()
