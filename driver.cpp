#include "driver.hpp"
#include "parser.hpp"

// Generazione di un'istanza per ciascuna della classi LLVMContext,
// Module e IRBuilder. Nel caso di singolo modulo è sufficiente
LLVMContext *context = new LLVMContext;
Module *module = new Module("Kaleidoscope", *context);
IRBuilder<> *builder = new IRBuilder(*context);

Value *LogErrorV(const std::string& Str) {
  std::cerr << Str << std::endl;
  return nullptr;
}

/* Il codice seguente sulle prime non è semplice da comprendere.
   Esso definisce una utility (funzione C++) con due parametri:
   1) la rappresentazione di una funzione llvm IR, e
   2) il nome per un registro SSA
   La chiamata di questa utility restituisce un'istruzione IR che alloca un double
   in memoria e ne memorizza il puntatore in un registro SSA cui viene attribuito
   il nome passato come secondo parametro. L'istruzione verrà scritta all'inizio
   dell'entry block della funzione passata come primo parametro.
   Si ricordi che le istruzioni sono generate da un builder. Per non
   interferire con il builder globale, la generazione viene dunque effettuata
   con un builder temporaneo TmpB
*/
static AllocaInst *CreateEntryBlockAlloca(Function *fun, StringRef VarName) {
  IRBuilder<> TmpB(&fun->getEntryBlock(), fun->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(*context), nullptr, VarName);
}

// Implementazione del costruttore della classe driver
driver::driver(): trace_parsing(false), trace_scanning(false) {};

// Implementazione del metodo parse
int driver::parse (const std::string &f) {
  file = f;                    // File con il programma
  location.initialize(&file);  // Inizializzazione dell'oggetto location
  scan_begin();                // Inizio scanning (ovvero apertura del file programma)
  yy::parser parser(*this);    // Istanziazione del parser
  parser.set_debug_level(trace_parsing); // Livello di debug del parsed
  int res = parser.parse();    // Chiamata dell'entry point del parser
  scan_end();                  // Fine scanning (ovvero chiusura del file programma)
  return res;
}

// Implementazione del metodo codegen, che è una "semplice" chiamata del 
// metodo omonimo presente nel nodo root (il puntatore root è stato scritto dal parser)
void driver::codegen() {
  root->codegen(*this);
  module->print(errs(), nullptr);
};

/************************* Sequence tree **************************/
SeqAST::SeqAST(RootAST* first, RootAST* continuation):
  first(first), continuation(continuation) {};

// La generazione del codice per una sequenza è banale:
// mediante chiamate ricorsive viene generato il codice di first e 
// poi quello di continuation (con gli opportuni controlli di "esistenza")
Value *SeqAST::codegen(driver& drv) {
  if (first != nullptr) {
    Value *f = first->codegen(drv);
  } else {
    if (continuation == nullptr) return nullptr;
  }
  Value *c = continuation->codegen(drv);
  return nullptr;
};

/********************* Number Expression Tree *********************/
NumberExprAST::NumberExprAST(double Val): Val(Val) {};

lexval NumberExprAST::getLexVal() const {
  // Non utilizzata, Inserita per continuità con versione precedente
  lexval lval = Val;
  return lval;
};

// Non viene generata un'struzione; soltanto una costante LLVM IR
// corrispondente al valore float memorizzato nel nodo
// La costante verrà utilizzata in altra parte del processo di generazione
// Si noti che l'uso del contesto garantisce l'unicità della costanti 
Value *NumberExprAST::codegen(driver& drv) {  
  return ConstantFP::get(*context, APFloat(Val));
};

/******************** Variable Expression Tree ********************/
VariableExprAST::VariableExprAST(const std::string &Name): Name(Name) {};

lexval VariableExprAST::getLexVal() const {
  lexval lval = Name;
  return lval;
};

// NamedValues è una tabella che ad ogni variabile (che, in Kaleidoscope1.0, 
// può essere solo un parametro di funzione) associa non un valore bensì
// la rappresentazione di una funzione che alloca memoria e restituisce in un
// registro SSA il puntatore alla memoria allocata. Generare il codice corrispondente
// ad una varibile equivale dunque a recuperare il tipo della variabile 
// allocata e il nome del registro e generare una corrispondente istruzione di load
// Negli argomenti della CreateLoad ritroviamo quindi: (1) il tipo allocato, (2) il registro
// SSA in cui è stato messo il puntatore alla memoria allocata (si ricordi che A è
// l'istruzione ma è anche il registro, vista la corrispodenza 1-1 fra le due nozioni), (3)
// il nome del registro in cui verrà trasferito il valore dalla memoria
Value *VariableExprAST::codegen(driver& drv) {
  // 1) prova a leggere una variabile locale (allocata in entry block)
  if (AllocaInst *A = drv.NamedValues[Name]) {
    return builder->CreateLoad(A->getAllocatedType(), A, Name.c_str());
  }
  // 2) poi prova tra le globali del modulo
  if (GlobalVariable *GV = module->getGlobalVariable(Name)) {
    // GlobalVariable::getValueType() o getType()->getPointerElementType()
    Type *elemTy = GV->getValueType(); 
    return builder->CreateLoad(elemTy, GV, Name.c_str());
  }
  // se ancora niente, è davvero un errore
  return LogErrorV("Variabile non definita: " + Name);
}

/******************** Binary Expression Tree **********************/
BinaryExprAST::BinaryExprAST(char Op, ExprAST* LHS, ExprAST* RHS):
  Op(Op), LHS(LHS), RHS(RHS) {};

// La generazione del codice in questo caso è di facile comprensione.
// Vengono ricorsivamente generati il codice per il primo e quello per il secondo
// operando. Con i valori memorizzati in altrettanti registri SSA si
// costruisce l'istruzione utilizzando l'opportuno operatore

// In driver.cpp

Value *BinaryExprAST::codegen(driver& drv) {
  if (Op == 'a') { // Gestione di 'and' con short-circuiting
      Value *L = LHS->codegen(drv);
      if (!L) return nullptr;

      // Converti LHS in booleano i1 (true se L != 0.0)
      L = builder->CreateFCmpONE(L, ConstantFP::get(*context, APFloat(0.0)), "tobool_l_and");
      
      Function *TheFunction = builder->GetInsertBlock()->getParent();
      
      // Blocco per valutare RHS (solo se LHS è true)
      BasicBlock *RHSBlock = BasicBlock::Create(*context, "rhs_and", TheFunction);
      // Blocco dove il risultato di 'and' viene finalizzato
      BasicBlock *MergeBlock = BasicBlock::Create(*context, "and_cont", TheFunction);

      // Blocco corrente prima del branch (dove LHS è stato valutato)
      BasicBlock *LHSBlock = builder->GetInsertBlock();
      // Se L è true (1), vai a RHSBlock per valutare RHS.
      // Se L è false (0), il risultato di 'and' è false, salta direttamente a MergeBlock.
      builder->CreateCondBr(L, RHSBlock, MergeBlock);

      // Codice per il blocco RHSBlock
      builder->SetInsertPoint(RHSBlock);
      Value *R = RHS->codegen(drv); // Valuta RHS
      if (!R) return nullptr;
      // Converti RHS in booleano i1
      R = builder->CreateFCmpONE(R, ConstantFP::get(*context, APFloat(0.0)), "tobool_r_and");
      builder->CreateBr(MergeBlock); // Salta al blocco di merge
      // Aggiorna RHSBlock per il PHI node (è il blocco da cui arriviamo se RHS è stato valutato)
      RHSBlock = builder->GetInsertBlock();

      // Codice per il blocco MergeBlock
      builder->SetInsertPoint(MergeBlock);
      PHINode *PN = builder->CreatePHI(Type::getInt1Ty(*context), 2, "and_phi");
      // Se arriviamo da LHSBlock (significa che L era false), il risultato di 'and' è false (0).
      PN->addIncoming(ConstantInt::get(Type::getInt1Ty(*context), 0), LHSBlock);
      // Se arriviamo da RHSBlock (significa che L era true), il risultato di 'and' è il valore di R.
      PN->addIncoming(R, RHSBlock);
      
      // Converti il risultato booleano (i1) in double (0.0 o 1.0)
      return builder->CreateUIToFP(PN, Type::getDoubleTy(*context), "bool_to_double");

  } else if (Op == 'o') { // Gestione di 'or' con short-circuiting (codice esistente, verificato)
      Value *L = LHS->codegen(drv);
      if (!L) return nullptr;

      // Converti LHS in booleano i1 (true se L != 0.0)
      L = builder->CreateFCmpONE(L, ConstantFP::get(*context, APFloat(0.0)), "tobool_l_or");
      
      Function *TheFunction = builder->GetInsertBlock()->getParent();
      
      BasicBlock *RHSBlock = BasicBlock::Create(*context, "rhs_or", TheFunction);
      BasicBlock *MergeBlock = BasicBlock::Create(*context, "or_cont", TheFunction);

      BasicBlock *LHSBlock = builder->GetInsertBlock();
      // Se L è true (1), il risultato di 'or' è true, salta direttamente a MergeBlock.
      // Se L è false (0), vai a RHSBlock per valutare RHS.
      builder->CreateCondBr(L, MergeBlock, RHSBlock);

      builder->SetInsertPoint(RHSBlock);
      Value *R = RHS->codegen(drv);
      if (!R) return nullptr;
      // Converti RHS in booleano i1
      R = builder->CreateFCmpONE(R, ConstantFP::get(*context, APFloat(0.0)), "tobool_r_or");
      builder->CreateBr(MergeBlock);
      RHSBlock = builder->GetInsertBlock();

      builder->SetInsertPoint(MergeBlock);
      PHINode *PN = builder->CreatePHI(Type::getInt1Ty(*context), 2, "or_phi");
      // Se arriviamo da LHSBlock (significa che L era true), il risultato di 'or' è true (1).
      PN->addIncoming(ConstantInt::get(Type::getInt1Ty(*context), 1), LHSBlock);
      // Se arriviamo da RHSBlock (significa che L era false), il risultato di 'or' è il valore di R.
      PN->addIncoming(R, RHSBlock);
      
      // Converti il risultato booleano (i1) in double (0.0 o 1.0)
      return builder->CreateUIToFP(PN, Type::getDoubleTy(*context), "bool_to_double");
  }

  // Codice per tutti gli altri operatori binari (aritmetici e di comparazione)
  // Questi vengono valutati solo se Op non è 'a' (and) o 'o' (or)
  Value *L = LHS->codegen(drv);
  Value *R_val = RHS->codegen(drv); // Rinominato R in R_val per evitare shadowing con la R nei blocchi 'and'/'or'
  if (!L || !R_val) 
     return nullptr;

  switch (Op) {
  case '+':
    return builder->CreateFAdd(L,R_val,"addres");
  case '-':
    return builder->CreateFSub(L,R_val,"subres");
  case '*':
    return builder->CreateFMul(L,R_val,"mulres");
  case '/':
    return builder->CreateFDiv(L,R_val,"addres");
  case '<':
    L = builder->CreateFCmpULT(L,R_val,"cmptmp");
    // Converti il risultato booleano (i1) in double (0.0 o 1.0)
    return builder->CreateUIToFP(L, Type::getDoubleTy(*context), "booltmp");
  case '=': // Assumendo che '=' sia per '==' come da token EQ
    L = builder->CreateFCmpUEQ(L,R_val,"cmptmp");
    // Converti il risultato booleano (i1) in double (0.0 o 1.0)
    return builder->CreateUIToFP(L, Type::getDoubleTy(*context), "booltmp");
  default:  
    return LogErrorV("Operatore binario non supportato: " + std::string(1, Op));
  }
};

/********************* Call Expression Tree ***********************/
/* Call Expression Tree */
CallExprAST::CallExprAST(std::string Callee, std::vector<ExprAST*> Args):
  Callee(Callee),  Args(std::move(Args)) {};

lexval CallExprAST::getLexVal() const {
  lexval lval = Callee;
  return lval;
};

Value* CallExprAST::codegen(driver& drv) {
  // La generazione del codice corrispondente ad una chiamata di funzione
  // inizia cercando nel modulo corrente (l'unico, nel nostro caso) una funzione
  // il cui nome coincide con il nome memorizzato nel nodo dell'AST
  // Se la funzione non viene trovata (e dunque non è stata precedentemente definita)
  // viene generato un errore
  Function *CalleeF = module->getFunction(Callee);
  if (!CalleeF)
     return LogErrorV("Funzione non definita");
  // Il secondo controllo è che la funzione recuperata abbia tanti parametri
  // quanti sono gi argomenti previsti nel nodo AST
  if (CalleeF->arg_size() != Args.size())
     return LogErrorV("Numero di argomenti non corretto");
  // Passato con successo anche il secondo controllo, viene predisposta
  // ricorsivamente la valutazione degli argomenti presenti nella chiamata 
  // (si ricordi che gli argomenti possono essere espressioni arbitarie)
  // I risultati delle valutazioni degli argomenti (registri SSA, come sempre)
  // vengono inseriti in un vettore, dove "se li aspetta" il metodo CreateCall
  // del builder, che viene chiamato subito dopo per la generazione dell'istruzione
  // IR di chiamata
  std::vector<Value *> ArgsV;
  for (auto arg : Args) {
     ArgsV.push_back(arg->codegen(drv));
     if (!ArgsV.back())
        return nullptr;
  }
  return builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

/************************* If Expression Tree *************************/
IfExprAST::IfExprAST(ExprAST* Cond, ExprAST* TrueExp, ExprAST* FalseExp):
   Cond(Cond), TrueExp(TrueExp), FalseExp(FalseExp) {};
   
// In driver.cpp - IfExprAST::codegen
Value* IfExprAST::codegen(driver& drv) {
    
    Value* CondV = Cond->codegen(drv);
    if (!CondV)
        return nullptr;

    // Modifica la stringa qui:
    CondV = builder->CreateFCmpONE(CondV, ConstantFP::get(*context, APFloat(0.0)), "ifcond_ULTRA_DEBUG"); 

    Function *function = builder->GetInsertBlock()->getParent();

    // Modifica anche un nome di blocco per sicurezza:
    BasicBlock *TrueBB =  BasicBlock::Create(*context, "trueexp_DBG", function); 
    BasicBlock *FalseBB = BasicBlock::Create(*context, "falseexp_DBG", function); 
    BasicBlock *MergeBB = BasicBlock::Create(*context, "endcond_DBG", function);  

    builder->CreateCondBr(CondV, TrueBB, FalseBB);

    builder->SetInsertPoint(TrueBB);
    Value *TrueVal = TrueExp->codegen(drv);
    if (!TrueVal) return nullptr;
    builder->CreateBr(MergeBB);
    TrueBB = builder->GetInsertBlock(); 

    builder->SetInsertPoint(FalseBB);
    Value *FalseVal = FalseExp->codegen(drv);
    if (!FalseVal) return nullptr;
    builder->CreateBr(MergeBB);
    FalseBB = builder->GetInsertBlock(); 

    builder->SetInsertPoint(MergeBB);
    // E il nome del PHI node:
    PHINode *PN = builder->CreatePHI(Type::getDoubleTy(*context), 2, "condval_DBG"); 
    PN->addIncoming(TrueVal, TrueBB);
    PN->addIncoming(FalseVal, FalseBB);
    return PN;
};


ForExprAST::ForExprAST(VarBindingAST* StartVar, ExprAST* StartExpr, ExprAST* Cond,
                       ExprAST* Step, ExprAST* Body)
    : StartVar(StartVar), StartExpr(StartExpr), Cond(Cond), Step(Step), Body(Body) {}
/************************* For Expression Tree *************************/
Value* ForExprAST::codegen(driver& drv) {
    // --- Gestione dello Scope e Inizializzazione ---
    
    AllocaInst* oldVal = nullptr;
    std::string varName;

    // Se il ciclo inizia con una dichiarazione "var i = ..."
    if (StartVar) {
        varName = StartVar->getName();
        // Controlla se una variabile con lo stesso nome esiste già nello scope esterno.
        // Se esiste, salviamo il suo valore per ripristinarlo dopo il ciclo.
        if (drv.NamedValues.count(varName)) {
            oldVal = drv.NamedValues[varName];
        }
        // Genera il codice per la dichiarazione, che creerà la nuova variabile 'i'
        // e la metterà in NamedValues, nascondendo quella vecchia.
        StartVar->codegen(drv);
    } 
    // Se invece inizia con un'espressione "i = ..."
    else if (StartExpr) {
        StartExpr->codegen(drv);
    }

    // --- Generazione del Ciclo (questa parte è quasi identica a prima) ---
    Function *TheFunction = builder->GetInsertBlock()->getParent();
    BasicBlock *LoopHeader = BasicBlock::Create(*context, "loop.header", TheFunction);
    BasicBlock *LoopBody = BasicBlock::Create(*context, "loop.body", TheFunction);
    BasicBlock *AfterLoop = BasicBlock::Create(*context, "after.loop", TheFunction);

    builder->CreateBr(LoopHeader);
    builder->SetInsertPoint(LoopHeader);

    Value *CondV = Cond->codegen(drv);
    if (!CondV) return nullptr;

    CondV = builder->CreateFCmpONE(CondV, ConstantFP::get(*context, APFloat(0.0)), "loopcond");

    builder->CreateCondBr(CondV, LoopBody, AfterLoop);

    builder->SetInsertPoint(LoopBody);
    if (Body) Body->codegen(drv);
    if (Step) Step->codegen(drv);
    builder->CreateBr(LoopHeader);

    builder->SetInsertPoint(AfterLoop);

    // --- Ripristino dello Scope ---
    // Se abbiamo creato una nuova variabile per il ciclo, ora dobbiamo "distruggerla".
    if (StartVar) {
        // Se c'era una vecchia variabile con lo stesso nome, la ripristiniamo.
        if (oldVal) {
            drv.NamedValues[varName] = oldVal;
        } else {
            // Altrimenti, la variabile non esisteva prima. La rimuoviamo completamente.
            drv.NamedValues.erase(varName);
        }
    }

    // Un'espressione 'for' restituisce 0.0
    return ConstantFP::get(*context, APFloat(0.0));
}

/********************** Block Expression Tree *********************/
// in driver.cpp

Value* BlockExprAST::codegen(driver& drv) {
  Value* last = nullptr;

  // 1) genera il side-effect di ciascuno stmt
  for (auto *S : Stmts) {
    last = S->codegen(drv);
    if (!last) return nullptr;  // errore
  }

  // 2) genera e ritorna il valore dell'ultima espressione
  if (RetExpr)
    return RetExpr->codegen(drv);
  // altrimenti ritorna 0.0 di default
  return ConstantFP::get(*context, APFloat(0.0));
}


/************************* Var binding Tree *************************/
VarBindingAST::VarBindingAST(const std::string Name, ExprAST* Val):
   Name(Name), Val(Val) {};
   
const std::string& VarBindingAST::getName() const { 
   return Name; 
};

AllocaInst* VarBindingAST::codegen(driver& drv) {
   Function *fun = builder->GetInsertBlock()->getParent();
   // Allocate memory for the variable in the entry block
   AllocaInst *Alloca = CreateEntryBlockAlloca(fun, Name);

   Value *InitialVal;
   if (Val) { // If an explicit initializer expression (Val) is provided
      InitialVal = Val->codegen(drv);
      if (!InitialVal) {
         // Handle error in initializer expression codegen if necessary,
         // for now, we assume if an init expr is there, it should compile.
         // If InitialVal is null, the variable might get an undef store.
         // For robustness, you might return nullptr or ensure a default even here.
         return nullptr; // Or LogErrorV and return nullptr
      }
   } else { // No explicit initializer, default to 0.0
      InitialVal = ConstantFP::get(*context, APFloat(0.0));
   }

   // Store the initial value (either from expression or default 0.0)
   builder->CreateStore(InitialVal, Alloca);
   
   // Add the variable to the named values map for the current scope
   drv.NamedValues[Name] = Alloca;
   
   return Alloca;
}

/************************* Prototype Tree *************************/
PrototypeAST::PrototypeAST(std::string Name, std::vector<std::string> Args):
  Name(Name), Args(std::move(Args)), emitcode(true) {};  //Di regola il codice viene emesso

lexval PrototypeAST::getLexVal() const {
   lexval lval = Name;
   return lval;	
};

const std::vector<std::string>& PrototypeAST::getArgs() const { 
   return Args;
};

// Previene la doppia emissione del codice. Si veda il commento più avanti.
void PrototypeAST::noemit() { 
   emitcode = false; 
};

Function *PrototypeAST::codegen(driver& drv) {
  // Costruisce una struttura, qui chiamata FT, che rappresenta il "tipo" di una
  // funzione. Con ciò si intende a sua volta una coppia composta dal tipo
  // del risultato (valore di ritorno) e da un vettore che contiene il tipo di tutti
  // i parametri. Si ricordi, tuttavia, che nel nostro caso l'unico tipo è double.
  
  // Prima definiamo il vettore (qui chiamato Doubles) con il tipo degli argomenti
  std::vector<Type*> Doubles(Args.size(), Type::getDoubleTy(*context));
  // Quindi definiamo il tipo (FT) della funzione
  FunctionType *FT = FunctionType::get(Type::getDoubleTy(*context), Doubles, false);
  // Infine definiamo una funzione (al momento senza body) del tipo creato e con il nome
  // presente nel nodo AST. ExternalLinkage vuol dire che la funzione può avere
  // visibilità anche al di fuori del modulo
  Function *F = Function::Create(FT, Function::ExternalLinkage, Name, *module);

  // Ad ogni parametro della funzione F (che, è bene ricordare, è la rappresentazione 
  // llvm di una funzione, non è una funzione C++) attribuiamo ora il nome specificato dal
  // programmatore e presente nel nodo AST relativo al prototipo
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  /* Abbiamo completato la creazione del codice del prototipo.
     Il codice può quindi essere emesso, ma solo se esso corrisponde
     ad una dichiarazione extern. Se invece il prototipo fa parte
     della definizione "completa" di una funzione (prototipo+body) allora
     l'emissione viene fatta al momendo dell'emissione della funzione.
     In caso contrario nel codice si avrebbe sia una dichiarazione
     (come nel caso di funzione esterna) sia una definizione della stessa
     funzione.
  */
  /*
  if (emitcode) {
    F->print(errs());
    fprintf(stderr, "\n");
  };*/
  
  return F;
}

/************************* Function Tree **************************/
FunctionAST::FunctionAST(PrototypeAST* Proto, ExprAST* Body): Proto(Proto), Body(Body) {};

Function *FunctionAST::codegen(driver& drv) {
  // Verifica che la funzione non sia già presente nel modulo, cioò che non
  // si tenti una "doppia definizion"
  Function *function = 
      module->getFunction(std::get<std::string>(Proto->getLexVal()));
  // Se la funzione non è già presente, si prova a definirla, innanzitutto
  // generando (ma non emettendo) il codice del prototipo
  if (!function)
    function = Proto->codegen(drv);
  else
    return nullptr;
  // Se, per qualche ragione, la definizione "fallisce" si restituisce nullptr
  if (!function)
    return nullptr;  

  // Altrimenti si crea un blocco di base in cui iniziare a inserire il codice
  BasicBlock *BB = BasicBlock::Create(*context, "entry", function);
  builder->SetInsertPoint(BB);
 
  // Ora viene la parte "più delicata". Per ogni parametro formale della
  // funzione, nella symbol table si registra una coppia in cui la chiave
  // è il nome del parametro mentre il valore è un'istruzione alloca, generata
  // invocando l'utility CreateEntryBlockAlloca già commentata.
  // Vale comunque la pena ricordare: l'istruzione di allocazione riserva 
  // spazio in memoria (nel nostro caso per un double) e scrive l'indirizzo
  // in un registro SSA
  // Il builder crea poi un'istruzione che memorizza il valore del parametro x
  // (al momento contenuto nel registro SSA %x) nell'area di memoria allocata.
  // Si noti che il builder conosce il registro che contiene il puntatore all'area
  // perché esso è parte della rappresentazione C++ dell'istruzione di allocazione
  // (variabile Alloca) 
  
  for (auto &Arg : function->args()) {
    // Genera l'istruzione di allocazione per il parametro corrente
    AllocaInst *Alloca = CreateEntryBlockAlloca(function, Arg.getName());
    // Genera un'istruzione per la memorizzazione del parametro nell'area
    // di memoria allocata
    builder->CreateStore(&Arg, Alloca);
    // Registra gli argomenti nella symbol table per eventuale riferimento futuro
    drv.NamedValues[std::string(Arg.getName())] = Alloca;
  } 
  
  // Ora può essere generato il codice corssipondente al body (che potrà
  // fare riferimento alla symbol table)
  if (Value *RetVal = Body->codegen(drv)) {
    // Se la generazione termina senza errori, ciò che rimane da fare è
    // di generare l'istruzione return, che ("a tempo di esecuzione") prenderà
    // il valore lasciato nel registro RetVal 
    builder->CreateRet(RetVal);

    // Effettua la validazione del codice e un controllo di consistenza
    verifyFunction(*function);
 
    // Emissione del codice su su stderr) 
    //function->print(errs());
    //fprintf(stderr, "\n");
    return function;
  }

  // Errore nella definizione. La funzione viene rimossa
  function->eraseFromParent();
  return nullptr;
};

UnaryExprAST::UnaryExprAST(char Op, ExprAST* Operand)
    : Op(Op), Operand(Operand) {}

// In driver.cpp - UnaryExprAST::codegen
Value* UnaryExprAST::codegen(driver& drv) {
    switch (Op) {
        case '+': { // Gestione del PRE-INCREMENTO '++'
            // L'operando di '++' deve essere una variabile (un l-value).
            VariableExprAST* varAST = dynamic_cast<VariableExprAST*>(Operand);
            if (!varAST)
                return LogErrorV("L'operando dell'operatore unario ++ deve essere una variabile");

            std::string varName = std::get<std::string>(varAST->getLexVal());
            
            // Cerca il puntatore alla variabile (prima locale, poi globale).
            Value* varPtr = drv.NamedValues[varName]; // Prova locali
            if (!varPtr) {
                varPtr = module->getGlobalVariable(varName); // Prova globali
            }
            if (!varPtr) {
                return LogErrorV("Variabile non definita per '++': " + varName);
            }

            // Carica il valore attuale della variabile.
            Value* oldVal = builder->CreateLoad(Type::getDoubleTy(*context), varPtr, varName.c_str());
            if (!oldVal) return nullptr;

            // Aggiungi 1.0 al valore.
            Value* newVal = builder->CreateFAdd(oldVal, ConstantFP::get(*context, APFloat(1.0)), "incrtmp");

            // Salva il nuovo valore nella variabile.
            builder->CreateStore(newVal, varPtr);

            // L'espressione di pre-incremento restituisce il nuovo valore.
            return newVal;
        }

        case '-': { // Gestione del MENO UNARIO '-'
            // La negazione può applicarsi a qualsiasi espressione che restituisce un valore.
            Value* operandV = Operand->codegen(drv);
            if (!operandV)
                return nullptr;
            
            // Crea l'istruzione LLVM per la negazione floating-point (fneg).
            return builder->CreateFNeg(operandV, "negtmp");
        }

        case '!': { // Gestione della NEGAZIONE LOGICA 'not'
            Value* operandV = Operand->codegen(drv);
            if (!operandV) return nullptr;

            // 1. Converti l'operando double (0.0 per false, !=0.0 per true) in un booleano i1.
            //    Un valore è "true" se è diverso da 0.0.
            Value* operand_i1 = builder->CreateFCmpONE(operandV, ConstantFP::get(*context, APFloat(0.0)), "tobool_not_arg");
            
            // 2. Nega il valore i1. Ci sono vari modi, ad esempio:
            //    - not X  è equivalente a  X == false (icmp eq i1 X, 0)
            //    - not X è equivalente a X xor true (xor i1 X, 1)
            Value* not_i1 = builder->CreateICmpEQ(operand_i1, ConstantInt::get(Type::getInt1Ty(*context), 0), "not_res_i1");
            
            // 3. Riconverti il risultato booleano i1 in double (0.0 o 1.0) per coerenza con il linguaggio.
            return builder->CreateUIToFP(not_i1, Type::getDoubleTy(*context), "bool_to_double_not");
        }

        default:
            return LogErrorV("Operatore unario sconosciuto: " + std::string(1, Op));
    }
}
IfStmtAST::IfStmtAST(ExprAST* Cond, ExprAST* ThenBranch, ExprAST* ElseBranch)
    : Cond(Cond), ThenBranch(ThenBranch), ElseBranch(ElseBranch) {}

// Implementazione del codegen CORRETTA
Value* IfStmtAST::codegen(driver& drv) {
    Value* CondV = Cond->codegen(drv);
    if (!CondV)
        return nullptr;
    CondV = builder->CreateFCmpONE(CondV, ConstantFP::get(*context, APFloat(0.0)), "ifcond");

    Function *TheFunction = builder->GetInsertBlock()->getParent();

    // Crea i blocchi per i rami 'then' ed 'else', associandoli a TheFunction.
    BasicBlock *ThenBB = BasicBlock::Create(*context, "then", TheFunction);
    BasicBlock *ElseBB = BasicBlock::Create(*context, "else", TheFunction);     // Aggiunto TheFunction
    BasicBlock *MergeBB = BasicBlock::Create(*context, "ifcont", TheFunction);  // Aggiunto TheFunction

    // Usa i nomi corretti dei membri: ElseBranch invece di Else
    if (ElseBranch) {
        // Se c'è un blocco 'else', salta a ThenBB o a ElseBB
        builder->CreateCondBr(CondV, ThenBB, ElseBB);
    } else {
        // Altrimenti, salta a ThenBB o direttamente dopo l'if (MergeBB)
        // e rimuovi il blocco ElseBB se non viene usato.
        ElseBB->eraseFromParent(); // Rimuoviamo ElseBB se non c'è un ramo else
        builder->CreateCondBr(CondV, ThenBB, MergeBB);
    }

    // Genera il codice per il blocco 'then'
    builder->SetInsertPoint(ThenBB);
    // Usa il nome corretto del membro: ThenBranch invece di Then
    Value *ThenV = ThenBranch->codegen(drv);
    if (!ThenV) return nullptr;
    builder->CreateBr(MergeBB); // Salta al blocco di continuazione
    ThenBB = builder->GetInsertBlock(); // Aggiorna ThenBB per consistenza (anche se non usato per PHI qui)

    // Genera il codice per il blocco 'else', se esiste
    // Usa il nome corretto del membro: ElseBranch invece di Else
    if (ElseBranch) {
        // ElseBB è già stato aggiunto a TheFunction (o lo sarà se la funzione insert venisse usata)
        // Non è necessario TheFunction->insert(TheFunction->end(), ElseBB); se creato con parent
        builder->SetInsertPoint(ElseBB);
        Value* ElseV = ElseBranch->codegen(drv); // Usa ElseBranch
        if (!ElseV) return nullptr;
        builder->CreateBr(MergeBB);
        ElseBB = builder->GetInsertBlock(); // Aggiorna ElseBB
    }
    
    // Il blocco MergeBB è già stato aggiunto a TheFunction
    // Non è necessario TheFunction->insert(TheFunction->end(), MergeBB);
    builder->SetInsertPoint(MergeBB);
    
    // Un if-statement non produce un valore (o produce un valore di default come 0.0),
    // quindi ritorniamo un valore nullo coerente con ExprAST o un valore costante.
    // Dato che IfStmtAST ora è un ExprAST, dovrebbe restituire un Value*.
    // Restituiamo 0.0 come valore di default per uno statement if.
    return ConstantFP::get(*context, APFloat(0.0)); 
}
// Se avevi una definizione separata del costruttore GlobalDeclAST in driver.cpp, 
// assicurati che corrisponda a quella in driver.hpp:
// GlobalDeclAST::GlobalDeclAST(const std::string &N, int size) : Name(N), ArraySize(size) {}
// Ma di solito è definita inline nell'header, come ho suggerito sopra.

Value* GlobalDeclAST::codegen(driver& drv) {
    // Controlla se la variabile globale esiste già per evitare ridefinizioni problematiche.
    // In LLVM, una variabile globale può essere dichiarata più volte se ha linkage 'common'
    // o se le dichiarazioni successive sono solo 'declarations' e non 'definitions'.
    // Per semplicità, se esiste già, restituiamo il puntatore esistente.
    if (GlobalVariable *ExistingGV = module->getGlobalVariable(Name)) {
        // Potresti voler verificare che il tipo e la dimensione corrispondano se è già definita,
        // ma per ora questo è sufficiente per evitare errori di linkage.
        return ExistingGV; 
    }

    if (isArray()) { // È un array
        // 1. Definisci il tipo dell'array: ArrayType::get(elementType, numElements)
        //    elementType è double, numElements è ArraySize.
        ArrayType* arrayTy = ArrayType::get(Type::getDoubleTy(*context), ArraySize);
        
        // 2. Crea l'inizializzatore. Per un array globale, di solito lo si inizializza a zero.
        //    ConstantAggregateZero::get(ArrayType*) crea un inizializzatore zero per l'array.
        Constant* Initializer = ConstantAggregateZero::get(arrayTy);
        
        // 3. Crea la GlobalVariable per l'array.
        //    Usiamo CommonLinkage: se più file .k definiscono lo stesso array globale,
        //    il linker ne sceglierà una (e allocherà lo spazio una volta).
        GlobalVariable *GV = new GlobalVariable(
            *module,                          // Modulo a cui appartiene
            arrayTy,                          // Tipo dell'array
            false,                            // isConstant (false = non è costante, può essere modificata)
            GlobalValue::CommonLinkage,       // Tipo di Linkage
            Initializer,                      // Inizializzatore (array di zeri)
            Name                              // Nome della variabile globale
        );
        // GV->setAlignment(Align(8)); // LLVM di solito gestisce l'allineamento, 
                                     // ma potresti specificarlo se necessario (es. 8 per double)
        return GV;
    } else { // È una variabile scalare (simile al tuo codice esistente)
        // 1. Definisci l'inizializzatore (0.0 per un double scalare)
        Constant* Initializer = ConstantFP::get(*context, APFloat(0.0));

        // 2. Crea la GlobalVariable per lo scalare.
        GlobalVariable *GV = new GlobalVariable(
            *module,
            Type::getDoubleTy(*context),      // Tipo (double)
            false,                            // isConstant
            GlobalValue::CommonLinkage,       // Tipo di Linkage
            Initializer,                      // Inizializzatore (0.0)
            Name                              // Nome
        );
        // GV->setAlignment(Align(8));
        return GV;
    }
    return nullptr; // Non dovrebbe mai essere raggiunto
}
Value* ArrayAccessExprAST::codegen(driver& drv) {
    // 1. Trova il puntatore all'array globale.
    GlobalVariable* arrayVar = module->getGlobalVariable(ArrayName);
    if (!arrayVar) {
        return LogErrorV("Array globale non definito: " + ArrayName);
    }

    // Verifica che sia effettivamente un puntatore a un tipo array.
    // Il tipo di una GlobalVariable è un PointerType al tipo della variabile.
    // Quindi arrayVar->getValueType() ci dà il tipo dell'array (es. [10 x double]).
    if (!arrayVar->getValueType()->isArrayTy()) {
        return LogErrorV(ArrayName + " non è un array globale.");
    }
    // ArrayType* arrayTy = cast<ArrayType>(arrayVar->getValueType()); // Utile se serve la dimensione

    // 2. Valuta l'espressione dell'indice.
    Value* indexVal = IndexExpr->codegen(drv);
    if (!indexVal) {
        return nullptr;
    }

    // L'indice dovrebbe essere un intero. LLVM GEP si aspetta i64 per gli indici.
    // Il nostro linguaggio usa double per tutto, quindi dobbiamo convertire l'indice
    // da double a i64. Attenzione: questo tronca la parte frazionaria.
    // Sarebbe meglio avere un tipo intero nel linguaggio per gli indici.
    // Per ora, facciamo fptosi (floating point to signed integer).
    Value* indexInt = builder->CreateFPToSI(indexVal, Type::getInt64Ty(*context), "indexcast");

    // 3. Prepara gli indici per l'istruzione GEP (GetElementPtr).
    // Per un array globale come @A = global [10 x double], ...
    // un GEP per accedere a A[i] necessita di due indici:
    //   - Il primo indice (0) dereferenzia il puntatore globale per ottenere l'array stesso.
    //   - Il secondo indice (indexInt) seleziona l'elemento nell'array.
    std::vector<Value*> indices;
    indices.push_back(ConstantInt::get(Type::getInt64Ty(*context), 0)); // Indice per il puntatore globale
    indices.push_back(indexInt);                                       // Indice per l'elemento dell'array

    // 4. Genera l'istruzione GEP per ottenere il puntatore all'elemento.
    //    arrayVar è già un pointer type, quindi il primo argomento di CreateGEP è il tipo PUNTATO da arrayVar,
    //    cioè il tipo dell'array stesso (es. [10 x double]).
    Value* elemPtr = builder->CreateGEP(arrayVar->getValueType(), arrayVar, indices, "arrayidx");

    // 5. Carica il valore dall'indirizzo dell'elemento.
    //    Il tipo da caricare è il tipo dell'elemento dell'array, che è double.
    return builder->CreateLoad(Type::getDoubleTy(*context), elemPtr, "loadtmp");
}

Value* ArrayAssignExprAST::codegen(driver& drv) {
    // 1. Trova il puntatore all'array globale.
    GlobalVariable* arrayVar = module->getGlobalVariable(ArrayName);
    if (!arrayVar) {
        return LogErrorV("Array globale non definito per l'assegnazione: " + ArrayName);
    }
    if (!arrayVar->getValueType()->isArrayTy()) {
        return LogErrorV(ArrayName + " non è un array globale per l'assegnazione.");
    }

    // 2. Valuta l'espressione dell'indice e convertila in intero.
    Value* indexVal = IndexExpr->codegen(drv);
    if (!indexVal) return nullptr;
    Value* indexInt = builder->CreateFPToSI(indexVal, Type::getInt64Ty(*context), "indexcast_assign");

    // 3. Valuta l'espressione del valore da assegnare (RHS).
    Value* valueToStore = ValueExpr->codegen(drv);
    if (!valueToStore) return nullptr;

    // 4. Prepara gli indici per GEP.
    std::vector<Value*> indices;
    indices.push_back(ConstantInt::get(Type::getInt64Ty(*context), 0)); // Indice per il puntatore globale
    indices.push_back(indexInt);                                       // Indice per l'elemento

    // 5. Genera l'istruzione GEP per ottenere il puntatore all'elemento.
    Value* elemPtr = builder->CreateGEP(arrayVar->getValueType(), arrayVar, indices, "arrayidx_assign");

    // 6. Genera l'istruzione Store.
    builder->CreateStore(valueToStore, elemPtr);

    // 7. L'espressione di assegnazione restituisce il valore assegnato.
    return valueToStore;
}