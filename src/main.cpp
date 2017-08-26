//
// Created by Martin Wickham on 8/26/17.
//

#include <cstdlib>
#include <cstring>
#include <cstdio>

char *filepath = nullptr;
char *outpath = nullptr;

void printHelp(char *programName) {
    printf("Usage: %s [-o outfile] filename\n", programName);
}

bool parseArgs(int argc, char *argv[]) {
    bool success = true;
    char mode = 0;

    for (int current = 1; current < argc; current++) {
        char nextmode = 0;
        char *cc = argv[current];

        switch (mode) {

        case 'o':
            if (outpath == nullptr) {
                outpath = cc;
            } else {
                printf("Output file specified more than once! Using %s\n", outpath);
            }
            break;

        default:
            printf("Unknown flag: '-%c'\n", mode);
            success = false;
            // intentional fall through
        case 0:
            if (cc[0] == '-') {
                nextmode = cc[1];
            } else if (filepath == nullptr) {
                filepath = cc;
            } else {
                printf("Unknown argument: %s\n", cc);
                success = false;
            }
            break;

        }

        switch (nextmode) {
            // handle unary args here
        default:
            mode = nextmode;
        }
    }

    if (mode != 0) {
        printf("Expected another parameter after '-%c'\n", mode);
        success = false;
    }

    if (filepath == nullptr) {
        printf("You must specify a file.\n");
        success = false;
    }

    if (!success) {
        printHelp(argv[0]);
    } else {
        int len = strlen(filepath);
        char *lastDot = strrchr(filepath, '.');

        int pos;
        if (lastDot == nullptr) pos = len;
        else                    pos = int(lastDot - filepath);

        outpath = new char[pos + 6]; // .p3db\0
        strncpy(outpath, filepath, pos);
        strncpy(outpath + pos, ".p3db", 6);
    }

    return success;
}

int main(int argc, char *argv[]) {
    if (!parseArgs(argc, argv)) {
        exit(1);
    }

    printf("Filepath = '%s', Outpath = '%s'\n", filepath, outpath);

    exit(0);
}