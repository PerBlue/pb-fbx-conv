//
// Created by Martin Wickham on 8/27/17.
//

#include <cstdio>
#include <cstring>
#include "args.h"

static void printHelp(char *programName) {
    printf("Usage: %s [options] filename\n", programName);
    printf("Options:\n");
    printf("  -o outfile    specify the [o]utput file\n");
    printf("  -B maxWeights specify the max number of [B]lend weights per vertex (default 8)\n");
    printf("  -t            dump the fbx object [t]ree to the file 'objects.out'\n");
    printf("  -e            dump the fbx [e]lement tree to the file 'elements.out'\n");
    printf("  -n            dump the fbx [n]ode tree to the file 'nodes.out'\n");
    printf("  -m            dump the raw [m]aterials to the console\n");
    printf("  -M            dump the raw [M]eshes to the console\n");
    printf("  -g            dump the raw [g]eometry to the console\n");
    printf("  -O            dump a .obj file containing the tesselated geometry to 'geom.obj'\n");
}

bool parseArgs(int argc, char *argv[], Options *opts) {
    bool success = true;
    char mode = 0;

    for (int current = 1; current < argc; current++) {
        char nextmode = 0;
        char *cc = argv[current];

        switch (mode) {

            case 'o':
                if (opts->outpath == nullptr) {
                    opts->outpath = cc;
                } else {
                    printf("Output file specified more than once! Using %s\n", opts->outpath);
                }
                break;

            case 'B': {
                int weights = atoi(cc);
                if (weights < 0 || weights > MAX_BLEND_WEIGHTS) {
                    printf("Error: number of blend weights must be between 0 and %d\n", MAX_BLEND_WEIGHTS);
                    success = false;
                } else {
                    opts->maxBlendWeights = weights;
                    if (weights == 0) printf("Disabling vertex skinning because of '-b 0' argument.");
                }
                break;
            }


            default:
                printf("Unknown flag: '-%c'\n", mode);
                success = false;
                // intentional fall through
            case 0:
                if (cc[0] == '-') {
                    nextmode = cc[1];
                } else if (opts->filepath == nullptr) {
                    opts->filepath = cc;
                } else {
                    printf("Unknown argument: %s\n", cc);
                    success = false;
                }
                break;

        }

        // handle unary args here
        switch (nextmode) {
            case 't':
                opts->dumpObjectTree = true;
                break;
            case 'e':
                opts->dumpElementTree = true;
                break;
            case 'n':
                opts->dumpNodeTree = true;
                break;
            case 'm':
                opts->dumpMaterials = true;
                break;
            case 'M':
                opts->dumpMeshes = true;
                break;
            case 'g':
                opts->dumpGeom = true;
                break;
            case 'O':
                opts->dumpObj = true;
                break;
            default:
                mode = nextmode;
        }
    }

    if (mode != 0) {
        printf("Expected another parameter after '-%c'\n", mode);
        success = false;
    }

    if (opts->filepath == nullptr) {
        printf("You must specify a file.\n");
        success = false;
    }

    if (!success) {
        printHelp(argv[0]);
    } else {
        char *lastDot = strrchr(opts->filepath, '.');

        size_t pos;
        if (lastDot == nullptr) pos = strlen(opts->filepath);
        else                    pos = lastDot - opts->filepath;

        opts->outpath = new char[pos + 6]; // .p3db\0
        strncpy(opts->outpath, opts->filepath, pos);
        strncpy(opts->outpath + pos, ".p3db", 6);
    }

    return success;
}
