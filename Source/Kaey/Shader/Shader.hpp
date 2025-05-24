#pragma once
#include "Kaey/ShaderCompiler/Variant.hpp"
#include "Kaey/Renderer/Renderer.hpp"

namespace Kaey::Renderer::Shader
{
    enum class BlendMode
    {
         
    };

    struct ShaderContext;

    struct IType : Variant<IType,
        struct BoolType,
        struct IntType,
        struct FloatType,
        struct DoubleType,
        struct VectorType,
        struct MatrixType,
        struct StructType,
        struct ArrayType,
        struct SamplerType,
        struct TextureType,
        struct VoidType
    >
    {
        virtual ~IType() = default;
    };

    struct IExpression : Variant<IExpression,
        struct IntExpression,
        struct FloatExpression,
        struct VectorExpression,
        struct ColorExpression,
        struct UnaryExpression,
        struct BinaryExpression,
        struct AssignExpression,
        struct AttributeExpression,
        struct FunctionCallExpression,
        struct VoidExpression
    >
    {
        virtual ~IExpression() = default;
        KR_VIRTUAL_GETTER(IType*, Type) = 0;
    };

    struct IStatement : Variant<IStatement,
        struct VariableDeclarationStatement,
        struct FunctionDeclarationStatement,
        struct IfStatement,
        struct DiscardStatement,
        struct ExpressionStatement
    >
    {
        virtual ~IStatement() = default;
    };

    struct BoolType final : IType::With<BoolType>
    {

    };

    struct IntType final : IType::With<IntType>
    {

    };

    struct FloatType final : IType::With<FloatType>
    {

    };

    struct DoubleType final : IType::With<DoubleType>
    {

    };

    struct VectorType final : IType::With<VectorType>
    {
        IType* UnderlyingType;
        i32 Count;
        VectorType(IType* underlyingType, i32 count);
    };

    struct MatrixType final : IType::With<MatrixType>
    {
    };

    struct StructType final : IType::With<StructType>
    {
    };

    struct ArrayType final : IType::With<ArrayType>
    {
    };

    struct SamplerType final : IType::With<SamplerType>
    {
    };

    struct TextureType final : IType::With<TextureType>
    {
    };

    struct VoidType final : IType::With<VoidType>
    {
    };

    enum class OperatorSymbol : i32
    {
        Plus,
        Minus,
        Star,
        Slash,
    };

    struct IntExpression final : IExpression::With<IntExpression>
    {
        explicit IntExpression(ShaderContext* context);

        KR_NO_COPY_MOVE(IntExpression);

        KR_GETTER(ShaderContext*, Context) { return context; }
        KR_GETTER(IType*, Type) override;

        i32 Value;

    private:
        ShaderContext* context;
    };

    struct FloatExpression final : IExpression::With<FloatExpression>
    {
        explicit FloatExpression(ShaderContext* context);

        KR_NO_COPY_MOVE(FloatExpression);

        KR_GETTER(ShaderContext*, Context) { return context; }
        KR_GETTER(IType*, Type) override;

        f32 Value;

    private:
        ShaderContext* context;
    };

    struct VectorExpression final : IExpression::With<VectorExpression>
    {
        explicit VectorExpression(ShaderContext* context);

        KR_NO_COPY_MOVE(VectorExpression);

        KR_GETTER(ShaderContext*, Context) { return context; }
        KR_GETTER(VectorType*, Type) override { return type; }

        Vector3 Value;

    private:
        ShaderContext* context;
        VectorType* type;
    };

    struct ColorExpression final : IExpression::With<ColorExpression>
    {
        explicit ColorExpression(ShaderContext* context);

        KR_NO_COPY_MOVE(ColorExpression);

        KR_GETTER(ShaderContext*, Context) { return context; }
        KR_GETTER(VectorType*, Type) override { return type; }

        Vector4 Value;

    private:
        ShaderContext* context;
        VectorType* type;
    };

    struct UnaryExpression : IExpression::With<UnaryExpression>
    {

    };

    struct BinaryExpression : IExpression::With<BinaryExpression>
    {
        IExpression* LeftOperand;
        OperatorSymbol Operator;
        IExpression* RightOperand;
        BinaryExpression(IExpression* leftOperand, OperatorSymbol op, IExpression* rightOperand);
    };

    struct AssignExpression : IExpression::With<AssignExpression>
    {

    };

    struct AttributeExpression final : IExpression::With<AttributeExpression>
    {
        AttributeExpression(string name, IType* type);

        KR_GETTER(IType*, Type) override { return type; }

        KR_GETTER(string_view, Name) { return name; }

    private:
        string name;
        IType* type;
    };

    struct FunctionCallExpression final : IExpression::With<FunctionCallExpression>
    {
        FunctionCallExpression(ShaderContext* context, string name, vector<IExpression*> arguments, IType* type);

        KR_GETTER(IType*, Type) override { return type; }
        KR_GETTER(cspan<IExpression*>, Arguments) { return arguments; }
        KR_GETTER(string_view, Name) { return name; }

    private:
        ShaderContext* context;
        string name;
        vector<IExpression*> arguments;
        IType* type;
    };

    struct VoidExpression : IExpression::With<VoidExpression>
    {
        //KR_GETTER(VoidType*, ReturnType) override { return  }
    };

    struct VariableDeclarationStatement final : IStatement::With<VariableDeclarationStatement>
    {
        string Name;
        IType* Type;
        IExpression* Initializer;
        VariableDeclarationStatement(string name, IType* type, IExpression* initializer);
    };

    struct FunctionDeclarationStatement final : IStatement::With<FunctionDeclarationStatement>
    {
        string Name;
        vector<VariableDeclarationStatement*> Parameters;
        IType* ReturnType;
        vector<IStatement*> Statements;
        FunctionDeclarationStatement(string name, vector<VariableDeclarationStatement*> parameters, IType* returnType);
    };

    struct IfStatement final : IStatement::With<IfStatement>
    {
        IStatement* TrueStatement;
        IStatement* FalseStatement;
        IfStatement(IStatement* trueStatement, IStatement* falseStatement);
    };

    struct DiscardStatement final : IStatement::With<DiscardStatement>
    {

    };

    struct ExpressionStatement final : IStatement::With<ExpressionStatement>
    {
        explicit ExpressionStatement(IExpression* expression) : expression(expression)
        {

        }

        KR_GETTER(IExpression*, Expression) { return expression; }

    private:
        IExpression* expression;
    };

    struct IShaderStage : Variant<IShaderStage,
        struct VertexShaderStage,
        struct FragmentShaderStage,
        struct ComputeShaderStage
    >
    {
        virtual ~IShaderStage() = default;
        KR_VIRTUAL_GETTER(ShaderContext*, Context) = 0;
        KR_VIRTUAL_GETTER(vector<VariableDeclarationStatement*>&, Outputs) = 0;
        KR_VIRTUAL_GETTER(IShaderStage*, PreviousStage) = 0;
        KR_VIRTUAL_GETTER(IShaderStage*, NextStage) = 0;

        KR_VIRTUAL_GETTER(vector<unique_ptr<IExpression>>&, Expressions) = 0;
    };

    struct BasicShaderStage : IShaderStage
    {
        BasicShaderStage(ShaderContext* context, IShaderStage* previousStage, IShaderStage* nextStage);

        KR_NO_COPY_MOVE(BasicShaderStage);

        KR_GETTER(ShaderContext*, Context) override { return context; }
        KR_GETTER(vector<VariableDeclarationStatement*>&, Outputs) override { return outputs; }
        KR_GETTER(IShaderStage*, PreviousStage) override { return previousStage; }
        KR_GETTER(IShaderStage*, NextStage) override { return nextStage; }

        KR_GETTER(vector<unique_ptr<IExpression>>&, Expressions) { return expressions; }

    private:
        ShaderContext* context;
        mutable vector<VariableDeclarationStatement*> outputs;
        IShaderStage* previousStage;
        IShaderStage* nextStage;
        mutable vector<unique_ptr<IExpression>> expressions;
    };

    struct VertexShaderStage final : IShaderStage::With<VertexShaderStage, BasicShaderStage>
    {
        using ParentClass::ParentClass;

        KR_NO_COPY_MOVE(VertexShaderStage);

    };

    struct FragmentShaderStage final : IShaderStage::With<FragmentShaderStage, BasicShaderStage>
    {
        using ParentClass::ParentClass;

        KR_NO_COPY_MOVE(FragmentShaderStage);

    };

    struct ComputeShaderStage final : IShaderStage::With<ComputeShaderStage, BasicShaderStage>
    {
        using ParentClass::ParentClass;

        KR_NO_COPY_MOVE(ComputeShaderStage);

    };

    struct ShaderContext final
    {
        ShaderContext();

        KR_NO_COPY_MOVE(ShaderContext);

        KR_GETTER(BoolType*, BoolTy) { return boolTy; }
        KR_GETTER(IntType*, IntTy) { return intTy; }
        KR_GETTER(FloatType*, FloatTy) { return floatTy; }
        KR_GETTER(VoidType*, VoidTy) { return voidTy; }

        KR_ARRAY_GETTER2(VectorType*, VectorTy, IType* underlyingType, i32 count)
        {
            auto it = vectorMap.find({ underlyingType, count });
            if (it == vectorMap.end())
            {
                bool _;
                tie(it, _) = vectorMap.emplace(pair{ underlyingType, count }, CreateType<VectorType>(underlyingType, count));
            }
            return it->second;
        }

        KR_GETTER(VectorType*, Vector3Ty) { return GetVectorTy(GetFloatTy(), 3); }
        KR_GETTER(VectorType*, ColorTy) { return GetVectorTy(GetFloatTy(), 4); }

        VariableDeclarationStatement* DeclareVariable(string name, IType* type, IExpression* initializer = nullptr)
        {
            return CreateStatement<VariableDeclarationStatement>(move(name), type, initializer);
        }

        FunctionDeclarationStatement* DeclareFunction(string name, vector<VariableDeclarationStatement*> parameters, IType* returnType)
        {
            return CreateStatement<FunctionDeclarationStatement>(move(name), move(parameters), returnType);
        }

        template<class T, class... Args>
            requires (std::is_base_of_v<IShaderStage, T>)
        T* CreateShader(Args&&... args)
        {
            auto ptr = make_unique<T>(this, std::forward<Args>(args)...);
            auto result = ptr.get();
            shaderStages.emplace_back(std::move(ptr));
            return result;
        }

        template<class T, class... Args>
            requires (std::is_base_of_v<IExpression, T>)
        T* CreateExpression(Args&&... args)
        {
            unique_ptr<T> ptr;
            if constexpr (requires { new T(this, std::forward<Args>(args)...); })
				ptr = make_unique<T>(this, std::forward<Args>(args)...);
            else ptr = make_unique<T>(std::forward<Args>(args)...);
            auto result = ptr.get();
            expressions.emplace_back(std::move(ptr));
            return result;
        }

        template<class T, class... Args>
            requires (std::is_base_of_v<IStatement, T>)
        T* CreateStatement(Args&&... args)
        {
            auto ptr = make_unique<T>(std::forward<Args>(args)...);
            auto result = ptr.get();
            statements.emplace_back(std::move(ptr));
            return result;
        }

        void DestroyExpression(IExpression* e);

    private:

        mutable vector<unique_ptr<IType>> types;
        vector<unique_ptr<IExpression>> expressions;
        vector<unique_ptr<IStatement>> statements;
        vector<unique_ptr<IShaderStage>> shaderStages;

        BoolType* boolTy;
        IntType* intTy;
        FloatType* floatTy;
        DoubleType* doubleTy;

        mutable unordered_map<pair<IType*, i32>, VectorType*, TupleHasher> vectorMap;

        VoidType* voidTy;

        template<class T, class... Args>
            requires (std::is_base_of_v<IType, T>)
        T* CreateType(Args&&... args) const
        {
            auto ptr = make_unique<T>(std::forward<Args>(args)...);
            auto result = ptr.get();
            types.emplace_back(std::move(ptr));
            return result;
        }

    };

    struct ShaderPipeline
    {
        explicit ShaderPipeline(ShaderContext* context);

        KR_NO_COPY_MOVE(ShaderPipeline);

        KR_GETTER(ShaderContext*, Context) { return context; }
        KR_GETTER(VertexShaderStage*, VertexShader) { return vertexShader.get(); }
        KR_GETTER(FragmentShaderStage*, FragmentShader) { return fragmentShader.get(); }

        FaceCulling Culling;

    private:
        ShaderContext* context;
        unique_ptr<VertexShaderStage> vertexShader;
        unique_ptr<FragmentShaderStage> fragmentShader;
    };

}
