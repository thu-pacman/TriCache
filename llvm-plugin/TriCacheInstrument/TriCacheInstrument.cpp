// Copyright 2022 Huanqi Cao, Tsinghua University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

class TriCacheInstrumentPass : public PassInfoMixin<TriCacheInstrumentPass>
{
public:
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
    static bool isRequired() { return true; }
};

namespace
{
    cl::opt<bool> ToInstAlloc("scache-inst-alloc",
                              cl::desc("Whether to instrument malloc and mmap calls in scache-inst"));

    Function *getCondGRP(Module &M, StringRef name, StringRef condName)
    {
        if (auto CondGRP = M.getFunction(condName))
            return CondGRP;

        auto &C = M.getContext();
        auto ptrTy = Type::getInt8PtrTy(C);
        auto sizeTy = Type::getInt64Ty(C);
        auto GRP = M.getOrInsertFunction(name, ptrTy, ptrTy);
        auto CondGRP = Function::Create(FunctionType::get(ptrTy, {ptrTy}, false),
                                        GlobalValue::LinkageTypes::PrivateLinkage, condName, M);
        CondGRP->addFnAttr(Attribute::AttrKind::AlwaysInline);

        Value *ptr = CondGRP->arg_begin();
        auto entryBB = BasicBlock::Create(C, "entry", CondGRP);
        auto cachedBB = BasicBlock::Create(C, "cached", CondGRP);
        auto rawBB = BasicBlock::Create(C, "raw", CondGRP);
        {
            IRBuilder<> builder(entryBB);
            auto cmp = builder.CreateICmpSLE(builder.CreatePtrToInt(ptr, sizeTy),
                                             Constant::getIntegerValue(sizeTy, APInt(64, 0, true)));
            builder.CreateCondBr(cmp, cachedBB, rawBB, MDBuilder(C).createBranchWeights({0, 1}));
        }
        {
            IRBuilder<> builder(cachedBB);
            builder.CreateRet(builder.CreateCall(GRP, {ptr}));
        }
        {
            IRBuilder<> builder(rawBB);
            builder.CreateRet(ptr);
        }
        return CondGRP;
    }

#define GET_COND_GRP(NAME) getCondGRP(M, NAME, NAME "_cond")
} // namespace

PreservedAnalyses TriCacheInstrumentPass::run(Function &F, FunctionAnalysisManager &AM)
{
    auto &C = F.getContext();
    auto &M = *F.getParent();
    auto ptrTy = Type::getInt8PtrTy(C);
    auto sizeTy = Type::getInt64Ty(C);
    auto intTy = Type::getInt32Ty(C);
    auto CondGRPLoad = GET_COND_GRP("cache_get_raw_ptr_load");
    auto CondGRPStore = GET_COND_GRP("cache_get_raw_ptr_store");
    auto CacheMemCpy = M.getOrInsertFunction("cache_memcpy", ptrTy, ptrTy, ptrTy, sizeTy);
    auto CacheMemSet = M.getOrInsertFunction("cache_memset", ptrTy, ptrTy, intTy, sizeTy);
    auto CacheMemMove = M.getOrInsertFunction("cache_memmove", ptrTy, ptrTy, ptrTy, sizeTy);
    for (auto &BB : F)
    {
        SmallVector<LoadInst *, 16> LIs;
        SmallVector<StoreInst *, 16> SIs;
        SmallVector<AtomicRMWInst *, 4> AtomicRMWs;
        SmallVector<AtomicCmpXchgInst *, 4> AtomicCXs;
        SmallVector<MemIntrinsic *, 4> MemIntrins;
        for (auto &I : BB)
        {
            if (auto LI = dyn_cast<LoadInst>(&I))
            {
                auto addr = LI->getPointerOperand();
                if (!isa<AllocaInst>(addr))
                    LIs.push_back(LI);
            }
            else if (auto SI = dyn_cast<StoreInst>(&I))
            {
                auto addr = SI->getPointerOperand();
                if (!isa<AllocaInst>(addr))
                    SIs.push_back(SI);
            }
            else if (auto x = dyn_cast<MemIntrinsic>(&I))
            {
                MemIntrins.push_back(x);
            }
            else if (auto x = dyn_cast<AtomicRMWInst>(&I))
            {
                AtomicRMWs.push_back(x);
            }
            else if (auto x = dyn_cast<AtomicCmpXchgInst>(&I))
            {
                AtomicCXs.push_back(x);
            }
        }
        auto ReplaceAddrOp = [&](Instruction *I, int OpIdx, bool IsLoad)
        {
            auto ptr = I->getOperand(OpIdx);
            IRBuilder<> builder(I);
            if (!I->getDebugLoc())
            {
                auto subprogram = F.getSubprogram();
                if (subprogram)
                    builder.SetCurrentDebugLocation(DILocation::get(C, subprogram->getLine(), 0, subprogram));
            }
            auto ptr_i8p = builder.CreatePointerCast(ptr, ptrTy);
            auto raw_i8p = builder.CreateCall(IsLoad ? CondGRPLoad : CondGRPStore, ptr_i8p);
            auto raw = builder.CreatePointerCast(raw_i8p, ptr->getType());
            I->setOperand(OpIdx, raw);
        };
        for (auto LI : LIs)
            ReplaceAddrOp(LI, LI->getPointerOperandIndex(), true);
        for (auto SI : SIs)
            ReplaceAddrOp(SI, SI->getPointerOperandIndex(), false);
        for (auto MI : MemIntrins)
        {
            IRBuilder<> builder(MI);
            if (isa<MemTransferInst>(MI))
            {
                builder.CreateCall(isa<MemMoveInst>(MI) ? CacheMemMove : CacheMemCpy,
                                   {builder.CreatePointerCast(MI->getOperand(0), ptrTy),
                                    builder.CreatePointerCast(MI->getOperand(1), ptrTy),
                                    builder.CreateIntCast(MI->getOperand(2), sizeTy, false)});
            }
            else if (isa<MemSetInst>(MI))
            {
                builder.CreateCall(CacheMemSet, {builder.CreatePointerCast(MI->getOperand(0), ptrTy),
                                                 builder.CreateIntCast(MI->getOperand(1), intTy, false),
                                                 builder.CreateIntCast(MI->getOperand(2), sizeTy, false)});
            }
            MI->eraseFromParent();
        }
        for (auto RMW : AtomicRMWs)
            ReplaceAddrOp(RMW, RMW->getPointerOperandIndex(), false);
        for (auto CX : AtomicCXs)
            ReplaceAddrOp(CX, CX->getPointerOperandIndex(), false);
    }
    return PreservedAnalyses::none();
}

extern "C" ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo()
{
    return {LLVM_PLUGIN_API_VERSION, "TriCache Instrumentation", "0.0.1",
            [](PassBuilder &PB)
            {
                PB.registerOptimizerLastEPCallback(
                    [&](ModulePassManager &MPM, PassBuilder::OptimizationLevel Level)
                    {
                        MPM.addPass(createModuleToFunctionPassAdaptor(TriCacheInstrumentPass()));
                        cantFail(PB.parsePassPipeline(MPM, "always-inline,globaldce"));
                    });
            }};
}
