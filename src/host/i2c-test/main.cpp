/**
 * @file main.cpp
 * @brief A tool to communicate over I2C to the Pixy
 * @copyright Copyright © 2021 Matternet. All rights reserved.
 */

#include "blob_data.hpp"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_I2C_PORT    "/dev/i2c-0"

static bool continue_ = true;

static void terminateHandler(int)
{
    continue_ = false;
}

static void help(const char *progname)
{
    printf("Usage: %s [-e] [-i]\n", progname);
    printf("  -e  Enable image logging (default: false)\n");
    printf("  -i  I2C interface path (default: %s)\n", DEFAULT_I2C_PORT);
    exit(1);
}

int main(int argc, char *argv[])
{
    signal(SIGTERM, terminateHandler);
    signal(SIGINT, terminateHandler);

    bool enable_logging = false;
    char i2c_port[80];
    strncpy(i2c_port, DEFAULT_I2C_PORT, sizeof(i2c_port));

    // Parse command line arguments
    int arg;
    while ((arg = getopt(argc, argv, "ehi:" )) != EOF)
    {
        switch (arg)
        {
            case 'e':
                enable_logging = true;
                break;

            case 'i':
                if (optarg != NULL)
                {
                    strncpy(i2c_port, optarg, sizeof(i2c_port));
                    i2c_port[sizeof(i2c_port) - 1] = 0;
                }
                break;

            case 'h':
            default:
                help(argv[0]);
                break;
        }
    }

    int rc = 0;
    BlobParser parser;

    if (parser.init(i2c_port, enable_logging) < 0)
    {
        printf("Failed to open i2c port %s\n", i2c_port);
        return -1;
    }

    printf("Ctrl-C to exit\n");
    while (continue_ && rc >= 0)
    {
        rc = parser.run();
    }

    return rc;
}
