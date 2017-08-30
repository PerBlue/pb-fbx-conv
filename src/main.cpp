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
#include "args.h"
#include "convertobj.h"

Options opts;

int main(int argc, char *argv[]) {
    clock_t start = clock();
    if (!parseArgs(argc, argv, &opts)) {
        exit(1);
    }

    printf("Converting '%s' to '%s'\n", opts.filepath, opts.outpath);

    // load the file into memory
    FILE* fp = fopen(opts.filepath, "rb");
    if (!fp) {
        printf("Failed to open file '%s'\n", opts.filepath);
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
        printf("Failed to parse fbx file '%s'\n", opts.filepath);
        if (error && error[0]) printf("OpenFBX Error: %s\n", error);
        exit(-2);
    }

    // print out the fbx tree
    if (opts.dumpElementTree) {
        dumpElements(scene);
    }

    if (opts.dumpObjectTree) {
        dumpObjects(scene);
    }

    if (opts.dumpNodeTree) {
        dumpNodes(scene);
    }

    if (opts.dumpObj) {
        convertFbxToObj(scene, "geom.obj");
    }

    // convert to a Model
    Model model;
    convertFbxToModel(scene, &model, &opts);

    // export model to json
    // TODO
    // exportModelToJson(&model);

    clock_t end = clock();
    printf("Completed in %dms\n", int((end - start) * 1000 / CLOCKS_PER_SEC));
    exit(0); // die before piecewise deallocating.

    scene->destroy();
    delete[] content; // Looks like OpenFBX may keep references to the original buffer, so we can't delete it until we're done.
}