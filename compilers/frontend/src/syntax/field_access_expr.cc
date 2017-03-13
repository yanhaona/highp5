#include "ast.h"
#include "ast_stmt.h"
#include "ast_expr.h"
#include "ast_type.h"
#include "../common/errors.h"
#include "../common/constant.h"
#include "../semantics/scope.h"
#include "../semantics/symbol.h"
#include "../../../common-libs/utils/list.h"
#include "../../../common-libs/utils/hashtable.h"

#include <iostream>
#include <sstream>
#include <cstdlib>

//-------------------------------------------------- Field Access -----------------------------------------------------/

FieldAccess::FieldAccess(Expr *b, Identifier *f, yyltype loc) : Expr(loc) {
        Assert(f != NULL);
        base = b;
        if (base != NULL) {
                base->SetParent(this);
        }
        field = f;
        field->SetParent(this);
	referenceField = false;
	arrayField = false;
	arrayDimensions = -1;
}

void FieldAccess::PrintChildren(int indentLevel) {
        if(base != NULL) base->Print(indentLevel + 1);
        field->Print(indentLevel + 1);
}

Node *FieldAccess::clone() {
	Expr *newBase = (Expr*) base->clone();
	Identifier *newField = (Identifier*) field->clone();
	FieldAccess *newFieldAcc = new FieldAccess(newBase, newField, *GetLocation());
	if (referenceField) {
		newFieldAcc->flagAsReferenceField();
	}
	if (arrayField) newFieldAcc->flagAsArrayField(arrayDimensions);
	return newFieldAcc;
}

void FieldAccess::retrieveExprByType(List<Expr*> *exprList, ExprTypeId typeId) {
	Expr::retrieveExprByType(exprList, typeId);
	if (base != NULL) base->retrieveExprByType(exprList, typeId);
}

void FieldAccess::flagAsArrayField(int arrayDimensions) {
	arrayField = true;
	this->arrayDimensions = arrayDimensions;
}

FieldAccess *FieldAccess::getTerminalField() {
	if (base == NULL) return this;
	FieldAccess *baseField = dynamic_cast<FieldAccess*>(base);
	if (baseField == NULL) return NULL;
	return baseField->getTerminalField();
}

int FieldAccess::resolveExprTypes(Scope *scope) {
	
	int resolvedExprs = 0;
	if (base == NULL) {			// consider the terminal case of accessing a variable first
		VariableSymbol *symbol = (VariableSymbol*) scope->lookup(field->getName());
                if (symbol != NULL && symbol->getType() != NULL) {
                        this->type = symbol->getType();
			resolvedExprs++;
		}
		return resolvedExprs;
	}

	resolvedExprs += base->resolveExprTypes(scope);
	Type *baseType = base->getType();
	if (baseType == NULL || baseType == Type::errorType) return resolvedExprs;

	ArrayType *arrayType = dynamic_cast<ArrayType*>(baseType);
	MapType *mapType = dynamic_cast<MapType*>(baseType);
	if (arrayType != NULL) {		// check for the field access to be a part of an array		
		if (strcmp(field->getName(), Identifier::LocalId) == 0) {
			this->type = arrayType;
			resolvedExprs++;
		} else if (dynamic_cast<DimensionIdentifier*>(field) != NULL) {
			this->type = Type::dimensionType;
			resolvedExprs++;
		}
	} else if (mapType != NULL) {		// check for the field access to be an item in a map
		if (mapType->hasElement(field->getName())) {
			Type *elemType = mapType->getElementType(field->getName());
			if (elemType != NULL) {
				this->type = elemType;
				resolvedExprs++;
			}
		}
	} else {				// check if the field access to a property of a custom type
		Symbol *symbol = scope->lookup(baseType->getName());
		if (symbol != NULL) {
			Scope *baseScope = symbol->getNestedScope();
			if (baseScope != NULL && baseScope->lookup(field->getName()) != NULL) {
				VariableSymbol *fieldSymbol
					= (VariableSymbol*) baseScope->lookup(field->getName());
				this->type = fieldSymbol->getType();
				resolvedExprs++;
			}
		}
	}

	return resolvedExprs;
}

int FieldAccess::inferExprTypes(Scope *scope, Type *assignedType) {

	// if the field-access is not a terminal/standalone field then type inference should work only when the 
	// base is of map type
	if (base != NULL) {
                Type *baseType = base->getType();
                if (baseType == NULL) return 0;

                MapType *mapType = dynamic_cast<MapType*>(baseType);
                if (mapType == NULL) return 0;

                if (!mapType->hasElement(field->getName())) {
                        this->type = assignedType;
                        mapType->setElement(new VariableDef(field, assignedType));
			return 1;
                }
		return 0;
        }

	// If the field-access is a standalone field then setup the type of the variable symbol it is associated
	// with. If there is no such symbol then create a new symbol for the field with the assigned type.
	this->type = assignedType;
	VariableSymbol *symbol = (VariableSymbol*) scope->lookup(field->getName());
	if (symbol != NULL) {
		symbol->setType(this->type);
	} else {
		symbol = new VariableSymbol(field->getName(), this->type);
		bool success = scope->insert_inferred_symbol(symbol);
		if (!success) {
			ReportError::Formatted(GetLocation(),
					"couldn't create symbol in the scope for %s",
					field->getName());
			return 0;
		}
	}
	return 1;
}

int FieldAccess::emitSemanticErrors(Scope *scope) {
	
	// check for the case when the current field access is not corresponding to accessing a property of
	// a larger object
	if (base == NULL) {
		Symbol *symbol = scope->lookup(field->getName());
                if (symbol != NULL && dynamic_cast<VariableSymbol*>(symbol) != NULL) {
			
			VariableSymbol *varSym = dynamic_cast<VariableSymbol*>(symbol);
                        this->type = varSym->getType();

			// if the field is of some custom-type then that type must be defined
                        NamedType *tupleType = dynamic_cast<NamedType*>(this->type);
                        if (tupleType != NULL) {
                                Symbol *typeSymbol = scope->lookup(tupleType->getName());
                                if (typeSymbol == NULL) {
                                        ReportError::UndeclaredTypeError(field, this->type, NULL, false);
                                        return 1;
                                }
                        }
                } else {
                        ReportError::UndefinedSymbol(field, false);
                        return 1;
                }
		return 0;
	}

	// check for the alternative case where the field access is accessing a property of a larger object
	int errors = 0;
	errors += base->emitScopeAndTypeErrors(scope);
	Type *baseType = base->getType();
	if (baseType != NULL) {
		ArrayType *arrayType = dynamic_cast<ArrayType*>(baseType);
		MapType *mapType = dynamic_cast<MapType*>(baseType);
		ListType *listType = dynamic_cast<ListType*>(baseType);
		if (arrayType != NULL) {
			DimensionIdentifier *dimension = dynamic_cast<DimensionIdentifier*>(field);
			if (dimension != NULL) {
				int dimensionality = arrayType->getDimensions();
				int fieldDimension = dimension->getDimensionNo();
				if (fieldDimension > dimensionality) {
					ReportError::NonExistingDimensionInArray(field, 
							dimensionality, fieldDimension, false);
					errors++;
				}
			} else {
				ReportError::NoSuchFieldInBase(field, arrayType, false);
				errors++;
			} 
		} else if (mapType == NULL && listType == NULL) {
			Symbol *symbol = scope->lookup(baseType->getName());
			if (symbol != NULL) {
				Scope *baseScope = symbol->getNestedScope();
				if (baseScope == NULL || baseScope->lookup(field->getName()) == NULL) {
					ReportError::NoSuchFieldInBase(field, baseType, false);
					errors++;
				}
			}
		}
	}
	return errors;
}
