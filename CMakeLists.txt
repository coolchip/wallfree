# SPDX-License-Identifier: Apache-2.0
# Zephyr-Modul Wallfree: hardwareunabhaengige Ladelogik und Parameterspeicher
add_subdirectory_ifdef(CONFIG_WALLFREE_EVSE lib/evse)
add_subdirectory_ifdef(CONFIG_WALLFREE_NVM lib/nvm)
zephyr_include_directories(include)
