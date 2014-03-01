// Copyright (c) The HLSL2GLSLFork Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE.txt file.


//
// Build the intermediate representation.
//

#include "localintermediate.h"
#include "RemoveTree.h"
#include <float.h>
#include <limits.h>

static TPrecision GetHigherPrecision (TPrecision left, TPrecision right) {
	return left > right ? left : right;
}


// First set of functions are to help build the intermediate representation.
// These functions are not member functions of the nodes.
// They are called from parser productions.


//
// Add a terminal node for an identifier in an expression.
//
// Returns the added node.
//
TIntermSymbol* TIntermediate::addSymbol(int id, const TString& name, const TType& type, TSourceLoc line)
{
   TIntermSymbol* node = new TIntermSymbol(id, name, type);
   node->setLine(line);

   return node;
}

//
// Add a terminal node for an identifier in an expression.
//
// Returns the added node.
//
TIntermSymbol* TIntermediate::addSymbol(int id, const TString& name, const TTypeInfo *info, const TType& type, TSourceLoc line)
{
   TIntermSymbol* node = new TIntermSymbol(id, name, info, type);
   node->setLine(line);

   return node;
}

//
// Connect two nodes with a new parent that does a binary operation on the nodes.
//
// Returns the added node.
//
TIntermTyped* TIntermediate::addBinaryMath(TOperator op, TIntermTyped* left, TIntermTyped* right, TSourceLoc line)
{
	if (!left || !right)
		return 0;
	bool isLHNonSquareMatrix = left->getTypePointer()->isNonSquareMatrix();
	bool isRHNonSquareMatrix = right->getTypePointer()->isNonSquareMatrix();
	
	switch (op)
	{
	case EOpLessThan:
	case EOpGreaterThan:
	case EOpLessThanEqual:
	case EOpGreaterThanEqual:
		if (left->getType().isMatrix() || left->getType().isArray() || left->getType().getBasicType() == EbtStruct)
		{
			return 0;
		}
		break;
	case EOpLogicalOr:
	case EOpLogicalXor:
	case EOpLogicalAnd:
		if (left->getType().isMatrix() || left->getType().isArray())
			return 0;
		
		if ( left->getBasicType() != EbtBool )
		{
			if ( left->getType().getBasicType() != EbtInt && left->getType().getBasicType() != EbtFloat )
				return 0;
			else
			{
				// If the left is a float or int, convert to a bool.  This is the conversion that HLSL
				// does
				left = addConversion( EOpConstructBool, 
									 TType ( EbtBool, left->getPrecision(), left->getQualifier(),
											left->getNominalSize(), left->isMatrix(), left->isArray()), 
									 left );
				
				if ( left == 0 )
					return 0;
			}
			
		}
		
		if (right->getType().isMatrix() || right->getType().isArray() || right->getType().isVector())
			return 0;
		
		if ( right->getBasicType() != EbtBool )
		{
			if ( right->getType().getBasicType() != EbtInt && right->getType().getBasicType() != EbtFloat )
				return 0;
			else
			{
				// If the right is a float or int, convert to a bool.  This is the conversion that HLSL
				// does
				right = addConversion( EOpConstructBool, 
									  TType ( EbtBool, right->getPrecision(), right->getQualifier(),
											 right->getNominalSize(), right->isMatrix(), right->isArray()), 
									  right );
				
				if ( right == 0 )
					return 0;
			}
			
		}
		break;
	case EOpAdd:
	case EOpSub:
	case EOpDiv:
	case EOpMul:
	case EOpMod:
		{
			TBasicType ltype = left->getType().getBasicType();
			TBasicType rtype = right->getType().getBasicType();
			if (!isLHNonSquareMatrix && ltype == EbtStruct)//non-square matrix is special case because it is treated as struct
				return 0;
			
			// If left or right type is a bool, convert to float.
			bool leftToFloat = (ltype == EbtBool);
			bool rightToFloat = (rtype == EbtBool);
			// For modulus, if either is an integer, convert to float as well.
			if (op == EOpMod)
			{
				leftToFloat |= (ltype == EbtInt);
				rightToFloat |= (rtype == EbtInt);
			}
				
			if (leftToFloat)
			{
				left = addConversion (EOpConstructFloat, TType (EbtFloat, left->getPrecision(), left->getQualifier(), left->getNominalSize(), left->isMatrix(), left->isArray()), left);
				if (left == 0)
					return 0;
			}
			if (rightToFloat)
			{
				right = addConversion (EOpConstructFloat, TType (EbtFloat, right->getPrecision(), right->getQualifier(), right->getNominalSize(), right->isMatrix(), right->isArray()), right);
				if (right == 0)
					return 0;
			}
		}
		break;
	default:
		break;
	}
	
	// 
	// First try converting the children to compatible types.
	//
	
	if (!(left->getType().getStruct() && right->getType().getStruct()))
	{
		TIntermTyped* child = 0;
		bool useLeft = true; //default to using the left child as the type to promote to
		
		//need to always convert up
		if ( left->getType().getBasicType() != EbtFloat && !isLHNonSquareMatrix)//non-square matrix has float type
		{
			if ( right->getTypePointer()->getBasicType() == EbtFloat 
				|| isRHNonSquareMatrix)
			{
				useLeft = false;
			}
			else
			{
				if ( left->getType().getBasicType() != EbtInt)
				{
					if ( right->getType().getBasicType() == EbtInt)
						useLeft = false;
				}
			}
		}
		
		if (useLeft)
		{
			if (!isRHNonSquareMatrix)//no need to conversion when right type is non-square matrix
			{
				//here: right is not square matrix
				if (isLHNonSquareMatrix)//if left operand is non-square matrix
				{
					TType type(EbtFloat, EbpUndefined);
					child = addConversion(op, type, right);//need to convert right operand to float		
				}
				else
					child = addConversion(op, left->getType(), right);
				if (child)
					right = child;
				else if (isLHNonSquareMatrix)//cannot convert
					return 0;//failed
				else//try to convert left operand
				{
					child = addConversion(op, right->getType(), left);
					if (child)
						left = child;
					else
						return 0;
				}
			}
		}
		else//useRight
		{
			if (!isLHNonSquareMatrix)//no need to conversion when left type is non-square matrix
			{
				//here: left is not square matrix
				if (isRHNonSquareMatrix)//if right operand is non-square matrix
				{
					TType type(EbtFloat, EbpUndefined);
					child = addConversion(op, type, left);//need to convert left operand to float		
				}
				else
					child = addConversion(op, right->getType(), left);
				if (child)
					left = child;
				else if (isRHNonSquareMatrix)
					return 0;//failed
				else//try to convert right operand
				{
					child = addConversion(op, left->getType(), right);
					if (child)
						right = child;
					else
						return 0;
				}
			}
		}
		
	}
	else
	{
		if (left->getType() != right->getType())
			return 0;
	}
	
	
	TIntermTyped *ret = NULL;
	// try to transform binary operator to function call if one of operand is non-square matrix
	if ((isLHNonSquareMatrix || isRHNonSquareMatrix) 
		&& !left->isArray() && !right->isArray()
		)
	{
		//
		// call __mulComp()/__addComp()/__divComp()/__subComp() function
		//
		TIntermAggregate *node = NULL;
		TString funcName;
		bool needTransformOperand = false;
		switch (op)
		{
		case EOpMul: funcName = "__mulComp"; break;
		case EOpDiv: funcName = "__divComp"; break;
		case EOpAdd: funcName = "__addComp"; break;
		case EOpSub: funcName = "__subComp"; break;
		}
		if (funcName.size() > 0)//must be valid
		{
			if (isLHNonSquareMatrix && isRHNonSquareMatrix)//both side are matrices
			{
				if (left->getType() == right->getType())//must be same type
				{
					node = setAggregateOperator(NULL, EOpFunctionCall, line);
					node->setType(right->getType());//return type
				}
			}
			//if (isLHNonSquareMatrix && isRHNonSquareMatrix)
			else if (isLHNonSquareMatrix)//left is matrix
			{
				if (!right->isArray() && !right->isVector())//right is scalar
				{
					node = setAggregateOperator(NULL, EOpFunctionCall, line);
					node->setType(left->getType());//return type
					switch (op)
					{
					case EOpDiv: funcName = "__mulComp";  needTransformOperand = true; break;
					case EOpSub: funcName = "__addComp";  needTransformOperand = true; break;
					}
				}
			}//else if (isLHNonSquareMatrix)
			else//right is matrix
			{
				if (!left->isArray() && !left->isVector() && op == EOpMul)//right is scalar, only multiplication is accepted
				{
					node = setAggregateOperator(NULL, EOpFunctionCall, line);
					node->setType(right->getType());//return type
				}
			}
		}
		
		if (node)//valid
		{
			node->getTypePointer()->changeQualifier(EvqTemporary);

			TString mangledName = funcName +"(";
			mangledName += left->getTypePointer()->getMangledName() + right->getTypePointer()->getMangledName();
			node->setName(mangledName);
			node->setPlainName(funcName);

			TIntermTyped *child = right;
			if (needTransformOperand)
			{
				if (op == EOpDiv)//division can be transformed to multiplication
				{
					//LHR/ x => LHR * (1/x)
					TIntermConstant *iconst = new TIntermConstant(TType(EbtFloat, EbpUndefined) );
					iconst->setValue(1.0f);

					child = this->addBinaryMath(EOpDiv, iconst, child, line);
				}
				else if (op == EOpSub)//transform sub to addition by negation
				{
					TIntermConstant *iconst = new TIntermConstant(TType(EbtFloat, EbpUndefined));
					iconst->setValue(-1.0f);

					child = this->addBinaryMath(EOpMul, iconst, child, line);
				}
			}
			

			
			node->getSequence().push_back(left);
			node->getSequence().push_back(child);
		}

		ret = node;
	}

	if (ret == NULL)//use normal binary operator
	{
		//
		// Need a new node holding things together then.  Make
		// one and promote it to the right type.
		//

		TIntermBinary* node = new TIntermBinary(op);
		if (line.line == 0)
			line = right->getLine();
		node->setLine(line);
		
		node->setLeft(left);
		node->setRight(right);
		if (! node->promote(infoSink))
			return 0;

		ret = node;
	}
	
	return ret;
}


//
// Connect two nodes through an assignment.
//
// Returns the added node.
//
TIntermTyped* TIntermediate::addAssign(TOperator op, TIntermTyped* left, TIntermTyped* right, TSourceLoc line)
{
   //
   // Like adding binary math, except the conversion can only go
   // from right to left.
   //
	if (!left || !right)
		return NULL;
   bool isLHNonSquareMatrix = left->getTypePointer()->isNonSquareMatrix();
   bool isRHNonSquareMatrix = right->getTypePointer()->isNonSquareMatrix();
   TIntermTyped* child = right;

   if (!isRHNonSquareMatrix)//no need for conversion if right operand is non-square
   {
	   if (isLHNonSquareMatrix)
	   {
		   TType floatType(EbtFloat, EbpUndefined);
		   child = addConversion(op, floatType, right);
	   }
	   else
			child = addConversion(op, left->getType(), right);
	   if (child == 0)
		  return 0;
   }

	if (isLHNonSquareMatrix && op != EOpAssign)
	{
		//transform right node to mul/add/div/sub function call
		TIntermTyped *op_node = NULL;
		TOperator binary_op;
		bool should_skip = false;
		switch (op)
		{
		case EOpMulAssign: binary_op = EOpMul; break;
		case EOpDivAssign: binary_op = EOpDiv; break;
		case EOpAddAssign: binary_op = EOpAdd; break;
		case EOpSubAssign: binary_op = EOpSub; break;
		default: should_skip = true; 
		}

		if (should_skip == false)//must be valid
		{
			op_node = addBinaryMath(binary_op, left, right, line);
			if (op_node != NULL)//valid
			{
				child = op_node;
				//then assign
				op = EOpAssign;
				left->AddRef();
			}
		}
	}//if (isLHNonSquareMatrix)

   TIntermBinary* node = new TIntermBinary(op);
   if (line.line == 0)
	  line = left->getLine();
   node->setLine(line);


   node->setLeft(left);
   node->setRight(child);
   if (! node->promote(infoSink))
	  return 0;

   return node;
}

//
// Connect two nodes through an index operator, where the left node is the base
// of an array or struct, and the right node is a direct or indirect offset.
//
// Returns the added node.
// The caller should set the type of the returned node.
//
TIntermTyped* TIntermediate::addIndex(TOperator op, TIntermTyped* base, TIntermTyped* index, TSourceLoc line)
{
   TIntermBinary* node = new TIntermBinary(op);
   if (line.line == 0)
      line = index->getLine();
   node->setLine(line);
   node->setLeft(base);
   node->setRight(index);

   // caller should set the type

   return node;
}

//
// Add one node as the parent of another that it operates on.
//
// Returns the added node.
//
TIntermTyped* TIntermediate::addUnaryMath(TOperator op, TIntermNode* childNode, TSourceLoc line)
{
   TIntermUnary* node;
   TIntermTyped* child = childNode->getAsTyped();

   if (child == 0)
   {
      infoSink.info.message(EPrefixInternalError, "Bad type in AddUnaryMath", line);
      return 0;
   }

   switch (op)
   {
   case EOpLogicalNot:
      if (child->getType().getBasicType() != EbtBool || child->getType().isMatrix() || child->getType().isArray() || child->getType().isVector())
      {
         return 0;
      }
      break;

   case EOpPostIncrement:
   case EOpPreIncrement:
   case EOpPostDecrement:
   case EOpPreDecrement:
   case EOpNegative:
      if (child->getType().getBasicType() == EbtStruct || child->getType().isArray())
         return 0;
   default: break;
   }

   //
   // Do we need to promote the operand?
   //
   // Note: Implicit promotions were removed from the language.
   //
   TBasicType newType = EbtVoid;
   switch (op)
   {
   case EOpConstructInt:   newType = EbtInt;   break;
   case EOpConstructBool:  newType = EbtBool;  break;
   case EOpConstructFloat: newType = EbtFloat; break;
   default: break;
   }

   if (newType != EbtVoid)
   {
      child = addConversion(op, TType(newType, child->getPrecision(), EvqTemporary, child->getNominalSize(), 
                                      child->isMatrix(), 
                                      child->isArray()),
                            child);
      if (child == 0)
         return 0;
   }

   //
   // For constructors, we are now done, it's all in the conversion.
   //
   switch (op)
   {
   case EOpConstructInt:
   case EOpConstructBool:
   case EOpConstructFloat:
      return child;
   default: break;
   }

   TIntermConstant *childTempConstant = 0;
   if (child->getAsConstant())
      childTempConstant = child->getAsConstant();

   //
   // Make a new node for the operator.
   //
   node = new TIntermUnary(op);
   if (line.line == 0)
      line = child->getLine();
   node->setLine(line);
   node->setOperand(child);

   if (! node->promote(infoSink))
      return 0;

   return node;
}

//
// This is the safe way to change the operator on an aggregate, as it
// does lots of error checking and fixing.  Especially for establishing
// a function call's operation on it's set of parameters.  Sequences
// of instructions are also aggregates, but they just direnctly set
// their operator to EOpSequence.
//
// Returns an aggregate node, which could be the one passed in if
// it was already an aggregate.
//
TIntermAggregate* TIntermediate::setAggregateOperator(TIntermNode* node, TOperator op, TSourceLoc line)
{
   TIntermAggregate* aggNode;

   //
   // Make sure we have an aggregate.  If not turn it into one.
   //
   if (node)
   {
      aggNode = node->getAsAggregate();
      if (aggNode == 0 || aggNode->getOp() != EOpNull)
      {
         //
         // Make an aggregate containing this node.
         //
         aggNode = new TIntermAggregate();
         aggNode->getSequence().push_back(node);
         if (line.line == 0)
            line = node->getLine();
      }
   }
   else
      aggNode = new TIntermAggregate();

   //
   // Set the operator.
   //
   aggNode->setOperator(op);
   if (line.line != 0)
      aggNode->setLine(line);

   return aggNode;
}

//
// Convert one type to another.
//
// Returns the node representing the conversion, which could be the same
// node passed in if no conversion was needed.
//
// Return 0 if a conversion can't be done.
//
TIntermTyped* TIntermediate::addConversion(TOperator op, const TType& type, TIntermTyped* node)
{
	if (!node)
		return 0;

   //
   // Does the base type allow operation?
   //
   switch (node->getBasicType())
   {
   case EbtVoid:
   case EbtSampler1D:
   case EbtSampler2D:
   case EbtSampler3D:
   case EbtSamplerCube:
   case EbtSampler1DShadow:
   case EbtSampler2DShadow:
   case EbtSamplerRect:        // ARB_texture_rectangle
   case EbtSamplerRectShadow:  // ARB_texture_rectangle
      return 0;
   default: break;
   }

   //
   // Otherwise, if types are identical, no problem
   //
   if (type == node->getType())
      return node;

   // if basic types are identical, promotions will handle everything
   if (type.getBasicType() == node->getTypePointer()->getBasicType())
      return node;

   //
   // If one's a structure, then no conversions.
   //
   if (type.getStruct() || node->getType().getStruct())
      return 0;

   //
   // If one's an array, then no conversions.
   //
   if (type.isArray() || node->getType().isArray())
      return 0;

   TBasicType promoteTo;

   switch (op)
   {
   //
   // Explicit conversions
   //
   case EOpConstructBool:
      promoteTo = EbtBool;
      break;
   case EOpConstructFloat:
      promoteTo = EbtFloat;
      break;
   case EOpConstructInt:
      promoteTo = EbtInt;
      break;
   default:
      //
      // implicit conversions are required for hlsl
      //
      promoteTo = type.getBasicType();
   }

   if (node->getAsConstant())
   {

      return(promoteConstant(promoteTo, node->getAsConstant()));
   }
   else
   {

      //
      // Add a new newNode for the conversion.
      //
      TIntermUnary* newNode = 0;

      TOperator newOp = EOpNull;
      switch (promoteTo)
      {
      case EbtFloat:
         switch (node->getBasicType())
         {
         case EbtInt:   newOp = EOpConvIntToFloat;  break;
         case EbtBool:  newOp = EOpConvBoolToFloat; break;
         default: 
            infoSink.info.message(EPrefixInternalError, "Bad promotion node", node->getLine());
            return 0;
         }
         break;
      case EbtBool:
         switch (node->getBasicType())
         {
         case EbtInt:   newOp = EOpConvIntToBool;   break;
         case EbtFloat: newOp = EOpConvFloatToBool; break;
         default: 
            infoSink.info.message(EPrefixInternalError, "Bad promotion node", node->getLine());
            return 0;
         }
         break;
      case EbtInt:
         switch (node->getBasicType())
         {
         case EbtBool:   newOp = EOpConvBoolToInt;  break;
         case EbtFloat:  newOp = EOpConvFloatToInt; break;
         default: 
            infoSink.info.message(EPrefixInternalError, "Bad promotion node", node->getLine());
            return 0;
         }
         break;
      default: 
         infoSink.info.message(EPrefixInternalError, "Bad promotion type", node->getLine());
         return 0;
      }

      TType type(promoteTo, node->getPrecision(), EvqTemporary, node->getNominalSize(), node->isMatrix(), node->isArray());
      newNode = new TIntermUnary(newOp, type);
      newNode->setLine(node->getLine());
      newNode->setOperand(node);

      return newNode;
   }
}

TIntermDeclaration* TIntermediate::addDeclaration(TIntermSymbol* symbol, TIntermTyped* initializer, TSourceLoc line) {
	TIntermDeclaration* decl = new TIntermDeclaration(symbol->getType());
	decl->setLine(line);
	
	if (!initializer)
		decl->getDeclaration() = symbol;
	else
		decl->getDeclaration() = addAssign(EOpAssign, symbol, initializer, line);
	if (!decl->getDeclaration())
		return NULL;
	return decl;
}

TIntermDeclaration* TIntermediate::addDeclaration(TSymbol* symbol, TIntermTyped* initializer, TSourceLoc line) {
	TVariable* var = static_cast<TVariable*>(symbol);
	TIntermSymbol* sym = addSymbol(var->getUniqueId(), var->getName(), var->getInfo(), var->getType(), line);
	sym->setGlobal(symbol->isGlobal());

	return addDeclaration(sym, initializer, line);
}

TIntermDeclaration* TIntermediate::growDeclaration(TIntermDeclaration* declaration, TSymbol* symbol, TIntermTyped* initializer) {
	TVariable* var = static_cast<TVariable*>(symbol);
	TIntermSymbol* sym = addSymbol(var->getUniqueId(), var->getName(), var->getInfo(), var->getType(), var->getType().getLine());
	sym->setGlobal(symbol->isGlobal());
	
	return growDeclaration(declaration, sym, initializer);
}

TIntermDeclaration* TIntermediate::growDeclaration(TIntermDeclaration* declaration, TIntermSymbol *symbol, TIntermTyped *initializer) {
	TIntermTyped* added_decl = symbol;
	if (initializer)
		added_decl = addAssign(EOpAssign, symbol, initializer, symbol->getLine());
	
	if (declaration->isSingleDeclaration()) {
		TIntermTyped* current = declaration->getDeclaration();
		TIntermAggregate* aggregate = makeAggregate(current, current->getLine());
		declaration->getDeclaration() = aggregate;
	}
	else
		declaration->getDeclaration()->getAsAggregate()->setOperator(EOpNull);//prevent the following method from creating new aggregate
	TIntermAggregate* aggregate = growAggregate(declaration->getDeclaration(), added_decl, added_decl->getLine());
	aggregate->setOperator(EOpComma);
	declaration->getDeclaration() = aggregate;
	
	return declaration;
}

bool TIntermDeclaration::containsArrayInitialization() {
	const TType& t = *this->getTypePointer();
	if (isSingleInitialization() && t.isArray())
		return true;
	
	if (t.isArray() && isMultipleDeclaration()) {
		TIntermSequence& decls = _declaration->getAsAggregate()->getSequence();
		unsigned n_decls = decls.size();
		for (unsigned i = 0; i != n_decls; ++i) {
			if (decls[i]->getAsBinaryNode())
				return true;
		}
	}
	
	return false;
}

//
// Safe way to combine two nodes into an aggregate.  Works with null pointers, 
// a node that's not a aggregate yet, etc.
//
// Returns the resulting aggregate, unless 0 was passed in for 
// both existing nodes.
//
TIntermAggregate* TIntermediate::growAggregate(TIntermNode* left, TIntermNode* right, TSourceLoc line)
{
   if (left == 0 && right == 0)
      return 0;

   TIntermAggregate* aggNode = 0;
   if (left)
      aggNode = left->getAsAggregate();
   if (!aggNode || aggNode->getOp() != EOpNull)
   {
      aggNode = new TIntermAggregate;
      if (left)
         aggNode->getSequence().push_back(left);
   }

   if (right)
      aggNode->getSequence().push_back(right);

   if (line.line != 0)
      aggNode->setLine(line);

   return aggNode;
}

//
// Turn an existing node into an aggregate.
//
// Returns an aggregate, unless 0 was passed in for the existing node.
//
TIntermAggregate* TIntermediate::makeAggregate(TIntermNode* node, TSourceLoc line)
{
	if (node == 0)
		return 0;

	TIntermAggregate* aggNode = new TIntermAggregate;
	if (node->getAsTyped())
		aggNode->setType(*node->getAsTyped()->getTypePointer());
	
	aggNode->getSequence().push_back(node);

	if (line.line != 0)
		aggNode->setLine(line);
	else
		aggNode->setLine(node->getLine());

	return aggNode;
}

//
// For "if" test nodes.  There are three children; a condition,
// a true path, and a false path.  The two paths are in the
// nodePair.
//
// Returns the selection node created.
//
TIntermNode* TIntermediate::addSelection(TIntermTyped* cond, TIntermNodePair nodePair, TSourceLoc line)
{   
   // Convert float/int to bool
   switch ( cond->getBasicType() )
   {
   case EbtFloat:
   case EbtInt:
      cond = addConversion ( EOpConstructBool, 
                             TType (EbtBool, cond->getPrecision(), cond->getQualifier(), cond->getNominalSize(), cond->isMatrix(), cond->isArray()),
                             cond );
      break;
   default:
      // Do nothing
      break;
   }

   TIntermSelection* node = new TIntermSelection(cond, nodePair.node1, nodePair.node2);
   node->setLine(line);

   return node;
}


TIntermTyped* TIntermediate::addComma(TIntermTyped* left, TIntermTyped* right, TSourceLoc line)
{
   if (left->getType().getQualifier() == EvqConst && right->getType().getQualifier() == EvqConst)
   {
      return right;
   }
   else
   {
      TIntermTyped *commaAggregate = growAggregate(left, right, line);
      commaAggregate->getAsAggregate()->setOperator(EOpComma);    
      commaAggregate->setType(right->getType());
      commaAggregate->getTypePointer()->changeQualifier(EvqTemporary);
      return commaAggregate;
   }
}

//
// For "?:" test nodes.  There are three children; a condition,
// a true path, and a false path.  The two paths are specified
// as separate parameters.
//
// Returns the selection node created, or 0 if one could not be.
//
TIntermTyped* TIntermediate::addSelection(TIntermTyped* cond, TIntermTyped* trueBlock, TIntermTyped* falseBlock, TSourceLoc line)
{
   bool bPromoteFromTrueBlockType = true;

   if (cond->getBasicType() != EbtBool)
   {
	   cond = addConversion (EOpConstructBool, 
		   TType (EbtBool, cond->getPrecision(), cond->getQualifier(), cond->getNominalSize(), cond->isMatrix(), cond->isArray()),
		   cond);
   }

   // Choose which one to try to promote to based on which has more precision
   // By default, it will promote from the falseBlock type to the trueBlock type.  However,
   // what we want to do is promote to the type with the most precision of the two.  So here,
   // check whether the false block has more precision than the true block, and if so use
   // its type instead.
   if ( trueBlock->getBasicType() == EbtBool )
   {
      if ( falseBlock->getBasicType() == EbtInt ||
           falseBlock->getBasicType() == EbtFloat )
      {
         bPromoteFromTrueBlockType = false;
      }
   }
   else if ( trueBlock->getBasicType() == EbtInt )
   {
      if ( falseBlock->getBasicType() == EbtFloat )
      {
         bPromoteFromTrueBlockType = false;
      }
   }

   //
   // Get compatible types.
   //
   if ( bPromoteFromTrueBlockType )
   {
      TIntermTyped* child = addConversion(EOpSequence, trueBlock->getType(), falseBlock);
      if (child)
         falseBlock = child;
      else
      {
         child = addConversion(EOpSequence, falseBlock->getType(), trueBlock);
         if (child)
            trueBlock = child;
         else
            return 0;
      }
   }
   else
   {
      TIntermTyped* child = addConversion(EOpSequence, falseBlock->getType(), trueBlock);
      if (child)
         trueBlock = child;
      else
      {
         child = addConversion(EOpSequence, trueBlock->getType(), falseBlock);
         if (child)
            falseBlock = child;
         else
            return 0;
      }
   }

   //
   // Make a selection node.
   //
   TIntermSelection* node = new TIntermSelection(cond, trueBlock, falseBlock, trueBlock->getType());
   node->setLine(line);
	
	if (!node->promoteTernary(infoSink))
		return 0;
	

   return node;
}

//
// Constant terminal nodes.  Has a union that contains bool, float or int constants
//
// Returns the constant union node created.
//

TIntermConstant* TIntermediate::addConstant(const TType& t, TSourceLoc line)
{
   TIntermConstant* node = new TIntermConstant(t);
   node->setLine(line);

   return node;
}

TIntermTyped* TIntermediate::addSwizzle(TVectorFields& fields, TSourceLoc line)
{
	TIntermAggregate* node = new TIntermAggregate(EOpSequence);

	node->setLine(line);
	TIntermConstant* constIntNode;
	TIntermSequence &sequenceVector = node->getSequence();

	for (int i = 0; i < fields.num; i++)
	{
		TIntermConstant* constant = addConstant(TType(EbtInt, EbpUndefined, EvqConst), line);
		constant->setValue(fields.offsets[i]);
		sequenceVector.push_back(constant);
	}

	return node;
}

// Create loop nodes.
TIntermNode* TIntermediate::addLoop(TLoopType type, TIntermTyped* cond, TIntermTyped* expr, TIntermNode* body, TSourceLoc line)
{
   if (expr != NULL)//move expr to the end of loop body
   {
	   if (body->getAsAggregate())//statement list
	   {
		   if (expr->getAsAggregate())
		   {
			   TIntermAggregate * aggExpr = expr->getAsAggregate();
			   for (size_t i = 0; i < aggExpr->getSequence().size(); ++i)
					body->getAsAggregate()->getSequence().push_back(aggExpr->getSequence()[i]);
		   }
		   else
				body->getAsAggregate()->getSequence().push_back(expr);
		   
	   }
	   else//single statement
	   {
		   //make new aggregate node
		   TIntermAggregate *aggre = setAggregateOperator(NULL, EOpSequence, line);
		   aggre->getSequence().push_back(body);
		  
		   if (expr->getAsAggregate())
		   {
			   TIntermAggregate * aggExpr = expr->getAsAggregate();
			   for (size_t i = 0; i < aggExpr->getSequence().size(); ++i)
					aggre->getSequence().push_back(aggExpr->getSequence()[i]);
		   }
		   else
				aggre->getSequence().push_back(expr);

		   body = aggre;
	   }

	   expr = NULL;
   }

   TIntermNode* node = new TIntermLoop(type, cond, expr, body);
   node->setLine(line);

   return node;
}

//
// Add branches.
//
TIntermBranch* TIntermediate::addBranch(TOperator branchOp, TSourceLoc line)
{
   return addBranch(branchOp, 0, line);
}

TIntermBranch* TIntermediate::addBranch(TOperator branchOp, TIntermTyped* expression, TSourceLoc line)
{
   TIntermBranch* node = new TIntermBranch(branchOp, expression);
   node->setLine(line);

   return node;
}

//
// This deletes the tree.
//
void TIntermediate::remove(TIntermNode* root)
{
   if (root)
      RemoveAllTreeNodes(root);
}


// ------------------------------------------------------------------
// Member functions of the nodes used for building the tree.


//
// Say whether or not an operation node changes the value of a variable.
//
// Returns true if state is modified.
//
bool TIntermOperator::modifiesState() const
{
   switch (op)
   {
   case EOpPostIncrement: 
   case EOpPostDecrement: 
   case EOpPreIncrement:  
   case EOpPreDecrement:  
   case EOpAssign:    
   case EOpAddAssign: 
   case EOpSubAssign: 
   case EOpMulAssign: 
   case EOpVectorTimesMatrixAssign:
   case EOpVectorTimesScalarAssign:
   case EOpMatrixTimesScalarAssign:
   case EOpMatrixTimesMatrixAssign:
   case EOpDivAssign: 
   case EOpModAssign: 
   case EOpAndAssign: 
   case EOpInclusiveOrAssign: 
   case EOpExclusiveOrAssign: 
   case EOpLeftShiftAssign:   
   case EOpRightShiftAssign:  
      return true;
   default:
      return false;
   }
}

//
// returns true if the operator is for one of the constructors
//
bool TIntermOperator::isConstructor() const
{
   switch (op)
   {
   case EOpConstructVec2:
   case EOpConstructVec3:
   case EOpConstructVec4:
   case EOpConstructMat2:
   case EOpConstructMat3:
   case EOpConstructMat4:
   case EOpConstructFloat:
   case EOpConstructIVec2:
   case EOpConstructIVec3:
   case EOpConstructIVec4:
   case EOpConstructInt:
   case EOpConstructBVec2:
   case EOpConstructBVec3:
   case EOpConstructBVec4:
   case EOpConstructBool:
   case EOpConstructStruct:
      return true;
   default:
      return false;
   }
}
//
// Make sure the type of a unary operator is appropriate for its 
// combination of operation and operand type.
//
// Returns false in nothing makes sense.
//
bool TIntermUnary::promote(TInfoSink&)
{
   switch (op)
   {
   case EOpLogicalNot:
      if (operand->getBasicType() != EbtBool)
         return false;
      break;
   case EOpBitwiseNot:
      if (operand->getBasicType() != EbtInt)
         return false;
      break;
   case EOpNegative:
   case EOpPostIncrement:
   case EOpPostDecrement:
   case EOpPreIncrement:
   case EOpPreDecrement:
      if (operand->getBasicType() == EbtBool)
         return false;
      break;

      // operators for built-ins are already type checked against their prototype
   case EOpAny:
   case EOpAll:
   case EOpVectorLogicalNot:
      return true;

   default:
      if (operand->getBasicType() != EbtFloat)
         return false;
   }

   setType(operand->getType());

   return true;
}

//
// Establishes the type of the resultant operation, as well as
// makes the operator the correct one for the operands.
//
// Returns false if operator can't work on operands.
//
bool TIntermBinary::promote(TInfoSink& infoSink)
{
   int size = left->getNominalSize();
   if (right->getNominalSize() < size)
      size = right->getNominalSize();

   if (size == 1)
   {
      size = left->getNominalSize();
      if (right->getNominalSize() > size)
         size = right->getNominalSize();
   }

   TBasicType type = left->getBasicType();

   //
   // Arrays have to be exact matches.
   //
   if ((left->isArray() || right->isArray()) && (left->getType() != right->getType()))
      return false;

   //
   // Base assumption:  just make the type the same as the left
   // operand.  Then only deviations from this need be coded.
   //
   setType(TType(left->getType(), left->getPrecision(), EvqTemporary));

   // The result gets promoted to the highest precision.
   TPrecision higherPrecision = GetHigherPrecision(left->getPrecision(), right->getPrecision());
   getTypePointer()->setPrecision(higherPrecision);


   //
   // Array operations.
   //
   if (left->isArray())
   {

      switch (op)
      {

      //
      // Promote to conditional
      //
      case EOpEqual:
      case EOpNotEqual:
         setType(TType(EbtBool, EbpUndefined));
         break;

         //
         // Set array information.
         //
      case EOpAssign:
         getType().setArraySize(left->getType().getArraySize());
         getType().setArrayInformationType(left->getType().getArrayInformationType());
         break;

      default:
         return false;
      }

      return true;
   }

   //
   // All scalars.  Code after this test assumes this case is removed!
   //
   if (size == 1)
   {

      switch (op)
      {

      //
      // Promote to conditional
      //
      case EOpEqual:
      case EOpNotEqual:
      case EOpLessThan:
      case EOpGreaterThan:
      case EOpLessThanEqual:
      case EOpGreaterThanEqual:
         setType(TType(EbtBool, EbpUndefined));
         break;

         //
         // And and Or operate on conditionals
         //
      case EOpLogicalAnd:
      case EOpLogicalOr:
         if (left->getBasicType() != EbtBool || right->getBasicType() != EbtBool)
            return false;
         setType(TType(EbtBool, EbpUndefined));
         break;

         //
         // Check for integer only operands.
         //
      case EOpRightShift:
      case EOpLeftShift:
      case EOpAnd:
      case EOpInclusiveOr:
      case EOpExclusiveOr:
         if (left->getBasicType() != EbtInt || right->getBasicType() != EbtInt)
            return false;
         break;
      case EOpModAssign:
      case EOpAndAssign:
      case EOpInclusiveOrAssign:
      case EOpExclusiveOrAssign:
      case EOpLeftShiftAssign:
      case EOpRightShiftAssign:
         if (left->getBasicType() != EbtInt || right->getBasicType() != EbtInt)
            return false;
         // fall through

         //
         // Everything else should have matching types
         //
      default:
         if (left->getBasicType() != right->getBasicType() ||
             left->isMatrix()     != right->isMatrix())
            return false;
      }

      return true;
   }

   //determine if this is an assignment
   bool assignment = ( op >= EOpAssign && op <= EOpRightShiftAssign) ? true : false;

   //
   // Are the sizes compatible?
   //
   if ( (left->getNominalSize() != size &&  left->getNominalSize() != 1) ||
        (right->getNominalSize() != size && right->getNominalSize() != 1))
   {
      //Insert a constructor on the larger type to make the sizes match

      if ( left->getNominalSize() > right->getNominalSize() )
      {

         if (assignment)
		 {
			 infoSink.info.message(EPrefixError, "Cannot promote type", getLine());
			 return false; //can't promote the destination
		 }

         //down convert left to match right
         TOperator convert = EOpNull;
         if (left->getTypePointer()->isMatrix())
         {
            switch (right->getNominalSize())
            {
            case 2: convert = EOpConstructMat2FromMat; break;
            case 3: convert = EOpConstructMat3FromMat; break;
            case 4: convert =  EOpConstructMat4; break; //should never need to down convert to mat4
            }
         }
         else if (left->getTypePointer()->isVector())
         {
            switch (left->getTypePointer()->getBasicType())
            {
            case EbtBool:  convert = TOperator( EOpConstructBVec2 + right->getNominalSize() - 2); break;
            case EbtInt:   convert = TOperator( EOpConstructIVec2 + right->getNominalSize() - 2); break;
            case EbtFloat: convert = TOperator( EOpConstructVec2 + right->getNominalSize() - 2); break;
            }
         }
         else
         {
            assert(0); //size 1 case should have been handled
         }
         TIntermAggregate *node = new TIntermAggregate(convert);
         node->setLine(left->getLine());
         node->setType(TType(left->getBasicType(), left->getPrecision(), EvqTemporary, right->getNominalSize(), left->isMatrix()));
         node->getSequence().push_back(left);
         left = node;
         //now reset this node's type
         setType(TType(left->getBasicType(), left->getPrecision(), EvqTemporary, right->getNominalSize(), left->isMatrix()));
      }
      else
      {
         //down convert right to match left
         TOperator convert = EOpNull;
         if (right->getTypePointer()->isMatrix())
         {
            switch (left->getNominalSize())
            {
            case 2: convert = EOpConstructMat2FromMat; break;
            case 3: convert = EOpConstructMat3FromMat; break;
            case 4: convert =  EOpConstructMat4; break; //should never need to down convert to mat4
            }
         }
         else if (right->getTypePointer()->isVector())
         {
            switch (right->getTypePointer()->getBasicType())
            {
            case EbtBool:  convert = TOperator( EOpConstructBVec2 + left->getNominalSize() - 2); break;
            case EbtInt:   convert = TOperator( EOpConstructIVec2 + left->getNominalSize() - 2); break;
            case EbtFloat: convert = TOperator( EOpConstructVec2 + left->getNominalSize() - 2); break;
            }
         }
         else
         {
            assert(0); //size 1 case should have been handled
         }
         TIntermAggregate *node = new TIntermAggregate(convert);
         node->setLine(right->getLine());
         node->setType(TType(right->getBasicType(), right->getPrecision(), EvqTemporary, left->getNominalSize(), right->isMatrix()));
         node->getSequence().push_back(right);
         right = node;
      }
   }

   //
   // Can these two operands be combined?
   //
   switch (op)
   {
   case EOpMul:
      if (!left->isMatrix() && right->isMatrix())
      {
         if (left->isVector())
            op = EOpVectorTimesMatrix;
         else
         {
            op = EOpMatrixTimesScalar;
            setType(TType(type, higherPrecision, EvqTemporary, size, true));
         }
      }
      else if (left->isMatrix() && !right->isMatrix())
      {
         if (right->isVector())
         {
            op = EOpMatrixTimesVector;
            setType(TType(type, higherPrecision, EvqTemporary, size, false));
         }
         else
         {
            op = EOpMatrixTimesScalar;
         }
      }
      else if (left->isMatrix() && right->isMatrix())
      {
         op = EOpMatrixTimesMatrix;
      }
      else if (!left->isMatrix() && !right->isMatrix())
      {
         if (left->isVector() && right->isVector())
         {
            // leave as component product
         }
         else if (left->isVector() || right->isVector())
         {
            op = EOpVectorTimesScalar;
            setType(TType(type, higherPrecision, EvqTemporary, size, false));
         }
      }
      else
      {
         infoSink.info.message(EPrefixInternalError, "Missing elses", getLine());
         return false;
      }
      break;
   case EOpMulAssign:
      if (!left->isMatrix() && right->isMatrix())
      {
         if (left->isVector())
            op = EOpVectorTimesMatrixAssign;
         else
         {
            return false;
         }
      }
      else if (left->isMatrix() && !right->isMatrix())
      {
         if (right->isVector())
         {
            return false;
         }
         else
         {
            op = EOpMatrixTimesScalarAssign;
         }
      }
      else if (left->isMatrix() && right->isMatrix())
      {
         op = EOpMatrixTimesMatrixAssign;
      }
      else if (!left->isMatrix() && !right->isMatrix())
      {
         if (left->isVector() && right->isVector())
         {
            // leave as component product
         }
         else if (left->isVector() || right->isVector())
         {
            if (! left->isVector())
               return false;
            op = EOpVectorTimesScalarAssign;
            setType(TType(type, higherPrecision, EvqTemporary, size, false));
         }
      }
      else
      {
         infoSink.info.message(EPrefixInternalError, "Missing elses", getLine());
         return false;
      }
      break;
   case EOpAssign:
      if (left->getNominalSize() != right->getNominalSize())
      {
         //right needs to be forced to match left
         TOperator convert = EOpNull;

         if (left->isMatrix() )
         {
            //TODO: These might need to be changed to smears
            switch (left->getNominalSize())
            {
            case 2: convert = EOpConstructMat2; break;
            case 3: convert = EOpConstructMat3; break;
            case 4: convert =  EOpConstructMat4; break; 
            }
         }
         else if (left->isVector() )
         {
            switch (right->getTypePointer()->getBasicType())
            {
            case EbtBool:  convert = TOperator( EOpConstructBVec2 + left->getNominalSize() - 2); break;
            case EbtInt:   convert = TOperator( EOpConstructIVec2 + left->getNominalSize() - 2); break;
            case EbtFloat: convert = TOperator( EOpConstructVec2 + left->getNominalSize() - 2); break;
            }
         }
         else
         {
            switch (right->getTypePointer()->getBasicType())
            {
            case EbtBool:  convert = EOpConstructBool; break;
            case EbtInt:   convert = EOpConstructInt; break;
            case EbtFloat: convert = EOpConstructFloat; break;
            }
         }

         assert( convert != EOpNull);
         TIntermAggregate *node = new TIntermAggregate(convert);
         node->setLine(right->getLine());
         node->setType(TType(left->getBasicType(), left->getPrecision(), right->getQualifier() == EvqConst ? EvqConst : EvqTemporary, left->getNominalSize(), left->isMatrix()));
         node->getSequence().push_back(right);
         right = node;
         size = right->getNominalSize();
      }
      // fall through
   case EOpMod:
   case EOpAdd:
   case EOpSub:
   case EOpDiv:
   case EOpAddAssign:
   case EOpSubAssign:
   case EOpDivAssign:
   case EOpModAssign:
      if (op == EOpMod)
		  type = EbtFloat;
      if (left->isMatrix() && right->isVector() ||
          left->isVector() && right->isMatrix() ||
          left->getBasicType() != right->getBasicType())
         return false;
      setType(TType(type, left->getPrecision(), EvqTemporary, size, left->isMatrix() || right->isMatrix()));
      break;

   case EOpEqual:
   case EOpNotEqual:
   case EOpLessThan:
   case EOpGreaterThan:
   case EOpLessThanEqual:
   case EOpGreaterThanEqual:
      if (left->isMatrix() && right->isVector() ||
          left->isVector() && right->isMatrix() ||
          left->getBasicType() != right->getBasicType())
         return false;
      setType(TType(EbtBool, higherPrecision, EvqTemporary, size, false));
      break;

   default:
      return false;
   }

   //
   // One more check for assignment.  The Resulting type has to match the left operand.
   //
   switch (op)
   {
   case EOpAssign:
   case EOpAddAssign:
   case EOpSubAssign:
   case EOpMulAssign:
   case EOpDivAssign:
   case EOpModAssign:
   case EOpAndAssign:
   case EOpInclusiveOrAssign:
   case EOpExclusiveOrAssign:
   case EOpLeftShiftAssign:
   case EOpRightShiftAssign:
      if (getType() != left->getType())
         return false;
      break;
   default: 
      break;
   }

   return true;
}


bool TIntermSelection::promoteTernary(TInfoSink& infoSink)
{
	if (!condition->isVector())
		return true;
	
	int size = condition->getNominalSize();
	TIntermTyped* trueb = trueBlock->getAsTyped();
	TIntermTyped* falseb = falseBlock->getAsTyped();
	if (!trueb || !falseb)
		return false;
	
	if (trueb->getNominalSize() == size && falseb->getNominalSize() == size)
		return true;
	
	// Base assumption: just make the type a float vector
	TPrecision higherPrecision = GetHigherPrecision(trueb->getPrecision(), falseb->getPrecision());
	setType(TType(EbtFloat, higherPrecision, EvqTemporary, size, condition->isMatrix()));
	
	TOperator convert = EOpNull;	
	{
		convert = TOperator( EOpConstructVec2 + size - 2);
		TIntermAggregate *node = new TIntermAggregate(convert);
		node->setLine(trueb->getLine());
		node->setType(TType(condition->getBasicType(), higherPrecision, trueb->getQualifier() == EvqConst ? EvqConst : EvqTemporary, size, condition->isMatrix()));
		node->getSequence().push_back(trueb);
		trueBlock = node;
	}
	{
		convert = TOperator( EOpConstructVec2 + size - 2);
		TIntermAggregate *node = new TIntermAggregate(convert);
		node->setLine(falseb->getLine());
		node->setType(TType(condition->getBasicType(), higherPrecision, falseb->getQualifier() == EvqConst ? EvqConst : EvqTemporary, size, condition->isMatrix()));
		node->getSequence().push_back(falseb);
		falseBlock = node;
	}
	
	return true;
}

TIntermTyped* TIntermediate::promoteConstant(TBasicType promoteTo, TIntermConstant* right) 
{
	unsigned size = right->getCount();
	const TType& t = right->getType();
	TIntermConstant* left = addConstant(TType(promoteTo, t.getPrecision(), t.getQualifier(), t.getNominalSize(), t.isMatrix(), t.isArray()), right->getLine());
	for (unsigned i = 0; i != size; ++i) {
		TIntermConstant::Value& value = right->getValue(i);
		
		switch (promoteTo)
		{
		case EbtFloat:
			switch (value.type) {
			case EbtInt:
				left->setValue(i, (float)value.asInt);
				break;
			case EbtBool:
				left->setValue(i, (float)value.asBool);
				break;
			case EbtFloat:
				left->setValue(i, value.asFloat);
				break;
			default: 
				infoSink.info.message(EPrefixInternalError, "Cannot promote", right->getLine());
				return 0;
			}                
			break;
		case EbtInt:
			switch (value.type) {
			case EbtInt:
				left->setValue(i, value.asInt);
				break;
			case EbtBool:
				left->setValue(i, (int)value.asBool);
				break;
			case EbtFloat:
				left->setValue(i, (int)value.asFloat);
				break;
			default: 
				infoSink.info.message(EPrefixInternalError, "Cannot promote", right->getLine());
				return 0;
			}                
			break;
		case EbtBool:
			switch (value.type) {
			case EbtInt:
				left->setValue(i, value.asInt != 0);
				break;
			case EbtBool:
				left->setValue(i, value.asBool);
				break;
			case EbtFloat:
				left->setValue(i, value.asFloat != 0.0f);
				break;
			default: 
				infoSink.info.message(EPrefixInternalError, "Cannot promote", right->getLine());
				return 0;
			}                
			break;
		default:
			infoSink.info.message(EPrefixInternalError, "Incorrect data type found", right->getLine());
			return 0;
		}
	}

	return left;
}
