//
// Created by Martin Wickham on 8/26/17.
//

#ifndef PB_FBX_CONV_MODEL_H
#define PB_FBX_CONV_MODEL_H

#include <string>
#include <vector>
#include <cassert>
#include "types.h"

namespace ofbx {
    struct Object;
}

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

static const char *attributeNames[] = {
        "POSITION", "NORMAL", "COLOR", "COLORPACKED", "TANGENT", "BINORMAL",
        "TEXCOORD0", "TEXCOORD1", "TEXCOORD2", "TEXCOORD3", "TEXCOORD4", "TEXCOORD5", "TEXCOORD6", "TEXCOORD7",
        "BLENDWEIGHT0", "BLENDWEIGHT1", "BLENDWEIGHT2", "BLENDWEIGHT3", "BLENDWEIGHT4", "BLENDWEIGHT5", "BLENDWEIGHT6", "BLENDWEIGHT7"
};

#define USAGE_NONE           1
#define USAGE_DIFFUSE        2
#define USAGE_EMISSIVE       3
#define USAGE_AMBIENT        4
#define USAGE_SPECULAR       5
#define USAGE_SHININESS      6
#define USAGE_NORMAL         7
#define USAGE_BUMP           8
#define USAGE_TRANSPARENCY   9
#define USAGE_REFLECTION    10
typedef u32 Usage;

static const char *textureTypes[] = {
        "NONE", "NONE", "DIFFUSE", "EMISSIVE", "AMBIENT", "SPECULAR",
        "SHININESS", "NORMAL", "BUMP", "TRANSPARENCY", "REFLECTION"
};

#define PRIMITIVETYPE_POINTS		0
#define PRIMITIVETYPE_LINES			1
#define PRIMITIVETYPE_LINESTRIP		2
#define PRIMITIVETYPE_TRIANGLES		3
#define PRIMITIVETYPE_TRIANGLESTRIP	4

static const char *primitiveNames[] = {
        "POINTS", "LINES", "LINE_STRIP", "TRIANGLES", "TRIANGLE_STRIP"
};

#define MAX_DRAW_BONES 32

struct ModelTexture {
    std::string id;
    std::string texturePath;
    f32 uvTranslation[2] = {0, 0};
    f32 uvScale[2] = {1, 1};
    Usage usage;
};

struct ModelMaterial {
    std::string id;
    bool lambertOnly = false;
    f32 ambient[3]  = {1,1,1};
    f32 diffuse[3]  = {1,1,1};
    f32 specular[3] = {0,0,0};
    f32 emissive[3] = {0,0,0};
    f32 shininess = 0;
    f32 opacity = 1;
    std::vector<ModelTexture> textures;
};

struct MeshPart {
    std::string id;
    std::vector<u16> indices;
    u32 primitive;
};

struct ModelMesh {
    Attributes attributes;
    u16 vertexSize;
    int vertexHashes[65536]; // small enough that we can just put all of it here.
    std::vector<float> vertices;
    std::vector<MeshPart> parts;
};

struct BoneBinding {
    std::string nodeID;
    float translation[3];
    float rotation[4];
    float scale[3];
};

struct NodePart {
    std::string meshPartID;
    std::string materialID;
    std::vector<BoneBinding> bones;
};

struct Node {
    const ofbx::Object *source;
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
    f32 samplingRate;
    std::vector<std::string> nodeIDs;
    std::vector<s32> nodeFormats;
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

static int MAX_VERTEX_SIZE = calculateVertexSize(~Attributes(0));

#endif //PB_FBX_CONV_MODEL_H
