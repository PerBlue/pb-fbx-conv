//
// Created by Martin Wickham on 8/26/17.
//

#ifndef PB_FBX_CONV_MODEL_H
#define PB_FBX_CONV_MODEL_H

#include <string>
#include <vector>
#include <cassert>
#include "types.h"

#define ATTR_POSITION       (1<<0)
#define ATTR_NORMAL         (1<<1)
#define ATTR_COLOR          (1<<2)
#define ATTR_COLORPACKED    (1<<3)
#define ATTR_TANGENT        (1<<4)
#define ATTR_BINORMAL       (1<<5)
#define ATTR_TEXCOORD0      (1<<6)
#define ATTR_TEXCOORD1      (1<<7)
#define ATTR_TEXCOORD2      (1<<8)
#define ATTR_TEXCOORD3      (1<<9)
#define ATTR_TEXCOORD4      (1<<10)
#define ATTR_TEXCOORD5      (1<<11)
#define ATTR_TEXCOORD6      (1<<12)
#define ATTR_TEXCOORD7      (1<<13)
#define ATTR_BLENDWEIGHT0   (1<<14)
#define ATTR_BLENDWEIGHT1   (1<<15)
#define ATTR_BLENDWEIGHT2   (1<<16)
#define ATTR_BLENDWEIGHT3   (1<<17)
#define ATTR_BLENDWEIGHT4   (1<<18)
#define ATTR_BLENDWEIGHT5   (1<<19)
#define ATTR_BLENDWEIGHT6   (1<<20)
#define ATTR_BLENDWEIGHT7   (1<<21)
#define ATTR_MAX            (1<<22)
#define MAX_TEX_COORDS      8
#define MAX_BLEND_WEIGHTS   8
// Adding a new attribute? Don't forget to update...
// - calculateVertexSize(Attributes)
typedef u32 Attributes;

struct Texture {
    std::string id;
    std::string texturePath;
    f32 uvTranslation[2] = {0, 0};
    f32 uvScale[2] = {1, 1};
};

struct ModelMaterial {
    std::string id;
    f32 ambient[3]  = {1,1,1};
    f32 diffuse[3]  = {1,1,1};
    f32 specular[3] = {0,0,0};
    f32 emissive[3] = {0,0,0};
    f32 shininess = 0;
    f32 opacity = 1;
    std::vector<Texture> textures;
};

struct MeshPart {
    std::string id;
    std::vector<u16> indices;
    u32 primitive;
};

struct ModelMesh {
    Attributes attributes;
    u16 vertexSize;
    std::vector<float> vertices;
    std::vector<MeshPart> parts;
};

struct NodePart {
    MeshPart *meshPart;
    ModelMaterial *material;
};

struct Node {
    std::string id;
    std::vector<NodePart> parts;
    std::vector<Node> children;
    float translation[3];
    float rotation[4];
    float scale[3];
};

struct Animation {
    std::string id;
    u32 stride;
    u32 frames;
    std::vector<Node *> nodes;
    std::vector<u32> nodeFormats;
    std::vector<f32> nodeData;
};

struct Model {
    std::string id;
    std::vector<Animation> animations;
    std::vector<ModelMaterial> materials;
    std::vector<ModelMesh> meshes;
    std::vector<Node> nodes;
};

inline u16 calculateVertexSize(Attributes attributes) {
#define checkAttribute(attr, s) if (attributes & (attr)) { size += (s); } else {}
    u16 size = 0;
    checkAttribute(ATTR_POSITION, 3);
    checkAttribute(ATTR_NORMAL, 3);
    checkAttribute(ATTR_COLOR, 4);
    checkAttribute(ATTR_COLORPACKED, 1);
    checkAttribute(ATTR_TANGENT, 3);
    checkAttribute(ATTR_BINORMAL, 3);
    for (int c = 0; c < 8; c++) {
        checkAttribute(ATTR_TEXCOORD0 << c, 2);
    }
    for (int c = 0; c < 8; c++) {
        checkAttribute(ATTR_BLENDWEIGHT0 << c, 2);
    }
    return size;
#undef checkAttribute
}

inline u16 calculateVertexOffset(Attributes attributes, Attributes attribute) {
    assert(attribute != 0 && (attribute & (attribute - 1)) == 0); // ensure attribute is a power of 2
    Attributes attrsBefore = attributes & (attribute - 1);
    return calculateVertexSize(attrsBefore);
}

#endif //PB_FBX_CONV_MODEL_H
