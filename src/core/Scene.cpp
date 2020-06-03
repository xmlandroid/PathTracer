#include <iostream>

#include "Scene.h"
#include "Camera.h"

namespace GLSLPT
{
    void Scene::AddCamera(glm::vec3 pos, glm::vec3 lookAt, float fov)
    {
		if (camera) 
		{
			delete camera;
			camera = nullptr;
		}
        camera = new Camera(pos, lookAt, fov);
    }

	int Scene::AddMesh(const std::string& filename)
	{
		for (int i = 0; i < meshes.size(); ++i) 
		{
			if (meshes[i]->meshName == filename) {
				return i;
			}
		}

		int id = -1;
		Mesh *mesh = new Mesh;

		if (mesh->LoadFromFile(filename))
		{
			id = meshes.size();
			meshes.push_back(mesh);
		}
		else
		{
			delete mesh;
			id = -1;
		}

		return id;
	}

	int Scene::AddTexture(const std::string& filename)
	{
		for (int i = 0; i < textures.size(); ++i)
		{
			if (textures[i]->name == filename) {
				return i;
			}
		}

		int id = -1;
		Texture *texture = new Texture;

		if (texture->LoadTexture(filename))
		{
			id = textures.size();
			textures.push_back(texture);
			printf("Texture %s loaded\n", filename.c_str());
		}
		else
		{
			id = -1;
			delete texture;
		}

		return id;
	}

	int Scene::AddMaterial(const Material& material)
	{
		int id = materials.size();
		materials.push_back(material);
		return id;
	}

	void Scene::AddHDR(const std::string& filename)
	{
		if (hdrData)
		{
			delete hdrData;
			hdrData = nullptr;
		}
		
		hdrData = HDRLoader::Load(filename.c_str());
		if (hdrData == nullptr)
		{
			printf("Unable to load HDR\n");
		}
		else
		{
			printf("HDR %s loaded\n", filename.c_str());
			renderOptions.useEnvMap = true;
		}
	}

	int Scene::AddMeshInstance(const MeshInstance &meshInstance)
	{
		int id = meshInstances.size();
		meshInstances.push_back(meshInstance);
		return id;
	}

	int Scene::AddLight(const Light &light)
	{
		int id = lights.size();
		lights.push_back(light);
		return id;
	}

	void Scene::CreateTLAS()
	{
		// Loop through all the mesh Instances and build a Top Level BVH
		std::vector<Bounds3D> bounds;
		bounds.resize(meshInstances.size());

		for (int i = 0; i < meshInstances.size(); i++)
		{
			Bounds3D bbox = meshes[meshInstances[i].meshID]->bvh->Bounds();
			glm::mat4 matrix = meshInstances[i].transform;

			glm::vec3 minBound = bbox.min;
			glm::vec3 maxBound = bbox.max;

			glm::vec3 right       = glm::vec3(matrix[0][0], matrix[0][1], matrix[0][2]);
			glm::vec3 up          = glm::vec3(matrix[1][0], matrix[1][1], matrix[1][2]);
			glm::vec3 forward     = glm::vec3(matrix[2][0], matrix[2][1], matrix[2][2]);
			glm::vec3 translation = glm::vec3(matrix[3][0], matrix[3][1], matrix[3][2]);

			glm::vec3 xa = right * minBound.x;
			glm::vec3 xb = right * maxBound.x;

			glm::vec3 ya = up * minBound.y;
			glm::vec3 yb = up * maxBound.y;

			glm::vec3 za = forward * minBound.z;
			glm::vec3 zb = forward * maxBound.z;

			minBound = glm::min(xa, xb) + glm::min(ya, yb) + glm::min(za, zb) + translation;
			maxBound = glm::max(xa, xb) + glm::max(ya, yb) + glm::max(za, zb) + translation;

			Bounds3D bound;
			bound.min = minBound;
			bound.max = maxBound;

			bounds[i] = bound;
		}
		sceneBvh->Build(&bounds[0], bounds.size());
		sceneBounds = sceneBvh->Bounds();
	}

	void Scene::CreateBLAS()
	{
		// Loop through all meshes and build BVHs
		for (int i = 0; i < meshes.size(); i++)
		{
			printf("Building BVH for %s\n", meshes[i]->meshName.c_str());
			meshes[i]->BuildBVH();
		}
	}
	
	void Scene::RebuildInstancesData()
	{
		if (sceneBvh)
		{
			delete sceneBvh;
			sceneBvh = nullptr;
		}
		
		sceneBvh = new RadeonRays::Bvh(10.0f, 64, false);

		CreateTLAS();
		bvhTranslator.UpdateTLAS(sceneBvh, meshInstances);
		
		//Copy transforms
		for (int i = 0; i < meshInstances.size(); i++) {
			transforms[i] = meshInstances[i].transform;
		}
			
		instancesModified = true;
	}

	void Scene::CreateAccelerationStructures()
	{
		CreateBLAS();

		printf("Building scene BVH\n");
		CreateTLAS();

		// Flatten BVH
		bvhTranslator.Process(sceneBvh, meshes, meshInstances);

		int verticesCnt = 0;

		//Copy mesh data
		for (int i = 0; i < meshes.size(); i++)
		{
			// Copy indices from BVH and not from Mesh
			int numIndices = meshes[i]->bvh->GetNumIndices();
			const int * triIndices = meshes[i]->bvh->GetIndices();

			for (int j = 0; j < numIndices; j++)
			{
				int index = triIndices[j];
				int v1 = (index * 3 + 0) + verticesCnt;
				int v2 = (index * 3 + 1) + verticesCnt;
				int v3 = (index * 3 + 2) + verticesCnt;

				vertIndices.push_back(Indices{ v1, v2, v3 });
			}

			verticesUVX.insert(verticesUVX.end(), meshes[i]->verticesUVX.begin(), meshes[i]->verticesUVX.end());
			normalsUVY.insert(normalsUVY.end(), meshes[i]->normalsUVY.begin(), meshes[i]->normalsUVY.end());

			verticesCnt += meshes[i]->verticesUVX.size();
		}

		// Resize to power of 2
		indicesTexWidth  = (int)(sqrt(vertIndices.size()) + 1); 
		triDataTexWidth  = (int)(sqrt(verticesUVX.size())+ 1); 

		vertIndices.resize(indicesTexWidth * indicesTexWidth);
		verticesUVX.resize(triDataTexWidth * triDataTexWidth);
		normalsUVY.resize(triDataTexWidth * triDataTexWidth);

		for (int i = 0; i < vertIndices.size(); i++)
		{
			vertIndices[i].x = ((vertIndices[i].x % triDataTexWidth) << 12) | (vertIndices[i].x / triDataTexWidth);
			vertIndices[i].y = ((vertIndices[i].y % triDataTexWidth) << 12) | (vertIndices[i].y / triDataTexWidth);
			vertIndices[i].z = ((vertIndices[i].z % triDataTexWidth) << 12) | (vertIndices[i].z / triDataTexWidth);
		}

		//Copy transforms
		transforms.resize(meshInstances.size());
		for (int i = 0; i < meshInstances.size(); i++) {
			transforms[i] = meshInstances[i].transform;
		}
		
		//Copy Textures
		for (int i = 0; i < textures.size(); i++)
		{
			texWidth = textures[i]->width;
			texHeight = textures[i]->height;
			int texSize = texWidth * texHeight;
			textureMapsArray.insert(textureMapsArray.end(), &textures[i]->texData[0], &textures[i]->texData[texSize * 3]);
		}
	}
}