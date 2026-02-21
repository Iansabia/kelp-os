#include <stdio.h>
#include <stdlib.h>
#include "cli.h"
#include "config.h"
#include "auth.h"
#include "openclaw.h"

int cmd_config(int argc, char **argv) {
    (void)argc; (void)argv;

    config_t *cfg = config_load(NULL);
    if (!cfg) {
        fprintf(stderr, "Error: Failed to load config\n");
        return 1;
    }

    printf("OpenClaw Configuration\n");
    printf("======================\n");
    printf("Config dir:     %s\n", cfg->config_dir ? cfg->config_dir : "(none)");
    printf("Provider:       %s\n", cfg->default_provider);
    printf("Anthropic model: %s\n", cfg->anthropic_model);
    printf("OpenAI model:   %s\n", cfg->openai_model);
    printf("Max tokens:     %d\n", cfg->max_tokens);
    printf("Temperature:    %.1f\n", cfg->temperature);
    printf("Gateway port:   %d\n", cfg->gateway_port);
    printf("Gateway bind:   %s\n", cfg->gateway_bind);
    printf("TLS cert:       %s\n", cfg->tls_cert_path ? cfg->tls_cert_path : "(none)");
    printf("Anthropic key:  %s\n", cfg->anthropic_api_key ? "***set***" : "(not set)");
    printf("OpenAI key:     %s\n", cfg->openai_api_key ? "***set***" : "(not set)");
    printf("System prompt:  %.60s%s\n", cfg->system_prompt,
           strlen(cfg->system_prompt) > 60 ? "..." : "");

    config_free(cfg);
    return 0;
}
