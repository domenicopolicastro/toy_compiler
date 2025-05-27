%skeleton "lalr1.cc" /* -*- C++ -*- */
%require "3.2"
%defines

%define api.token.constructor
%define api.location.file none
%define api.value.type variant
%define parse.assert

%code requires {
  # include <string>
  #include <exception>
  class driver;
  class RootAST;
  class ExprAST;
  class NumberExprAST;
  class VariableExprAST;
  class CallExprAST;
  class FunctionAST;
  class SeqAST;
  class PrototypeAST;
  class BlockExprAST;
  class VarBindingAST;
}

// The parsing context.
%param { driver& drv }

%locations

%define parse.trace
%define parse.error verbose

%code {
# include "driver.hpp"
}

%define api.token.prefix {TOK_}
%token
  END  0  "end of file"
  SEMICOLON  ";"
  COMMA      ","
  MINUS      "-"
  PLUS       "+"
  STAR       "*"
  SLASH      "/"
  LPAREN     "("
  RPAREN     ")"
  QMARK      "?"
  COLON      ":"
  LT         "<"
  EQ         "=="
  ASSIGN     "="
  LBRACE     "{"
  RBRACE     "}"
  EXTERN     "extern"
  DEF        "def"
  VAR        "var"
  GLOBAL     "global"
  FOR        "for"
  PLUSPLUS   "++"
;

%token <std::string> IDENTIFIER "id"
%token <double> NUMBER "number"

/* --- INIZIO MODIFICHE IMPORTANTI --- */

// Dichiarazioni dei tipi per i non-terminali
%type <ExprAST*> exp
%type <ExprAST*> simple_exp  // <-- AGGIUNTO: Dichiarazione per il nuovo simbolo
%type <ExprAST*> idexp
%type <ExprAST*> expif
%type <ExprAST*> forexpr
%type <ExprAST*> blockexp
%type <std::vector<ExprAST*>> optexp
%type <std::vector<ExprAST*>> explist
%type <RootAST*> program
%type <RootAST*> top
%type <FunctionAST*> definition
%type <PrototypeAST*> external
%type <PrototypeAST*> proto
%type <std::vector<std::string>> idseq
%type <VarBindingAST*> binding
%type <RootAST*> stmt
%type <std::vector<RootAST*>> stmtlist
// %type <ExprAST*> condexp   <-- RIMOSSO: Non più necessario
// %type <std::vector<VarBindingAST*>> vardefs <-- RIMOSSO: Non utilizzato

/* --- FINE MODIFICHE IMPORTANTI --- */


%%
%start startsymb;

startsymb:
program                 { drv.root = $1; }

program:
  %empty                { $$ = new SeqAST(nullptr,nullptr); }
|  top ";" program      { $$ = new SeqAST($1,$3); };

top:
    %empty                   { $$ = nullptr; }
  | definition               { $$ = $1; }
  | external                 { $$ = $1; }
  | GLOBAL IDENTIFIER        { $$ = new GlobalDeclAST($2); }
;

definition:
  "def" proto exp       { $$ = new FunctionAST($2,$3); $2->noemit(); };

external:
  "extern" proto        { $$ = $2; };

proto:
  "id" "(" idseq ")"    { $$ = new PrototypeAST($1,$3);  };

idseq:
  %empty                { std::vector<std::string> args;
                         $$ = args; }
| "id" idseq            { $2.insert($2.begin(),$1); $$ = $2; };

// Definizione della precedenza degli operatori
%right ASSIGN;
%right QMARK; // L'operatore ternario ha bassa precedenza
%left ":";
%right PLUSPLUS;  
%left "<" "==";
%left "+" "-";
%left "*" "/";

 stmt:
     binding
       { $$ = (RootAST*)$1; }
   | IDENTIFIER ASSIGN exp
       { $$ = (RootAST*) new AssignExprAST($1,$3); }
   | exp
       { $$ = (RootAST*)$1; }
 ;

stmtlist:
    /* empty */            %empty
                            { $$ = std::vector<RootAST*>(); }
  | stmt                    /* un unico statement senza punto-e-virgola */
                            { $$ = std::vector<RootAST*>{ $1 }; }
  | stmtlist ";" stmt      /* lista estesa */
                            {
                              $$ = $1;
                              $$ .push_back($3);
                            }
;

/* --- INIZIO GRAMMATICA ESPRESSIONI CORRETTA (NON DUPLICATA) --- */

exp:
    IDENTIFIER ASSIGN exp     { $$ = new AssignExprAST($1,$3); }
  | simple_exp                { $$ = $1; }
  | expif                     { $$ = $1; }
;

// simple_exp contiene le espressioni non ambigue
simple_exp:
  PLUSPLUS simple_exp      { $$ = new UnaryExprAST('+', $2); }
  | simple_exp "+" simple_exp { $$ = new BinaryExprAST('+',$1,$3); }
  | simple_exp "-" simple_exp { $$ = new BinaryExprAST('-',$1,$3); }
  | simple_exp "*" simple_exp { $$ = new BinaryExprAST('*',$1,$3); }
  | simple_exp "/" simple_exp { $$ = new BinaryExprAST('/',$1,$3); }
  | simple_exp "<" simple_exp { $$ = new BinaryExprAST('<',$1,$3); }
  | simple_exp "==" simple_exp{ $$ = new BinaryExprAST('=',$1,$3); }
  | idexp                     { $$ = $1; }
  | "(" exp ")"               { $$ = $2; }
  | "number"                  { $$ = new NumberExprAST($1); }
  | blockexp                  { $$ = $1; }
  | forexpr                   { $$ = $1; }
;

blockexp:
  "{" stmtlist ";" exp "}"
    {
      $$ = new BlockExprAST($2, $4);
    }
| "{" exp "}"
    {
      std::vector<RootAST*> empty;
      $$ = new BlockExprAST(empty, $2);
    }
;

forexpr:
  "for" "(" exp ";" exp ";" exp ")" exp { $$ = new ForExprAST($3, $5, $7, $9); }
;

binding:
   "var" IDENTIFIER "=" exp  { $$ = new VarBindingAST($2,$4); }

expif:
  exp "?" exp ":" exp { $$ = new IfExprAST($1,$3,$5); }

idexp:
  "id"                  { $$ = new VariableExprAST($1); }
| "id" "(" optexp ")"   { $$ = new CallExprAST($1,$3); };

optexp:
  %empty                { std::vector<ExprAST*> args;
                         $$ = args; }
| explist               { $$ = $1; };

explist:
  exp                   { std::vector<ExprAST*> args;
                         args.push_back($1);
                         $$ = args;
                        }
| exp "," explist       { $3.insert($3.begin(), $1); $$ = $3; };

/* --- FINE GRAMMATICA ESPRESSIONI CORRETTA --- */
 
%%

void
yy::parser::error (const location_type& l, const std::string& m)
{
  std::cerr << l << ": " << m << '\\n';
}