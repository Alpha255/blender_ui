#pragma once
#include <cmath>

namespace bl_ui {

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;

    Vec3 operator+(const Vec3& b) const { return {x+b.x, y+b.y, z+b.z}; }
    Vec3 operator-(const Vec3& b) const { return {x-b.x, y-b.y, z-b.z}; }
    Vec3 operator*(float s)        const { return {x*s,   y*s,   z*s};   }
    Vec3 operator-()               const { return {-x,    -y,    -z};    }

    float dot  (const Vec3& b) const { return x*b.x + y*b.y + z*b.z; }
    Vec3  cross (const Vec3& b) const {
        return { y*b.z - z*b.y, z*b.x - x*b.z, x*b.y - y*b.x };
    }
    float len2() const { return x*x + y*y + z*z; }
    float len()  const { return std::sqrt(len2()); }
    Vec3  normalized() const {
        float l = len();
        return l > 1e-8f ? Vec3{x/l, y/l, z/l} : Vec3{};
    }
};

// Column-major 4×4 float matrix (OpenGL layout: m[col*4 + row]).
struct Mat4 {
    float m[16] = {};

    static Mat4 identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
        return r;
    }

    // Symmetric perspective frustum.  fov_y in radians, near/far > 0.
    static Mat4 perspective(float fov_y, float aspect, float near_z, float far_z) {
        float f  = 1.f / std::tan(fov_y * 0.5f);
        float dz = near_z - far_z;   // negative
        Mat4 r{};
        r.m[0]  =  f / aspect;
        r.m[5]  =  f;
        r.m[10] =  (far_z + near_z) / dz;
        r.m[11] = -1.f;
        r.m[14] =  2.f * far_z * near_z / dz;
        return r;
    }

    // Standard gluLookAt — right-handed, y-up.
    static Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up) {
        Vec3 f = (center - eye).normalized();
        Vec3 r = f.cross(up).normalized();
        Vec3 u = r.cross(f);
        Mat4 mat{};
        // Columns of the rotation block
        mat.m[0] =  r.x; mat.m[1] =  u.x; mat.m[2]  = -f.x; mat.m[3]  = 0.f;
        mat.m[4] =  r.y; mat.m[5] =  u.y; mat.m[6]  = -f.y; mat.m[7]  = 0.f;
        mat.m[8] =  r.z; mat.m[9] =  u.z; mat.m[10] = -f.z; mat.m[11] = 0.f;
        // Translation column
        mat.m[12] = -r.dot(eye);
        mat.m[13] = -u.dot(eye);
        mat.m[14] =  f.dot(eye);
        mat.m[15] =  1.f;
        return mat;
    }

    // this * b  (left × right)
    Mat4 operator*(const Mat4& b) const {
        Mat4 r{};
        for (int col = 0; col < 4; col++)
            for (int row = 0; row < 4; row++)
                for (int k   = 0; k   < 4; k++)
                    r.m[col*4+row] += m[k*4+row] * b.m[col*4+k];
        return r;
    }

    // General 4×4 inverse via cofactor expansion.  Returns false if singular.
    bool inverse(Mat4& out) const {
        const float* a = m;
        float inv[16];

        inv[0] = a[5]*a[10]*a[15]-a[5]*a[11]*a[14]-a[9]*a[6]*a[15]+a[9]*a[7]*a[14]+a[13]*a[6]*a[11]-a[13]*a[7]*a[10];
        inv[4] =-a[4]*a[10]*a[15]+a[4]*a[11]*a[14]+a[8]*a[6]*a[15]-a[8]*a[7]*a[14]-a[12]*a[6]*a[11]+a[12]*a[7]*a[10];
        inv[8] = a[4]*a[9]*a[15] -a[4]*a[11]*a[13]-a[8]*a[5]*a[15]+a[8]*a[7]*a[13]+a[12]*a[5]*a[11]-a[12]*a[7]*a[9];
        inv[12]=-a[4]*a[9]*a[14] +a[4]*a[10]*a[13]+a[8]*a[5]*a[14]-a[8]*a[6]*a[13]-a[12]*a[5]*a[10]+a[12]*a[6]*a[9];

        inv[1] =-a[1]*a[10]*a[15]+a[1]*a[11]*a[14]+a[9]*a[2]*a[15]-a[9]*a[3]*a[14]-a[13]*a[2]*a[11]+a[13]*a[3]*a[10];
        inv[5] = a[0]*a[10]*a[15]-a[0]*a[11]*a[14]-a[8]*a[2]*a[15]+a[8]*a[3]*a[14]+a[12]*a[2]*a[11]-a[12]*a[3]*a[10];
        inv[9] =-a[0]*a[9]*a[15] +a[0]*a[11]*a[13]+a[8]*a[1]*a[15]-a[8]*a[3]*a[13]-a[12]*a[1]*a[11]+a[12]*a[3]*a[9];
        inv[13]= a[0]*a[9]*a[14] -a[0]*a[10]*a[13]-a[8]*a[1]*a[14]+a[8]*a[2]*a[13]+a[12]*a[1]*a[10]-a[12]*a[2]*a[9];

        inv[2] = a[1]*a[6]*a[15] -a[1]*a[7]*a[14] -a[5]*a[2]*a[15]+a[5]*a[3]*a[14]+a[13]*a[2]*a[7] -a[13]*a[3]*a[6];
        inv[6] =-a[0]*a[6]*a[15] +a[0]*a[7]*a[14] +a[4]*a[2]*a[15]-a[4]*a[3]*a[14]-a[12]*a[2]*a[7] +a[12]*a[3]*a[6];
        inv[10]= a[0]*a[5]*a[15] -a[0]*a[7]*a[13] -a[4]*a[1]*a[15]+a[4]*a[3]*a[13]+a[12]*a[1]*a[7] -a[12]*a[3]*a[5];
        inv[14]=-a[0]*a[5]*a[14] +a[0]*a[6]*a[13] +a[4]*a[1]*a[14]-a[4]*a[2]*a[13]-a[12]*a[1]*a[6] +a[12]*a[2]*a[5];

        inv[3] =-a[1]*a[6]*a[11] +a[1]*a[7]*a[10] +a[5]*a[2]*a[11]-a[5]*a[3]*a[10]-a[9]*a[2]*a[7]  +a[9]*a[3]*a[6];
        inv[7] = a[0]*a[6]*a[11] -a[0]*a[7]*a[10] -a[4]*a[2]*a[11]+a[4]*a[3]*a[10]+a[8]*a[2]*a[7]  -a[8]*a[3]*a[6];
        inv[11]=-a[0]*a[5]*a[11] +a[0]*a[7]*a[9]  +a[4]*a[1]*a[11]-a[4]*a[3]*a[9] -a[8]*a[1]*a[7]  +a[8]*a[3]*a[5];
        inv[15]= a[0]*a[5]*a[10] -a[0]*a[6]*a[9]  -a[4]*a[1]*a[10]+a[4]*a[2]*a[9] +a[8]*a[1]*a[6]  -a[8]*a[2]*a[5];

        float det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
        if (std::abs(det) < 1e-10f) return false;
        float inv_det = 1.f / det;
        for (int i = 0; i < 16; i++) out.m[i] = inv[i] * inv_det;
        return true;
    }

    const float* data() const { return m; }
};

} // namespace bl_ui
