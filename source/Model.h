#pragma once
#include <glm/glm.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <time.h>
#include <algorithm>
#include "Struct.h"
#define iINF 123456789
#define INF 123456789.0f


using namespace std;
using namespace glm;

class Model {
public:
	string name;			// scene name
	unsigned int vertex_num{ 0 }, tri_num{ 0 };
	AABB aabb, center_aabb;
	vector<string> Texture;	// Textrue path
	vector<vec3> Vertexs;	// Vertex vectors
	vector<vec3> Vns;		// Vertex normal vectors
	vector<vec2> Vts;		// Vertex texture coordinates
	vector<Triangle> Tris;	// Triangel faces

	vector<Triangle2Shader> Tris2Shader;	// tris with material info,prepare for shader
	vector<Triangle2Shader> Lights2Shader;	// tris of lights,prepare for shader

	vector<BVHNode> Nodes;
	vector<BVHNode2Shader> Nodes2Shader;
	vector<MortonTri> MortonTris;
	map<string, Material> Materials;	// (material name, material)
	Camera camera;
	
	Model();
	~Model();

	int ReadOBJ(const string& file);
	int ReadMTL(const string& file);
	int ReadXML(const string& file);
	int buildBVHwithSAH(int l, int r, int n, int depth = 0);
	int buildBVH(int l, int r, int n);
	//void GetMorton();
	//void GenTreelet();
	void Pre2Shader();
	//vector<Triangle> RadixSort();
	//BVHNode* HLBVHBuild(int* totalNodes);
	//BVHNode* buildUpperSAH(vector<BVHNode*>& treeletRoots, int start, int end, int* totalNodes);
	//__global__ friend void BulidTreelet_cuda();
	//__global__ friend void EncodeMorton3(Triangle* Tris, const unsigned int& size, const AABB& aabb);
};