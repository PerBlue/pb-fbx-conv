//
// Created by Martin Wickham on 8/26/17.
//

#include <cmath>
#include <sstream>
#include <cstring>
#include "convertfbx.h"
#include "dumpfbx.h"
#include "mathutil.h"

using namespace ofbx;


// ---------------------- Utility ------------------------

const int bufsize = 128;

static void dumpMatrix(Matrix mat) {
    for (int n = 0; n < 4; n++) {
        printf("%7.6f, %7.6f, %7.6f, %7.6f\n", mat.m[n+0], mat.m[n+4], mat.m[n+8], mat.m[n+12]);
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
    int nodes[MAX_BLEND_WEIGHTS];
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
    Matrix positionTransform;
    Matrix normalTransform;

    const Vec3 *positions;
    const Vec3 *normals;
    const Vec2 *texCoords;
    const Vec4 *colors;
    const Vec3 *tangents;
    const int *materials;
    const Skin *skin;
    int nBlendWeights = 0;
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

static int findOrCreateMeshPart(std::vector<PreMeshPart> &parts, int material, BlendWeight *weights, int nVertWeights) {
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

    // If we have more bones in this poly than nVertWeights, we need to cull some.
    if (maxIdx > nVertWeights) {
        qsort(polyNodes, maxIdx, sizeof(PolyBlendWeight), polyCompare);
        // zero any weights we've removed
        for (int c = nVertWeights; c < maxIdx; c++) {
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

        maxIdx = nVertWeights;
    }

    // Find an existing mesh part that can accept this set of weights and material
    for (PreMeshPart &part : parts) {
        if (part.material == material) {
            int combinedNodes[MAX_BLEND_WEIGHTS];
            memcpy(combinedNodes, part.nodes, sizeof(combinedNodes));

            for (int c = 0; c < maxIdx; c++) {
                PolyBlendWeight &p = polyNodes[c];
                int i = 0;
                while (i < nVertWeights && combinedNodes[i] >= 0 && combinedNodes[i] != p.index) i++;
                if (i >= nVertWeights) goto nextPart;
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

static int *assignTrisToPartsFromSkin(const int *materials, BlendWeight *weights, int nVertWeights, int nVerts,
                                      std::vector<PreMeshPart> &parts) {
    // TODO: This is a really nasty optimization problem that I'm going to ignore for now.
    // The problem is as follows:
    // We have a bunch of polygons; each polygon has a set of up to nVertWeights bones on which it depends.
    // We'd like to make a bunch of mesh parts. Each part has nVertWeight bones which it has available.
    // Find the minimum set of mesh parts such that the bones for any polygon are a subset of one of the parts.

    // My dumb greedy solution:
    // For each polygon,
    //   Look through all existing mesh parts for one in which the size of the union of the mesh part's bones and the polygon's bones is less than nVertWeights
    //   If there is such a mesh part, add any missing bones to it and assign the polygon to that part.
    //   Otherwise, make a new mesh part, set its bones to the polygon's bones, and assign the polygon to that part.

    int nTris = nVerts / 3;
    int *trisToParts = new int[nTris];
    for (int c = 0; c < nTris; c++) {
        int material = 0;
        // find the material
        if (materials) {
            material = findMaterialForTri(materials);
        }

        trisToParts[c] = findOrCreateMeshPart(parts, material, weights, nVertWeights);

        // move forward one vertex
        weights += nVertWeights * 3;
        if (materials) materials += 3;
    }

    printf("Packed %d triangles into %d mesh parts.\n", nTris, int(parts.size()));
    return trisToParts;
}

static void addBones(MeshData *data, PreMeshPart *part, NodePart *np, const Matrix *geometryMatrix) {
    if (part->nodes[0] < 0) return; // no bones
    const Skin *skin = data->skin;
    if (!skin) return;

    int maxBones = data->nBlendWeights;
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
        Matrix clusterTransform = cluster->getTransformMatrix();
        Matrix clusterLinkTransform = cluster->getTransformLinkMatrix();
        Matrix clusterGeom = mul(&clusterTransform, geometryMatrix);
        Matrix invLinkTransform;
        invertMatrix(&clusterLinkTransform, &invLinkTransform);
        Matrix bindPose = mul(&invLinkTransform, &clusterGeom);
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

static void fetchVertex(MeshData *data, int vertexIndex, float *vertex) {
    int attrs = data->attrs;
    float *pos = vertex;
    if (attrs & ATTR_POSITION) {
        fetch(pos, mul(&data->positionTransform, data->positions[vertexIndex]));
    }

    if (attrs & ATTR_NORMAL) {
        Vec3 normal = mul(&data->normalTransform, data->normals[vertexIndex]);
        normalize(&normal);
        fetch(pos, normal);
    }

    if (attrs & ATTR_COLOR) {
        fetch(pos, data->colors[vertexIndex]);
    }
    // TODO: Packed color

    if (attrs & ATTR_TANGENT) {
        fetch(pos, mul(&data->normalTransform, data->tangents[vertexIndex]));
    }
    // TODO: Binormal

    // For now we only support one tex coord.
    if (attrs & ATTR_TEXCOORD0) {
        fetch(pos, data->texCoords[vertexIndex]);
    }

    int nVertWeights = data->nBlendWeights;
    BlendWeight *weights = data->blendWeights + vertexIndex * nVertWeights;
    for (int c = 0; c < nVertWeights; c++) {
        BlendWeight weight = weights[c];
        if (weight.index < 0) {
            pos[0] = 0;
            pos[1] = 0;
        } else {
            pos[0] = (float) weight.index; // TODO: Should this be a reinterpret cast?
            pos[1] = weight.weight;
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

static void buildMesh(MeshData *data, ModelMesh *mesh, std::vector<u16> *indices, int partID) {
    float vertex[MAX_VERTEX_SIZE];
    int nTris = data->nVerts / 3;
    int *trisToParts = data->trisToParts;

    for (int c = 0, v = 0; c < nTris; c++, v += 3) {
        if (trisToParts != nullptr && trisToParts[c] != partID) continue;

        u16 index;

        fetchVertex(data, v+0, vertex);
        index = addVertex(mesh, vertex);
        indices->push_back(index);

        fetchVertex(data, v+1, vertex);
        index = addVertex(mesh, vertex);
        indices->push_back(index);

        fetchVertex(data, v+2, vertex);
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
    Matrix globalTf = geom->getGlobalTransform();
    Matrix geomTf = mesh->getGeometricMatrix();
    data.positionTransform = mul(&globalTf, &geomTf);
    calculateNormalFromTransform(&data.positionTransform, &data.normalTransform);
    dumpMatrix(globalTf);
    dumpMatrix(geomTf);
    dumpMatrix(data.positionTransform);
    dumpMatrix(data.normalTransform);

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
    if (data.colors)    data.attrs |= ATTR_COLOR; // TODO: Pack colors
    if (data.tangents)  data.attrs |= ATTR_TANGENT;
    if (data.texCoords) data.attrs |= ATTR_TEXCOORD0;
    if (data.skin) {
        data.nBlendWeights = computeBlendWeightCount(data.skin, data.nVerts, opts->maxBlendWeights);
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
        data.trisToParts = assignTrisToPartsFromSkin(data.materials, data.blendWeights, data.nBlendWeights, data.nVerts, data.parts);
    } else if (data.materials) {
        data.trisToParts = assignTrisToPartsFromMaterials(data.materials, data.nVerts, data.parts);
    } else {
        data.parts.emplace_back(0);
    }


    int nMaterials = mesh->getMaterialCount();
    int baseIdx = model->materials.size();
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


    ModelMesh *outMesh = findOrCreateMesh(model, data.attrs, data.nVerts);
    std::vector<float> *verts = &outMesh->vertices;
    verts->reserve(verts->size() + data.nVerts * outMesh->vertexSize);

    // reify the mesh parts
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
        buildMesh(&data, outMesh, &mp.indices, partID);

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
    Matrix transform = obj->getGlobalTransform();
    extractTransform(&transform, node->translation, node->rotation, node->scale);

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
            convertNode(scene, child, node, model, opts);
            convertChildrenRecursive(scene, child, model, &node->children, opts);
        }
    }

}

bool convertFbxToModel(const IScene *scene, Model *model, Options *opts) {
    const Object *root = scene->getRoot();
    convertChildrenRecursive(scene, root, model, &model->nodes, opts);
    return true;
}
