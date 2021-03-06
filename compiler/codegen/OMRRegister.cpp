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

#include "codegen/OMRRegister.hpp"      // for RegisterExt, etc

#include <stddef.h>                              // for NULL
#include <stdint.h>                              // for uint16_t, uint32_t
#include "codegen/RealRegister.hpp"              // for RealRegister
#include "codegen/Register.hpp"                  // for Register
#include "codegen/RegisterConstants.hpp"         // for TR_RegisterKinds, etc
#include "codegen/RegisterRematerializationInfo.hpp"
#include "codegen/OMRRealRegister.hpp"
#include "compile/Compilation.hpp"               // for Compilation
#include "ras/Debug.hpp"                         // for TR_DebugBase

OMR::Register::Register(uint32_t f):
   _flags(f),
   _backingStorage(NULL),
   _pinningArrayPointer(NULL),
   _assignedRegister(NULL),
   _colouredRegister(NULL),
   _siblingRegister(NULL),
   _startOfRange(NULL),
   _endOfRange(NULL),
   _startOfRangeNode(NULL),
   _totalUseCount(0),
   _futureUseCount(0),
   _outOfLineUseCount(0),
   _association(0),
   _kind(TR_GPR),
   _index(0),
   _cc(NULL)
   {}

OMR::Register::Register(TR_RegisterKinds rk):
   _flags(0),
   _backingStorage(NULL),
   _pinningArrayPointer(NULL),
   _assignedRegister(NULL),
   _colouredRegister(NULL),
   _siblingRegister(NULL),
   _startOfRange(NULL),
   _endOfRange(NULL),
   _startOfRangeNode(NULL),
   _totalUseCount(0),
   _futureUseCount(0),
   _outOfLineUseCount(0),
   _association(0),
   _kind(rk),
   _index(0),
   _cc(NULL)
   {}

OMR::Register::Register(TR_RegisterKinds rk, uint16_t ar):
   _flags(0),
   _backingStorage(NULL),
   _pinningArrayPointer(NULL),
   _assignedRegister(NULL),
   _colouredRegister(NULL),
   _siblingRegister(NULL),
   _startOfRange(NULL),
   _endOfRange(NULL),
   _startOfRangeNode(NULL),
   _totalUseCount(0),
   _futureUseCount(0),
   _outOfLineUseCount(0),
   _association(ar),
   _kind(rk),
   _index(0),
   _cc(NULL)
   {}

TR::Register*
OMR::Register::self()
    {
    return static_cast<TR::Register*>(this);
    }

ncount_t
OMR::Register::getTotalUseCount()
   {
   TR_SSR_ASSERT();
   return _totalUseCount;
   }

ncount_t
OMR::Register::setTotalUseCount(ncount_t tuc)
   {
   TR_SSR_ASSERT();
   return (_totalUseCount = tuc);
   }

ncount_t
OMR::Register::incTotalUseCount(ncount_t tuc)
   {
   TR_SSR_ASSERT();
   TR_MAX_USECOUNT_ASSERT(tuc);
   return (_totalUseCount += tuc);
   }

ncount_t
OMR::Register::decTotalUseCount(ncount_t tuc)
   {
   TR_SSR_ASSERT();
   return (_totalUseCount -= tuc);
   }

TR::RealRegister *
OMR::Register::getAssignedRealRegister()
   {
   return self()->getAssignedRegister() ? self()->getAssignedRegister()->getRealRegister() : NULL;
   }

TR::Register *
OMR::Register::getRegister()
   {
   return self(); //return subclass ptr
   }

void
OMR::Register::block()
   {
   if (self()->getAssignedRegister() != NULL)
      {
      TR::RealRegister *assignedReg = self()->getAssignedRegister()->getRealRegister();
      if (assignedReg != NULL)
         {
         if (assignedReg->getState() == TR::RealRegister::Assigned)
            {
            assignedReg->setState(TR::RealRegister::Blocked);
            }
         }
      }
   }


void
OMR::Register::unblock()
   {
   if (self()->getAssignedRegister() != NULL)
      {
      TR::RealRegister *assignedReg = self()->getAssignedRegister()->getRealRegister();
      if (assignedReg != NULL)
         {
         if (assignedReg->getState() == TR::RealRegister::Blocked)
            {
            assignedReg->setState(TR::RealRegister::Assigned, self()->isPlaceholderReg());
            }
         }
      }
   }


const char *
OMR::Register::getRegisterName(TR::Compilation *comp, TR_RegisterSizes size)
   {
   if (comp->getDebug())
      return comp->getDebug()->getName(self(), size);
   return "unknown";
   }


const char *
OMR::Register::getRegisterKindName(TR::Compilation *comp, TR_RegisterKinds rk)
   {
   if (comp->getDebug())
      return comp->getDebug()->getRegisterKindName(rk);
   return "??R";
   }

void
OMR::Register::setContainsInternalPointer()
    {
    TR_ASSERT(!self()->containsCollectedReference(), "assertion failure");
    _flags.set(ContainsInternalPointer);
    }

#if defined(DEBUG)
void
OMR::Register::print(TR::Compilation *comp, TR::FILE *pOutFile, TR_RegisterSizes size)
   {
   comp->getDebug()->print(pOutFile, self(), size);
   }
#endif


#if defined(DEBUG)
const char *
TR_RematerializationInfo::toString(TR::Compilation *comp)
   {
   return comp->getDebug()->toString(this);
   }
#endif
