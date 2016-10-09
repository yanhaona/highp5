#ifndef _H_reduction_primitive_mgmt
#define _H_reduction_primitive_mgmt

// This header file contains library functions to generate runtime routines and data structures for implementing any
// parallel reductions found in an IT task. 

#include "../utils/list.h"
#include "../utils/common_constant.h"
#include "../semantics/task_space.h"
#include "../static-analysis/data_flow.h"

#include <iostream>
#include <fstream>

class MappingNode;

/**********************************************************************************************************************
						Utility Functions
***********************************************************************************************************************/

const char *getMpiDataTypeStr(Type *type, ReductionOperator op);
const char *getMpiReductionOp(ReductionOperator op);
const char *getReductionOpString(ReductionOperator op);

/**********************************************************************************************************************
				Generators for Intermediate Result Update Functions' body
***********************************************************************************************************************/

void generateUpdateCodeForMax(std::ofstream &programFile, Type *varType);
void generateUpdateCodeForSum(std::ofstream &programFile, Type *varType);

/**********************************************************************************************************************
				Generators for Perform Cross Segment Reduction Functions' body
***********************************************************************************************************************/

void generateCodeForDataReduction(std::ofstream &programFile, 
		ReductionOperator op, Type *varType);

/**********************************************************************************************************************
					Reduction Primitive Class Generators
***********************************************************************************************************************/

void generateIntraSegmentReductionPrimitive(std::ofstream &headerFile, 
		std::ofstream &programFile, 
		const char *initials, 
		ReductionMetadata *rdMetadata, 
		Space *rootLps);

void generateCrossSegmentReductionPrimitive(std::ofstream &headerFile, 
		std::ofstream &programFile, 
		const char *initials, 
		ReductionMetadata *rdMetadata, 
		Space *rootLps);

void generateReductionPrimitiveClasses(const char *headerFile,
                const char *programFile,
                const char *initials,
                MappingNode *mappingRoot, 
		List<ReductionMetadata*> *reductionInfos);

/**********************************************************************************************************************
					Reduction Primitive Instantiation  
***********************************************************************************************************************/

void generateReductionPrimitiveDecls(const char *headerFile, List<ReductionMetadata*> *reductionInfos);

void generateReductionPrimitiveInitFn(const char *headerFile, 
		const char *programFile, 
		const char *initials, 
		List<ReductionMetadata*> *reductionInfos);

#endif