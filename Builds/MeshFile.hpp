#pragma once
#include "Kaey/Renderer/Utility.hpp"

namespace Kaey::Renderer
{
    enum class MeshAttributeDomain : u8
    {
        Point,
        Edge,
        Face,
        Corner,
    };

    enum class MeshAttributeType : u8
    {
        Boolean,
        UInt8,
        UInt16,
        UInt32,
        Float,
        Vec2,
        Vec3,
        Vec4,
        Vec2Int,
        Vec3Int,
        Vec4Int,
        Vec2F16,
        Vec3F16,
        Vec4F16,
    };

    enum class MeshRotationMode : u8
    {
        Quat,
        XYZ,
        XZY,
        YXZ,
        YZX,
        ZXY,
        ZYX,
    };

    u32 ByteSizeOfAttribute(MeshAttributeType type);

}

namespace Kaey::Renderer
{
    struct MeshFileMaterial
    {
        string Name;
    };

    struct MeshFileMaterialRange
    {
        u32 MaterialIndex;
        u32 Offset;
        u32 Count;
    };

    struct MeshFileMorph
    {
        string Name;
        string BaseName;
        f32 Value, Min, Max;
        vector<u8> Buffer;
    };

    struct MeshFileAttribute
    {
        string Name;
        MeshAttributeDomain Domain;
        MeshAttributeType Type;
        vector<u8> Buffer;
        vector<MeshFileMorph> Morphs;
    };

    struct MeshFile
    {
        string     Name;
        u32  PointCount;
        u32   EdgeCount;
        u32   FaceCount;
        u32 CornerCount;
        vector<MeshFileAttribute> Attributes;
        vector<u32> UvIndices;
        vector<MeshFileMaterialRange> Materials;
        void Save(crpath path) const;
        static MeshFile Load(crpath path);
    };

    enum class ObjectType
    {
        Mesh,
    };

    struct ObjectInstance
    {
        string Name;
        u32 DataIndex;
        Vector3 Location;
        MeshRotationMode RotationMode;
        Vector3 Rotation;
        Quaternion RotationQuat;
        Vector3 Scale;
        struct
        {
            u32 Flags;
            KR_BITFIELD_PROP(Name, Flags, 0);
            KR_BITFIELD_PROP(Axes, Flags, 0);
            KR_BITFIELD_PROP(Wireframe, Flags, 0);
            KR_BITFIELD_PROP(AllEdges, Flags, 0);
            KR_BITFIELD_PROP(TextureSpace, Flags, 0);
            KR_BITFIELD_PROP(Shadow, Flags, 0);
            KR_BITFIELD_PROP(InFront, Flags, 0);
        }ViewportDisplay;
    };

    struct Collection
    {
        string Name;
        vector<unique_ptr<Collection>> Children;
        vector<u32> ObjectIds;

        u32 Flags;
        KR_BITFIELD_PROP(ViewLayerEnabled, Flags, 0);
        KR_BITFIELD_PROP(SelectionEnabled, Flags, 1);
        KR_BITFIELD_PROP(ViewportEnabled,  Flags, 2);
        KR_BITFIELD_PROP(RenderEnabled,    Flags, 3);
    };

    struct SceneFile
    {
        vector<MeshFile> Meshes;
        vector<ObjectInstance> Objects;
        vector<MeshFileMaterial> Materials;
        unique_ptr<Collection> Collection;
        void Save(crpath path) const;
        static SceneFile Load(crpath path);
    };

}

namespace Kaey
{
    namespace detail
    {
        struct UniversalType
        {
            template<class T>
            operator T() { return {}; }
        };

        template<class T>
        consteval auto MemberCount(auto... args)
        {
            if constexpr (requires { T{ args... }; } == false)
                return sizeof...(args) - 1;
            else return MemberCount<T>(args..., UniversalType{});
        }

        auto ToTuple(auto& v)
        {
            using Ty = std::remove_cvref_t<decltype(v)>;
            static_assert(MemberCount<Ty>() <= 20, "More than 20 elements is not supported!");
            if constexpr (MemberCount<Ty>() == 1)
            {
                auto& [e1] = v;
                return std::tie(e1);
            }
            else if constexpr (MemberCount<Ty>() == 2)
            {
                auto& [e1, e2] = v;
                return std::tie(e1, e2);
            }
            else if constexpr (MemberCount<Ty>() == 3)
            {
                auto& [e1, e2, e3] = v;
                return std::tie(e1, e2, e3);
            }
            else if constexpr (MemberCount<Ty>() == 4)
            {
                auto& [e1, e2, e3, e4] = v;
                return std::tie(e1, e2, e3, e4);
            }
            else if constexpr (MemberCount<Ty>() == 5)
            {
                auto& [e1, e2, e3, e4, e5] = v;
                return std::tie(e1, e2, e3, e4, e5);
            }
            else if constexpr (MemberCount<Ty>() == 6)
            {
                auto& [e1, e2, e3, e4, e5, e6] = v;
                return std::tie(e1, e2, e3, e4, e5, e6);
            }
            else if constexpr (MemberCount<Ty>() == 7)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7);
            }
            else if constexpr (MemberCount<Ty>() == 8)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8);
            }
            else if constexpr (MemberCount<Ty>() == 9)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8, e9] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8, e9);
            }
            else if constexpr (MemberCount<Ty>() == 10)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8, e9, e10] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10);
            }
            else if constexpr (MemberCount<Ty>() == 11)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11);
            }
            else if constexpr (MemberCount<Ty>() == 12)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12);
            }
            else if constexpr (MemberCount<Ty>() == 13)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13);
            }
            else if constexpr (MemberCount<Ty>() == 14)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14);
            }
            else if constexpr (MemberCount<Ty>() == 15)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15);
            }
            else if constexpr (MemberCount<Ty>() == 16)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15, e16] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15, e16);
            }
            else if constexpr (MemberCount<Ty>() == 17)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15, e16, e17] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15, e16, e17);
            }
            else if constexpr (MemberCount<Ty>() == 18)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15, e16, e17, e18] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15, e16, e17, e18);
            }
            else if constexpr (MemberCount<Ty>() == 19)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15, e16, e17, e18, e19] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15, e16, e17, e18, e19);
            }
            else if constexpr (MemberCount<Ty>() == 20)
            {
                auto& [e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15, e16, e17, e18, e19, e20] = v;
                return std::tie(e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, e12, e13, e14, e15, e16, e17, e18, e19, e20);
            }
        }

    }

    template<class T>
    struct Serializer
    {
        static_assert(!std::is_pointer_v<T>, "Type T must not be a pointer!");
        void Serialize(ostream& stream, const T& value) const;
        void UnSerialize(istream& stream, T& value) const;
    };

    template<class... Args>
    void Serialize(ostream& stream, const Args&... args)
    {
        (Serializer<Args>{ }.Serialize(stream, args), ...);
    }

    template<class... Args>
        requires (!std::is_const_v<Args> && ...)
    void UnSerialize(istream& stream, Args&... args)
    {
        (Serializer<Args>{ }.UnSerialize(stream, args), ...);
    }

    template <class T>
    void Serializer<T>::Serialize(ostream& stream, const T& value) const
    {
        if constexpr (std::is_trivially_copyable_v<T>)
            stream.write((const char*)&value, sizeof(T));
        else Kaey::Serialize(stream, detail::ToTuple(value));
    }

    template <class T>
    void Serializer<T>::UnSerialize(istream& stream, T& value) const
    {
        if constexpr (std::is_trivially_copyable_v<T>)
            stream.read((char*)&value, sizeof(T));
        else
        {
            auto tied = detail::ToTuple(value);
            Kaey::UnSerialize(stream, tied);
        }
    }

    template<class T, class A>
    struct Serializer<vector<T, A>>
    {
        Serializer<T> ElementSerializer;

        void Serialize(ostream& stream, const vector<T, A>& v)
        {
            Kaey::Serialize(stream, v.size());
            for (auto& e : v)
                ElementSerializer.Serialize(stream, e);
        }

        void UnSerialize(istream& stream, vector<T, A>& v)
        {
            size_t size;
            Kaey::UnSerialize(stream, size);
            v.resize(size);
            if constexpr (std::is_trivial_v<T>)
                stream.read((char*)v.data(), size * sizeof(T));
            else for (size_t i = 0; i < size; ++i)
                ElementSerializer.UnSerialize(stream, v[i]);
        }
    };

    template<class T>
    struct Serializer<span<T>>
    {
        Serializer<T> ElementSerializer;

        void Serialize(ostream& stream, const span<T>& v)
        {
            Kaey::Serialize(stream, v.size());
            for (auto& e : v)
                ElementSerializer.Serialize(stream, e);
        }

    };

    template<>
    struct Serializer<string>
    {
        Serializer<string::size_type> SizeSerializer;

        void Serialize(ostream& stream, const string& str) const
        {
            SizeSerializer.Serialize(stream, str.size());
            stream.write(str.data(), (std::streamsize)str.size());
        }

        void UnSerialize(istream& stream, string& str) const
        {
            str.clear();
            string::size_type size = 0;
            SizeSerializer.UnSerialize(stream, size);
            str.resize(size);
            stream.read(str.data(), (streamsize)size);
        }

    };

    template<>
    struct Serializer<string_view>
    {
        Serializer<string_view::size_type> SizeSerializer;

        void Serialize(ostream& stream, const string_view& str) const
        {
            SizeSerializer.Serialize(stream, str.size());
            stream.write(str.data(), (std::streamsize)str.size());
        }

    };

    template<class A, class B>
    struct Serializer<pair<A, B>>
    {
        void Serialize(ostream& stream, const pair<A, B>& p) const
        {
            Kaey::Serialize(stream, p.first, p.second);
        }

        void UnSerialize(istream& stream, pair<A, B>& p) const
        {
            Kaey::UnSerialize(stream, p.first, p.second);
        }

    };

    template<class... Tys>
    struct Serializer<tuple<Tys...>>
    {
        void Serialize(ostream& stream, const tuple<Tys...>& t) const
        {
            std::apply([&](auto&... args)
            {
                Kaey::Serialize(stream, args...);
            }, t);
        }

        void UnSerialize(istream& stream, tuple<Tys...>& t) const
        {
            std::apply([&](auto&... args)
            {
                Kaey::UnSerialize(stream, args...);
            }, t);
        }

    };

    template<class A, class B>
    struct Serializer<map<A, B>>
    {
        void Serialize(ostream& stream, const map<A, B>& m) const
        {
            Kaey::Serialize(stream, m.size());
            for (auto& p : m)
                Kaey::Serialize(stream, p);
        }

        void UnSerialize(istream& stream, map<A, B>& m) const
        {
            size_t size = 0;
            Kaey::UnSerialize(stream, size);
            for (size_t i = 0; i < size; ++i)
            {
                pair<A, B> p;
                Kaey::UnSerialize(stream, p);
                m[move(p.first)] = move(p.second);
            }
        }

    };

    template<class T, class A>
    struct Serializer<unique_ptr<T, A>>
    {
        Serializer<T> ElementSerializer;

        void Serialize(ostream& stream, const unique_ptr<T, A>& v)
        {
            if (v != nullptr)
            {
                Kaey::Serialize(stream, true);
                ElementSerializer.Serialize(stream, *v);
            }
            else Kaey::Serialize(stream, false);
        }

        void UnSerialize(istream& stream, unique_ptr<T, A>& v)
        {
            bool hasValue;
            Kaey::UnSerialize(stream, hasValue);
            if (hasValue)
            {
                v.reset(new T);
                ElementSerializer.UnSerialize(stream, *v);
            }
            else v.reset();
        }

    };

    template<class T, class Hasher, class Comparer, class Allocator>
    struct Serializer<unordered_set<T, Hasher, Comparer, Allocator>>
    {
        using Set = unordered_set<T, Hasher, Comparer, Allocator>;

        Serializer<T> ElementSerializer;

        void Serialize(ostream& stream, const Set& v)
        {
            Kaey::Serialize(stream, v.size());
            for (auto& e : v)
                ElementSerializer.Serialize(stream, e);
        }

        void UnSerialize(istream& stream, Set& v)
        {
            size_t size;
            Kaey::UnSerialize(stream, size);
            if constexpr (std::is_trivial_v<T>)
            {
                vector<T, Allocator> vec;
                vec.resize(size);
                stream.read((char*)vec.data(), size * sizeof(T));
                v = vec | rn::to<Set>();
            }
            else
            {
                T value;
                for (size_t i = 0; i < size; ++i)
                {
                    ElementSerializer.UnSerialize(stream, value);
                    v.emplace(std::move(value));
                }
            }
        }
    };

}
