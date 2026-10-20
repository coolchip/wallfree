# SPDX-License-Identifier: Apache-2.0
zephyr_library()
zephyr_library_sources(
  md5.c
  digest.c
  json.c
  evse_api.c
  events.c
  alog.c
  sysconf.c
  ratelimit.c
  reset.c
)
