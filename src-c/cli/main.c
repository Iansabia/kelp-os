#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "cli.h"
#include "openclaw.h"

int parse_agent_opts(int argc, char **argv, agent_opts_t *opts) {
    static struct option long_options[] = {
        {"message",     required_argument, 0, 'm'},
        {"model",       required_argument, 0, 'M'},
        {"provider",    required_argument, 0, 'p'},
        {"system",      required_argument, 0, 's'},
        {"max-tokens",  required_argument, 0, 't'},
        {"temperature", required_argument, 0, 'T'},
        {"json",        no_argument,       0, 'j'},
        {"local",       no_argument,       0, 'l'},
        {"verbose",     no_argument,       0, 'v'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    optind = 1;  /* Reset getopt */
    int c;
    while ((c = getopt_long(argc, argv, "m:M:p:s:t:T:jlvh", long_options, NULL)) != -1) {
        switch (c) {
        case 'm': opts->message = optarg; break;
        case 'M': opts->model = optarg; break;
        case 'p': opts->provider = optarg; break;
        case 's': opts->system_prompt = optarg; break;
        case 't': opts->max_tokens = atoi(optarg); break;
        case 'T': opts->temperature = atof(optarg); break;
        case 'j': opts->json_output = true; break;
        case 'l': opts->local = true; break;
        case 'v': opts->verbose = true; break;
        case 'h':
            printf("Usage: openclaw agent [options]\n\n");
            printf("Options:\n");
            printf("  -m, --message MSG       Message to send\n");
            printf("  -M, --model MODEL       AI model to use\n");
            printf("  -p, --provider NAME     Provider (anthropic|openai)\n");
            printf("  -s, --system PROMPT     System prompt\n");
            printf("  -t, --max-tokens N      Max response tokens\n");
            printf("  -T, --temperature F     Temperature (0.0-2.0)\n");
            printf("  -j, --json              JSON output\n");
            printf("  -l, --local             Use local gateway\n");
            printf("  -v, --verbose           Verbose output\n");
            printf("  -h, --help              Show this help\n");
            return OC_ERR; /* Not really an error, but we want to exit */
        default:
            return OC_ERR;
        }
    }

    /* If no -m flag, check for remaining args as message */
    if (!opts->message && optind < argc) {
        /* Join remaining args into one message */
        size_t total = 0;
        for (int i = optind; i < argc; i++) {
            total += strlen(argv[i]) + 1;
        }
        char *msg = malloc(total);
        if (!msg) return OC_ERR;
        msg[0] = '\0';
        for (int i = optind; i < argc; i++) {
            if (i > optind) strcat(msg, " ");
            strcat(msg, argv[i]);
        }
        opts->message = msg;
    }

    return OC_OK;
}

void cli_usage(const char *progname) {
    printf("Usage: %s <command> [options]\n\n", progname);
    printf("Commands:\n");
    printf("  agent     Send a message to an AI provider\n");
    printf("  config    Display configuration\n");
    printf("  version   Show version information\n");
    printf("\nRun '%s <command> --help' for command-specific help.\n", progname);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        cli_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    /* Shift argv for subcommands */
    if (strcmp(cmd, "agent") == 0) {
        return cmd_agent(argc - 1, argv + 1);
    } else if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0 || strcmp(cmd, "-V") == 0) {
        return cmd_version(argc - 1, argv + 1);
    } else if (strcmp(cmd, "config") == 0) {
        return cmd_config(argc - 1, argv + 1);
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        cli_usage(argv[0]);
        return 0;
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        cli_usage(argv[0]);
        return 1;
    }
}
