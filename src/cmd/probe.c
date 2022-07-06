// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018,2021 IBM Corp.

#include "compiler.h"

#include "host.h"
#include "log.h"
#include "soc.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>

static void
cmd_probe_help(const char *name, int argc __unused, char *argv[] __unused)
{
    static const char *probe_help =
        "Usage:\n"
        "%s probe --help\n"
        "%s probe --interface INTERFACE ...\n"
        "%s probe --list-interfaces\n"
        "%s probe --require <integrity|confidentiality>\n";

    printf(probe_help, name, name, name, name);
}

int cmd_probe(const char *name, int argc, char *argv[])
{
    enum bridge_mode required = bm_permissive;
    struct host _host, *host = &_host;
    struct soc _soc, *soc = &_soc;
    enum bridge_mode discovered;
    bool opt_list_ifaces = false;
    char *opt_iface = NULL;
    struct ahb *ahb;
    int rc;

    while (1) {
        int option_index = 0;
        int c;

        static struct option long_options[] = {
            { "help", no_argument, NULL, 'h' },
            { "interface", required_argument, NULL, 'i' },
            { "list-interfaces", no_argument, NULL, 'l' },
            { "require", required_argument, NULL, 'r' },
            { },
        };

        c = getopt_long(argc, argv, "hi:lr:", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                cmd_probe_help(name, argc, argv);
                rc = EXIT_SUCCESS;
                goto cleanup_soc;
            case 'i':
                opt_iface = optarg;
                break;
            case 'l':
                opt_list_ifaces = true;
                break;
            case 'r':
            {
                if (!strcmp("confidentiality", optarg)) {
                    required = bm_disabled;
                } else if (!strcmp("integrity", optarg)) {
                    required = bm_restricted;
                } else {
                    loge("Unrecognised requirement: %s\n", optarg);
                    loge("Valid requirements:\n"
                         "integrity\n"
                         "confidentiality\n");
                    rc = EXIT_FAILURE;
                    goto cleanup_soc;
                }
                break;
            }
        }
    }

    if ((rc = host_init(host, argc - optind, &argv[optind])) < 0) {
        loge("Failed to initialise host interfaces: %d\n", rc);
        rc = EXIT_FAILURE;
        goto done;
    }

    if (!(ahb = host_get_ahb(host))) {
        loge("Failed to acquire AHB interface, exiting\n");
        rc = EXIT_FAILURE;
        goto cleanup_host;
    }

    if ((rc = soc_probe(soc, ahb)) < 0) {
        loge("Failed to probe SoC, exiting: %d\n", rc);
        goto cleanup_host;
    }

    if (opt_list_ifaces) {
        soc_list_bridge_controllers(soc);
        rc = EXIT_SUCCESS;
    } else {
        if ((rc = soc_probe_bridge_controllers(soc, &discovered, opt_iface)) < 0) {
            loge("Failed to probe SoC bridge controllers: %d\n", rc);
            rc = EXIT_FAILURE;
            goto cleanup_soc;
        }

        rc = (required <= discovered) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

cleanup_soc:
    soc_destroy(soc);

cleanup_host:
    host_destroy(host);

done:
    exit(rc);
}
