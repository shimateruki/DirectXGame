#pragma once
#include "engine/utility/math/Math.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

// 描画頂点から作る凸包。凹形状の門などは、柱・梁を別Colliderにして使用する。
namespace convex_collision {
using V = engine::Vector3;
using Face = std::array<V, 3>;
inline float Dot(const V& a, const V& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline V Cross(const V& a, const V& b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
inline V Normal(const Face& f) {
    // 面積ゼロの面からNaNを広げない。
    const V n = Cross(f[1]-f[0], f[2]-f[0]);
    const float length = std::sqrt(Dot(n,n));
    return length > 1e-7f ? n / length : V{};
}
inline std::vector<Face> Build(const std::vector<V>& input) {
    // 最初の四面体を外側の点へ順に拡張する。読み込み時だけ実行する。
    std::vector<V> points;
    for (const V& p : input) {
        if (std::none_of(points.begin(), points.end(), [&](const V& q) { return Dot(p-q,p-q)<1e-10f; }))
            points.push_back(p);
    }
    if (points.size()<4)
        return {};
    size_t a=0,b=0,c=0,d=0;
    float best=0;
    for (size_t i=1;i<points.size();++i) {
        const float value=Dot(points[i]-points[a],points[i]-points[a]);
        if (value>best) {
            best=value;
            b=i;
        }
    }
    best=0;
    for (size_t i=0;i<points.size();++i) {
        const V n=Cross(points[b]-points[a],points[i]-points[a]);
        const float value=Dot(n,n);
        if(value>best) {
            best=value;
            c=i;
        }
    }
    if(best<1e-12f)
        return {};
    const V n=Normal({points[a],points[b],points[c]});
    best=0;
    for(size_t i=0;i<points.size();++i) {
        const float value=std::abs(Dot(n,points[i]-points[a]));
        if(value>best) {
            best=value;
            d=i;
        }
    }
    if(best<1e-6f)
        return {};
    const V inside=(points[a]+points[b]+points[c]+points[d])*0.25f;
    using Indices=std::array<size_t,3>;
    std::vector<Indices> faces;
    const auto add=[&](size_t x,size_t y,size_t z) {
        if(Dot(Cross(points[y]-points[x],points[z]-points[x]),inside-points[x])>0)
            std::swap(y,z);
        faces.push_back({x,y,z});
    };
    add(a,b,c);
    add(a,d,b);
    add(a,c,d);
    add(b,d,c);
    for(size_t i=0;i<points.size();++i) {
        std::vector<std::array<size_t,2>> boundary;
        std::vector<Indices> kept;
        for(const auto& f:faces) {
            if(Dot(Normal({points[f[0]],points[f[1]],points[f[2]]}),points[i]-points[f[0]])<=1e-5f) {
                kept.push_back(f);
                continue;
            }
            for(int e=0;e<3;++e) {
                const std::array<size_t,2> edge{f[e],f[(e+1)%3]};
                auto reverse=std::find(boundary.begin(),boundary.end(),std::array<size_t,2>{edge[1],edge[0]});
                if(reverse==boundary.end())
                    boundary.push_back(edge);
                else boundary.erase(reverse);
            }
        }
        faces=std::move(kept);
        for(const auto& e:boundary)
            add(e[0],e[1],i);
    }
    std::vector<Face> result;
    for(const auto& f:faces)
        result.push_back({points[f[0]],points[f[1]],points[f[2]]});
    return result;
}

inline V Closest(const V& p,const Face& f) {
    // 三角形の面・辺・頂点の領域を分け、球が角に触れる場合も距離を正しく求める。
    const V a=f[0],b=f[1],c=f[2],ab=b-a,ac=c-a,ap=p-a;
    const float d1=Dot(ab,ap),d2=Dot(ac,ap);
    if(d1<=0 && d2<=0)
        return a;
    const V bp=p-b;
    const float d3=Dot(ab,bp),d4=Dot(ac,bp);
    if(d3>=0 && d4<=d3)
        return b;
    const float vc=d1*d4-d3*d2;
    if(vc<=0 && d1>=0 && d3<=0)
        return a+ab*(d1/(d1-d3));
    const V cp=p-c;
    const float d5=Dot(ab,cp),d6=Dot(ac,cp);
    if(d6>=0 && d5<=d6)
        return c;
    const float vb=d5*d2-d1*d6;
    if(vb<=0 && d2>=0 && d6<=0)
        return a+ac*(d2/(d2-d6));
    const float va=d3*d6-d5*d4;
    if(va<=0 && d4-d3>=0 && d5-d6>=0)
        return b+(c-b)*((d4-d3)/((d4-d3)+(d5-d6)));
    const float inverse=1.0f/(va+vb+vc);
    return a+ab*(vb*inverse)+ac*(vc*inverse);
}

inline bool Ray(const std::vector<Face>& hull,const V& start,const V& direction,float limit,float& distance,V& normal) {
    // 全ての外向き平面でレイ区間を切り詰める。側面・底面も同じ経路で調べる。
    float enter=0,exit=limit;
    V enterNormal{},exitNormal{};
    bool inside=true;
    for(const auto& f:hull) {
        const V n=Normal(f);
        const float signedDistance=Dot(n,start-f[0]),speed=Dot(n,direction);
        inside=inside && signedDistance<=1e-5f;
        if(std::abs(speed)<1e-7f) {
            if(signedDistance>1e-5f)
                return false;
            continue;
        }
        const float t=-signedDistance/speed;
        if(speed<0) {
            if(t>enter){
                enter=t;
                enterNormal=n;
            }
        }
        else if(t<exit){
            exit=t;
            exitNormal=n;
        }
        if(enter>exit)
            return false;
    }
    distance=inside?exit:enter;
    normal=inside?exitNormal:enterNormal;
    return distance>=0 && distance<=limit && Dot(normal,normal)>0.5f;
}

inline bool Sphere(const std::vector<Face>& hull,const V& center,float radius,float& depth,V& normal) {
    // 外側は最近接点、内側は最寄りの面から押し出す。
    float nearest=(std::numeric_limits<float>::max)(),plane=-nearest;
    V closest{},planeNormal{};
    bool inside=true;
    for(const auto& f:hull) {
        const V n=Normal(f);
        const float signedDistance=Dot(n,center-f[0]);
        if(signedDistance>plane){
            plane=signedDistance;
            planeNormal=n;
        }
        inside=inside && signedDistance<=0;
        const V q=Closest(center,f);
        const float squared=Dot(center-q,center-q);
        if(squared<nearest){
            nearest=squared;
            closest=q;
        }
    }
    if(inside){
        depth=radius-plane;
        normal=planeNormal;
        return true;
    }
    const float distance=std::sqrt(nearest);
    if(distance>=radius)
        return false;
    depth=radius-distance;
    normal=distance>1e-6f?(center-closest)/distance:planeNormal;
    return true;
}

inline bool Polyhedra(const std::vector<V>& pointsA, const std::vector<V>& normalsA,
                      const std::vector<V>& edgesA, const std::vector<V>& pointsB,
                      const std::vector<V>& normalsB, const std::vector<V>& edgesB,
                      float& depth, V& normal) {
    // キャッシュ済みの頂点・方向を使う。辺の外積を巨大な配列へ集めて
    // 重複比較すると二乗以上の負荷になるため、生成した軸をその場で判定する。
    depth = (std::numeric_limits<float>::max)();
    const auto test = [&](V axis) {
        const float squaredLength = Dot(axis, axis);
        if (squaredLength < 1e-12f)
            return true;
        axis = axis / std::sqrt(squaredLength);
        float minA = (std::numeric_limits<float>::max)(), maxA = -minA;
        float minB = minA, maxB = -minB;
        for (const V& point : pointsA) {
            const float value = Dot(point, axis);
            minA = (std::min)(minA, value);
            maxA = (std::max)(maxA, value);
        }
        for (const V& point : pointsB) {
            const float value = Dot(point, axis);
            minB = (std::min)(minB, value);
            maxB = (std::max)(maxB, value);
        }
        const float positive = maxA - minB, negative = maxB - minA;
        if (positive <= 0 || negative <= 0)
            return false;
        const float overlap = (std::min)(positive, negative);
        if (overlap < depth) {
            depth = overlap;
            normal = positive < negative ? axis : axis * -1.0f;
        }
        return true;
    };
    // 面だけで分離できる組み合わせを先に除外し、辺同士の比較まで進めない。
    for (const V& axis : normalsA)
        if (!test(axis))
            return false;
    for (const V& axis : normalsB)
        if (!test(axis))
            return false;
    for (const V& edgeA : edgesA)
        for (const V& edgeB : edgesB)
            if (!test(Cross(edgeA, edgeB)))
                return false;
    return !pointsA.empty() && !pointsB.empty();
}

inline bool Box(const std::vector<V>& points, const std::vector<V>& normals, const std::vector<V>& edges,
                const V& center, const V* boxAxes, const V& halfSize, float& depth, V& normal) {
    // 箱の投影幅は3軸の内積から直接計算し、頂点・軸の重複生成を接触ごとに行わない。
    depth=(std::numeric_limits<float>::max)();
    const auto test=[&](V axis) {
        const float length=std::sqrt(Dot(axis,axis));
        if(length<1e-6f)
            return true;
        axis=axis/length;
        float minimum=(std::numeric_limits<float>::max)(),maximum=-minimum;
        for(const V& point:points){
            const float value=Dot(point,axis);
            minimum=(std::min)(minimum,value);
            maximum=(std::max)(maximum,value);
        }
        const float middle=Dot(center,axis);
        const float extent=std::abs(Dot(boxAxes[0],axis))*halfSize.x+
                           std::abs(Dot(boxAxes[1],axis))*halfSize.y+
                           std::abs(Dot(boxAxes[2],axis))*halfSize.z;
        const float positive=maximum-(middle-extent),negative=(middle+extent)-minimum;
        if(positive<=0 || negative<=0)
            return false;
        const float overlap=(std::min)(positive,negative);
        if(overlap<depth){
            depth=overlap;
            normal=positive<negative?axis:axis*-1.0f;
        }
        return true;
    };
    for(const V& axis:normals)
        if(!test(axis))
            return false;
    for(int i=0;i<3;++i)
        if(!test(boxAxes[i]))
            return false;
    for(const V& edge:edges)
        for(int i=0;i<3;++i)
            if(!test(Cross(edge,boxAxes[i])))
                return false;
    return !points.empty();
}
} // namespace convex_collision
