//
// Created by Martin Wickham on 8/26/17.
//

#ifndef PB_FBX_CONV_DUMPFBX_H
#define PB_FBX_CONV_DUMPFBX_H

#include "ofbx.h"

void dumpObject(FILE *file, const ofbx::Object *obj, int indent = 2);

void dumpFbx(const ofbx::IScene *scene);

#endif //PB_FBX_CONV_DUMPFBX_H
