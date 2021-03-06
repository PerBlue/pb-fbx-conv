//
// Created by Martin Wickham on 8/27/17.
//

#ifndef PB_FBX_CONV_ARGS_H
#define PB_FBX_CONV_ARGS_H

#include "model.h"

struct Options {
    char *filepath = nullptr;
    char *outpath = nullptr;

    int maxVertices = 32767;
    int maxDrawBones = 12;
    int maxBlendWeights = 4;
    bool flipV = false;
    bool packVertexColors = false;
    double animFramerate = 15.0;
    double animPlaySpeed = 1.0;
    double animSamplingRate = 1.0 / 15.0;
    float animError = 0.0001;

    bool useJson = false;
    bool p3db = false;

    bool dumpElementTree = false;
    bool dumpObjectTree = false;
    bool dumpNodeTree = false;
    bool dumpMaterials = false;
    bool dumpMeshes = false;
    bool dumpGeom = false;
    bool dumpObj = false;
};

bool parseArgs(int argc, char *argv[], Options *opts);

#endif //PB_FBX_CONV_ARGS_H
