# SPDX-License-Identifier: Apache-2.0
zephyr_library()
zephyr_library_sources(meter.c)
# simulierter Zaehler nur fuer PC-Tests und native_sim
zephyr_library_sources_ifdef(CONFIG_ARCH_POSIX meter_sim.c)
