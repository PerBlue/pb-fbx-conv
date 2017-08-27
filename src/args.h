//
// Created by Martin Wickham on 8/27/17.
//

#ifndef PB_FBX_CONV_ARGS_H
#define PB_FBX_CONV_ARGS_H

#include "model.h"

struct Options {
    char *filepath = nullptr;
    char *outpath = nullptr;
    bool dumpFbxTree = false;
    bool dumpMaterials = false;
    bool dumpMeshes = false;
    bool dumpGeom = false;
    bool dumpObj = false;
    int maxBlendWeights = MAX_BLEND_WEIGHTS;
};

bool parseArgs(int argc, char *argv[], Options *opts);

#endif //PB_FBX_CONV_ARGS_H
