//
// Created by Martin Wickham on 8/30/17.
//

#ifndef PB_FBX_CONV_WRITEP3DB_H
#define PB_FBX_CONV_WRITEP3DB_H

#include "model.h"

bool writeP3db(Model *model, const char *filename, bool pbAnimations);
bool writeP3dj(Model *model, const char *filename, bool pbAnimations);

#endif //PB_FBX_CONV_WRITEP3DB_H
