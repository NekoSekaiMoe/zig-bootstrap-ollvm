/**
 * @file Utils.cpp
 * @author SsageParuders
 * @brief 本代码参考原OLLVM项目:https://github.com/obfuscator-llvm/obfuscator
 *        感谢地球人前辈的指点
 * @version 0.1
 * @date 2022-07-14
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "Utils.h"
#include "llvm/IR/IntrinsicInst.h"

using namespace llvm;
using std::vector;

LLVMContext *CONTEXT = nullptr;
bool obf_function_name_cmd = false;

/**
 * @brief 参考资料:https://www.jianshu.com/p/0567346fd5e8
 *        作用是读取llvm.global.annotations中的annotation值 从而实现过滤函数
 * 只对单独某功能开启PASS
 * @param f
 * @return std::string
 */
std::string llvm::readAnnotate(Function *f) { // 取自原版ollvm项目
  std::string annotation = "";
  /* Get annotation variable */
  GlobalVariable *glob =
      f->getParent()->getGlobalVariable("llvm.global.annotations");
  if (glob != NULL) {
    /* Get the array */
    if (ConstantArray *ca = dyn_cast<ConstantArray>(glob->getInitializer())) {
      for (unsigned i = 0; i < ca->getNumOperands(); ++i) {
        /* Get the struct */
        if (ConstantStruct *structAn =
                dyn_cast<ConstantStruct>(ca->getOperand(i))) {
          if (ConstantExpr *expr =
                  dyn_cast<ConstantExpr>(structAn->getOperand(0))) {
            /*
             * If it's a bitcast we can check if the annotation is concerning
             * the current function
             */
            if (expr->getOpcode() == Instruction::BitCast &&
                expr->getOperand(0) == f) {
              ConstantExpr *note = cast<ConstantExpr>(structAn->getOperand(1));
              /*
               * If it's a GetElementPtr, that means we found the variable
               * containing the annotations
               */
              if (note->getOpcode() == Instruction::GetElementPtr) {
                if (GlobalVariable *annoteStr =
                        dyn_cast<GlobalVariable>(note->getOperand(0))) {
                  if (ConstantDataSequential *data =
                          dyn_cast<ConstantDataSequential>(
                              annoteStr->getInitializer())) {
                    if (data->isString()) {
                      annotation += data->getAsString().lower() + " ";
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return (annotation);
}

/**
 * @brief 从注解中获取函数注解
 * 解决在llvm15+上读取不到注解 (测试版本在LLVM-18.0.1)
 * @param F
 * @return std::string
 */
std::string getFunctionAnnotation(Function *F) {
  Module *M = F->getParent();
  // 查找名为 "llvm.global.annotations" 的全局变量
  GlobalVariable *GA = M->getNamedGlobal("llvm.global.annotations");
  if (!GA)
    return ""; // 如果没有注解，返回空字符串
  // 解析 llvm.global.annotations
  if (ConstantArray *CA = dyn_cast<ConstantArray>(GA->getInitializer())) {
    for (unsigned i = 0; i < CA->getNumOperands(); ++i) {
      if (ConstantStruct *CS = dyn_cast<ConstantStruct>(CA->getOperand(i))) {
        // 第一个元素是被注解的函数
        if (Function *AnnotatedFunction =
                dyn_cast<Function>(CS->getOperand(0)->stripPointerCasts())) {
          if (AnnotatedFunction == F) {
            // 第二个元素是注解字符串的全局变量
            if (GlobalVariable *GV = dyn_cast<GlobalVariable>(
                    CS->getOperand(1)->stripPointerCasts())) {
              if (ConstantDataArray *Anno =
                      dyn_cast<ConstantDataArray>(GV->getInitializer())) {
                return Anno->getAsCString().str();
              }
            }
          }
        }
      }
    }
  }

  return ""; // 如果没有找到对应函数的注解，返回空字符串
}

/**
 * @brief 用于判断是否开启混淆
 *
 * @param flag
 * @param f
 * @param attribute
 * @return true
 * @return false
 */

// Fix in line 193
bool valueEscapes(Instruction &I) {
  BasicBlock *BB =I.getParent();
  for (const Use &U : I.uses()) {
    //检测是否跨基本块调用（控制流分析）
    if (Instruction *User = dyn_cast<Instruction>(U.getUser())) {
      if (User->getParent() != BB) {
        return true;
      }
      //检测是否涉及内存操作
      if (User->mayReadFromMemory() || User ->mayWriteToMemory()) {
        return true;
      }
    } else {
      //使用者不是指令（比如常量表达式），以为发生逃逸
      return false;
    }
  }
  return false;
}

bool llvm::toObfuscate(bool flag, Function *f,
                       std::string const &attribute) { // 取自原版ollvm项目
  std::string attr = attribute;
  std::string attrNo = "no" + attr;
  // Check if declaration
  if (f->isDeclaration()) {
    return false;
  }
  // Check external linkage
  if (f->hasAvailableExternallyLinkage() != 0) {
    return false;
  }
  // outs() << "[Soule] function: " << f->getName().str() << " # annotation: "
  // << readAnnotate(f) << "\n";
  //
  //  We have to check the nofla flag first
  //  Because .find("fla") is true for a string like "fla" or
  //  "nofla"
  if (getFunctionAnnotation(f).find(attrNo) !=
      std::string::npos) { // 是否禁止开启XXX
    return false;
  }
  // If fla annotations
  if (getFunctionAnnotation(f).find(attr) != std::string::npos) { // 是否开启XXX
    return true;
  }
  // 由于Visual Studio无法传入annotation,
  // 增加一个使用函数名匹配是否单独开关的功能
  if (obf_function_name_cmd == true) { // 开启使用函数名匹配混淆功能开关
    if (f->getName().find("_" + attrNo + "_") != StringRef::npos) {
      outs() << "[Soule] " << attrNo << ".function: " << f->getName().str()
             << "\n";
      return false;
    }
    if (f->getName().find("_" + attr + "_") != StringRef::npos) {
      outs() << "[Soule] " << attr << ".function: " << f->getName().str()
             << "\n";
      return true;
    }
  }
  // If fla flag is set
  if (flag == true) { // 开启PASS
    return true;
  }
  return false;
}

/** LLVM\llvm\lib\Transforms\Scalar\Reg2Mem.cpp
 * @brief 修复PHI指令和逃逸变量
 *
 * @param F
 */
void llvm::fixStack(Function &F) {
  // Insert all new allocas into entry block.
  BasicBlock *BBEntry = &F.getEntryBlock();
  assert(pred_empty(BBEntry) &&
         "Entry block to function must not have predecessors!");

  // Find first non-alloca instruction and create insertion point. This is
  // safe if block is well-formed: it always have terminator, otherwise
  // we'll get and assertion.
  BasicBlock::iterator I = BBEntry->begin();
  while (isa<AllocaInst>(I))
    ++I;

  CastInst *AllocaInsertionPoint = new BitCastInst(
      Constant::getNullValue(Type::getInt32Ty(F.getContext())),
      Type::getInt32Ty(F.getContext()), "fix_stack_point", &*I);

  // Find the escaped instructions. But don't create stack slots for
  // allocas in entry block.
  std::list<Instruction *> WorkList;
  for (Instruction &I : instructions(F))
    if (!(isa<AllocaInst>(I) && I.getParent() == BBEntry) && valueEscapes(I))
      WorkList.push_front(&I);

  // Demote escaped instructions
  //NumRegsDemoted += WorkList.size();
  for (Instruction *I : WorkList)
    DemoteRegToStack(*I, false, AllocaInsertionPoint);

  WorkList.clear();

  // Find all phi's
  for (BasicBlock &BB : F)
    for (auto &Phi : BB.phis())
      WorkList.push_front(&Phi);

  // Demote phi nodes
  //NumPhisDemoted += WorkList.size();
  for (Instruction *I : WorkList)
    DemotePHIToStack(cast<PHINode>(I), AllocaInsertionPoint);
}

/**
 * @brief
 *
 * @param Func
 */
void llvm::FixFunctionConstantExpr(Function *Func) {
  // Replace ConstantExpr with equal instructions
  // Otherwise replacing on Constant will crash the compiler
  for (BasicBlock &BB : *Func) {
    FixBasicBlockConstantExpr(&BB);
  }
}
/**
 * @brief
 *
 * @param BB
 */
void llvm::FixBasicBlockConstantExpr(BasicBlock *BB) {
  // Replace ConstantExpr with equal instructions
  // Otherwise replacing on Constant will crash the compiler
  // Things to note:
  // - Phis must be placed at BB start so CEs must be placed prior to current BB
  assert(!BB->empty() && "BasicBlock is empty!");
  assert((BB->getParent() != NULL) && "BasicBlock must be in a Function!");
  Instruction *FunctionInsertPt =
      &*(BB->getParent()->getEntryBlock().getFirstInsertionPt());
  // Instruction* LocalBBInsertPt=&*(BB.getFirstInsertionPt());
  for (Instruction &I : *BB) {
    if (isa<LandingPadInst>(I) || isa<FuncletPadInst>(I)) {
      continue;
    }
    for (unsigned i = 0; i < I.getNumOperands(); i++) {
      if (ConstantExpr *C = dyn_cast<ConstantExpr>(I.getOperand(i))) {
        Instruction *InsertPt = &I;
        IRBuilder<NoFolder> IRB(InsertPt);
        if (isa<PHINode>(I)) {
          IRB.SetInsertPoint(FunctionInsertPt);
        }
        Instruction *Inst = IRB.Insert(C->getAsInstruction());
        I.setOperand(i, Inst);
      }
    }
  }
}

/**
 * @brief 随机字符串
 *
 * @param len
 * @return string
 */
string llvm::rand_str(int len) {
  string str;
  char c = 'O';
  int idx;
  for (idx = 0; idx < len; idx++) {

    switch ((rand() % 3)) {
    case 1:
      c = 'O';
      break;
    case 2:
      c = '0';
      break;
    default:
      c = 'o';
      break;
    }
    str.push_back(c);
  }
  return str;
}

// LLVM-MSVC有这个函数, 官方版LLVM没有 (LLVM:17.0.6 | LLVM-MSVC:3.2.6)
void llvm::LowerConstantExpr(Function &F) {
  SmallPtrSet<Instruction *, 8> WorkList;

  for (inst_iterator It = inst_begin(F), E = inst_end(F); It != E; ++It) {
    Instruction *I = &*It;

    if (isa<LandingPadInst>(I) || isa<CatchPadInst>(I) ||
        isa<CatchSwitchInst>(I) || isa<CatchReturnInst>(I))
      continue;
    if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      if (II->getIntrinsicID() == Intrinsic::eh_typeid_for) {
        continue;
      }
    }

    for (unsigned int i = 0; i < I->getNumOperands(); ++i) {
      if (isa<ConstantExpr>(I->getOperand(i)))
        WorkList.insert(I);
    }
  }

  while (!WorkList.empty()) {
    auto It = WorkList.begin();
    Instruction *I = *It;
    WorkList.erase(*It);

    if (PHINode *PHI = dyn_cast<PHINode>(I)) {
      for (unsigned int i = 0; i < PHI->getNumIncomingValues(); ++i) {
        Instruction *TI = PHI->getIncomingBlock(i)->getTerminator();
        if (ConstantExpr *CE =
                dyn_cast<ConstantExpr>(PHI->getIncomingValue(i))) {
          Instruction *NewInst = CE->getAsInstruction();
          NewInst->insertBefore(TI);
          PHI->setIncomingValue(i, NewInst);
          WorkList.insert(NewInst);
        }
      }
    } else {
      for (unsigned int i = 0; i < I->getNumOperands(); ++i) {
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(I->getOperand(i))) {
          Instruction *NewInst = CE->getAsInstruction();
          NewInst->insertBefore(I);
          I->replaceUsesOfWith(CE, NewInst);
          WorkList.insert(NewInst);
        }
      }
    }
  }
}
