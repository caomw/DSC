//
// Created by Morten Nobel-Jørgensen on 19/11/14.
// Copyright (c) 2014 ___FULLUSERNAME___. All rights reserved.
//

#include "Query.h"
#include "is_mesh/is_mesh.h"
#include <limits>
#include <cmath>

using namespace CGLA;

/**
* Pseudocode
* 1. Find "back" boundary triangle (O(n))
* 2. Find enclosing tetrahedron
* 3. While tetrahedron exists
* 3.1 Find intersection with other faces on tetrahedron
* 3.2 Find new tetrahedron
*/
namespace is_mesh {

    // It is very likely to raytrace regular axis aligned meshes where the origin of the ray happens to match one of the
    // planes in the mesh. This minimizes the change of this significant.
    vec3 addEpsilonOffset(vec3 v){
        return v + vec3(0.001,0.003,0.007);
//        return vec3(
//                nextafter((float)v[0],FLT_MAX),
//                nextafter(nextafter((float)v[1],FLT_MAX),FLT_MAX),
//                nextafter(nextafter(nextafter((float)v[2],FLT_MAX),FLT_MAX),FLT_MAX));
    }

    Query::Query(ISMesh *mesh)
            : mesh(mesh) {

    }

    Query::~Query() {
        delete boundary_faces;
    }

    QueryResult Query::raycast_faces(Ray ray, QueryType queryType) {
        ray = Ray(addEpsilonOffset(ray.get_origin()), ray.get_direction());
        if (!boundary_faces){
            rebuild_boundary_cache();
        }
        FaceKey firstIntersection((unsigned int) -1);
        double dist = std::numeric_limits<double>::max();

        for (auto faceIter : *boundary_faces){
            Face & face = mesh->get(faceIter);
            auto nodePos = mesh->get_pos(face.node_keys());
            double newDist;
            if (ray.intersect_triangle(nodePos[0], nodePos[1], nodePos[2], newDist)){
                bool isFirstFoundTriangle = dist == std::numeric_limits<double>::max();
                if (isFirstFoundTriangle){
                    firstIntersection = faceIter;
                    dist = newDist;
                } else {
                    if (newDist < dist){
                        firstIntersection = faceIter;
                        dist = newDist;
                    }
                }
            }
        }

        if ((unsigned int)firstIntersection == (unsigned int)-1){
            return QueryResult();
        }

        return QueryResult(firstIntersection, dist, ray, queryType, mesh);
    }

    void Query::rebuild_boundary_cache() {
        if (!boundary_faces){
            boundary_faces = new std::vector<FaceKey>();
        } else {
            boundary_faces->clear();
        }

        for (auto & faceiter : mesh->faces()){
            if (faceiter.is_boundary()){
                boundary_faces->push_back(faceiter.key());
            }
        }
    }

    QueryResultIterator::QueryResultIterator() {
        face_key = FaceKey((unsigned int) -1);
    }


    QueryResultIterator::QueryResultIterator(FaceKey const &first_boundary_intersection, double dist, Ray const &ray, QueryType const &query_type, ISMesh *mesh)
            : face_key(first_boundary_intersection), dist(dist), ray(ray.get_origin(), ray.get_direction()),query_type(query_type), mesh(mesh) {

        tetrahedron_key = mesh->get(first_boundary_intersection).tet_keys(); // since boundary always only one
        if (dist < 0 || query_type == QueryType::Interface){
            next();
        }
    }

    void QueryResultIterator::next() {
        if (tetrahedron_key.size()==0){
            face_key = FaceKey((unsigned int) -1);
        } else {
            while (tetrahedron_key.size()>0){
                SimplexSet<FaceKey> faces = mesh->get_faces(tetrahedron_key) - face_key;
                double oldDist = dist;
                if (tetWalking(faces)){
                    return;
                }
                bool found_new_tet = oldDist != dist;
                if (found_new_tet){
                    continue;
                }
                break;
            }
        }
        face_key = FaceKey((unsigned int) -1);
    }

    bool QueryResultIterator::tetWalking(SimplexSet < FaceKey > &faces) {
        double newDist = std::numeric_limits<double>::max();
        bool found = false;
        for (FaceKey current_face_key : faces) {
            Face & face = mesh->get(current_face_key);
            auto nodePos = mesh->get_pos(face.node_keys());
            if (ray.intersect_triangle(nodePos[0], nodePos[1], nodePos[2], newDist)) {
                face_key = current_face_key;
                tetrahedron_key = face.get_co_boundary() - tetrahedron_key;
                dist = newDist;
                if (newDist >= dist && (query_type == QueryType::All ||
                        (query_type == QueryType::Interface && face.is_interface()) ||
                        (query_type == QueryType::Boundary && face.is_boundary()))){
                    found = true;
                }
                break;
            }
        }
        return found;
    }

    CGLA::Vec3d QueryResultIterator::collision_point() {
        return ray.get_origin() + ray.get_direction() * dist;
    }

    FaceKey QueryResultIterator::operator*() { return face_key; }

    bool QueryResultIterator::operator!=(const QueryResultIterator & other) {
        return !(face_key == other.face_key);
    }

    bool QueryResultIterator::operator==(const QueryResultIterator & other) {
        return (face_key == other.face_key);
    }


    QueryResult::QueryResult(FaceKey const &first_intersection, double dist, Ray const &ray, QueryType const &query_type, ISMesh *mesh)
            : first_intersection(first_intersection), dist(dist), ray(ray), query_type(query_type), mesh(mesh) {
    }

    QueryResultIterator QueryResult::begin() {
        if (mesh == nullptr){
            return end();
        }
        return QueryResultIterator{first_intersection, dist, ray, query_type, mesh};
    }

    QueryResultIterator QueryResult::end() {
        return QueryResultIterator{};
    }

    QueryResultIterator &QueryResultIterator::operator++() {
        next();
        return *this;
    }

    QueryResultIterator::QueryResultIterator(const QueryResultIterator &other)
        :dist(other.dist),
        ray(other.ray),
        query_type(other.query_type),
        tetrahedron_key(other.tetrahedron_key),
        face_key(other.face_key),
        mesh(other.mesh)
    {
    }


    std::set<NodeKey> Query::neighborhood(vec3 from, double max_distance) {
        std::set<NodeKey> res;
        double max_distanceSqr = max_distance * max_distance;
        for (auto & nodeiter : mesh->nodes()){
            vec3 to_pos = nodeiter.get_pos();
            double lengthSqr = sqr_length(from - to_pos);
            if (lengthSqr < max_distanceSqr){
                res.insert(nodeiter.key());
            }
        }
        return res;
    }

    std::set<NodeKey> Query::neighborhood(NodeKey from_node, double max_distance) {
        vec3 from_pos = mesh->get(from_node).get_pos();
        return neighborhood(from_pos, max_distance);
    }

    std::set<EdgeKey> Query::edges(const std::set<NodeKey> nodeKeys) {
        std::set<EdgeKey> res;
        for (auto & edgeIter : mesh->edges()){
            const auto & nodes = edgeIter.node_keys();

            if (nodeKeys.find(nodes[0]) != nodeKeys.end()  && nodeKeys.find(nodes[1]) != nodeKeys.end() ){
                res.insert(edgeIter.key());
            }
        }

        return res;
    }

    std::set<FaceKey> Query::faces(const std::set<EdgeKey> edgeKeys) {
        std::set<FaceKey> res;
        for (auto & faceIter : mesh->faces()){
            const auto &edges = faceIter.edge_keys();
            if (edgeKeys.find(edges[0]) != edgeKeys.end() && edgeKeys.find(edges[1]) != edgeKeys.end() && edgeKeys.find(edges[2]) != edgeKeys.end()){
                res.insert(faceIter.key());
            }
        }

        return res;
    }

    std::set<TetrahedronKey> Query::tetrahedra(const std::set<FaceKey> faceKeys){
        std::set<TetrahedronKey> res;
        for (auto & tetIter : mesh->tetrahedra()){
            const auto &faces = tetIter.face_keys();
            if (faceKeys.find(faces[0]) != faceKeys.end() && faceKeys.find(faces[1]) != faceKeys.end() &&
                    faceKeys.find(faces[2]) != faceKeys.end() && faceKeys.find(faces[3]) != faceKeys.end() ){
                res.insert(tetIter.key());
            }
        }
        return res;
    }

    std::set<NodeKey> Query::nodes(is_mesh::Geometry *geometry) {
        std::set<NodeKey> res;
        for (auto &n:mesh->nodes()){
            if (geometry->is_inside(n.get_pos())){
                res.insert(n.key());
            }
        }
        return res;
    }

    void Query::filter_subset(std::set<NodeKey> &nodes, std::set<EdgeKey> &edges, std::set<FaceKey> &faces, std::set<TetrahedronKey> &tets) {
        nodes.clear();
        edges.clear();
        faces.clear();
        for (auto t : tets){
            for (auto f : mesh->get(t).face_keys()){
                faces.insert(f);
            }
        }
        for (auto f : faces){
            for (auto e : mesh->get(f).edge_keys()){
                edges.insert(e);
            }
        }
        for (auto e : edges){
            for (auto n : mesh->get(e).node_keys()){
                nodes.insert(n);
            }
        }

        // exclude boundary vertices connected to non-manifold edges
        int maxKey = 0;
        for (auto t:tets){
            maxKey = std::max(maxKey, (int)t);
        }
        std::vector<bool> tetLookup(maxKey+1, false);
        for (auto t:tets){
            tetLookup[(int)t] = true;
        }

        std::set<TetrahedronKey> tetsToDelete;
        for (auto ek : edges){
            auto faceKeys = mesh->get(ek).face_keys();
            std::vector<TetrahedronKey> boundaryTets;
            for (auto fk : faceKeys){
                Face & face = mesh->get(fk);
                auto intersection = face.tet_keys();

                if (intersection.size() == 1 && tetLookup[(int)intersection[0]]){
                    boundaryTets.push_back(intersection[0]);
                } else if (intersection.size() == 2 && (tetLookup[(int)intersection[0]] ^ tetLookup[(int)intersection[1]])){
                    if (tetLookup[(int)intersection[0]]){
                        boundaryTets.push_back(intersection[0]);
                    } else {
                        boundaryTets.push_back(intersection[1]);
                    }
                }
            }
            bool isNonManifoldEdge = boundaryTets.size()>2; // has more than two boundary edges
            if (isNonManifoldEdge){
                for (auto tk : boundaryTets){
                    tetsToDelete.insert(tk);
                }
            }
        }
        if (!tetsToDelete.empty()){
            for (auto tk : tetsToDelete) {
                auto iter = tets.find(tk);
                if (iter != tets.end()){
                    tets.erase(iter);
                }
            }
            filter_subset(nodes, edges, faces, tets);
        }
    }

    const ISMesh *Query::get_is_mesh() {
        return mesh;
    }
}