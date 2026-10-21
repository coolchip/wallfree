# SPDX-License-Identifier: Apache-2.0
zephyr_library()
zephyr_library_sources(
  md5.c
  digest.c
  json.c
  evse_api.c
  events.c
  alog.c
  history.c
  sysconf.c
  mqtt.c
  ratelimit.c
  reset.c
)
zephyr_library_sources_ifdef(CONFIG_WALLFREE_METER meter_json.c)
