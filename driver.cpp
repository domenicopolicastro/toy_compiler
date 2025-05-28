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
   // Viene subito recuperato il riferimento alla funzione in cui si trova
   // il blocco corrente. Il riferimento è necessario perché lo spazio necessario
   // per memorizzare una variabile (ovunque essa sia definita, si tratti cioè
   // di un parametro oppure di una variabile locale ad un blocco espressione)
   // viene sempre riservato nell'entry block della funzione. Ricordiamo che
   // l'allocazione viene fatta tramite l'utility CreateEntryBlockAlloca
   Function *fun = builder->GetInsertBlock()->getParent();
   // Ora viene generato il codice che definisce il valore della variabile
   Value *BoundVal = Val->codegen(drv);
   if (!BoundVal)  // Qualcosa è andato storto nella generazione del codice?
      return nullptr;
   // Se tutto ok, si genera l'struzione che alloca memoria per la varibile ...
   AllocaInst *Alloca = CreateEntryBlockAlloca(fun, Name);
   // ... e si genera l'istruzione per memorizzarvi il valore dell'espressione,
   // ovvero il contenuto del registro BoundVal
   drv.NamedValues[Name] = Alloca;
   builder->CreateStore(BoundVal, Alloca);
   
   // L'istruzione di allocazione (che include il registro "puntatore" all'area di memoria
   // allocata) viene restituita per essere inserita nella symbol table
   return Alloca;
};

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

IfStmtAST::IfStmtAST(ExprAST* Cond, BlockExprAST* Then, BlockExprAST* Else)
    : Cond(Cond), Then(Then), Else(Else) {}

// Implementazione del codegen
Value* IfStmtAST::codegen(driver& drv) {
    Value* CondV = Cond->codegen(drv);
    if (!CondV)
        return nullptr;
    CondV = builder->CreateFCmpONE(CondV, ConstantFP::get(*context, APFloat(0.0)), "ifcond");

    Function *TheFunction = builder->GetInsertBlock()->getParent();

    // Crea i blocchi per i rami 'then' ed 'else'.
    BasicBlock *ThenBB = BasicBlock::Create(*context, "then", TheFunction);
    BasicBlock *ElseBB = BasicBlock::Create(*context, "else");
    BasicBlock *MergeBB = BasicBlock::Create(*context, "ifcont");

    if (Else) {
        // Se c'è un blocco 'else', salta a ThenBB o a ElseBB
        builder->CreateCondBr(CondV, ThenBB, ElseBB);
    } else {
        // Altrimenti, salta a ThenBB o direttamente dopo l'if (MergeBB)
        builder->CreateCondBr(CondV, ThenBB, MergeBB);
    }

    // Genera il codice per il blocco 'then'
    builder->SetInsertPoint(ThenBB);
    Value *ThenV = Then->codegen(drv);
    if (!ThenV) return nullptr;
    builder->CreateBr(MergeBB); // Salta al blocco di continuazione

    // Il blocco 'then' potrebbe aver creato altri blocchi, quindi aggiorniamo ThenBB
    // per il PHI node, anche se qui non lo usiamo, è buona norma.
    ThenBB = builder->GetInsertBlock();

    // Genera il codice per il blocco 'else', se esiste
    if (Else) {
        TheFunction->insert(TheFunction->end(), ElseBB);
        builder->SetInsertPoint(ElseBB);
        Value* ElseV = Else->codegen(drv);
        if (!ElseV) return nullptr;
        builder->CreateBr(MergeBB);
        ElseBB = builder->GetInsertBlock();
    }
    
    // Inserisci il blocco di continuazione
    TheFunction->insert(TheFunction->end(), MergeBB);
    builder->SetInsertPoint(MergeBB);
    
    // Un if-statement non produce un valore, quindi ritorniamo un valore nullo o costante.
    return Constant::getNullValue(Type::getDoubleTy(*context));
}