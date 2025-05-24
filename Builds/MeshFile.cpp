#include "MeshFile.hpp"

namespace Kaey::Renderer
{
    using enum MeshAttributeType;

    u32 ByteSizeOfAttribute(MeshAttributeType type)
    {
        switch (type)
        {
        case UInt8:   return sizeof(u8);
        case UInt16:  return sizeof(u16);
        case UInt32:  return sizeof(u32);
        case Float:   return sizeof(f32);
        case Vec2:    return sizeof(Vector2);
        case Vec3:    return sizeof(Vector3);
        case Vec4:    return sizeof(Vector4);
        case Vec2Int: return sizeof(Vector2I32);
        case Vec3Int: return sizeof(Vector3I32);
        case Vec4Int: return sizeof(Vector4I32);
        case Boolean: return sizeof(bool);
        case Vec2F16: return sizeof(Vector2F16);
        case Vec3F16: return sizeof(Vector3F16);
        case Vec4F16: return sizeof(Vector4F16);
        default: throw invalid_argument("Invalid value for 'type': {}"_f((u32)type));
        }
    }

}

namespace Kaey::Renderer
{
    namespace
    {
        constexpr u32  MeshFileMagic = 'K' << 0u | 'M' << 8u | 'F' << 16u | '\0' << 24u;
        constexpr u32 SceneFileMagic = 'K' << 0u | 'S' << 8u | 'C' << 16u | '\0' << 24u;
    }

    void MeshFile::Save(crpath path) const
    {
        auto os = ofstream(path, std::ios::ate | std::ios::binary);
        if (!os.is_open())
            return;
        Serialize(os,
            MeshFileMagic,
            *this
        );
    }

    MeshFile MeshFile::Load(crpath path)
    {
        auto is = ifstream(path, std::ios::binary);
        if (!is.is_open())
            throw std::system_error(errno, std::generic_category(), path.string());
        u32 magic;
        UnSerialize(is, magic);
        if (magic != MeshFileMagic)
            throw runtime_error("Invalid magic in file: expected '{:x}', found '{:x}'"_f(MeshFileMagic, magic));
        MeshFile mf;
        UnSerialize(is, mf);
        return mf;
    }

    void SceneFile::Save(crpath path) const
    {
        auto os = ofstream(path, std::ios::ate | std::ios::binary);
        if (!os.is_open())
            return;
        Serialize(os, SceneFileMagic);
        Serialize(os, *this);
    }

    SceneFile SceneFile::Load(crpath path)
    {
        auto is = ifstream(path, std::ios::binary);
        if (!is.is_open())
            throw std::system_error(errno, std::generic_category(), path.string());
        u32 magic;
        UnSerialize(is, magic);
        if (magic != SceneFileMagic)
            throw runtime_error("Invalid magic in file: expected '{:x}', found '{:x}'"_f(SceneFileMagic, magic));
        SceneFile sf;
        UnSerialize(is, sf);
        return sf;
    }

}
