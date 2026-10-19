# SPDX-License-Identifier: Apache-2.0
# Zephyr-Modul Wallfree: hardwareunabhaengige Ladelogik
add_subdirectory_ifdef(CONFIG_WALLFREE_EVSE lib/evse)
zephyr_include_directories(include)
