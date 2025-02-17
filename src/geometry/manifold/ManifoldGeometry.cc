// Portions of this file are Copyright 2023 Google LLC, and licensed under GPL2+. See COPYING.
#include "ManifoldGeometry.h"
#include "manifold.h"
#include "PolySet.h"
#include "PolySetBuilder.h"
#include "PolySetUtils.h"
#include "manifoldutils.h"
#ifdef ENABLE_CGAL
#include "cgal.h"
#include "cgalutils.h"
#endif

namespace {

template <typename Result, typename V>
Result vector_convert(V const& v) {
  return Result(v[0], v[1], v[2]);
}

}

ManifoldGeometry::ManifoldGeometry() : manifold_(std::make_shared<manifold::Manifold>()) {}

ManifoldGeometry::ManifoldGeometry(const std::shared_ptr<manifold::Manifold>& mani) : manifold_(mani) {
  assert(manifold_);
  if (!manifold_) clear();
}

ManifoldGeometry::ManifoldGeometry(const ManifoldGeometry& other) : manifold_(other.manifold_) {}

std::unique_ptr<Geometry> ManifoldGeometry::copy() const
{
  return std::make_unique<ManifoldGeometry>(*this);
}

ManifoldGeometry& ManifoldGeometry::operator=(const ManifoldGeometry& other) {
  manifold_ = other.manifold_;
  return *this;
}

const manifold::Manifold& ManifoldGeometry::getManifold() const {
  assert(manifold_);
  return *manifold_;
}

bool ManifoldGeometry::isEmpty() const {
  return getManifold().IsEmpty();
}

size_t ManifoldGeometry::numFacets() const {
  return getManifold().NumTri();
}

size_t ManifoldGeometry::numVertices() const {
  return getManifold().NumVert();
}

bool ManifoldGeometry::isManifold() const {
  return getManifold().Status() == manifold::Manifold::Error::NoError;
}

bool ManifoldGeometry::isValid() const {
  return manifold_->Status() == manifold::Manifold::Error::NoError;
}

void ManifoldGeometry::clear() {
  manifold_ = std::make_shared<manifold::Manifold>();
}

size_t ManifoldGeometry::memsize() const {
  // We don't introspect on the manifold here, as this would force it to leaf node (ie. would render it).
  return 0;
}

std::string ManifoldGeometry::dump() const {
  std::ostringstream out;
  auto &manifold = getManifold();
  auto mesh = manifold.GetMesh();
  out << "Manifold:"
      << "\n status: " << ManifoldUtils::statusToString(manifold.Status())
      << "\n genus: " << manifold.Genus()
      << "\n num vertices: " << mesh.vertPos.size()
      << "\n num polygons: " << mesh.triVerts.size()
      << "\n polygons data:";

  for (const auto &tv : mesh.triVerts) {
    out << "\n  polygon begin:";
    for (const int j : {0, 1, 2}) {
      Vector3d v = vector_convert<Vector3d>(mesh.vertPos[tv[j]]);
      out << "\n   vertex:" << v;
    }
  }
  out << "Manifold end";
  return out.str();
}

std::shared_ptr<const PolySet> ManifoldGeometry::toPolySet() const {
  manifold::MeshGL mesh = getManifold().GetMeshGL();
  auto ps = std::make_shared<PolySet>(3);
  ps->vertices.reserve(mesh.NumVert());
  ps->indices.reserve(mesh.NumTri());
  // first 3 channels are xyz coordinate
  for (int i = 0; i < mesh.vertProperties.size(); i += mesh.numProp)
    ps->vertices.push_back({
        mesh.vertProperties[i],
        mesh.vertProperties[i+1],
        mesh.vertProperties[i+2]});
  for (int i = 0; i < mesh.triVerts.size(); i += 3)
    ps->indices.push_back({
        static_cast<int>(mesh.triVerts[i]),
        static_cast<int>(mesh.triVerts[i+1]),
        static_cast<int>(mesh.triVerts[i+2])});
  return ps;
}

#ifdef ENABLE_CGAL
template <typename Polyhedron>
class CGALPolyhedronBuilderFromManifold : public CGAL::Modifier_base<typename Polyhedron::HalfedgeDS>
{
  using HDS = typename Polyhedron::HalfedgeDS;
  using CGAL_Polybuilder = CGAL::Polyhedron_incremental_builder_3<typename Polyhedron::HalfedgeDS>;
public:
  using CGALPoint = typename CGAL_Polybuilder::Point_3;

  const manifold::Mesh& mesh;
  CGALPolyhedronBuilderFromManifold(const manifold::Mesh& mesh) : mesh(mesh) { }

  void operator()(HDS& hds) override {
    CGAL_Polybuilder B(hds, true);
  
    B.begin_surface(mesh.vertPos.size(), mesh.triVerts.size());
    for (const auto &v : mesh.vertPos) {
      B.add_vertex(CGALUtils::vector_convert<CGALPoint>(v));
    }

    for (const auto &tv : mesh.triVerts) {
      B.begin_facet();
      for (const int j : {0, 1, 2}) {
        B.add_vertex_to_facet(tv[j]);
      }
      B.end_facet();
    }
    B.end_surface();
  }
};

template <class Polyhedron>
std::shared_ptr<Polyhedron> ManifoldGeometry::toPolyhedron() const
{
  auto p = std::make_shared<Polyhedron>();
  try {
    manifold::Mesh mesh = getManifold().GetMesh();
    CGALPolyhedronBuilderFromManifold<Polyhedron> builder(mesh);
    p->delegate(builder);
  } catch (const CGAL::Assertion_exception& e) {
    LOG(message_group::Error, "CGAL error in CGALUtils::createPolyhedronFromPolySet: %1$s", e.what());
  }
  return p;
}

template std::shared_ptr<CGAL::Polyhedron_3<CGAL_Kernel3>> ManifoldGeometry::toPolyhedron() const;
#endif

std::shared_ptr<manifold::Manifold> binOp(ManifoldGeometry& lhs, ManifoldGeometry& rhs, manifold::OpType opType) {
  return std::make_shared<manifold::Manifold>(lhs.getManifold().Boolean(rhs.getManifold(), opType));
}

void ManifoldGeometry::operator+=(ManifoldGeometry& other) {
  manifold_ = binOp(*this, other, manifold::OpType::Add);
}

void ManifoldGeometry::operator*=(ManifoldGeometry& other) {
  manifold_ = binOp(*this, other, manifold::OpType::Intersect);
}

void ManifoldGeometry::operator-=(ManifoldGeometry& other) {
  manifold_ = binOp(*this, other, manifold::OpType::Subtract);
}

void ManifoldGeometry::minkowski(ManifoldGeometry& other) {
// FIXME: How to deal with operation not supported?
#ifdef ENABLE_CGAL
  auto lhs = std::shared_ptr<CGAL_Nef_polyhedron>(CGALUtils::createNefPolyhedronFromPolySet(*this->toPolySet()));
  auto rhs = std::shared_ptr<CGAL_Nef_polyhedron>(CGALUtils::createNefPolyhedronFromPolySet(*other.toPolySet()));
  if (lhs->isEmpty() || rhs->isEmpty()) {
    clear();
    return;
  }
  lhs->minkowski(*rhs);

  auto ps = PolySetUtils::getGeometryAsPolySet(lhs);
  if (!ps) clear();
  else {
    manifold_ = ManifoldUtils::trustedPolySetToManifold(*ps);
  }
#endif
}

void ManifoldGeometry::transform(const Transform3d& mat) {
  glm::mat4x3 glMat(
    // Column-major ordering
    mat(0, 0), mat(1, 0), mat(2, 0),
    mat(0, 1), mat(1, 1), mat(2, 1),
    mat(0, 2), mat(1, 2), mat(2, 2),
    mat(0, 3), mat(1, 3), mat(2, 3)
  );                            
  manifold_ = std::make_shared<manifold::Manifold>(getManifold().Transform(glMat));
}

BoundingBox ManifoldGeometry::getBoundingBox() const
{
  BoundingBox result;
  manifold::Box bbox = getManifold().BoundingBox();
  result.extend(vector_convert<Eigen::Vector3d>(bbox.min));
  result.extend(vector_convert<Eigen::Vector3d>(bbox.max));
  return result;
}

void ManifoldGeometry::resize(const Vector3d& newsize, const Eigen::Matrix<bool, 3, 1>& autosize) {
  transform(GeometryUtils::getResizeTransform(this->getBoundingBox(), newsize, autosize));
}

/*! Iterate over all vertices' points until the function returns true (for done). */
void ManifoldGeometry::foreachVertexUntilTrue(const std::function<bool(const glm::vec3& pt)>& f) const {
  auto mesh = getManifold().GetMesh();
  for (const auto &pt : mesh.vertPos) {
    if (f(pt)) {
      return;
    }
  }
}
