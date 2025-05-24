#include "GlslShader.hpp"

namespace Kaey::Renderer::Shader
{
    GlArrayType::GlArrayType(GlType* underlyingType, i32 count) :
        underlyingType(underlyingType),
        count(count),
        name("{}[{}]"_f(underlyingType->Name, count > 0 ? "{}"_f(count) : ""))
    {

    }

    GlVectorType::GlVectorType(GlType* underlyingType, i32 count) :
        underlyingType(underlyingType),
        count(count),
        name("{}vec{}"_f(underlyingType->Is<GlFloatType>() ? "" : underlyingType->Name.substr(0, 1), count))
    {
        assert(count >= 2);
        assert(count <= 4);
    }

    GlMatrixType::GlMatrixType(i32 rowCount, i32 columnCount) :
        rowCount(rowCount),
        columnCount(columnCount),
        name(rowCount == columnCount ? "mat{}"_f(rowCount) : "mat{}x{}"_f(columnCount, rowCount)) //glsl is backward from convention in mathematics
    {

    }

    GlStructType::GlStructType(string name, vector<GlVariableDeclarationStatement*> fields) :
        name(move(name)),
        fields(move(fields))
    {

    }

    GlVariableDeclarationStatement::GlVariableDeclarationStatement(string name, GlType* type, GlExpression* initializer) :
        Name(move(name)),
        Type(type),
        Initializer(initializer)
    {

    }

    GlFunctionDeclarationStatement::GlFunctionDeclarationStatement(string name, vector<GlVariableDeclarationStatement*> parameters, GlType* returnType) :
        Name(move(name)),
        Parameters(move(parameters)),
        ReturnType(returnType)
    {

    }

    GlShaderContext::GlShaderContext() :
        boolTy(createType<GlBoolType>()),
        intTy(createType<GlIntType>()),
        floatTy(createType<GlFloatType>()),
        textureTy(createType<GlTextureType>()),
        textureCubeTy(createType<GlTextureCubeType>()),
        samplerTy(createType<GlSamplerType>()),
        voidTy(createType<GlVoidType>())
    {

    }

    GlVariableDeclarationStatement* GlShaderContext::DeclareVariable(string name, GlType* type, GlExpression* initializer)
    {
        return createStatement<GlVariableDeclarationStatement>(move(name), type, initializer);
    }

    GlFunctionDeclarationStatement* GlShaderContext::DeclareFunction(string name, vector<GlVariableDeclarationStatement*> parameters, GlType* returnType)
    {
        return createStatement<GlFunctionDeclarationStatement>(move(name), move(parameters), returnType);
    }

    GlStructType* GlShaderContext::DeclareStruct(string name, vector<GlVariableDeclarationStatement*> fields)
    {
        auto st = createType<GlStructType>(move(name), move(fields));
        structs.emplace_back(st);
        return st;
    }

    GlStatementPrinterVisitor::GlStatementPrinterVisitor(std::ostream* os) :
        os(os),
        space(0),
        spaceRequired(true)
    {

    }

    void GlStatementPrinterVisitor::Visit(GlVariableDeclarationStatement* statement)
    {
        println("{} {}{}", statement->Type->Name, statement->Name, statement->Initializer == nullptr ? ";" : " = {};"_f("(TODO)"));
    }

    void GlStatementPrinterVisitor::Visit(GlFunctionDeclarationStatement* statement)
    {
        print("{} {}(", statement->ReturnType->Name, statement->Name);
        for (auto [i, parameter] : statement->Parameters | indexed)
        {
            if (i != 0)
                print(", ");
            print("{} {}", parameter->Type->Name, parameter->Name);
            //if (parameter->Initializer)
            //    Visit(parameter->Initializer);
        }
        println(")");

        beginScope();
        for (auto s : statement->Statements)
            Dispatch(s);
        endScope();
    }

    void GlStatementPrinterVisitor::Visit(GlVertexShaderStage* shader)
    {
        for (auto st : shader->Context->Structs) if (st != shader->PushConstant && !st->Name.empty())
        {
            println("struct {}", st->Name);
            beginScope();
            for (auto f : st->Fields)
                println("{} {};", f->Type->Name, f->Name);
            endScopeSemi();
            println();
        }

        if (auto pc = shader->PushConstant)
        {
            println("layout(push_constant) uniform {}", pc->Name);
            beginScope();
            for (auto f : pc->Fields)
                println("{} {};", f->Type->Name, f->Name);
            endScopeSemi();
            println();
        }

        for (auto& [i, layout, tf, name, ty] : shader->Bindings)
        {
            print("layout(binding = {}", i);
            if (layout == GlBindingLayout::Std430)
                print(", std430");
            print(")");
            if (tf && GlTypeFlags::Restrict) print(" restrict");
            if (tf && GlTypeFlags::Readonly) print(" readonly");

            print(" {} ", tf && GlTypeFlags::Buffer ? "buffer" : "uniform");

            if (auto st = ty->As<GlStructType>())
            {
                println("{}", name);
                beginScope();
                for (auto f : st->Fields)
                    println("{} {};", f->Type->Name, f->Name);
                endScopeSemi();
            }
            else println("{} {};", ty->Name, name);

            println();
        }

        for (auto& [i, n, ty] : shader->Inputs)
            println("layout(location = {}) in {} {};", i, ty->Name, n);
        println();

        for (auto& [i, n, ty] : shader->Outputs)
            println("layout(location = {}) in {} {};", i, ty->Name, n);
        println();

        for (auto fn : shader->Functions) if (fn != shader->MainFunction)
        {
            Visit(fn);
            println();
        }

    }

    void GlStatementPrinterVisitor::Visit(GlFragmentShaderStage* shader)
    {
        println("#version 450\n");

        for (auto st : shader->Context->Structs) if (st != shader->PushConstant && !st->Name.empty())
        {
            println("struct {}", st->Name);
            beginScope();
            for (auto f : st->Fields)
                println("{} {};", f->Type->Name, f->Name);
            endScopeSemi();
            println();
        }

        if (auto pc = shader->PushConstant)
        {
            println("layout(push_constant) uniform {}", pc->Name);
            beginScope();
            for (auto f : pc->Fields)
                println("{} {};", f->Type->Name, f->Name);
            endScopeSemi();
            println();
        }

        for (auto& [i, layout, tf, name, ty] : shader->Bindings)
        {
            print("layout(binding = {}", i);
            if (layout == GlBindingLayout::Std430)
                print(", std430");
            print(")");
            if (tf && GlTypeFlags::Restrict) print(" restrict");
            if (tf && GlTypeFlags::Readonly) print(" readonly");

            print(" {} ", tf && GlTypeFlags::Buffer ? "buffer" : "uniform");

            if (auto st = ty->As<GlStructType>())
            {
                println("{}", name);
                beginScope();
                for (auto f : st->Fields)
                    println("{} {};", f->Type->Name, f->Name);
                endScopeSemi();
            }
            else println("{} {};", ty->Name, name);

            println();
        }

        for (auto& [i, n, ty] : shader->Inputs)
            println("layout(location = {}) in {} {};", i, ty->Name, n);
        println();

        for (auto& [i, n, ty] : shader->Outputs)
            println("layout(location = {}) in {} {};", i, ty->Name, n);
        println();

        for (auto fn : shader->Functions) if (fn != shader->MainFunction)
        {
            Visit(fn);
            println();
        }

    }

    void GlStatementPrinterVisitor::Dispatch(GlStatement* statement)
    {
        Kaey::Dispatch(statement, *this);
    }

    void GlStatementPrinterVisitor::Dispatch(GlShaderStage* stage)
    {
        Kaey::Dispatch(stage, *this);
    }

    void GlStatementPrinterVisitor::println()
    {
        *os << '\n';
        spaceRequired = true;
    }

    void GlStatementPrinterVisitor::pushSpace()
    {
        ++space;
    }

    void GlStatementPrinterVisitor::popSpace()
    {
        --space;
    }

    void GlStatementPrinterVisitor::beginScope()
    {
        println("{{");
        pushSpace();
    }

    void GlStatementPrinterVisitor::endScope()
    {
        popSpace();
        println("}}");
    }

    void GlStatementPrinterVisitor::endScopeSemi()
    {
        popSpace();
        println("}};");
    }

}
