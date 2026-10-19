# SPDX-License-Identifier: Apache-2.0
# Zephyr-Modul Wallfree: hardwareunabhaengige Ladelogik, Parameterspeicher und API
add_subdirectory_ifdef(CONFIG_WALLFREE_EVSE lib/evse)
add_subdirectory_ifdef(CONFIG_WALLFREE_NVM lib/nvm)
add_subdirectory_ifdef(CONFIG_WALLFREE_API lib/api)
zephyr_include_directories(include)
