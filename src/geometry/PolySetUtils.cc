#include "PolySetUtils.h"
#include "PolySet.h"
#include "PolySetBuilder.h"
#include "Polygon2d.h"
#include "printutils.h"
#include "GeometryUtils.h"
#include "Reindexer.h"
#ifdef ENABLE_CGAL
#include "cgalutils.h"
#include "CGALHybridPolyhedron.h"
#endif
#ifdef ENABLE_MANIFOLD
#include "ManifoldGeometry.h"
#endif

namespace PolySetUtils {

// Project all polygons (also back-facing) into a Polygon2d instance.
// It is important to select all faces, since filtering by normal vector here
// will trigger floating point incertainties and cause problems later.
std::unique_ptr<Polygon2d> project(const PolySet& ps) {
  auto poly = std::make_unique<Polygon2d>();

  Vector3d pt;
  for (const auto& p : ps.indices) {
    Outline2d outline;
    for (const auto& v : p) {
      pt=ps.vertices[v];	    
      outline.vertices.emplace_back(pt[0], pt[1]);
    }
    poly->addOutline(outline);
  }
  return poly;
}

/* Tessellation of 3d PolySet faces

   This code is for tessellating the faces of a 3d PolySet, assuming that
   the faces are near-planar polygons.

   The purpose of this code is originally to fix github issue 349. Our CGAL
   kernel does not accept polygons for Nef_Polyhedron_3 if each of the
   points is not exactly coplanar. "Near-planar" or "Almost planar" polygons
   often occur due to rounding issues on, for example, polyhedron() input.
   By tessellating the 3d polygon into individual smaller tiles that
   are perfectly coplanar (triangles, for example), we can get CGAL to accept
   the polyhedron() input.
 */
/* Given a 3D PolySet with near planar polygonal faces, tessellate the
   faces. As of writing, our only tessellation method is triangulation
   using CGAL's Constrained Delaunay algorithm. This code assumes the input
   polyset has simple polygon faces with no holes.
   The tessellation will be robust wrt. degenerate and self-intersecting
 */
std::unique_ptr<PolySet> tessellate_faces(const PolySet& polyset)
{
  int degeneratePolygons = 0;

  // Build Indexed PolyMesh
  Reindexer<Vector3f> allVertices;
  std::vector<std::vector<IndexedFace>> polygons;

  // best estimate without iterating all polygons, to reduce reallocations
  polygons.reserve(polyset.indices.size() );

  // minimum estimate without iterating all polygons, to reduce reallocation and rehashing
  allVertices.reserve(3 * polyset.indices.size() );

  for (const auto& pgon : polyset.indices) {
    if (pgon.size() < 3) {
      degeneratePolygons++;
      continue;
    }

    polygons.emplace_back();
    auto& faces = polygons.back();
    faces.push_back(IndexedFace());
    auto& currface = faces.back();
    for (const auto& ind : pgon) {
      const Vector3d v=polyset.vertices[ind];
      // Create vertex indices and remove consecutive duplicate vertices
      // NOTE: a lot of time is spent here (cast+hash+lookup+insert+rehash)
      auto idx = allVertices.lookup(v.cast<float>());
      if (currface.empty() || idx != currface.back()) currface.push_back(idx);
    }
    if (currface.front() == currface.back()) currface.pop_back();
    if (currface.size() < 3) {
      faces.pop_back(); // Cull empty triangles
      if (faces.empty()) polygons.pop_back(); // All faces were culled
    }
  }

  // Tessellate indexed mesh
  const auto& verts = allVertices.getArray();

  // we will reuse this memory instead of reallocating for each polygon
  std::vector<IndexedTriangle> triangles;

  // Estimate how many polygons we will need and preallocate.
  // This is usually an undercount, but still prevents a lot of reallocations.
  PolySetBuilder builder(verts.size(), polygons.size(), polyset.getDimension(), polyset.convexValue());
  builder.setConvexity(polyset.getConvexity());
  for(int i=0;i<verts.size();i++)
    builder.vertexIndex({verts[i][0],verts[i][1],verts[i][2]});


  for (const auto& faces : polygons) {
    if (faces[0].size() == 3) {
      // trivial case - triangles cannot be concave or have holes
       builder.appendPoly({faces[0][0],faces[0][1],faces[0][2]});
    }
    // Quads seem trivial, but can be concave, and can have degenerate cases.
    // So everything more complex than triangles goes into the general case.
    else {
      triangles.clear();
      auto err = GeometryUtils::tessellatePolygonWithHoles(verts, faces, triangles, nullptr);
      if (!err) {
        for (const auto& t : triangles) {
       	  builder.appendPoly({t[0],t[1],t[2]});
        }
      }
    }
  }
  if (degeneratePolygons > 0) {
    LOG(message_group::Warning, "PolySet has degenerate polygons");
  }
  return builder.build();
}

bool is_approximately_convex(const PolySet& ps) {
#ifdef ENABLE_CGAL
  return CGALUtils::is_approximately_convex(ps);
#else
  return false;
#endif
}

// Get as or convert the geometry to a PolySet.
std::shared_ptr<const PolySet> getGeometryAsPolySet(const std::shared_ptr<const Geometry>& geom)
{
  if (auto ps = std::dynamic_pointer_cast<const PolySet>(geom)) {
    return ps;
  }
#ifdef ENABLE_CGAL
  if (auto N = std::dynamic_pointer_cast<const CGAL_Nef_polyhedron>(geom)) {
    if (!N->isEmpty()) {
      if (auto ps = CGALUtils::createPolySetFromNefPolyhedron3(*N->p3)) {
	ps->setConvexity(N->getConvexity());
	return ps;
      }
      LOG(message_group::Error, "Nef->PolySet failed.");
    }
    return std::make_unique<PolySet>(3);
  }
  if (auto hybrid = std::dynamic_pointer_cast<const CGALHybridPolyhedron>(geom)) {
    return hybrid->toPolySet();
  }
#endif
#ifdef ENABLE_MANIFOLD
  if (auto mani = std::dynamic_pointer_cast<const ManifoldGeometry>(geom)) {
    return mani->toPolySet();
  }
#endif
  return nullptr;
}

} // namespace PolySetUtils
