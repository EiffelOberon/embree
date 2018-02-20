// ======================================================================== //
// Copyright 2009-2018 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "subgrid.h"
#include "quad_intersector_moeller.h"

namespace embree
{
  namespace isa
  {

    template<int K>
      struct SubGridQuadHitK
      {
        __forceinline SubGridQuadHitK(const vfloat<K>& U,
                                      const vfloat<K>& V,
                                      const vfloat<K>& T,
                                      const vfloat<K>& absDen,
                                      const Vec3vf<K>& Ng,
                                      const vbool<K>& flags,
                                      const GridMesh::Grid &g, 
                                      const SubGrid& subgrid,
                                      const size_t i)
        : U(U), V(V), T(T), absDen(absDen), flags(flags), tri_Ng(Ng), g(g), subgrid(subgrid), i(i) {}

        __forceinline std::tuple<vfloat<K>,vfloat<K>,vfloat<K>,Vec3vf<K>> operator() () const
        {
          const vfloat<K> rcpAbsDen = rcp(absDen);
          const vfloat<K> t = T * rcpAbsDen;
          const vfloat<K> u0 = U * rcpAbsDen;
          const vfloat<K> v0 = V * rcpAbsDen;
          const vfloat<K> u1 = vfloat<K>(1.0f) - u0;
          const vfloat<K> v1 = vfloat<K>(1.0f) - v0;
          const vfloat<K> uu = select(flags,u1,u0);
          const vfloat<K> vv = select(flags,v1,v0);
          const int sx = subgrid.x() + (i % 2);
          const int sy = subgrid.y() + (i >> 1);
          const float inv_resX = rcp((float)((int)g.resX-1));
          const float inv_resY = rcp((float)((int)g.resY-1));          
          const vfloat<K> u = (uu + (float)sx) * inv_resX;
          const vfloat<K> v = (vv + (float)sy) * inv_resY;          
          const Vec3vf<K> Ng(tri_Ng.x,tri_Ng.y,tri_Ng.z);
          return std::make_tuple(u,v,t,Ng);
        }

      private:
        const vfloat<K> U;
        const vfloat<K> V;
        const vfloat<K> T;
        const vfloat<K> absDen;
        const vbool<K> flags;
        const Vec3vf<K> tri_Ng;

        const GridMesh::Grid &g;
        const SubGrid& subgrid;
        const size_t i;
      };

    /* ----------------------------- */
    /* -- single ray intersectors -- */
    /* ----------------------------- */

    template<int M>
      __forceinline void interpolateUV(MoellerTrumboreHitM<M> &hit,const GridMesh::Grid &g, const SubGrid& subgrid) 
    {
      /* correct U,V interpolation across the entire grid */
      const vint<M> sx((int)subgrid.x());
      const vint<M> sy((int)subgrid.y());
      const vint<M> sxM(sx + vint<M>(0,1,1,0));
      const vint<M> syM(sy + vint<M>(0,0,1,1));
      const float inv_resX = rcp((float)((int)g.resX-1));
      const float inv_resY = rcp((float)((int)g.resY-1));          
      hit.U = (hit.U + (vfloat<M>)sxM * hit.absDen) * inv_resX;
      hit.V = (hit.V + (vfloat<M>)syM * hit.absDen) * inv_resY;          
    }
    
    template<int M, bool filter>
      struct SubGridQuadMIntersector1MoellerTrumbore;

    /*! Intersects M quads with 1 ray */
    template<int M, bool filter>
      struct SubGridQuadMIntersector1MoellerTrumbore
      {
        __forceinline SubGridQuadMIntersector1MoellerTrumbore() {}

        __forceinline SubGridQuadMIntersector1MoellerTrumbore(const Ray& ray, const void* ptr) {}

        __forceinline void intersect(RayHit& ray, IntersectContext* context,
                                     const Vec3vf<M>& v0, const Vec3vf<M>& v1, const Vec3vf<M>& v2, const Vec3vf<M>& v3,
                                     const GridMesh::Grid &g, const SubGrid& subgrid) const
        {
          MoellerTrumboreHitM<M> hit;
          MoellerTrumboreIntersector1<M> intersector(ray,nullptr);
          const vint<M> gIDs(subgrid.geomID()); // needs to be explicity here, otherwise clang produces wrong code
          const vint<M> pIDs(subgrid.primID());
          Intersect1EpilogM<M,M,filter> epilog(ray,context,gIDs,pIDs);

          /* intersect first triangle */
          if (intersector.intersect(ray,v0,v1,v3,hit)) 
          {
            interpolateUV<M>(hit,g,subgrid);
            epilog(hit.valid,hit);
          }

          /* intersect second triangle */
          if (intersector.intersect(ray,v2,v3,v1,hit)) 
          {
            hit.U = hit.absDen - hit.U;
            hit.V = hit.absDen - hit.V;
            interpolateUV<M>(hit,g,subgrid);
            epilog(hit.valid,hit);
          }
        }
      
        __forceinline bool occluded(Ray& ray, IntersectContext* context,
                                    const Vec3vf<M>& v0, const Vec3vf<M>& v1, const Vec3vf<M>& v2, const Vec3vf<M>& v3,
                                    const GridMesh::Grid &g, const SubGrid& subgrid) const
        {
          MoellerTrumboreHitM<M> hit;
          MoellerTrumboreIntersector1<M> intersector(ray,nullptr);
          Occluded1EpilogM<M,M,filter> epilog(ray,context,vint<M>(subgrid.geomID()),vint<M>(subgrid.primID()));
          /* intersect first triangle */
          if (intersector.intersect(ray,v0,v1,v3,hit)) 
          {
            interpolateUV<M>(hit,g,subgrid);
            if (epilog(hit.valid,hit))
              return true;
          }

          /* intersect second triangle */
          if (intersector.intersect(ray,v2,v3,v1,hit)) 
          {
            hit.U = hit.absDen - hit.U;
            hit.V = hit.absDen - hit.V;
            interpolateUV<M>(hit,g,subgrid);
            if (epilog(hit.valid,hit))
              return true;
          }
          return false;
        }
      };

#if defined(__AVX512F__)

    /*! Intersects 4 quads with 1 ray using AVX512 */
    template<bool filter>
      struct SubGridQuadMIntersector1MoellerTrumbore<4,filter>
    {
      __forceinline SubGridQuadMIntersector1MoellerTrumbore() {}

      __forceinline SubGridQuadMIntersector1MoellerTrumbore(const Ray& ray, const void* ptr) {}

      template<typename Epilog>
        __forceinline bool intersect(Ray& ray, const Vec3vf4& v0, const Vec3vf4& v1, const Vec3vf4& v2, const Vec3vf4& v3, const GridMesh::Grid &g, const SubGrid& subgrid, const Epilog& epilog) const
      {
        const Vec3vf16 vtx0(select(0x0f0f,vfloat16(v0.x),vfloat16(v2.x)),
                            select(0x0f0f,vfloat16(v0.y),vfloat16(v2.y)),
                            select(0x0f0f,vfloat16(v0.z),vfloat16(v2.z)));
#if !defined(EMBREE_BACKFACE_CULLING)
        const Vec3vf16 vtx1(vfloat16(v1.x),vfloat16(v1.y),vfloat16(v1.z));
        const Vec3vf16 vtx2(vfloat16(v3.x),vfloat16(v3.y),vfloat16(v3.z));
#else
        const Vec3vf16 vtx1(select(0x0f0f,vfloat16(v1.x),vfloat16(v3.x)),
                            select(0x0f0f,vfloat16(v1.y),vfloat16(v3.y)),
                            select(0x0f0f,vfloat16(v1.z),vfloat16(v3.z)));
        const Vec3vf16 vtx2(select(0x0f0f,vfloat16(v3.x),vfloat16(v1.x)),
                            select(0x0f0f,vfloat16(v3.y),vfloat16(v1.y)),
                            select(0x0f0f,vfloat16(v3.z),vfloat16(v1.z)));
#endif
        const vbool16 flags(0xf0f0);

        MoellerTrumboreHitM<16> hit;
        MoellerTrumboreIntersector1<16> intersector(ray,nullptr);
        if (unlikely(intersector.intersect(ray,vtx0,vtx1,vtx2,hit))) 
        {
          vfloat16 U = hit.U, V = hit.V, absDen = hit.absDen;
#if !defined(EMBREE_BACKFACE_CULLING)
          hit.U = select(flags,absDen-V,U);
          hit.V = select(flags,absDen-U,V);
          hit.vNg *= select(flags,vfloat16(-1.0f),vfloat16(1.0f)); // FIXME: use XOR
#else
          hit.U = select(flags,absDen-U,U);
          hit.V = select(flags,absDen-V,V);
#endif

          /* correct U,V interpolation across the entire grid */
          const vint16 sx((int)subgrid.x());
          const vint16 sy((int)subgrid.y());
          const vint16 sx16(sx + vint16(0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0));
          const vint16 sy16(sy + vint16(0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1));
          const float inv_resX = rcp((float)((int)g.resX-1));
          const float inv_resY = rcp((float)((int)g.resY-1));          
          hit.U = (hit.U + (vfloat16)sx16 * absDen) * inv_resX;
          hit.V = (hit.V + (vfloat16)sy16 * absDen) * inv_resY;          

          if (likely(epilog(hit.valid,hit)))
            return true;
        }
        return false;
      }
      
      __forceinline bool intersect(RayHit& ray, IntersectContext* context,
                                   const Vec3vf4& v0, const Vec3vf4& v1, const Vec3vf4& v2, const Vec3vf4& v3, 
                                   const GridMesh::Grid &g, const SubGrid& subgrid) const
      {
        return intersect(ray,v0,v1,v2,v3,g,subgrid,Intersect1EpilogM<8,16,filter>(ray,context,vint8(subgrid.geomID()),vint8(subgrid.primID())));
      }
      
      __forceinline bool occluded(Ray& ray, IntersectContext* context,
                                  const Vec3vf4& v0, const Vec3vf4& v1, const Vec3vf4& v2, const Vec3vf4& v3, 
                                  const GridMesh::Grid &g, const SubGrid& subgrid) const
      {
        return intersect(ray,v0,v1,v2,v3,g,subgrid,Occluded1EpilogM<8,16,filter>(ray,context,vint8(subgrid.geomID()),vint8(subgrid.primID())));
      }
    };

#elif defined (__AVX__)

    /*! Intersects 4 quads with 1 ray using AVX */
    template<bool filter>
      struct SubGridQuadMIntersector1MoellerTrumbore<4,filter>
    {
      __forceinline SubGridQuadMIntersector1MoellerTrumbore() {}

      __forceinline SubGridQuadMIntersector1MoellerTrumbore(const Ray& ray, const void* ptr) {}
      
      template<typename Epilog>
        __forceinline bool intersect(Ray& ray, const Vec3vf4& v0, const Vec3vf4& v1, const Vec3vf4& v2, const Vec3vf4& v3, const GridMesh::Grid &g, const SubGrid& subgrid, const Epilog& epilog) const
      {
        const Vec3vf8 vtx0(vfloat8(v0.x,v2.x),vfloat8(v0.y,v2.y),vfloat8(v0.z,v2.z));
#if !defined(EMBREE_BACKFACE_CULLING)
        const Vec3vf8 vtx1(vfloat8(v1.x),vfloat8(v1.y),vfloat8(v1.z));
        const Vec3vf8 vtx2(vfloat8(v3.x),vfloat8(v3.y),vfloat8(v3.z));        
#else
        const Vec3vf8 vtx1(vfloat8(v1.x,v3.x),vfloat8(v1.y,v3.y),vfloat8(v1.z,v3.z));
        const Vec3vf8 vtx2(vfloat8(v3.x,v1.x),vfloat8(v3.y,v1.y),vfloat8(v3.z,v1.z));
#endif
        MoellerTrumboreHitM<8> hit;
        MoellerTrumboreIntersector1<8> intersector(ray,nullptr);
        const vbool8 flags(0,0,0,0,1,1,1,1);
        if (unlikely(intersector.intersect(ray,vtx0,vtx1,vtx2,hit)))
        {
          vfloat8 U = hit.U, V = hit.V, absDen = hit.absDen;

#if !defined(EMBREE_BACKFACE_CULLING)
          hit.U = select(flags,absDen-V,U);
          hit.V = select(flags,absDen-U,V);
          hit.vNg *= select(flags,vfloat8(-1.0f),vfloat8(1.0f)); // FIXME: use XOR
#else
          hit.U = select(flags,absDen-U,U);
          hit.V = select(flags,absDen-V,V);
#endif
          /* correct U,V interpolation across the entire grid */
          const vint8 sx((int)subgrid.x());
          const vint8 sy((int)subgrid.y());
          const vint8 sx8(sx + vint8(0,1,1,0,0,1,1,0));
          const vint8 sy8(sy + vint8(0,0,1,1,0,0,1,1));
          const float inv_resX = rcp((float)((int)g.resX-1));
          const float inv_resY = rcp((float)((int)g.resY-1));          
          hit.U = (hit.U + (vfloat8)sx8 * absDen) * inv_resX;
          hit.V = (hit.V + (vfloat8)sy8 * absDen) * inv_resY;          

          if (unlikely(epilog(hit.valid,hit)))
            return true;
        }
        return false;
      }
      
      __forceinline bool intersect(RayHit& ray, IntersectContext* context,
                                   const Vec3vf4& v0, const Vec3vf4& v1, const Vec3vf4& v2, const Vec3vf4& v3, 
                                   const GridMesh::Grid &g, const SubGrid& subgrid) const
      {
          return intersect(ray,v0,v1,v2,v3,g,subgrid,Intersect1EpilogM<8,8,filter>(ray,context,vint8(subgrid.geomID()),vint8(subgrid.primID())));
      }
      
      __forceinline bool occluded(Ray& ray, IntersectContext* context,
                                  const Vec3vf4& v0, const Vec3vf4& v1, const Vec3vf4& v2, const Vec3vf4& v3, 
                                  const GridMesh::Grid &g, const SubGrid& subgrid) const
      {
          return intersect(ray,v0,v1,v2,v3,g,subgrid,Occluded1EpilogM<8,8,filter>(ray,context,vint8(subgrid.geomID()),vint8(subgrid.primID())));
      }
    };

#endif

    /* ----------------------------- */
    /* -- ray packet intersectors -- */
    /* ----------------------------- */


    template<int M, int K, bool filter>
      struct SubGridQuadMIntersectorKMoellerTrumboreBase
      {
        __forceinline SubGridQuadMIntersectorKMoellerTrumboreBase(const vbool<K>& valid, const RayK<K>& ray) {}
            
        /*! Intersects K rays with one of M triangles. */
        template<typename Epilog>
        __forceinline vbool<K> intersectK(const vbool<K>& valid0,
                                          RayK<K>& ray,
                                          const Vec3vf<K>& tri_v0,
                                          const Vec3vf<K>& tri_e1,
                                          const Vec3vf<K>& tri_e2,
                                          const Vec3vf<K>& tri_Ng,
                                          const vbool<K>& flags,
                                          const GridMesh::Grid &g, 
                                          const SubGrid &subgrid,
                                          const size_t i,
                                          const Epilog& epilog) const
        { 
          /* calculate denominator */
          vbool<K> valid = valid0;
          const Vec3vf<K> C = tri_v0 - ray.org;
          const Vec3vf<K> R = cross(C,ray.dir);
          const vfloat<K> den = dot(tri_Ng,ray.dir);
          const vfloat<K> absDen = abs(den);
          const vfloat<K> sgnDen = signmsk(den);
        
          /* test against edge p2 p0 */
          const vfloat<K> U = dot(R,tri_e2) ^ sgnDen;
          valid &= U >= 0.0f;
          if (likely(none(valid))) return false;
        
          /* test against edge p0 p1 */
          const vfloat<K> V = dot(R,tri_e1) ^ sgnDen;
          valid &= V >= 0.0f;
          if (likely(none(valid))) return false;
        
          /* test against edge p1 p2 */
          const vfloat<K> W = absDen-U-V;
          valid &= W >= 0.0f;
          if (likely(none(valid))) return false;
        
          /* perform depth test */
          const vfloat<K> T = dot(tri_Ng,C) ^ sgnDen;
          valid &= (absDen*ray.tnear() < T) & (T <= absDen*ray.tfar);
          if (unlikely(none(valid))) return false;
        
          /* perform backface culling */
#if defined(EMBREE_BACKFACE_CULLING)
          valid &= den < vfloat<K>(zero);
          if (unlikely(none(valid))) return false;
#else
          valid &= den != vfloat<K>(zero);
          if (unlikely(none(valid))) return false;
#endif
        
          /* calculate hit information */
          SubGridQuadHitK<K> hit(U,V,T,absDen,tri_Ng,flags,g,subgrid,i);
          return epilog(valid,hit);
        }
      
        /*! Intersects K rays with one of M quads. */
        template<typename Epilog>
        __forceinline vbool<K> intersectK(const vbool<K>& valid0, 
                                          RayK<K>& ray,
                                          const Vec3vf<K>& tri_v0,
                                          const Vec3vf<K>& tri_v1,
                                          const Vec3vf<K>& tri_v2,
                                          const vbool<K>& flags,
                                          const GridMesh::Grid &g, 
                                          const SubGrid &subgrid,
                                          const size_t i,
                                          const Epilog& epilog) const
        {
          const Vec3vf<K> e1 = tri_v0-tri_v1;
          const Vec3vf<K> e2 = tri_v2-tri_v0;
          const Vec3vf<K> Ng = cross(e2,e1);
          return intersectK(valid0,ray,tri_v0,e1,e2,Ng,flags,g,subgrid,i,epilog);
        }

        /*! Intersects K rays with one of M quads. */
        template<typename Epilog>
        __forceinline bool intersectK(const vbool<K>& valid0, 
                                      RayK<K>& ray,
                                      const Vec3vf<K>& v0,
                                      const Vec3vf<K>& v1,
                                      const Vec3vf<K>& v2,
                                      const Vec3vf<K>& v3,
                                      const GridMesh::Grid &g, 
                                      const SubGrid &subgrid,
                                      const size_t i,
                                      const Epilog& epilog) const
        {
          intersectK(valid0,ray,v0,v1,v3,vbool<K>(false),g,subgrid,i,epilog);
          if (none(valid0)) return true;
          intersectK(valid0,ray,v2,v3,v1,vbool<K>(true ),g,subgrid,i,epilog);
          return none(valid0);
        }

      /*! Intersect k'th ray from ray packet of size K with M triangles. */
        static  __forceinline bool intersect1(RayK<K>& ray,
                                              size_t k,
                                              const Vec3vf<M>& tri_v0,
                                              const Vec3vf<M>& tri_e1,
                                              const Vec3vf<M>& tri_e2,
                                              const Vec3vf<M>& tri_Ng,
                                              MoellerTrumboreHitM<M> &hit)
        {
          /* calculate denominator */
          const Vec3vf<M> O = broadcast<vfloat<M>>(ray.org,k);
          const Vec3vf<M> D = broadcast<vfloat<M>>(ray.dir,k);
          const Vec3vf<M> C = Vec3vf<M>(tri_v0) - O;
          const Vec3vf<M> R = cross(C,D);
          const vfloat<M> den = dot(Vec3vf<M>(tri_Ng),D);
          const vfloat<M> absDen = abs(den);
          const vfloat<M> sgnDen = signmsk(den);
        
          /* perform edge tests */
          const vfloat<M> U = dot(R,Vec3vf<M>(tri_e2)) ^ sgnDen;
          const vfloat<M> V = dot(R,Vec3vf<M>(tri_e1)) ^ sgnDen;
        
          /* perform backface culling */
#if defined(EMBREE_BACKFACE_CULLING)
          vbool<M> valid = (den < vfloat<M>(zero)) & (U >= 0.0f) & (V >= 0.0f) & (U+V<=absDen);
#else
          vbool<M> valid = (den != vfloat<M>(zero)) & (U >= 0.0f) & (V >= 0.0f) & (U+V<=absDen);
#endif
          if (likely(none(valid))) return false;
        
          /* perform depth test */
          const vfloat<M> T = dot(Vec3vf<M>(tri_Ng),C) ^ sgnDen;
          valid &= (absDen*vfloat<M>(ray.tnear()[k]) < T) & (T <= absDen*vfloat<M>(ray.tfar[k]));
          if (likely(none(valid))) return false;
        
          /* calculate hit information */
          new (&hit) MoellerTrumboreHitM<M>(valid,U,V,T,absDen,tri_Ng);
          return true;
        }

        static __forceinline bool intersect1(RayK<K>& ray,
                                             size_t k,
                                             const Vec3vf<M>& v0,
                                             const Vec3vf<M>& v1,
                                             const Vec3vf<M>& v2,
                                             MoellerTrumboreHitM<M> &hit)
        {
          const Vec3vf<M> e1 = v0-v1;
          const Vec3vf<M> e2 = v2-v0;
          const Vec3vf<M> Ng = cross(e2,e1);
          return intersect1(ray,k,v0,e1,e2,Ng,hit);
        }

      };

    template<int M, int K, bool filter>
      struct SubGridQuadMIntersectorKMoellerTrumbore : public SubGridQuadMIntersectorKMoellerTrumboreBase<M,K,filter>
    {
      __forceinline SubGridQuadMIntersectorKMoellerTrumbore(const vbool<K>& valid, const RayK<K>& ray)
        : SubGridQuadMIntersectorKMoellerTrumboreBase<M,K,filter>(valid,ray) {}

      __forceinline void intersect1(RayHitK<K>& ray, size_t k, IntersectContext* context,
                                    const Vec3vf<M>& v0, const Vec3vf<M>& v1, const Vec3vf<M>& v2, const Vec3vf<M>& v3, const GridMesh::Grid &g, const SubGrid &subgrid) const
      {
        const vint<M> gIDs(subgrid.geomID()); // needs to be explicity here, otherwise clang produces wrong code
        const vint<M> pIDs(subgrid.primID());

        Intersect1KEpilogM<M,M,K,filter> epilog(ray,k,context,gIDs,pIDs);

        MoellerTrumboreHitM<4> hit;
        if (SubGridQuadMIntersectorKMoellerTrumboreBase<4,K,filter>::intersect1(ray,k,v0,v1,v3,hit))
        {
          interpolateUV<M>(hit,g,subgrid);
          epilog(hit.valid,hit);
        }

        if (SubGridQuadMIntersectorKMoellerTrumboreBase<4,K,filter>::intersect1(ray,k,v2,v3,v1,hit))
        {
          hit.U = hit.absDen - hit.U;
          hit.V = hit.absDen - hit.V;
          interpolateUV<M>(hit,g,subgrid);
          epilog(hit.valid,hit);
        }

      }
      
      __forceinline bool occluded1(RayK<K>& ray, size_t k, IntersectContext* context,
                                   const Vec3vf<M>& v0, const Vec3vf<M>& v1, const Vec3vf<M>& v2, const Vec3vf<M>& v3, const GridMesh::Grid &g, const SubGrid &subgrid) const
      {
        const vint<M> gIDs(subgrid.geomID()); // needs to be explicity here, otherwise clang produces wrong code
        const vint<M> pIDs(subgrid.primID());

        Occluded1KEpilogM<M,M,K,filter> epilog(ray,k,context,gIDs,pIDs);

        MoellerTrumboreHitM<4> hit;
        if (SubGridQuadMIntersectorKMoellerTrumboreBase<4,K,filter>::intersect1(ray,k,v0,v1,v3,hit))
        {
          interpolateUV<M>(hit,g,subgrid);
          if (epilog(hit.valid,hit)) return true;
        }

        if (SubGridQuadMIntersectorKMoellerTrumboreBase<4,K,filter>::intersect1(ray,k,v2,v3,v1,hit))
        {
          hit.U = hit.absDen - hit.U;
          hit.V = hit.absDen - hit.V;
          interpolateUV<M>(hit,g,subgrid);
          if (epilog(hit.valid,hit)) return true;
        }
        return false;
      }
    };


#if defined(__AVX512F__)

    /*! Intersects 4 quads with 1 ray using AVX512 */
    template<int K, bool filter>
      struct SubGridQuadMIntersectorKMoellerTrumbore<4,K,filter> : public SubGridQuadMIntersectorKMoellerTrumboreBase<4,K,filter>
    {
      __forceinline SubGridQuadMIntersectorKMoellerTrumbore(const vbool<K>& valid, const RayK<K>& ray)
        : SubGridQuadMIntersectorKMoellerTrumboreBase<4,K,filter>(valid,ray) {}

      template<typename Epilog>
        __forceinline bool intersect1(RayK<K>& ray, size_t k,
                                      const Vec3vf4& v0, const Vec3vf4& v1, const Vec3vf4& v2, const Vec3vf4& v3, const GridMesh::Grid &g, const SubGrid &subgrid, const Epilog& epilog) const
      {
        const Vec3vf16 vtx0(select(0x0f0f,vfloat16(v0.x),vfloat16(v2.x)),
                            select(0x0f0f,vfloat16(v0.y),vfloat16(v2.y)),
                            select(0x0f0f,vfloat16(v0.z),vfloat16(v2.z)));
#if !defined(EMBREE_BACKFACE_CULLING)
        const Vec3vf16 vtx1(vfloat16(v1.x),vfloat16(v1.y),vfloat16(v1.z));
        const Vec3vf16 vtx2(vfloat16(v3.x),vfloat16(v3.y),vfloat16(v3.z));
#else
        const Vec3vf16 vtx1(select(0x0f0f,vfloat16(v1.x),vfloat16(v3.x)),
                            select(0x0f0f,vfloat16(v1.y),vfloat16(v3.y)),
                            select(0x0f0f,vfloat16(v1.z),vfloat16(v3.z)));
        const Vec3vf16 vtx2(select(0x0f0f,vfloat16(v3.x),vfloat16(v1.x)),
                            select(0x0f0f,vfloat16(v3.y),vfloat16(v1.y)),
                            select(0x0f0f,vfloat16(v3.z),vfloat16(v1.z)));
#endif
        const vbool16 flags(0xf0f0);
        MoellerTrumboreHitM<16> hit;
        if (SubGridQuadMIntersectorKMoellerTrumboreBase<16,K,filter>::intersect1(ray,k,vtx0,vtx1,vtx2,hit))
        {
          vfloat16 U = hit.U, V = hit.V, absDen = hit.absDen;
#if !defined(EMBREE_BACKFACE_CULLING)
          hit.U = select(flags,absDen-V,U);
          hit.V = select(flags,absDen-U,V);
          hit.vNg *= select(flags,vfloat16(-1.0f),vfloat16(1.0f)); // FIXME: use XOR
#else
          hit.U = select(flags,absDen-U,U);
          hit.V = select(flags,absDen-V,V);
#endif

          /* correct U,V interpolation across the entire grid */
          const vint16 sx((int)subgrid.x());
          const vint16 sy((int)subgrid.y());
          const vint16 sx16(sx + vint16(0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0));
          const vint16 sy16(sy + vint16(0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1));
          const float inv_resX = rcp((float)((int)g.resX-1));
          const float inv_resY = rcp((float)((int)g.resY-1));          
          hit.U = (hit.U + (vfloat16)sx16 * absDen) * inv_resX;
          hit.V = (hit.V + (vfloat16)sy16 * absDen) * inv_resY;          
          if (unlikely(epilog(hit.valid,hit)))
            return true;
        }
        return false;
      }
      
      __forceinline bool intersect1(RayHitK<K>& ray, size_t k, IntersectContext* context,
                                    const Vec3vf4& v0, const Vec3vf4& v1, const Vec3vf4& v2, const Vec3vf4& v3,const GridMesh::Grid &g, const SubGrid &subgrid) const
      {
        return intersect1(ray,k,v0,v1,v2,v3,g,subgrid,Intersect1KEpilogM<8,16,K,filter>(ray,k,context,vint8(subgrid.geomID()),vint8(subgrid.primID())));
      }
      
      __forceinline bool occluded1(RayK<K>& ray, size_t k, IntersectContext* context,
                                   const Vec3vf4& v0, const Vec3vf4& v1, const Vec3vf4& v2, const Vec3vf4& v3, const GridMesh::Grid &g, const SubGrid &subgrid) const
      {
        return intersect1(ray,k,v0,v1,v2,v3,g,subgrid,Occluded1KEpilogM<8,16,K,filter>(ray,k,context,vint8(subgrid.geomID()),vint8(subgrid.primID())));
      }
    };

#elif defined (__AVX__)

    /*! Intersects 4 quads with 1 ray using AVX */
    template<int K, bool filter>
      struct SubGridQuadMIntersectorKMoellerTrumbore<4,K,filter> : public SubGridQuadMIntersectorKMoellerTrumboreBase<4,K,filter>
    {
      __forceinline SubGridQuadMIntersectorKMoellerTrumbore(const vbool<K>& valid, const RayK<K>& ray)
        : SubGridQuadMIntersectorKMoellerTrumboreBase<4,K,filter>(valid,ray) {}
      
      template<typename Epilog>
        __forceinline bool intersect1(RayK<K>& ray, size_t k,const Vec3vf4& v0, const Vec3vf4& v1, const Vec3vf4& v2, const Vec3vf4& v3, 
                                      const GridMesh::Grid &g, const SubGrid &subgrid, const Epilog& epilog) const
      {
        const Vec3vf8 vtx0(vfloat8(v0.x,v2.x),vfloat8(v0.y,v2.y),vfloat8(v0.z,v2.z));
#if !defined(EMBREE_BACKFACE_CULLING)
        const Vec3vf8 vtx1(vfloat8(v1.x),vfloat8(v1.y),vfloat8(v1.z));
        const Vec3vf8 vtx2(vfloat8(v3.x),vfloat8(v3.y),vfloat8(v3.z));
#else
        const Vec3vf8 vtx1(vfloat8(v1.x,v3.x),vfloat8(v1.y,v3.y),vfloat8(v1.z,v3.z));
        const Vec3vf8 vtx2(vfloat8(v3.x,v1.x),vfloat8(v3.y,v1.y),vfloat8(v3.z,v1.z));
#endif
        const vbool8 flags(0,0,0,0,1,1,1,1);

        MoellerTrumboreHitM<8> hit;
        if (SubGridQuadMIntersectorKMoellerTrumboreBase<8,K,filter>::intersect1(ray,k,vtx0,vtx1,vtx2,hit))
        {
          vfloat8 U = hit.U, V = hit.V, absDen = hit.absDen;
#if !defined(EMBREE_BACKFACE_CULLING)
          hit.U = select(flags,absDen-V,U);
          hit.V = select(flags,absDen-U,V);
          hit.vNg *= select(flags,vfloat8(-1.0f),vfloat8(1.0f)); // FIXME: use XOR
#else
          hit.U = select(flags,absDen-U,U);
          hit.V = select(flags,absDen-V,V);
#endif

          /* correct U,V interpolation across the entire grid */
          const vint8 sx((int)subgrid.x());
          const vint8 sy((int)subgrid.y());
          const vint8 sx8(sx + vint8(0,1,1,0,0,1,1,0));
          const vint8 sy8(sy + vint8(0,0,1,1,0,0,1,1));
          const float inv_resX = rcp((float)((int)g.resX-1));
          const float inv_resY = rcp((float)((int)g.resY-1));          
          hit.U = (hit.U + (vfloat8)sx8 * absDen) * inv_resX;
          hit.V = (hit.V + (vfloat8)sy8 * absDen) * inv_resY;          
          if (unlikely(epilog(hit.valid,hit)))
            return true;

        }
        return false;
      }
      
      __forceinline bool intersect1(RayHitK<K>& ray, size_t k, IntersectContext* context,
                                    const Vec3vf4& v0, const Vec3vf4& v1, const Vec3vf4& v2, const Vec3vf4& v3, const GridMesh::Grid &g, const SubGrid &subgrid) const
      {
        return intersect1(ray,k,v0,v1,v2,v3,g,subgrid,Intersect1KEpilogM<8,8,K,filter>(ray,k,context,vint8(subgrid.geomID()),vint8(subgrid.primID())));
      }
      
      __forceinline bool occluded1(RayK<K>& ray, size_t k, IntersectContext* context,
                                   const Vec3vf4& v0, const Vec3vf4& v1, const Vec3vf4& v2, const Vec3vf4& v3, const GridMesh::Grid &g, const SubGrid &subgrid) const
      {
        return intersect1(ray,k,v0,v1,v2,v3,g,subgrid,Occluded1KEpilogM<8,8,K,filter>(ray,k,context,vint8(subgrid.geomID()),vint8(subgrid.primID())));
      }
    };

#endif

    // =======================================================================================
    // =================================== SubGridIntersectors ===============================
    // =======================================================================================



    /*! Intersects M quads with 1 ray */
    template<int N, bool filter>
    struct SubGridIntersector1Moeller
    {
      //typedef SubGrid Primitive;
      typedef SubGridQBVHN<N> Primitive;
      typedef SubGridQuadMIntersector1MoellerTrumbore<4,filter> Precalculations;

      /*! Intersect a ray with the M quads and updates the hit. */
      static __forceinline void intersect(const Precalculations& pre, RayHit& ray, IntersectContext* context, const SubGrid& subgrid)
      {
        STAT3(normal.trav_prims,1,1,1);
        const GridMesh* mesh    = context->scene->get<GridMesh>(subgrid.geomID());
        const GridMesh::Grid &g = mesh->grid(subgrid.primID());

        Vec3vf4 v0,v1,v2,v3; subgrid.gather(v0,v1,v2,v3,context->scene);
        pre.intersect(ray,context,v0,v1,v2,v3,g,subgrid);
      }

      /*! Test if the ray is occluded by one of M subgrids. */
      static __forceinline bool occluded(const Precalculations& pre, Ray& ray, IntersectContext* context, const SubGrid& subgrid)
      {
        STAT3(shadow.trav_prims,1,1,1);
        const GridMesh* mesh    = context->scene->get<GridMesh>(subgrid.geomID());
        const GridMesh::Grid &g = mesh->grid(subgrid.primID());

        Vec3vf4 v0,v1,v2,v3; subgrid.gather(v0,v1,v2,v3,context->scene);
        return pre.occluded(ray,context,v0,v1,v2,v3,g,subgrid);
      }

      template<int Nx, bool robust>
        static __forceinline void intersect(const Accel::Intersectors* This, Precalculations& pre, RayHit& ray, IntersectContext* context, const Primitive* prim, size_t num, const TravRay<N,Nx,robust> &tray, size_t& lazy_node)
      {
        for (size_t i=0;i<num;i++)
        {
          vfloat<Nx> dist;
          /* QBVH intersection test */
          size_t mask = intersectNode(&prim[i].qnode,tray,dist); //FIXME: maybe do node ordering here
          while(mask != 0)
          {
            const size_t ID = __bscf(mask); 
            intersect(pre,ray,context,prim[i].subgrid(ID));
          }
        }
      }

      template<int Nx, bool robust>        
        static __forceinline bool occluded(const Accel::Intersectors* This, Precalculations& pre, Ray& ray, IntersectContext* context, const Primitive* prim, size_t num, const TravRay<N,Nx,robust> &tray, size_t& lazy_node)
      {
        for (size_t i=0;i<num;i++)
        {
          vfloat<Nx> dist;
          /* QBVH intersection test */
          size_t mask = intersectNode(&prim[i].qnode,tray,dist); 
          while(mask != 0)
          {
            const size_t ID = __bscf(mask); 
            if (occluded(pre,ray,context,prim[i].subgrid(ID)))
              return true;
          }
        }
        return false;
      }


    };


    /*! Intersects M triangles with K rays. */
    template<int N, int K, bool filter>
    struct SubGridIntersectorKMoeller
    {
      typedef SubGridQBVHN<N> Primitive;
      typedef SubGridQuadMIntersectorKMoellerTrumbore<4,K,filter> Precalculations;

      /*! Intersects K rays with M triangles. */
      static __forceinline void intersect(const vbool<K>& valid_i, Precalculations& pre, RayHitK<K>& ray, IntersectContext* context, const SubGrid& subgrid)
      {
        Vec3fa vtx[16];
        const GridMesh* mesh    = context->scene->get<GridMesh>(subgrid.geomID());
        const GridMesh::Grid &g = mesh->grid(subgrid.primID());

        subgrid.gather(vtx,context->scene);
        for (size_t i=0;i<4;i++)
        {
          const Vec3vf<K> p0 = vtx[i*4+0];
          const Vec3vf<K> p1 = vtx[i*4+1];
          const Vec3vf<K> p2 = vtx[i*4+2];
          const Vec3vf<K> p3 = vtx[i*4+3];
          STAT3(normal.trav_prims,1,popcnt(valid_i),K);
          pre.intersectK(valid_i,ray,p0,p1,p2,p3,g,subgrid,i,IntersectKEpilogM<4,K,filter>(ray,context,subgrid.geomID(),subgrid.primID(),i));
        }
      }

      /*! Test for K rays if they are occluded by any of the M triangles. */
      static __forceinline vbool<K> occluded(const vbool<K>& valid_i, Precalculations& pre, RayK<K>& ray, IntersectContext* context, const SubGrid& subgrid)
      {
        vbool<K> valid0 = valid_i;
        Vec3fa vtx[16];
        const GridMesh* mesh    = context->scene->get<GridMesh>(subgrid.geomID());
        const GridMesh::Grid &g = mesh->grid(subgrid.primID());

        subgrid.gather(vtx,context->scene);
        for (size_t i=0;i<4;i++)
        {
          const Vec3vf<K> p0 = vtx[i*4+0];
          const Vec3vf<K> p1 = vtx[i*4+1];
          const Vec3vf<K> p2 = vtx[i*4+2];
          const Vec3vf<K> p3 = vtx[i*4+3];
          STAT3(shadow.trav_prims,1,popcnt(valid0),K);
          if (pre.intersectK(valid0,ray,p0,p1,p2,p3,g,subgrid,i,OccludedKEpilogM<4,K,filter>(valid0,ray,context,subgrid.geomID(),subgrid.primID(),i)))
            break;
        }
        return !valid0;
      }

      /*! Intersect a ray with M triangles and updates the hit. */
      static __forceinline void intersect(Precalculations& pre, RayHitK<K>& ray, size_t k, IntersectContext* context, const SubGrid& subgrid)
      {
        STAT3(normal.trav_prims,1,1,1);
        const GridMesh* mesh    = context->scene->get<GridMesh>(subgrid.geomID());
        const GridMesh::Grid &g = mesh->grid(subgrid.primID());

        Vec3vf4 v0,v1,v2,v3; subgrid.gather(v0,v1,v2,v3,context->scene);
        pre.intersect1(ray,k,context,v0,v1,v2,v3,g,subgrid);
      }

      /*! Test if the ray is occluded by one of the M triangles. */
      static __forceinline bool occluded(Precalculations& pre, RayK<K>& ray, size_t k, IntersectContext* context, const SubGrid& subgrid)
      {
        STAT3(shadow.trav_prims,1,1,1);
        const GridMesh* mesh    = context->scene->get<GridMesh>(subgrid.geomID());
        const GridMesh::Grid &g = mesh->grid(subgrid.primID());
        Vec3vf4 v0,v1,v2,v3; subgrid.gather(v0,v1,v2,v3,context->scene);
        return pre.occluded1(ray,k,context,v0,v1,v2,v3,g,subgrid);
      }

        template<bool robust>
          static __forceinline void intersect(const vbool<K>& valid, const Accel::Intersectors* This, Precalculations& pre, RayHitK<K>& ray, IntersectContext* context, const Primitive* prim, size_t num, const TravRayK<K, robust> &tray, size_t& lazy_node)
        {
          for (size_t j=0;j<num;j++)
          {
            const size_t items = prim[j].size();
            vfloat<K> dist;
            for (size_t i=0;i<items;i++)
            {
              if (none(valid & intersectNodeK<N>(&prim[j].qnode,i,tray,dist))) continue;
              intersect(valid,pre,ray,context,prim[j].subgrid(i));
            }
          }
        }

        template<bool robust>        
        static __forceinline vbool<K> occluded(const vbool<K>& valid, const Accel::Intersectors* This, Precalculations& pre, RayK<K>& ray, IntersectContext* context, const Primitive* prim, size_t num, const TravRayK<K, robust> &tray, size_t& lazy_node)
        {
          vbool<K> valid0 = valid;
          for (size_t j=0;j<num;j++)
          {
            const size_t items = prim[j].size();
            vfloat<K> dist;
            for (size_t i=0; i<items; i++) {
              if (none(valid0 & intersectNodeK<N>(&prim[j].qnode,i,tray,dist))) continue;
              valid0 &= !occluded(valid0,pre,ray,context,prim[j].subgrid(i));
              if (none(valid0)) break;
            }
          }
          return !valid0;
        }

        template<int Nx, bool robust>        
          static __forceinline void intersect(const Accel::Intersectors* This, Precalculations& pre, RayHitK<K>& ray, size_t k, IntersectContext* context, const Primitive* prim, size_t num, const TravRay<N,Nx,robust> &tray, size_t& lazy_node)
        {
          for (size_t i=0;i<num;i++)
          {
            vfloat<Nx> dist;
            /* QBVH intersection test */
            size_t mask = intersectNode(&prim[i].qnode,tray,dist); //FIXME: maybe do node ordering here
            while(mask != 0)
            {
              const size_t ID = __bscf(mask); 
              intersect(pre,ray,k,context,prim[i].subgrid(ID));
            }
          }
        }
        
        template<int Nx, bool robust>
        static __forceinline bool occluded(const Accel::Intersectors* This, Precalculations& pre, RayK<K>& ray, size_t k, IntersectContext* context, const Primitive* prim, size_t num, const TravRay<N,Nx,robust> &tray, size_t& lazy_node)
        {
          for (size_t i=0;i<num;i++)
          {
            vfloat<Nx> dist;
            /* QBVH intersection test */
            size_t mask = intersectNode(&prim[i].qnode,tray,dist); 
            while(mask != 0)
            {
              const size_t ID = __bscf(mask); 
              if (occluded(pre,ray,k,context,prim[i].subgrid(ID)))
                return true;
            }
          }
          return false;
        }


    };


  }
}
