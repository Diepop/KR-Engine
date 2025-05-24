#pragma once
#include "Kaey/Renderer/Renderer.hpp"

#include <Slang/TestPipeline.hpp>

#include "MeshFile.hpp"

namespace Kaey::Renderer
{
    using UniformScene    = Slang::TestPipeline::UniformScene;
    using UniformMesh     = Slang::TestPipeline::UniformMesh;
    using UniformMaterial = Slang::TestPipeline::UniformMaterial;

    class SceneData
    {
        RenderDevice* device;
        mutable MemoryBuffer sceneBuffer;
        mutable MemoryBuffer attributeBuffer;

        mutable GPUVirtualMemoryAllocator sceneAllocator;
        mutable GPUVirtualMemoryAllocator attributeAllocator;

        u32 sceneIndex;

        unique_ptr<Mesh::NormalOfFacesPipeline> normalOfFacesPipeline;
        unique_ptr<Mesh::NormalOfVerticesPipeline> normalOfVerticesPipeline;
        unique_ptr<Mesh::TangentOfCornersPipeline> tangentOfCornersPipeline;
        unique_ptr<Shapes::UpdateShapePipeline> updateShapePipeline;

    public:
        explicit SceneData(RenderDevice* device);

        KR_NO_COPY_MOVE(SceneData);

        ~SceneData();

        KR_GETTER(RenderDevice*, Device) { return device; }

        KR_GETTER(MemoryBuffer*, SceneBuffer) { return &sceneBuffer; }
        KR_GETTER(MemoryBuffer*, AttributeBuffer) { return &attributeBuffer; }

        KR_GETTER(GPUVirtualMemoryAllocator*, SceneAllocator) { return &sceneAllocator; }
        KR_GETTER(GPUVirtualMemoryAllocator*, AttributeAllocator) { return &attributeAllocator; }

        KR_GETTER(u32, Index) { return sceneIndex; }
        KR_GETTER(UniformScene*, Data) { return (UniformScene*)sceneAllocator.MappedAddress + Index; }

        KR_GETTER(Mesh::NormalOfFacesPipeline*,       NormalOfFacesPipeline) { return    normalOfFacesPipeline.get(); }
        KR_GETTER(Mesh::NormalOfVerticesPipeline*, NormalOfVerticesPipeline) { return normalOfVerticesPipeline.get(); }
        KR_GETTER(Mesh::TangentOfCornersPipeline*, TangentOfCornersPipeline) { return tangentOfCornersPipeline.get(); }
        
        KR_GETTER(Shapes::UpdateShapePipeline*,         UpdateShapePipeline) { return      updateShapePipeline.get(); }

    };

    struct MeshMorph
    {
        string Name;
        f32 Value, Min, Max;
    };

    struct MeshAttribute
    {
        string Name;
        MeshAttributeDomain Domain;
        MeshAttributeType Type;
        u32 IndexOffset;
        vector<u8, GPUAllocator<u8>> Buffer;
        size_t ElementSize;
        struct MeshAttributeMorphs
        {
            u32 ValuesIndexOffset;
            vector<MeshMorph> Values;
            MeshAttribute* Attribute = nullptr;
        }Morphs;
    };

    class MeshData2
    {
        SceneData* data;
        u32 pointCount;
        u32 faceCount;
        u32 cornerCount;

        vector<unique_ptr<MeshAttribute>> attributes;

    public:
        MeshData2(SceneData* data, u32 pointCount, u32 faceCount, u32 cornerCount);

        MeshAttribute* FindAttribute(string_view name) const;

        MeshAttribute* AddAttribute(string name, MeshAttributeDomain domain, MeshAttributeType type, u32 extraCount = 0);

        MeshAttribute* AddAttributeMorphs(MeshAttribute* at, u32 shapeCount);

        KR_GETTER(SceneData*, Data) { return data; }
        KR_GETTER(RenderDevice*, Device) { return Data->Device; }
        KR_GETTER(cspan<MeshAttribute*>, Attributes) { return { (MeshAttribute**)attributes.data(), attributes.size() }; }

        KR_GETTER(u32, PointCount)  { return  pointCount; }
        KR_GETTER(u32, FaceCount)   { return   faceCount; }
        KR_GETTER(u32, CornerCount) { return cornerCount; }

        KR_GETTER(u32, CornerPerFace) { return CornerCount / FaceCount; }

        vector<pair<u32, u32>> MaterialRanges;

    };

    class MeshData3D : public MeshData2
    {
        string name;
        u32 meshIndex;

    protected:
        mutable const MeshAttribute* faceIndices;

        KR_GETTER(const MeshAttribute*, FaceIndices);

        vector<MeshAttribute*> uvMaps;

    public:

        MeshData3D(SceneData* data, string name, u32 pointCount, u32 faceCount, u32 cornerCount);

        KR_NO_COPY_MOVE(MeshData3D);

        void Write(Frame* frame = nullptr);

        void CalcMorphs(Frame* frame = nullptr);

        void CalcFaceNormals(Frame* frame = nullptr);

        void CalcPointNormals(Frame* frame = nullptr);

        void CalcUvTangents(Frame* frame = nullptr);

        MeshAttribute* AddUvMap(string name);

        KR_GETTER(string_view, Name) { return name; }
        KR_GETTER(u32, MeshIndex) { return meshIndex; }
        KR_GETTER(UniformMesh*, Uniform) { return (UniformMesh*)Data->SceneAllocator->MappedAddress + MeshIndex; }

        KR_GETTER(MeshAttribute*, PointOfCorner) { return Attributes[0]; }
        KR_GETTER(MeshAttribute*, NormalOfFace)  { return Attributes[1]; }
        KR_GETTER(MeshAttribute*, Position)      { return Attributes[2]; }
        KR_GETTER(MeshAttribute*, Normal)        { return Attributes[3]; }
        KR_GETTER(MeshAttribute*, FaceList)      { return Attributes[4]; }

        KR_GETTER(cspan<MeshAttribute*>, Uvs)    { return uvMaps; }

        KR_GETTER(span<u16>,        PointsOfCorners16) { return {        (u16*)PointOfCorner->Buffer.data(), CornerCount }; }
        KR_GETTER(span<u32>,        PointsOfCorners32) { return {        (u32*)PointOfCorner->Buffer.data(), CornerCount }; }
        KR_GETTER(span<Vector3>,    NormalsOfFaces)    { return {    (Vector3*) NormalOfFace->Buffer.data(),   FaceCount }; }
        KR_GETTER(span<Vector3>,    Positions)         { return {    (Vector3*)     Position->Buffer.data(),  PointCount }; }
        KR_GETTER(span<Vector3F16>, Normals)           { return { (Vector3F16*)       Normal->Buffer.data(),  PointCount }; }
        KR_GETTER(span<u32>,        FaceLists)         { return {        (u32*)     FaceList->Buffer.data(),  PointCount }; }

    };

    struct LoadedScene
    {
        vector<unique_ptr<MeshData3D>> MeshDatas;
        vector<ObjectInstance> Objects;
        unique_ptr<Collection> Collection;
    };

    LoadedScene LoadSceneFile(SceneData* sceneData, crpath path);

}
