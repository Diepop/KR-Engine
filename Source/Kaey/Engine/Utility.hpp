#pragma once

namespace Kaey
{
    using std::mem_fn;

    namespace rn = std::ranges;
    namespace vs = std::views;

    constexpr size_t DEFAULT_HASH = 0x435322AC;

    template<class T, template<class...>class Specialization>
    struct is_specialization_s : std::false_type {  };

    template<template<class...>class Specialization, class... Args>
    struct is_specialization_s<Specialization<Args...>, Specialization> : std::true_type {  };

    template<class T, template<class...>class Specialization>
    constexpr auto is_specialization = is_specialization_s<T, Specialization>::value;

    constexpr auto operator""_f(const char* f, size_t l) { return[=]<class... Args>(Args&&... args) { return vformat(f, std::make_format_args(std::forward<Args>(args)...)); }; }

    template<rn::range Range, class Delim>
    struct Join
    {
        Range range;
        Delim delim;
    };

    template<rn::range Range, class Delim>
    auto join(Range range, Delim delim) -> Join<Range, Delim>
    {
        return { std::move(range), std::move(delim) };
    }

    template<class Bound>
        requires std::is_constructible_v<Bound, int>
    auto irange(Bound b)
    {
        return rn::iota_view(Bound(0), b);
    }

    template <class T>
    void hash_combine(size_t& seed, const T& v)
    {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    struct EmptyVector
    {
        template<class U, class V>
        operator std::vector<U, V>() const { return {}; }
    };

    struct TupleHasher
    {
        template<class... Args>
        size_t operator()(const std::tuple<Args...>& v) const
        {
            size_t seed = DEFAULT_HASH;
            std::apply([&](auto&&... args) { (hash_combine(seed, args), ...); }, v);
            return seed;
        }
        template<class A, class B>
        size_t operator()(const std::pair<A, B>& v) const
        {
            size_t seed = DEFAULT_HASH;
            hash_combine(seed, v.first);
            hash_combine(seed, v.second);
            return seed;
        }
    };

    struct RangeHasher
    {
        template<class T>
        size_t operator()(const T& v) const
        {
            size_t seed = DEFAULT_HASH;
            for (auto value : v)
                hash_combine(seed, value);
            return seed;
        }
    };

    template<class T>
        requires std::is_same_v<T, std::remove_cvref_t<T>>
    struct ArrayView : rn::view_interface<ArrayView<T>>
    {
        ArrayView(const T* ptr, size_t len) : ptr(ptr), len(len) {  }
        ArrayView(const T* begin, const T* end) : ptr(begin), len(end - begin) {  }
        template<size_t Lenght>
        ArrayView(const T(&arr)[Lenght]) : ArrayView(+arr, Lenght) {  }
        ArrayView(const std::vector<T>& v) : ArrayView(v.data(), v.size()) {  }
        ArrayView(std::initializer_list<T> list) : ArrayView(list.begin(), list.end()) {  }

        size_t size_bytes() const { return sizeof(T) * len; }

        auto begin() const { return ptr; }
        auto end() const { return ptr + len; }

        void remove_prefix(size_t n)
        {
            assert(len >= n);
            ptr += n;
            len -= n;
        }

        void remove_suffix(size_t n)
        {
            assert(len >= n);
            len -= n;
        }

        friend bool operator==(const ArrayView& lhs, const ArrayView& rhs)
        {
            return (lhs.ptr == rhs.ptr && lhs.len == rhs.len) || rn::equal(lhs, rhs);
        }

        friend size_t hash_value(const ArrayView& obj)
        {
            return RangeHasher()(obj);
        }

    private:
        const T* ptr;
        size_t len;
    };

    template<class T>
    ArrayView(std::initializer_list<T> list) -> ArrayView<T>;

    template<class Itl, class Itr>
    struct ZipView : rn::view_interface<ZipView<Itl, Itr>>
    {
        struct iterator
        {
            Itl il;
            Itr ir;
            iterator(Itl il, Itr ir) : il(std::move(il)), ir(std::move(ir)) {}
            auto operator*() const -> std::pair<typename Itl::reference, typename Itr::reference>
            {
                return { *il, *ir };
            }
            iterator& operator++()
            {
                ++il;
                ++ir;
                return *this;
            }
            iterator operator++(int)
            {
                auto result = *this;
                ++* this;
                return result;
            }

            friend bool operator==(const iterator& lhs, const iterator& rhs)
            {
                return lhs.il == rhs.il
                    && lhs.ir == rhs.ir;
            }

        };

        template<class A, class B>
        ZipView(A&& a, B&& b) : begl(std::begin(a)), endl(std::end(a)), begr(std::begin(b)), endr(std::end(b))
        {

        }

        ZipView(Itl begl, Itl endl, Itr begr, Itr endr) :
            begl(std::move(begl)),
            endl(std::move(endl)),
            begr(std::move(begr)),
            endr(std::move(endr))
        {

        }

        iterator begin() const { return { begl, begr }; }
        iterator end() const { return { endl, endr }; }

    private:
        Itl begl, endl;
        Itr begr, endr;
    };

    template<rn::range A, rn::range B>
    ZipView(A&& a, B&& b) -> ZipView<decltype(std::begin(a)), decltype(std::begin(b))>;

    template<rn::range A, rn::range B>
    auto zip(A&& a, B&& b)
    {
        return ZipView{ a, b };
    }

    template<rn::range Range, class Fn>
    auto reduce(Range&& range, Fn&& fn)
    {
        auto first = rn::begin(range);
        auto last = rn::end(range);
        if (first == last)
            throw std::runtime_error("Empty range used!");
        auto init = *first++;
        return reduce(first, last, init, std::forward<Fn>(fn));
    }

    namespace detail
    {
        template <class ReturnType, class... Args>
        struct function_traits_base
        {
            static constexpr auto arity = sizeof...(Args);
            using result_type = ReturnType;
            using arg_types = std::tuple<Args...>;
            template <size_t i>
            using arg_type = std::tuple_element_t<i, arg_types>;
        };

        template <class T>
        struct function_traits : function_traits<decltype(&T::operator())> {};

        template <class ClassType, class ReturnType, class... Args>
        struct function_traits<ReturnType(ClassType::*)(Args...) const> : function_traits_base<ReturnType, Args...> {  };

        template <class ClassType, class ReturnType, class... Args>
        struct function_traits<ReturnType(ClassType::*)(Args...)> : function_traits_base<ReturnType, Args...> {  };

        template <class ReturnType, class... Args>
        struct function_traits<ReturnType(*)(Args...)> : function_traits_base<ReturnType, Args...> {  };

        template<class T>
        struct is_optional : std::false_type {  };

        template<class T>
        struct is_optional<std::optional<T>> : std::true_type {  };

        template<class T>
        constexpr bool is_optional_v = is_optional<T>::value;

        template<class T>
        struct is_mono_variant : std::false_type {  };

        template<class... Ts>
        struct is_mono_variant<std::variant<Ts...>> : std::bool_constant<(std::is_same_v<std::monostate, Ts> || ...)> {  };

        template<class T>
        constexpr bool is_mono_variant_v = is_mono_variant<T>::value;

        template<class Variant, class... Args>
        size_t TupleHash(std::tuple<Args*...>*)
        {
            return TupleHasher()(std::make_tuple(Variant::template IdOf<Args>()...));
        }

        template<class Fn, class Base, size_t... Is>
        auto Invoke(ArrayView<Base*> args, Fn fn, std::index_sequence<Is...>)
        {
            return fn(static_cast<typename function_traits<Fn>::template arg_type<Is>>(args[Is])...);
        }

        template<class Fn, class Base>
        auto Invoke(ArrayView<Base*> args, Fn fn)
        {
            return Invoke(args, fn, std::make_index_sequence<function_traits<Fn>::arity>());
        }

        template<class Result, class... Tys>
        using FindResultType = std::conditional_t<std::is_void_v<Result>, std::common_type_t<Tys...>, Result>;

    }

    constexpr struct ToVector
    {
        template <rn::range Range>
        friend auto operator|(Range&& r, ToVector)
        {
            return std::vector<std::decay_t<rn::range_value_t<Range>>>{ r.begin(), r.end() };
        }

    }to_vector;

    template<class T>
    struct Cast
    {
        template<class Range>
        friend auto operator|(Range&& range, const Cast&)
        {
            return std::forward<Range>(range) | vs::transform([](auto ptr) { return ptr->template As<T>(); });
        }
    };

    template<class... Ts>
    struct CreateOverload : Ts... { using Ts::operator()...; };

    template<class... Ts>
    CreateOverload(Ts...) -> CreateOverload<Ts...>;

    template<class BaseType, class... Tys>
    struct Variant
    {
        using Types = std::tuple<Tys...>;

        template<size_t I>
        using Type = std::tuple_element_t<I, Types>;

        template<class T>
        static constexpr size_t IdOf()
        {
            size_t id = -1;
            ((++id, std::is_same_v<T, Tys>) || ...);
            return id;
        }

        static std::string_view NameOf(size_t id)
        {
            static const std::string_view names[]{ typeid(Tys).name()... };
            return names[id];
        }

        template<class T, class Parent = BaseType>
            requires std::is_base_of_v<Variant, Parent>
        struct With : Parent
        {
            using ParentClass = With;
            static constexpr size_t Id = IdOf<T>();
            template<class... Args>
            With(Args&&... args) : Parent(std::forward<Args>(args)...) { this->index = Id; }
        };

        template<class Result>
        struct Visitor
        {

        };

    private:
        template<class Result>
        static auto VisitorResultTypeFn(const Visitor<Result>*) -> Result { throw 0; }
        static auto VisitorResultTypeFn(const void*) -> void { throw 0; }
        template<class Result, class Visitor>
        using VisitorResultType = std::conditional_t<std::is_void_v<Result>, decltype(Variant::VisitorResultTypeFn((std::remove_reference_t<Visitor>*)nullptr)), Result>;
    public:

        template<class T>
        bool Is() const { return index == IdOf<T>(); }

        template<class... Ts>
        bool IsNot() const { return (!Is<Ts>() && ...); }

        template<class T>
        T* As() { return Is<T>() ? static_cast<T*>(this) : nullptr; }

        template<class T>
        const T* As() const { return const_cast<Variant*>(this)->As<T>(); }

        template<class... Ts>
        bool IsOr() const { return (Is<Ts>() || ...); }

        size_t KindId() const { return index; }

        std::string_view NameOf() const { return NameOf(KindId()); }

        template<class Res, class Visitor>
        static auto Visit(BaseType* value, Visitor& visitor) -> VisitorResultType<Res, Visitor>
        {
            using Result = VisitorResultType<Res, Visitor>;
            using Fn = Result(*)(BaseType*, Visitor&);
            static constexpr Fn Fns[]
            {
                (Fn)+[](BaseType* v, Visitor& vis) -> Result { return VisitSingle<Result>(vis, v); },
                (Fn)+[](BaseType* v, Visitor& vis) -> Result { return VisitSingle<Result>(vis, static_cast<Tys*>(v)); }...
            };
            if (value == nullptr)
            {
                if constexpr (requires { visitor(nullptr); })
                        return visitor(nullptr);
                else throw std::runtime_error("No overload for nullptr!");
            }
            return Fns[value->index + 1](value, visitor);
        }

        template<class Res, class Visitor>
        static auto VariadicVisit(ArrayView<BaseType*> v, Visitor& vis)
        {
            return VariadicVisitImpl2<Res>(v, vis, std::make_index_sequence<3>());
        }

        template<class Res, rn::range Range, class Visitor>
        static auto VisitRange(Range& range, Visitor& visitor)
        {
            using Result = VisitorResultType<Res, Visitor>;
            if constexpr (std::is_void_v<Result>)
                for (auto&& ptr : range)
                    Variant::Visit<Res>(ptr, visitor);
            else return range | vs::transform([&](auto&& ptr) -> Result { return Variant::Visit<Res>(ptr, visitor); }) | to_vector;
        }

        template<class Res, class Visitor>
        static auto InvokeIndex(size_t index, Visitor& visitor) -> VisitorResultType<Res, Visitor>
        {
            using Result = VisitorResultType<Res, Visitor>;
            using Fn = Result(*)(Visitor&);
            static constexpr Fn Fns[]
            {
                (Fn)+[](Visitor& vis) -> Result { return VisitSingle<Result>(vis, static_cast<BaseType*>(nullptr)); },
                (Fn)+[](Visitor& vis) -> Result { return VisitSingle<Result>(vis, static_cast<Tys*>(nullptr)); }...
            };
            return Fns[index + 1](visitor);
        }

    private:
        size_t index = -1;
#ifdef _DEBUG
        virtual void DummyMethodForDebugInfo() {  }
#endif

        template<class Result, class Visitor, class... Args>
        static auto VisitSingle(Visitor& vis, Args... args) -> Result
        {
            if constexpr (requires { { vis(args...) } -> std::same_as<Result>; })
                return vis(args...);
            else if constexpr (requires { Result(vis(args...)); })
                return Result(vis(args...));
            else if constexpr (requires { { vis.Visit(args...) } -> std::same_as<Result>; })
                return vis.Visit(args...);
            else if constexpr (requires { Result(vis.Visit(args...)); })
                return Result(vis.Visit(args...));
            else if constexpr (detail::is_optional_v<Result>)
                return std::nullopt;
            else if constexpr (std::is_pointer_v<Result>)
                return nullptr;
            else if constexpr (std::is_void_v<Result>)
                return;
            else
            {
                if constexpr (sizeof...(Args) > 0)
                    throw std::runtime_error("Unimplemented Method Visit({})"_f(join(ArrayView{ typeid(Args).name()... }, ", ")));
                else throw std::runtime_error("Unimplemented Method Visit()");
            }
        }

        static constexpr auto MakeArray(auto&&... args)
        {
            return std::array{ std::forward<decltype(args)>(args)... };
        }

        template<size_t... I, class... Args>
        static constexpr auto ApplyImpl(auto fn, std::index_sequence<I...>, Args... seqs)
        {
            if constexpr (sizeof...(Args) > 0)
                return MakeArray(ApplyImpl([&](auto... args) { return fn((Type<I>*)nullptr, args...); }, seqs...)...);
            else return MakeArray(fn((Type<I>*)nullptr)...);
        }

        template<size_t Length, size_t... I>
        static constexpr auto Apply(auto fn, std::index_sequence<I...>)
        {
            return ApplyImpl(fn, std::make_index_sequence<((I, Length))>()...);
        }

        template<size_t Length>
        static constexpr auto Apply(auto fn)
        {
            return Apply<sizeof...(Tys)>(fn, std::make_index_sequence<Length>());
        }

        template<class Arr>
        static constexpr auto AccessIndex(Arr&& arr, auto first, auto... args) -> decltype(auto)
        {
            if constexpr (sizeof...(args) > 0)
                return AccessIndex(arr[first], args...);
            else return arr[first];
        }

        template<class Res, class Visitor, size_t... I>
        static auto VariadicVisitImpl(ArrayView<BaseType*> v, Visitor& vis, std::index_sequence<I...>) -> VisitorResultType<Res, Visitor>
        {
            using Result = VisitorResultType<Res, Visitor>;
            using Fn = Result(*)(ArrayView<BaseType*>, Visitor&);
            static constexpr auto Callbacks = Apply<sizeof...(I)>([]<class... Args>(Args*...) constexpr
            {
                return (Fn)+[](ArrayView<BaseType*> view, Visitor& visitor) -> Result
                {
                    size_t idx = view.size();
                    return VisitSingle<Result>(visitor, static_cast<Args*>(view[--idx])...);
                };
            });
            return AccessIndex(Callbacks, v[I]->index...)(v, vis);
        }

        template<class Res, class Visitor, size_t... I>
        static auto VariadicVisitImpl2(ArrayView<BaseType*> v, Visitor& vis, std::index_sequence<I...>) -> VisitorResultType<Res, Visitor>
        {
            if (v.size() > sizeof...(I))
                throw std::runtime_error("Invalid range size, was {} max expected was {}."_f(v.size(), sizeof...(I)));
            using Result = VisitorResultType<Res, Visitor>;
            using Fn = Result(*)(ArrayView<BaseType*>, Visitor&);
            static constexpr Fn Callbacks[]
            {
                (Fn)+[](ArrayView<BaseType*>, Visitor& visitor) -> Result { return VisitSingle<Result>(visitor); },
                (Fn)+[](ArrayView<BaseType*> args, Visitor& visitor) -> Result
                {
                    return VariadicVisitImpl<Res>(args, visitor, std::make_index_sequence<I + 1>());
                }...
            };
            return Callbacks[v.size()](v, vis);
        }

    };

    template<class Res = void, rn::range Range, class Visitor, class... Args>
    auto VariadicVisit(Range&& range, Visitor&& visitor, Args&&... args)
    {
        if constexpr (std::is_pointer_v<std::remove_reference_t<Visitor>>)
            return VariadicVisit<Res>(std::forward<Range>(range), *visitor);
        else if constexpr (sizeof...(Args))
            return VariadicVisit<Res>(std::forward<Range>(range), CreateOverload(std::forward<Visitor>(visitor), std::forward<Args>(args)...));
        else
        {
            using BaseType = std::remove_pointer_t<rn::range_value_t<Range>>;
            if constexpr (requires { ArrayView<BaseType*>(std::forward<Range>(range)); })
                return BaseType::template VariadicVisit<Res>(ArrayView<BaseType*>(std::forward<Range>(range)), visitor);
            else return VariadicVisit<Res>(range | to_vector, std::forward<Visitor>(visitor));
        }
    }

    template<class Res = void, class BaseType, class Visitor, class... Args>
    auto VariadicVisit(std::initializer_list<BaseType*> list, Visitor&& visitor, Args&&... args)
    {
        return VariadicVisit<Res>(ArrayView<BaseType*>(list), std::forward<Visitor>(visitor), std::forward<Args>(args)...);
    }

    template<class Res = void, rn::range Range, class Visitor, class... Args>
    auto VisitRange(Range&& range, Visitor&& visitor, Args&&... args)
    {
        if constexpr (std::is_pointer_v<std::remove_reference_t<Visitor>>)
            return VisitRange<Res>(std::forward<Range>(range), *visitor);
        else if constexpr (sizeof...(Args) > 0)
            return VisitRange<Res>(std::forward<Range>(range), CreateOverload(std::forward<Visitor>(visitor), std::forward<Args>(args)...));
        else
        {
            using Value = std::remove_pointer_t<rn::range_value_t<std::remove_reference_t<Range>>>;
            return Value::template VisitRange<Res>(range, visitor);
        }
    }
    
    template<class Res = void, class BaseType, class Visitor, class... Args>
    auto Dispatch(BaseType* value, Visitor&& visitor, Args&&... args)
    {
        if constexpr (std::is_pointer_v<std::remove_reference_t<Visitor>>)
            return Dispatch<Res>(value, *visitor);
        else if constexpr (sizeof...(Args) > 0)
            return Dispatch<Res>(value, CreateOverload(std::forward<Visitor>(visitor), std::forward<Args>(args)...));
        else return BaseType::template Visit<Res>(value, visitor);
    }

    template<class BaseType, class Res = void, class Visitor, class... Args>
    static auto InvokeIndex(size_t index, Visitor&& visitor, Args&&... args)
    {
        if constexpr (std::is_pointer_v<std::remove_reference_t<Visitor>>)
            return InvokeIndex<BaseType, Res>(index, *visitor);
        else if constexpr (sizeof...(Args) > 0)
            return InvokeIndex<BaseType, Res>(index, CreateOverload(std::forward<Visitor>(visitor), std::forward<Args>(args)...));
        else return BaseType::template InvokeIndex<Res>(index, visitor);
    }

    //Uses "v->Type()" in all the elements of the range.
    constexpr auto type_of_range = vs::transform([](auto ptr) { return ptr->Type(); });

    //Uses "v->Name()" in all the elements of the range.
    constexpr auto name_of_range = vs::transform([](auto ptr) { return ptr->Name(); });

}

namespace std
{
    template<class T>
    struct hash<Kaey::ArrayView<T>>
    {
        size_t operator()(Kaey::ArrayView<T> av)
        {
            return hash_value(av);
        }
    };

}
