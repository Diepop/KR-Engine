#include "Shader.hpp"

namespace Kaey::Renderer::Shader
{
    VectorType::VectorType(IType* underlyingType, i32 count) :
        UnderlyingType(underlyingType),
        Count(count)
    {
        auto sizeCheck = count > 1 && count <= 4;
        assert(sizeCheck);
        if (!sizeCheck) throw std::invalid_argument("Invalid Argument: 'count'");
        auto typeCheck = underlyingType->IsOr<BoolType, IntType, FloatType, DoubleType>();
        assert(typeCheck);
        if (!typeCheck) throw std::invalid_argument("Invalid Argument: 'underlyingType'");
    }

    IntExpression::IntExpression(ShaderContext* context) : Value(0), context(context)
    {

    }

    KR_GETTER_DEF(IntExpression, Type)
    {
        return Context->IntTy;
    }

    FloatExpression::FloatExpression(ShaderContext* context) : Value(0), context(context)
    {

    }

    KR_GETTER_DEF(FloatExpression, Type)
    {
        return Context->FloatTy;
    }

    VectorExpression::VectorExpression(ShaderContext* context) : context(context), type(context->GetVectorTy(context->FloatTy, 3))
    {

    }

    ColorExpression::ColorExpression(ShaderContext* context) :
		Value(1_xyzw),
		context(context),
		type(context->GetVectorTy(context->FloatTy, 4))
    {

    }

    BinaryExpression::BinaryExpression(IExpression* leftOperand, OperatorSymbol op, IExpression* rightOperand) :
        LeftOperand(leftOperand),
        Operator(op),
        RightOperand(rightOperand)
    {

    }

    AttributeExpression::AttributeExpression(string name, IType* type) :
		name(move(name)),
		type(type)
    {

    }

    FunctionCallExpression::FunctionCallExpression(ShaderContext* context, string name, vector<IExpression*> arguments, IType* type) :
        context(context),
		name(move(name)),
		arguments(move(arguments)),
		type(type)
    {

    }

    VariableDeclarationStatement::VariableDeclarationStatement(string name, IType* type, IExpression* initializer) :
        Name(move(name)),
        Type(type),
        Initializer(initializer)
    {

    }

    FunctionDeclarationStatement::FunctionDeclarationStatement(string name, vector<VariableDeclarationStatement*> parameters, IType* returnType) :
        Name(move(name)),
        Parameters(move(parameters)),
        ReturnType(returnType)
    {

    }

    IfStatement::IfStatement(IStatement* trueStatement, IStatement* falseStatement) :
        TrueStatement(trueStatement),
        FalseStatement(falseStatement)
    {
        
    }

    BasicShaderStage::BasicShaderStage(ShaderContext* context, IShaderStage* previousStage, IShaderStage* nextStage) :
		context(context),
		previousStage(previousStage),
		nextStage(nextStage)
    {

    }

    ShaderContext::ShaderContext() :
        boolTy(CreateType<BoolType>()),
        intTy(CreateType<IntType>()),
        floatTy(CreateType<FloatType>()),
        doubleTy(CreateType<DoubleType>()),
        voidTy(CreateType<VoidType>())
    {

    }

    void ShaderContext::DestroyExpression(IExpression* e)
    {
        erase_if(expressions, [=](auto& ptr) { return ptr.get() == e; });
    }

    ShaderPipeline::ShaderPipeline(ShaderContext* context) :
		Culling(FaceCulling::Back),
		context(context),
        vertexShader((VertexShaderStage*)operator new(sizeof(VertexShaderStage))),
        fragmentShader((FragmentShaderStage*)operator new(sizeof(FragmentShaderStage)))
    {
        std::construct_at(vertexShader.get(), context, nullptr, fragmentShader.get());
        std::construct_at(fragmentShader.get(), context, vertexShader.get(), nullptr);
    }

}
