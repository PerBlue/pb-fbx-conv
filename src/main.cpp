//
// Created by Martin Wickham on 8/26/17.
//

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "ofbx.h"
#include "types.h"
#include "model.h"
#include "convertfbx.h"
#include "dumpfbx.h"

char *filepath = nullptr;
char *outpath = nullptr;
bool dumpFbxTree = false;

void printHelp(char *programName) {
    printf("Usage: %s [options] filename\n", programName);
    printf("Options:\n");
    printf("  -o outfile    specify the [o]utput file\n");
    printf("  -t            dump the fbx [t]ree to the file 'tree.out'\n");
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

        // handle unary args here
        switch (nextmode) {
        case 't':
            dumpFbxTree = true;
            break;
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
        char *lastDot = strrchr(filepath, '.');

        size_t pos;
        if (lastDot == nullptr) pos = strlen(filepath);
        else                    pos = lastDot - filepath;

        outpath = new char[pos + 6]; // .p3db\0
        strncpy(outpath, filepath, pos);
        strncpy(outpath + pos, ".p3db", 6);
    }

    return success;
}

int main(int argc, char *argv[]) {
    clock_t start = clock();
    if (!parseArgs(argc, argv)) {
        exit(1);
    }

    printf("Converting '%s' to '%s'\n", filepath, outpath);

    // load the file into memory
    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        printf("Failed to open file '%s'\n", filepath);
        exit(-1);
    }
    fseek(fp, 0, SEEK_END);
    size_t file_size = size_t(ftell(fp));
    fseek(fp, 0, SEEK_SET);
    auto* content = new u8[file_size];
    fread(content, 1, file_size, fp);
    fclose(fp);

    // parse fbx into a usable format
    ofbx::IScene *scene = ofbx::load(content, file_size);
    const char *error = ofbx::getError();
    if (!scene || (error && error[0])) {
        printf("Failed to parse fbx file '%s'\n", filepath);
        if (error && error[0]) printf("OpenFBX Error: %s\n", error);
        exit(-2);
    }

    // print out the fbx tree
    if (dumpFbxTree) {
        dumpFbx(scene);
    }

    // convert to a Model
    Model model;
    convertFbxToModel(scene, &model);

    // export model to json
    // TODO
    // exportModelToJson(&model);

    clock_t end = clock();
    printf("Completed in %dms\n", int((end - start) * 1000 / CLOCKS_PER_SEC));
    exit(0); // die before piecewise deallocating.

    scene->destroy();
    delete[] content; // Looks like OpenFBX may keep references to the original buffer, so we can't delete it until we're done.
}