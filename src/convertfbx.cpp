//
// Created by Martin Wickham on 8/26/17.
//

#include <cmath>
#include <sstream>
#include <cstring>
#include <strings.h>
#include <cstdio>
#include <cstdlib>
#include "convertfbx.h"
#include "dumpfbx.h"
#include "mathutil.h"

using namespace ofbx;


// ---------------------- Utility ------------------------

const int bufsize = 128;

static void dumpMatrix(Matrix mat) {
    float translation[3], rotation[4], scale[3];
    extractTransform(&mat, translation, rotation, scale);
    printf("t %7.6f %7.6f %7.6f\n", translation[0], translation[1], translation[2]);
    printf("r %7.6f %7.6f %7.6f %7.6f\n", rotation[0], rotation[1], rotation[2], rotation[3]);
    printf("s %7.6f %7.6f %7.6f\n", scale[0], scale[1], scale[2]);

//    for (int n = 0; n < 4; n++) {
//        printf("%7.6f, %7.6f, %7.6f, %7.6f\n", mat.m[n+0], mat.m[n+4], mat.m[n+8], mat.m[n+12]);
//    }
}

static ModelMesh *findOrCreateMesh(Model *model, Attributes attributes, u32 vertexCount, int maxVertices) {
    // try to find an existing mesh with the same attributes that has room for the vertices
    for (ModelMesh &mesh : model->meshes) {
        if (mesh.attributes == attributes &&
            mesh.vertices.size() / mesh.vertexSize + vertexCount < maxVertices) {
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

static inline int hashVertex(float *vert, int vertSize) {
    int h = 0;
    for (int c = 0; c < vertSize; c++) {
        int bits = *(int *) &vert[c];
        h += 31 * (bits >> 8); // don't hash the bottom bits, we want loose equality checks.
    }
    return h;
}

static inline bool checkVertexEquality(float *a, float *b, int vertSize) {
    for (int c = 0; c < vertSize; c++) {
        int ia = *(int *) &a[c];
        int ib = *(int *) &b[c];
        if (ia >> 8 != ib >> 8) return false;
    }
    return true;
}

static void findName(const Object *node, const char *type, std::string &out) {
    if (node->name[0]) out = &node->name[0];
    else {
        std::stringstream stream;
        stream << type << node->id;
        out = stream.str();
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

struct PreMeshPart {
    int nodes[MAX_DRAW_BONES];
    int material;
    PreMeshPart(int material) : material(material) {
        memset(nodes, -1, sizeof(nodes));
    }
};

struct BlendWeight {
    s32 index = -1;
    f32 weight = 0;
};

struct MeshData {
    Options *opts;
    Attributes attrs;
    int nVerts;

    const Vec3 *positions;
    const Vec3 *normals;
    const Vec2 *texCoords;
    const Vec4 *colors;
    const Vec3 *tangents;
    const int *materials;
    const Skin *skin;
    int nBlendWeights = 0;
    int nDrawBones = 0;
    BlendWeight *blendWeights = nullptr;
    int *trisToParts = nullptr;
    std::vector<PreMeshPart> parts;
};


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

static void findFilename(const Texture *tex, std::string &out) {
    char buffer[bufsize];
    tex->getRelativeFileName().toString(buffer);
    if (!buffer[0]) tex->getFileName().toString(buffer);
    char *lastSlash = strrchr(buffer, '/');
    char *lastBack = strrchr(buffer, '\\');
    char *last = lastSlash > lastBack ? lastSlash : lastBack;
    if (last == nullptr) last = buffer;
    else last++;
    out = last;
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
        findFilename(diffuseTex, tex.texturePath);
        tex.usage = USAGE_DIFFUSE;
        out->textures.push_back(std::move(tex));
    }

    const Texture *normalTex = mat->getTexture(Texture::NORMAL);
    if (normalTex) {
        ModelTexture tex;
        findName(normalTex, "Texture", tex.id);
        findFilename(normalTex, tex.texturePath);
        tex.usage = USAGE_NORMAL;
        out->textures.push_back(std::move(tex));
    }
}

static int findMaterialForTri(const int *materials) {
    for (int k = 0; k < 3; k++) {
        if (materials[k] >= 0) {
            return materials[k];
        }
    }
    return 0; // use the default material, I guess.
}

static int *assignTrisToPartsFromMaterials(const int *materials, int nVerts, std::vector<PreMeshPart> &parts) {
    int nTris = nVerts / 3;
    int *trisToParts = new int[nTris];

    for (int c = 0; c < nTris; c++) {
        int material = findMaterialForTri(materials);
        int index = -1;
        for (PreMeshPart &part : parts) {
            if (part.material == material) {
                index = int(&part - &parts[0]);
                break;
            }
        }
        if (index < 0) {
            parts.emplace_back(material);
            index = int(parts.size() - 1);
        }
        trisToParts[c] = index;
        materials += 3;
    }

    return trisToParts;
}



// ---------------------- Blend Weights ------------------------

static int computeBlendWeightCount(const Skin *skin, int nVerts, int maxBlendWeights) {
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
    if (max > maxBlendWeights) {
        printf("Truncating number of blend weights from %d -> %d\n", max, maxBlendWeights);
        max = maxBlendWeights;
    }
    printf("Max blend weights: %d\n", max);
    return max;
}

static inline void normalizeBlendWeights(BlendWeight *weights, int nVertWeights) {
    float sum = 0;
    for (int d = 0; d < nVertWeights; d++) {
        sum += weights[d].weight;
    }
    if (sum != 0) {
        for (int d = 0; d < nVertWeights; d++) {
            weights[d].weight /= sum;
        }
    }
}

static BlendWeight *computeBlendWeights(const Skin *skin, int nVertWeights, int nVerts) {
    int totalWeights = nVertWeights * nVerts;
    BlendWeight *data = new BlendWeight[totalWeights];

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
            if (weight < 0.00001) continue;

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
        normalizeBlendWeights(vertWeights, nVertWeights);
    }

    return data;
}

struct PolyBlendWeight {
    s32 index = -1;
    f32 weight = 0;
};

static int polyCompare(const void *a, const void *b) {
    PolyBlendWeight *ba = (PolyBlendWeight *) a;
    PolyBlendWeight *bb = (PolyBlendWeight *) b;
    float result = bb->weight - ba->weight;
    return result < 0 ? -1 : 1;
}

static int findOrCreateMeshPart(std::vector<PreMeshPart> &parts, int material, BlendWeight *weights, int nVertWeights, int nDrawBones) {
    // build a list of the required nodes for this poly
    const int nPolyNodes = MAX_BLEND_WEIGHTS * 3;
    PolyBlendWeight polyNodes[nPolyNodes];

    int totalPolyWeights = nVertWeights * 3;
    for (int w = 0; w < totalPolyWeights; w++) {
        BlendWeight &weight = weights[w];
        int idx = weight.index;
        if (idx < 0) continue;

        // find or add the node id
        int i = 0;
        while (polyNodes[i].index >= 0 && polyNodes[i].index != idx) i++;
        polyNodes[i].index = idx;
        polyNodes[i].weight += weight.weight;
    }

    int maxIdx = 0;
    while (maxIdx < nPolyNodes && polyNodes[maxIdx].index >= 0) maxIdx++;

    // If we have more bones in this poly than nDrawBones, we need to cull some.
    if (maxIdx > nDrawBones) {
        qsort(polyNodes, maxIdx, sizeof(PolyBlendWeight), polyCompare);
        // zero any weights we've removed
        for (int c = nDrawBones; c < maxIdx; c++) {
            PolyBlendWeight &pw = polyNodes[c];
            for (int w = 0; w < totalPolyWeights; w++) {
                BlendWeight &vw = weights[w];
                if (vw.index == pw.index) {
                    vw.index = -1;
                    vw.weight = 0;
                }
            }
        }
        // renormalize the blend weights on our vertices
        normalizeBlendWeights(weights + 0*nVertWeights, nVertWeights);
        normalizeBlendWeights(weights + 1*nVertWeights, nVertWeights);
        normalizeBlendWeights(weights + 2*nVertWeights, nVertWeights);

        maxIdx = nDrawBones;
    }

    // Find an existing mesh part that can accept this set of weights and material
    for (PreMeshPart &part : parts) {
        if (part.material == material) {
            int combinedNodes[MAX_DRAW_BONES];
            memcpy(combinedNodes, part.nodes, sizeof(combinedNodes));

            for (int c = 0; c < maxIdx; c++) {
                PolyBlendWeight &p = polyNodes[c];
                int i = 0;
                while (i < nDrawBones && combinedNodes[i] >= 0 && combinedNodes[i] != p.index) i++;
                if (i >= nDrawBones) goto nextPart;
                combinedNodes[i] = p.index;
            }

            // If we get here, we successfully combined all the attributes!
            memcpy(part.nodes, combinedNodes, sizeof(combinedNodes));
            return int(&part - &parts[0]);
        }
    nextPart:;
    }

    // No existing mesh part that fits, time to make a new one.
    parts.emplace_back(material);
    PreMeshPart *part = &parts.back();
    for (int c = 0; c < maxIdx; c++) {
        part->nodes[c] = polyNodes[c].index;
    }

    return int(part - &parts[0]);
}

static int *assignTrisToPartsFromSkin(MeshData *data) {
    // TODO: This is a really nasty optimization problem that I'm going to ignore for now.
    // The problem is as follows:
    // We have a bunch of polygons; each polygon has a set of up to nDrawBones bones on which it depends.
    // We'd like to make a bunch of mesh parts. Each part has nVertWeight bones which it has available.
    // Find the minimum set of mesh parts such that the bones for any polygon are a subset of one of the parts.

    // My dumb greedy solution:
    // For each polygon,
    //   Look through all existing mesh parts for one in which the size of the union of the mesh part's bones and the polygon's bones is less than nVertWeights
    //   If there is such a mesh part, add any missing bones to it and assign the polygon to that part.
    //   Otherwise, make a new mesh part, set its bones to the polygon's bones, and assign the polygon to that part.

    int nVerts = data->nVerts;
    int nVertWeights = data->nBlendWeights;
    int nDrawBones = data->nDrawBones;
    BlendWeight *weights = data->blendWeights;
    const int *materials = data->materials;

    int nTris = nVerts / 3;
    int *trisToParts = new int[nTris];
    for (int c = 0; c < nTris; c++) {
        int material = 0;
        // find the material
        if (materials) {
            material = findMaterialForTri(materials);
        }

        trisToParts[c] = findOrCreateMeshPart(data->parts, material, weights, nVertWeights, nDrawBones);

        // move forward one vertex
        weights += nVertWeights * 3;
        if (materials) materials += 3;
    }

    printf("Packed %d triangles into %d mesh parts not exceeding %d bones.\n", nTris, int(data->parts.size()), nDrawBones);
    return trisToParts;
}

static void addBones(MeshData *data, PreMeshPart *part, NodePart *np, const Matrix *geometryMatrix) {
    if (part->nodes[0] < 0) return; // no bones
    const Skin *skin = data->skin;
    if (!skin) return;

    int maxBones = data->nDrawBones;
    int nBonesUsed = 0;
    while (nBonesUsed < maxBones && part->nodes[nBonesUsed] >= 0) nBonesUsed++;

    np->bones.resize(nBonesUsed);
    for (int c = 0; c < nBonesUsed; c++) {
        BoneBinding *bone = &np->bones[c];
        int clusterIndex = part->nodes[c];
        const Cluster *cluster = skin->getCluster(clusterIndex);
        const Object *link = cluster->getLink(); // the node which represents this bone
        assert(link->isNode());
        findName(link, "Node", bone->nodeID);

        // calculate the inverse bind pose
        // This is pretty much a total guess, but it produces the same results as the reference converter.
        Matrix clusterLinkTransform = cluster->getTransformLinkMatrix();
        Matrix invLinkTransform;
        invertMatrix(&clusterLinkTransform, &invLinkTransform);
        Matrix bindPose = mul(&invLinkTransform, geometryMatrix);
        Matrix invBindPose;
        invertMatrix(&bindPose, &invBindPose);
        extractTransform(&invBindPose, bone->translation, bone->rotation, bone->scale);
    }
}


// ---------------------- Vertices ------------------------

static inline void fetch(float *&pos, const Vec2 &vec) {
    pos[0] = (float) vec.x;
    pos[1] = (float) vec.y;
    pos += 2;
}
static inline void fetch(float *&pos, const Vec3 &vec) {
    pos[0] = (float) vec.x;
    pos[1] = (float) vec.y;
    pos[2] = (float) vec.z;
    pos += 3;
}
static inline void fetch(float *&pos, const Vec4 &vec) {
    pos[0] = (float) vec.x;
    pos[1] = (float) vec.y;
    pos[2] = (float) vec.z;
    pos[3] = (float) vec.w;
    pos += 4;
}

static void fetchVertex(MeshData *data, int vertexIndex, float *vertex, PreMeshPart *part) {
    int attrs = data->attrs;
    float *pos = vertex;
    if (attrs & ATTR_POSITION) {
        fetch(pos, data->positions[vertexIndex]);
    }

    if (attrs & ATTR_NORMAL) {
        fetch(pos, data->normals[vertexIndex]);
    }

    if (attrs & ATTR_COLOR) {
        fetch(pos, data->colors[vertexIndex]);
    }

    if (attrs & ATTR_COLORPACKED) {
        Vec4 color = data->colors[vertexIndex];
        u8 r = u8(color.x >= 1.0 ? 255 : color.x * 256.0);
        u8 g = u8(color.y >= 1.0 ? 255 : color.y * 256.0);
        u8 b = u8(color.z >= 1.0 ? 255 : color.z * 256.0);
        u8 a = u8(color.w >= 1.0 ? 255 : color.w * 256.0);
        u32 packed = u32(a)<<24 | u32(b)<<16 | u32(g)<<8 | u32(r);
        *pos++ = *(float *)&packed;
    }

    if (attrs & ATTR_TANGENT) {
        fetch(pos, data->tangents[vertexIndex]);
    }
    // TODO: Binormal

    // For now we only support one tex coord.
    if (attrs & ATTR_TEXCOORD0) {
        Vec2 tc = data->texCoords[vertexIndex];
        pos[0] = float(tc.x);
        if (data->opts->flipV) {
            pos[1] = float(1.0 - tc.y);
        } else {
            pos[1] = float(tc.y);
        }
        pos += 2;
    }

    int nVertWeights = data->nBlendWeights;
    int nDrawBones = data->nDrawBones;
    BlendWeight *weights = data->blendWeights + vertexIndex * nVertWeights;
    for (int c = 0; c < nVertWeights; c++) {
        BlendWeight weight = weights[c];
        if (weight.index < 0) {
            pos[0] = 0;
            pos[1] = 0;
        } else {
            int idx = weight.index;
            int c = 0;
            while (c < nDrawBones && part->nodes[c] != idx) c++;
            if (c >= nDrawBones) {
                assert(false);
                pos[0] = 0;
                pos[1] = 0;
            } else {
                pos[0] = (float) c;
                pos[1] = weight.weight;
            }
        }
        pos += 2;
    }
}

static u16 addVertex(ModelMesh *mesh, float *vertex) {
    int vSize = mesh->vertexSize;

    // hash and check for existing
    size_t end = mesh->vertices.size();
    u32 index = u32(end) / u32(vSize);
    assert (index < 65536);
    int hash = hashVertex(vertex, vSize);
    for (u32 c = 0, pos = 0; c < index; c++, pos += vSize) {
        if (mesh->vertexHashes[c] == hash) {
            if (checkVertexEquality(vertex, &mesh->vertices[pos], vSize)) {
                return u16(c);
            }
        }
    }

    // add a new vertex
    mesh->vertices.resize(end + vSize);
    float *newVertex = &mesh->vertices[end];
    memcpy(newVertex, vertex, vSize * sizeof(float));
    mesh->vertexHashes[index] = hash;

    return u16(index);
}

static void buildMesh(MeshData *data, ModelMesh *mesh, std::vector<u16> *indices, int partID, PreMeshPart *part) {
    float vertex[MAX_VERTEX_SIZE];
    int nTris = data->nVerts / 3;
    int *trisToParts = data->trisToParts;

    for (int c = 0, v = 0; c < nTris; c++, v += 3) {
        if (trisToParts != nullptr && trisToParts[c] != partID) continue;

        u16 index;

        fetchVertex(data, v+0, vertex, part);
        index = addVertex(mesh, vertex);
        indices->push_back(index);

        fetchVertex(data, v+1, vertex, part);
        index = addVertex(mesh, vertex);
        indices->push_back(index);

        fetchVertex(data, v+2, vertex, part);
        index = addVertex(mesh, vertex);
        indices->push_back(index);
    }
}



static void convertMeshNode(const IScene *scene, const Mesh *mesh, Node *node, Model *model, Options *opts) {
    if (opts->dumpMeshes) {
        dumpElement(stdout, &mesh->element, 2);
        dumpElementRecursive(stdout, mesh->element.getFirstChild(), 4);
    }

    const Geometry *geom = mesh->getGeometry();
    if (opts->dumpGeom) {
        dumpElement(stdout, &geom->element, 2);
        dumpElementRecursive(stdout, geom->element.getFirstChild(), 4);
    }

    MeshData data;

    data.opts = opts;
    data.nVerts = geom->getVertexCount();

    data.positions = geom->getVertices();
    data.normals = geom->getNormals();
    data.texCoords = geom->getUVs();
    data.colors = geom->getColors();
    data.tangents = geom->getTangents();
    data.materials = geom->getMaterials();
    data.skin = geom->getSkin();

    data.attrs = 0;
    if (data.positions) data.attrs |= ATTR_POSITION;
    if (data.normals)   data.attrs |= ATTR_NORMAL;
    if (data.colors) {
        if (opts->packVertexColors) data.attrs |= ATTR_COLORPACKED;
        else                        data.attrs |= ATTR_COLOR;
    }
    if (data.tangents)  data.attrs |= ATTR_TANGENT;
    if (data.texCoords) data.attrs |= ATTR_TEXCOORD0;
    if (data.skin) {
        data.nBlendWeights = computeBlendWeightCount(data.skin, data.nVerts, opts->maxBlendWeights);
        data.nDrawBones = opts->maxDrawBones;
        if (data.nBlendWeights > MAX_BLEND_WEIGHTS) data.nBlendWeights = MAX_BLEND_WEIGHTS;
        if (data.nBlendWeights > 0) {
            for (int c = 0; c < data.nBlendWeights; c++) {
                data.attrs |= ATTR_BLENDWEIGHT0 << c;
            }
            data.blendWeights = computeBlendWeights(data.skin, data.nBlendWeights, data.nVerts);
        }
    }

    if (data.blendWeights) {
        // handles null materials
        data.trisToParts = assignTrisToPartsFromSkin(&data);
    } else if (data.materials) {
        data.trisToParts = assignTrisToPartsFromMaterials(data.materials, data.nVerts, data.parts);
    } else {
        data.parts.emplace_back(0);
    }


    int nMaterials = mesh->getMaterialCount();
    size_t baseIdx = model->materials.size();
    if (nMaterials == 0) {
        printf("Warning: No materials for mesh %s. Generating default material.\n", &mesh->name[0]);
        model->materials.emplace_back();
        ModelMaterial *defaultMaterial = &model->materials.back();
        defaultMaterial->id = "PerFbx_Default_Material";
        defaultMaterial->lambertOnly = true;
    } else {
        model->materials.reserve(baseIdx + nMaterials);
        for (int c = 0; c < nMaterials; c++) {
            const Material *mat = mesh->getMaterial(c);
            if (opts->dumpMaterials) {
                dumpElement(stdout, &mat->element, 2);
                dumpElementRecursive(stdout, mat->element.getFirstChild(), 4);
            }
            model->materials.emplace_back();
            ModelMaterial *modelMaterial = &model->materials.back();
            convertMaterial(mat, modelMaterial);
        }
    }


    ModelMesh *outMesh = findOrCreateMesh(model, data.attrs, data.nVerts, opts->maxVertices);
    std::vector<float> *verts = &outMesh->vertices;
    verts->reserve(verts->size() + data.nVerts * outMesh->vertexSize);

    // reify the mesh parts
    Matrix geomTf = mesh->getGeometricMatrix();
    outMesh->parts.reserve(outMesh->parts.size() + data.parts.size());
    int partID = 0;
    for (PreMeshPart &part : data.parts) {
        // make a mesh part
        outMesh->parts.emplace_back();
        MeshPart &mp = outMesh->parts.back();

        // give it a name
        findName(mesh, "Model", mp.id);
        std::stringstream builder;
        builder << mp.id << '_' << partID;
        mp.id = builder.str();

        // build the vertices and indices
        mp.primitive = PRIMITIVETYPE_TRIANGLES;
        buildMesh(&data, outMesh, &mp.indices, partID, &part);

        // attach rendering info to the node
        node->parts.emplace_back();
        NodePart *np = &node->parts.back();
        np->meshPartID = mp.id;
        np->materialID = model->materials[baseIdx + part.material].id;
        addBones(&data, &part, np, &geomTf);

        partID++;
    }


    // delete [] nullptr is defined and has no effect.
    delete [] data.blendWeights;
    delete [] data.trisToParts;
}

static void convertNode(const IScene *scene, const Object *obj, Node *node, Model *model, Options *opts) {
    findName(obj, "Node", node->id);
    Matrix localTransform = obj->evalLocal(obj->getLocalTranslation(), obj->getLocalRotation());
    extractTransform(&localTransform, node->translation, node->rotation, node->scale);

    switch (obj->getType()) {
    case Object::Type::MESH:
        const Mesh *mesh = dynamic_cast<const Mesh *>(obj);
        convertMeshNode(scene, mesh, node, model, opts);
        break;
    // TODO: Other object types?
    }
}

static void convertChildrenRecursive(const IScene *scene, const Object *obj, Model *model, std::vector<Node> *nodeList, Options *opts) {
    const Object *child;
    for (int i = 0; (child = obj->resolveObjectLink(i)); i++) {
        if (child->isNode()) {
            nodeList->emplace_back();
            Node *node = &nodeList->back();
            node->source = child;
            convertNode(scene, child, node, model, opts);
            convertChildrenRecursive(scene, child, model, &node->children, opts);
        }
    }
}

static void collectNodesRecursive(std::vector<Node> &storage, std::vector<Node *> &nodes) {
    for (int c = 0, n = storage.size(); c < n; c++) {
        Node *node = &storage[c];
        nodes.push_back(node);
        collectNodesRecursive(node->children, nodes);
    }
}

static bool close(float *a, float *b, int len, float epsilon) {
    for (int c = 0; c < len; c++) {
        if (fabsf(a[c] - b[c]) >= epsilon) return false;
    }
    return true;
}

struct AnimatedNode {
    Node *modelNode;
    const AnimationCurveNode *translation;
    const AnimationCurveNode *rotation;
    const AnimationCurveNode *scale;
    s32 toff = -1;
    s32 roff = -1;
    s32 soff = -1;
    std::vector<float> data;
    s32 width;
    float *tdata = nullptr;
    float *rdata = nullptr;
    float *sdata = nullptr;
    bool needsT = false;
    bool needsR = false;
    bool needsS = false;
};

static void convertAnimations(const IScene *scene, Model *model, Options *opts) {
    std::vector<Node *> nodes;
    collectNodesRecursive(model->nodes, nodes);

    int nAnimation = scene->getAnimationStackCount();
    for (int c = 0; c < nAnimation; c++) {
        const AnimationStack *stack = scene->getAnimationStack(c);
        const TakeInfo *take = scene->getTakeInfo(stack->name);
        if (take == nullptr) {
            printf("Warning: Take info not found for animation %s\n", stack->name);
            continue;
        }
        double startTime = take->local_time_from;
        double timespan = take->local_time_to - startTime;
        if (timespan < 0) {
            printf("Warning: Ignoring negative timespan for animation %s\n", stack->name);
            continue;
        }
        int numKeyframes = (int) ceil(timespan / opts->animSamplingRate);
        if (numKeyframes == 0) {
            printf("Warning: Ignoring animation with no keyframes: %s\n", stack->name);
            continue;
        }
        printf("Take %s for (%f, %f), sampling %d keyframes.\n", stack->name, take->local_time_from, take->local_time_to, numKeyframes);

        std::vector<AnimatedNode> animatedNodes;
        animatedNodes.reserve(nodes.size());

        model->animations.emplace_back();
        Animation *anim = &model->animations.back();
        anim->id = &stack->name[0];
        anim->frames = numKeyframes;
        anim->samplingRate = opts->animSamplingRate;

        // First set up a buffer for each node that's animated
        const AnimationLayer *layer = stack->getLayer(0);
        for (Node *node : nodes) {
            const AnimationCurveNode *translation = layer->getCurveNode(*node->source, "Lcl Translation");
            const AnimationCurveNode *rotation = layer->getCurveNode(*node->source, "Lcl Rotation");
            const AnimationCurveNode *scale = layer->getCurveNode(*node->source, "Lcl Scaling");
            if (translation || rotation || scale) {
                animatedNodes.emplace_back();
                AnimatedNode &an = animatedNodes.back();
                an.modelNode = node;
                an.translation = translation;
                an.rotation = rotation;
                an.scale = scale;
                u32 size = 0;
                if (translation) size += 3;
                if (rotation) size += 4;
                if (scale) size += 3;
                an.width = size;
                an.data.resize(size * numKeyframes);
                float *data = an.data.data();
                if (translation) {
                    an.tdata = data;
                    data += 3;
                }
                if (rotation) {
                    an.rdata = data;
                    data += 4;
                }
                if (scale) {
                    an.sdata = data;
                }
            }
        }
        if (animatedNodes.size() == 0) {
            printf("Warning: Ignoring animation with no keyframes: %s\n", anim->id.c_str());
            model->animations.pop_back();
            continue;
        }

        // Second, calculate all of the transforms for each keyframe and figure out which channels are necessary
        for (int frame = 0; frame < numKeyframes; frame++) {
            double frameTime = startTime + (frame * timespan) / (numKeyframes - 1);
            for (AnimatedNode &an : animatedNodes) {
                // Get animated t/r/s
                Vec3 ts, rs, ss;
                if (an.translation) ts = an.translation->getNodeLocalTransform(frameTime);
                else ts = an.modelNode->source->getLocalTranslation();
                if (an.rotation) rs = an.rotation->getNodeLocalTransform(frameTime);
                else rs = an.modelNode->source->getLocalRotation();
                if (an.scale) ss = an.scale->getNodeLocalTransform(frameTime);
                else ss = an.modelNode->source->getLocalScaling();

                // Convert to the object's local coordinate frame
                Matrix animTransform = an.modelNode->source->evalLocal(ts, rs, ss);
                float td[3], rd[4], sd[3];
                extractTransform(&animTransform, td, rd, sd);

                // Get bind pose t/r/s for reference
                float *tn = an.modelNode->translation;
                float *rn = an.modelNode->rotation;
                float *sn = an.modelNode->scale;

                // Record channel data and check for necessity
                if (an.translation) {
                    float *tr = &an.tdata[an.width * frame];
                    memcpy(tr, td, sizeof(td));
                    if (!close(tn, td, 3, opts->animError)) {
                        an.needsT = true;
                    }
                }
                if (an.rotation) {
                    float *rr = &an.rdata[an.width * frame];
                    memcpy(rr, rd, sizeof(rd));
                    if (!close(rn, rd, 4, opts->animError)) {
                        an.needsR = true;
                    }
                }
                if (an.scale) {
                    float *sr = &an.sdata[an.width * frame];
                    memcpy(sr, sd, sizeof(sd));
                    if (!close(sn, sd, 3, opts->animError)) {
                        an.needsS = true;
                    }
                }
            }
        }

        // Next up, assign offsets to all of the channels for the packed format and strip untransformed nodes.
        int offset = 0;
        for (AnimatedNode &an : animatedNodes) {
            if (an.needsT | an.needsR | an.needsS) {
                anim->nodeIDs.push_back(an.modelNode->id);
                if (an.needsT) {
                    an.toff = offset;
                    offset += 3;
                }
                if (an.needsR) {
                    an.roff = offset;
                    offset += 4;
                }
                if (an.needsS) {
                    an.soff = offset;
                    offset += 3;
                }
                anim->nodeFormats.push_back(an.toff);
                anim->nodeFormats.push_back(an.roff);
                anim->nodeFormats.push_back(an.soff);
            }
        }
        anim->stride = offset;

        int frameSize = offset;
        u32 totalSize = frameSize * numKeyframes;
        anim->nodeData.resize(totalSize);
        float *nodeData = anim->nodeData.data();
        for (AnimatedNode &an : animatedNodes) {
            if (an.needsT | an.needsR | an.needsS) {
                float *frame = nodeData;
                float *t = an.tdata;
                float *r = an.rdata;
                float *s = an.sdata;
                int w = an.width;
                for (int c = 0; c < numKeyframes; c++) {
                    if (an.needsT) {
                        memcpy(&frame[an.toff], t, 3*sizeof(float));
                        t += w;
                    }
                    if (an.needsR) {
                        memcpy(&frame[an.roff], r, 4*sizeof(float));
                        r += w;
                    }
                    if (an.needsS) {
                        memcpy(&frame[an.soff], s, 3*sizeof(float));
                        s += w;
                    }
                    frame += frameSize;
                }
            }
        }
    }
}

bool convertFbxToModel(const IScene *scene, Model *model, Options *opts) {
    const Object *root = scene->getRoot();
    convertChildrenRecursive(scene, root, model, &model->nodes, opts);

    convertAnimations(scene, model, opts);

    return true;
}
