//
// Created by Martin Wickham on 8/26/17.
//

#ifndef PB_FBX_CONV_DUMPFBX_H
#define PB_FBX_CONV_DUMPFBX_H

#include "ofbx.h"

void dumpElements(const ofbx::IScene *scene);
void dumpElementRecursive(FILE *file, const ofbx::IElement *element, int indent = 2);
void dumpElement(FILE *file, const ofbx::IElement *element, int indent = 2);

void dumpObjects(const ofbx::IScene *scene);
void dumpObjectRecursive(FILE *file, const ofbx::Object *obj, int indent = 2);
void dumpObject(FILE *file, const ofbx::Object *obj, int indent = 2);

void dumpNodes(const ofbx::IScene *scene);
void dumpNodeRecursive(FILE *file, const ofbx::Object *obj, int indent = 2);

#endif //PB_FBX_CONV_DUMPFBX_H
