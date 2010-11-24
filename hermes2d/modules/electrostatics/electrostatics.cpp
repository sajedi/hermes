#include "electrostatics.h"

// FIXME: Because of callbacks that depend on global variables
// we need to define this here, but it is a temporary solution.
// Like this, one cannot have two instances of the Electrostatics
// module -- their global variables would interfere with each other.
std::vector<int> _global_mat_markers;
std::vector<double> _global_permittivity_array;
std::vector<double> _global_charge_density_array;
std::vector<int> _global_bdy_markers_val;
std::vector<double> _global_bc_val;
std::vector<int> _global_bdy_markers_der;
std::vector<double> _global_bc_der;

MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_AMESOS, SOLVER_MUMPS, 
                                                  // SOLVER_PARDISO, SOLVER_PETSC, SOLVER_UMFPACK.

// Weak forms.
template<typename Real, typename Scalar>
Scalar bilinear_form(int n, double *wt, Func<Real> *u_ext[], Func<Real> *u, Func<Real> *v, 
                     Geom<Real> *e, ExtData<Scalar> *ext)
{
  int marker = e->marker;
  return int_grad_u_grad_v<Real, Scalar>(n, wt, u, v);
}

template<typename Real, typename Scalar>
Scalar linear_form(int n, double *wt, Func<Real> *u_ext[], Func<Real> *v, 
                   Geom<Real> *e, ExtData<Scalar> *ext)
{
  int marker = e->marker;
  return int_v<Real, Scalar>(n, wt, v);
}

template<typename Real, typename Scalar>
Scalar linear_form_surf(int n, double *wt, Func<Real> *u_ext[], Func<Real> *v, 
                        Geom<Real> *e, ExtData<Scalar> *ext)
{
  int marker = e->marker;
  return int_v<Real, Scalar>(n, wt, v);
}

// Constructor.
Electrostatics::Electrostatics()
{
  init_ref_num = -1;
  init_p = -1;
  n_mat_markers = -1;
  n_bc_value = -1;
  n_bc_derivative = -1;
  mesh = new Mesh();
  space = NULL;
}

// Destructor.
Electrostatics::~Electrostatics()
{
  delete this->mesh;
  delete this->space;
}

// Set mesh via a string. See electrostatics.h for an example of such string.
void Electrostatics::set_mesh_str(const std::string &mesh)
{
    this->mesh_str = mesh;
}

// Set the number of initial uniform mesh refinements.
void Electrostatics::set_initial_mesh_refinement(int init_ref_num) 
{
  this->init_ref_num = init_ref_num;
}

// Set initial poly degree in elements.
void Electrostatics::set_initial_poly_degree(int p) 
{
  this->init_p = p;
}

// Set material markers, and check compatibility with mesh file.
void Electrostatics::set_material_markers(const std::vector<int> &mat_markers)
{
    this->mat_markers = mat_markers;
    _global_mat_markers = mat_markers;
}

// Set permittivity array.
void Electrostatics::set_permittivity_array(const std::vector<double> &p_array)
{
    this->permittivity_array = p_array;
    _global_permittivity_array = p_array;
}

// Set charge density array.
void Electrostatics::set_charge_density_array(const std::vector<double> &cd_array)
{
    this->charge_density_array = cd_array;
    _global_charge_density_array = cd_array;
}

// Set VALUE boundary markers (also check with the mesh file).
void Electrostatics::set_boundary_markers_value(const std::vector<int>
            &bdy_markers_val)
{
    this->bc_markers_value = bdy_markers_val;
    _global_bdy_markers_val = bdy_markers_val;
}

// Set boundary values.
void Electrostatics::set_boundary_values(const std::vector<double> &bc_val)
{
    this->bc_values = bc_val;
    _global_bc_val = bc_val;
}

// Set DERIVATIVE boundary markers (also check with the mesh file).
void Electrostatics::set_boundary_markers_derivative(const std::vector<int> &bdy_markers_der)
{
    this->bc_markers_derivative = bdy_markers_der;
    _global_bdy_markers_der = bdy_markers_der;
}

// Set boundary derivatives.
void Electrostatics::set_boundary_derivatives(const std::vector<double> &bc_der)
{
    this->bc_derivatives = bc_der;
    _global_bc_der = bc_der;
}

// Look up an integer number in an array.
bool index(const std::vector<int> &array, int x, int &i_out)
{
    for (int i=0; i < array.size(); i++)
        if (array[i] == x) {
            i_out = i;
            return true;
        }
    return false;
}

// Boundary condition types.
// Note: "essential" means that solution value is prescribed.
BCType bc_types(int marker)
{
    if (marker == 0)
        return BC_NONE;
    int idx;
    if (index(_global_bdy_markers_val, marker, idx))
        return BC_ESSENTIAL;
    if (index(_global_bdy_markers_der, marker, idx))
        return BC_NATURAL;
    error("Wrong boundary marker");
}

// Essential (Dirichlet) boundary condition values.
scalar essential_bc_values(int ess_bdy_marker, double x, double y)
{
    int idx;
    if (!index(_global_bdy_markers_val, ess_bdy_marker, idx))
        error("marker not found");
    return _global_bc_val[idx];
}

// Solve the problem.
bool Electrostatics::calculate(Solution* phi) 
{
  // Load the mesh.
  H2DReader mloader;
  mloader.load_str(this->mesh_str.c_str(), this->mesh);
  /*
  MeshView m("", 0, 0, 400, 400);
  m.show(this->mesh);
  View::wait();
  */

  // Perform initial uniform mesh refinements.
  for (int i = 0; i < this->init_ref_num; i++) this->mesh->refine_all_elements();

  // Create an H1 space with default shapeset.
  this->space = new H1Space(this->mesh, bc_types, essential_bc_values,
          this->init_p);
  int ndof = Space::get_num_dofs(this->space);
  info("ndof = %d", ndof);
  /*
  BaseView b("", new WinGeom(0, 0, 400, 400));
  b.show(this->space);
  View::wait();
  */


  // Initialize the weak formulation.
  WeakForm wf;
  wf.add_matrix_form(callback(bilinear_form));
  wf.add_vector_form(callback(linear_form));
  wf.add_vector_form_surf(callback(linear_form_surf));

  // Initialize the FE problem.
  bool is_linear = true;
  DiscreteProblem dp(&wf, this->space, is_linear);

  // Set up the solver, matrix, and rhs according to the solver selection.
  SparseMatrix* matrix = create_matrix(matrix_solver);
  Vector* rhs = create_vector(matrix_solver);
  Solver* solver = create_linear_solver(matrix_solver, matrix, rhs);

  // Assemble the stiffness matrix and right-hand side vector.
  info("Assembling the stiffness matrix and right-hand side vector.");
  dp.assemble(matrix, rhs);

  // Solve the linear system and if successful, obtain the solution.
  info("Solving the matrix problem.");
  if(solver->solve()) Solution::vector_to_solution(solver->get_solution(), this->space, phi);
  else error ("Matrix solver failed.\n");

  // Clean up.
  delete solver;
  delete matrix;
  delete rhs;

  return true;
}

