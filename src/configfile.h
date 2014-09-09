#pragma once

#include "common.h"

typedef struct {
  bool allow_new_privs_permitted;
  bool default_private_network;
  run_mode_t default_run_mode;
  bool default_bind_run_media;
} appjail_config;

appjail_config *parse_config();
void free_config(appjail_config *config);
