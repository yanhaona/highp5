#include "thread_state_mgmt.h"
#include "space_mapping.h"
#include "../semantics/task_space.h"
#include "../utils/list.h"
#include "../syntax/ast_task.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// This function is simple. It just copy the dimension information from the global array metadata object to the 
// partition dimensions of individual arrays. Memory for these arrays are not allocated as it is not done for 
// any LPU in any LPS. For memory allocation further analysis of the compute block is needed and the whole allocation 
// logic will be handled in a separate module.
void generateRootLpuComputeRoutine(std::ofstream &programFile, MappingNode *mappingRoot) {
	
	std::cout << "Generating routine for root LPU calculation" << std::endl;
        programFile << "// Construction of task specific root LPU\n";

	std::string statementSeparator = ";\n";
        std::string singleIndent = "\t";
	
	// specify the signature of the compute-Next-Lpu function matching the virtual function in Thread-State class
	std::ostringstream functionHeader;
        functionHeader << "void ThreadStateImpl::setRootLpu()";
        std::ostringstream functionBody;
	functionBody << "{\n";

	// allocate an LPU for the root
	Space *rootLps = mappingRoot->mappingConfig->LPS;
	functionBody << singleIndent;
	functionBody << "Space" << rootLps->getName() << "_LPU *lpu = new Space";
	functionBody  << rootLps->getName() << "_LPU";
	functionBody << statementSeparator;

	// initialize each array in the root LPU
	List<const char*> *localArrays = rootLps->getLocallyUsedArrayNames();
	for (int i = 0; i < localArrays->NumElements(); i++) {
		if (i > 0) functionBody << std::endl;
		const char* arrayName = localArrays->Nth(i);
		ArrayDataStructure *array = (ArrayDataStructure*) rootLps->getLocalStructure(arrayName);
		int dimensionCount = array->getDimensionality();
		functionBody << singleIndent << "lpu->" << arrayName << " = NULL" << statementSeparator;
		std::ostringstream varName;
		varName << "lpu->" << arrayName << "PartDims";
		functionBody << singleIndent << varName.str() << " = new PartitionDimension*[";
		functionBody << dimensionCount << "]" << statementSeparator;
		for (int j = 0; j < dimensionCount; j++) {
			functionBody << singleIndent << varName.str() << "[" << j << "] = ";
			functionBody << "new PartitionDimension" << statementSeparator;
			functionBody << singleIndent << varName.str() << "[" << j << "]->storageDim = ";
			functionBody << varName.str() << "[" << j << "]->partitionDim" << std::endl;
			functionBody << singleIndent << singleIndent << singleIndent;
			functionBody << "= &arrayMetadata." << arrayName << "Dims[" << j << "]";
			functionBody << statementSeparator;			
		}	
	}
	
	// store the LPU in the proper LPS state
	functionBody << std::endl;	
	functionBody << singleIndent << "lpsStates[Space_" << rootLps->getName() << "]->lpu = lpu";
	functionBody << statementSeparator << "}\n";
	
	programFile << functionHeader.str() << " " << functionBody.str();
	programFile << std::endl;
}

void generateParentIndexMapRoutine(std::ofstream &programFile, MappingNode *mappingRoot) {
	
	std::cout << "Generating parent pointer index map" << std::endl;
        programFile << "// Construction of task specific LPS hierarchy index map\n";
	
	std::string statementSeparator = ";\n";
        std::string singleIndent = "\t";
	std::ostringstream allocateStmt;
	std::ostringstream initializeStmts;

	allocateStmt << singleIndent << "lpsParentIndexMap = new int";
	allocateStmt << "[Space_Count]" << statementSeparator;
	initializeStmts << singleIndent << "lpsParentIndexMap[Space_";
	initializeStmts << mappingRoot->mappingConfig->LPS->getName();
	initializeStmts << "] = INVALID_ID" << statementSeparator;

	std::deque<MappingNode*> nodeQueue;
        for (int i = 0; i < mappingRoot->children->NumElements(); i++) {
        	nodeQueue.push_back(mappingRoot->children->Nth(i));
        }
        while (!nodeQueue.empty()) {
                MappingNode *node = nodeQueue.front();
                nodeQueue.pop_front();
                for (int i = 0; i < node->children->NumElements(); i++) {
                        nodeQueue.push_back(node->children->Nth(i));
                }
		Space *lps = node->mappingConfig->LPS;
		initializeStmts << singleIndent;
		initializeStmts << "lpsParentIndexMap[Space_" << lps->getName() << "] = ";
		initializeStmts << "Space_" << lps->getParent()->getName();
		initializeStmts << statementSeparator;
	}

	programFile << "void ThreadStateImpl::setLpsParentIndexMap() {\n";
	programFile << allocateStmt.str() << initializeStmts.str();
	programFile << "}\n\n";
}

void generateComputeLpuCountRoutine(std::ofstream &programFile, MappingNode *mappingRoot, 
                Hashtable<List<PartitionParameterConfig*>*> *countFunctionsArgsConfig) {

	std::cout << "Generating compute LPU count function" << std::endl;
        programFile << "// Implementation of task specific compute-LPU-Count function ";
	
	std::string statementSeparator = ";\n";
        std::string singleIndent = "\t";
	std::string doubleIndent = "\t\t";
	std::string tripleIndent = "\t\t\t";
	std::string parameterSeparator = ", ";

	// specify the signature of the compute-Next-Lpu function matching the virtual function in Thread-State class
	std::ostringstream functionHeader;
        functionHeader << "int *ThreadStateImpl::computeLpuCounts(int lpsId)";
        std::ostringstream functionBody;
	functionBody << "{\n";

	std::deque<MappingNode*> nodeQueue;
        nodeQueue.push_back(mappingRoot);
        while (!nodeQueue.empty()) {
                MappingNode *node = nodeQueue.front();
                nodeQueue.pop_front();
                for (int i = 0; i < node->children->NumElements(); i++) {
                        nodeQueue.push_back(node->children->Nth(i));
                }
		Space *lps = node->mappingConfig->LPS;
		// if the space is unpartitioned then we can return NULL as there is no need for a counter then
		functionBody << singleIndent << "if (lpsId == Space_" << lps->getName() << ") {\n";
		if (lps->getDimensionCount() == 0) {
			functionBody << doubleIndent << "return NULL" << statementSeparator;
		// otherwise, we have to call the appropriate get-LPU-count function generated before 
		} else {
			// declare a local variable for PPU count which is a default argument to all count functions
			functionBody << doubleIndent << "int ppuCount = ";
			functionBody << "threadIds->ppuIds[Space_" << lps->getName() << "].ppuCount";
			functionBody << statementSeparator;
			
			// create local variables for ancestor LPUs that will be needed to determine the dimension
			// arguments for the get-LPU-Count function 
			List<PartitionParameterConfig*> *paramConfigs 
					= countFunctionsArgsConfig->Lookup(lps->getName());
			if (paramConfigs == NULL) std::cout << "strange!!" << std::endl;
			Hashtable<const char*> *parentLpus = new Hashtable<const char*>;
			Hashtable<const char*> *arrayToParentLpus = new Hashtable<const char*>;
			for (int i = 0; i < paramConfigs->NumElements(); i++) {
				PartitionParameterConfig *currentConfig = paramConfigs->Nth(i);
				const char *arrayName = currentConfig->arrayName;
				if (arrayName == NULL) continue;
				Space *parentLps = lps->getLocalStructure(arrayName)->getSource()->getSpace();
				// no variable is yet created for the parent LPU so create it
				if (parentLpus->Lookup(parentLps->getName()) == NULL) {
					std::ostringstream parentLpuStr;
					functionBody << doubleIndent;
					functionBody << "Space" << parentLps->getName() << "_LPU *";
					parentLpuStr << "space" << parentLps->getName() << "Lpu";
					functionBody << parentLpuStr.str() << " = ";
					functionBody << "(Space" << parentLps->getName() << "_LPU*) \n";
					functionBody << doubleIndent << doubleIndent;
					functionBody << "lpsStates[Space_" << parentLps->getName() << "]->lpu";
					functionBody << statementSeparator;
					arrayToParentLpus->Enter(arrayName, strdup(parentLpuStr.str().c_str()));
					parentLpus->Enter(parentLps->getName(), strdup(parentLpuStr.str().c_str()));
				// otherwise just get the reference of the already created LPU
				} else {
					arrayToParentLpus->Enter(arrayName, parentLpus->Lookup(parentLps->getName()));
				}
			}

			// call the get-LPU-Count function with appropriate parameters
			functionBody << doubleIndent << "return getLPUsCountOfSpace" << lps->getName();
			functionBody << "(ppuCount";
			for (int i = 0; i < paramConfigs->NumElements(); i++) {
				PartitionParameterConfig *currentConfig = paramConfigs->Nth(i);	 
				const char *arrayName = currentConfig->arrayName;
				if (arrayName != NULL) {
					functionBody << parameterSeparator << "\n" << doubleIndent << doubleIndent;
					functionBody <<  "*" << arrayToParentLpus->Lookup(arrayName) << "->" << arrayName;
					functionBody << "PartDims[" << currentConfig->dimensionNo - 1;
					functionBody << "]->partitionDim"; 
				}
				// pass any partition arguments used by current count function
				for (int j = 0; j < currentConfig->partitionArgsIndexes->NumElements(); j++) {
					int index = currentConfig->partitionArgsIndexes->Nth(j);
					functionBody << parameterSeparator << "\n" << doubleIndent << doubleIndent;
					functionBody << "partitionArgs[" << index << "]";		
				}
			}
			functionBody << ")" << statementSeparator; 
		}
		functionBody << singleIndent << "}\n";
	}
	
	functionBody << singleIndent << "return NULL" << statementSeparator;
	functionBody << "}\n";
	
	programFile << std::endl << functionHeader.str() << " " << functionBody.str();
	programFile << std::endl;
}

void generateComputeNextLpuRoutine(std::ofstream &programFile, MappingNode *mappingRoot, 
                Hashtable<List<int>*> *lpuPartFunctionsArgsConfig) {
       
	std::cout << "Generating compute next LPU function" << std::endl; 
	programFile << "// Implementation of task specific compute-Next-LPU function ";
	
	std::string statementSeparator = ";\n";
        std::string singleIndent = "\t";
	std::string doubleIndent = "\t\t";
	std::string tripleIndent = "\t\t\t";
	std::string parameterSeparator = ", ";

	// specify the signature of the compute-Next-Lpu function matching the virtual function in Thread-State class
	std::ostringstream functionHeader;
        functionHeader << "LPU *ThreadStateImpl::computeNextLpu(int lpsId, int *lpuCounts, int *nextLpuId)";
        std::ostringstream functionBody;
	functionBody << "{\n";

	std::deque<MappingNode*> nodeQueue;
	// we skip the mapping root and start iterating with its descendents because the LPU for the root space should
	// not change throughout the computation of the task
        for (int i = 0; i < mappingRoot->children->NumElements(); i++) {
       		nodeQueue.push_back(mappingRoot->children->Nth(i));
        }

        while (!nodeQueue.empty()) {
                MappingNode *node = nodeQueue.front();
                nodeQueue.pop_front();
                for (int i = 0; i < node->children->NumElements(); i++) {
                        nodeQueue.push_back(node->children->Nth(i));
                }
		Space *lps = node->mappingConfig->LPS;
		functionBody << singleIndent << "if (lpsId == Space_" << lps->getName() << ") {\n";

		// create a list of local variables from parent LPUs needed to compute the current LPU for current LPS
		Hashtable<const char*> *parentLpus = new Hashtable<const char*>;
		Hashtable<const char*> *arrayToParentLpus = new Hashtable<const char*>;
		List<const char*> *localArrays = lps->getLocallyUsedArrayNames();
		for (int i = 0; i < localArrays->NumElements(); i++) {
			const char *arrayName = localArrays->Nth(i);
			DataStructure *structure = lps->getLocalStructure(arrayName);
			Space *parentLps = structure->getSource()->getSpace();
			// if the array is inherited from parent LPS by a subpartitioned LPS then correct parent reference
			if (structure->getSpace() != lps) {
				parentLps = structure->getSpace();
			}
			if (parentLpus->Lookup(parentLps->getName()) == NULL) {
				std::ostringstream parentLpuStr;
				functionBody << doubleIndent;
				functionBody << "Space" << parentLps->getName() << "_LPU *";
				parentLpuStr << "space" << parentLps->getName() << "Lpu";
				functionBody << parentLpuStr.str() << " = ";
				functionBody << "(Space" << parentLps->getName() << "_LPU*) \n";
				functionBody << doubleIndent << doubleIndent;
				functionBody << "lpsStates[Space_" << parentLps->getName() << "]->lpu";
				functionBody << statementSeparator;
				arrayToParentLpus->Enter(arrayName, strdup(parentLpuStr.str().c_str()));
				parentLpus->Enter(parentLps->getName(), strdup(parentLpuStr.str().c_str()));
			} else {
				arrayToParentLpus->Enter(arrayName, parentLpus->Lookup(parentLps->getName()));
			}
		}

		// create a variable for current LPU
		functionBody << doubleIndent;
		functionBody << "Space" << lps->getName() << "_LPU *currentLpu = new Space" << lps->getName() << "_LPU";
		functionBody << statementSeparator;

		// if the LPS is partitioned then copy queried LPU id to the lpuId static array of the created LPU
		if (lps->getDimensionCount() > 0) {
			for (int i = 0; i < lps->getDimensionCount(); i++) {
				functionBody << doubleIndent;
				functionBody << "currentLpu->lpuId[" << i << "] = nextLpuId[" << i << "]"; 
				functionBody << statementSeparator;
			}
		}	

		// if the LPS is unpartitioned then we need to create the new LPU by extracting elements from
		// LPUs of ancestor LPSes for common data structures.
		if (lps->getDimensionCount() == 0) {
			for (int i = 0; i < localArrays->NumElements(); i++) {
				const char *arrayName = localArrays->Nth(i);
				const char *parentLpu = arrayToParentLpus->Lookup(arrayName);
				functionBody << doubleIndent << "currentLpu->" << arrayName << " = NULL";
				functionBody << statementSeparator;
				functionBody << doubleIndent << "currentLpu->" << arrayName << "PartDims = ";
				functionBody << parentLpu << "->" << arrayName << "PartDims";
				functionBody << statementSeparator;
			}
		// otherwise we need to call appropriate get-Part functions for invidual data structures of the LPU
		} else {
			for (int i = 0; i < localArrays->NumElements(); i++) {
				const char *arrayName = localArrays->Nth(i);
				ArrayDataStructure *array = (ArrayDataStructure*) lps->getLocalStructure(arrayName);
				functionBody << doubleIndent << "currentLpu->" << arrayName << " = NULL";
				functionBody << statementSeparator;
				functionBody << doubleIndent << "currentLpu->" << arrayName << "PartDims = ";
				
				// if the structure is replicated then just copy its information from the parent
				// NOTE the second condition is for arrays inherited by a subpartitioned LPS
				if (!(array->isPartitioned() && array->getSpace() == lps)) {
					const char *parentLpu = arrayToParentLpus->Lookup(arrayName);
					functionBody << parentLpu << "->" << arrayName << "PartDims";
				// otherwise get the array part for current LPU by calling appropriate function
				} else {
					functionBody << "get" << arrayName << "PartForSpace" << lps->getName() << "Lpu(";
					// pass the default parent dimension parameter to the function
					const char *parentLpu = arrayToParentLpus->Lookup(arrayName);
					functionBody << "\n" << doubleIndent << doubleIndent;
					functionBody << parentLpu << "->" << arrayName << "PartDims";

					// pass default LPU-Count and LPU-Id parameters to the function
					functionBody << parameterSeparator << "lpuCounts";
					functionBody << parameterSeparator << "nextLpuId";
			
					// then pass any additional partition arguments needed for computing part
					// for current data structurre
					std::ostringstream entryName;
                        		entryName << lps->getName() << "_" << arrayName;
					List<int> *argList =  lpuPartFunctionsArgsConfig->Lookup(entryName.str().c_str());
					if (argList->NumElements() > 0) {
						functionBody << parameterSeparator << "\n";	
						functionBody << doubleIndent << doubleIndent;
					}
					for (int j = 0; j < argList->NumElements(); j++) {
						if (j > 0) functionBody << parameterSeparator;	
						functionBody << "partitionArgs[" << argList->Nth(j) << "]";
					}
					functionBody << ")";
				}
				functionBody << statementSeparator;
			}
		}
		functionBody << doubleIndent << "return currentLpu" << statementSeparator;
		functionBody << singleIndent << "}\n";
	}
	
	functionBody << singleIndent << "return NULL" << statementSeparator;
	functionBody << "}\n";
	
	programFile << std::endl << functionHeader.str() << " " << functionBody.str();
	programFile << std::endl;
}

void generateThreadStateImpl(const char *headerFileName, const char *programFileName, 
		MappingNode *mappingRoot, 
                Hashtable<List<PartitionParameterConfig*>*> *countFunctionsArgsConfig,
                Hashtable<List<int>*> *lpuPartFunctionsArgsConfig) {

	std::cout << "Generating task spacific Thread State implementation task.............." << std::endl;	
	std::ofstream programFile, headerFile;
	headerFile.open (headerFileName, std::ofstream::out | std::ofstream::app);
	programFile.open (programFileName, std::ofstream::out | std::ofstream::app);
        if (!programFile.is_open() || !headerFile.is_open()) {
		std::cout << "Unable to open program or header file";
		std::exit(EXIT_FAILURE);
	}
                
	// write the common class definition from the sample file in the header file
	headerFile << "/*-----------------------------------------------------------------------------------\n";
        headerFile << "Thread-State implementation class for the task" << std::endl;
        headerFile << "------------------------------------------------------------------------------------*/\n\n";
	std::string line;
        std::ifstream classDefinitionFile("codegen/thread-state-class-def.txt");
	if (classDefinitionFile.is_open()) {
                while (std::getline(classDefinitionFile, line)) {
			headerFile << line << std::endl;
		}
		headerFile << std::endl;
		classDefinitionFile.close();
	} else {
		std::cout << "Unable to open common include file";
		std::exit(EXIT_FAILURE);
	}
	headerFile.close();

	// write the implementions of virtual functions in the program file
	programFile << "/*-----------------------------------------------------------------------------------\n";
        programFile << "Thread-State implementation class for the task" << std::endl;
        programFile << "------------------------------------------------------------------------------------*/\n\n";
	
	// construct the index array that encode the LPS hierarchy for this task
	generateParentIndexMapRoutine(programFile, mappingRoot);
	// generate the function for creating the root LPU from array metadata information
	generateRootLpuComputeRoutine(programFile, mappingRoot);
	// then call the compute-LPU-Count function generator method for class specific implementation
	generateComputeLpuCountRoutine(programFile, mappingRoot, countFunctionsArgsConfig);
	// then call the compute-Next-LPU function generator method for class specific implementation
	generateComputeNextLpuRoutine(programFile, mappingRoot, lpuPartFunctionsArgsConfig);
 
	programFile.close();
}