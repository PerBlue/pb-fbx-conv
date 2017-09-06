//
// Created by Martin Wickham on 8/30/17.
//

#include "writep3db.h"

#include "ofbx.h"

#pragma warning(push, 0)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#include "json/BaseJSONWriter.h"
#include "json/JSONWriter.h"
#include "json/UBJSONWriter.h"
#include "args.h"

#pragma clang diagnostic pop
#pragma warning(pop)

using namespace std;
using namespace ofbx;
using namespace json;

static int countSetBits(Attributes attributes) {
    attributes &= ATTR_MAX - 1;
    attributes = attributes - ((attributes >> 1) & 0x55555555);
    attributes = (attributes & 0x33333333) + ((attributes >> 2) & 0x33333333);
    return (((attributes + (attributes >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

static void writeAttributes(Attributes attributes, BaseJSONWriter &writer) {
    writer.arr(countSetBits(attributes), 1000);
    for (int f = 1, c = 0; f < ATTR_MAX; f <<= 1, c++) {
        if (f & attributes) {
            writer.val(attributeNames[c]);
        }
    }
    writer.end();
}

static void writeMeshPart(MeshPart *part, BaseJSONWriter &writer) {
    writer.obj(3);
    writer << "id" = part->id;
    writer << "type" = primitiveNames[part->primitive];
    writer.val("indices").data(part->indices, 12);
    writer.end();
}

static void writeMesh(ModelMesh *mesh, BaseJSONWriter &writer) {
    writer.obj(3);

    writer.val("attributes");
    writeAttributes(mesh->attributes, writer);

    writer.val("vertices").data(mesh->vertices, mesh->vertexSize);

    writer.val("parts").arr(mesh->parts.size());
    for (MeshPart &part : mesh->parts) {
        writeMeshPart(&part, writer);
    }
    writer.end();

    writer.end();
}

static void writeTexture(ModelTexture *tex, BaseJSONWriter &writer) {
    writer.obj();
    writer << "id" = tex->id;
    writer << "filename" = tex->texturePath;
    if (tex->uvTranslation[0] != 0 || tex->uvTranslation[1] != 0) {
        writer << "uvtranslation" = tex->uvTranslation;
    }
    if (tex->uvScale[0] != 1 || tex->uvScale[1] != 1) {
        writer << "uvscaling" = tex->uvScale;
    }
    writer << "type" = textureTypes[tex->usage];
    writer.end();
}

static void writeMaterial(ModelMaterial *mat, BaseJSONWriter &writer) {
    writer.obj();
    writer << "id" = mat->id;
    if (mat->ambient[0] != 1 || mat->ambient[1] != 1 || mat->ambient[2] != 1) {
        writer << "ambient" = mat->ambient;
    }
    if (mat->diffuse[0] != 1 || mat->diffuse[1] != 1 || mat->diffuse[2] != 1) {
        writer << "diffuse" = mat->diffuse;
    }
    if (mat->emissive[0] != 0 || mat->emissive[1] != 0 || mat->emissive[2] != 0) {
        writer << "emissive" = mat->emissive;
    }
    if (mat->opacity != 1) {
        writer << "opacity" = mat->opacity;
    }
    if (!mat->lambertOnly) {
        if (mat->specular[0] != 0 || mat->specular[1] != 0 || mat->specular[2] != 0) {
            writer << "specular" = mat->specular;
        }
        if (mat->shininess != 0) {
            writer << "shininess" = mat->shininess;
        }
    }
    if (!mat->textures.empty()) {
        writer.val("textures").arr(mat->textures.size());
        for (ModelTexture &texture : mat->textures) {
            writeTexture(&texture, writer);
        }
        writer.end();
    }
    writer.end();
}

static void writeNodePart(NodePart *part, BaseJSONWriter &writer) {
    writer.obj();
    writer << "meshpartid" = part->meshPartID;
    writer << "materialid" = part->materialID;
    if (!part->bones.empty()) {
        writer.val("bones").arr(part->bones.size());
        for (BoneBinding &binding : part->bones) {
            writer.obj(4);
            writer << "node" = binding.nodeID;
            writer << "translation" = binding.translation;
            writer << "rotation" = binding.rotation;
            writer << "scale" = binding.scale;
            writer.end();
        }
        writer.end();
    }
    writer.end();
}

static void writeNodeRecursive(Node *node, BaseJSONWriter &writer) {
    writer.obj();

    writer << "id" = node->id;
    if (node->rotation[0] != 0. || node->rotation[1] != 0. || node->rotation[2] != 0. || node->rotation[3] != 1.)
        writer << "rotation" = node->rotation;
    if (node->scale[0] != 1. || node->scale[1] != 1. || node->scale[2] != 1.)
        writer << "scale" = node->scale;
    if (node->translation[0] != 0. || node->translation[1] != 0. || node->translation[2] != 0.)
        writer << "translation" = node->translation;
    if (!node->parts.empty()) {
        writer.val("parts").arr(node->parts.size());
        for (NodePart &part : node->parts) {
            writeNodePart(&part, writer);
        }
        writer.end();
    }
    if (!node->children.empty()) {
        writer.val("children").arr(node->children.size());
        for (Node &child : node->children) {
            writeNodeRecursive(&child, writer);
        }
        writer.end();
    }

    writer.end();
}

static void writeG3dAnimation(Animation *anim, BaseJSONWriter &writer) {
    writer.obj(2);
    writer << "id" = anim->id;
    u32 nNodes = anim->nodeIDs.size();
    u32 nFrames = anim->frames;
    writer.val("bones").arr(nNodes);
    for (int c = 0; c < nNodes; c++) {
        writer.obj(2);
        writer << "boneId" = anim->nodeIDs[c];
        writer.val("keyframes").arr(nFrames);

        int frameSize = anim->stride;
        float *frameData = anim->nodeData.data();
        int toff = anim->nodeFormats[3*c + 0];
        int roff = anim->nodeFormats[3*c + 1];
        int soff = anim->nodeFormats[3*c + 2];
        for (int d = 0; d < nFrames; d++) {
            writer.obj();
            writer << "keytime" = (d * anim->samplingRate * 1000);
            if (roff >= 0) writer.val("rotation").data(&frameData[roff], 4);
            if (toff >= 0) writer.val("translation").data(&frameData[toff], 3);
            if (soff >= 0) writer.val("scale").data(&frameData[soff], 3);
            writer.end();
            frameData += frameSize;
        }

        writer.end(); // keyframes[]
        writer.end(); // {}
    }
    writer.end(); // bones[]
    writer.end(); // {}
}

static void writeP3dAnimation(Animation *anim, BaseJSONWriter &writer) {
    writer.obj(7);
    writer << "id" = anim->id;
    writer << "duration" = (anim->samplingRate * (anim->frames - 1));
    writer << "frames" = anim->frames;
    writer << "bones" = anim->nodeIDs;
    writer << "formats" = anim->nodeFormats;
    writer << "stride" = anim->stride;
    writer << "data" = anim->nodeData;
    writer.end();
}

static void writeModel(Model *model, BaseJSONWriter &writer, bool pbAnimations) {
    writer.obj(6);
    short version[2] = {0, 1};
    writer << "version" = version;
    writer << "id" = model->id;

    writer.val("meshes").arr(model->meshes.size());
    for (ModelMesh &mesh : model->meshes) {
        writeMesh(&mesh, writer);
    }
    writer.end();

    writer.val("materials").arr(model->materials.size());
    for (ModelMaterial &mat : model->materials) {
        writeMaterial(&mat, writer);
    }
    writer.end();

    writer.val("nodes").arr(model->nodes.size());
    for (Node &node : model->nodes) {
        writeNodeRecursive(&node, writer);
    }
    writer.end();

    writer.val("animations").arr(model->animations.size());
    for (Animation &anim : model->animations) {
        if (pbAnimations) writeP3dAnimation(&anim, writer);
        else              writeG3dAnimation(&anim, writer);
    }
    writer.end();

    writer.end();
}

bool writeP3db(Model *model, const char *filename, bool pbAnimations) {
    ofstream out(filename);
    if (!out) {
        printf("Error: Failed to open output file %s for writing.\n", filename);
        return false;
    }
    UBJSONWriter writer(out);

    writeModel(model, writer, pbAnimations);

    return true;
}

bool writeP3dj(Model *model, const char *filename, bool pbAnimations) {
    ofstream out(filename);
    if (!out) {
        printf("Error: Failed to open output file %s for writing.\n", filename);
        return false;
    }
    JSONWriter writer(out);

    writeModel(model, writer, pbAnimations);

    return true;
}
