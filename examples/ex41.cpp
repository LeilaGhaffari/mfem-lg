#include "mfem.hpp"
using namespace mfem;

int main(int argc, char *argv[])
{
   int n = 3;
   Mesh orig_mesh = Mesh::MakeCartesian3D(n, n, n, mfem::Element::Type::HEXAHEDRON, 1.0, 1.0, 1.0, true);
   orig_mesh.Save("cube.mesh");

   std::vector<Vector> translations =
   {
      Vector({1.0, 0.0, 0.0}),
      Vector({0.0, 1.0, 0.0}),
      Vector({0.0, 0.0, 1.0})
   };
   Mesh periodic_mesh = Mesh::MakePeriodic(
                        orig_mesh,
                        orig_mesh.CreatePeriodicVertexMapping(translations));
   periodic_mesh.Save("cube_periodic.mesh");

   return 0;
}
