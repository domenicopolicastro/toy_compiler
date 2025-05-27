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
Value *BinaryExprAST::codegen(driver& drv) {
  Value *L = LHS->codegen(drv);
  Value *R = RHS->codegen(drv);
  if (!L || !R) 
     return nullptr;
  switch (Op) {
  case '+':
    return builder->CreateFAdd(L,R,"addres");
  case '-':
    return builder->CreateFSub(L,R,"subres");
  case '*':
    return builder->CreateFMul(L,R,"mulres");
  case '/':
    return builder->CreateFDiv(L,R,"addres");
  case '<':
    return builder->CreateFCmpULT(L,R,"lttest");
  case '=':
    return builder->CreateFCmpUEQ(L,R,"eqtest");
  default:  
    std::cout << Op << std::endl;
    return LogErrorV("Operatore binario non supportato");
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
   
Value* IfExprAST::codegen(driver& drv) {
    // Viene dapprima generato il codice per valutare la condizione, che
    // memorizza il risultato (di tipo i1, dunque booleano) nel registro SSA 
    // che viene "memorizzato" in CondV. 
    Value* CondV = Cond->codegen(drv);
    if (!CondV)
       return nullptr;
    
    // Ora bisogna generare l'istruzione di salto condizionato, ma prima
    // vanno creati i corrispondenti basic block nella funzione attuale
    // (ovvero la funzione di cui fa parte il corrente blocco di inserimento)
    Function *function = builder->GetInsertBlock()->getParent();
    BasicBlock *TrueBB =  BasicBlock::Create(*context, "trueexp", function);
    // Il blocco TrueBB viene inserito nella funzione dopo il blocco corrente
    BasicBlock *FalseBB = BasicBlock::Create(*context, "falseexp");
    BasicBlock *MergeBB = BasicBlock::Create(*context, "endcond");
    // Gli altri due blocchi non vengono ancora inseriti perché le istruzioni
    // previste nel "ramo" true del condizionale potrebbe dare luogo alla creazione
    // di altri blocchi, che naturalmente andrebbero inseriti prima di FalseBB
    
    // Ora possiamo crere l'istruzione di salto condizionato
    builder->CreateCondBr(CondV, TrueBB, FalseBB);
    
    // "Posizioniamo" il builder all'inizio del blocco true, 
    // generiamo ricorsivamente il codice da eseguire in caso di
    // condizione vera e, in chiusura di blocco, generiamo il saldo 
    // incondizionato al blocco merge
    builder->SetInsertPoint(TrueBB);
    Value *TrueV = TrueExp->codegen(drv);
    if (!TrueV)
       return nullptr;
    builder->CreateBr(MergeBB);
    
    // Come già ricordato, la chiamata di codegen in TrueExp potrebbe aver inserito 
    // altri blocchi (nel caso in cui la parte trueexp sia a sua volta un condizionale).
    // Ne consegue che il blocco corrente potrebbe non coincidere più con TrueBB.
    // Il branch alla parte merge deve però essere effettuato dal blocco corrente,
    // che dunque va recuperato. Ed è fondamentale sapere da quale blocco origina
    // il salto perché tale informazione verrà utilizzata da un'istruzione PHI.
    // Nel caso in cui non sia stato inserito alcun nuovo blocco, la seguente
    // istruzione corrisponde ad una NO-OP
    TrueBB = builder->GetInsertBlock();
    function->insert(function->end(), FalseBB);
    
    // "Posizioniamo" il builder all'inizio del blocco false, 
    // generiamo ricorsivamente il codice da eseguire in caso di
    // condizione falsa e, in chiusura di blocco, generiamo il saldo 
    // incondizionato al blocco merge
    builder->SetInsertPoint(FalseBB);
    
    Value *FalseV = FalseExp->codegen(drv);
    if (!FalseV)
       return nullptr;
    builder->CreateBr(MergeBB);
    
    // Esattamente per la ragione spiegata sopra (ovvero il possibile inserimento
    // di nuovi blocchi da parte della chiamata di codegen in FalseExp), andiamo ora
    // a recuperare il blocco corrente 
    FalseBB = builder->GetInsertBlock();
    function->insert(function->end(), MergeBB);
    
    // Andiamo dunque a generare il codice per la parte dove i due "flussi"
    // di esecuzione si riuniscono. Impostiamo correttamente il builder
    builder->SetInsertPoint(MergeBB);
  
    // Il codice di riunione dei flussi è una "semplice" istruzione PHI: 
    //a seconda del blocco da cui arriva il flusso, TrueBB o FalseBB, il valore
    // del costrutto condizionale (si ricordi che si tratta di un "expression if")
    // deve essere copiato (in un nuovo registro SSA) da TrueV o da FalseV
    // La creazione di un'istruzione PHI avviene però in due passi, in quanto
    // il numero di "flussi entranti" non è fissato.
    // 1) Dapprima si crea il nodo PHI specificando quanti sono i possibili nodi sorgente
    // 2) Per ogni possibile nodo sorgente, viene poi inserita l'etichetta e il registro
    //    SSA da cui prelevare il valore 
    PHINode *PN = builder->CreatePHI(Type::getDoubleTy(*context), 2, "condval");
    PN->addIncoming(TrueV, TrueBB);
    PN->addIncoming(FalseV, FalseBB);
    return PN;
};

ForExprAST::ForExprAST(ExprAST* Start, ExprAST* Cond, ExprAST* Step, ExprAST* Body)
    : Start(Start), Cond(Cond), Step(Step), Body(Body) {}

/************************* For Expression Tree *************************/
Value* ForExprAST::codegen(driver& drv) {
    Function *TheFunction = builder->GetInsertBlock()->getParent();

    // Generate code for the initialization expression.
    if (Start) {
        Start->codegen(drv);
    }

    // Create blocks for the loop header, body, and for code after the loop.
    // Pass TheFunction as the parent to automatically add them to the function.
    BasicBlock *LoopHeader = BasicBlock::Create(*context, "loop.header", TheFunction);
    BasicBlock *LoopBody = BasicBlock::Create(*context, "loop.body", TheFunction);
    BasicBlock *AfterLoop = BasicBlock::Create(*context, "after.loop", TheFunction);

    // Jump into the loop header.
    builder->CreateBr(LoopHeader);

    // Start generating code in the loop header block.
    builder->SetInsertPoint(LoopHeader);

    // Generate the condition code.
    Value *CondV = Cond->codegen(drv); // Questo produce un i1 per condizioni come "i < n"
    if (!CondV)
        return nullptr;

    // NON è necessaria un'ulteriore conversione se CondV è già i1.
    // La riga seguente era l'origine dell'errore e va rimossa o commentata:
    // CondV = builder->CreateFCmpONE(CondV, ConstantFP::get(*context, APFloat(0.0)), "loop.cond");

    // Create the conditional branch usando direttamente CondV (che è i1).
    builder->CreateCondBr(CondV, LoopBody, AfterLoop);

    // Set the insertion point to the loop body.
    builder->SetInsertPoint(LoopBody);

    // Generate the code for the body of the loop.
    if (Body) {
        Body->codegen(drv);
    }

    // Generate the step/increment code.
    if (Step) {
        Step->codegen(drv);
    }

    // After the body, jump back to the header to re-evaluate the condition.
    builder->CreateBr(LoopHeader);

    // Any new code will be added to the AfterLoop block.
    builder->SetInsertPoint(AfterLoop);

    // The 'for' loop expression evaluates to 0.0 (or null in some contexts for expressions).
    // Ritorna un valore double per coerenza con le altre espressioni.
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

Value* UnaryExprAST::codegen(driver& drv) {
    // L'operando deve essere un l-value (una variabile).
    // Proviamo a vedere se è una VariableExprAST.
    VariableExprAST* varAST = dynamic_cast<VariableExprAST*>(Operand);
    if (!varAST)
        return LogErrorV("L'operando dell'operatore unario ++ deve essere una variabile");

    // Ottieni il nome della variabile.
    std::string varName = std::get<std::string>(varAST->getLexVal());
    
    // Cerca la variabile (prima locale, poi globale).
    Value* varPtr = drv.NamedValues[varName];
    if (!varPtr) {
        varPtr = module->getGlobalVariable(varName);
    }
    if (!varPtr) {
        return LogErrorV("Variabile non definita: " + varName);
    }

    // Carica il valore attuale della variabile.
    Value* oldVal = builder->CreateLoad(Type::getDoubleTy(*context), varPtr, varName.c_str());
    if (!oldVal)
        return nullptr;

    // Aggiungi 1.0 al valore.
    Value* newVal = builder->CreateFAdd(oldVal, ConstantFP::get(*context, APFloat(1.0)), "incrtmp");

    // Salva il nuovo valore nella variabile.
    builder->CreateStore(newVal, varPtr);

    // L'espressione di pre-incremento restituisce il nuovo valore.
    return newVal;
}