//
// Created by Martin Wickham on 8/26/17.
//

#ifndef PB_FBX_CONV_LOADFBX_H
#define PB_FBX_CONV_LOADFBX_H

#include "ofbx.h"
#include "model.h"

bool convertFbxToModel(const ofbx::IScene *scene, Model *model);

#endif //PB_FBX_CONV_LOADFBX_H
