#pragma once
#include "Shader.hpp"

namespace Kaey::Renderer::Shader
{
    enum class GlTypeFlags : u32
    {
        Default   = 0,
        Constant  = 1 << 1,
        Input     = 1 << 2,
        Output    = 1 << 3,
        Coherent  = 1 << 4,
        Volatile  = 1 << 5,
        Restrict  = 1 << 6,
        Readonly  = 1 << 7,
        Writeonly = 1 << 8,
        Buffer    = 1 << 9,
    };

    constexpr GlTypeFlags operator|(GlTypeFlags a, GlTypeFlags b) { return GlTypeFlags(u32(a) | u32(b)); }
    constexpr GlTypeFlags operator&(GlTypeFlags a, GlTypeFlags b) { return GlTypeFlags(u32(a) & u32(b)); }
    constexpr GlTypeFlags operator^(GlTypeFlags a, GlTypeFlags b) { return GlTypeFlags(u32(a) ^ u32(b)); }

    constexpr GlTypeFlags& operator|=(GlTypeFlags& a, GlTypeFlags b) { return a = a | b; }
    constexpr GlTypeFlags& operator&=(GlTypeFlags& a, GlTypeFlags b) { return a = a & b; }
    constexpr GlTypeFlags& operator^=(GlTypeFlags& a, GlTypeFlags b) { return a = a ^ b; }

    constexpr GlTypeFlags operator~(GlTypeFlags a) { return GlTypeFlags(~u32(a)); }
    constexpr bool operator!(GlTypeFlags a) { return !u32(a); }
    constexpr bool operator&&(GlTypeFlags a, GlTypeFlags b) { return !!(a & b); }

    struct GlType : Variant<GlType,
        struct GlBoolType,
        struct GlIntType,
        struct GlFloatType,
        struct GlDoubleType,
        struct GlArrayType,
        struct GlVectorType,
        struct GlMatrixType,
        struct GlStructType,
        struct GlSamplerType,
        struct GlTextureType,
        struct GlTextureCubeType,
        struct GlVoidType
    >
    {
        virtual ~GlType() = default;
        KR_VIRTUAL_GETTER(string_view, Name) = 0;
    };

    struct GlStatement : Variant<GlStatement,
        struct GlVariableDeclarationStatement,
        struct GlFunctionDeclarationStatement,
        struct GlIfStatement,
        struct GlDiscardStatement,
        struct GlExpressionStatement,
        struct GlEmptyStatement
    >
    {
        virtual ~GlStatement() = default;
    };

    struct GlExpression : Variant<GlExpression,
        struct GlVoidExpression
    >
    {
        virtual ~GlExpression() = default;
    };

    struct GlShaderStage : Variant<GlShaderStage,
        struct GlVertexShaderStage,
        struct GlFragmentShaderStage,
        struct GlComputeShaderStage
    >
    {
        virtual ~GlShaderStage() = default;
    };

    struct GlBoolType final : GlType::With<GlBoolType>
    {
        KR_GETTER(string_view, Name) override { return "bool"; }
    };

    struct GlIntType final : GlType::With<GlIntType>
    {
        KR_GETTER(string_view, Name) override { return "int"; }
    };

    struct GlFloatType final : GlType::With<GlFloatType>
    {
        KR_GETTER(string_view, Name) override { return "float"; }
    };

    struct GlDoubleType final : GlType::With<GlDoubleType>
    {
        KR_GETTER(string_view, Name) override { return "double"; }
    };

    struct GlArrayType final : GlType::With<GlArrayType>
    {
        GlArrayType(GlType* underlyingType, i32 count);

        KR_GETTER(string_view, Name) override { return name; }

        KR_GETTER(GlType*, UnderlyingType) { return underlyingType; }
        KR_GETTER(i32, Count) { return count; }

    private:
        GlType* underlyingType;
        i32 count;
        string name;
    };

    struct GlVectorType final : GlType::With<GlVectorType>
    {
        GlVectorType(GlType* underlyingType, i32 count);

        KR_GETTER(string_view, Name) override { return name; }

        KR_GETTER(GlType*, UnderlyingType) { return underlyingType; }
        KR_GETTER(i32, Count) { return count; }

    private:
        GlType* underlyingType;
        i32 count;
        string name;
    };

    struct GlMatrixType final : GlType::With<GlMatrixType>
    {
        GlMatrixType(i32 rowCount, i32 columnCount);

        KR_GETTER(string_view, Name) override { return name; }

        KR_GETTER(i32, RowCount) { return rowCount; }
        KR_GETTER(i32, ColumnCount) { return columnCount; }

    private:
        i32 rowCount;
        i32 columnCount;
        string name;
    };

    struct GlStructType final : GlType::With<GlStructType>
    {
        GlStructType(string name, vector<GlVariableDeclarationStatement*> fields);

        KR_GETTER(string_view, Name) override { return name; }
        KR_GETTER(cspan<GlVariableDeclarationStatement*>, Fields) { return fields; }

    private:
        string name;
        vector<GlVariableDeclarationStatement*> fields;
    };

    struct GlSamplerType final : GlType::With<GlSamplerType>
    {
        KR_GETTER(string_view, Name) override { return "sampler"; }
    };

    struct GlTextureType final : GlType::With<GlTextureType>
    {

        KR_GETTER(string_view, Name) override { return "texture2D"; }

    };

    struct GlTextureCubeType final : GlType::With<GlTextureCubeType>
    {

        KR_GETTER(string_view, Name) override { return "textureCube"; }

    };

    struct GlVoidType final : GlType::With<GlVoidType>
    {
        KR_GETTER(string_view, Name) override { return "void"; }
    };

    struct GlVariableDeclarationStatement final : GlStatement::With<GlVariableDeclarationStatement>
    {
        string Name;
        GlType* Type;
        GlExpression* Initializer;
        GlVariableDeclarationStatement(string name, GlType* type, GlExpression* initializer);
    };

    struct GlFunctionDeclarationStatement final : GlStatement::With<GlFunctionDeclarationStatement>
    {
        string Name;
        vector<GlVariableDeclarationStatement*> Parameters;
        GlType* ReturnType;
        vector<GlStatement*> Statements;
        GlFunctionDeclarationStatement(string name, vector<GlVariableDeclarationStatement*> parameters, GlType* returnType);
    };

    struct GlIfStatement final : GlStatement::With<GlIfStatement> {};
    struct GlDiscardStatement final : GlStatement::With<GlDiscardStatement> {};
    struct GlExpressionStatement final : GlStatement::With<GlExpressionStatement> {};
    struct GlEmptyStatement final : GlStatement::With<GlEmptyStatement> {};

    struct GlShaderContext
    {
        GlShaderContext();

        KR_NO_COPY_MOVE(GlShaderContext);

        ~GlShaderContext() = default;

        KR_GETTER(GlBoolType*, BoolType) { return boolTy; }
        KR_GETTER(GlIntType*, IntType) { return intTy; }
        KR_GETTER(GlFloatType*, FloatType) { return floatTy; }
        KR_GETTER(GlTextureType*, TextureType) { return textureTy; }
        KR_GETTER(GlTextureCubeType*, TextureCubeType) { return textureCubeTy; }
        KR_GETTER(GlSamplerType*, SamplerType) { return samplerTy; }
        KR_GETTER(GlVoidType*, VoidType) { return voidTy; }

        KR_ARRAY_GETTER2(GlArrayType*, ArrayType, GlType* underlyingType, i32 count)
        {
            auto it = arrayMap.find({ underlyingType, count });
            if (it == arrayMap.end())
            {
                bool _;
                tie(it, _) = arrayMap.emplace(pair{ underlyingType, count }, createType<GlArrayType>(underlyingType, count));
            }
            return it->second;
        }

        KR_ARRAY_GETTER2(GlVectorType*, VectorType, GlType* underlyingType, i32 count)
        {
            auto it = vectorMap.find({ underlyingType, count });
            if (it == vectorMap.end())
            {
                bool _;
                tie(it, _) = vectorMap.emplace(pair{ underlyingType, count }, createType<GlVectorType>(underlyingType, count));
            }
            return it->second;
        }

        KR_ARRAY_GETTER2(GlMatrixType*, MatrixType, i32 rowCount, i32 columnCount)
        {
            auto it = matrixMap.find({ rowCount, columnCount });
            if (it == matrixMap.end())
            {
                bool _;
                tie(it, _) = matrixMap.emplace(pair{ rowCount, columnCount }, createType<GlMatrixType>(rowCount, columnCount));
            }
            return it->second;
        }

        KR_GETTER(cspan<GlStructType*>, Structs) { return structs; }

        GlVariableDeclarationStatement* DeclareVariable(string name, GlType* type, GlExpression* initializer = nullptr);

        GlFunctionDeclarationStatement* DeclareFunction(string name, vector<GlVariableDeclarationStatement*> parameters, GlType* returnType);

        GlStructType* DeclareStruct(string name, vector<GlVariableDeclarationStatement*> fields);

        template<class T>
        auto CreateShader()
        {
            auto ptr = std::make_unique<T>();
            ptr->Context = this;
            auto result = ptr.get();
            shaderStages.emplace_back(std::move(ptr));
            return result;
        }

    private:
        mutable vector<unique_ptr<GlType>> types;
        vector<unique_ptr<GlStatement>> statements;
        vector<unique_ptr<GlShaderStage>> shaderStages;
        vector<GlStructType*> structs;

        GlBoolType* boolTy;
        GlIntType* intTy;
        GlFloatType* floatTy;

        mutable unordered_map<pair<GlType*, i32>, GlArrayType*, TupleHasher> arrayMap;
        mutable unordered_map<pair<GlType*, i32>, GlVectorType*, TupleHasher> vectorMap;
        mutable unordered_map<pair<i32, i32>, GlMatrixType*, TupleHasher> matrixMap;

        GlTextureType* textureTy;
        GlTextureCubeType* textureCubeTy;
        GlSamplerType* samplerTy;
        GlVoidType* voidTy;

        template<class T, class... Args>
        T* createType(Args&&... args) const
        {
            auto ptr = make_unique<T>(std::forward<Args>(args)...);
            auto result = ptr.get();
            types.emplace_back(std::move(ptr));
            return result;
        }

        template<class T, class... Args>
        T* createStatement(Args&&... args)
        {
            auto ptr = make_unique<T>(std::forward<Args>(args)...);
            auto result = ptr.get();
            statements.emplace_back(std::move(ptr));
            return result;
            
        }

    };

    enum class GlBindingLayout
    {
        Default,
        Std430,
    };

    struct GlBinding
    {
        i32 Binding = 0;
        GlBindingLayout Layout = GlBindingLayout::Default;
        GlTypeFlags TypeFlags;
        string Name;
        GlType* Type = nullptr;
    };

    struct GlVarying
    {
        i32 Location = 0;
        string Name;
        GlType* Type = nullptr;
    };

    struct GlVertexShaderStage final : GlShaderStage::With<GlVertexShaderStage>
    {
        GlShaderContext* Context = nullptr;
        GlStructType* PushConstant = nullptr;
        vector<GlBinding> Bindings = {};
        vector<GlFunctionDeclarationStatement*> Functions = {};
        vector<GlVarying> Inputs = {};
        vector<GlVarying> Outputs = {};
        GlFunctionDeclarationStatement* MainFunction = nullptr;
    };

    struct GlFragmentShaderStage final : GlShaderStage::With<GlFragmentShaderStage>
    {
        GlShaderContext* Context = nullptr;
        GlStructType* PushConstant = nullptr;
        vector<GlBinding> Bindings = {};
        vector<GlFunctionDeclarationStatement*> Functions = {};
        vector<GlVarying> Inputs = {};
        vector<GlVarying> Outputs = {};
        GlFunctionDeclarationStatement* MainFunction = nullptr;
    };

    struct GlComputeShaderStage final : GlShaderStage::With<GlComputeShaderStage>
    {

    };

    struct GlShaderPipeline
    {
        GlShaderContext* Context;
        GlVertexShaderStage* VertexShader;
        GlFragmentShaderStage* FragmentShader;
    };

    struct GlStatementPrinterVisitor final : GlStatement::Visitor<void>, GlShaderStage::Visitor<void>
    {
        explicit GlStatementPrinterVisitor(std::ostream* os);

        void Visit(GlVariableDeclarationStatement* statement);

        void Visit(GlFunctionDeclarationStatement* statement);

        void Visit(GlVertexShaderStage* shader);

        void Visit(GlFragmentShaderStage* shader);

        void Dispatch(GlStatement* statement);

        void Dispatch(GlShaderStage* stage);

    private:
        std::ostream* os;
        i32 space;
        bool spaceRequired;

        template<class... Args>
        void print(std::format_string<Args...> fmt, Args&&... args)
        {
            if (spaceRequired)
            for (auto _ : irange(space))
                *os << "    ";
            std::format_to(std::ostream_iterator<char>(*os), fmt, std::forward<Args>(args)...);
            spaceRequired = false;
        }

        template<class... Args>
        void println(std::format_string<Args...> fmt, Args&&... args)
        {
            print(fmt, std::forward<Args>(args)...);
            println();
        }

        void println();

        void pushSpace();

        void popSpace();

        void beginScope();

        void endScope();

        void endScopeSemi();

    };

}