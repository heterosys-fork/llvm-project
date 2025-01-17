//===- LLVMToLLVMIRTranslation.cpp - Translate LLVM dialect to LLVM IR ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a translation between the MLIR LLVM dialect and LLVM IR.
//
//===----------------------------------------------------------------------===//

#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Operation.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Target/LLVMIR/ModuleTranslation.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/MatrixBuilder.h"
#include "llvm/IR/Operator.h"

using namespace mlir;
using namespace mlir::LLVM;
using mlir::LLVM::detail::getLLVMConstant;

#include "mlir/Dialect/LLVMIR/LLVMConversionEnumsToLLVM.inc"

/// Convert MLIR integer comparison predicate to LLVM IR comparison predicate.
static llvm::CmpInst::Predicate getLLVMCmpPredicate(ICmpPredicate p) {
  switch (p) {
  case LLVM::ICmpPredicate::eq:
    return llvm::CmpInst::Predicate::ICMP_EQ;
  case LLVM::ICmpPredicate::ne:
    return llvm::CmpInst::Predicate::ICMP_NE;
  case LLVM::ICmpPredicate::slt:
    return llvm::CmpInst::Predicate::ICMP_SLT;
  case LLVM::ICmpPredicate::sle:
    return llvm::CmpInst::Predicate::ICMP_SLE;
  case LLVM::ICmpPredicate::sgt:
    return llvm::CmpInst::Predicate::ICMP_SGT;
  case LLVM::ICmpPredicate::sge:
    return llvm::CmpInst::Predicate::ICMP_SGE;
  case LLVM::ICmpPredicate::ult:
    return llvm::CmpInst::Predicate::ICMP_ULT;
  case LLVM::ICmpPredicate::ule:
    return llvm::CmpInst::Predicate::ICMP_ULE;
  case LLVM::ICmpPredicate::ugt:
    return llvm::CmpInst::Predicate::ICMP_UGT;
  case LLVM::ICmpPredicate::uge:
    return llvm::CmpInst::Predicate::ICMP_UGE;
  }
  llvm_unreachable("incorrect comparison predicate");
}

static llvm::CmpInst::Predicate getLLVMCmpPredicate(FCmpPredicate p) {
  switch (p) {
  case LLVM::FCmpPredicate::_false:
    return llvm::CmpInst::Predicate::FCMP_FALSE;
  case LLVM::FCmpPredicate::oeq:
    return llvm::CmpInst::Predicate::FCMP_OEQ;
  case LLVM::FCmpPredicate::ogt:
    return llvm::CmpInst::Predicate::FCMP_OGT;
  case LLVM::FCmpPredicate::oge:
    return llvm::CmpInst::Predicate::FCMP_OGE;
  case LLVM::FCmpPredicate::olt:
    return llvm::CmpInst::Predicate::FCMP_OLT;
  case LLVM::FCmpPredicate::ole:
    return llvm::CmpInst::Predicate::FCMP_OLE;
  case LLVM::FCmpPredicate::one:
    return llvm::CmpInst::Predicate::FCMP_ONE;
  case LLVM::FCmpPredicate::ord:
    return llvm::CmpInst::Predicate::FCMP_ORD;
  case LLVM::FCmpPredicate::ueq:
    return llvm::CmpInst::Predicate::FCMP_UEQ;
  case LLVM::FCmpPredicate::ugt:
    return llvm::CmpInst::Predicate::FCMP_UGT;
  case LLVM::FCmpPredicate::uge:
    return llvm::CmpInst::Predicate::FCMP_UGE;
  case LLVM::FCmpPredicate::ult:
    return llvm::CmpInst::Predicate::FCMP_ULT;
  case LLVM::FCmpPredicate::ule:
    return llvm::CmpInst::Predicate::FCMP_ULE;
  case LLVM::FCmpPredicate::une:
    return llvm::CmpInst::Predicate::FCMP_UNE;
  case LLVM::FCmpPredicate::uno:
    return llvm::CmpInst::Predicate::FCMP_UNO;
  case LLVM::FCmpPredicate::_true:
    return llvm::CmpInst::Predicate::FCMP_TRUE;
  }
  llvm_unreachable("incorrect comparison predicate");
}

static llvm::AtomicRMWInst::BinOp getLLVMAtomicBinOp(AtomicBinOp op) {
  switch (op) {
  case LLVM::AtomicBinOp::xchg:
    return llvm::AtomicRMWInst::BinOp::Xchg;
  case LLVM::AtomicBinOp::add:
    return llvm::AtomicRMWInst::BinOp::Add;
  case LLVM::AtomicBinOp::sub:
    return llvm::AtomicRMWInst::BinOp::Sub;
  case LLVM::AtomicBinOp::_and:
    return llvm::AtomicRMWInst::BinOp::And;
  case LLVM::AtomicBinOp::nand:
    return llvm::AtomicRMWInst::BinOp::Nand;
  case LLVM::AtomicBinOp::_or:
    return llvm::AtomicRMWInst::BinOp::Or;
  case LLVM::AtomicBinOp::_xor:
    return llvm::AtomicRMWInst::BinOp::Xor;
  case LLVM::AtomicBinOp::max:
    return llvm::AtomicRMWInst::BinOp::Max;
  case LLVM::AtomicBinOp::min:
    return llvm::AtomicRMWInst::BinOp::Min;
  case LLVM::AtomicBinOp::umax:
    return llvm::AtomicRMWInst::BinOp::UMax;
  case LLVM::AtomicBinOp::umin:
    return llvm::AtomicRMWInst::BinOp::UMin;
  case LLVM::AtomicBinOp::fadd:
    return llvm::AtomicRMWInst::BinOp::FAdd;
  case LLVM::AtomicBinOp::fsub:
    return llvm::AtomicRMWInst::BinOp::FSub;
  }
  llvm_unreachable("incorrect atomic binary operator");
}

static llvm::AtomicOrdering getLLVMAtomicOrdering(AtomicOrdering ordering) {
  switch (ordering) {
  case LLVM::AtomicOrdering::not_atomic:
    return llvm::AtomicOrdering::NotAtomic;
  case LLVM::AtomicOrdering::unordered:
    return llvm::AtomicOrdering::Unordered;
  case LLVM::AtomicOrdering::monotonic:
    return llvm::AtomicOrdering::Monotonic;
  case LLVM::AtomicOrdering::acquire:
    return llvm::AtomicOrdering::Acquire;
  case LLVM::AtomicOrdering::release:
    return llvm::AtomicOrdering::Release;
  case LLVM::AtomicOrdering::acq_rel:
    return llvm::AtomicOrdering::AcquireRelease;
  case LLVM::AtomicOrdering::seq_cst:
    return llvm::AtomicOrdering::SequentiallyConsistent;
  }
  llvm_unreachable("incorrect atomic ordering");
}

static llvm::FastMathFlags getFastmathFlags(FastmathFlagsInterface &op) {
  using llvmFMF = llvm::FastMathFlags;
  using FuncT = void (llvmFMF::*)(bool);
  const std::pair<FastmathFlags, FuncT> handlers[] = {
      // clang-format off
      {FastmathFlags::nnan,     &llvmFMF::setNoNaNs},
      {FastmathFlags::ninf,     &llvmFMF::setNoInfs},
      {FastmathFlags::nsz,      &llvmFMF::setNoSignedZeros},
      {FastmathFlags::arcp,     &llvmFMF::setAllowReciprocal},
      {FastmathFlags::contract, &llvmFMF::setAllowContract},
      {FastmathFlags::afn,      &llvmFMF::setApproxFunc},
      {FastmathFlags::reassoc,  &llvmFMF::setAllowReassoc},
      // clang-format on
  };
  llvm::FastMathFlags ret;
  ::mlir::LLVM::FastmathFlags fmfMlir = op.getFastmathAttr().getValue();
  for (auto it : handlers)
    if (bitEnumContainsAll(fmfMlir, it.first))
      (ret.*(it.second))(true);
  return ret;
}

/// Returns an LLVM metadata node corresponding to a loop option. This metadata
/// is attached to an llvm.loop node.
static llvm::MDNode *getLoopOptionMetadata(llvm::LLVMContext &ctx,
                                           LoopOptionCase option,
                                           int64_t value) {
  StringRef name;
  llvm::Constant *cstValue = nullptr;
  switch (option) {
  case LoopOptionCase::disable_licm:
    name = "llvm.licm.disable";
    cstValue = llvm::ConstantInt::getBool(ctx, value);
    break;
  case LoopOptionCase::disable_unroll:
    name = "llvm.loop.unroll.disable";
    cstValue = llvm::ConstantInt::getBool(ctx, value);
    break;
  case LoopOptionCase::interleave_count:
    name = "llvm.loop.interleave.count";
    cstValue = llvm::ConstantInt::get(
        llvm::IntegerType::get(ctx, /*NumBits=*/32), value);
    break;
  case LoopOptionCase::disable_pipeline:
    name = "llvm.loop.pipeline.disable";
    cstValue = llvm::ConstantInt::getBool(ctx, value);
    break;
  case LoopOptionCase::pipeline_initiation_interval:
    name = "llvm.loop.pipeline.initiationinterval";
    cstValue = llvm::ConstantInt::get(
        llvm::IntegerType::get(ctx, /*NumBits=*/32), value);
    break;
  }
  return llvm::MDNode::get(ctx, {llvm::MDString::get(ctx, name),
                                 llvm::ConstantAsMetadata::get(cstValue)});
}

static void setLoopMetadata(Operation &opInst, llvm::Instruction &llvmInst,
                            llvm::IRBuilderBase &builder,
                            LLVM::ModuleTranslation &moduleTranslation) {

  Attribute attr = opInst.getAttr(LLVMDialect::getLoopAttrName());
  
  // If Xilinx extensions exist, the loop metadata is set as well.
  Attribute altAttr = opInst.getAttr("llvm.loop.name");
  if (!altAttr) altAttr = opInst.getAttr("llvm.loop.vectorize.width");
  if (!altAttr) altAttr = opInst.getAttr("llvm.loop.interleave.count");
  if (!altAttr) altAttr = opInst.getAttr("llvm.loop.unroll.count");
  if (!altAttr) altAttr = opInst.getAttr("llvm.loop.unroll.withoutcheck");
  if (!altAttr) altAttr = opInst.getAttr("llvm.loop.vectorize.enable");
  if (!altAttr) altAttr = opInst.getAttr("llvm.loop.distribute.enable");
  if (!altAttr) altAttr = opInst.getAttr("llvm.loop.flatten.enable");
  if (!altAttr) altAttr = opInst.getAttr("llvm.loop.dataflow.enable");
  if (!altAttr) altAttr = opInst.getAttr("llvm.loop.pipeline.enable");
  if (!altAttr) altAttr = opInst.getAttr("llvm.loop.latency");
  if (!altAttr) altAttr = opInst.getAttr("llvm.loop.tripcount");

  if (attr || altAttr) {
    llvm::Module *module = builder.GetInsertBlock()->getModule();
    llvm::MDNode *loopMD = nullptr;

    if (attr) loopMD = moduleTranslation.lookupLoopOptionsMetadata(attr);
    if (altAttr) loopMD = moduleTranslation.lookupLoopOptionsMetadata(altAttr);

    if (!loopMD) {
      llvm::LLVMContext &ctx = module->getContext();

      SmallVector<llvm::Metadata *> loopOptions;
      // Reserve operand 0 for loop id self reference.
      auto dummy = llvm::MDNode::getTemporary(ctx, llvm::None);
      loopOptions.push_back(dummy.get());

      if (attr) {
        auto loopAttr = attr.cast<DictionaryAttr>();
        auto parallelAccessGroup =
            loopAttr.getNamed(LLVMDialect::getParallelAccessAttrName());
        if (parallelAccessGroup) {
          SmallVector<llvm::Metadata *> parallelAccess;
          parallelAccess.push_back(
              llvm::MDString::get(ctx, "llvm.loop.parallel_accesses"));
          for (SymbolRefAttr accessGroupRef : parallelAccessGroup->getValue()
                                                  .cast<ArrayAttr>()
                                                  .getAsRange<SymbolRefAttr>())
            parallelAccess.push_back(
                moduleTranslation.getAccessGroup(opInst, accessGroupRef));
          loopOptions.push_back(llvm::MDNode::get(ctx, parallelAccess));
        }

        if (auto loopOptionsAttr = loopAttr.getAs<LoopOptionsAttr>(
                LLVMDialect::getLoopOptionsAttrName())) {
          for (auto option : loopOptionsAttr.getOptions())
            loopOptions.push_back(
                getLoopOptionMetadata(ctx, option.first, option.second));
        }
      }

      // Set Xilinx extensions
      // The folllowing are adapted from clang/lib/CodeGen/CGLoopInfo.cpp
      // of Xilinx/hls-llvm-project and may be copyrighted by Xilinx.
      // The original code was licensed with Apache.
      if (Attribute attr = opInst.getAttr("llvm.loop.name")) {
        auto loopAttr = attr.cast<StringAttr>();
        loopOptions.push_back(
            llvm::MDNode::get(ctx, {llvm::MDString::get(ctx, "llvm.loop.name"),
                                    llvm::MDString::get(ctx, loopAttr.str())}));
      }

      auto createIntAttr = [&](llvm::Type *type, llvm::StringRef name) {
        if (Attribute attr = opInst.getAttr(name)) {
          auto loopAttr = attr.cast<IntegerAttr>();
          loopOptions.push_back(llvm::MDNode::get(
              ctx, {llvm::MDString::get(ctx, name),
                    llvm::ConstantAsMetadata::get(
                        llvm::ConstantInt::get(type, loopAttr.getInt()))}));
        }
      };

      auto *i32Type = llvm::Type::getInt32Ty(ctx);
      createIntAttr(i32Type, "llvm.loop.vectorize.width");
      createIntAttr(i32Type, "llvm.loop.interleave.count");
      createIntAttr(i32Type, "llvm.loop.unroll.count");
      createIntAttr(i32Type, "llvm.loop.unroll.withoutcheck");

      auto *i1Type = llvm::Type::getInt1Ty(ctx);
      createIntAttr(i1Type, "llvm.loop.vectorize.enable");
      createIntAttr(i1Type, "llvm.loop.distribute.enable");
      createIntAttr(i1Type, "llvm.loop.flatten.enable");
      createIntAttr(i1Type, "llvm.loop.dataflow.enable");

      auto createUnitAttr = [&](llvm::StringRef name) {
        if (Attribute attr = opInst.getAttr(name)) {
          loopOptions.push_back(
              llvm::MDNode::get(ctx, {llvm::MDString::get(ctx, name)}));
        }
      };
      createUnitAttr("llvm.loop.unroll.enable");
      createUnitAttr("llvm.loop.unroll.full");
      createUnitAttr("llvm.loop.unroll.disable");

      // Special attributes with multiple operands
      if (Attribute attr = opInst.getAttr("llvm.loop.pipeline.enable")) {
        auto loopAttr = attr.cast<ArrayAttr>();
        auto pipelineII = loopAttr.getValue()[0].cast<IntegerAttr>();
        auto rewind = loopAttr.getValue()[1].cast<IntegerAttr>();
        auto pipelineStyle = loopAttr.getValue()[2].cast<IntegerAttr>();
        loopOptions.push_back(llvm::MDNode::get(
            ctx, {llvm::MDString::get(ctx, "llvm.loop.pipeline.enable"),
                  llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                      llvm::Type::getInt32Ty(ctx), pipelineII.getInt())),
                  llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                      llvm::Type::getInt1Ty(ctx), rewind.getInt())),
                  llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                      llvm::Type::getInt8Ty(ctx), pipelineStyle.getInt()))}));
      }

      if (Attribute attr = opInst.getAttr("llvm.loop.latency")) {
        auto loopAttr = attr.cast<ArrayAttr>();
        auto min = loopAttr.getValue()[0].cast<IntegerAttr>();
        auto max = loopAttr.getValue()[1].cast<IntegerAttr>();
        loopOptions.push_back(llvm::MDNode::get(
            ctx, {llvm::MDString::get(ctx, "llvm.loop.latency"),
                  llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                      llvm::Type::getInt32Ty(ctx), min.getInt())),
                  llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                      llvm::Type::getInt32Ty(ctx), max.getInt()))}));
      }

      if (Attribute attr = opInst.getAttr("llvm.loop.tripcount")) {
        auto loopAttr = attr.cast<ArrayAttr>();
        auto v0 = loopAttr.getValue()[0].cast<IntegerAttr>();
        auto v1 = loopAttr.getValue()[1].cast<IntegerAttr>();
        auto v2 = loopAttr.getValue()[2].cast<IntegerAttr>();
        loopOptions.push_back(llvm::MDNode::get(
            ctx, {llvm::MDString::get(ctx, "llvm.loop.tripcount"),
                  llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                      llvm::Type::getInt32Ty(ctx), v0.getInt())),
                  llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                      llvm::Type::getInt32Ty(ctx), v1.getInt())),
                  llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                      llvm::Type::getInt32Ty(ctx), v2.getInt()))}));
      }

      // Create loop options and set the first operand to itself.
      loopMD = llvm::MDNode::get(ctx, loopOptions);
      loopMD->replaceOperandWith(0, loopMD);

      // Store a map from this Attribute to the LLVM metadata in case we
      // encounter it again.
      if (attr) moduleTranslation.mapLoopOptionsMetadata(attr, loopMD);
      if (altAttr) moduleTranslation.mapLoopOptionsMetadata(altAttr, loopMD);
    }

    llvmInst.setMetadata(module->getMDKindID("llvm.loop"), loopMD);
  }
}

/// Convert the value of a DenseI64ArrayAttr to a vector of unsigned indices.
static SmallVector<unsigned> extractPosition(ArrayRef<int64_t> indices) {
  SmallVector<unsigned> position;
  llvm::append_range(position, indices);
  return position;
}

/// Get the declaration of an overloaded llvm intrinsic. First we get the
/// overloaded argument types and/or result type from the CallIntrinsicOp, and
/// then use those to get the correct declaration of the overloaded intrinsic.
static FailureOr<llvm::Function *>
getOverloadedDeclaration(CallIntrinsicOp &op, llvm::Intrinsic::ID id,
                         llvm::Module *module,
                         LLVM::ModuleTranslation &moduleTranslation) {
  SmallVector<llvm::Type *, 8> allArgTys;
  for (Type type : op->getOperandTypes())
    allArgTys.push_back(moduleTranslation.convertType(type));

  llvm::Type *resTy;
  if (op.getNumResults() == 0)
    resTy = llvm::Type::getVoidTy(module->getContext());
  else
    resTy = moduleTranslation.convertType(op.getResult(0).getType());

  // ATM we do not support variadic intrinsics.
  llvm::FunctionType *ft = llvm::FunctionType::get(resTy, allArgTys, false);

  SmallVector<llvm::Intrinsic::IITDescriptor, 8> table;
  getIntrinsicInfoTableEntries(id, table);
  ArrayRef<llvm::Intrinsic::IITDescriptor> tableRef = table;

  SmallVector<llvm::Type *, 8> overloadedArgTys;
  if (llvm::Intrinsic::matchIntrinsicSignature(ft, tableRef,
                                               overloadedArgTys) !=
      llvm::Intrinsic::MatchIntrinsicTypesResult::MatchIntrinsicTypes_Match) {
    return op.emitOpError("intrinsic type is not a match");
  }

  ArrayRef<llvm::Type *> overloadedArgTysRef = overloadedArgTys;
  return llvm::Intrinsic::getDeclaration(module, id, overloadedArgTysRef);
}

/// Builder for LLVM_CallIntrinsicOp
static LogicalResult
convertCallLLVMIntrinsicOp(CallIntrinsicOp &op, llvm::IRBuilderBase &builder,
                           LLVM::ModuleTranslation &moduleTranslation) {
  llvm::Module *module = builder.GetInsertBlock()->getModule();
  llvm::Intrinsic::ID id =
      llvm::Function::lookupIntrinsicID(op.getIntrinAttr());
  if (!id)
    return op.emitOpError()
           << "couldn't find intrinsic: " << op.getIntrinAttr();

  llvm::Function *fn = nullptr;
  if (llvm::Intrinsic::isOverloaded(id)) {
    auto fnOrFailure =
        getOverloadedDeclaration(op, id, module, moduleTranslation);
    if (failed(fnOrFailure))
      return failure();
    fn = fnOrFailure.value();
  } else {
    fn = llvm::Intrinsic::getDeclaration(module, id, {});
  }

  auto *inst =
      builder.CreateCall(fn, moduleTranslation.lookupValues(op.getOperands()));
  if (op.getNumResults() == 1)
    moduleTranslation.mapValue(op->getResults().front()) = inst;
  return success();
}

static LogicalResult
convertOperationImpl(Operation &opInst, llvm::IRBuilderBase &builder,
                     LLVM::ModuleTranslation &moduleTranslation) {

  llvm::IRBuilder<>::FastMathFlagGuard fmfGuard(builder);
  if (auto fmf = dyn_cast<FastmathFlagsInterface>(opInst))
    builder.setFastMathFlags(getFastmathFlags(fmf));

#include "mlir/Dialect/LLVMIR/LLVMConversions.inc"
#include "mlir/Dialect/LLVMIR/LLVMIntrinsicConversions.inc"

  // Emit function calls.  If the "callee" attribute is present, this is a
  // direct function call and we also need to look up the remapped function
  // itself.  Otherwise, this is an indirect call and the callee is the first
  // operand, look it up as a normal value.  Return the llvm::Value
  // representing the function result, which may be of llvm::VoidTy type.
  auto convertCall = [&](Operation &op) -> llvm::Value * {
    auto operands = moduleTranslation.lookupValues(op.getOperands());
    ArrayRef<llvm::Value *> operandsRef(operands);
    if (auto attr = op.getAttrOfType<FlatSymbolRefAttr>("callee"))
      return builder.CreateCall(
          moduleTranslation.lookupFunction(attr.getValue()), operandsRef);
    auto calleeType =
        op.getOperands().front().getType().cast<LLVMPointerType>();
    auto *calleeFunctionType = cast<llvm::FunctionType>(
        moduleTranslation.convertType(calleeType.getElementType()));
    return builder.CreateCall(calleeFunctionType, operandsRef.front(),
                              operandsRef.drop_front());
  };

  // Emit calls.  If the called function has a result, remap the corresponding
  // value.  Note that LLVM IR dialect CallOp has either 0 or 1 result.
  if (isa<LLVM::CallOp>(opInst)) {
    llvm::Value *result = convertCall(opInst);
    if (opInst.getNumResults() != 0) {
      moduleTranslation.mapValue(opInst.getResult(0), result);
      return success();
    }
    // Check that LLVM call returns void for 0-result functions.
    return success(result->getType()->isVoidTy());
  }

  if (auto inlineAsmOp = dyn_cast<LLVM::InlineAsmOp>(opInst)) {
    // TODO: refactor function type creation which usually occurs in std-LLVM
    // conversion.
    SmallVector<Type, 8> operandTypes;
    llvm::append_range(operandTypes, inlineAsmOp.getOperands().getTypes());

    Type resultType;
    if (inlineAsmOp.getNumResults() == 0) {
      resultType = LLVM::LLVMVoidType::get(&moduleTranslation.getContext());
    } else {
      assert(inlineAsmOp.getNumResults() == 1);
      resultType = inlineAsmOp.getResultTypes()[0];
    }
    auto ft = LLVM::LLVMFunctionType::get(resultType, operandTypes);
    llvm::InlineAsm *inlineAsmInst =
        inlineAsmOp.getAsmDialect()
            ? llvm::InlineAsm::get(
                  static_cast<llvm::FunctionType *>(
                      moduleTranslation.convertType(ft)),
                  inlineAsmOp.getAsmString(), inlineAsmOp.getConstraints(),
                  inlineAsmOp.getHasSideEffects(),
                  inlineAsmOp.getIsAlignStack(),
                  convertAsmDialectToLLVM(*inlineAsmOp.getAsmDialect()))
            : llvm::InlineAsm::get(static_cast<llvm::FunctionType *>(
                                       moduleTranslation.convertType(ft)),
                                   inlineAsmOp.getAsmString(),
                                   inlineAsmOp.getConstraints(),
                                   inlineAsmOp.getHasSideEffects(),
                                   inlineAsmOp.getIsAlignStack());
    llvm::CallInst *inst = builder.CreateCall(
        inlineAsmInst,
        moduleTranslation.lookupValues(inlineAsmOp.getOperands()));
    if (auto maybeOperandAttrs = inlineAsmOp.getOperandAttrs()) {
      llvm::AttributeList attrList;
      for (const auto &it : llvm::enumerate(*maybeOperandAttrs)) {
        Attribute attr = it.value();
        if (!attr)
          continue;
        DictionaryAttr dAttr = attr.cast<DictionaryAttr>();
        TypeAttr tAttr =
            dAttr.get(InlineAsmOp::getElementTypeAttrName()).cast<TypeAttr>();
        llvm::AttrBuilder b(moduleTranslation.getLLVMContext());
        llvm::Type *ty = moduleTranslation.convertType(tAttr.getValue());
        b.addTypeAttr(llvm::Attribute::ElementType, ty);
        // shift to account for the returned value (this is always 1 aggregate
        // value in LLVM).
        int shift = (opInst.getNumResults() > 0) ? 1 : 0;
        attrList = attrList.addAttributesAtIndex(
            moduleTranslation.getLLVMContext(), it.index() + shift, b);
      }
      inst->setAttributes(attrList);
    }

    if (opInst.getNumResults() != 0)
      moduleTranslation.mapValue(opInst.getResult(0), inst);
    return success();
  }

  if (auto invOp = dyn_cast<LLVM::InvokeOp>(opInst)) {
    auto operands = moduleTranslation.lookupValues(invOp.getCalleeOperands());
    ArrayRef<llvm::Value *> operandsRef(operands);
    llvm::Instruction *result;
    if (auto attr = opInst.getAttrOfType<FlatSymbolRefAttr>("callee")) {
      result = builder.CreateInvoke(
          moduleTranslation.lookupFunction(attr.getValue()),
          moduleTranslation.lookupBlock(invOp.getSuccessor(0)),
          moduleTranslation.lookupBlock(invOp.getSuccessor(1)), operandsRef);
    } else {
      auto calleeType =
          invOp.getCalleeOperands().front().getType().cast<LLVMPointerType>();
      auto *calleeFunctionType = cast<llvm::FunctionType>(
          moduleTranslation.convertType(calleeType.getElementType()));
      result = builder.CreateInvoke(
          calleeFunctionType, operandsRef.front(),
          moduleTranslation.lookupBlock(invOp.getSuccessor(0)),
          moduleTranslation.lookupBlock(invOp.getSuccessor(1)),
          operandsRef.drop_front());
    }
    moduleTranslation.mapBranch(invOp, result);
    // InvokeOp can only have 0 or 1 result
    if (invOp->getNumResults() != 0) {
      moduleTranslation.mapValue(opInst.getResult(0), result);
      return success();
    }
    return success(result->getType()->isVoidTy());
  }

  if (auto lpOp = dyn_cast<LLVM::LandingpadOp>(opInst)) {
    llvm::Type *ty = moduleTranslation.convertType(lpOp.getType());
    llvm::LandingPadInst *lpi =
        builder.CreateLandingPad(ty, lpOp.getNumOperands());
    lpi->setCleanup(lpOp.getCleanup());

    // Add clauses
    for (llvm::Value *operand :
         moduleTranslation.lookupValues(lpOp.getOperands())) {
      // All operands should be constant - checked by verifier
      if (auto *constOperand = dyn_cast<llvm::Constant>(operand))
        lpi->addClause(constOperand);
    }
    moduleTranslation.mapValue(lpOp.getResult(), lpi);
    return success();
  }

  // Emit branches.  We need to look up the remapped blocks and ignore the
  // block arguments that were transformed into PHI nodes.
  if (auto brOp = dyn_cast<LLVM::BrOp>(opInst)) {
    llvm::BranchInst *branch =
        builder.CreateBr(moduleTranslation.lookupBlock(brOp.getSuccessor()));
    moduleTranslation.mapBranch(&opInst, branch);
    setLoopMetadata(opInst, *branch, builder, moduleTranslation);
    return success();
  }
  if (auto condbrOp = dyn_cast<LLVM::CondBrOp>(opInst)) {
    llvm::MDNode *branchWeights = nullptr;
    if (auto weights = condbrOp.getBranchWeights()) {
      // Map weight attributes to LLVM metadata.
      auto weightValues = weights->getValues<APInt>();
      auto trueWeight = weightValues[0].getSExtValue();
      auto falseWeight = weightValues[1].getSExtValue();
      branchWeights =
          llvm::MDBuilder(moduleTranslation.getLLVMContext())
              .createBranchWeights(static_cast<uint32_t>(trueWeight),
                                   static_cast<uint32_t>(falseWeight));
    }
    llvm::BranchInst *branch = builder.CreateCondBr(
        moduleTranslation.lookupValue(condbrOp.getOperand(0)),
        moduleTranslation.lookupBlock(condbrOp.getSuccessor(0)),
        moduleTranslation.lookupBlock(condbrOp.getSuccessor(1)), branchWeights);
    moduleTranslation.mapBranch(&opInst, branch);
    setLoopMetadata(opInst, *branch, builder, moduleTranslation);
    return success();
  }
  if (auto switchOp = dyn_cast<LLVM::SwitchOp>(opInst)) {
    llvm::MDNode *branchWeights = nullptr;
    if (auto weights = switchOp.getBranchWeights()) {
      llvm::SmallVector<uint32_t> weightValues;
      weightValues.reserve(weights->size());
      for (llvm::APInt weight : weights->cast<DenseIntElementsAttr>())
        weightValues.push_back(weight.getLimitedValue());
      branchWeights = llvm::MDBuilder(moduleTranslation.getLLVMContext())
                          .createBranchWeights(weightValues);
    }

    llvm::SwitchInst *switchInst = builder.CreateSwitch(
        moduleTranslation.lookupValue(switchOp.getValue()),
        moduleTranslation.lookupBlock(switchOp.getDefaultDestination()),
        switchOp.getCaseDestinations().size(), branchWeights);

    auto *ty = llvm::cast<llvm::IntegerType>(
        moduleTranslation.convertType(switchOp.getValue().getType()));
    for (auto i :
         llvm::zip(switchOp.getCaseValues()->cast<DenseIntElementsAttr>(),
                   switchOp.getCaseDestinations()))
      switchInst->addCase(
          llvm::ConstantInt::get(ty, std::get<0>(i).getLimitedValue()),
          moduleTranslation.lookupBlock(std::get<1>(i)));

    moduleTranslation.mapBranch(&opInst, switchInst);
    return success();
  }

  // Emit addressof.  We need to look up the global value referenced by the
  // operation and store it in the MLIR-to-LLVM value mapping.  This does not
  // emit any LLVM instruction.
  if (auto addressOfOp = dyn_cast<LLVM::AddressOfOp>(opInst)) {
    LLVM::GlobalOp global =
        addressOfOp.getGlobal(moduleTranslation.symbolTable());
    LLVM::LLVMFuncOp function =
        addressOfOp.getFunction(moduleTranslation.symbolTable());

    // The verifier should not have allowed this.
    assert((global || function) &&
           "referencing an undefined global or function");

    moduleTranslation.mapValue(
        addressOfOp.getResult(),
        global ? moduleTranslation.lookupGlobal(global)
               : moduleTranslation.lookupFunction(function.getName()));
    return success();
  }

  return failure();
}

namespace {
/// Implementation of the dialect interface that converts operations belonging
/// to the LLVM dialect to LLVM IR.
class LLVMDialectLLVMIRTranslationInterface
    : public LLVMTranslationDialectInterface {
public:
  using LLVMTranslationDialectInterface::LLVMTranslationDialectInterface;

  /// Translates the given operation to LLVM IR using the provided IR builder
  /// and saving the state in `moduleTranslation`.
  LogicalResult
  convertOperation(Operation *op, llvm::IRBuilderBase &builder,
                   LLVM::ModuleTranslation &moduleTranslation) const final {
    return convertOperationImpl(*op, builder, moduleTranslation);
  }
};
} // namespace

void mlir::registerLLVMDialectTranslation(DialectRegistry &registry) {
  registry.insert<LLVM::LLVMDialect>();
  registry.addExtension(+[](MLIRContext *ctx, LLVM::LLVMDialect *dialect) {
    dialect->addInterfaces<LLVMDialectLLVMIRTranslationInterface>();
  });
}

void mlir::registerLLVMDialectTranslation(MLIRContext &context) {
  DialectRegistry registry;
  registerLLVMDialectTranslation(registry);
  context.appendDialectRegistry(registry);
}
