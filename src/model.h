//
// Created by Martin Wickham on 8/26/17.
//

#ifndef PB_FBX_CONV_MODEL_H
#define PB_FBX_CONV_MODEL_H

#include <string>
#include <vector>
#include "types.h"

struct Texture {
    std::string id;
    std::string texturePath;
    f32 uvTranslation[2] = {0, 0};
    f32 uvScale[2] = {1, 1};
};

struct Material {
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

struct Mesh {
    u64 attributes;
    u16 vertexSize;
    std::vector<float> vertices;
    std::vector<MeshPart> parts;
};

struct NodePart {
    MeshPart *meshPart;
    Material *material;
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
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Node> nodes;
};


#endif //PB_FBX_CONV_MODEL_H
