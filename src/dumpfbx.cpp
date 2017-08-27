//
// Created by Martin Wickham on 8/26/17.
//

#include <cstdio>
#include <cstring>
#include "dumpfbx.h"

using namespace ofbx;

const int indentation = 2;
const size_t elbuffersize = 256;

static void catProperty(char str[elbuffersize], const IElementProperty *prop) {
    char tmp[elbuffersize];
    IElementProperty::Type type = prop->getType();
    DataView value = prop->getValue();
    switch (type) {
        case IElementProperty::DOUBLE: snprintf(tmp, elbuffersize, "%5f", value.toDouble()); break;
        case IElementProperty::LONG: snprintf(tmp, elbuffersize, "%lld", value.toLong()); break;
        case IElementProperty::INTEGER: snprintf(tmp, elbuffersize, "%d", *(int*)value.begin); break;
        case IElementProperty::STRING: prop->getValue().toString(tmp); break;
        default: snprintf(tmp, elbuffersize, "Type: %c [%d bytes]", type, int(value.end - value.begin));
    }
    strncat(str, tmp, elbuffersize - strlen(str) - 1);
}

// This has the schlemiel the painter problem, but it should be fast enough regardless.
static void dumpElement(FILE *file, const IElement *e, int indent) {
    char str[elbuffersize];
    int c = 0;
    for (; c < indent; c++) {
        str[c] = ' ';
    }
    e->getID().toString(str + c, elbuffersize - c); // adds a null at the end
    const IElementProperty *prop = e->getFirstProperty();
    if (prop) {
        strncat(str, " (", elbuffersize - strlen(str) - 1);
        catProperty(str, prop);
        prop = prop->getNext();
        while (prop) {
            strncat(str, ", ", elbuffersize - strlen(str) - 1);
            catProperty(str, prop);
            prop = prop->getNext();
        }
        strncat(str, ")", elbuffersize - strlen(str) - 1);
    }
    fprintf(file, "%s\n", str);
}

static void dumpElementRecursive(FILE *file, const IElement *e, int indent = indentation) {
    while (e) {
        dumpElement(file, e, indent);
        dumpElementRecursive(file, e->getFirstChild(), indent + indentation);
        e = e->getSibling();
    }
}

void dumpFbx(const IScene *scene) {
    FILE *file = fopen("tree.out", "w");
    if (file != nullptr) {
        printf("Dumping FBX tree to tree.out\n");
    } else {
        printf("Could not open file 'tree.out'. Continuing.");
        return;
    }
    fprintf(file, "Elements:\n");
    const IElement *rootElement = scene->getRootElement();
    dumpElementRecursive(file, rootElement);
    fclose(file);
}