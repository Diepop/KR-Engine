#include "Mesh.hpp"

#include <Slang/MeshPipeline.hpp>
#include <Slang/ShapesPipeline.hpp>

namespace Kaey::Renderer
{
    using enum MeshAttributeDomain;
    using enum MeshAttributeType;
    using enum MeshRotationMode;

    namespace
    {
        constexpr auto AttributeFlags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer;
        constexpr auto     SceneFlags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer;
    }

    SceneData::SceneData(RenderDevice* device) :
        device(device),
        sceneBuffer    (device, 4_mb,     SceneFlags, { .DeviceLocal = true, .HostVisible = true  }),
        attributeBuffer(device, 1_gb, AttributeFlags, { .DeviceLocal = true, .HostVisible = false }),
        sceneAllocator    (&sceneBuffer,      true),
        attributeAllocator(&attributeBuffer, false),
        sceneIndex(sceneAllocator.AllocateIndex32<UniformScene>(1)),
        normalOfFacesPipeline(make_unique<Mesh::NormalOfFacesPipeline>(device)),
        normalOfVerticesPipeline(make_unique<Mesh::NormalOfVerticesPipeline>(device)),
        tangentOfCornersPipeline(make_unique<Mesh::TangentOfCornersPipeline>(device)),
        updateShapePipeline(make_unique<Shapes::UpdateShapePipeline>(device))
    {

    }

    SceneData::~SceneData() = default;

    MeshData2::MeshData2(SceneData* data, u32 pointCount, u32 faceCount, u32 cornerCount) : data(data), pointCount(pointCount), faceCount(faceCount), cornerCount(cornerCount)
    {

    }

    MeshAttribute* MeshData2::FindAttribute(string_view name) const
    {
        if (name.empty())
            return nullptr;
        auto it = rn::find_if(attributes, [&](auto& att) { return att->Name == name; });
        return it != attributes.end() ? it->get() : nullptr;
    }

    MeshAttribute* MeshData2::AddAttribute(string name, MeshAttributeDomain domain, MeshAttributeType type, u32 extraCount)
    {
        assert("Named attribute is already present!" && FindAttribute(name) == nullptr);
        auto count =
            domain == Point  ?  PointCount :
            domain == Face   ?   FaceCount :
            domain == Corner ? CornerCount :
            throw runtime_error("!");
        count += extraCount;
        auto offset =
            type == UInt8   ? data->AttributeAllocator->AllocateIndex32<u8>        (count) :
            type == UInt16  ? data->AttributeAllocator->AllocateIndex32<u16>       (count) :
            type == UInt32  ? data->AttributeAllocator->AllocateIndex32<u32>       (count) :
            type == Float   ? data->AttributeAllocator->AllocateIndex32<f32>       (count) :
            type == Vec2    ? data->AttributeAllocator->AllocateIndex32<Vector2>   (count) :
            type == Vec3    ? data->AttributeAllocator->AllocateIndex32<Vector3>   (count) :
            type == Vec4    ? data->AttributeAllocator->AllocateIndex32<Vector4>   (count) :
            type == Vec2Int ? data->AttributeAllocator->AllocateIndex32<Vector2>   (count) :
            type == Vec3Int ? data->AttributeAllocator->AllocateIndex32<Vector3>   (count) :
            type == Vec4Int ? data->AttributeAllocator->AllocateIndex32<Vector4>   (count) :
            type == Vec2F16 ? data->AttributeAllocator->AllocateIndex32<Vector2F16>(count) :
            type == Vec3F16 ? data->AttributeAllocator->AllocateIndex32<Vector3F16>(count) :
            type == Vec4F16 ? data->AttributeAllocator->AllocateIndex32<Vector4F16>(count) :
            throw runtime_error("!");
        auto size =
            type == UInt8   ? sizeof(u8)         :
            type == UInt16  ? sizeof(u16)        :
            type == UInt32  ? sizeof(u32)        :
            type == Float   ? sizeof(f32)        :
            type == Vec2    ? sizeof(Vector2)    :
            type == Vec3    ? sizeof(Vector3)    :
            type == Vec4    ? sizeof(Vector4)    :
            type == Vec2Int ? sizeof(Vector2)    :
            type == Vec3Int ? sizeof(Vector3)    :
            type == Vec4Int ? sizeof(Vector4)    :
            type == Vec2F16 ? sizeof(Vector2F16) :
            type == Vec3F16 ? sizeof(Vector3F16) :
            type == Vec4F16 ? sizeof(Vector4F16) :
            throw runtime_error("!");
        return attributes.emplace_back(make_unique<MeshAttribute>(move(name), domain, type, offset, vector<u8, GPUAllocator<u8>>(size * count, {}, { Device }), size)).get();
    }

    MeshAttribute* MeshData2::AddAttributeMorphs(MeshAttribute* at, u32 shapeCount)
    {
        assert(at->Morphs.Attribute == nullptr);
        assert(rn::contains(Attributes, at));
        auto count =
            at->Domain == Point  ? PointCount  :
            at->Domain == Face   ? FaceCount   :
            at->Domain == Corner ? CornerCount :
            throw runtime_error("!");
        auto res = AddAttribute({}, at->Domain, at->Type, count * shapeCount);
        at->Morphs.Attribute = res;
        at->Morphs.ValuesIndexOffset = Data->AttributeAllocator->AllocateIndex32<f32>(shapeCount);
        at->Morphs.Values.resize(shapeCount, { {}, 0, 0, 1 });
        return res;
    }

    KR_GETTER_DEF(MeshData3D, FaceIndices)
    {
        if (faceIndices)
            return faceIndices;

        auto facesOfVertices = vector<vector<u32>>(PointCount + 1); //One more to get the count of the last vertex.
        auto emplaceCount = u32(0);
        auto createCorners = [&](auto&& pointsOfCorner)
        {
            for (auto faceId : irange(FaceCount))
            for (auto cornerId : vs::iota(CornerPerFace * faceId) | vs::take(CornerPerFace))
            {
                auto vertexId = pointsOfCorner[cornerId];
                facesOfVertices[vertexId].emplace_back(faceId);
                ++emplaceCount;
            }
        };

        if (PointOfCorner->Type == UInt32)
            createCorners(PointsOfCorners32);
        else createCorners(PointsOfCorners16);

        auto faceIndexType = FaceCount <= UINT16_MAX ? UInt16 : UInt32;
        faceIndices = const_cast<MeshData3D*>(this)->AddAttribute("FaceIndex", Point, faceIndexType, emplaceCount - PointCount);

        auto createFaceIndices = [&]<class Int>(Int* faceIndexIt)
        {
            auto faceIt = FaceLists.data();
            auto  index = u32(0);
            for (auto [vertexId, faces] : facesOfVertices | uindexed32)
            {
                for (auto faceId : faces)
                    *faceIndexIt++ = (Int)faceId;
                *faceIt++ = index;
                index += u32(faces.size());
            }
        };

        if (FaceCount <= UINT16_MAX)
            createFaceIndices((u16*)faceIndices->Buffer.data());
        else createFaceIndices((u32*)faceIndices->Buffer.data());

        Uniform->FaceIndexOfPointOffset = faceIndices->IndexOffset;

        return faceIndices;
    }

    MeshData3D::MeshData3D(SceneData* data, string name, u32 pointCount, u32 faceCount, u32 cornerCount) :
        MeshData2(data, pointCount, faceCount, cornerCount),
        name(move(name)),
        meshIndex(data->SceneAllocator->AllocateIndex32<UniformMesh>(1)),
        faceIndices(nullptr)
    {
        auto pointIndexType = PointCount <= UINT16_MAX ? UInt16 : UInt32;
        AddAttribute("PointOfCorner", Corner,           pointIndexType, 0);
        AddAttribute("NormalOfFace",  Face,  Vec3F16, 0);
        AddAttribute("Position",      Point, Vec3,    0);
        AddAttribute("Normal",        Point, Vec3F16, 0);
        AddAttribute("FaceList",      Point, UInt32,  1);

        Uniform->PointCount             = PointCount;
        Uniform->FaceCount              = FaceCount;
        Uniform->CornerCount            = CornerCount;
        Uniform->PointOfCornerOffset    = PointOfCorner->IndexOffset;
        Uniform->PositionOffset         = Position->IndexOffset;
        Uniform->NormalOffset           = Normal->IndexOffset;
        Uniform->NormalOfFaceOffset     = NormalOfFace->IndexOffset;
        Uniform->FaceOfPointOffset      = FaceList->IndexOffset;
        //Uniform->FaceIndexOfPointOffset = FaceIndices->IndexOffset;
        Uniform->UseU32Indices          = PointOfCorner->Type == UInt32;
        Uniform->UseF32Normals          = false;
        Uniform->UvOffset               = u32(-1);
    }

    void MeshData3D::Write(Frame* frame)
    {
        Device->ExecuteSingleTimeCommands(frame, [this](Frame* fr)
        {
            auto writer = fr->NewObject<BufferQueue>(Device);
            (void)GetFaceIndices();
            for (auto& attribute : Attributes)
                writer->QueueWrite(Data->AttributeBuffer, attribute->IndexOffset * attribute->ElementSize, attribute->Buffer);
            writer->Execute(fr);
        });
    }

    void MeshData3D::CalcMorphs(Frame* frame)
    {
        if (Position->Morphs.Values.empty())
            return;
        Device->ExecuteSingleTimeCommands(frame, [this](Frame* fr)
        {
            auto writer = fr->NewObject<BufferQueue>(Device);
            auto vals = Position->Morphs.Values | vs::member_of(&MeshMorph::Value) | to_vector;
            writer->QueueWrite((TypedMemoryBuffer<f32>*)Data->AttributeBuffer, Position->Morphs.ValuesIndexOffset, vals);
            writer->Execute(fr);
            fr->WaitForCommands();
            auto sp = Data->UpdateShapePipeline;
            sp->PushConstantValue =
            {
                .PositionOffset = Position->IndexOffset,
                .PointCount     = PointCount,
                .ShapeOffset    = Position->Morphs.Attribute->IndexOffset,
                .ShapeCount     = (u32)Position->Morphs.Values.size(),
                .DeltaOffset    = Position->Morphs.ValuesIndexOffset,
            };
            sp->Params.Binding1 = Data->AttributeBuffer;
            sp->Compute({ PointCount }, fr);
        });
    }

    void MeshData3D::CalcFaceNormals(Frame* frame)
    {
        Device->ExecuteSingleTimeCommands(frame, [this](Frame* fr)
        {
            auto nfp = Data->NormalOfFacesPipeline;
            nfp->PushConstantValue =
            {
                .MeshIndex = MeshIndex,
                .CornerPerFace = CornerPerFace,
            };
            nfp->Params.Binding0 = Data->SceneBuffer;
            nfp->Params.Binding1 = Data->AttributeBuffer;
            nfp->Compute({ FaceCount }, fr);
        });
    }

    void MeshData3D::CalcPointNormals(Frame* frame)
    {
        Device->ExecuteSingleTimeCommands(frame, [this](Frame* fr)
        {
            auto nvp = Data->NormalOfVerticesPipeline;
            nvp->PushConstantValue =
            {
                .MeshIndex = MeshIndex,
                .CornerPerFace = CornerPerFace,
            };
            nvp->Params.Binding0 = Data->SceneBuffer;
            nvp->Params.Binding1 = Data->AttributeBuffer;
            nvp->Compute({ PointCount }, fr);
        });
    }

    void MeshData3D::CalcUvTangents(Frame* frame)
    {
        if (Uvs.empty())
            return;
        Device->ExecuteSingleTimeCommands(frame, [this](Frame* fr)
        {
            auto tgp = Data->TangentOfCornersPipeline;
            tgp->PushConstantValue =
            {
                .MeshIndex = MeshIndex,
                .CornerPerFace = CornerPerFace,
            };
            tgp->Params.Binding0 = Data->SceneBuffer;
            tgp->Params.Binding1 = Data->AttributeBuffer;
            tgp->Compute({ CornerCount }, fr);
        });
    }

    MeshAttribute* MeshData3D::AddUvMap(string name)
    {
        auto uv = AddAttribute(move(name), Corner, Vec2F16);
        auto tg = AddAttribute("TangentOf{}"_f(uv->Name), Corner, Vec3F16);
        if (uvMaps.empty())
        {
            Uniform->UvOffset = uv->IndexOffset;
            Uniform->TangentOffset = tg->IndexOffset;
        }
        uvMaps.emplace_back(uv);
        return uv;
    }

    LoadedScene LoadSceneFile(SceneData* sceneData, crpath path)
    {
        using namespace std::string_view_literals;
        auto sf = SceneFile::Load(path);
        auto meshes = sf.Meshes | vs::transform([&](MeshFile& mf) -> unique_ptr<MeshData3D>
        {
            if (mf.FaceCount == 0)
                return nullptr;
            assert("Invalid Mixed Topology!" && mf.CornerCount % mf.FaceCount == 0);
            auto mesh = make_unique<MeshData3D>(sceneData, mf.Name, mf.PointCount, mf.FaceCount, mf.CornerCount);

            auto pos = rn::find_if(mf.Attributes, [](auto& at) { return at.Name == "position"sv; });
            rn::copy(pos->Buffer, (u8*)mesh->Positions.data());

            if (auto count = pos->Morphs.size(); count > 1)
            {
                auto mp = mesh->Position;
                auto mAtt = mesh->AddAttributeMorphs(mp, (u32)pos->Morphs.size());
                for (auto it = mAtt->Buffer.data(); auto [i, morph] : pos->Morphs | indexed)
                {
                    auto& m = mp->Morphs.Values[i];
                    m.Name  = morph.Name;
                    m.Value = morph.Value;
                    m.Min   = morph.Min;
                    m.Max   = morph.Max;
                    it = rn::copy(morph.Buffer, it).out;
                }
            }

            auto cvs = rn::find_if(mf.Attributes, [](auto& at) { return at.Name == ".corner_vert"sv; });
            rn::copy(cvs->Buffer, (u8*)mesh->PointsOfCorners32.data());

            for (auto& uvIndex : mf.UvIndices)
            {
                auto& uv = mf.Attributes[uvIndex];
                assert(uv.Type == Vec2F16);
                auto uvAtt = mesh->AddUvMap(uv.Name);
                auto uvValues = span{ (Vector2F16*)uv.Buffer.data(), mf.CornerCount };
                rn::copy(uvValues, (Vector2F16*)uvAtt->Buffer.data());
            }

            mesh->MaterialRanges = mf.Materials | vs::transform([](auto r) { return pair(r.Offset, r.Count); }) | to_vector;

            return mesh;
        }) | to_vector;
        return { move(meshes), move(sf.Objects), move(sf.Collection), };
    }

}
