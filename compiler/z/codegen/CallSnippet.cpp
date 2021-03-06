/*******************************************************************************
 *
 * (c) Copyright IBM Corp. 2000, 2016
 *
 *  This program and the accompanying materials are made available
 *  under the terms of the Eclipse Public License v1.0 and
 *  Apache License v2.0 which accompanies this distribution.
 *
 *      The Eclipse Public License is available at
 *      http://www.eclipse.org/legal/epl-v10.html
 *
 *      The Apache License v2.0 is available at
 *      http://www.opensource.org/licenses/apache2.0.php
 *
 * Contributors:
 *    Multiple authors (IBM Corp.) - initial implementation and documentation
 *******************************************************************************/

#include "z/codegen/CallSnippet.hpp"

#include <stddef.h>                          // for NULL
#include <stdint.h>                          // for int32_t, uint8_t, uint32_t, etc
#include "codegen/CodeGenerator.hpp"         // for CodeGenerator
#include "codegen/FrontEnd.hpp"              // for TR_FrontEnd
#include "codegen/InstOpCode.hpp"            // for InstOpCode, etc
#include "codegen/Instruction.hpp"           // for Instruction
#include "codegen/Linkage.hpp"               // for Linkage
#include "codegen/Machine.hpp"               // for Machine
#include "codegen/RealRegister.hpp"          // for RealRegister
#include "compile/Compilation.hpp"           // for Compilation
#include "compile/SymbolReferenceTable.hpp"  // for SymbolReferenceTable
#include "control/Options.hpp"
#include "control/Options_inlines.hpp"
#include "env/CompilerEnv.hpp"
#include "env/IO.hpp"
#include "env/jittypes.h"                    // for intptrj_t, uintptrj_t
#include "il/DataTypes.hpp"                  // for DataTypes::Address, etc
#include "il/Node.hpp"                       // for Node
#include "il/Node_inlines.hpp"               // for Node::getDataType, etc
#include "il/Symbol.hpp"                     // for Symbol
#include "il/SymbolReference.hpp"            // for SymbolReference
#include "il/symbol/LabelSymbol.hpp"         // for LabelSymbol
#include "il/symbol/MethodSymbol.hpp"        // for MethodSymbol
#include "infra/Assert.hpp"                  // for TR_ASSERT
#include "ras/Debug.hpp"                     // for TR_Debug
#include "runtime/Runtime.hpp"               // for TR_RuntimeHelper, etc
#include "z/codegen/S390Instruction.hpp"     // for toS390Cursor

#define TR_S390_ARG_SLOT_SIZE 4

uint8_t *
TR_S390CallSnippet::storeArgumentItem(TR::InstOpCode::Mnemonic op, uint8_t * buffer, TR::RealRegister * reg, int32_t offset,
   TR::CodeGenerator * cg)
   {
   TR::RealRegister * stackPtr = cg->getStackPointerRealRegister();
   TR::InstOpCode opCode(op);
   opCode.copyBinaryToBuffer(buffer);
   reg->setRegisterField(toS390Cursor(buffer));
   stackPtr->setBaseRegisterField(toS390Cursor(buffer));
   TR_ASSERT((offset & 0xfffff000) == 0, "CallSnippet: Unexpected displacement %d for storeArgumentItem", offset);
   *toS390Cursor(buffer) |= offset & 0x00000fff;
   return buffer + opCode.getInstructionLength();
   }

uint8_t *
TR_S390CallSnippet::loadArgumentItem(TR::InstOpCode::Mnemonic op, uint8_t * buffer, TR::RealRegister * reg, int32_t offset)
   {
   TR::RealRegister * stackPtr = cg()->getStackPointerRealRegister();
   TR::InstOpCode opCode(op);
   opCode.copyBinaryToBuffer(buffer);
   reg->setRegisterField(toS390Cursor(buffer));
   stackPtr->setBaseRegisterField(toS390Cursor(buffer));
   TR_ASSERT( (offset & 0xfffff000) == 0, "CallSnippet: Unexpected displacement for loadArgumentItem");
   *toS390Cursor(buffer) |= offset & 0x00000fff;
   return buffer + opCode.getInstructionLength();
   }


uint8_t *
TR_S390CallSnippet::S390flushArgumentsToStack(uint8_t * buffer, TR::Node * callNode, int32_t argSize, TR::CodeGenerator * cg)
   {
   int32_t intArgNum = 0, floatArgNum = 0, offset;
   TR::Machine *machine = cg->machine();
   TR::Linkage * linkage = cg->getLinkage(callNode->getSymbol()->castToMethodSymbol()->getLinkageConvention());

   int32_t argStart = callNode->getFirstArgumentIndex();
   bool rightToLeft = linkage->getRightToLeft() &&
	  //we want the arguments for induceOSR to be passed from left to right as in any other non-helper call
      callNode->getSymbolReference() != cg->symRefTab()->element(TR_induceOSRAtCurrentPC);
   if (rightToLeft)
      {
      offset = linkage->getOffsetToFirstParm();
      }
   else
      {
      offset = argSize + linkage->getOffsetToFirstParm();
      }

   for (int32_t i = argStart; i < callNode->getNumChildren(); i++)
      {
      TR::Node * child = callNode->getChild(i);
      switch (child->getDataType())
         {
         case TR::Int8:
         case TR::Int16:
         case TR::Int32:
            if (!rightToLeft)
               {
               offset -= TR::Compiler->target.is64Bit() ? 8 : 4;
               }
            if (intArgNum < linkage->getNumIntegerArgumentRegisters())
               {
               buffer = storeArgumentItem(TR::InstOpCode::ST, buffer,
                           machine->getS390RealRegister(linkage->getIntegerArgumentRegister(intArgNum)), offset, cg);
               }
            intArgNum++;

            if (rightToLeft)
               {
               offset += TR::Compiler->target.is64Bit() ? 8 : 4;
               }
            break;
         case TR::Address:
            if (!rightToLeft)
               {
               offset -= TR::Compiler->target.is64Bit() ? 8 : 4;
               }
            if (intArgNum < linkage->getNumIntegerArgumentRegisters())
               {
               buffer = storeArgumentItem(TR::InstOpCode::getStoreOpCode(), buffer,
                           machine->getS390RealRegister(linkage->getIntegerArgumentRegister(intArgNum)), offset, cg);
               }
            intArgNum++;

            if (rightToLeft)
               {
               offset += TR::Compiler->target.is64Bit() ? 8 : 4;
               }
            break;

         case TR::Int64:
            if (!rightToLeft)
               {
               offset -= (TR::Compiler->target.is64Bit() ? 16 : 8);
               }
            if (intArgNum < linkage->getNumIntegerArgumentRegisters())
               {
               if (TR::Compiler->target.is64Bit())
                  {
                  buffer = storeArgumentItem(TR::InstOpCode::STG, buffer,
                              machine->getS390RealRegister(linkage->getIntegerArgumentRegister(intArgNum)), offset, cg);
                  }
               else
                  {
                  buffer = storeArgumentItem(TR::InstOpCode::ST, buffer,
                              machine->getS390RealRegister(linkage->getIntegerArgumentRegister(intArgNum)), offset, cg);
                  if (intArgNum < linkage->getNumIntegerArgumentRegisters() - 1)
                     {
                     buffer = storeArgumentItem(TR::InstOpCode::ST, buffer,
                                 machine->getS390RealRegister(linkage->getIntegerArgumentRegister(intArgNum + 1)), offset + 4, cg);
                     }
                  }
               }
            intArgNum += TR::Compiler->target.is64Bit() ? 1 : 2;
            if (rightToLeft)
               {
               offset += TR::Compiler->target.is64Bit() ? 16 : 8;
               }
            break;

         case TR::Float:
            if (!rightToLeft)
               {
               offset -= TR::Compiler->target.is64Bit() ? 8 : 4;
               }
            if (floatArgNum < linkage->getNumFloatArgumentRegisters())
               {
               buffer = storeArgumentItem(TR::InstOpCode::STE, buffer,
                           machine->getS390RealRegister(linkage->getFloatArgumentRegister(floatArgNum)), offset, cg);
               }
            floatArgNum++;
            if (rightToLeft)
               {
               offset += TR::Compiler->target.is64Bit() ? 8 : 4;
               }
            break;

         case TR::Double:
            if (!rightToLeft)
               {
               offset -= TR::Compiler->target.is64Bit() ? 16 : 8;
               }
            if (floatArgNum < linkage->getNumFloatArgumentRegisters())
               {
               buffer = storeArgumentItem(TR::InstOpCode::STD, buffer,
                           machine->getS390RealRegister(linkage->getFloatArgumentRegister(floatArgNum)), offset, cg);
               }
            floatArgNum++;
            if (rightToLeft)
               {
               offset += TR::Compiler->target.is64Bit() ? 16 : 8;
               }
            break;
         }
      }

   return buffer;
   }

/**
 * @return the total instruction length in bytes for setting up arguments
 */
int32_t
TR_S390CallSnippet::instructionCountForArguments(TR::Node * callNode, TR::CodeGenerator * cg)
   {
   int32_t intArgNum = 0, floatArgNum = 0, count = 0;
   TR::Linkage* linkage = cg->getLinkage(callNode->getSymbol()->castToMethodSymbol()->getLinkageConvention());
   int32_t argStart = callNode->getFirstArgumentIndex();

   for (int32_t i = argStart; i < callNode->getNumChildren(); i++)
      {
      TR::Node * child = callNode->getChild(i);
      switch (child->getDataType())
         {
         case TR::Int8:
         case TR::Int16:
         case TR::Int32:
            if (intArgNum < linkage->getNumIntegerArgumentRegisters())
               {
               count += TR::InstOpCode::getInstructionLength(TR::InstOpCode::ST);
               }
            intArgNum++;
            break;
         case TR::Address:
            if (intArgNum < linkage->getNumIntegerArgumentRegisters())
               {
               count += TR::InstOpCode::getInstructionLength(TR::InstOpCode::getLoadOpCode());
               }
            intArgNum++;
            break;
         case TR::Int64:
            if (intArgNum < linkage->getNumIntegerArgumentRegisters())
               {
               count += TR::InstOpCode::getInstructionLength(TR::InstOpCode::getLoadOpCode());
               if ((TR::Compiler->target.is32Bit()) && intArgNum < linkage->getNumIntegerArgumentRegisters() - 1)
                  {
                  count += TR::InstOpCode::getInstructionLength(TR::InstOpCode::getLoadOpCode());
                  }
               }
            intArgNum += TR::Compiler->target.is64Bit() ? 1 : 2;
            break;
         case TR::Float:
            if (floatArgNum < linkage->getNumFloatArgumentRegisters())
               {
               count += TR::InstOpCode::getInstructionLength(TR::InstOpCode::LE);
               }
            floatArgNum++;
            break;
         case TR::Double:
            if (floatArgNum < linkage->getNumFloatArgumentRegisters())
               {
               count += TR::InstOpCode::getInstructionLength(TR::InstOpCode::LD);
               }
            floatArgNum++;
            break;
         }
      }
   return count;
   }

uint8_t *
TR_S390CallSnippet::getCallRA()
   {
   //Return Address is the address of the instr follows the branch instr
   TR_ASSERT( getBranchInstruction() != NULL, "CallSnippet: branchInstruction is NULL");
   return (uint8_t *) (getBranchInstruction()->getNext()->getBinaryEncoding());
   }


TR_RuntimeHelper
TR_S390CallSnippet::getHelper(TR::MethodSymbol * methodSymbol, TR::DataTypes type, TR::CodeGenerator * cg)
   {
   bool synchronised = methodSymbol->isSynchronised();

   if (methodSymbol->isVMInternalNative() || methodSymbol->isJITInternalNative())
      {
      return TR_S390nativeStaticHelper;
      }
   else
      {
      switch (type)
         {
         case TR::NoType:
            if (synchronised)
               {
               return TR_S390interpreterSyncVoidStaticGlue;
               }
            else
               {
               return TR_S390interpreterVoidStaticGlue;
               }
            break;

         case TR::Int8:
         case TR::Int16:
         case TR::Int32:
            if (synchronised)
               {
               return TR_S390interpreterSyncIntStaticGlue;
               }
            else
               {
               return TR_S390interpreterIntStaticGlue;
               }
            break;

         case TR::Address:
            if (TR::Compiler->target.is64Bit())
               {
               if (synchronised)
                  {
                  return TR_S390interpreterSyncLongStaticGlue;
                  }
               else
                  {
                  return TR_S390interpreterLongStaticGlue;
                  }
               }
            else
               {
               if (synchronised)
                  {
                  return TR_S390interpreterSyncIntStaticGlue;
                  }
               else
                  {
                  return TR_S390interpreterIntStaticGlue;
                  }
               }
            break;

         case TR::Int64:
            if (synchronised)
               {
               return TR_S390interpreterSyncLongStaticGlue;
               }
            else
               {
               return TR_S390interpreterLongStaticGlue;
               }
            break;

         case TR::Float:
            if (synchronised)
               {
               return TR_S390interpreterSyncFloatStaticGlue;
               }
            else
               {
               return TR_S390interpreterFloatStaticGlue;
               }
            break;

         case TR::Double:
            if (synchronised)
               {
               return TR_S390interpreterSyncDoubleStaticGlue;
               }
            else
               {
               return TR_S390interpreterDoubleStaticGlue;
               }
            break;

         default:
            TR_ASSERT(0, "Bad return data type for a call node.  DataType was %s\n", cg->getDebug()->getName(type));
            return (TR_RuntimeHelper) 0;
         }
      }
   }

TR_RuntimeHelper TR_S390CallSnippet::getInterpretedDispatchHelper(
   TR::SymbolReference *methodSymRef,
   TR::DataTypes        type)
   {
   TR::Compilation *comp = cg()->comp();
   TR::MethodSymbol * methodSymbol = methodSymRef->getSymbol()->castToMethodSymbol();
   bool isJitInduceOSRCall = false;
   if (methodSymbol->isHelper() &&
       methodSymRef == cg()->symRefTab()->element(TR_induceOSRAtCurrentPC))
      {
      isJitInduceOSRCall = true;
      }

   if (methodSymRef->isUnresolved() || comp->compileRelocatableCode())
      {
      TR_ASSERT(!isJitInduceOSRCall || !comp->compileRelocatableCode(), "calling jitInduceOSR is not supported yet under AOT\n");
      if (methodSymbol->isSpecial())
         return TR_S390interpreterUnresolvedSpecialGlue;
      else if (methodSymbol->isStatic())
         return TR_S390interpreterUnresolvedStaticGlue;
      else
         return TR_S390interpreterUnresolvedDirectVirtualGlue;
      }
   else if (isJitInduceOSRCall)
      return TR_induceOSRAtCurrentPC;
   else
      return getHelper(methodSymbol, type, cg());
   }



uint8_t *
TR_S390CallSnippet::emitSnippetBody()
   {
   TR_ASSERT(0, "TR_S390CallSnippet::emitSnippetBody not implemented");
   return NULL;
   }


uint32_t
TR_S390CallSnippet::getLength(int32_t  estimatedSnippetStart)
   {
   // *this   swipeable for debugger
   // length = instructionCountForArgsInBytes + (BASR + L(or LG) + BASR +3*sizeof(uintptrj_t)) + NOPs
   // number of pad bytes has not been set when this method is called to
   // estimate codebuffer size, so -- i'll put an conservative number here...
   return (instructionCountForArguments(getNode(), cg()) +
      getPICBinaryLength(cg()) +
      3 * sizeof(uintptrj_t) +
      getRuntimeInstrumentationOnOffInstructionLength(cg()) +
      sizeof(uintptrj_t));  // the last item is for padding
   }


void
TR_Debug::print(TR::FILE *pOutFile, TR_S390CallSnippet * snippet)
   {
   uint8_t * bufferPos = snippet->getSnippetLabel()->getCodeLocation();
   TR::Node * callNode = snippet->getNode();
   TR::SymbolReference * methodSymRef = snippet->getRealMethodSymbolReference();
   if(!methodSymRef)
      methodSymRef = callNode->getSymbolReference();

   TR::MethodSymbol * methodSymbol = methodSymRef->getSymbol()->castToMethodSymbol();
   TR::SymbolReference * glueRef;
   int8_t padbytes = snippet->getPadBytes();

   printSnippetLabel(pOutFile, snippet->getSnippetLabel(), bufferPos,
      methodSymRef->isUnresolved() ? "Unresolved Call Snippet" : "Call Snippet");

   bufferPos = printS390ArgumentsFlush(pOutFile, callNode, bufferPos, snippet->getSizeOfArguments());

   if (methodSymRef->isUnresolved() || _comp->compileRelocatableCode())
      {
      if (methodSymbol->isSpecial())
         {
         glueRef = _cg->getSymRef(TR_S390interpreterUnresolvedSpecialGlue);
         }
      else if (methodSymbol->isStatic())
         {
         glueRef = _cg->getSymRef(TR_S390interpreterUnresolvedStaticGlue);
         }
      else
         {
         glueRef = _cg->getSymRef(TR_S390interpreterUnresolvedDirectVirtualGlue);
         }
      }
   else
      {
      bool synchronised = methodSymbol->isSynchronised();

      if ((methodSymbol->isVMInternalNative() || methodSymbol->isJITInternalNative()))
         {
         glueRef = _cg->getSymRef(TR_S390nativeStaticHelper);
         }
      else
         {
         switch (callNode->getDataType())
            {
            case TR::NoType:
               if (synchronised)
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterSyncVoidStaticGlue);
                  }
               else
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterVoidStaticGlue);
                  }
               break;
            case TR::Int8:
            case TR::Int16:
            case TR::Int32:
               if (synchronised)
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterSyncIntStaticGlue);
                  }
               else
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterIntStaticGlue);

                  }
               break;
            case TR::Address:
            if (TR::Compiler->target.is64Bit())
               {
               if (synchronised)
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterSyncLongStaticGlue);
                  }
               else
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterLongStaticGlue);
                  }
               }
            else
               {
               if (synchronised)
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterSyncIntStaticGlue);
                  }
               else
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterIntStaticGlue);
                  }
               }
               break;

            case TR::Int64:
               if (synchronised)
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterSyncLongStaticGlue);
                  }
               else
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterLongStaticGlue);
                  }
               break;

            case TR::Float:
               if (synchronised)
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterSyncFloatStaticGlue);
                  }
               else
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterFloatStaticGlue);
                  }
               break;

            case TR::Double:
               if (synchronised)
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterSyncDoubleStaticGlue);
                  }
               else
                  {
                  glueRef = _cg->getSymRef(TR_S390interpreterDoubleStaticGlue);
                  }
               break;

            default:
               TR_ASSERT(0, "Bad return data type for a call node.  DataType was %s\n",
                  getName(callNode->getDataType()));
            }
         }
      }
   bufferPos = printRuntimeInstrumentationOnOffInstruction(pOutFile, bufferPos, false); // RIOFF

   if (snippet->getKind() == TR::Snippet::IsUnresolvedCall)
      {
      int lengthOfLoad = (TR::Compiler->target.is64Bit())?6:4;

      printPrefix(pOutFile, NULL, bufferPos, 6);
      trfprintf(pOutFile, "LARL \tGPR14, *+%d <%p>\t# Start of Data Const.",
                        8 + lengthOfLoad + padbytes,
                        bufferPos + 8 + lengthOfLoad + padbytes);
      bufferPos += 6;
      if (TR::Compiler->target.is64Bit())
         {
         printPrefix(pOutFile, NULL, bufferPos, 6);
         trfprintf(pOutFile, "LG  \tGPR_EP, 0(,GPR14)");
         bufferPos += 6;
         }
      else
         {
         printPrefix(pOutFile, NULL, bufferPos, 4);
         trfprintf(pOutFile, "L   \tGPR_EP, 0(,GPR14)");
         bufferPos += 4;
         }
      printPrefix(pOutFile, NULL, bufferPos, 2);
      trfprintf(pOutFile, "BCR    \tGPR_EP");
      bufferPos += 2;
      }
   else
      {
      printPrefix(pOutFile, NULL, bufferPos, 6);
      trfprintf(pOutFile, "BRASL \tGPR14, <%p>\t# Branch to Helper Method %s",
                    snippet->getSnippetDestAddr(),
                    snippet->usedTrampoline()?"- Trampoline Used.":"");
      bufferPos += 6;
      }

   if (padbytes == 2)
      {
      printPrefix(pOutFile, NULL, bufferPos, 2);
      trfprintf(pOutFile, "DC   \t0x0000 \t\t\t# 2-bytes padding for alignment");
      bufferPos += 2;
      }
   else if (padbytes == 4)
      {
      printPrefix(pOutFile, NULL, bufferPos, 4) ;
      trfprintf(pOutFile, "DC   \t0x00000000 \t\t# 4-bytes padding for alignment");
      bufferPos += 4;
      }
   else if (padbytes == 6)
      {
      printPrefix(pOutFile, NULL, bufferPos, 6) ;
      trfprintf(pOutFile, "DC   \t0x000000000000 \t\t# 6-bytes padding for alignment");
      bufferPos += 6;
      }

   printPrefix(pOutFile, NULL, bufferPos, sizeof(intptrj_t));
   trfprintf(pOutFile, "DC   \t%p \t\t# Method Address", glueRef->getMethodAddress());
   bufferPos += sizeof(intptrj_t);


   printPrefix(pOutFile, NULL, bufferPos, sizeof(intptrj_t));
   trfprintf(pOutFile, "DC   \t%p \t\t# Call Site RA", snippet->getCallRA());
   bufferPos += sizeof(intptrj_t);

   if (methodSymRef->isUnresolved())
      {
      printPrefix(pOutFile, NULL, bufferPos, 0);
      }
   else
      {
      printPrefix(pOutFile, NULL, bufferPos, sizeof(intptrj_t));
      }

   trfprintf(pOutFile, "DC   \t%p \t\t# Method Pointer", methodSymRef->isUnresolved() ? 0 : methodSymbol->getMethodAddress());
   }
