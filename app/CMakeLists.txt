# SPDX-License-Identifier: Apache-2.0
# Wallfree-Anwendung fuer die Steuerung (nur bauen, nicht flashen) und fuer
# native_sim (Ende-zu-Ende-Test auf dem PC, docs/implementierung.md)
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/..)
# SOLL-BOOT-35: Anhang mit Laenge und CRC32 an zephyr.bin; nur dieses Abbild flashen
# (zephyr.hex/.elf haben ihn nicht, die Firmware verweigert dann das Laden). Vor
# find_package, weil Zephyr die Liste dort schon auswertet; fuer native_sim gibt es keine
# zephyr.bin, das Skript tut dann nichts.
set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
  COMMAND python3 -I ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/image-trailer.py --if-exists
  ${CMAKE_BINARY_DIR}/zephyr/zephyr.bin)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wallfree)
target_sources(app PRIVATE
  src/main.c
  src/evse_task.c
  src/shell_evse.c
  src/storage.c
  src/auth.c
  src/http_api.c
  src/netcfg.c
  src/sysinfo.c
  src/meter_task.c
  src/mqtt_task.c
  src/history_task.c
  src/timesync.c
  src/resolve.c
)
if(CONFIG_ARCH_POSIX)
  # PC-Simulation (native_sim): Modell statt TIM1/ADC/GPIO, Neustart per execv
  target_sources(app PRIVATE src/hw_sim.c)
  target_sources(native_simulator INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/sim_host.c)
  target_include_directories(native_simulator INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
else()
  target_sources(app PRIVATE src/hw_stm32.c src/hw_gpio.c src/meter_bus_modbus.c)
endif()
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
# Firmwarestand: Version aus VERSION (Major.Minor, Zwischenstaende "-dev"), Bauzeichen = Commit
# (Tags ausgeschlossen, bei Aenderungen "-dirty", ohne Git "unbekannt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION WALLFREE_VERSION LIMIT_COUNT 1)
execute_process(COMMAND git describe --always --dirty --exclude=*
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE WALLFREE_BUILD OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT WALLFREE_BUILD)
  set(WALLFREE_BUILD unbekannt)
endif()
target_compile_definitions(app PRIVATE WALLFREE_VERSION="${WALLFREE_VERSION}"
  WALLFREE_BUILD="${WALLFREE_BUILD}")
