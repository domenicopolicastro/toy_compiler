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
%type <ExprAST*> ifstmt
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
  // Modifica questa sezione per GLOBAL
  | GLOBAL IDENTIFIER { // Variabile globale scalare
        // Passiamo size 0 o -1 per indicare che non è un array, o un flag booleano
        $$ = new GlobalDeclAST($2, 0); // Assumiamo che GlobalDeclAST ora prenda una dimensione
    }
  | GLOBAL IDENTIFIER LBRACKET INTEGER RBRACKET { // Array globale
        if ($4 <= 0) { // Controllo sulla dimensione dell'array
            yy::parser::error(drv.location, "La dimensione dell'array deve essere positiva.");
            YYERROR; // Segnala un errore di parsing
        }
        $$ = new GlobalDeclAST($2, static_cast<int>($4)); // Passa la dimensione
    }
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
%left OR;
%left AND;
%right UMINUS NOT;
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
   | ifstmt                  
      { $$ = $1; }
 ;

ifstmt: // Ricorda che ifstmt ora restituisce ExprAST*
    IF "(" exp ")" exp { // Era blockexp, ora è exp
        $$ = new IfStmtAST($3, $5, nullptr); 
    }
  | IF "(" exp ")" exp ELSE exp { // Erano blockexp, ora sono exp
        $$ = new IfStmtAST($3, $5, $7); 
    }
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
  | IDENTIFIER LBRACKET exp RBRACKET ASSIGN exp { // <-- NUOVA REGOLA PER ASSEGNAZIONE AD ARRAY
    // $1: Nome Array (IDENTIFIER)
    // $3: Espressione Indice (exp dentro [])
    // $6: Espressione Valore (exp dopo =)
    $$ = new ArrayAssignExprAST($1, $3, $6); 
    }
  | simple_exp                { $$ = $1; }
  | expif                     { $$ = $1; }
;

// simple_exp contiene le espressioni non ambigue
simple_exp:
  NOT simple_exp %prec NOT    { $$ = new UnaryExprAST('!', $2); }
  | MINUS simple_exp %prec UMINUS { $$ = new UnaryExprAST('-', $2); }
  | PLUSPLUS simple_exp      { $$ = new UnaryExprAST('+', $2); }
  | simple_exp "+" simple_exp { $$ = new BinaryExprAST('+',$1,$3); }
  | simple_exp "-" simple_exp { $$ = new BinaryExprAST('-',$1,$3); }
  | simple_exp "*" simple_exp { $$ = new BinaryExprAST('*',$1,$3); }
  | simple_exp "/" simple_exp { $$ = new BinaryExprAST('/',$1,$3); }
  | simple_exp "<" simple_exp { $$ = new BinaryExprAST('<',$1,$3); }
  | simple_exp "==" simple_exp{ $$ = new BinaryExprAST('=',$1,$3); }
  | simple_exp AND simple_exp  { $$ = new BinaryExprAST('a', $1, $3); }
  | simple_exp OR simple_exp    { $$ = new BinaryExprAST('o', $1, $3); }
  | idexp                     { $$ = $1; }
  | "(" exp ")"               { $$ = $2; }
  | "number"                  { $$ = new NumberExprAST($1); }
  | INTEGER                   { $$ = new NumberExprAST(static_cast<double>($1)); }
  | blockexp                  { $$ = $1; }
  | forexpr                   { $$ = $1; }
  | ifstmt                    { $$ = $1; }
;

blockexp:
  /* 1) Blocco con valore di ritorno esplicito */
  "{" stmtlist ";" exp "}"
    {
      $$ = new BlockExprAST($2, $4);
    }
| /* 2) Blocco con una singola espressione come valore di ritorno */
  "{" exp "}"
    {
      std::vector<RootAST*> empty;
      $$ = new BlockExprAST(empty, $2);
    }
| /* 3) Blocco con solo statement (valore di ritorno di default 0.0) */
  "{" stmtlist "}"      // <-- AGGIUNGI QUESTA NUOVA REGOLA
    {
      // Crea un NumberExprAST(0.0) come espressione di ritorno di default
      $$ = new BlockExprAST($2, new NumberExprAST(0.0));
    }
;

forexpr:
    // Nuova regola per: for (var i = 1; ... )
    "for" "(" binding ";" exp ";" exp ")" exp { $$ = new ForExprAST($3, nullptr, $5, $7, $9); }
    // Vecchia regola modificata per: for (i = 1; ... )
  | "for" "(" exp ";" exp ";" exp ")" exp   { $$ = new ForExprAST(nullptr, $3, $5, $7, $9); }
;

binding:
   "var" IDENTIFIER "=" exp  { $$ = new VarBindingAST($2,$4); }

expif:
  exp "?" exp ":" exp { $$ = new IfExprAST($1,$3,$5); }

idexp:
  "id"                  { $$ = new VariableExprAST($1); }
| "id" "(" optexp ")"   { $$ = new CallExprAST($1,$3); };
| IDENTIFIER LBRACKET exp RBRACKET { // <-- NUOVA REGOLA PER ACCESSO ARRAY
      $$ = new ArrayAccessExprAST($1, $3); 
  }

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