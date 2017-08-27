//
// Created by Martin Wickham on 8/27/17.
//

#ifndef PB_FBX_CONV_ARGS_H
#define PB_FBX_CONV_ARGS_H


struct Options {
    char *filepath = nullptr;
    char *outpath = nullptr;
    bool dumpFbxTree = false;
    bool dumpMaterials = false;
};

bool parseArgs(int argc, char *argv[], Options *opts);

#endif //PB_FBX_CONV_ARGS_H
