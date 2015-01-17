#include "code_generator.h"
#include "space_mapping.h"
#include "name_transformer.h"
#include "../semantics/task_space.h"
#include "../utils/list.h"
#include "../utils/string_utils.h"
#include "../syntax/ast_def.h"
#include "../syntax/ast_task.h"
#include "../syntax/ast_type.h"
#include "../static-analysis/task_global.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string.h>
#include <stdio.h>

void initializeOutputFiles(const char *headerFileName, 
		const char *programFileName, const char *initials) {

	std::string line;
        std::ifstream commIncludeFile("codegen/default-includes.txt");
	std::ofstream programFile, headerFile;
        headerFile.open (headerFileName, std::ofstream::out);
        programFile.open (programFileName, std::ofstream::out);
        if (!programFile.is_open()) {
		std::cout << "Unable to open output program file";
		std::exit(EXIT_FAILURE);
	}
	if (!headerFile.is_open()) {
		std::cout << "Unable to open output header file";
		std::exit(EXIT_FAILURE);
	}
	
	headerFile << "#ifndef _H_" << initials << std::endl;
	headerFile << "#define _H_" << initials << std::endl << std::endl;

	int taskNameIndex = string_utils::getLastIndexOf(headerFileName, '/') + 1;
	char *taskName = string_utils::substr(headerFileName, taskNameIndex, strlen(headerFileName));
	
	programFile << "/*-----------------------------------------------------------------------------------" << std::endl;
        programFile << "header file for the task" << std::endl;
        programFile << "------------------------------------------------------------------------------------*/" << std::endl;
	programFile << "#include \"" << taskName  << '"' << std::endl << std::endl;
                
	programFile << "/*-----------------------------------------------------------------------------------" << std::endl;
        programFile << "header files included for different purposes" << std::endl;
        programFile << "------------------------------------------------------------------------------------*/" << std::endl;
	
	if (commIncludeFile.is_open()) {
                while (std::getline(commIncludeFile, line)) {
			headerFile << line << std::endl;
			programFile << line << std::endl;
		}
		headerFile << std::endl;
		programFile << std::endl;
	} else {
		std::cout << "Unable to open common include file";
		std::exit(EXIT_FAILURE);
	}

	headerFile << "namespace " << string_utils::toLower(initials) << " {\n\n";
	programFile << "using namespace " << string_utils::toLower(initials) << ";\n\n";

	commIncludeFile.close();
	programFile.close();
	headerFile.close();
}

void generateThreadCountConstants(const char *outputFile,       
                MappingNode *mappingRoot, List<PPS_Definition*> *pcubesConfig) {
	
	std::ofstream programFile;
	programFile.open (outputFile, std::ofstream::out | std::ofstream::app);
        if (programFile.is_open()) {
                programFile << "/*-----------------------------------------------------------------------------------" << std::endl;
                programFile << "constants for total and par core thread counts" << std::endl;
                programFile << "------------------------------------------------------------------------------------*/" << std::endl;
	} else {
		std::cout << "Unable to open output program file";
		std::exit(EXIT_FAILURE);
	}
	
	// find lowest PPS to which any LPS has been mapped and highest PPS that has an un-partitioned LPS 
	// mapped to it 
	std::deque<MappingNode*> nodeQueue;
        nodeQueue.push_back(mappingRoot);
	int lowestPpsId = pcubesConfig->Nth(0)->id;
	int highestPartitionedPpsId = 1;
	int highestUnpartitionedPpsId = lowestPpsId; // the top-most PPS handling the root LPS
        while (!nodeQueue.empty()) {
                MappingNode *node = nodeQueue.front();
                nodeQueue.pop_front();
                for (int i = 0; i < node->children->NumElements(); i++) {
                        nodeQueue.push_back(node->children->Nth(i));
                }
		PPS_Definition *pps = node->mappingConfig->PPS;
		if (pps->id < lowestPpsId) lowestPpsId = pps->id;
		Space *lps = node->mappingConfig->LPS;
		if (lps->getDimensionCount() > 0 && pps->id > highestPartitionedPpsId) {
			highestPartitionedPpsId = pps->id;
		} else if (lps->getDimensionCount() == 0) {
			if (pps->id > highestPartitionedPpsId && pps->id < highestUnpartitionedPpsId) {
				highestUnpartitionedPpsId = pps->id;
			}
		}
	}
	
	// compute the total number of threads that will participate in computing for the task
	int totalThreads = 1;
	for (int i = 0; i < pcubesConfig->NumElements(); i++) {
		PPS_Definition *pps = pcubesConfig->Nth(i);
		if (pps->id >= highestUnpartitionedPpsId) continue;
		totalThreads *= pps->units;
		if (pps->id == lowestPpsId) break;
	}
	programFile << "const int Total_Threads = " << totalThreads << ';' << std::endl;
	
	// determine the number of threads attached par core to understand how to do thread affinity management
	int coreSpaceId = pcubesConfig->Nth(0)->id;
	for (int i = 0; i < pcubesConfig->NumElements(); i++) {
		PPS_Definition *pps = pcubesConfig->Nth(i);
		if (pps->coreSpace) {
			coreSpaceId = pps->id;
			break;
		}
	}
	int ppsCount = pcubesConfig->NumElements();
	int threadsParCore = 1;
	for (int i = coreSpaceId - 1; i >= lowestPpsId; i--) {
		PPS_Definition *pps = pcubesConfig->Nth(ppsCount - i);
		threadsParCore *= pps->units;
	}	
	programFile << "const int Threads_Par_Core = " << threadsParCore << ';' << std::endl;
	programFile.close();
}

void generateFnForThreadIdsAllocation(const char *headerFileName, 
                const char *programFileName, 
                const char *initials,
                MappingNode *mappingRoot, 
                List<PPS_Definition*> *pcubesConfig) {

        std::string statementSeparator = ";\n";
        std::string statementIndent = "\t";
	std::ofstream programFile, headerFile;
        
	programFile.open (programFileName, std::ofstream::out | std::ofstream::app);
	headerFile.open (headerFileName, std::ofstream::out | std::ofstream::app);
        if (!programFile.is_open() || !headerFile.is_open()) {
		std::cout << "Unable to open header/program file";
		std::exit(EXIT_FAILURE);
	}
                
	headerFile << "\n/*-----------------------------------------------------------------------------------\n";
        headerFile << "function to generate PPU IDs and PPU group IDs for a thread\n";
        headerFile << "------------------------------------------------------------------------------------*/\n";
	programFile << "/*-----------------------------------------------------------------------------------\n";
        programFile << "function to generate PPU IDs and PPU group IDs for a thread\n";
        programFile << "------------------------------------------------------------------------------------*/\n";

	std::ostringstream functionHeader;
        functionHeader << "getPpuIdsForThread(int threadNo)";
        std::ostringstream functionBody;
        
	functionBody << " {\n\n" << statementIndent;
	functionBody << "ThreadIds *threadIds = new ThreadIds";
	functionBody << statementSeparator;
	// allocate a new array to hold the PPU Ids of the thread
	functionBody << statementIndent<< "threadIds->ppuIds = new PPU_Ids[Space_Count]" << statementSeparator;
	// declare a local array to hold the index of the thread in different PPS group for ID assignment
	// to be done accurately 
	functionBody << statementIndent << "int idsArray[Space_Count]" << statementSeparator;
	functionBody << statementIndent << "idsArray[Space_Root] = threadNo" << statementSeparator; 

	std::deque<MappingNode*> nodeQueue;
        for (int i = 0; i < mappingRoot->children->NumElements(); i++) {
        	nodeQueue.push_back(mappingRoot->children->Nth(i));
        }

	// declare some local variables needed for thread Id calculation
	functionBody << std::endl;
	functionBody << statementIndent << "int threadCount" << statementSeparator;
	functionBody << statementIndent << "int groupSize" << statementSeparator;
	functionBody << statementIndent << "int groupThreadId" << statementSeparator;
	functionBody << std::endl;

        while (!nodeQueue.empty()) {
                MappingNode *node = nodeQueue.front();
                nodeQueue.pop_front();
                for (int i = 0; i < node->children->NumElements(); i++) {
                        nodeQueue.push_back(node->children->Nth(i));
                }

		PPS_Definition *pps = node->mappingConfig->PPS;
		Space *lps = node->mappingConfig->LPS;
		MappingNode *parent = node->parent;
		Space *parentLps = parent->mappingConfig->LPS;	
		PPS_Definition *parentPps = parent->mappingConfig->PPS;

		// determine the number of partitions current PPS makes to the parent PPS
		int partitionCount = 1;
		int ppsCount = pcubesConfig->NumElements();
		for (int i = parentPps->id - 1; i >= pps->id; i--) {
			partitionCount *= pcubesConfig->Nth(ppsCount - i)->units;
		}

		// create a prefix and variable name to make future references easy
		std::string namePrefix = "threadIds->ppuIds[Space_";
		std::ostringstream varNameStr;
		varNameStr << namePrefix << lps->getName() << "]";
		std::string varName = varNameStr.str();
		std::ostringstream groupThreadIdStr; 
	
		functionBody << statementIndent << "// for Space " << lps->getName() << statementSeparator;
		// if the current LPS is a subpartition then most of the fields of a thread Id can be 
		// copied from its parent LPU configuration
		if (lps->isSubpartitionSpace()) {
			functionBody << statementIndent << varName << ".groupId = 0" << statementSeparator;	
			functionBody << statementIndent << varName << ".ppuCount = 1" << statementSeparator;
			functionBody << statementIndent;
			functionBody  << varName << ".groupSize = ";
			functionBody << namePrefix << parentLps->getName() << "].groupSize";
			functionBody << statementSeparator;
			functionBody << statementIndent << varName << ".id = 0" << statementSeparator;
			functionBody << statementIndent;
			functionBody << "idsArray[Space_" << lps->getName() << "] = idsArray[Space_";
			functionBody << parentLps->getName() << "]" << statementSeparator << std::endl;
			continue;
		}

		// determine the total number of threads contributing in the parent PPS and current thread's 
		// index in that PPS 
		if (parent == mappingRoot) {
			functionBody << statementIndent << "threadCount = Total_Threads";
			functionBody << statementSeparator;
			groupThreadIdStr << "idsArray[Space_Root]";
		} else {
			functionBody << statementIndent;
			functionBody << "threadCount = " << namePrefix << parentLps->getName() << "].groupSize";
			functionBody << statementSeparator;
			groupThreadIdStr << "idsArray[Space_" << parentLps->getName() << "]";
		}

		// determine the number of threads per group in the current PPS
		functionBody << statementIndent;
		if (lps->getDimensionCount() > 0) {
			functionBody << "groupSize = threadCount" << " / " << partitionCount;
		} else 	functionBody << "groupSize = threadCount";
		functionBody << statementSeparator;

		// determine the id of the thread in the group it belongs to	
		functionBody << statementIndent;
		functionBody << "groupThreadId = " << groupThreadIdStr.str() << " \% groupSize";
		functionBody << statementSeparator;

		// assign proper group Id, PPU count, and group size in the PPU-Ids variable created before 
		functionBody << statementIndent;
		functionBody  << varName << ".groupId = " << groupThreadIdStr.str() << " / groupSize";
		functionBody << statementSeparator;	
		functionBody << statementIndent;
		functionBody  << varName << ".ppuCount = " << partitionCount;
		functionBody << statementSeparator;
		functionBody << statementIndent;
		functionBody  << varName << ".groupSize = groupSize";
		functionBody << statementSeparator;

		// assign PPU Id to the thread depending on its groupThreadId
		functionBody << statementIndent;
		functionBody << "if (groupThreadId == 0) " << varName << ".id\n"; 
		functionBody << statementIndent << statementIndent << statementIndent;
		functionBody <<  "= " << varName << ".groupId";
		functionBody << statementSeparator;	
		functionBody << statementIndent;
		functionBody << "else " << varName << ".id = INVALID_ID";
		functionBody << statementSeparator;	
		
		// store the index of the thread in the group for subsequent references	
		functionBody << statementIndent;
		functionBody << "idsArray[Space_" << lps->getName() << "] = groupThreadId";
		functionBody << statementSeparator;
		functionBody << std::endl;
	}
	functionBody << statementIndent << "return threadIds" << statementSeparator;
	functionBody << "}\n";

	headerFile << "ThreadIds *" << functionHeader.str() << ";\n\n";	
	programFile << std::endl << "ThreadIds *" << initials << "::"; 
	programFile <<functionHeader.str() << " " << functionBody.str();
	programFile << std::endl;

	headerFile.close();
	programFile.close();
}

void generateLpuDataStructures(const char *outputFile, MappingNode *mappingRoot) {
       
	std::cout << "Generating data structures for LPUs\n";
 
	std::string statementSeparator = ";\n";
        std::string statementIndent = "\t";
	std::ofstream programFile;
        
	programFile.open (outputFile, std::ofstream::out | std::ofstream::app);
        if (programFile.is_open()) {
                programFile << "/*-----------------------------------------------------------------------------------" << std::endl;
                programFile << "Data structures representing LPS and LPU contents " << std::endl;
                programFile << "------------------------------------------------------------------------------------*/" << std::endl;
	} else {
		std::cout << "Unable to open output program file";
		std::exit(EXIT_FAILURE);
	}

	std::deque<MappingNode*> nodeQueue;
        nodeQueue.push_back(mappingRoot);
        while (!nodeQueue.empty()) {
                MappingNode *node = nodeQueue.front();
                nodeQueue.pop_front();
                for (int i = 0; i < node->children->NumElements(); i++) {
                        nodeQueue.push_back(node->children->Nth(i));
                }
		Space *lps = node->mappingConfig->LPS;
		List<const char*> *localArrays = lps->getLocallyUsedArrayNames();

		// create the object for containing references to data structures of the LPS
		programFile << "\nclass Space" << lps->getName() << "_Content {\n";
		programFile << "  public:\n";
		for (int i = 0; i < localArrays->NumElements(); i++) {
			ArrayDataStructure *array = (ArrayDataStructure*) lps->getLocalStructure(localArrays->Nth(i));
			ArrayType *arrayType = (ArrayType*) array->getType();
			const char *elemType = arrayType->getTerminalElementType()->getName();
			programFile << statementIndent << elemType << " *" << array->getName();
			programFile << statementSeparator;	
		}
		programFile << "};\n\n";

		// create the object for representing an LPU of the LPS
		programFile << "class Space" << lps->getName() << "_LPU : public LPU {\n";
		programFile << "  public:\n";
		for (int i = 0; i < localArrays->NumElements(); i++) {
			ArrayDataStructure *array = (ArrayDataStructure*) lps->getLocalStructure(localArrays->Nth(i));
			ArrayType *arrayType = (ArrayType*) array->getType();
			const char *elemType = arrayType->getTerminalElementType()->getName();
			programFile << statementIndent << elemType << " *" << array->getName();
			programFile << statementSeparator;
			int dimensions = array->getDimensionality();
			programFile << statementIndent << "PartitionDimension **";
			programFile << array->getName() << "PartDims";
			programFile << statementSeparator;	
		}
		// add a specific lpu_id static array with dimensionality equals to the dimensions of the LPS
		if (lps->getDimensionCount() > 0) {
			programFile << statementIndent << "int lpuId[";
			programFile << lps->getDimensionCount() << "]";
			programFile << statementSeparator;
		}	
		programFile << "};\n";
	}
	
	programFile << std::endl;
	programFile.close();
}

List<const char*> *generateArrayMetadataAndEnvLinks(const char *outputFile, MappingNode *mappingRoot,
                List<EnvironmentLink*> *envLinks) {

	std::cout << "Generating array metadata and environment links\n";
	
	std::string statementSeparator = ";\n";
        std::string statementIndent = "\t";
	std::ofstream programFile;
        
	programFile.open (outputFile, std::ofstream::out | std::ofstream::app);
        if (programFile.is_open()) {
                programFile << "/*-----------------------------------------------------------------------------------" << std::endl;
                programFile << "Data structures for Array-Metadata and Environment-Links " << std::endl;
                programFile << "------------------------------------------------------------------------------------*/" << std::endl;
	} else {
		std::cout << "Unable to open output program file";
		std::exit(EXIT_FAILURE);
	}
	
	// construct an array metadata object by listing all arrays present in the root LPS
	Space *rootLps = mappingRoot->mappingConfig->LPS;
	programFile << "\nclass ArrayMetadata {\n";
	programFile << "  public:\n";
	List<const char*> *localArrays = rootLps->getLocallyUsedArrayNames();
	for (int i = 0; i < localArrays->NumElements(); i++) {
		ArrayDataStructure *array = (ArrayDataStructure*) rootLps->getLocalStructure(localArrays->Nth(i));
		int dimensions = array->getDimensionality();
		programFile << statementIndent;
		programFile << "Dimension " << array->getName() << "Dims[" << dimensions << "]";
		programFile << statementSeparator;
	}
	programFile << "};\n";
	programFile << "ArrayMetadata arrayMetadata" << statementSeparator;
	
	// create a class for environment links; also generate a list of the name of such links to be returned
	List<const char*> *linkList = new List<const char*>;
	programFile << "\nclass EnvironmentLinks {\n";
	programFile << "  public:\n";
	for (int i = 0; i < envLinks->NumElements(); i++) {
		EnvironmentLink *link = envLinks->Nth(i);
		if (!link->isExternal()) continue;
		const char *linkName = link->getVariable()->getName();
		DataStructure *structure = rootLps->getLocalStructure(linkName);
		ArrayDataStructure *array = dynamic_cast<ArrayDataStructure*>(structure);
		if (array != NULL) {
               		ArrayType *arrayType = (ArrayType*) array->getType();
               		const char *elemType = arrayType->getTerminalElementType()->getName();
               		programFile << statementIndent << elemType << " *" << array->getName();
               		programFile << statementSeparator;
               		int dimensions = array->getDimensionality();
               		programFile << statementIndent;
               		programFile << "Dimension " << array->getName() << "Dims[" << dimensions << "]";
               		programFile << statementSeparator;
		} else {
			Type *type = structure->getType();
			const char *declaration = type->getCppDeclaration(structure->getName());
			programFile << statementIndent << declaration << statementSeparator;
		}
		linkList->Append(linkName);
	}	
	programFile << "};\n";
	programFile << "EnvironmentLinks environmentLinks" << statementSeparator << std::endl;
	programFile.close();
	return linkList;
}

void closeNameSpace(const char *headerFile) {
	std::ofstream programFile;
	programFile.open (headerFile, std::ofstream::app);
	if (programFile.is_open()) {
		programFile << std::endl << '}' << std::endl;
		programFile << "#endif" << std::endl;
		programFile.close();
	} else {
		std::cout << "Could not open header file" << std::endl;
		std::exit(EXIT_FAILURE);
	}
	programFile.close();
}

void generateClassesForTuples(const char *filePath, List<TupleDef*> *tupleDefList) {
	std::ofstream headerFile;
	headerFile.open(filePath, std::ofstream::out);
	if (!headerFile.is_open()) {
		std::cout << "Unable to open header file for tuple definitions\n";
		std::exit(EXIT_FAILURE);
	}
	headerFile << "#ifndef _H_tuple\n";
	headerFile << "#define _H_tuple\n\n";

	// by default include header file for standard vector for any list variable that may present
	// in any tuple definition
	headerFile << "#include <iostream>\n";	
	headerFile << "#include <vector>\n\n";	

	// first have a list of forward declarations for all tuples to avoid having errors during 
	// compilation of individual classes
	for (int i = 0; i < tupleDefList->NumElements(); i++) {
		TupleDef *tupleDef = tupleDefList->Nth(i);
		headerFile << "class " << tupleDef->getId()->getName() << ";\n";
	}
	headerFile << "\n";

	// then generate a class for each tuple in the list
	for (int i = 0; i < tupleDefList->NumElements(); i++) {
		// if the tuple definition has no element inside then ignore it and proceed to the next
		TupleDef *tupleDef = tupleDefList->Nth(i);
		List<VariableDef*> *variables = tupleDef->getComponents();
		// otherwise, generate a new class and add the elements as public components
		headerFile << "class " << tupleDef->getId()->getName() << " {\n";
		headerFile << "  public:\n";
		for (int j = 0; j < variables->NumElements(); j++) {
			headerFile << "\t";
			VariableDef *variable = variables->Nth(j);
			Type *type = variable->getType();
			const char *varName = variable->getId()->getName();
			headerFile << type->getCppDeclaration(varName);
			headerFile << ";\n";
		}
		headerFile << "};\n\n";
	}

	headerFile << "#endif\n";
	headerFile.close();
}

void generateClassesForGlobalScalars(const char *filePath, List<TaskGlobalScalar*> *globalList) {
	
	std::cout << "Generating structures holding task global and thread local scalar\n";

	std::ofstream headerFile;
	headerFile.open (filePath, std::ofstream::out | std::ofstream::app);
	if (!headerFile.is_open()) {
		std::cout << "Unable to open output header file for task\n";
		std::exit(EXIT_FAILURE);
	}
                
	headerFile << "/*-----------------------------------------------------------------------------------\n";
        headerFile << "Data structures for Task-Global and Thread-Local scalar variables\n";
        headerFile << "------------------------------------------------------------------------------------*/\n\n";
	
	std::ostringstream taskGlobals, threadLocals;
	taskGlobals << "class TaskGlobals {\n";
	taskGlobals << "  public:\n";
	threadLocals << "class ThreadLocals {\n";
	threadLocals << "  public:\n";

	for (int i = 0; i < globalList->NumElements(); i++) {
		TaskGlobalScalar *scalar = globalList->Nth(i);
		// determine to which class the global should go into
		std::ostringstream *stream = &taskGlobals;
		if (scalar->isLocallyManageable()) {
			stream = &threadLocals;
		}
		// then write the variable declaration within the stream
		Type *type = scalar->getType();
		*stream << "\t";
		*stream << type->getCppDeclaration(scalar->getName());
		*stream << ";\n";		
	}
	
	taskGlobals << "};\n\n";
	threadLocals << "};\n";

	headerFile << taskGlobals.str() << threadLocals.str();
	headerFile.close();
}

void generateInitializeFunction(const char *headerFileName, const char *programFileName, const char *initials,
                List<const char*> *envLinkList, TaskDef *taskDef, Space *rootLps) {
        
	std::cout << "Generating function for the initialize block\n";

	std::string statementSeparator = ";\n";
        std::string statementIndent = "\t";
	std::string parameterSeparator = ", ";
	std::ofstream programFile, headerFile;
        
	programFile.open (programFileName, std::ofstream::out | std::ofstream::app);
	headerFile.open (headerFileName, std::ofstream::out | std::ofstream::app);
        if (!programFile.is_open() || !headerFile.is_open()) {
		std::cout << "Unable to open header/program file for initialize block generation";
		std::exit(EXIT_FAILURE);
	}
                
	headerFile << "\n/*-----------------------------------------------------------------------------------\n";
        headerFile << "function for the initialize block\n";
        headerFile << "------------------------------------------------------------------------------------*/\n";
	programFile << "/*-----------------------------------------------------------------------------------\n";
        programFile << "function for the initialize block\n";
        programFile << "------------------------------------------------------------------------------------*/\n";

	// put three default parameters for task-globals, thread-locals, and partition configuration
	std::ostringstream functionHeader;
        functionHeader << "initializeTask(TaskGlobals taskGlobals";
	functionHeader << parameterSeparator << '\n' << statementIndent << statementIndent; 
	functionHeader << "ThreadLocals threadLocals";
	functionHeader << parameterSeparator << '\n' << statementIndent << statementIndent;
	functionHeader << string_utils::getInitials(taskDef->getName());
	functionHeader << "Partition partition";

        std::ostringstream functionBody;
	functionBody << "{\n\n";

	ntransform::NameTransformer *transformer = ntransform::NameTransformer::transformer;
	for (int i = 0; i < envLinkList->NumElements(); i++) {
		const char *envLink = envLinkList->Nth(i);
		// if the variable is an array then its dimension information needs to be copied from the
		// environment link object to array metadata object of all subsequent references
		if (transformer->isGlobalArray(envLink)) {
			const char *varName = transformer->getTransformedName(envLink, true, false);
			ArrayDataStructure *array = (ArrayDataStructure*) rootLps->getLocalStructure(envLink);
			int dimensionCount = array->getDimensionality();
			for (int j = 0; j < dimensionCount; j++) {
				functionBody << statementIndent;
				functionBody << varName << "[" << j << "]";
				functionBody << " = " << "environmentLinks.";
				functionBody << envLink;
				functionBody << "Dims[" << j << "]";
				functionBody << statementSeparator;
			}
		// otherwise the value of the scalar variable should be copied back to task global or thread
		// local variable depending on what is the right destination
		} else {
			functionBody << statementIndent;
			functionBody << transformer->getTransformedName(envLink, true, false);
			functionBody << " = " << "environmentLinks.";
			functionBody << envLink;
			functionBody << statementSeparator;
		}
	}

	InitializeInstr *initSection = taskDef->getInitSection();
	if (initSection != NULL) {
		
		// iterate over all initialization parameters and add them as function arguments
		List<const char*> *argNames = initSection->getArguments();
		List<Type*> *argTypes = initSection->getArgumentTypes();
		for (int i = 0; i < argNames->NumElements(); i++) {
			const char *arg = argNames->Nth(i);
			Type *type = argTypes->Nth(i);
			functionHeader << parameterSeparator;
			functionHeader << "\n" << statementIndent << statementIndent;
			functionHeader << type->getCppDeclaration(arg);
			// if any argument matches a global variable in the task then copy it to the appropriate
			// data structure
			if (transformer->isThreadLocal(arg) || transformer->isTaskGlobal(arg)) {
				functionBody << statementIndent;
				functionBody << transformer->getTransformedName(arg, true, false);
				functionBody << " = " << arg;
				functionBody << statementSeparator;
			}
		}
		// then translate the user code in the init section into a c++ instruction stream
		initSection->generateCode(functionBody);
	}

	functionHeader << ")";
	functionBody << "}\n";

	headerFile << "void " << functionHeader.str() << ";\n\n";	
	programFile << std::endl << "void " << initials << "::"; 
	programFile <<functionHeader.str() << " " << functionBody.str();
	programFile << std::endl;

	headerFile.close();
	programFile.close();
}