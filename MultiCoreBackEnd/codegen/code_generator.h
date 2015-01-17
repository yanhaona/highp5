#ifndef _H_code_generation
#define _H_code_generation

#include "../utils/list.h"

class TaskGlobalScalar;
class TupleDef;
class TaskDef;
class Space;
class MappingNode;
class PPS_Definition;
class EnvironmentLink;

/* function definition to import common header files in generated code and write the namespace */
void initializeOutputFiles(const char *headerFile, 
		const char *programFile, const char *initials);

/* function definition for generating constants for total number of threads and threads per core  */
void generateThreadCountConstants(const char *outputFile, 
		MappingNode *mappingRoot, List<PPS_Definition*> *pcubesConfig);

/* function definition for generating the runtime library routine that will create ThreadIds */
void generateFnForThreadIdsAllocation(const char *headerFile, 
		const char *programFile, 
		const char *initials, 
		MappingNode *mappingRoot, 
		List<PPS_Definition*> *pcubesConfig);

/* function definition for generating array metadata and environment links structures for a task */
List<const char*> *generateArrayMetadataAndEnvLinks(const char *outputFile, 
		MappingNode *mappingRoot,
		List<EnvironmentLink*> *envLinks);

/* function definition to generate data structures representing LPUs of different LPSes */
void generateLpuDataStructures(const char *outputFile, MappingNode *mappingRoot);

/* function definition to close the namespace of the header file after all update is done */
void closeNameSpace(const char *headerFile);

/* function definition to generate classes for all tuple definitions found in the source code */
void generateClassesForTuples(const char *filePath, List<TupleDef*> *tupleDefList);

/* function definition to generate classes for storing task global and thread local variables */
void generateClassesForGlobalScalars(const char *filePath, List<TaskGlobalScalar*> *globalList);

/* function definition to translate the initialize block of a task if exists */
void generateInitializeFunction(const char *headerFile, 
		const char *programFile, const char *initials, 
		List<const char*> *envLinkList, TaskDef *taskDef, Space *rootLps);	

#endif