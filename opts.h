#pragma once

typedef enum { false, true } bool;

typedef struct {
} appjail_options;

void parse_options(appjail_options *opts, int argc, char *argv[]);