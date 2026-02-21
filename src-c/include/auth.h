#ifndef AUTH_H
#define AUTH_H

#include "openclaw.h"
#include "config.h"

const char *auth_resolve(config_t *cfg, provider_t provider);
provider_t auth_parse_provider(const char *name);
const char *auth_provider_name(provider_t provider);

#endif
