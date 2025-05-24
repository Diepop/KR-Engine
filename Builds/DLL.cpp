#include "MeshFile.hpp"

using namespace Kaey::Renderer;
using namespace Kaey;

using namespace std::string_view_literals;

struct EnumFormatter
{
    std::formatter<string_view> form;

    constexpr auto parse(auto& ctx)
    {
        return form.parse(ctx);
    }

    auto format(auto e, auto& ctx) const
    {
        form.format(magic_enum::enum_name(e), ctx);
        return ctx.out();
    }

};

namespace std
{
    template<> struct formatter<MeshAttributeDomain> : EnumFormatter{};
    template<> struct formatter<MeshAttributeType>   : EnumFormatter{};
}

using enum MeshAttributeDomain;
using enum MeshAttributeType;
using enum MeshRotationMode;

namespace
{
    const unordered_map<string_view, MeshAttributeDomain> DomainMap
    {
        { "POINT",  Point  },
        { "EDGE",   Edge   },
        { "FACE",   Face   },
        { "CORNER", Corner },
    };

    const unordered_map<string_view, MeshAttributeType> TypeMap
    {
        { "BOOLEAN",      Boolean },
        { "INT",          UInt32  },
        { "INT32_2D",     Vec2Int },
        { "FLOAT",        Float   },
        { "FLOAT2",       Vec2    },
        { "FLOAT_VECTOR", Vec3    },
        { "BYTE_COLOR",   UInt32  },
    };

    const unordered_map<string_view, MeshRotationMode> RotationModeMap
    {
        { "QUATERNION", Quat },
        { "XYZ",        XYZ  },
        { "XZY",        XZY  },
        { "YXZ",        YXZ  },
        { "YZX",        YZX  },
        { "ZXY",        ZXY  },
        { "ZYX",        ZYX  },
    };

    void ReorderMeshFaces(MeshFile* mf)
    {
        if (mf->Materials.size() <= 1)
            return;
        auto it = rn::find_if(mf->Attributes, [&](MeshFileAttribute& at) { return at.Name == "material_index"; });
        if (it == mf->Attributes.end())
            return;
        auto ids = span((u32*)it->Buffer.data(), mf->FaceCount);
        map<u32, vector<pair<size_t, size_t>>> matIdRanges;
        for (size_t i = 0; i < ids.size();)
        {
            auto id = ids[i];
            auto& [offset, count] = matIdRanges[id].emplace_back(i, 0);
            do ++count;
            while (i + count < ids.size() && ids[i + count] == id);
            i += count;
        }
        auto cornerPerFace = mf->CornerCount / mf->FaceCount;
        vector<u8> atData;
        for (auto& attribute : mf->Attributes) if (attribute.Domain == Corner || attribute.Domain == Face)
        {
            auto byteSize = ByteSizeOfAttribute(attribute.Type) * (attribute.Domain == Corner ? cornerPerFace : 1);
            atData.clear();
            atData.reserve(attribute.Buffer.size());
            for (auto& ranges : matIdRanges | vs::values)
            for (auto& [offset, count] : ranges)
            {
                auto v = span(attribute.Buffer.data() + offset * byteSize, count * byteSize);
                atData.insert_range(atData.end(), v);
            }
            rn::copy(atData, attribute.Buffer.data());
        }
        for (u32 offset = 0; auto [i, ranges] : matIdRanges | vs::values | uindexed32)
        {
            auto& r = mf->Materials[i];
            r.Offset = offset;
            r.Count = 0;
            for (auto& count : ranges | vs::values)
                r.Count += count;
            offset += r.Count;
        }
    }

}

extern "C"
{
    MeshAttributeDomain AttributeDomain(const char* n)
    {
        assert(DomainMap.contains(n));
        return DomainMap.at(n);
    }

    MeshAttributeType AttributeType(const char* n)
    {
        assert(TypeMap.contains(n));
        return TypeMap.at(n);
    }

    MeshRotationMode RotationMode(const char* n)
    {
        assert(RotationModeMap.contains(n));
        return RotationModeMap.at(n);
    }

    struct ObjectRotation
    {
        Quaternion RotationQuat;
        Vector3 Rotation;
        MeshRotationMode Mode;
    };

    MeshFileMaterial* CreateMaterial(const char* name)
    {
        return new MeshFileMaterial{ .Name = name };
    }

    void DestroyMaterial(MeshFileMaterial* mat)
    {
        delete mat;
    }

    ObjectInstance* CreateObject(const char* name, u32 dataIndex, const Vector3& location, const ObjectRotation& rotation, const Vector3& scale)
    {
        auto loc = location;
        swap(loc.y, loc.z); //We use y as up, while blender uses z as up.
        auto [quat, euler, mode] = rotation;
        swap(euler.y, euler.z); //We use y as up, while blender uses z as up.
        euler = -euler; //Blender uses counter clockwise rotation, we use clockwise.
        switch (mode)
        {
        case Quat: euler = quat.EulerAngle; break;
        case XYZ: break;
        case XZY: euler = { euler.x, euler.z, euler.y }; break;
        case YXZ: euler = { euler.y, euler.x, euler.z }; break;
        case YZX: euler = { euler.y, euler.z, euler.x }; break;
        case ZXY: euler = { euler.z, euler.x, euler.y }; break;
        case ZYX: euler = { euler.z, euler.y, euler.x }; break;
        }
        if (mode != Quat)
            quat = Quaternion::EulerAngles(euler);
        return new ObjectInstance
        {
            .Name            = name,
            .DataIndex       = dataIndex,
            .Location        = loc,
            .RotationMode    = mode,
            .Rotation        = euler,
            .RotationQuat    = quat,
            .Scale           = scale,
            .ViewportDisplay = {},
        };
    }

    void DestroyObject(ObjectInstance* obj)
    {
        delete obj;
    }

    Collection* CreateCollection(const char* name)
    {
        return new Collection{ .Name = name, .Children = {}, .ObjectIds = {}, .Flags = {}, };
    }

    void CollectionAddChild(Collection* col, Collection* child)
    {
        col->Children.emplace_back().reset(child);
    }

    void CollectionAddObject(Collection* col, u32 objId)
    {
        col->ObjectIds.emplace_back(objId);
    }

    void CollectionSetViewLayerEnabled(Collection* col, bool v)
    {
        col->ViewLayerEnabled = v;
    }

    void CollectionSetSelectionEnabled(Collection* col, bool v)
    {
        col->SelectionEnabled = v;
    }

    void CollectionSetViewportEnabled(Collection* col, bool v)
    {
        col->ViewportEnabled = v;
    }

    void CollectionSetRenderEnabled(Collection* col, bool v)
    {
        col->RenderEnabled = v;
    }

    void DestroyCollection(Collection* col)
    {
        delete col;
    }

    MeshFile* CreateMeshFile(const char* name, u32 pointCount, u32 edgeCount, u32 faceCount, u32 cornerCount)
    {
        return new MeshFile{ name, pointCount, edgeCount, faceCount, cornerCount, {} };
    }

    void MeshFileAddAttribute(MeshFile* mf, const char* name, MeshAttributeDomain domain, MeshAttributeType type, const u8* buffer, bool isUv)
    {
        if (isUv && type != Vec2F16)
        {
            assert(domain == Corner);
            assert(type == Vec2);
            auto uvValues = span((Vector2*)buffer, mf->CornerCount) | vs::transform([](Vector2 v)
            {
                float y;
                v.y = 1 - std::modf(v.y, &y);
                v.y += y;
                return (Vector2F16)v;
            }) | to_vector;
            return MeshFileAddAttribute(mf, name, domain, Vec2F16, (u8*)uvValues.data(), true);
        }
        if (type != UInt16 && name == ".corner_vert"sv && mf->PointCount <= std::numeric_limits<u16>::max()) //Use u16 indices instead.
        {
            assert(domain == Corner);
            auto u16Indices = span((u32*)buffer, mf->CornerCount) | rn::to<vector<u16>>();
            return MeshFileAddAttribute(mf, name, domain, UInt16, (u8*)u16Indices.data(), false);
        }
        auto count = [&]
        {
            switch (domain)
            {
            case Point:  return  mf->PointCount;
            case Edge:   return   mf->EdgeCount;
            case Face:   return   mf->FaceCount;
            case Corner: return mf->CornerCount;
            default: throw invalid_argument("Invalid value for 'domain': {}"_f((u32)domain));
            }
        }();
        auto size = ByteSizeOfAttribute(type);
        auto vec = vector(buffer, buffer + size * count);
        switch (type)
        {
        case Vec3:
        {
            for (auto& v : span((Vector3*)vec.data(), count))
                std::swap(v.y, v.z);
        }break;
        }
        if (isUv)
            mf->UvIndices.emplace_back((u32)mf->Attributes.size());
        mf->Attributes.emplace_back(name, domain, type, move(vec));
    }

    void MeshFileAddShapeKey(MeshFile* mf, const char* name, const char* relativeName, f32 value, f32 min, f32 max, const u8* buffer)
    {
        auto it = rn::find_if(mf->Attributes, [&](auto& at) { return at.Name == "position"; });
        assert(it != mf->Attributes.end());
        auto vec = span(buffer, mf->PointCount * sizeof(Vector3)) | to_vector;
        it->Morphs.emplace_back(name, relativeName, value, min, max, move(vec));
    }

    void MeshFileAddMaterial(MeshFile* mf, u32 id)
    {
        mf->Materials.emplace_back(id, 0, mf->FaceCount);
    }

    void MeshFileSave(MeshFile* mf, const char* path)
    {
        return mf->Save(path);
    }

    void DestroyMeshFile(MeshFile* mf)
    {
        delete mf;
    }

    SceneFile* CreateSceneFile()
    {
        return new SceneFile();
    }

    void SceneFileAddMesh(SceneFile* scene, MeshFile* mf)
    {
        ReorderMeshFaces(mf);
        scene->Meshes.emplace_back(move(*mf));
        delete mf;
    }

    void SceneFileAddObject(SceneFile* scene, ObjectInstance* obj)
    {
        scene->Objects.emplace_back(move(*obj));
        delete obj;
    }

    void SceneFileAddMaterial(SceneFile* scene, MeshFileMaterial* mat)
    {
        scene->Materials.emplace_back(move(*mat));
        delete mat;
    }

    void SceneFileSetColletion(SceneFile* scene, Collection* col)
    {
        scene->Collection.reset(col);
    }

    void SceneFileSave(SceneFile* scene, const char* path)
    {
        scene->Save(path);
    }

    void DestroySceneFile(SceneFile* scene)
    {
        delete scene;
    }

}
