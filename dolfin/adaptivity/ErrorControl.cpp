// Copyright (C) 2010 Marie E. Rognes.
// Licensed under the GNU LGPL Version 3.0 or any later version.
//
// Modified by Anders Logg, 2011.
//
// First added:  2010-09-16
// Last changed: 2011-01-26

#include <armadillo>

#include <dolfin/common/Timer.h>
#include <dolfin/fem/assemble.h>
#include <dolfin/fem/BoundaryCondition.h>
#include <dolfin/fem/DirichletBC.h>
#include <dolfin/fem/DofMap.h>
#include <dolfin/fem/Form.h>
#include <dolfin/fem/UFC.h>
#include <dolfin/fem/VariationalProblem.h>
#include <dolfin/function/Function.h>
#include <dolfin/function/FunctionSpace.h>
#include <dolfin/function/SubSpace.h>
#include <dolfin/la/Vector.h>
#include <dolfin/mesh/Cell.h>
#include <dolfin/mesh/Facet.h>

#include "LocalAssembler.h"
#include "SpecialFacetFunction.h"
#include "ErrorControl.h"

using namespace dolfin;

//-----------------------------------------------------------------------------
ErrorControl::ErrorControl(boost::shared_ptr<Form> a_star,
                           boost::shared_ptr<Form> L_star,
                           boost::shared_ptr<Form> residual,
                           boost::shared_ptr<Form> a_R_T,
                           boost::shared_ptr<Form> L_R_T,
                           boost::shared_ptr<Form> a_R_dT,
                           boost::shared_ptr<Form> L_R_dT,
                           boost::shared_ptr<Form> eta_T,
                           bool is_linear)
{
  // Assign input
  _a_star = a_star;
  _L_star = L_star;
  _residual = residual;
  _a_R_T = a_R_T;
  _L_R_T = L_R_T;
  _a_R_dT = a_R_dT;
  _L_R_dT = L_R_dT;
  _eta_T = eta_T;
  _is_linear = is_linear;

  // Extract and store additional function spaces
  const uint improved_dual = _residual->num_coefficients() - 1;
  const Function& e_tmp(dynamic_cast<const Function&>(_residual->coefficient(improved_dual)));
  _E = e_tmp.function_space_ptr();

  const Function& b_tmp(dynamic_cast<const Function&>(_a_R_dT->coefficient(0)));
  _C = b_tmp.function_space_ptr();
}
//-----------------------------------------------------------------------------
double ErrorControl::estimate_error(const Function& u,
                                    std::vector<const BoundaryCondition*> bcs)
{
  // Compute discrete dual approximation
  Function z_h(_a_star->function_space(1));
  compute_dual(z_h, bcs);

  // Compute extrapolation of discrete dual
  compute_extrapolation(z_h,  bcs);

  // Extract number of coefficients in residual
  const uint num_coeffs = _residual->num_coefficients();

  // Attach improved dual approximation to residual
  _residual->set_coefficient(num_coeffs - 1, _Ez_h);

  // Attach primal approximation if linear problem (already attached
  // otherwise).
  if (_is_linear)
    _residual->set_coefficient(num_coeffs - 2, u);

  // Assemble error estimate
  const double error_estimate = assemble(*_residual);

  // Return estimate
  return error_estimate;
}
//-----------------------------------------------------------------------------
void ErrorControl::compute_dual(Function& z,
                                std::vector<const BoundaryCondition*> bcs)
{
  // FIXME: Create dual (homogenized) boundary conditions
  std::vector<const BoundaryCondition*> dual_bcs;
  for (uint i = 0; i < bcs.size(); i++)
    dual_bcs.push_back(bcs[i]);   // push_back(bcs[i].homogenize());

  // Create and solve dual variational problem
  VariationalProblem dual(*_a_star, *_L_star, dual_bcs);
  dual.solve(z);
}
//-----------------------------------------------------------------------------
void ErrorControl::compute_extrapolation(const Function& z,
                                         std::vector<const BoundaryCondition*> bcs)
{
  // Extrapolate
  _Ez_h.reset(new Function(_E));
  _Ez_h->extrapolate(z);

  // Apply homogeneous boundary conditions to extrapolated dual
  for (uint i = 0; i < bcs.size(); i++)
  {
    // FIXME: Suboptimal cast.
    DirichletBC bc(*dynamic_cast<const DirichletBC*>(bcs[i]));

    // Extract SubSpace component
    const FunctionSpace& V(bc.function_space());
    const Array<uint>& component(V.component());

    // If bcs[i].function_space is non subspace:
    if (component.size() == 0)
    {
      // Define constant 0.0 on this space
      const Function u0(V);

      // Create corresponding boundary condition for extrapolation
      DirichletBC e_bc(V, u0, bc.markers());

      // Apply boundary condition to extrapolation
      e_bc.apply(_Ez_h->vector());
      continue;
    }

    // Create Subspace of _Ez_h
    SubSpace S(*_E, component[0]); // FIXME: Only one level allowed so far...

    // Define constant 0.0 on this space
    const Function u0(S);

    // Create corresponding boundary condition for extrapolation
    DirichletBC e_bc(S, u0, bc.markers());

    // Apply boundary condition to extrapolation
    //e_bc.apply(_Ez_h->vector()); // FIXME!! Awaits BUG #698229
  }
}
//-----------------------------------------------------------------------------
void ErrorControl::compute_indicators(Vector& indicators, const Function& u)
{
  // Create Function for the strong cell residual (R_T)
  Function R_T(_a_R_T->function_space(1));

  // Create SpecialFacetFunction for the strong facet residual (R_dT)
  std::vector<Function> f_e;
  for (uint i = 0; i <= R_T.geometric_dimension(); i++)
    f_e.push_back(Function(_a_R_dT->function_space(1)));

  SpecialFacetFunction* R_dT;
  if (f_e[0].value_rank() == 0)
    R_dT = new SpecialFacetFunction(f_e);
  else if (f_e[0].value_rank() == 1)
    R_dT = new SpecialFacetFunction(f_e, f_e[0].value_dimension(0));
  else
  {
    R_dT = new SpecialFacetFunction(f_e, f_e[0].value_dimension(0));
    error("Not implemented for tensor-valued functions");
  }

  // Compute residual representation
  residual_representation(R_T, *R_dT, u);

  // Interpolate dual extrapolation into primal test (dual trial space)
  Function Pi_E_z_h(_a_star->function_space(1));
  Pi_E_z_h.interpolate(*_Ez_h);

  // Attach coefficients to error indicator form
  _eta_T->set_coefficient(0, *_Ez_h);
  _eta_T->set_coefficient(1, R_T);
  _eta_T->set_coefficient(2, *R_dT);
  _eta_T->set_coefficient(3, Pi_E_z_h);

  // Assemble error indicator form
  assemble(indicators, *_eta_T);

  // Take absolute value of indicators
  indicators.abs();

  // Delete stuff
  //for (uint i = 0; i <= R_T.geometric_dimension(); i++)
  //  delete f_e[i];
  delete R_dT;
}
//-----------------------------------------------------------------------------
void ErrorControl::residual_representation(Function& R_T,
                                           SpecialFacetFunction& R_dT,
                                           const Function& u)
{
  begin("Computing residual representation");

  // Compute cell residual
  Timer timer("Computation of residual representation");
  compute_cell_residual(R_T, u);

  // Compute facet residual
  compute_facet_residual(R_dT, u, R_T);
  timer.stop();

  end();
}
//-----------------------------------------------------------------------------
void ErrorControl::compute_cell_residual(Function& R_T, const Function& u)
{
  begin("Computing cell residual representation");

  // FIXME:
  const MeshFunction<uint>* cell_domains = 0;
  const MeshFunction<uint>* exterior_facet_domains = 0;
  const MeshFunction<uint>* interior_facet_domains = 0;

  // Attach primal approximation to left-hand side form (residual) if
  // necessary
  if (_is_linear)
  {
    const uint num_coeffs = _L_R_T->num_coefficients();
    _L_R_T->set_coefficient(num_coeffs - 2, u);
  }

  // Create data structures for local assembly data
  UFC ufc_lhs(*_a_R_T);
  UFC ufc_rhs(*_L_R_T);

  // Extract common space, mesh and dofmap
  const FunctionSpace& V(R_T.function_space());
  const Mesh& mesh(V.mesh());
  const GenericDofMap& dof_map = V.dofmap();

  // Define matrices for cell-residual problems
  const uint N = V.element().space_dimension();
  arma::mat A(N, N);
  arma::mat b(N, 1);
  arma::vec x(N);

  // Assemble and solve local linear systems
  for (CellIterator cell(mesh); !cell.end(); ++cell)
  {
    // Assemble local linear system
    LocalAssembler::assemble(A, ufc_lhs, *cell, cell_domains,
                             exterior_facet_domains, interior_facet_domains);
    LocalAssembler::assemble(b, ufc_rhs, *cell, cell_domains,
                             exterior_facet_domains, interior_facet_domains);

    // Solve linear system and convert result
    x = arma::solve(A, b);

    // Get local-to-global dof map for cell
    const std::vector<uint>& dofs = dof_map.cell_dofs(cell->index());

    // Plug local solution into global vector
    R_T.vector().set(x.memptr(), N, &dofs[0]);
  }
  end();
}
//-----------------------------------------------------------------------------
void ErrorControl::compute_facet_residual(SpecialFacetFunction& R_dT,
                                          const Function& u,
                                          const Function& R_T)
{
  begin("Computing facet residual representation");

  // FIXME:
  const MeshFunction<uint>* cell_domains = 0;
  const MeshFunction<uint>* exterior_facet_domains = 0;
  const MeshFunction<uint>* interior_facet_domains = 0;

  // Extract function space for facet residual approximation
  const FunctionSpace& V = R_dT[0].function_space();
  const uint N = V.element().space_dimension();

  // Extract mesh
  const Mesh& mesh(V.mesh());
  //const int q = mesh.topology().dim();
  const int dim = mesh.topology().dim();

  // Extract dimension of cell cone space (DG_{dim})
  //const int n = _C->element().space_dimension();
  const int local_cone_dim = _C->element().space_dimension();

  // Extract number of coefficients on right-hand side (for use with
  // attaching coefficients)
  const uint L_R_dT_num_coefficients = _L_R_dT->num_coefficients();

  // Attach primal approximation if linear (already attached
  // otherwise).
  if (_is_linear)
    _L_R_dT->set_coefficient(L_R_dT_num_coefficients - 3, u);

  // Attach cell residual to residual form
  _L_R_dT->set_coefficient(L_R_dT_num_coefficients - 2, R_T);

  // Extract (common) dof map
  const GenericDofMap& dof_map = V.dofmap();

  // Define matrices for facet-residual problems
  arma::mat A(N, N);
  arma::mat b(N, 1);
  arma::vec x(N);

  // Variables to be used for the construction of the cone function
  // b_e
  const uint num_cells = mesh.num_cells();
  const std::vector<double> ones(num_cells, 1.0);
  std::vector<uint> facet_dofs(num_cells);

  // Compute the facet residual for each local facet number
  for (int local_facet = 0; local_facet < (dim + 1); local_facet++)
  {
    // Construct "cone function" for this local facet number by
    // setting the "right" degree of freedom equal to one on each
    // cell. (Requires dof-ordering knowledge.)
    Function b_e(_C);
    facet_dofs.clear();
    for (uint k = 0; k < num_cells; k++)
      facet_dofs.push_back(local_cone_dim*(k + 1) - (dim + 1) + local_facet);
    b_e.vector().set(&ones[0], num_cells, &facet_dofs[0]);

    // Attach b_e to _a_R_dT and _L_R_dT
    _a_R_dT->set_coefficient(0, b_e);
    _L_R_dT->set_coefficient(L_R_dT_num_coefficients - 1, b_e);

    // Create data structures for local assembly data
    UFC ufc_lhs(*_a_R_dT);
    UFC ufc_rhs(*_L_R_dT);

    // Assemble and solve local linear systems
    for (CellIterator cell(mesh); !cell.end(); ++cell)
    {
      // Assemble linear system
      LocalAssembler::assemble(A, ufc_lhs, *cell, cell_domains,
                               exterior_facet_domains, interior_facet_domains);
      LocalAssembler::assemble(b, ufc_rhs, *cell, cell_domains,
                               exterior_facet_domains, interior_facet_domains);

      // Non-singularize local matrix
      for (uint i = 0; i < N; ++i)
      {
        if (std::abs(A(i, i)) < 1.0e-10)
        {
          A(i, i) = 1.0;
          b(i) = 0.0;
        }
      }

      // Solve linear system and convert result
      x = arma::solve(A, b);

      // Get local-to-global dof map for cell
      const std::vector<uint>& dofs = dof_map.cell_dofs(cell->index());

      // Plug local solution into global vector
      R_dT[local_facet].vector().set(x.memptr(), N, &dofs[0]);
    }
  }
  end();
}
//-----------------------------------------------------------------------------