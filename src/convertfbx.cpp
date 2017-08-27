//
// Created by Martin Wickham on 8/26/17.
//

#include <cmath>
#include "convertfbx.h"

using namespace ofbx;

static void convertMeshes(const IScene *scene, Model *model);

bool convertFbxToModel(const IScene *scene, Model *model) {
    convertMeshes(scene, model);


    return true;
}

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

struct BlendWeight {
    u32 index;
    f32 weight;
};

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

static void convertMeshes(const IScene *scene, Model *model) {
    int nMesh = scene->getMeshCount();
    for (int c = 0; c < nMesh; c++) {
        const Mesh *mesh = scene->getMesh(c);
        const Geometry *geom = mesh->getGeometry();

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

        ModelMesh *outMesh = findOrCreateMesh(model, attrs, nVerts);
        std::vector<float> *verts = &outMesh->vertices;
        verts->reserve(verts->size() + nVerts * outMesh->vertexSize);


        delete [] blendWeights; // delete [] nullptr is defined and has no effect.
    }
}
