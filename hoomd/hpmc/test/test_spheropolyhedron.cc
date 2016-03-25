
#include "hoomd/ExecutionConfiguration.h"

//! Name the unit test module
#define BOOST_TEST_MODULE ShapeSpheropolyhedron
#include "boost_utf_configure.h"

#include "hoomd/hpmc/IntegratorHPMC.h"
#include "hoomd/hpmc/Moves.h"
#include "hoomd/hpmc/ShapeSpheropolyhedron.h"

#include <iostream>
#include <string>

#include <boost/bind.hpp>
#include <boost/python.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

using namespace hpmc;
using namespace std;
using namespace hpmc::detail;

unsigned int err_count = 0;
const unsigned int max_verts = 8;

// helper function to compute poly radius
poly3d_verts<max_verts> setup_verts(const vector< vec3<OverlapReal> > vlist, OverlapReal sweep_radius)
    {
    if (vlist.size() > max_verts)
        throw runtime_error("Too many polygon vertices");

    poly3d_verts<max_verts> result;
    result.N = vlist.size();
    result.sweep_radius = sweep_radius;
    result.ignore = 0;

    // extract the verts from the python list and compute the radius on the way
    OverlapReal radius_sq = OverlapReal(0.0);
    for (unsigned int i = 0; i < vlist.size(); i++)
        {
        vec3<OverlapReal> vert = vlist[i];
        result.x[i] = vert.x;
        result.y[i] = vert.y;
        result.z[i] = vert.z;
        radius_sq = std::max(radius_sq, dot(vert, vert));
        }
    for (unsigned int i = vlist.size(); i < max_verts; i++)
        {
        result.x[i] = 0;
        result.y[i] = 0;
        result.z[i] = 0;
        }

    // set the diameter
    result.diameter = 2*(sqrt(radius_sq)+sweep_radius);

    return result;
    }

BOOST_AUTO_TEST_CASE( construction )
    {
    quat<Scalar> o(1.0, vec3<Scalar>(-3.0, 9.0, 6.0));

    vector< vec3<OverlapReal> > vlist;
    vlist.push_back(vec3<Scalar>(0,0,0));
    vlist.push_back(vec3<Scalar>(1,0,0));
    vlist.push_back(vec3<Scalar>(0,1.25,0));
    vlist.push_back(vec3<Scalar>(0,0,1.1));
    poly3d_verts<max_verts> verts = setup_verts(vlist, 0.25);

    ShapeSpheropolyhedron<max_verts> a(o, verts);

    MY_BOOST_CHECK_CLOSE(a.orientation.s, o.s, tol);
    MY_BOOST_CHECK_CLOSE(a.orientation.v.x, o.v.x, tol);
    MY_BOOST_CHECK_CLOSE(a.orientation.v.y, o.v.y, tol);
    MY_BOOST_CHECK_CLOSE(a.orientation.v.z, o.v.z, tol);

    BOOST_REQUIRE_EQUAL(a.verts.N, verts.N);
    for (unsigned int i = 0; i < verts.N; i++)
        {
        MY_BOOST_CHECK_CLOSE(a.verts.x[i], verts.x[i], tol);
        MY_BOOST_CHECK_CLOSE(a.verts.y[i], verts.y[i], tol);
        }
    BOOST_REQUIRE_EQUAL(a.verts.sweep_radius, verts.sweep_radius);
    BOOST_REQUIRE_EQUAL(a.verts.ignore, verts.ignore);

    BOOST_CHECK(a.hasOrientation());

    MY_BOOST_CHECK_CLOSE(a.getCircumsphereDiameter(), 3.0, tol);
    }

BOOST_AUTO_TEST_CASE( support )
    {
    // Find the support of a tetrahedron
    quat<Scalar> o;
    BoxDim box(100);

    vector< vec3<OverlapReal> > vlist;
    vlist.push_back(vec3<OverlapReal>(-0.5, -0.5, -0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5, 0.5, 0.5));
    vlist.push_back(vec3<OverlapReal>(0.5, -0.5, 0.5));
    vlist.push_back(vec3<OverlapReal>(0.5, 0.5, -0.5));
    poly3d_verts<max_verts> verts = setup_verts(vlist, 0.0);

    ShapeSpheropolyhedron<max_verts> a(o, verts);
    SupportFuncSpheropolyhedron<max_verts> sa = SupportFuncSpheropolyhedron<max_verts>(verts);
    vec3<OverlapReal> v1, v2;

    v1 = sa(vec3<OverlapReal>(-0.5, -0.5, -0.5));
    v2 = vec3<OverlapReal>(-0.5, -0.5, -0.5);
    BOOST_CHECK(v1 == v2);
    v1 = sa(vec3<OverlapReal>(-0.1, 0.1, 0.1));
    v2 = vec3<OverlapReal>(-0.5, 0.5, 0.5);
    BOOST_CHECK(v1 == v2);
    v1 = sa(vec3<OverlapReal>(1, -1, 1));
    v2 = vec3<OverlapReal>(0.5, -0.5, 0.5);
    BOOST_CHECK(v1 == v2);
    v1 = sa(vec3<OverlapReal>(0.51, 0.49, -0.1));
    v2 = vec3<OverlapReal>(0.5, 0.5, -0.5);
    BOOST_CHECK(v1 == v2);
    }

/*! Not sure how best to test this because not sure what a valid support has to be...
BOOST_AUTO_TEST_CASE( composite_support )
    {
    // Find the support of the Minkowski difference of two offset cubes
    quat<Scalar> o;
    BoxDim box(100);

    vector< vec3<OverlapReal> > vlist;
    vlist.push_back(vec3<Scalar>(-0.5, -0.5, -0.5));
    vlist.push_back(vec3<Scalar>(-0.5, 0.5, 0.5));
    vlist.push_back(vec3<Scalar>(0.5, -0.5, 0.5));
    vlist.push_back(vec3<Scalar>(0.5, 0.5, -0.5));
    vlist.push_back(vec3<Scalar>(-0.5, -0.5, 0.5));
    vlist.push_back(vec3<Scalar>(-0.5, 0.5, -0.5));
    vlist.push_back(vec3<Scalar>(0.5, -0.5, -0.5));
    vlist.push_back(vec3<Scalar>(0.5, 0.5, 0.5));
    poly3d_verts verts = setup_verts(vlist, 0.0);

    ShapeSpheropolyhedron a(o, verts);
    ShapeSpheropolyhedron b(o, verts);
    SupportFuncSpheropolyhedron s = SupportFuncSpheropolyhedron(verts);
    CompositeSupportFunc3D<SupportFuncSpheropolyhedron, SupportFuncSpheropolyhedron>* S;
    vec3<Scalar> v1;

    S = new CompositeSupportFunc3D<SupportFuncSpheropolyhedron, SupportFuncSpheropolyhedron>(s, s, b.pos - a.pos, a.orientation, b.orientation);

    // v1 should always be the half-body-diagonal of a cube with corners at ([-]1,[-]1,[-]1)
    v1 = (*S)(vec3<Scalar>(0.6,0.4,1));
    cout << "v1 is (" << v1.x << "," << v1.y << "," << v1.z << ")\n";
    BOOST_CHECK(dot(v1,v1) == 3);
    v1 = (*S)(vec3<Scalar>(.1,0,0));
    cout << "v1 is (" << v1.x << "," << v1.y << "," << v1.z << ")\n";
    BOOST_CHECK(dot(v1,v1) == 3);
    v1 = (*S)(vec3<Scalar>(.1,.1,0));
    cout << "v1 is (" << v1.x << "," << v1.y << "," << v1.z << ")\n";
    BOOST_CHECK(dot(v1,v1) == 3);
    cout << flush;
    }
*/

BOOST_AUTO_TEST_CASE( sphere )
    {
    // test sphere: zero-vertex special case
    vec3<Scalar> r_ij;
    quat<Scalar> o;
    BoxDim box(100);

    // build a sphere
    vector< vec3<OverlapReal> > vlist;
    poly3d_verts<max_verts> verts = setup_verts(vlist, 0.5);

    // test overla
    ShapeSpheropolyhedron<max_verts> a(o, verts);
    ShapeSpheropolyhedron<max_verts> b(o, verts);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));
    r_ij = vec3<Scalar>(.2,.2,.1);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));
    r_ij = vec3<Scalar>(-.2,-.2,-.1);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    // test non-overlap using calculated circumsphere
    r_ij = vec3<Scalar>(3,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));
    r_ij = vec3<Scalar>(2,2,1);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));
    r_ij = vec3<Scalar>(-2,-2,-1);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // test non-overlap using Minkowski difference
    verts.diameter = 10.0;
    ShapeSpheropolyhedron<max_verts> c(o, verts);
    ShapeSpheropolyhedron<max_verts> d(o, verts);
    r_ij = vec3<Scalar>(3,0,0);
    BOOST_CHECK(!test_overlap(r_ij,c,d,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,d,c,err_count));
    r_ij = vec3<Scalar>(2,2,1);
    BOOST_CHECK(!test_overlap(r_ij,c,d,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,d,c,err_count));
    r_ij = vec3<Scalar>(-2,-2,-1);
    BOOST_CHECK(!test_overlap(r_ij,c,d,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,d,c,err_count));
    }

BOOST_AUTO_TEST_CASE( overlap_octahedron_no_rot )
    {
    // first set of simple overlap checks is two octahedra at unit orientation
    vec3<Scalar> r_ij;
    quat<Scalar> o;
    BoxDim box(100);

    // build a square
    vector< vec3<OverlapReal> > vlist;
    vlist.push_back(vec3<OverlapReal>(-0.5,-0.5,0));
    vlist.push_back(vec3<OverlapReal>(0.5,-0.5,0));
    vlist.push_back(vec3<OverlapReal>(0.5,0.5,0));
    vlist.push_back(vec3<OverlapReal>(-0.5,0.5,0));
    vlist.push_back(vec3<OverlapReal>(0,0,0.707106781186548));
    vlist.push_back(vec3<OverlapReal>(0,0,-0.707106781186548));
    poly3d_verts<max_verts> verts = setup_verts(vlist, 0.0);

    ShapeSpheropolyhedron<max_verts> a(o, verts);

    // first test, separate squares by a large distance
    ShapeSpheropolyhedron<max_verts> b(o, verts);
    r_ij = vec3<Scalar>(10,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // next test, set them close, but not overlapping - from all four sides of base
    r_ij = vec3<Scalar>(1.1,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-1.1,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,1.1,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,-1.1,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // now test them close, but slightly offset and not overlapping - from all four sides
    r_ij = vec3<Scalar>(1.1,0.2,0.1);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-1.1,0.2,0.1);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,1.1,0.1);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,-1.1,0.1);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // and finally, make them overlap slightly in each direction
    r_ij = vec3<Scalar>(0.9,0.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.9,0.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,0.9,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,-0.9,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    // torture test, overlap along most of a line
    // this torture test works because 1.0 and 0.5 (the polygon verts) are exactly representable in floating point
    // checking this is important because in a large MC simulation, you are certainly going to find cases where edges or
    // vertices touch exactly
    r_ij = vec3<Scalar>(1.0,0.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));
    }

BOOST_AUTO_TEST_CASE( overlap_cube_no_rot )
    {
    // first set of simple overlap checks is two squares at unit orientation
    vec3<Scalar> r_ij;
    quat<Scalar> o;
    BoxDim box(100);

    // build a square
    vector< vec3<OverlapReal> > vlist;
    vlist.push_back(vec3<OverlapReal>(-0.5,-0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,-0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,-0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,-0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,0.5,0.5));
    poly3d_verts<max_verts> verts = setup_verts(vlist, 0.0);

    ShapeSpheropolyhedron<max_verts> a(o, verts);

    // first test, separate squares by a large distance
    ShapeSpheropolyhedron<max_verts> b(o, verts);
    r_ij = vec3<Scalar>(10,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // next test, set them close, but not overlapping - from all four sides
    r_ij = vec3<Scalar>(1.1,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-1.1,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,1.1,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,-1.1,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // now test them close, but slightly offset and not overlapping - from all four sides
    r_ij = vec3<Scalar>(1.1,0.2,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-1.1,0.2,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,1.1,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,-1.1,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // make them overlap slightly in each direction
    r_ij = vec3<Scalar>(0.9,0.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.9,0.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,0.9,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,-0.9,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    // Make them overlap a lot
    r_ij = vec3<Scalar>(0.2,0,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,0.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0.2,tol,tol);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0.1,0.2,0.1);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    // torture test, overlap along most of a line
    // this torture test works because 1.0 and 0.5 (the polygon verts) are exactly representable in floating point
    // checking this is important because in a large MC simulation, you are certainly going to find cases where edges or
    // vertices touch exactly
    r_ij = vec3<Scalar>(1.0,0.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));
    }


BOOST_AUTO_TEST_CASE( overlap_cube_rot1 )
    {
    // second set of simple overlap checks is two cubes, with one rotated by 45 degrees
    vec3<Scalar> r_ij;
    quat<Scalar> o_a;
    Scalar alpha = M_PI/4.0;
    quat<Scalar> o_b(cos(alpha/2.0), (Scalar)sin(alpha/2.0) * vec3<Scalar>(0,0,1)); // rotation quaternion

    BoxDim box(100);

    // build a square
    vector< vec3<OverlapReal> > vlist;
    vlist.push_back(vec3<OverlapReal>(-0.5,-0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,-0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,-0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,-0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,0.5,0.5));
    poly3d_verts<max_verts> verts = setup_verts(vlist, 0.0);

    ShapeSpheropolyhedron<max_verts> a(o_a, verts);

    // first test, separate squares by a large distance
    ShapeSpheropolyhedron<max_verts> b(o_b, verts);
    r_ij = vec3<Scalar>(10,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // next test, set them close, but not overlapping - from all four sides
    r_ij = vec3<Scalar>(1.3,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-1.3,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,1.3,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,-1.3,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // now test them close, but slightly offset and not overlapping - from all four sides
    r_ij = vec3<Scalar>(1.3,0.2,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-1.3,0.2,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,1.3,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,-1.3,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // and finally, make them overlap slightly in each direction
    r_ij = vec3<Scalar>(1.2,0.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-1.2,0.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,1.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,-1.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));
    }

BOOST_AUTO_TEST_CASE( overlap_cube_rot2 )
    {
    // third set of simple overlap checks is two cubes, with the other one rotated by 45 degrees
    vec3<Scalar> r_ij;
    quat<Scalar> o_a;
    Scalar alpha = M_PI/4.0;
    quat<Scalar> o_b(cos(alpha/2.0), (Scalar)sin(alpha/2.0) * vec3<Scalar>(0,0,1)); // rotation quaternion

    BoxDim box(100);

    // build a cube
    vector< vec3<OverlapReal> > vlist;
    vlist.push_back(vec3<OverlapReal>(-0.5,-0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,-0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,-0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,-0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,0.5,0.5));
    poly3d_verts<max_verts> verts = setup_verts(vlist, 0.0);

    ShapeSpheropolyhedron<max_verts> a(o_b, verts);

    // first test, separate cubes by a large distance
    ShapeSpheropolyhedron<max_verts> b(o_a, verts);
    r_ij = vec3<Scalar>(10,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // next test, set them close, but not overlapping - from all four sides
    r_ij = vec3<Scalar>(1.3,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-1.3,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,1.3,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,-1.3,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // now test them close, but slightly offset and not overlapping - from all four sides
    r_ij = vec3<Scalar>(1.3,0.2,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-1.3,0.2,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,1.3,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,-1.3,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // and finally, make them overlap slightly in each direction
    r_ij = vec3<Scalar>(1.2,0.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-1.2,0.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,1.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,-1.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));
    }

BOOST_AUTO_TEST_CASE( overlap_cube_rot3 )
    {
    // two cubes, with one rotated by 45 degrees around two axes. This lets us look at edge-edge and point-face collisions
    vec3<Scalar> r_ij;
    quat<Scalar> o_a;
    Scalar alpha = M_PI/4.0;
    // rotation around x and then z
    const quat<Scalar> q1(cos(alpha/2.0), (Scalar)sin(alpha/2.0) * vec3<Scalar>(1,0,0));
    const quat<Scalar> q2(cos(alpha/2.0), (Scalar)sin(alpha/2.0) * vec3<Scalar>(0,0,1));
    quat<Scalar> o_b(q2 * q1);

    BoxDim box(100);

    // build a cube
    vector< vec3<OverlapReal> > vlist;
    vlist.push_back(vec3<OverlapReal>(-0.5,-0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,-0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,-0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,-0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,0.5,0.5));
    poly3d_verts<max_verts> verts = setup_verts(vlist, 0.0);

    ShapeSpheropolyhedron<max_verts> a(o_a, verts);

    // first test, separate squares by a large distance
    ShapeSpheropolyhedron<max_verts> b(o_b, verts);
    r_ij = vec3<Scalar>(10,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // next test, set them close, but not overlapping - from four sides
    r_ij = vec3<Scalar>(1.4,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-1.4,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,1.4,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,-1.4,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // now test them close, but slightly offset and not overlapping - from four sides
    r_ij = vec3<Scalar>(1.4,0.2,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-1.4,0.2,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,1.4,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.2,-1.4,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // Test point-face overlaps
    r_ij = vec3<Scalar>(0,1.2,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,1.2,0.1);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0.1,1.2,0.1);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(1.2,0,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(1.2,0.1,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(1.2,0.1,0.1);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    // Test edge-edge overlaps
    r_ij = vec3<Scalar>(-0.9,0.9,0.0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.9,0.899,0.001);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0.9,-0.9,0.0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count)); // this and only this test failed :-(
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.9,0.899,0.001);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-0.9,0.9,0.1);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    }


BOOST_AUTO_TEST_CASE( overlap_cube_precise )
    {
    // Rounding radius
    Scalar R=0.1;

    // set the precision
    Scalar offset = 0.00001 / 10.0;

    // test two squares just touching and barely separated to test precision
    vec3<Scalar> r_ij;
    quat<Scalar> o;
    BoxDim box(100);

    // build a square
    // build a cube
    vector< vec3<OverlapReal> > vlist;
    vlist.push_back(vec3<OverlapReal>(-0.5,-0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,-0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,0.5,-0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,-0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,-0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(0.5,0.5,0.5));
    vlist.push_back(vec3<OverlapReal>(-0.5,0.5,0.5));
    poly3d_verts<max_verts> verts = setup_verts(vlist, R);

    ShapeSpheropolyhedron<max_verts> a(o, verts);
    ShapeSpheropolyhedron<max_verts> b(o, verts);

    // test face-face non-overlaps
    r_ij = vec3<Scalar>(1 + 2*R + offset,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-(1 + 2*R + offset),0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,1 + 2*R + offset,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,-(1 + 2*R + offset),0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,0,1 + 2*R + offset);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,0,-(1 + 2*R + offset));
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // test face-face overlaps
    r_ij = vec3<Scalar>(1 + 2*R - offset,0,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-(1 + 2*R - offset),0,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,1 + 2*R - offset,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,-(1 + 2*R - offset),0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,0,1 + 2*R - offset);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,0,-(1 + 2*R - offset));
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));


    // edge-edge test
    // rotate cubes by 45 degrees about z
    Scalar alpha = M_PI/4.0;
    quat<Scalar> o_45(cos(alpha/2.0), (Scalar)sin(alpha/2.0) * vec3<Scalar>(0,0,1)); // rotation quaternion
    a.orientation = o_45;
    b.orientation = o_45;

    // check non-overlapping
    Scalar d = sqrt(2) + 2*R + offset;
    r_ij = vec3<Scalar>(d,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-d,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,d,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,-d,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // check overlaps
    d = sqrt(2) + 2*R - offset;
    r_ij = vec3<Scalar>(d,0,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-d,0,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,d,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,-d,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    // edge-face test
    // rotate only one cube by 45 degrees
    a.orientation = o;
    b.orientation = o_45;

    // check non-overlapping
    d = 0.5 * (1+sqrt(2)) + 2*R + offset;
    r_ij = vec3<Scalar>(d,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-d,0,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,d,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,-d,0);
    BOOST_CHECK(!test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(!test_overlap(-r_ij,b,a,err_count));

    // check overlaps
    d = 0.5 * (1+sqrt(2)) + 2*R - offset;
    r_ij = vec3<Scalar>(d,0,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(-d,0,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,d,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));

    r_ij = vec3<Scalar>(0,-d,0);
    BOOST_CHECK(test_overlap(r_ij,a,b,err_count));
    BOOST_CHECK(test_overlap(-r_ij,b,a,err_count));
    }
