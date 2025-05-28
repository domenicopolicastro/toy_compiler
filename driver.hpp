#ifndef DRIVER_HPP
#define DRIVER_HPP
/************************* IR related modules ******************************/
#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/GlobalVariable.h"

extern llvm::LLVMContext *context;
extern llvm::Module      *module;
extern llvm::IRBuilder<> *builder;

/**************** C++ modules and generic data types ***********************/
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <variant>

#include "parser.hpp"

using namespace llvm;
Value* LogErrorV(const std::string& Str);

// Dichiarazione del prototipo yylex per Flex
// Flex va proprio a cercare YY_DECL perché
// deve espanderla (usando M4) nel punto appropriato
# define YY_DECL \
  yy::parser::symbol_type yylex (driver& drv)
// Per il parser è sufficiente una forward declaration
YY_DECL;

// Classe che organizza e gestisce il processo di compilazione
class driver
{
public:
  driver();
  std::map<std::string, AllocaInst*> NamedValues; // Tabella associativa in cui ogni 
            // chiave x è una variabile e il cui corrispondente valore è un'istruzione 
            // che alloca uno spazio di memoria della dimensione necessaria per 
            // memorizzare un variabile del tipo di x (nel nostro caso solo double)
  RootAST* root;      // A fine parsing "punta" alla radice dell'AST
  int parse (const std::string& f);
  std::string file;
  bool trace_parsing; // Abilita le tracce di debug el parser
  void scan_begin (); // Implementata nello scanner
  void scan_end ();   // Implementata nello scanner
  bool trace_scanning;// Abilita le tracce di debug nello scanner
  yy::location location; // Utillizata dallo scannar per localizzare i token
  void codegen();
};

typedef std::variant<std::string,double> lexval;
const lexval NONE = 0.0;

// Classe base dell'intera gerarchia di classi che rappresentano
// gli elementi del programma
class RootAST {
public:
  virtual ~RootAST() {};
  virtual lexval getLexVal() const {return NONE;};
  virtual Value *codegen(driver& drv) { return nullptr; };
};

class GlobalDeclAST;
class AssignExprAST;

// Classe che rappresenta la sequenza di statement
class SeqAST : public RootAST {
private:
  RootAST* first;
  RootAST* continuation;

public:
  SeqAST(RootAST* first, RootAST* continuation);
  Value *codegen(driver& drv) override;
};

/// ExprAST - Classe base per tutti i nodi espressione
class ExprAST : public RootAST {};

/// NumberExprAST - Classe per la rappresentazione di costanti numeriche
class NumberExprAST : public ExprAST {
private:
  double Val;

public:
  NumberExprAST(double Val);
  lexval getLexVal() const override;
  Value *codegen(driver& drv) override;
};

/// VariableExprAST - Classe per la rappresentazione di riferimenti a variabili
class VariableExprAST : public ExprAST {
private:
  std::string Name;
  
public:
  VariableExprAST(const std::string &Name);
  lexval getLexVal() const override;
  Value *codegen(driver& drv) override;
};

/// BinaryExprAST - Classe per la rappresentazione di operatori binari
class BinaryExprAST : public ExprAST {
private:
  char Op;
  ExprAST* LHS;
  ExprAST* RHS;

public:
  BinaryExprAST(char Op, ExprAST* LHS, ExprAST* RHS);
  Value *codegen(driver& drv) override;
};

/// CallExprAST - Classe per la rappresentazione di chiamate di funzione
class CallExprAST : public ExprAST {
private:
  std::string Callee;
  std::vector<ExprAST*> Args;  // ASTs per la valutazione degli argomenti

public:
  CallExprAST(std::string Callee, std::vector<ExprAST*> Args);
  lexval getLexVal() const override;
  Value *codegen(driver& drv) override;
};

/// IfExprAST
class IfExprAST : public ExprAST {
private:
  ExprAST* Cond;
  ExprAST* TrueExp;
  ExprAST* FalseExp;
public:
  IfExprAST(ExprAST* Cond, ExprAST* TrueExp, ExprAST* FalseExp);
  Value *codegen(driver& drv) override;
};


class ForExprAST : public ExprAST {
private:
    VarBindingAST* StartVar; // Per for (var i...
    ExprAST* StartExpr;      // Per for (i=...
    ExprAST* Cond;
    ExprAST* Step;
    ExprAST* Body;
public:
    // Questa è la dichiarazione del nuovo costruttore
    ForExprAST(VarBindingAST* StartVar, ExprAST* StartExpr, ExprAST* Cond, 
               ExprAST* Step, ExprAST* Body);
    
    Value* codegen(driver& drv) override;
};

class UnaryExprAST : public ExprAST {
private:
  char Op;
  ExprAST* Operand;
public:
  UnaryExprAST(char Op, ExprAST* Operand);
  Value *codegen(driver& drv) override;
};

class IfStmtAST : public RootAST {
private:
  ExprAST* Cond;
  BlockExprAST* Then;
  BlockExprAST* Else; // Può essere nullptr
public:
  IfStmtAST(ExprAST* Cond, BlockExprAST* Then, BlockExprAST* Else);
  Value *codegen(driver& drv) override;
};

/// BlockExprAST
class BlockExprAST : public ExprAST {
  std::vector<RootAST*> Stmts;
  ExprAST*            RetExpr;
public:
  BlockExprAST(std::vector<RootAST*> Stmts,
               ExprAST*            RetExpr)
    : Stmts(std::move(Stmts)),
      RetExpr(RetExpr)
  {}
  Value *codegen(driver& drv) override;
};

/// VarBindingAST
class VarBindingAST: public RootAST {
private:
  const std::string Name;
  ExprAST* Val;
public:
  VarBindingAST(const std::string Name, ExprAST* Val);
  AllocaInst *codegen(driver& drv) override;
  const std::string& getName() const;
};

/// PrototypeAST - Classe per la rappresentazione dei prototipi di funzione
/// (nome, numero e nome dei parametri; in questo caso il tipo è implicito
/// perché unico)
class PrototypeAST : public RootAST {
private:
  std::string Name;
  std::vector<std::string> Args;
  bool emitcode;

public:
  PrototypeAST(std::string Name, std::vector<std::string> Args);
  const std::vector<std::string> &getArgs() const;
  lexval getLexVal() const override;
  Function *codegen(driver& drv) override;
  void noemit();
};

/// FunctionAST - Classe che rappresenta la definizione di una funzione
class FunctionAST : public RootAST {
private:
  PrototypeAST* Proto;
  ExprAST* Body;
  bool external;
  
public:
  FunctionAST(PrototypeAST* Proto, ExprAST* Body);
  Function *codegen(driver& drv) override;
};


class GlobalDeclAST : public RootAST {
  std::string Name;
  int ArraySize; // 0 o valore negativo se non è un array, >0 se è un array

public:
  // Costruttore modificato
  GlobalDeclAST(const std::string &N, int size = 0) : Name(N), ArraySize(size) {}
  
  bool isArray() const { return ArraySize > 0; }
  int getArraySize() const { return ArraySize; }
  const std::string& getName() const { return Name; }

  Value *codegen(driver& drv) override; // Il codegen dovrà essere modificato
};
class AssignExprAST : public ExprAST {
  std::string LHS;
  ExprAST *RHS;
public:
  AssignExprAST(const std::string &L, ExprAST *R) : LHS(L), RHS(R) {}
  Value *codegen(driver& drv) override {
    Value *V = RHS->codegen(drv);
    if (!V) return nullptr;
    // locale?
    if (AllocaInst *A = drv.NamedValues[LHS]) {
      builder->CreateStore(V, A);
      return V;
    }
    // globale?
    if (GlobalVariable *G = module->getGlobalVariable(LHS)) {
      builder->CreateStore(V, G);
      return V;
    }
    return LogErrorV("Variabile non definita: "+LHS);
  }
};


#endif // ! DRIVER_HH
