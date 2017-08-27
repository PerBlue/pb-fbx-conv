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

static void extractTransform(const ofbx::Matrix *mat, float *translation, float *rotation, float *scale) {
    double (&m)[16] = mat->m;

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
    double ryx = m[1] * isx;
    double rzx = m[2] * isx;
    double rxy = m[4] * isy;
    double ryy = m[5] * isy;
    double rzy = m[6] * isy;
    double rxz = m[8] * isz;
    double ryz = m[9] * isz;
    double rzz = m[10] * isz;

    double qw = sqrt(1.0 + rxx + ryy + rzz) / 2;
    double iw4 = 1f / (qw * 4);
    double qx = (rzy - ryz) * iw4;
    double qy = (rxz - rzx) * iw4;
    double qz = (ryx - rxy) * iw4;

    rotation[0] = (float) qx;
    rotation[1] = (float) qy;
    rotation[2] = (float) qz;
    rotation[3] = (float) qw;
}

static bool invertMatrix(const ofbx::Matrix *mat, ofbx::Matrix *out)
{
    double inv[16], det;
    int i;

    double (&m)[16] = mat->m;

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

#endif //PB_FBX_CONV_MATHUTIL_H
