// Copyright (c) 2010-2023, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "../general/forall.hpp"
#include "../mesh/face_nbr_geom.hpp"
#include "gridfunc.hpp"
#include "qfunction.hpp"
#include "fe/face_map_utils.hpp"

using namespace std;

namespace mfem
{

static void PADGDiffusionSetup2D(const int Q1D,
                                 const int NE,
                                 const int NF,
                                 const Array<double> &w,
                                 const GeometricFactors &el_geom,
                                 const FaceGeometricFactors &face_geom,
                                 const FaceNeighborGeometricFactors *nbr_geom,
                                 const Vector &q,
                                 const double sigma,
                                 const double kappa,
                                 Vector &pa_data,
                                 const Array<int> &face_info_)
{
   const auto J_loc = Reshape(el_geom.J.Read(), Q1D, Q1D, 2, 2, NE);
   const auto detJe_loc = Reshape(el_geom.detJ.Read(), Q1D, Q1D, NE);

   const int n_nbr = nbr_geom ? nbr_geom->num_neighbor_elems : 0;
   const auto J_shared = Reshape(nbr_geom ? nbr_geom->J.Read() : nullptr,
                                 Q1D, Q1D, 2, 2, n_nbr);
   const auto detJ_shared = Reshape(nbr_geom ? nbr_geom->detJ.Read() : nullptr,
                                    Q1D, Q1D, n_nbr);

   const auto detJf = Reshape(face_geom.detJ.Read(), Q1D, NF);
   const auto n = Reshape(face_geom.normal.Read(), Q1D, 2, NF);

   const bool const_q = (q.Size() == 1);
   const auto Q = const_q ? Reshape(q.Read(), 1,1) : Reshape(q.Read(), Q1D,NF);

   const auto W = w.Read();

   // (normal0, normal1, e0, e1, fid0, fid1)
   const auto face_info = Reshape(face_info_.Read(), 6, NF);

   // (q, 1/h, J0_0, J0_1, J1_0, J1_1)
   auto pa = Reshape(pa_data.Write(), 6, Q1D, NF);

   mfem::forall(NF, [=] MFEM_HOST_DEVICE (int f) -> void
   {
      const int normal_dir[] = {face_info(0, f), face_info(1, f)};
      const int fid[] = {face_info(4, f), face_info(5, f)};

      int el[] = {face_info(2, f), face_info(3, f)};
      const bool interior = el[1] >= 0;
      const int nsides = (interior) ? 2 : 1;
      const double factor = interior ? 0.5 : 1.0;

      const bool shared = el[1] >= NE;
      el[1] = shared ? el[1] - NE : el[1];

      const int sgn0 = (fid[0] == 0 || fid[0] == 1) ? 1 : -1;
      const int sgn1 = (fid[1] == 0 || fid[1] == 1) ? 1 : -1;

      for (int p = 0; p < Q1D; ++p)
      {
         const double Qp = const_q ? Q(0,0) : Q(p, f);
         pa(0, p, f) = kappa * Qp * W[p] * detJf(p, f);

         double hi = 0.0;
         for (int side = 0; side < nsides; ++side)
         {
            int i, j;
            internal::FaceIdxToVolIdx2D(p, Q1D, fid[0], fid[1], side, i, j);

            // Always opposite direction in "native" ordering
            // Need to multiply the native=>lex0 with native=>lex1 and negate
            const int sgn = (side == 1) ? -1*sgn0*sgn1 : 1;

            const int e = el[side];
            const auto &J = (side == 1 && shared) ? J_shared : J_loc;
            const auto &detJ = (side == 1 && shared) ? detJ_shared : detJe_loc;

            double nJi[2];
            nJi[0] = n(p,0,f)*J(i,j, 1,1, e) - n(p,1,f)*J(i,j,0,1,e);
            nJi[1] = -n(p,0,f)*J(i,j,1,0, e) + n(p,1,f)*J(i,j,0,0,e);

            const double dJe = detJ(i,j,e);
            const double dJf = detJf(p, f);

            const double w = factor * Qp * W[p] * dJf / dJe;

            const int ni = normal_dir[side];
            const int ti = 1 - ni;

            // Normal
            pa(2 + 2*side + 0, p, f) = w * nJi[ni];
            // Tangential
            pa(2 + 2*side + 1, p, f) = sgn * w * nJi[ti];

            hi += factor * dJf / dJe;
         }

         if (nsides == 1)
         {
            pa(4, p, f) = 0.0;
            pa(5, p, f) = 0.0;
         }

         pa(1, p, f) = hi;
      }
   });
}

static void PADGDiffusionSetup3D(const int Q1D,
                                 const int NE,
                                 const int NF,
                                 const Array<double> &w,
                                 const GeometricFactors &el_geom,
                                 const FaceGeometricFactors &face_geom,
                                 const FaceNeighborGeometricFactors *nbr_geom,
                                 const Vector &q,
                                 const double sigma,
                                 const double kappa,
                                 Vector &pa_data,
                                 const Array<int> &face_info_)
{
   const auto J_loc = Reshape(el_geom.J.Read(), Q1D, Q1D, Q1D, 3, 3, NE);
   const auto detJe_loc = Reshape(el_geom.detJ.Read(), Q1D, Q1D, Q1D, NE);

   const int n_nbr = nbr_geom ? nbr_geom->num_neighbor_elems : 0;
   const auto J_shared = Reshape(nbr_geom ? nbr_geom->J.Read() : nullptr,
                                 Q1D, Q1D, Q1D, 3, 3, n_nbr);
   const auto detJ_shared = Reshape(nbr_geom ? nbr_geom->detJ.Read() : nullptr,
                                    Q1D, Q1D, Q1D, n_nbr);

   const auto detJf = Reshape(face_geom.detJ.Read(), Q1D, Q1D, NF);
   const auto n = Reshape(face_geom.normal.Read(), Q1D, Q1D, 3, NF);

   const bool const_q = (q.Size() == 1);
   const auto Q = const_q ? Reshape(q.Read(), 1, 1, 1)
                  : Reshape(q.Read(), Q1D, Q1D, NF);

   const auto W = Reshape(w.Read(), Q1D, Q1D);

   // (perm[0], perm[1], perm[2], element_index, local_face_id, orientation)
   const auto face_info = Reshape(face_info_.Read(), 6, 2, NF);
   constexpr int _el_ = 3; // offset in face_info for element index
   constexpr int _fid_ = 4; // offset in face_info for local face id
   constexpr int _or_ = 5; // offset in face_info for orientation

   // (q, 1/h, J00, J01, J02, J10, J11, J12)
   const auto pa = Reshape(pa_data.Write(), 8, Q1D, Q1D, NF);

   mfem::forall_2D(NF, Q1D, Q1D, [=] MFEM_HOST_DEVICE (int f) -> void
   {
      MFEM_SHARED int perm[2][3];
      MFEM_SHARED int el[2];
      MFEM_SHARED bool shared[2];
      MFEM_SHARED int fid[2];
      MFEM_SHARED int ortn[2];

      MFEM_FOREACH_THREAD(side, x, 2)
      {
         MFEM_FOREACH_THREAD(i, y, 3)
         {
            perm[side][i] = face_info(i, side, f);
         }

         if (MFEM_THREAD_ID(y) == 0)
         {
            el[side] = face_info(_el_, side, f);
            fid[side] = face_info(_fid_, side, f);
            ortn[side] = face_info(_or_, side, f);

            // If the element index is beyond the local partition NE, then the
            // element is a "face neighbor" element.
            shared[side] = (el[side] >= NE);
            el[side] = shared[side] ? el[side] - NE : el[side];
         }
      }

      MFEM_SYNC_THREAD;

      const bool interior = el[1] >= 0;
      const int nsides = interior ? 2 : 1;
      const double factor = interior ? 0.5 : 1.0;

      MFEM_FOREACH_THREAD(p1, x, Q1D)
      {
         MFEM_FOREACH_THREAD(p2, y, Q1D)
         {
            const double Qp = const_q ? Q(0,0,0) : Q(p1, p2, f);
            const double dJf = detJf(p1,p2,f);
            pa(0, p1, p2, f) = kappa * Qp * W(p1, p2) * dJf;

            double hi = 0.0;

            for (int side = 0; side < nsides; ++side)
            {
               int i, j, k;
               internal::FaceIdxToVolIdx3D(
                  p1 + Q1D*p2, Q1D, fid[0], fid[1], side, ortn[1], i, j, k);

               const int e = el[side];
               const auto &J = shared[side] ? J_shared : J_loc;
               const auto &detJe = shared[side] ? detJ_shared : detJe_loc;

               // *INDENT-OFF*
               double nJi[3];
               nJi[0] = (  -J(i,j,k, 1,2, e)*J(i,j,k, 2,1, e) + J(i,j,k, 1,1, e)*J(i,j,k, 2,2, e)) * n(p1,p2, 0, f)
                        + ( J(i,j,k, 0,2, e)*J(i,j,k, 2,1, e) - J(i,j,k, 0,1, e)*J(i,j,k, 2,2, e)) * n(p1,p2, 1, f)
                        + (-J(i,j,k, 0,2, e)*J(i,j,k, 1,1, e) + J(i,j,k, 0,1, e)*J(i,j,k, 1,2, e)) * n(p1,p2, 2, f);

               nJi[1] = (   J(i,j,k, 1,2, e)*J(i,j,k, 2,0, e) - J(i,j,k, 1,0, e)*J(i,j,k, 2,2, e)) * n(p1,p2, 0, f)
                        + (-J(i,j,k, 0,2, e)*J(i,j,k, 2,0, e) + J(i,j,k, 0,0, e)*J(i,j,k, 2,2, e)) * n(p1,p2, 1, f)
                        + ( J(i,j,k, 0,2, e)*J(i,j,k, 1,0, e) - J(i,j,k, 0,0, e)*J(i,j,k, 1,2, e)) * n(p1,p2, 2, f);

               nJi[2] = (  -J(i,j,k, 1,1, e)*J(i,j,k, 2,0, e) + J(i,j,k, 1,0, e)*J(i,j,k, 2,1, e)) * n(p1,p2, 0, f)
                        + ( J(i,j,k, 0,1, e)*J(i,j,k, 2,0, e) - J(i,j,k, 0,0, e)*J(i,j,k, 2,1, e)) * n(p1,p2, 1, f)
                        + (-J(i,j,k, 0,1, e)*J(i,j,k, 1,0, e) + J(i,j,k, 0,0, e)*J(i,j,k, 1,1, e)) * n(p1,p2, 2, f);
               // *INDENT-ON*

               const double dJe = detJe(i,j,k,e);
               const double val = factor * Qp * W(p1, p2) * dJf / dJe;

               for (int d = 0; d < 3; ++d)
               {
                  const int idx = std::abs(perm[side][d]) - 1;
                  const int sgn = (perm[side][d] < 0) ? -1 : 1;
                  pa(2+3*side + d, p1, p2, f) = sgn * val * nJi[idx];
               }

               hi += factor * dJf / dJe;
            }

            if (nsides == 1)
            {
               pa(5, p1, p2, f) = 0.0;
               pa(6, p1, p2, f) = 0.0;
               pa(7, p1, p2, f) = 0.0;
            }

            pa(1, p1, p2, f) = hi;
         }
      }
   });
}

static void PADGDiffusionSetupFaceInfo2D(const int nf, const Mesh &mesh,
                                         const FaceType type, Array<int> &face_info_)
{
   const int ne = mesh.GetNE();

   int fidx = 0;
   face_info_.SetSize(nf * 6);

   // normal0 and normal1 are the indices of the face normal direction relative
   // to the element in reference coordinates, i.e. if the face is normal to the
   // x-vector (left or right face), then it will be 0, otherwise 1.

   // 2d: (normal0, normal1, e0, e1, fid0, fid1)
   auto face_info = Reshape(face_info_.HostWrite(), 6, nf);
   for (int f = 0; f < mesh.GetNumFaces(); ++f)
   {
      auto f_info = mesh.GetFaceInformation(f);

      if (f_info.IsOfFaceType(type))
      {
         const int face_id_1 = f_info.element[0].local_face_id;
         face_info(0, fidx) = (face_id_1 == 1 || face_id_1 == 3) ? 0 : 1;
         face_info(2, fidx) = f_info.element[0].index;
         face_info(4, fidx) = face_id_1;

         if (f_info.IsInterior())
         {
            const int face_id_2 = f_info.element[1].local_face_id;
            face_info(1, fidx) = (face_id_2 == 1 || face_id_2 == 3) ? 0 : 1;
            if (f_info.IsShared())
            {
               face_info(3, fidx) = ne + f_info.element[1].index;
            }
            else
            {
               face_info(3, fidx) = f_info.element[1].index;
            }
            face_info(5, fidx) = face_id_2;
         }
         else
         {
            face_info(1, fidx) = -1;
            face_info(3, fidx) = -1;
            face_info(5, fidx) = -1;
         }

         fidx++;
      }
   }
}

// Assigns to perm the permuation:
//    perm[0] <- normal component
//    perm[1] <- first tangential component
//    perm[2] <- second tangential component
//
// (Tangential components are ordering lexicographically).
inline void FaceNormalPermutation(int perm[3], const int face_id)
{
   const bool xy_plane = (face_id == 0 || face_id == 5);
   const bool xz_plane = (face_id == 1 || face_id == 3);
   // const bool yz_plane = (face_id == 2 || face_id == 4);

   perm[0] = (xy_plane) ? 3 : (xz_plane) ? 2 : 1;
   perm[1] = (xy_plane || xz_plane) ? 1 : 2;
   perm[2] = (xy_plane) ? 2 : 3;
}

// Assigns to perm the permutation as in FaceNormalPermutation for the second
// element on the face but signed to indicate the sign of the normal derivative.
inline void SignedFaceNormalPermutation(int perm[3],
                                        const int face_id1,
                                        const int face_id2,
                                        const int orientation)
{
   FaceNormalPermutation(perm, face_id2);

   // Sets perm according to the inverse of PermuteFace3D
   if (face_id2 == 3 || face_id2 == 4)
   {
      perm[1] *= -1;
   }
   else if (face_id2 == 0)
   {
      perm[2] *= -1;
   }

   switch (orientation)
   {
      case 1:
         std::swap(perm[1], perm[2]);
         break;
      case 2:
         std::swap(perm[1], perm[2]);
         perm[1] *= -1;
         break;
      case 3:
         perm[1] *= -1;
         break;
      case 4:
         perm[1] *= -1;
         perm[2] *= -1;
         break;
      case 5:
         std::swap(perm[1], perm[2]);
         perm[1] *= -1;
         perm[2] *= -1;
         break;
      case 6:
         std::swap(perm[1], perm[2]);
         perm[2] *= -1;
         break;
      case 7:
         perm[2] *= -1;
         break;
      default:
         break;
   }

   if (face_id1 == 3 || face_id1 == 4)
   {
      perm[1] *= -1;
   }
   else if (face_id1 == 0)
   {
      perm[2] *= -1;
   }
}

static void PADGDiffusionSetupFaceInfo3D(const int nf, const Mesh &mesh,
                                         const FaceType type, Array<int> &face_info_)
{
   const int ne = mesh.GetNE();

   int fidx = 0;
   // face_info array has 12 entries per face, 6 for each of the adjacent elements:
   // (perm[0], perm[1], perm[2], element_index, local_face_id, orientation)
   face_info_.SetSize(nf * 12);
   constexpr int _e_ = 3; // offset for element index
   constexpr int _fid_ = 4; // offset for local face id
   constexpr int _or_ = 5; // offset for orientation

   auto face_info = Reshape(face_info_.HostWrite(), 6, 2, nf);
   for (int f = 0; f < mesh.GetNumFaces(); ++f)
   {
      auto f_info = mesh.GetFaceInformation(f);

      if (f_info.IsOfFaceType(type))
      {
         const int fid0 = f_info.element[0].local_face_id;
         const int or0 = f_info.element[0].orientation;

         face_info(  _e_, 0, fidx) = f_info.element[0].index;
         face_info(_fid_, 0, fidx) = fid0;
         face_info( _or_, 0, fidx) = or0;

         FaceNormalPermutation(&face_info(0, 0, fidx), fid0);

         if (f_info.IsInterior())
         {
            const int fid1 = f_info.element[1].local_face_id;
            const int or1 = f_info.element[1].orientation;

            if (f_info.IsShared())
            {
               face_info(  _e_, 1, fidx) = ne + f_info.element[1].index;
            }
            else
            {
               face_info(  _e_, 1, fidx) = f_info.element[1].index;
            }
            face_info(_fid_, 1, fidx) = fid1;
            face_info( _or_, 1, fidx) = or1;

            SignedFaceNormalPermutation(&face_info(0, 1, fidx), fid0, fid1, or1);
         }
         else
         {
            for (int i = 0; i < 6; ++i)
            {
               face_info(i, 1, fidx) = -1;
            }
         }

         fidx++;
      }
   }
}

void DGDiffusionIntegrator::SetupPA(const FiniteElementSpace &fes,
                                    FaceType type)
{
   const MemoryType mt = (pa_mt == MemoryType::DEFAULT) ?
                         Device::GetDeviceMemoryType() : pa_mt;

   const int ne = fes.GetNE();
   nf = fes.GetNFbyType(type);

   // Assumes tensor-product elements
   Mesh &mesh = *fes.GetMesh();
   const FiniteElement &el =
      *fes.GetTraceElement(0, mesh.GetFaceGeometry(0));
   FaceElementTransformations &T0 =
      *fes.GetMesh()->GetFaceElementTransformations(0);
   const IntegrationRule &ir = IntRule ? *IntRule : GetRule(el.GetOrder(), T0);
   dim = mesh.Dimension();
   const int q1d = pow(double(ir.Size()), 1.0/(dim - 1));

   const auto vol_ir = irs.Get(mesh.GetElementGeometry(0), 2*q1d - 3);
   const auto geom_flags = GeometricFactors::JACOBIANS |
                           GeometricFactors::DETERMINANTS;
   const auto el_geom = mesh.GetGeometricFactors(vol_ir, geom_flags, mt);

   std::unique_ptr<FaceNeighborGeometricFactors> nbr_geom;
   if (type == FaceType::Interior)
   {
      nbr_geom.reset(new FaceNeighborGeometricFactors(*el_geom));
   }

   const auto face_geom_flags = FaceGeometricFactors::DETERMINANTS |
                                FaceGeometricFactors::NORMALS;
   auto face_geom = mesh.GetFaceGeometricFactors(ir, face_geom_flags, type, mt);
   maps = &el.GetDofToQuad(ir, DofToQuad::TENSOR);
   dofs1D = maps->ndof;
   quad1D = maps->nqpt;

   const int pa_size = (dim == 2) ? (6 * q1d * nf) : (8 * q1d * q1d * nf);
   pa_data.SetSize(pa_size, Device::GetMemoryType());

   // Evaluate the coefficient at the face quadrature points.
   FaceQuadratureSpace fqs(mesh, ir, type);
   CoefficientVector q(fqs, CoefficientStorage::COMPRESSED);
   if (Q) { q.Project(*Q); }
   else if (MQ) { MFEM_ABORT("Not yet implemented"); /* q.Project(*MQ); */ }
   else { q.SetConstant(1.0); }

   // Precompute face info arrays.
   Array<int> face_info;
   if (dim == 2)
   {
      PADGDiffusionSetupFaceInfo2D(nf, mesh, type, face_info);
   }
   else if (dim == 3)
   {
      PADGDiffusionSetupFaceInfo3D(nf, mesh, type, face_info);
   }

   if (dim == 1)
   {
      MFEM_ABORT("dim==1 not supported in PADGTraceSetup");
   }
   else if (dim == 2)
   {
      PADGDiffusionSetup2D(quad1D, ne, nf, ir.GetWeights(), *el_geom, *face_geom,
                           nbr_geom.get(), q, sigma, kappa, pa_data, face_info);
   }
   else if (dim == 3)
   {
      PADGDiffusionSetup3D(quad1D, ne, nf, ir.GetWeights(), *el_geom, *face_geom,
                           nbr_geom.get(), q, sigma, kappa, pa_data, face_info);
   }
}

void DGDiffusionIntegrator::AssemblePAInteriorFaces(
   const FiniteElementSpace &fes)
{
   SetupPA(fes, FaceType::Interior);
}

void DGDiffusionIntegrator::AssemblePABoundaryFaces(
   const FiniteElementSpace &fes)
{
   SetupPA(fes, FaceType::Boundary);
}

template<int T_D1D = 0, int T_Q1D = 0> static
void PADGDiffusionApply2D(const int NF,
                          const Array<double> &b,
                          const Array<double> &bt,
                          const Array<double>& g,
                          const Array<double>& gt,
                          const double sigma,
                          const double kappa,
                          const Vector &pa_data,
                          const Vector &x_,
                          const Vector &dxdn_,
                          Vector &y_,
                          Vector &dydn_,
                          const int d1d = 0,
                          const int q1d = 0)
{
   const int D1D = T_D1D ? T_D1D : d1d;
   const int Q1D = T_Q1D ? T_Q1D : q1d;
   MFEM_VERIFY(D1D <= MAX_D1D, "");
   MFEM_VERIFY(Q1D <= MAX_Q1D, "");

   auto B_ = Reshape(b.Read(), Q1D, D1D);
   auto G_ = Reshape(g.Read(), Q1D, D1D);

   auto pa = Reshape(pa_data.Read(), 6, Q1D, NF); // (q, 1/h, J00, J01, J10, J11)

   auto x =    Reshape(x_.Read(),         D1D, 2, NF);
   auto y =    Reshape(y_.ReadWrite(),    D1D, 2, NF);
   auto dxdn = Reshape(dxdn_.Read(),      D1D, 2, NF);
   auto dydn = Reshape(dydn_.ReadWrite(), D1D, 2, NF);

   const int NBX = std::max(D1D, Q1D);

   mfem::forall_2D(NF, NBX, 2, [=] MFEM_HOST_DEVICE (int f) -> void
   {
      constexpr int max_D1D = T_D1D ? T_D1D : MAX_D1D;
      constexpr int max_Q1D = T_Q1D ? T_Q1D : MAX_Q1D;

      MFEM_SHARED double u0[max_D1D];
      MFEM_SHARED double u1[max_D1D];
      MFEM_SHARED double du0[max_D1D];
      MFEM_SHARED double du1[max_D1D];

      MFEM_SHARED double Bu0[max_Q1D];
      MFEM_SHARED double Bu1[max_Q1D];
      MFEM_SHARED double Bdu0[max_Q1D];
      MFEM_SHARED double Bdu1[max_Q1D];

      MFEM_SHARED double r[max_Q1D];

      MFEM_SHARED double BG[2*max_D1D*max_Q1D];
      DeviceMatrix B(BG, Q1D, D1D);
      DeviceMatrix G(BG + D1D*Q1D, Q1D, D1D);

      if (MFEM_THREAD_ID(y) == 0)
      {
         MFEM_FOREACH_THREAD(p,x,Q1D)
         {
            for (int d = 0; d < D1D; ++d)
            {
               B(p,d) = B_(p,d);
               G(p,d) = G_(p,d);
            }
         }
      }
      MFEM_SYNC_THREAD;

      // copy edge values to u0, u1 and copy edge normals to du0, du1
      MFEM_FOREACH_THREAD(side,y,2)
      {
         double *u = (side == 0) ? u0 : u1;
         double *du = (side == 0) ? du0 : du1;
         MFEM_FOREACH_THREAD(d,x,D1D)
         {
            u[d] = x(d, side, f);
            du[d] = dxdn(d, side, f);
         }
      }
      MFEM_SYNC_THREAD;

      // eval @ quad points
      MFEM_FOREACH_THREAD(side,y,2)
      {
         double *u = (side == 0) ? u0 : u1;
         double *du = (side == 0) ? du0 : du1;
         double *Bu = (side == 0) ? Bu0 : Bu1;
         double *Bdu = (side == 0) ? Bdu0 : Bdu1;

         MFEM_FOREACH_THREAD(p,x,Q1D)
         {
            const double Je_side[] = {pa(2 + 2*side, p, f), pa(2 + 2*side + 1, p, f)};

            Bu[p] = 0.0;
            Bdu[p] = 0.0;

            for (int d = 0; d < D1D; ++d)
            {
               const double b = B(p,d);
               const double g = G(p,d);

               Bu[p] += b*u[d];
               Bdu[p] += Je_side[0] * b * du[d] + Je_side[1] * g * u[d];
            }
         }
      }
      MFEM_SYNC_THREAD;

      // term - < {Q du/dn}, [v] > +  kappa * < {Q/h} [u], [v] >:
      if (MFEM_THREAD_ID(y) == 0)
      {
         MFEM_FOREACH_THREAD(p,x,Q1D)
         {
            const double q = pa(0, p, f);
            const double hi = pa(1, p, f);
            const double jump = Bu0[p] - Bu1[p];
            const double avg = Bdu0[p] + Bdu1[p]; // = {Q du/dn} * w * det(J)
            r[p] = -avg + hi * q * jump;
         }
      }
      MFEM_SYNC_THREAD;

      MFEM_FOREACH_THREAD(d,x,D1D)
      {
         double Br = 0.0;

         for (int p = 0; p < Q1D; ++p)
         {
            Br += B(p, d) * r[p];
         }

         u0[d] =  Br; // overwrite u0, u1
         u1[d] = -Br;
      } // for d
      MFEM_SYNC_THREAD;


      MFEM_FOREACH_THREAD(side,y,2)
      {
         double *du = (side == 0) ? du0 : du1;
         MFEM_FOREACH_THREAD(d,x,D1D)
         {
            du[d] = 0.0;
         }
      }
      MFEM_SYNC_THREAD;

      // term sigma * < [u], {Q dv/dn} >
      MFEM_FOREACH_THREAD(side,y,2)
      {
         double * const du = (side == 0) ? du0 : du1;
         double * const u = (side == 0) ? u0 : u1;

         MFEM_FOREACH_THREAD(d,x,D1D)
         {
            for (int p = 0; p < Q1D; ++p)
            {
               const double Je[] = {pa(2 + 2*side, p, f), pa(2 + 2*side + 1, p, f)};
               const double jump = Bu0[p] - Bu1[p];
               const double r_p = Je[0] * jump; // normal
               const double w_p = Je[1] * jump; // tangential
               du[d] += sigma * B(p, d) * r_p;
               u[d] += sigma * G(p, d) * w_p;
            }
         }
      }
      MFEM_SYNC_THREAD;

      MFEM_FOREACH_THREAD(side,y,2)
      {
         double *u = (side == 0) ? u0 : u1;
         double *du = (side == 0) ? du0 : du1;
         MFEM_FOREACH_THREAD(d,x,D1D)
         {
            y(d, side, f) += u[d];
            dydn(d, side, f) += du[d];
         }
      }
   }); // mfem::forall
}

template <int T_D1D = 0, int T_Q1D = 0>
static void PADGDiffusionApply3D(const int NF,
                                 const Array<double>& b,
                                 const Array<double>& bt,
                                 const Array<double>& g,
                                 const Array<double>& gt,
                                 const double sigma,
                                 const double kappa,
                                 const Vector& pa_data,
                                 const Vector& x_,
                                 const Vector& dxdn_,
                                 Vector& y_,
                                 Vector& dydn_,
                                 const int d1d = 0,
                                 const int q1d = 0)
{
   const int D1D = T_D1D ? T_D1D : d1d;
   const int Q1D = T_Q1D ? T_Q1D : q1d;
   MFEM_VERIFY(D1D <= MAX_D1D, "");
   MFEM_VERIFY(Q1D <= MAX_Q1D, "");

   auto B_ = Reshape(b.Read(), Q1D, D1D);
   auto G_ = Reshape(g.Read(), Q1D, D1D);

   // (q, 1/h, J0[0], J0[1], J0[2], J1[0], J1[1], J1[2])
   auto pa = Reshape(pa_data.Read(), 8, Q1D, Q1D, NF);

   auto x =    Reshape(x_.Read(),         D1D, D1D, 2, NF);
   auto y =    Reshape(y_.ReadWrite(),    D1D, D1D, 2, NF);
   auto dxdn = Reshape(dxdn_.Read(),      D1D, D1D, 2, NF);
   auto dydn = Reshape(dydn_.ReadWrite(), D1D, D1D, 2, NF);

   const int NBX = std::max(D1D, Q1D);

   mfem::forall_3D(NF, NBX, NBX, 2, [=] MFEM_HOST_DEVICE (int f) -> void
   {
      constexpr int max_D1D = T_D1D ? T_D1D : MAX_D1D;
      constexpr int max_Q1D = T_Q1D ? T_Q1D : MAX_Q1D;

      MFEM_SHARED double u0[max_Q1D][max_Q1D];
      MFEM_SHARED double u1[max_Q1D][max_Q1D];

      MFEM_SHARED double du0[max_Q1D][max_Q1D];
      MFEM_SHARED double du1[max_Q1D][max_Q1D];

      MFEM_SHARED double Gu0[max_Q1D][max_D1D];
      MFEM_SHARED double Gu1[max_Q1D][max_D1D];

      MFEM_SHARED double Bu0[max_Q1D][max_Q1D];
      MFEM_SHARED double Bu1[max_Q1D][max_Q1D];

      MFEM_SHARED double Bdu0[max_Q1D][max_D1D];
      MFEM_SHARED double Bdu1[max_Q1D][max_D1D];

      MFEM_SHARED double Jump[max_Q1D][max_Q1D];

      double (*BBu0)[max_Q1D] = u0;
      double (*BBu1)[max_Q1D] = u1;
      double (*r)[max_Q1D] = Bu0;
      double (*Br)[max_Q1D] = Bu1;
      double (*Bj0)[max_D1D] = Bdu0;
      double (*Bj1)[max_D1D] = Bdu1;
      double (*Gj0)[max_D1D] = Gu0;
      double (*Gj1)[max_D1D] = Gu1;

      MFEM_SHARED double BG[2*max_D1D*max_Q1D];
      DeviceMatrix B(BG, Q1D, D1D);
      DeviceMatrix G(BG + D1D*Q1D, Q1D, D1D);

      if (MFEM_THREAD_ID(z) == 0)
      {
         MFEM_FOREACH_THREAD(p, x, Q1D)
         {
            MFEM_FOREACH_THREAD(d, y, D1D)
            {
               B(p, d) = B_(p, d);
               G(p, d) = G_(p, d);
            }
         }
      }
      MFEM_SYNC_THREAD;

      // copy face values to u0, u1 and copy normals to du0, du1
      MFEM_FOREACH_THREAD(side, z, 2)
      {
         double (*u)[max_Q1D] = (side == 0) ? u0 : u1;
         double (*du)[max_Q1D] = (side == 0) ? du0 : du1;

         MFEM_FOREACH_THREAD(d1, x, D1D)
         {
            MFEM_FOREACH_THREAD(d2, y, D1D)
            {
               u[d1][d2] = x(d1, d2, side, f);
               du[d1][d2] = dxdn(d1, d2, side, f);
            }
         }
      }
      MFEM_SYNC_THREAD;

      // eval u and normal derivative @ quad points
      MFEM_FOREACH_THREAD(side, z, 2)
      {
         double (*u)[max_Q1D] = (side == 0) ? u0 : u1;
         double (*du)[max_Q1D] = (side == 0) ? du0 : du1;
         double (*Bu)[max_Q1D] = (side == 0) ? Bu0 : Bu1;
         double (*Bdu)[max_D1D] = (side == 0) ? Bdu0 : Bdu1;
         double (*Gu)[max_D1D] = (side == 0) ? Gu0 : Gu1;

         MFEM_FOREACH_THREAD(p1, x, Q1D)
         {
            MFEM_FOREACH_THREAD(d2, y, D1D)
            {
               double bu = 0.0;
               double bdu = 0.0;
               double gu = 0.0;

               for (int d1=0; d1 < D1D; ++d1)
               {
                  const double b = B(p1, d1);
                  const double g = G(p1, d1);
                  bu += b * u[d1][d2];
                  bdu += b * du[d1][d2];
                  gu += g * u[d1][d2];
               }

               Bu[p1][d2] = bu;
               Bdu[p1][d2] = bdu;
               Gu[p1][d2] = gu;
            }
         }
      }
      MFEM_SYNC_THREAD;

      MFEM_FOREACH_THREAD(side, z, 2)
      {
         double (*Bu)[max_Q1D] = (side == 0) ? Bu0 : Bu1;
         double (*BBu)[max_Q1D] = (side == 0) ? BBu0 : BBu1;
         double (*Gu)[max_D1D] = (side == 0) ? Gu0 : Gu1;
         double (*Bdu)[max_D1D] = (side == 0) ? Bdu0 : Bdu1;
         double (*du)[max_Q1D] = (side == 0) ? du0 : du1;

         MFEM_FOREACH_THREAD(p1, x, Q1D)
         {
            MFEM_FOREACH_THREAD(p2, y, Q1D)
            {
               const double Je[] = {pa(2+3*side + 0, p1, p2, f), pa(2+3*side + 1, p1, p2, f), pa(2+3*side + 2, p1, p2, f)};

               double bbu = 0.0;
               double bgu = 0.0;
               double gbu = 0.0;
               double bbdu = 0.0;
               for (int d2 = 0; d2 < D1D; ++d2)
               {
                  const double b = B(p2, d2);
                  const double g = G(p2, d2);
                  bbu += b * Bu[p1][d2];
                  gbu += g * Bu[p1][d2];
                  bgu += b * Gu[p1][d2];
                  bbdu += b * Bdu[p1][d2];
               }

               BBu[p1][p2] = bbu;
               // du <- Q du/dn * w * det(J)
               du[p1][p2] = Je[0] * bbdu + Je[1] * bgu + Je[2] * gbu;
            }
         }
      }
      MFEM_SYNC_THREAD;

      // term: - < {Q du/dn}, [v] > + kappa * < {Q/h} [u], [v] >
      if (MFEM_THREAD_ID(z) == 0)
      {
         MFEM_FOREACH_THREAD(p1, x, Q1D)
         {
            MFEM_FOREACH_THREAD(p2, y, Q1D)
            {
               const double q = pa(0, p1, p2, f);
               const double hi = pa(1, p1, p2, f);
               const double jump = BBu0[p1][p2] - BBu1[p1][p2];
               const double avg = du0[p1][p2] + du1[p1][p2]; // {Q du/dn} * w * det(J)
               r[p1][p2] = -avg + hi * q * jump;
               Jump[p1][p2] = jump;
            }
         }
      }
      MFEM_SYNC_THREAD;

      // u0, u1 <- B' * r
      if (MFEM_THREAD_ID(z) == 0)
      {
         MFEM_FOREACH_THREAD(d1, x, D1D)
         {
            MFEM_FOREACH_THREAD(p2, y, Q1D)
            {
               double br = 0.0;
               for (int p1 = 0; p1 < Q1D; ++p1)
               {
                  const double b = B(p1, d1);
                  br += b * r[p1][p2];
               }
               Br[p2][d1] = br;
            }
         }
      }
      MFEM_SYNC_THREAD;

      if (MFEM_THREAD_ID(z) == 0)
      {
         MFEM_FOREACH_THREAD(d1, x, D1D)
         {
            MFEM_FOREACH_THREAD(d2, y, D1D)
            {
               double bbr = 0.0;
               for (int p2 = 0; p2 < Q1D; ++p2)
               {
                  const double b = B(p2, d2);
                  bbr += b * Br[p2][d1];
               }

               u0[d1][d2] =  bbr; // reuse u0, u1
               u1[d1][d2] = -bbr;
            }
         }
      }

      // term: sigma * < [u], {Q dv/dn} >
      MFEM_FOREACH_THREAD(side, z, 2)
      {
         double (*du)[max_Q1D] = (side == 0) ? du0 : du1;

         MFEM_FOREACH_THREAD(d1, x, D1D)
         {
            MFEM_FOREACH_THREAD(d2, y, D1D)
            {
               du[d1][d2] = 0.0;
            }
         }
      }

      MFEM_FOREACH_THREAD(side, z, 2)
      {
         double (*Bj)[max_D1D] = (side == 0) ? Bj0 : Bj1;
         double (*Gj)[max_D1D] = (side == 0) ? Gj0 : Gj1;

         MFEM_FOREACH_THREAD(d1, x, D1D)
         {
            MFEM_FOREACH_THREAD(p2, y, Q1D)
            {
               double bj = 0.0;
               double gj = 0.0;
               for (int p1 = 0; p1 < Q1D; ++p1)
               {
                  const double b = B(p1, d1);
                  const double Je0 = pa(2+3*side + 0, p1, p2, f);
                  bj += b * Je0 * Jump[p1][p2];

                  const double Je1 = pa(2+3*side + 1, p1, p2, f);
                  const double g = G(p1, d1);
                  gj += g * Je1 * Jump[p1][p2];
               }

               Bj[p2][d1] = bj;
               Gj[p2][d1] = gj;
            }
         }
      }
      MFEM_SYNC_THREAD;

      MFEM_FOREACH_THREAD(side, z, 2)
      {
         double (*u)[max_Q1D] = (side == 0) ? u0 : u1;
         double (*du)[max_Q1D] = (side == 0) ? du0 : du1;
         double (*Bj)[max_D1D] = (side == 0) ? Bj0 : Bj1;
         double (*Gj)[max_D1D] = (side == 0) ? Gj0 : Gj1;

         MFEM_FOREACH_THREAD(d1, x, D1D)
         {
            MFEM_FOREACH_THREAD(d2, y, D1D)
            {
               double bbj = 0.0;
               double bgj = 0.0;
               for (int p2 = 0; p2 < Q1D; ++p2)
               {
                  const double b = B(p2, d2);
                  bbj += b * Bj[p2][d1];
                  bgj += b * Gj[p2][d1];
               }

               du[d1][d2] += sigma * bbj;
               u[d1][d2] += sigma * bgj;
            }
         }
      }

      MFEM_FOREACH_THREAD(side, z, 2)
      {
         double (*Bj)[max_D1D] = (side == 0) ? Bj0 : Bj1;

         MFEM_FOREACH_THREAD(d1, x, D1D)
         {
            MFEM_FOREACH_THREAD(p2, y, Q1D)
            {
               double bj = 0.0;
               for (int p1 = 0; p1 < Q1D; ++p1)
               {
                  const double b = B(p1, d1);
                  const double Je2 = pa(2+3*side + 2, p1, p2, f);
                  bj += b * Je2 * Jump[p1][p2];
               }

               Bj[p2][d1] = bj;
            }
         }
      }
      MFEM_SYNC_THREAD;

      MFEM_FOREACH_THREAD(side, z, 2)
      {
         double (*u)[max_Q1D] = (side == 0) ? u0 : u1;
         double (*Bj)[max_D1D] = (side == 0) ? Bj0 : Bj1;

         MFEM_FOREACH_THREAD(d1, x, D1D)
         {
            MFEM_FOREACH_THREAD(d2, y, D1D)
            {
               double gbj = 0.0;
               for (int p2 = 0; p2 < Q1D; ++p2)
               {
                  const double g = G(p2, d2);
                  gbj += g * Bj[p2][d1];
               }

               u[d1][d2] += sigma * gbj;
            }
         }
      }
      MFEM_SYNC_THREAD;

      // map back to y and dydn
      MFEM_FOREACH_THREAD(side, z, 2)
      {
         const double (*u)[max_Q1D] = (side == 0) ? u0 : u1;
         const double (*du)[max_Q1D] = (side == 0) ? du0 : du1;

         MFEM_FOREACH_THREAD(d1, x, D1D)
         {
            MFEM_FOREACH_THREAD(d2, y, D1D)
            {
               y(d1, d2, side, f) += u[d1][d2];
               dydn(d1, d2, side, f) += du[d1][d2];
            }
         }
      }
   });
}

static void PADGDiffusionApply(const int dim,
                               const int D1D,
                               const int Q1D,
                               const int NF,
                               const Array<double> &B,
                               const Array<double> &Bt,
                               const Array<double> &G,
                               const Array<double> &Gt,
                               const double sigma,
                               const double kappa,
                               const Vector &pa_data,
                               const Vector &x,
                               const Vector &dxdn,
                               Vector &y,
                               Vector &dydn)
{
   if (dim == 2)
   {
      auto kernel = PADGDiffusionApply2D<0,0>;
      switch ((D1D << 4 ) | Q1D)
      {
         case 0x23: kernel = PADGDiffusionApply2D<2,3>; break;
         case 0x34: kernel = PADGDiffusionApply2D<3,4>; break;
         case 0x45: kernel = PADGDiffusionApply2D<4,5>; break;
         case 0x56: kernel = PADGDiffusionApply2D<5,6>; break;
         case 0x67: kernel = PADGDiffusionApply2D<6,7>; break;
         case 0x78: kernel = PADGDiffusionApply2D<7,8>; break;
         case 0x89: kernel = PADGDiffusionApply2D<8,9>; break;
         case 0x9A: kernel = PADGDiffusionApply2D<9,10>; break;
      }
      kernel(NF, B, Bt, G, Gt, sigma, kappa, pa_data, x, dxdn, y, dydn, D1D, Q1D);
   }
   else if (dim == 3)
   {
      auto kernel = PADGDiffusionApply3D<0,0>;
      switch ((D1D << 4) | Q1D)
      {
         case 0x24: kernel = PADGDiffusionApply3D<2,4>; break;
         case 0x35: kernel = PADGDiffusionApply3D<3,5>; break;
         case 0x46: kernel = PADGDiffusionApply3D<4,6>; break;
         case 0x57: kernel = PADGDiffusionApply3D<5,7>; break;
         case 0x68: kernel = PADGDiffusionApply3D<6,8>; break;
         case 0x79: kernel = PADGDiffusionApply3D<7,9>; break;
         case 0x8A: kernel = PADGDiffusionApply3D<8,10>; break;
         case 0x9B: kernel = PADGDiffusionApply3D<9,11>; break;
      }
      kernel(NF, B, Bt, G, Gt, sigma, kappa, pa_data, x, dxdn, y, dydn, D1D, Q1D);
   }
   else
   {
      MFEM_ABORT("Unsupported dimension");
   }
}

void DGDiffusionIntegrator::AddMultPAFaceNormalDerivatives(const Vector &x,
                                                           const Vector &dxdn, Vector &y, Vector &dydn) const
{
   PADGDiffusionApply(dim, dofs1D, quad1D, nf,
                      maps->B, maps->Bt, maps->G, maps->Gt,
                      sigma, kappa, pa_data, x, dxdn, y, dydn);
}

const IntegrationRule &DGDiffusionIntegrator::GetRule(
   int order, FaceElementTransformations &T)
{
   int int_order = T.Elem1->OrderW() + 2*order;
   return irs.Get(T.GetGeometryType(), int_order);
}

} // namespace mfem
