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
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

class TriCacheAllocPass : public PassInfoMixin<TriCacheAllocPass>
{
public:
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
    static bool isRequired() { return true; }
};

PreservedAnalyses TriCacheAllocPass::run(Function &F, FunctionAnalysisManager &AM)
{
    auto &C = F.getContext();
    auto &M = *F.getParent();
    auto ptrTy = Type::getInt8PtrTy(C);
    auto sizeTy = Type::getInt64Ty(C);
    auto intTy = Type::getInt32Ty(C);
    std::unordered_map<std::string, FunctionCallee> HookedFunc;
    HookedFunc["malloc"] = M.getOrInsertFunction("cache_malloc_hook", ptrTy, sizeTy);
    HookedFunc["_Znwm"] = M.getOrInsertFunction("cache_malloc_hook", ptrTy, sizeTy);
    HookedFunc["_Znam"] = M.getOrInsertFunction("cache_malloc_hook", ptrTy, sizeTy);
    HookedFunc["calloc"] = M.getOrInsertFunction("cache_calloc_hook", ptrTy, sizeTy, sizeTy);
    HookedFunc["realloc"] = M.getOrInsertFunction("cache_realloc_hook", ptrTy, ptrTy, sizeTy);
    HookedFunc["aligned_alloc"] = M.getOrInsertFunction("cache_aligned_alloc_hook", ptrTy, sizeTy, sizeTy);
    HookedFunc["mmap"] = M.getOrInsertFunction("cache_mmap_hook", ptrTy, ptrTy, sizeTy, intTy, intTy, intTy, sizeTy);
    for (auto &BB : F)
    {
        SmallVector<std::pair<CallInst *, std::string>, 4> CIs;
        for (auto &I : BB)
        {
            if (auto CI = dyn_cast<CallInst>(&I))
                if (auto Callee = CI->getCalledFunction())
                {
                    auto Name = Callee->getName().str();
                    if (HookedFunc.count(Name))
                        CIs.emplace_back(CI, std::move(Name));
                }
        }
        for (auto &[CI, Name] : CIs)
        {
            SmallVector<Value *, 6> args;
            for (auto &argUse : CI->args())
                args.emplace_back(argUse);
            ReplaceInstWithInst(CI, CallInst::Create(HookedFunc[Name], args));
        }
    }
    return PreservedAnalyses::none();
}

extern "C" ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo()
{
    return {LLVM_PLUGIN_API_VERSION, "TriCache Instrumentation for Allocation", "0.0.1",
            [](PassBuilder &PB)
            {
                PB.registerPipelineStartEPCallback(
                    [&](ModulePassManager &MPM, PassBuilder::OptimizationLevel Level)
                    { MPM.addPass(createModuleToFunctionPassAdaptor(TriCacheAllocPass())); });
            }};
}
