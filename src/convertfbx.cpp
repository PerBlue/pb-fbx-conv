//
// Created by Martin Wickham on 8/26/17.
//

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

static void convertMeshes(const IScene *scene, Model *model) {
    int nMesh = scene->getMeshCount();
    for (int c = 0; c < nMesh; c++) {
        const Mesh *mesh = scene->getMesh(c);
        const Geometry *geom = mesh->getGeometry();

        dumpMatrix(geom->getGlobalTransform());
        const Vec3 *positions = geom->getVertices();
        const Vec3 *normals = geom->getNormals();
        const Vec2 *texCoords = geom->getUVs();
        const Vec4 *colors = geom->getColors();

        int nVerts = geom->getVertexCount();

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
