// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

// Implementation of class BilinearForm

#include "fem.hpp"
#include <cmath>

namespace mfem
{

void BilinearForm::AllocMat()
{
   if (precompute_sparsity == 0 || fes->GetVDim() > 1)
   {
      mat = new SparseMatrix(height);
      return;
   }

   fes->BuildElementToDofTable();
   const Table &elem_dof = fes->GetElementToDofTable();
   Table dof_dof;

   if (fbfi.Size() > 0)
   {
      // the sparsity pattern is defined from the map: face->element->dof
      Table face_dof, dof_face;
      {
         Table *face_elem = fes->GetMesh()->GetFaceToElementTable();
         mfem::Mult(*face_elem, elem_dof, face_dof);
         delete face_elem;
      }
      Transpose(face_dof, dof_face, height);
      mfem::Mult(dof_face, face_dof, dof_dof);
   }
   else
   {
      // the sparsity pattern is defined from the map: element->dof
      Table dof_elem;
      Transpose(elem_dof, dof_elem, height);
      mfem::Mult(dof_elem, elem_dof, dof_dof);
   }

   int *I = dof_dof.GetI();
   int *J = dof_dof.GetJ();
   double *data = new double[I[height]];

   mat = new SparseMatrix(I, J, data, height, height);
   *mat = 0.0;

   dof_dof.LoseData();
}

BilinearForm::BilinearForm (FiniteElementSpace * f)
   : Matrix (f->GetVSize())
{
   fes = f;
   mat = mat_e = NULL;
   extern_bfs = 0;
   element_matrices = NULL;
   precompute_sparsity = 0;
}

BilinearForm::BilinearForm (FiniteElementSpace * f, BilinearForm * bf, int ps)
   : Matrix (f->GetVSize())
{
   int i;
   Array<BilinearFormIntegrator*> *bfi;

   fes = f;
   mat_e = NULL;
   extern_bfs = 1;
   element_matrices = NULL;
   precompute_sparsity = ps;

   bfi = bf->GetDBFI();
   dbfi.SetSize (bfi->Size());
   for (i = 0; i < bfi->Size(); i++)
   {
      dbfi[i] = (*bfi)[i];
   }

   bfi = bf->GetBBFI();
   bbfi.SetSize (bfi->Size());
   for (i = 0; i < bfi->Size(); i++)
   {
      bbfi[i] = (*bfi)[i];
   }

   bfi = bf->GetFBFI();
   fbfi.SetSize (bfi->Size());
   for (i = 0; i < bfi->Size(); i++)
   {
      fbfi[i] = (*bfi)[i];
   }

   bfi = bf->GetBFBFI();
   bfbfi.SetSize (bfi->Size());
   for (i = 0; i < bfi->Size(); i++)
   {
      bfbfi[i] = (*bfi)[i];
   }

   AllocMat();
}

double& BilinearForm::Elem (int i, int j)
{
   return mat -> Elem(i,j);
}

const double& BilinearForm::Elem (int i, int j) const
{
   return mat -> Elem(i,j);
}

void BilinearForm::Mult (const Vector & x, Vector & y) const
{
   mat -> Mult (x, y);
}

MatrixInverse * BilinearForm::Inverse() const
{
   return mat -> Inverse();
}

void BilinearForm::Finalize (int skip_zeros)
{
   mat -> Finalize (skip_zeros);
   if (mat_e)
   {
      mat_e -> Finalize (skip_zeros);
   }
}

void BilinearForm::AddDomainIntegrator (BilinearFormIntegrator * bfi)
{
   dbfi.Append (bfi);
}

void BilinearForm::AddBoundaryIntegrator (BilinearFormIntegrator * bfi)
{
   bbfi.Append (bfi);
}

void BilinearForm::AddInteriorFaceIntegrator (BilinearFormIntegrator * bfi)
{
   fbfi.Append (bfi);
}

void BilinearForm::AddBdrFaceIntegrator (BilinearFormIntegrator * bfi)
{
   bfbfi.Append (bfi);
}

void BilinearForm::ComputeElementMatrix(int i, DenseMatrix &elmat)
{
   if (element_matrices)
   {
      elmat.SetSize(element_matrices->SizeI(), element_matrices->SizeJ());
      elmat = element_matrices->GetData(i);
      return;
   }

   if (dbfi.Size())
   {
      const FiniteElement &fe = *fes->GetFE(i);
      ElementTransformation *eltrans = fes->GetElementTransformation(i);
      dbfi[0]->AssembleElementMatrix(fe, *eltrans, elmat);
      for (int k = 1; k < dbfi.Size(); k++)
      {
         dbfi[k]->AssembleElementMatrix(fe, *eltrans, elemmat);
         elmat += elemmat;
      }
   }
   else
   {
      fes->GetElementVDofs(i, vdofs);
      elmat.SetSize(vdofs.Size());
      elmat = 0.0;
   }
}

void BilinearForm::AssembleElementMatrix(
   int i, const DenseMatrix &elmat, Array<int> &vdofs, int skip_zeros)
{
   if (mat == NULL)
   {
      AllocMat();
   }
   fes->GetElementVDofs(i, vdofs);
   mat->AddSubMatrix(vdofs, vdofs, elmat, skip_zeros);
}

void BilinearForm::Assemble (int skip_zeros)
{
   ElementTransformation *eltrans;
   Mesh *mesh = fes -> GetMesh();

   int i;

   if (mat == NULL)
   {
      AllocMat();
   }

#ifdef MFEM_USE_OPENMP
   int free_element_matrices = 0;
   if (!element_matrices)
   {
      ComputeElementMatrices();
      free_element_matrices = 1;
   }
#endif

   if (dbfi.Size())
   {
      for (i = 0; i < fes -> GetNE(); i++)
      {
         fes->GetElementVDofs(i, vdofs);
         if (element_matrices)
         {
            mat->AddSubMatrix(vdofs, vdofs, (*element_matrices)(i), skip_zeros);
         }
         else
         {
            const FiniteElement &fe = *fes->GetFE(i);
            eltrans = fes->GetElementTransformation(i);
            for (int k = 0; k < dbfi.Size(); k++)
            {
               dbfi[k]->AssembleElementMatrix(fe, *eltrans, elemmat);
               mat->AddSubMatrix(vdofs, vdofs, elemmat, skip_zeros);
            }
         }
      }
   }

   if (bbfi.Size())
   {
      for (i = 0; i < fes -> GetNBE(); i++)
      {
         const FiniteElement &be = *fes->GetBE(i);
         fes -> GetBdrElementVDofs (i, vdofs);
         eltrans = fes -> GetBdrElementTransformation (i);
         for (int k=0; k < bbfi.Size(); k++)
         {
            bbfi[k] -> AssembleElementMatrix(be, *eltrans, elemmat);
            mat -> AddSubMatrix (vdofs, vdofs, elemmat, skip_zeros);
         }
      }
   }

   if (fbfi.Size())
   {
      FaceElementTransformations *tr;
      Array<int> vdofs2;

      int nfaces = mesh->GetNumFaces();
      for (i = 0; i < nfaces; i++)
      {
         tr = mesh -> GetInteriorFaceTransformations (i);
         if (tr != NULL)
         {
            fes -> GetElementVDofs (tr -> Elem1No, vdofs);
            fes -> GetElementVDofs (tr -> Elem2No, vdofs2);
            vdofs.Append (vdofs2);
            for (int k = 0; k < fbfi.Size(); k++)
            {
               fbfi[k] -> AssembleFaceMatrix (*fes -> GetFE (tr -> Elem1No),
                                              *fes -> GetFE (tr -> Elem2No),
                                              *tr, elemmat);
               mat -> AddSubMatrix (vdofs, vdofs, elemmat, skip_zeros);
            }
         }
      }
   }

   if (bfbfi.Size())
   {
      FaceElementTransformations *tr;
      const FiniteElement *fe1, *fe2;

      for (i = 0; i < fes -> GetNBE(); i++)
      {
         tr = mesh -> GetBdrFaceTransformations (i);
         if (tr != NULL)
         {
            fes -> GetElementVDofs (tr -> Elem1No, vdofs);
            fe1 = fes -> GetFE (tr -> Elem1No);
            // The fe2 object is really a dummy and not used on the boundaries,
            // but we can't dereference a NULL pointer, and we don't want to
            // actually make a fake element.
            fe2 = fe1;
            for (int k = 0; k < bfbfi.Size(); k++)
            {
               bfbfi[k] -> AssembleFaceMatrix (*fe1, *fe2, *tr, elemmat);
               mat -> AddSubMatrix (vdofs, vdofs, elemmat, skip_zeros);
            }
         }
      }
   }

#ifdef MFEM_USE_OPENMP
   if (free_element_matrices)
   {
      FreeElementMatrices();
   }
#endif
}

void BilinearForm::ConformingAssemble()
{
   // Do not remove zero entries to preserve the symmetric structure of the
   // matrix which in turn will give rise to symmetric structure in the new
   // matrix. This ensures that subsequent calls to EliminateRowCol will work
   // correctly.
   Finalize(0);

   SparseMatrix *P = fes->GetConformingProlongation();
   if (!P) { return; } // assume conforming mesh

   SparseMatrix *R = Transpose(*P);
   SparseMatrix *RA = mfem::Mult(*R, *mat);
   delete mat;
   if (mat_e)
   {
      SparseMatrix *RAe = mfem::Mult(*R, *mat_e);
      delete mat_e;
      mat_e = RAe;
   }
   delete R;
   mat = mfem::Mult(*RA, *P);
   delete RA;
   if (mat_e)
   {
      SparseMatrix *RAeP = mfem::Mult(*mat_e, *P);
      delete mat_e;
      mat_e = RAeP;
   }

   height = mat->Height();
   width = mat->Width();
}

void BilinearForm::ComputeElementMatrices()
{
   if (element_matrices || dbfi.Size() == 0 || fes->GetNE() == 0)
   {
      return;
   }

   int num_elements = fes->GetNE();
   int num_dofs_per_el = fes->GetFE(0)->GetDof() * fes->GetVDim();

   element_matrices = new DenseTensor(num_dofs_per_el, num_dofs_per_el,
                                      num_elements);

   DenseMatrix tmp;
   IsoparametricTransformation eltrans;

#ifdef MFEM_USE_OPENMP
   #pragma omp parallel for private(tmp,eltrans)
#endif
   for (int i = 0; i < num_elements; i++)
   {
      DenseMatrix elmat(element_matrices->GetData(i),
                        num_dofs_per_el, num_dofs_per_el);
      const FiniteElement &fe = *fes->GetFE(i);
#ifdef MFEM_DEBUG
      if (num_dofs_per_el != fe.GetDof()*fes->GetVDim())
         mfem_error("BilinearForm::ComputeElementMatrices:"
                    " all elements must have same number of dofs");
#endif
      fes->GetElementTransformation(i, &eltrans);

      dbfi[0]->AssembleElementMatrix(fe, eltrans, elmat);
      for (int k = 1; k < dbfi.Size(); k++)
      {
         // note: some integrators may not be thread-safe
         dbfi[k]->AssembleElementMatrix(fe, eltrans, tmp);
         elmat += tmp;
      }
      elmat.ClearExternalData();
   }
}

void BilinearForm::EliminateEssentialBC (
   Array<int> &bdr_attr_is_ess, Vector &sol, Vector &rhs, int d )
{
   Array<int> ess_dofs, conf_ess_dofs;
   fes->GetEssentialVDofs(bdr_attr_is_ess, ess_dofs);
   if (fes->GetConformingProlongation() == NULL)
   {
      EliminateEssentialBCFromDofs(ess_dofs, sol, rhs, d);
   }
   else
   {
      fes->ConvertToConformingVDofs(ess_dofs, conf_ess_dofs);
      EliminateEssentialBCFromDofs(conf_ess_dofs, sol, rhs, d);
   }
}

void BilinearForm::EliminateVDofs (
   Array<int> &vdofs, Vector &sol, Vector &rhs, int d)
{
   for (int i = 0; i < vdofs.Size(); i++)
   {
      int vdof = vdofs[i];
      if ( vdof >= 0 )
      {
         mat -> EliminateRowCol (vdof, sol(vdof), rhs, d);
      }
      else
      {
         mat -> EliminateRowCol (-1-vdof, sol(-1-vdof), rhs, d);
      }
   }
}

void BilinearForm::EliminateVDofs(Array<int> &vdofs, int d)
{
   if (mat_e == NULL)
   {
      mat_e = new SparseMatrix(height);
   }

   for (int i = 0; i < vdofs.Size(); i++)
   {
      int vdof = vdofs[i];
      if ( vdof >= 0 )
      {
         mat -> EliminateRowCol (vdof, *mat_e, d);
      }
      else
      {
         mat -> EliminateRowCol (-1-vdof, *mat_e, d);
      }
   }
}

void BilinearForm::EliminateVDofsInRHS(
   Array<int> &vdofs, const Vector &x, Vector &b)
{
   mat_e->AddMult(x, b, -1.);
   mat->PartMult(vdofs, x, b);
}

void BilinearForm::EliminateEssentialBC (Array<int> &bdr_attr_is_ess, int d)
{
   Array<int> ess_dofs, conf_ess_dofs;
   fes->GetEssentialVDofs(bdr_attr_is_ess, ess_dofs);
   if (fes->GetConformingProlongation() == NULL)
   {
      EliminateEssentialBCFromDofs(ess_dofs, d);
   }
   else
   {
      fes->ConvertToConformingVDofs(ess_dofs, conf_ess_dofs);
      EliminateEssentialBCFromDofs(conf_ess_dofs, d);
   }
}

void BilinearForm::EliminateEssentialBCFromDofs (
   Array<int> &ess_dofs, Vector &sol, Vector &rhs, int d )
{
   MFEM_ASSERT(ess_dofs.Size() == height, "incorrect dof Array size");
   MFEM_ASSERT(sol.Size() == height, "incorrect sol Vector size");
   MFEM_ASSERT(rhs.Size() == height, "incorrect rhs Vector size");

   for (int i = 0; i < ess_dofs.Size(); i++)
      if (ess_dofs[i] < 0)
      {
         mat -> EliminateRowCol (i, sol(i), rhs, d);
      }
}

void BilinearForm::EliminateEssentialBCFromDofs (Array<int> &ess_dofs, int d)
{
   MFEM_ASSERT(ess_dofs.Size() == height, "incorrect dof Array size");

   for (int i = 0; i < ess_dofs.Size(); i++)
      if (ess_dofs[i] < 0)
      {
         mat -> EliminateRowCol (i, d);
      }
}

void BilinearForm::Update (FiniteElementSpace *nfes)
{
   if (nfes) { fes = nfes; }

   delete mat_e;
   delete mat;
   FreeElementMatrices();

   height = width = fes->GetVSize();

   mat = mat_e = NULL;
}

BilinearForm::~BilinearForm()
{
   delete mat_e;
   delete mat;
   delete element_matrices;

   if (!extern_bfs)
   {
      int k;
      for (k=0; k < dbfi.Size(); k++) { delete dbfi[k]; }
      for (k=0; k < bbfi.Size(); k++) { delete bbfi[k]; }
      for (k=0; k < fbfi.Size(); k++) { delete fbfi[k]; }
      for (k=0; k < bfbfi.Size(); k++) { delete bfbfi[k]; }
   }
}


MixedBilinearForm::MixedBilinearForm (FiniteElementSpace *tr_fes,
                                      FiniteElementSpace *te_fes)
   : Matrix(te_fes->GetVSize(), tr_fes->GetVSize())
{
   trial_fes = tr_fes;
   test_fes = te_fes;
   mat = NULL;
}

double & MixedBilinearForm::Elem (int i, int j)
{
   return (*mat)(i, j);
}

const double & MixedBilinearForm::Elem (int i, int j) const
{
   return (*mat)(i, j);
}

void MixedBilinearForm::Mult (const Vector & x, Vector & y) const
{
   mat -> Mult (x, y);
}

void MixedBilinearForm::AddMult (const Vector & x, Vector & y,
                                 const double a) const
{
   mat -> AddMult (x, y, a);
}

void MixedBilinearForm::AddMultTranspose (const Vector & x, Vector & y,
                                          const double a) const
{
   mat -> AddMultTranspose (x, y, a);
}

MatrixInverse * MixedBilinearForm::Inverse() const
{
   return mat -> Inverse ();
}

void MixedBilinearForm::Finalize (int skip_zeros)
{
   mat -> Finalize (skip_zeros);
}

void MixedBilinearForm::GetBlocks(Array2D<SparseMatrix *> &blocks) const
{
   if (trial_fes->GetOrdering() != Ordering::byNODES ||
       test_fes->GetOrdering() != Ordering::byNODES)
      mfem_error("MixedBilinearForm::GetBlocks :\n"
                 " Both trial and test spaces must use Ordering::byNODES!");

   blocks.SetSize(test_fes->GetVDim(), trial_fes->GetVDim());

   mat->GetBlocks(blocks);
}

void MixedBilinearForm::AddDomainIntegrator (BilinearFormIntegrator * bfi)
{
   dom.Append (bfi);
}

void MixedBilinearForm::AddBoundaryIntegrator (BilinearFormIntegrator * bfi)
{
   bdr.Append (bfi);
}

void MixedBilinearForm::AddTraceFaceIntegrator (BilinearFormIntegrator * bfi)
{
   skt.Append (bfi);
}

void MixedBilinearForm::Assemble (int skip_zeros)
{
   int i, k;
   Array<int> tr_vdofs, te_vdofs;
   ElementTransformation *eltrans;
   DenseMatrix elemmat;

   Mesh *mesh = test_fes -> GetMesh();

   if (mat == NULL)
   {
      mat = new SparseMatrix(height, width);
   }

   if (dom.Size())
   {
      for (i = 0; i < test_fes -> GetNE(); i++)
      {
         trial_fes -> GetElementVDofs (i, tr_vdofs);
         test_fes  -> GetElementVDofs (i, te_vdofs);
         eltrans = test_fes -> GetElementTransformation (i);
         for (k = 0; k < dom.Size(); k++)
         {
            dom[k] -> AssembleElementMatrix2 (*trial_fes -> GetFE(i),
                                              *test_fes  -> GetFE(i),
                                              *eltrans, elemmat);
            mat -> AddSubMatrix (te_vdofs, tr_vdofs, elemmat, skip_zeros);
         }
      }
   }

   if (bdr.Size())
   {
      for (i = 0; i < test_fes -> GetNBE(); i++)
      {
         trial_fes -> GetBdrElementVDofs (i, tr_vdofs);
         test_fes  -> GetBdrElementVDofs (i, te_vdofs);
         eltrans = test_fes -> GetBdrElementTransformation (i);
         for (k = 0; k < bdr.Size(); k++)
         {
            bdr[k] -> AssembleElementMatrix2 (*trial_fes -> GetBE(i),
                                              *test_fes  -> GetBE(i),
                                              *eltrans, elemmat);
            mat -> AddSubMatrix (te_vdofs, tr_vdofs, elemmat, skip_zeros);
         }
      }
   }

   if (skt.Size())
   {
      FaceElementTransformations *ftr;
      Array<int> te_vdofs2;
      const FiniteElement *trial_face_fe, *test_fe1, *test_fe2;

      int nfaces = mesh->GetNumFaces();
      for (i = 0; i < nfaces; i++)
      {
         ftr = mesh->GetFaceElementTransformations(i);
         trial_fes->GetFaceVDofs(i, tr_vdofs);
         test_fes->GetElementVDofs(ftr->Elem1No, te_vdofs);
         trial_face_fe = trial_fes->GetFaceElement(i);
         test_fe1 = test_fes->GetFE(ftr->Elem1No);
         if (ftr->Elem2No >= 0)
         {
            test_fes->GetElementVDofs(ftr->Elem2No, te_vdofs2);
            te_vdofs.Append(te_vdofs2);
            test_fe2 = test_fes->GetFE(ftr->Elem2No);
         }
         else
         {
            // The test_fe2 object is really a dummy and not used on the
            // boundaries, but we can't dereference a NULL pointer, and we don't
            // want to actually make a fake element.
            test_fe2 = test_fe1;
         }
         for (int k = 0; k < skt.Size(); k++)
         {
            skt[k]->AssembleFaceMatrix(*trial_face_fe, *test_fe1, *test_fe2,
                                       *ftr, elemmat);
            mat->AddSubMatrix(te_vdofs, tr_vdofs, elemmat, skip_zeros);
         }
      }
   }
}

void MixedBilinearForm::ConformingAssemble()
{
   Finalize();

   SparseMatrix *P2 = test_fes->GetConformingProlongation();
   if (P2)
   {
      SparseMatrix *R = Transpose(*P2);
      SparseMatrix *RA = mfem::Mult(*R, *mat);
      delete R;
      delete mat;
      mat = RA;
   }

   SparseMatrix *P1 = trial_fes->GetConformingProlongation();
   if (P1)
   {
      SparseMatrix *RAP = mfem::Mult(*mat, *P1);
      delete mat;
      mat = RAP;
   }

   height = mat->Height();
   width = mat->Width();
}

void MixedBilinearForm::EliminateTrialDofs (
   Array<int> &bdr_attr_is_ess, Vector &sol, Vector &rhs )
{
   int i, j, k;
   Array<int> tr_vdofs, cols_marker (trial_fes -> GetVSize());

   cols_marker = 0;
   for (i = 0; i < trial_fes -> GetNBE(); i++)
      if (bdr_attr_is_ess[trial_fes -> GetBdrAttribute (i)-1])
      {
         trial_fes -> GetBdrElementVDofs (i, tr_vdofs);
         for (j = 0; j < tr_vdofs.Size(); j++)
         {
            if ( (k = tr_vdofs[j]) < 0 )
            {
               k = -1-k;
            }
            cols_marker[k] = 1;
         }
      }
   mat -> EliminateCols (cols_marker, &sol, &rhs);
}

void MixedBilinearForm::EliminateEssentialBCFromTrialDofs (
   Array<int> &marked_vdofs, Vector &sol, Vector &rhs)
{
   mat -> EliminateCols (marked_vdofs, &sol, &rhs);
}

void MixedBilinearForm::EliminateTestDofs (Array<int> &bdr_attr_is_ess)
{
   int i, j, k;
   Array<int> te_vdofs;

   for (i = 0; i < test_fes -> GetNBE(); i++)
      if (bdr_attr_is_ess[test_fes -> GetBdrAttribute (i)-1])
      {
         test_fes -> GetBdrElementVDofs (i, te_vdofs);
         for (j = 0; j < te_vdofs.Size(); j++)
         {
            if ( (k = te_vdofs[j]) < 0 )
            {
               k = -1-k;
            }
            mat -> EliminateRow (k);
         }
      }
}

void MixedBilinearForm::Update()
{
   delete mat;
   mat = NULL;
   height = test_fes->GetVSize();
   width = trial_fes->GetVSize();
}

MixedBilinearForm::~MixedBilinearForm()
{
   int i;

   if (mat) { delete mat; }
   for (i = 0; i < dom.Size(); i++) { delete dom[i]; }
   for (i = 0; i < bdr.Size(); i++) { delete bdr[i]; }
   for (i = 0; i < skt.Size(); i++) { delete skt[i]; }
}


void DiscreteLinearOperator::Assemble(int skip_zeros)
{
   Array<int> dom_vdofs, ran_vdofs;
   ElementTransformation *T;
   const FiniteElement *dom_fe, *ran_fe;
   DenseMatrix totelmat, elmat;

   if (mat == NULL)
   {
      mat = new SparseMatrix(height, width);
   }

   if (dom.Size() > 0)
      for (int i = 0; i < test_fes->GetNE(); i++)
      {
         trial_fes->GetElementVDofs(i, dom_vdofs);
         test_fes->GetElementVDofs(i, ran_vdofs);
         T = test_fes->GetElementTransformation(i);
         dom_fe = trial_fes->GetFE(i);
         ran_fe = test_fes->GetFE(i);

         dom[0]->AssembleElementMatrix2(*dom_fe, *ran_fe, *T, totelmat);
         for (int j = 1; j < dom.Size(); j++)
         {
            dom[j]->AssembleElementMatrix2(*dom_fe, *ran_fe, *T, elmat);
            totelmat += elmat;
         }
         mat->SetSubMatrix(ran_vdofs, dom_vdofs, totelmat, skip_zeros);
      }
}

}
