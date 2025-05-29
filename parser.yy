%skeleton "lalr1.cc"
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
  class GlobalDeclAST;
  class AssignExprAST;
  class ArrayAccessExprAST;
  class ArrayAssignExprAST;
  class IfStmtAST;
  class ForExprAST;
  class UnaryExprAST;
  class IfExprAST;
}

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
  IF         "if"
  ELSE       "else"
  OR         "or"
  AND        "and"
  NOT        "not"
  LBRACKET   "["
  RBRACKET   "]"
;

%token <std::string> IDENTIFIER "id"
%token <double> NUMBER "number"
%token <long long> INTEGER "integer"

%type <ExprAST*> exp
%type <ExprAST*> simple_exp_terms
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
%type <ExprAST*> ifstmt
%type <std::vector<RootAST*>> stmtlist

%%
%start startsymb;

startsymb:
  program                   { drv.root = $1; }

program:
  %empty                    { $$ = new SeqAST(nullptr,nullptr); }
| top ";" program           { $$ = new SeqAST($1,$3); };

top:
    %empty                                              { $$ = nullptr; }
  | definition                                          { $$ = $1; }
  | external                                            { $$ = $1; }
  | GLOBAL IDENTIFIER                                   { $$ = new GlobalDeclAST($2, 0); }
  | GLOBAL IDENTIFIER LBRACKET INTEGER RBRACKET     {
                                                          if ($4 <= 0) {
                                                              yy::parser::error(drv.location, "La dimensione dell'array deve essere positiva.");
                                                              YYERROR;
                                                          }
                                                          $$ = new GlobalDeclAST($2, static_cast<int>($4));
                                                      }
;

definition:
  DEF proto exp             { $$ = new FunctionAST($2,$3); $2->noemit(); };

external:
  EXTERN proto              { $$ = $2; };

proto:
  IDENTIFIER "(" idseq ")" { $$ = new PrototypeAST($1,$3);  };

idseq:
  %empty                    { std::vector<std::string> args; $$ = args; }
| IDENTIFIER idseq          { $2.insert($2.begin(),$1); $$ = $2; };

%right ASSIGN;
%right QMARK;
%left OR;
%left AND;
%right UMINUS NOT;
%right PLUSPLUS;
%left LT EQ;
%left PLUS MINUS;
%left STAR SLASH;

stmt:
  binding                   { $$ = (RootAST*)$1; }
| exp                       { $$ = (RootAST*)$1; }
;

ifstmt:
  IF "(" exp ")" exp {
    $$ = new IfStmtAST($3, $5, nullptr);
  }
| IF "(" exp ")" exp ELSE exp {
    $$ = new IfStmtAST($3, $5, $7);
  }
;

stmtlist:
  %empty                    { $$ = std::vector<RootAST*>(); }
| stmt                      { $$ = std::vector<RootAST*>{ $1 }; }
| stmtlist ";" stmt         {
                                $$ = $1;
                                $$.push_back($3);
                              }
;

exp:
  IDENTIFIER ASSIGN exp                          { $$ = new AssignExprAST($1,$3); }
| IDENTIFIER LBRACKET exp RBRACKET ASSIGN exp   { $$ = new ArrayAssignExprAST($1, $3, $6); }
| simple_exp_terms                              { $$ = $1; }
| expif                                         { $$ = $1; }
| ifstmt                                        { $$ = $1; }
| forexpr                                       { $$ = $1; }
;

simple_exp_terms:
  NOT simple_exp_terms %prec NOT      { $$ = new UnaryExprAST('!', $2); }
| MINUS simple_exp_terms %prec UMINUS { $$ = new UnaryExprAST('-', $2); }
| PLUSPLUS simple_exp_terms           { $$ = new UnaryExprAST('+', $2); }
| simple_exp_terms PLUS simple_exp_terms  { $$ = new BinaryExprAST('+',$1,$3); }
| simple_exp_terms MINUS simple_exp_terms { $$ = new BinaryExprAST('-',$1,$3); }
| simple_exp_terms STAR simple_exp_terms  { $$ = new BinaryExprAST('*',$1,$3); }
| simple_exp_terms SLASH simple_exp_terms { $$ = new BinaryExprAST('/',$1,$3); }
| simple_exp_terms LT simple_exp_terms    { $$ = new BinaryExprAST('<',$1,$3); }
| simple_exp_terms EQ simple_exp_terms    { $$ = new BinaryExprAST('=',$1,$3); }
| simple_exp_terms AND simple_exp_terms   { $$ = new BinaryExprAST('a', $1, $3); }
| simple_exp_terms OR simple_exp_terms    { $$ = new BinaryExprAST('o', $1, $3); }
| idexp                               { $$ = $1; }
| LPAREN exp RPAREN                   { $$ = $2; }
| NUMBER                              { $$ = new NumberExprAST($1); }
| INTEGER                             { $$ = new NumberExprAST(static_cast<double>($1)); }
| blockexp                            { $$ = $1; }
;

blockexp:
  LBRACE stmtlist ";" exp RBRACE  { $$ = new BlockExprAST($2, $4); }
| LBRACE exp RBRACE               {
                                    std::vector<RootAST*> empty;
                                    $$ = new BlockExprAST(empty, $2);
                                  }
| LBRACE stmtlist RBRACE          { $$ = new BlockExprAST($2, new NumberExprAST(0.0)); }
;

forexpr:
  FOR LPAREN binding SEMICOLON exp SEMICOLON exp RPAREN exp { $$ = new ForExprAST($3, nullptr, $5, $7, $9); }
| FOR LPAREN exp SEMICOLON exp SEMICOLON exp RPAREN exp    { $$ = new ForExprAST(nullptr, $3, $5, $7, $9); }
;

binding:
  VAR IDENTIFIER ASSIGN exp { $$ = new VarBindingAST($2,$4); }

expif:
  exp QMARK exp COLON exp %prec QMARK { $$ = new IfExprAST($1,$3,$5); }
;

idexp:
  IDENTIFIER                          { $$ = new VariableExprAST($1); }
| IDENTIFIER LPAREN optexp RPAREN     { $$ = new CallExprAST($1,$3); }
| IDENTIFIER LBRACKET exp RBRACKET    { $$ = new ArrayAccessExprAST($1, $3); }
;

optexp:
  %empty                    { std::vector<ExprAST*> args; $$ = args; }
| explist                   { $$ = $1; };

explist:
  exp                       {
                                std::vector<ExprAST*> args;
                                args.push_back($1);
                                $$ = args;
                              }
| exp COMMA explist         { $3.insert($3.begin(), $1); $$ = $3; };

%%

void
yy::parser::error (const location_type& l, const std::string& m)
{
  std::cerr << l << ": " << m << '\\n';
}
