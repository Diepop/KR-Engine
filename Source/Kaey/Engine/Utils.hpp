#pragma once
#include "Utility.hpp"

#define KAEY_ENGINE_PROPERTY(type, name) __declspec(property(get=Get##name, put=Set##name)) type name
#define KAEY_ENGINE_READONLY_PROPERTY(type, name) __declspec(property(get=Get##name)) type name
#define KAEY_ENGINE_ARRAY_PROPERTY(type, name) __declspec(property(get=Get##name, put=Set##name)) type name[]
#define KAEY_ENGINE_READONLY_ARRAY_PROPERTY(type, name) __declspec(property(get=Get##name)) type name[]
#define KAEY_ENGINE_DERIVED_PROPERTY(base, name) using base::name

#define KAEY_ENGINE_GETTER(type, name) \
    KAEY_ENGINE_READONLY_PROPERTY(type, name); \
    type Get##name() const

#define KAEY_ENGINE_ARRAY_GETTER(type, name, valType) \
    KAEY_ENGINE_READONLY_ARRAY_PROPERTY(type, name); \
    type Get##name(valType value) const

#define KAEY_ENGINE_NO_COPY(type) \
    type(const type&) = delete; \
    type& operator=(const type&) = delete; \

#define KAEY_ENGINE_NO_COPY_MOVE(type) \
    KAEY_ENGINE_NO_COPY(type) \
    type& operator=(const type&) = delete; \
    type& operator=(type&&) noexcept = delete;

namespace Kaey::Engine
{
    using namespace linm::aliases;
    using namespace std::chrono_literals;

    namespace fs = std::filesystem;
    namespace vs = std::views;

    namespace rn
    {
        using namespace std::ranges;

        template<range Range>
        bool any(Range&& r, const range_value_t<Range>& val)
        {
            for (auto&& v : r) if (v == val)
                return true;
            return false;
        }

        template<range Range>
        bool none(Range&& r, const range_value_t<Range>& val)
        {
            return !any(r, val);
        }

    }

    using u8  =  uint8_t;
    using i8  =   int8_t;
    using u16 = uint16_t;
    using i16 =  int16_t;
    using u32 = uint32_t;
    using i32 =  int32_t;
    using u64 = uint64_t;
    using i64 =  int64_t;

    using f32 = float;
    using f64 = double;

    using semaphore = std::counting_semaphore<>;

    using std::unique_ptr;
    using std::shared_ptr;
    using std::weak_ptr;
    using std::vector;
    using std::span;
    using std::future;
    using std::function;
    using std::optional;
    using std::string;
    using std::string_view;
    using std::runtime_error;
    using std::exception;
    using std::optional;
    using std::ifstream;
    using std::ofstream;
    using std::unordered_map;
    using std::unordered_set;
    using std::pair;
    using std::tuple;
    using std::istream;
    using std::ostream;
    using std::mutex;
    using std::list;
    using std::streamsize;
    using std::set;
    using std::map;
    using std::variant;
    using std::packaged_task;
    using std::lock_guard;
    using std::queue;
    using std::jthread;
    using std::array;
    using std::stringstream;

    template<class T, size_t Extent = std::dynamic_extent>
    using cspan = span<const T, Extent>;

    using nlohmann::json;

    using std::move;
    using std::swap;
    using std::forward;
    using std::make_unique;
    using std::make_shared;

    using std::nullopt;

    using Kaey::join;

    constexpr f32 operator""_deg(long double value) { return f32(value * linm::constants<long double>::Tau / 360); }
    constexpr f32 operator""_turn(long double value) { return f32(value * linm::constants<long double>::Tau); }
    constexpr f32 operator""_deg(unsigned long long int value) { return operator""_deg((long double)value); }
    constexpr f32 operator""_turn(unsigned long long int value) { return operator""_turn((long double)value); }

    constexpr Vector2 operator""_xy(long double value) { return Vector2(f32(value)); }
    constexpr Vector2 operator""_xy(unsigned long long int value) { return Vector2(f32(value)); }
    constexpr Vector3 operator""_xyz(long double value) { return Vector3(f32(value)); }
    constexpr Vector3 operator""_xyz(unsigned long long int value) { return Vector3(f32(value)); }
    constexpr Vector4 operator""_xyzw(long double value) { return Vector4(f32(value)); }
    constexpr Vector4 operator""_xyzw(unsigned long long int value) { return Vector4(f32(value)); }

    inline namespace Utils
    {
        constexpr size_t DEFAULT_HASH = 0x435322AC;

        extern mutex PrintMutex;

        constexpr struct ToVector
        {
            template<rn::range R>
            friend auto operator|(R&& r, const ToVector&)
            {
                vector<rn::range_value_t<R>> v;
                if constexpr (requires { rn::size(r); })
                    v.reserve(rn::size(r));
                for (auto&& e : r)
                    v.emplace_back(move(e));
                return v;
            }

            template<class T>
            friend vector<T> operator|(std::initializer_list<T> list, const ToVector&)
            {
                vector<T> v;
                v.reserve(v.size());
                for (auto&& e : list)
                    v.emplace_back(move(e));
                return v;
            }

        }to_vector;

        constexpr struct ToSet
        {
            template<rn::range R>
            friend auto operator|(R&& r, const ToSet&)
            {
                unordered_set<rn::range_value_t<R>> v;
                if constexpr (requires { rn::size(r); })
                    v.reserve(rn::size(r));
                for (auto&& e : r)
                    v.emplace(move(e));
                return v;
            }

            template<class T>
            friend vector<T> operator|(std::initializer_list<T> list, const ToSet&)
            {
                unordered_set<T> v;
                v.reserve(v.size());
                for (auto&& e : list)
                    v.emplace(move(e));
                return v;
            }

        }to_set;

        inline auto operator""_f(const char* str, size_t)
        {
            return [=](auto&&... args)
            {
                return std::vformat(str, std::make_format_args(std::forward<decltype(args)>(args)...));
            };
        }

        template<class Int>
        auto irange(Int max) { return vs::iota((Int)0, max); }

        template<class Fmt, class... Args>
        void print(Fmt&& fmt, Args&&... args)
        {
            lock_guard l(PrintMutex);
            std::vformat_to(std::ostreambuf_iterator(std::cout), fmt, std::make_format_args(std::forward<Args>(args)...));
        }

        inline void println()
        {
            putchar('\n');
        }

        template<class Fmt, class... Args>
        void println(Fmt&& fmt, Args&&... args)
        {
            lock_guard l(PrintMutex);
            std::vformat_to(std::ostreambuf_iterator(std::cout), fmt, std::make_format_args(std::forward<Args>(args)...));
            println();
        }

        constexpr size_t chash(string_view sv)
        {
            size_t seed = 0xDEADBEEF;
            for (auto d : sv)
                seed ^= size_t(d * 0x9e3779b9) + (seed << 6) + (seed >> 2);
            return seed;
        }

        constexpr size_t operator""_h(const char* str, size_t len)
        {
            return chash(str);
        }

        template<class K, class V, class Fn, class... Args>
        V* AtomicEmplace(unordered_map<K, unique_ptr<V>>& m, mutex& mut, K key, Fn fn, Args&&... args)
        {
            typename unordered_map<K, unique_ptr<V>>::iterator it;
            bool load = false;
            {
                lock_guard l{ mut };
                it = m.find(key);
                if (it == m.end())
                    tie(it, load) = m.emplace(move(key), nullptr);
            }
            if (load)
                it->second = std::invoke(fn, forward<Args>(args)...);
            while (!it->second)
                std::this_thread::sleep_for(1ns);
            return it->second.get();
        }

        template<rn::range Range, class Fn>
        auto reduce(Range&& range, Fn&& fn)
        {
            auto first = rn::begin(range);
            auto last = rn::end(range);
            if (first == last)
                throw std::runtime_error("Empty range used!");
            auto init = *first++;
            return std::reduce(first, last, init, std::forward<Fn>(fn));
        }

    }

    struct KaeyEngine;
    struct MaterialRange;
    struct ComputePipeline;
    struct MeshObject;
    struct Swapchain;
    struct RenderDevice;
    struct MeshData;
    struct Frame;
    struct GraphicsPipeline;
    struct DeviceQueue;
    struct MemoryBuffer;
    template<class T>
    struct DefinedMemoryBuffer;
    struct Material;
    struct PipelineLayout;
    struct Texture;
    struct RenderEngine;
    struct ThreadPool;
    struct ComputeData;
    struct GameObject;
    struct LightObject;
    struct KaeyEngine;
    struct Project;
    struct Time;
    struct CameraObject;
    struct NodeMaterial;
    struct NodeMaterial;
    struct NodeOutput;
    struct Node;

#ifdef _DEBUG
    constexpr bool IsDebug = true;
    constexpr bool IsRelease = false;
#else
    constexpr bool IsDebug = false;
    constexpr bool IsRelease = true;
#endif

    template<class T>
    concept Mutable = !std::is_const_v<T>;

    void CantFail(vk::Result result, const char* msg);

    template<class Result>
    Result CantFail(vk::ResultValue<Result> result, const char* msg)
    {
        return result.result == vk::Result::eSuccess ? move(result.value) : throw runtime_error(msg);
    }

    u32 FindMemoryIndex(vk::PhysicalDevice physicalDevice, u32 typeFilter, vk::MemoryPropertyFlags properties);

    template<class... Ts>
    struct Overload : Ts... { using Ts::operator()...; };

    template<class... Ts>
    Overload(Ts...) -> Overload<Ts...>;

    // Loops the value t, so that it is never larger than length and never smaller than 0.
    template<std::floating_point Fl, class Len>
    Fl repeat(Fl t, Len len)
    {
        auto length = Fl(len);
        return std::clamp(t - std::floor(t / length) * length, 0.0f, length);
    }

    constexpr auto not_null = vs::filter([](auto&& v) { return bool(v); });

}

namespace std
{
    template<std::ranges::range Range, class Delim>
    struct formatter<Kaey::Join<Range, Delim>>
    {
        using element_type = ranges::range_value_t<Range>;

        formatter<element_type> form;
        formatter<Delim> dForm;

        constexpr auto parse(auto& ctx)
        {
            return form.parse(ctx);
        }

        auto format(Kaey::Join<Range, Delim> p, auto& ctx) const
        {
            auto it = ranges::begin(p.range);
            auto end = ranges::end(p.range);
            if (it != end)
                form.format(*it++, ctx);
            for (; it != end; ++it)
            {
                dForm.format(p.delim, ctx);
                form.format(*it, ctx);
            }
            return ctx.out();
        }

    };

    template<>
    struct formatter<vk::QueueFlags>
    {
        formatter<string> fmt;

        constexpr auto parse(auto& ctx)
        {
            return fmt.parse(ctx);
        }

        auto format(vk::QueueFlags flags, auto& ctx) const
        {
            fmt.format("QueueFlag(", ctx);

            auto printBit = [&, first = true](vk::QueueFlagBits bit) mutable
                {
                    if (!(flags & bit))
                        return;
                    if (!first)
                        fmt.format("|", ctx);
                    fmt.format(magic_enum::enum_name(bit).data() + 1, ctx);
                    first = false;
                };

            for (auto v : magic_enum::enum_values<vk::QueueFlagBits>())
                printBit(v);

            fmt.format(")", ctx);
            return ctx.out();
        }

    };

}

namespace ImGui
{
    void Text(std::string_view str);
    void TextUnformatted(std::string_view str);
    bool TreeNode(std::string_view str);
    bool TreeNode(const std::string& str);
    void Image(Kaey::Engine::Texture* tex, const Kaey::Engine::Vector2& size = Kaey::Engine::Vector2::Zero);
    bool ImageButton(Kaey::Engine::Texture* tex, const Kaey::Engine::Vector2& size = Kaey::Engine::Vector2::Zero);
    bool InputText(std::string_view label, std::filesystem::path& path, ImGuiInputTextFlags flags = ImGuiInputTextFlags_None, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr);
    //bool InputText(std::string_view label, std::string& str, ImGuiInputTextFlags flags = ImGuiInputTextFlags_None, ImGuiInputTextCallback callback = nullptr, void* userData = nullptr);
    bool TreeNode(const std::filesystem::path& path);
    bool Checkbox(const char* label, bool value);

    void MaterialEdit(Kaey::Engine::Material* mat);

    template<class Enum>
        requires (std::is_enum_v<Enum>)
    bool Combo(const char* label, Enum* e, Enum end)
    {
        return Combo(label, (int*)e, [](void*, int i, const char** out) -> bool { return *out = magic_enum::enum_names<Enum>()[i].data(); }, nullptr, (int)end);
    }

    template<class Enum>
        requires (std::is_enum_v<Enum>)
    bool Combo(const char* label, Enum* e)
    {
        return Combo(label, e, (Enum)magic_enum::enum_count<Enum>());
    }

    struct BeginArgs
    {
        ImGuiWindowFlags Flags = 0;
        bool* Open = nullptr;
    };

    template<class Fn, class... Args>
        requires (std::is_invocable_v<Fn, Args...>)
    bool Begin(const char* name, const BeginArgs& beg, Fn&& fn, Args&&... args)
    {
        auto result = Begin(name, beg.Open, beg.Flags);
        if (result)
            fn(std::forward<Args>(args)...);
        End();
        return result;
    }

}

#include "AssetMap.hpp"
