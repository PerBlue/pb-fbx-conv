//
// Created by Martin Wickham on 8/26/17.
//

#include <cmath>
#include <sstream>
#include "convertfbx.h"
#include "dumpfbx.h"

using namespace ofbx;


// ---------------------- Utility ------------------------

const int bufsize = 128;

static void dumpMatrix(Matrix mat) {
    for (int n = 0; n < 4; n++) {
        printf("%.2f, %.2f, %.2f, %.2f\n", mat.m[n+0], mat.m[n+4], mat.m[n+8], mat.m[n+12]);
    }
}

static ModelMesh *findOrCreateMesh(Model *model, Attributes attributes, u32 vertexCount) {
    // try to find an existing mesh with the same attributes that has room for the vertices
    for (ModelMesh &mesh : model->meshes) {
        if (mesh.attributes == attributes &&
            mesh.vertices.size() / mesh.vertexSize + vertexCount < 65536) {
            return &mesh;
        }
    }
    // didn't find a mesh, let's make one.
    model->meshes.emplace_back();
    ModelMesh *mesh = &model->meshes.back();
    mesh->attributes = attributes;
    mesh->vertexSize = calculateVertexSize(attributes);
    return mesh;
}

static int hashVertex(float *vert, int vertSize) {
    int h = 0;
    for (int c = 0; c < vertSize; c++) {
        h += 31 * *(int *) &vert[c];
    }
    return h;
}

static void findName(const Object *node, const char *type, std::string &out) {
    if (node->name[0]) out = &node->name[0];
    else {
        std::ostringstream stream;
        stream << type << node->id;
        stream.str(out);
    }
}

static const IElementProperty *findNonemptyStringProperty(const IElementProperty *prop, char(&buf)[bufsize]) {
    while (prop != nullptr) {
        if (prop->getType() == IElementProperty::STRING) {
            prop->getValue().toString(buf);
            if (buf[0]) return prop;
        }
        prop = prop->getNext();
    }
    return prop;
}

static const IElementProperty *findDoubleProperty(const IElementProperty *prop) {
    while (prop != nullptr) {
        if (prop->getType() == IElementProperty::DOUBLE) {
            return prop;
        }
        prop = prop->getNext();
    }
    return prop;
}

// attempts to get n floats from prop to put in buf, returns actual number retrieved.
static int getFloats(float *buf, int num, const IElementProperty *prop) {
    int c = 0;
    prop = findDoubleProperty(prop);
    while (c < num && prop) {
        buf[c++] = (float) prop->getValue().toDouble();
        prop = findDoubleProperty(prop->getNext());
    }
    return c;
}


// ---------------------- Materials ------------------------

// There are two ways to set each of these properties (WLOG we use Ambient as an example):
// 1) specify AmbientColor and, optionally, AmbientFactor
// 2) specify Ambient, which is AmbientColor * AmbientFactor
// The problem is that some exporters (including Maya 2016) export all three attributes.
// If the order is AmbientColor, Ambient, AmbientFactor, the factor will effectively be multiplied twice.
// To avoid this, we set the ML_AMBIENT flag when processing the Ambient attribute,
// and ignore the AmbientColor and AmbientFactor attributes when ML_AMBIENT is set.
const int ML_EMISSIVE = 1<<0;
const int ML_AMBIENT  = 1<<1;
const int ML_DIFFUSE  = 1<<2;
const int ML_SPECULAR = 1<<3;
struct MaterialLoading {
    bool lambertOnly = false;
    int flags = 0;

    // lambert vars
    float emissiveColor[3] = {0,0,0};
    float emissiveFactor = 1;
    float ambientColor[3] = {0,0,0};
    float ambientFactor = 1;
    float diffuseColor[3] = {1,1,1};
    float diffuseFactor = 1;
    float opacity = 1;

    // phong vars
    float specularColor[3] = {0,0,0};
    float specularFactor = 1;
    float shininess = 0;
    float shininessExponent = 0;
};

static void applyMaterialAttribute(const char *name, const IElementProperty *prop, MaterialLoading *out) {
    float buf[4];

    #define loadAll(str, field, flag) \
        do { if ((out->flags & (flag)) == 0) { \
            if (strcasecmp(name, str "Color") == 0) { \
                if (getFloats(buf, 3, prop) == 3) { \
                    memcpy(out->field##Color, buf, 3 * sizeof(float)); \
                } \
                return; \
            } else if (strcasecmp(name, str "Factor") == 0) { \
                prop = findDoubleProperty(prop); \
                if (prop) out->field##Factor = (float) prop->getValue().toDouble(); \
                return; \
            } else if (strcasecmp(name, str) == 0) { \
                if (getFloats(buf, 3, prop) == 3) { \
                    memcpy(out->field##Color, buf, 3 * sizeof(float)); \
                    out->field##Factor = 1; \
                    out->flags |= (flag); \
                } \
                return; \
            } \
        } } while (0)

    loadAll("Ambient", ambient, ML_AMBIENT);
    loadAll("Diffuse", diffuse, ML_DIFFUSE);
    loadAll("Specular", specular, ML_SPECULAR);
    loadAll("Emissive", emissive, ML_EMISSIVE);

    if (strcasecmp(name, "Shininess") == 0) {
        prop = findDoubleProperty(prop);
        if (prop) out->shininess = (float) prop->getValue().toDouble();
        return;
    }

    if (strcasecmp(name, "ShininessExponent") == 0) {
        prop = findDoubleProperty(prop);
        if (prop) out->shininessExponent = (float) prop->getValue().toDouble();
        return;
    }

    if (strcasecmp(name, "Opacity") == 0) {
        prop = findDoubleProperty(prop);
        if (prop) out->opacity = (float) prop->getValue().toDouble();
        return;
    }

    #undef loadAll
}

static void processMaterialNode(const IElement *mat, MaterialLoading *out) {
    char name[bufsize];
    mat->getID().toString(name);

    if (strcasecmp(name, "ShadingModel") == 0) {
        const IElementProperty *prop = mat->getFirstProperty();
        prop = findNonemptyStringProperty(prop, name);
        if (prop) {
            out->lambertOnly = strcasecmp(name, "Lambert") == 0;
        }

    } else if (strcasecmp(name, "P") == 0) {
        const IElementProperty *prop = mat->getFirstProperty();
        prop = findNonemptyStringProperty(prop, name);
        if (prop) {
            applyMaterialAttribute(name, prop, out);
        }
    }
}

static void processMaterialNodeRecursive(const IElement *mat, MaterialLoading *out) {
    while (mat) {
        processMaterialNode(mat, out);
        processMaterialNodeRecursive(mat->getFirstChild(), out);
        mat = mat->getSibling();
    }
}

static void convertMaterial(const Material *mat, ModelMaterial *out) {
    char buffer[bufsize];

    MaterialLoading loading;

    const IElement *matElm = &mat->element;
    processMaterialNode(matElm, &loading);
    processMaterialNodeRecursive(matElm->getFirstChild(), &loading);

    findName(mat, "Material", out->id);
    out->lambertOnly = loading.lambertOnly;
    out->ambient[0] = loading.ambientColor[0] * loading.ambientFactor;
    out->ambient[1] = loading.ambientColor[1] * loading.ambientFactor;
    out->ambient[2] = loading.ambientColor[2] * loading.ambientFactor;
    out->diffuse[0] = loading.diffuseColor[0] * loading.diffuseFactor;
    out->diffuse[1] = loading.diffuseColor[1] * loading.diffuseFactor;
    out->diffuse[2] = loading.diffuseColor[2] * loading.diffuseFactor;
    out->emissive[0] = loading.emissiveColor[0] * loading.emissiveFactor;
    out->emissive[1] = loading.emissiveColor[1] * loading.emissiveFactor;
    out->emissive[2] = loading.emissiveColor[2] * loading.emissiveFactor;
    out->opacity = loading.opacity;
    if (!loading.lambertOnly) {
        out->specular[0] = loading.specularColor[0] * loading.specularFactor;
        out->specular[1] = loading.specularColor[1] * loading.specularFactor;
        out->specular[2] = loading.specularColor[2] * loading.specularFactor;
        out->shininess = loading.shininessExponent == 0 ? loading.shininess : loading.shininessExponent;
    }

    const Texture *diffuseTex = mat->getTexture(Texture::DIFFUSE);
    if (diffuseTex) {
        ModelTexture tex;
        findName(diffuseTex, "Texture", tex.id);
        diffuseTex->getFileName().toString(buffer);
        tex.texturePath = &buffer[0];
        tex.usage = USAGE_DIFFUSE;
        out->textures.push_back(std::move(tex));
    }

    const Texture *normalTex = mat->getTexture(Texture::NORMAL);
    if (normalTex) {
        ModelTexture tex;
        findName(normalTex, "Texture", tex.id);
        normalTex->getFileName().toString(buffer);
        tex.texturePath = &buffer[0];
        tex.usage = USAGE_NORMAL;
        out->textures.push_back(std::move(tex));
    }
}


// ---------------------- Blend Weights ------------------------

struct BlendWeight {
    u32 index;
    f32 weight;
};

static int computeBlendWeightCount(const Skin *skin, int nVerts) {
    int *refCounts = new int[nVerts];
    memset(refCounts, 0, nVerts * sizeof(int));
    int nCluster = skin->getClusterCount();
    for (int c = 0; c < nCluster; c++) {
        const Cluster *cluster = skin->getCluster(c);
        // not sure why these are separate, they seem to always be the same. Min just in case.
        int nWeights = cluster->getWeightsCount();
        int nIndices = cluster->getIndicesCount();
        int nPoints = nWeights < nIndices ? nWeights : nIndices;

        const int *indices = cluster->getIndices();
        const double *weights = cluster->getWeights();
        for (int d = 0; d < nPoints; d++) {
            int index = indices[d];
            if (u32(index) < nVerts && weights[d] != 0) {
                refCounts[index]++;
            }
        }
    }
    int max = 0;
    for (int c = 0; c < nVerts; c++) {
        if (refCounts[c] > max) max = refCounts[c];
    }
    printf("Max blend weights: %d\n", max);
    return max;
}

static BlendWeight *computeBlendWeights(const Skin *skin, int nVertWeights, int nVerts) {
    int totalWeights = nVertWeights * nVerts;
    BlendWeight *data = new BlendWeight[totalWeights];
    memset(data, 0, sizeof(BlendWeight) * totalWeights);

    int nCluster = skin->getClusterCount();
    for (int c = 0; c < nCluster; c++) {
        const Cluster *cluster = skin->getCluster(c);
        // not sure why these are separate, they seem to always be the same. Min just in case.
        int nWeights = cluster->getWeightsCount();
        int nIndices = cluster->getIndicesCount();
        int nPoints = nWeights < nIndices ? nWeights : nIndices;

        const int *indices = cluster->getIndices();
        const double *weights = cluster->getWeights();
        for (int d = 0; d < nPoints; d++) {
            int index = indices[d];
            if (u32(index) >= nVerts) continue;

            double weight = weights[d];
            if (weight == 0) continue;

            BlendWeight *vertWeights = &data[index * nVertWeights];
            // find the minimum current weight
            int minWeightIdx = 0;
            for (int e = 1; e < nVertWeights; e++) {
                if (fabs(vertWeights[e].weight) < fabs(vertWeights[minWeightIdx].weight)) {
                    minWeightIdx = e;
                }
            }

            // replace with the new weight if greater
            if (fabs(weight) > fabs(vertWeights[minWeightIdx].weight)) {
                vertWeights[minWeightIdx].weight = f32(weight);
                vertWeights[minWeightIdx].index = u32(c);
            }
        }
    }

    // normalize weights
    BlendWeight *vertWeights = data;
    for (int c = 0; c < nVerts; c++, vertWeights += nVertWeights) {
        float sum = 0;
        for (int d = 0; d < nVertWeights; d++) {
            sum += vertWeights[d].weight;
        }
        if (sum != 0) {
            for (int d = 0; d < nVertWeights; d++) {
                vertWeights[d].weight /= sum;
            }
        }
    }

    return data;
}



static void convertMeshes(const IScene *scene, Model *model, Options *opts) {
    int nMesh = scene->getMeshCount();
    for (int c = 0; c < nMesh; c++) {
        const Mesh *mesh = scene->getMesh(c);
        if (opts->dumpMeshes) dumpObject(stdout, mesh);

        const Geometry *geom = mesh->getGeometry();
        if (opts->dumpGeom) dumpObject(stdout, geom);

        dumpMatrix(geom->getGlobalTransform());
        int nVerts = geom->getVertexCount();

        const Vec3 *positions = geom->getVertices();
        const Vec3 *normals = geom->getNormals();
        const Vec2 *texCoords = geom->getUVs();
        const Vec4 *colors = geom->getColors();
        const Vec3 *tangents = geom->getTangents();
        const Skin *skin = geom->getSkin();
        int nBlendWeights = 0;
        BlendWeight *blendWeights = nullptr;

        Attributes attrs = 0;
        if (positions) attrs |= ATTR_POSITION;
        if (normals)   attrs |= ATTR_NORMAL;
        if (colors)    attrs |= ATTR_COLOR; // TODO: Pack colors
        if (tangents)  attrs |= ATTR_TANGENT;
        if (texCoords) attrs |= ATTR_TEXCOORD0;
        if (skin) {
            nBlendWeights = computeBlendWeightCount(skin, nVerts);
            if (nBlendWeights > MAX_BLEND_WEIGHTS) nBlendWeights = MAX_BLEND_WEIGHTS;
            if (nBlendWeights > 0) {
                for (int c = 0; c < nBlendWeights; c++) {
                    attrs |= ATTR_BLENDWEIGHT0 << c;
                }
                blendWeights = computeBlendWeights(skin, nBlendWeights, nVerts);
            }
        }

        int nMaterials = mesh->getMaterialCount();
        for (int c = 0; c < nMaterials; c++) {
            const Material *mat = mesh->getMaterial(c);
            if (opts->dumpMaterials) {
                dumpObject(stdout, mat);
            }
            model->materials.emplace_back();
            ModelMaterial *modelMaterial = &model->materials.back();
            convertMaterial(mat, modelMaterial);
        }


        ModelMesh *outMesh = findOrCreateMesh(model, attrs, nVerts);
        std::vector<float> *verts = &outMesh->vertices;
        verts->reserve(verts->size() + nVerts * outMesh->vertexSize);


        delete [] blendWeights; // delete [] nullptr is defined and has no effect.
    }
}

bool convertFbxToModel(const IScene *scene, Model *model, Options *opts) {
    convertMeshes(scene, model, opts);
    return true;
}
