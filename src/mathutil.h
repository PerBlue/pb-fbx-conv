//
// Created by Martin Wickham on 8/27/17.
//

#ifndef PB_FBX_CONV_MATHUTIL_H
#define PB_FBX_CONV_MATHUTIL_H

#include <cmath>
#include "ofbx.h"

static ofbx::Vec3 mul(const ofbx::Matrix *mat, ofbx::Vec3 vec) {
    ofbx::Vec3 out = {0};
    for (int c = 0; c < 3; c++) {
        double component = vec.xyz[c];
        out.x += mat->m[c+0] * component;
        out.y += mat->m[c+4] * component;
        out.z += mat->m[c+8] * component;
    }
    out.x += mat->m[12];
    out.y += mat->m[13];
    out.z += mat->m[14];
    return out;
}

static ofbx::Matrix mul(const ofbx::Matrix *a, const ofbx::Matrix *b) {
    ofbx::Matrix result = {0};
    double *outCol = result.m;
    const double *bCol = b->m;
    for (int d = 0; d < 4; d++) {
        const double *aCol = a->m;
        for (int c = 0; c < 4; c++) {
            double component = bCol[c];
            outCol[0] += aCol[0] * component;
            outCol[1] += aCol[1] * component;
            outCol[2] += aCol[2] * component;
            outCol[3] += aCol[3] * component;
            aCol += 4;
        }
        outCol += 4;
        bCol += 4;
    }
    return result;
}

static void extractTransform(const ofbx::Matrix *mat, float *translation, float *rotation, float *scale) {
    const double (&m)[16] = mat->m;

    double sx = sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    double sy = sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    double sz = sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);

    translation[0] = (float) m[12];
    translation[1] = (float) m[13];
    translation[2] = (float) m[14];

    scale[0] = (float) sx;
    scale[1] = (float) sy;
    scale[2] = (float) sz;

    double isx = 1.0 / sx;
    double isy = 1.0 / sy;
    double isz = 1.0 / sz;

    double rxx = m[0] * isx;
    double rxy = m[1] * isx;
    double rxz = m[2] * isx;
    double ryx = m[4] * isy;
    double ryy = m[5] * isy;
    double ryz = m[6] * isy;
    double rzx = m[8] * isz;
    double rzy = m[9] * isz;
    double rzz = m[10] * isz;

    // Thanks to Mike Day @ Insomniac Games for this quaternion conversion routine.
    // https://d3cw3dd2w32x2b.cloudfront.net/wp-content/uploads/2015/01/matrix-to-quat.pdf
    double t, qx, qy, qz, qw;

    if (rzz < 0) { // is |(x,y)| bigger than |(z,w)|?
        if (rxx > ryy) { // is |x| bigger than |y|?
            // use x-form
            t = 1 + rxx - ryy - rzz;
            qx = t; qy = rxy+ryx; qz = rzx+rxz; qw = ryz-rzy;
        } else {
            // use y-form
            t = 1 - rxx + ryy - rzz;
            qx = rxy+ryx; qy = t; qz = ryz+rzy; qw = rzx-rxz;
        }
    } else {
        if (rxx < -ryy) { // is |z| bigger than |w|?
            // use z-form
            t = 1 - rxx - ryy + rzz;
            qx = rzx+rxz; qy = ryz+rzy; qz = t; qw = rxy-ryx;
        } else {
            // use w-form
            t = 1 + rxx + ryy + rzz;
            qx = ryz-rzy; qy = rzx-rxz; qz = rxy-ryx; qw = t;
        }
    }

    double qn = 0.5 / sqrt(t);
    rotation[0] = (float) (qx * qn);
    rotation[1] = (float) (qy * qn);
    rotation[2] = (float) (qz * qn);
    rotation[3] = (float) (qw * qn);
}

static bool invertMatrix(const ofbx::Matrix *mat, ofbx::Matrix *out)
{
    double inv[16], det;
    int i;

    const double (&m)[16] = mat->m;

    inv[0] = m[5]  * m[10] * m[15] -
             m[5]  * m[11] * m[14] -
             m[9]  * m[6]  * m[15] +
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] -
             m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] +
             m[4]  * m[11] * m[14] +
             m[8]  * m[6]  * m[15] -
             m[8]  * m[7]  * m[14] -
             m[12] * m[6]  * m[11] +
             m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9] * m[15] -
             m[4]  * m[11] * m[13] -
             m[8]  * m[5] * m[15] +
             m[8]  * m[7] * m[13] +
             m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] +
              m[4]  * m[10] * m[13] +
              m[8]  * m[5] * m[14] -
              m[8]  * m[6] * m[13] -
              m[12] * m[5] * m[10] +
              m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] +
             m[1]  * m[11] * m[14] +
             m[9]  * m[2] * m[15] -
             m[9]  * m[3] * m[14] -
             m[13] * m[2] * m[11] +
             m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] -
             m[0]  * m[11] * m[14] -
             m[8]  * m[2] * m[15] +
             m[8]  * m[3] * m[14] +
             m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] +
             m[0]  * m[11] * m[13] +
             m[8]  * m[1] * m[15] -
             m[8]  * m[3] * m[13] -
             m[12] * m[1] * m[11] +
             m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] -
              m[0]  * m[10] * m[13] -
              m[8]  * m[1] * m[14] +
              m[8]  * m[2] * m[13] +
              m[12] * m[1] * m[10] -
              m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] -
             m[1]  * m[7] * m[14] -
             m[5]  * m[2] * m[15] +
             m[5]  * m[3] * m[14] +
             m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] +
             m[0]  * m[7] * m[14] +
             m[4]  * m[2] * m[15] -
             m[4]  * m[3] * m[14] -
             m[12] * m[2] * m[7] +
             m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] -
              m[0]  * m[7] * m[13] -
              m[4]  * m[1] * m[15] +
              m[4]  * m[3] * m[13] +
              m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] +
              m[0]  * m[6] * m[13] +
              m[4]  * m[1] * m[14] -
              m[4]  * m[2] * m[13] -
              m[12] * m[1] * m[6] +
              m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] +
             m[1] * m[7] * m[10] +
             m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] +
             m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] -
             m[0] * m[7] * m[10] -
             m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] +
              m[0] * m[7] * m[9] +
              m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] +
              m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] -
              m[0] * m[6] * m[9] -
              m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

    if (det == 0)
        return false;

    det = 1.0f / det;

    for (i = 0; i < 16; i++)
        out->m[i] = inv[i] * det;

    return true;
}

static void transpose(ofbx::Matrix *mat) {
    std::swap(mat->m[1], mat->m[4]);
    std::swap(mat->m[2], mat->m[8]);
    std::swap(mat->m[3], mat->m[12]);
    std::swap(mat->m[6], mat->m[9]);
    std::swap(mat->m[7], mat->m[13]);
    std::swap(mat->m[11], mat->m[14]);
}

// The covector transformation matrix is the inverse of the transpose of the rotation and scaling part of the vector transformation matrix.
// This operation preserves the rotation from the original transformation but reverses the scale for use with covectors.
static void calculateNormalFromTransform(const ofbx::Matrix *transform, ofbx::Matrix *normal) {
    ofbx::Matrix tmp = *transform;
    // zero translation and projection
    tmp.m[3] = tmp.m[7] = tmp.m[11] = 0;
    tmp.m[12] = tmp.m[13] = tmp.m[14] = 0;
    tmp.m[15] = 1;
    invertMatrix(&tmp, normal);
    transpose(normal);
}

static void normalize(ofbx::Vec3 *vec) {
    double len = sqrt(vec->x * vec->x + vec->y * vec->y + vec->z * vec->z);
    if (len != 0) {
        double ilen = 1.0 / len;
        vec->x *= ilen;
        vec->y *= ilen;
        vec->z *= ilen;
    }
}

#endif //PB_FBX_CONV_MATHUTIL_H
