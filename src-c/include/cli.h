/*
 * cli.h — Argument parsing and command dispatch
 */
#ifndef CLI_H
#define CLI_H

#include "openclaw.h"
#include "config.h"

/* Agent command options */
typedef struct {
    const char *message;       /* -m, --message */
    const char *model;         /* --model */
    const char *provider;      /* --provider (anthropic|openai) */
    const char *system_prompt; /* --system */
    int         max_tokens;    /* --max-tokens */
    double      temperature;   /* --temperature */
    bool        json_output;   /* --json */
    bool        local;         /* --local (use gateway) */
    bool        verbose;       /* -v, --verbose */
} agent_opts_t;

/*
 * Parse agent subcommand options from argv.
 * Returns OC_OK on success, OC_ERR on failure.
 */
int parse_agent_opts(int argc, char **argv, agent_opts_t *opts);

/*
 * Command handlers.
 */
int cmd_agent(int argc, char **argv);
int cmd_version(int argc, char **argv);
int cmd_config(int argc, char **argv);

/*
 * Print usage/help.
 */
void cli_usage(const char *progname);

#endif /* CLI_H */
