#ifndef _H_part_management
#define _H_part_management

/* This header file lists classes that are used to hold the data content of a PPU during a task execution.
   As an LPU is scheduled for execution, instances of this classes are consulted to generate appropriate
   metadata and set proper data references to the template LPU description based on partition configurations.	
   This classes are also used to deduce data interval configurations of other PPUs that the current PPU may
   interact with to synchronize on update of shared data. Note that most of the functionalities offered in
   this library are actually implemented in other libraries of the memory-management module. Therefore, this
   library is just a kind of a convenient interface to manage data at runtime.  
*/

#include "../utils/utility.h"
#include "../utils/list.h"
#include "../utils/hashtable.h"
#include "../codegen/structure.h"
#include "part_generation.h"
#include "allocation.h"

/* This class holds the configuration and content of a data structure of a single LPS  handled by a PPU */
class DataItems {
  protected:
	// name of the data structure
	const char *name;
	// dimensionality of the data structure
	int dimensionality;
	// partition configuration for each dimension
	List<DimPartitionConfig*> *dimConfigList;
	// generated data partition config from individual dimension configuration
	DataPartitionConfig *partitionConfig;
	// structure holding the list of data parts that belong to current PPU 
	DataPartsList *partsList;
	// the number of epoch step needs to be retained if the structure is epoch dependent
	int epochCount;
	// a flag indicating that the data items have been initialized and ready to be used in computation
	bool ready;
  public:
	DataItems(const char *name, int dimensionality, int epochCount);
	void addDimPartitionConfig(int dimensionId, DimPartitionConfig *dimConfig);
	void generatePartitionConfig();
	DataPartitionConfig *getPartitionConfig();
	void setPartsList(DataPartsList *partsList) { this->partsList = partsList; }
	// function to get the most uptodate version of a part of the structure
	DataPart *getDataPart(int *lpuId);
	// function to get an older epoch version of a part
	DataPart *getDataPart(int *lpuId, int epoch);
	List<DataPart*> *getAllDataParts();
	virtual void advanceEpoch() { partsList->advanceEpoch(); }
};

/* Scalar variables are dimensionless; therefore do not mesh well with the DataItems class structure. 
   Regardless, we want to make an uniform interface for epoch dependency and holding LPU contents. Thereby,
   this class has been added to extend the DataItems class.
*/
class ScalarDataItems : public DataItems {
  protected:
	// to be generic, the versions of the scalar variable are stored as void pointers; a circular array
	// of these pointers are maintained for version dependency
	void **variableList;
	// points to the most recent version of the 
	int epochHead;
  public:
	ScalarDataItems(const char *name, int epochCount);
	template <class type> void allocate(type zeroValue) {
		variableList = (void **) new type*[epochCount];
		for (int i = 0; i < epochCount; i++) {
			type *version = new type;
			*version = zeroValue;
			variableList[i] = (void*) version;
		}
		ready = true;
	}
	// function to get the reference of the latest version of the variable
	void *getVariable();
	// function to get the reference of some earlier epoch version of the variable
	void *getVariable(int version);
	inline void advanceEpoch() { epochHead = (epochHead + 1) % epochCount; };
};

/* This class holds LPU data parts of all variables correspond to a single LPS */
class LpsContent {
  protected:
	// id of the LPS
	int id;
	// a mapping from variable names to their data parts
	Hashtable<DataItems*> *dataItemsMap;
  public:
	LpsContent(int id);
	inline void addDataItems(const char *varName, DataItems *dataItems) {
		dataItemsMap->Enter(varName, dataItems);
	}
	inline DataItems *getDataItems(const char *varName) { return dataItemsMap->Lookup(varName); }
	void advanceItemEpoch(const char *varName);
};

#endif