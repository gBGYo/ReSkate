#pragma once
#include "blood_ground.h"
#include <algorithm>
#include <cmath>

namespace dingosdk::blood::blood_surface {
inline bool valid_pose(const BloodMatrix& m) {
    for (float v:m) if (!std::isfinite(v)) return false;
    for (unsigned a=0;a<3;++a) {
        if (std::abs(m[12+a])>100000 || std::abs(m[4*a+3])>.001f) return false;
        for (unsigned b=0;b<=a;++b) {
            float dot{}; for (unsigned i=0;i<3;++i) dot+=m[4*a+i]*m[4*b+i];
            if (std::abs(dot-(a==b ? 1.f : 0.f))>.01f) return false;
        }
    }
    return std::abs(m[15]-1)<.001f;
}
inline BloodMatrix local(const BloodMatrix& pose,const BloodMatrix& world) {
    BloodMatrix out{}; out[15]=1;
    for (unsigned column=0;column<4;++column) for (unsigned row=0;row<3;++row)
        for (unsigned i=0;i<3;++i)
            out[4*column+row]+=pose[4*row+i]*(world[4*column+i]-(column==3 ? pose[12+i] : 0.f));
    return out;
}
inline BloodMatrix world(const BloodMatrix& pose,const BloodMatrix& local) {
    BloodMatrix out{}; out[15]=1;
    for (unsigned column=0;column<4;++column) for (unsigned row=0;row<3;++row) {
        out[4*column+row]=column==3 ? pose[12+row] : 0.f;
        for (unsigned i=0;i<3;++i) out[4*column+row]+=pose[4*i+row]*local[4*column+i];
    }
    return out;
}
inline bool changed(const BloodMatrix& a,const BloodMatrix& b) {
    for (unsigned i=0;i<16;++i) if (std::abs(a[i]-b[i])>.00001f) return true;
    return false;
}
// Native physics readers return quaternion XYZW and world position XYZ.
inline bool pose(const std::array<float,4>& rotation,const std::array<float,4>& position,BloodMatrix& out) {
    float norm{};
    for (float v:rotation) {if (!std::isfinite(v)) return false; norm+=v*v;}
    if (norm<.9f || norm>1.1f) return false;
    const float scale=1/std::sqrt(norm),x=rotation[0]*scale,y=rotation[1]*scale,z=rotation[2]*scale,w=rotation[3]*scale;
    out={1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,
        2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,
        2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,position[0],position[1],position[2],1};
    return valid_pose(out);
}
}
