#ifndef _H_env_instruction
#define _H_env_instruction

#include "environment.h"
#include "../runtime/array_transfer.h"

/*-------------------------------------------------------------------------------------------------------------------------------------
				 Environment Instructions to be Processed At Task Initialization
-------------------------------------------------------------------------------------------------------------------------------------*/

// this is the base class for all types of instructions for initializing an environmental data structure that a task going 
// to access/create as part of its execution 
class TaskInitEnvInstruction {
  protected:
	// the item in the task environment this instruction is going to operate on
	TaskItem *itemToUpdate;
  public:
	TaskInitEnvInstruction(TaskItem *itemToUpdate) { this->itemToUpdate = itemToUpdate; }
	TaskItem *getItemToUpdate() { return itemToUpdate; }
	
	// this function should be called before the task has been scheduled for execution because without dimension lengths
	// information, partition configuration and other necessary metadata for parts of the data structure cannot be
	// constructed, precluding any further processing of data parts
	virtual void setupDimensions() = 0;

	// some instructions may lead to update/removal of existing versions of the data structure stored in the program
	// environment; this function should be called to do those changes 
	virtual void preprocessProgramEnv() = 0;

	// this function should be called after partition configurations, part-container tree, etc. metadata have been
	// gathered for the task item; to prepare the parts list for the data structure before processing of computation 
	// stages can begin
	virtual void setupPartsList() = 0;

	// this function should be invoked to ensure any new/updated parts list for the data structure has been included in 
	// the program environment
	virtual void postprocessProgramEnv() = 0;		

	// each subclass should return an unique type number to enable instructions retrieval by type
	virtual int getType() = 0;
  protected:
	//------------------------------------------------- a group of helper functions to be used by sub-classes to provide 
	// ------------------------------------------------ implementations for the virtual functions
	
	// function to let go of any existing parts-list references to the program environment for the target task item and
	// initiate garbage collection if applicable 
	void removeOldPartsListReferences();

	// allocates memory for the data parts of different LPS allocations of the target task item	
	void allocatePartsLists();

	// generates a new data source key for the target item
	void assignDataSourceKeyForItem();

	// creates a new object-version-manager and initialize it in the program environment for a newly created data item 
	void initiateVersionManagement();

	// flags parts-lists already existing in the environment for the underlying data item as fresh 
	void recordFreshPartsListVersions();	
};

/* This is the default instruction for linked task environmental variables. If there is no other instruction associated with
 * such a variable at task invocation, a checking must be performed to ensure that the existing parts list is up-to-date. If
 * it is stale then an automatic data transfer instruction should be issued by the library to undertake a fresh to stale list
 * content transfer. 
 */
class StaleRefreshInstruction : public TaskInitEnvInstruction {
  public:
	StaleRefreshInstruction(TaskItem *itemToUpdate) : TaskInitEnvInstruction(itemToUpdate) {}
	
	// refreshing a probably stale parts list of an existing data item does not change its dimension information  
	void setupDimensions() {}

	// no program environment preprocessing is required for this instruction 
	void preprocessProgramEnv() {}

	void setupPartsList() {};

	// At the end of parts-list setup -- may it cause data transfer or not -- all parts-lists of the underlying item are
	// fresh again. So they should be flagged fresh in the program environment.
	void postprocessProgramEnv() { recordFreshPartsListVersions(); }

	int getType() { return 0; }			
};

/* This is the instruction for environmental variables created by the task; creation of a new data item for such a variable 
 * for may result in removal of a previously created item during a previous execution of the task.
 */
class CreateFreshInstruction : public TaskInitEnvInstruction {
  public:	
	CreateFreshInstruction(TaskItem *itemToUpdate) : TaskInitEnvInstruction(itemToUpdate) {}
	
	// items created because of the task execution gets their dimension set-up by the task initializer section; there is
	// no need for their dimensions to be initialized, neither there is any scope for it
	void setupDimensions() {}

	// if a new data item is going to be created for the underlying variable in the task then the task should let go of
	// its reference for the parts list of the same variable that has been created during an earlier execution of the task 	
	void preprocessProgramEnv() { removeOldPartsListReferences(); }

	// setting up parts list should involve just allocating memory for the parts and prepare an item key source reference
	void setupPartsList();

	// a fresh version manager should be instantiated as the item is a created data structure
	void postprocessProgramEnv() { initiateVersionManagement(); }		
	
	int getType() { return 1; }			
};

/* This, as the name suggests, causes the data parts content of a task item to be read from some external file.
 */
class ReadFromFileInstruction : public TaskInitEnvInstruction {
  protected:
	const char *fileName;
  public:	
	ReadFromFileInstruction(TaskItem *itemToUpdate) : TaskInitEnvInstruction(itemToUpdate) {
		this->fileName = NULL;
	}
	void setFileName(const char *fileName) { this->fileName = fileName; }
	
	// read the dimension metadata that appears at the beginning of the data file and copy back that information in the
	// dimension properties of the task-item
	void setupDimensions();

	// Reading contents from a file means the task is letting go of its earlier data item for the underlying variable. So
	// the task's reference to any earlier parts-list, if exists, maintained in the program environment should be removed.
	void preprocessProgramEnv() { removeOldPartsListReferences(); }

	void setupPartsList();

	// As reading contents from a file results in generation of a new data item, a new version manager should be started
	// for this case too.   
	void postprocessProgramEnv() { initiateVersionManagement(); }
			
	int getType() { return 2; }			
};

/* This encodes an explicit object assignment from one task to another task environment in the form envA.a = envB.b; note
 * that only portion of the data item can be assigned from the source to the destination task's environment using the array
 * sub-range expression.
 * TODO: note that at the initial phase, we are assuming that the source and the destination items have the same dimension to
 * do make the implementation simplar. This restriction does not hold in general and we should remove it in the future.
 */
class DataTransferInstruction : public TaskInitEnvInstruction {
  protected:
	ArrayTransferConfig *transferConfig;
  public:	
	DataTransferInstruction(TaskItem *itemToUpdate) : TaskInitEnvInstruction(itemToUpdate) {
		this->transferConfig = NULL;
	}
	void setTransferConfig(ArrayTransferConfig *config) { transferConfig = config; }
	ArrayTransferConfig *getTransferConfig() { return transferConfig; }
	
	// the root dimension for the destination should be determined from the dimension transfer information available in 
	// the transfer config object
	void setupDimensions();
	
	// data transfer from some other task's item to the target item of the underlying task should result in removal of the
	// current references the task has for the item as the item is now going to hold a different data content
	void preprocessProgramEnv() { removeOldPartsListReferences(); }

	void setupPartsList() {};
	
	// Data transfer instruction is always related to some existing data version manager and after the parts-list setup
	// current task-item's LPS allocations are fresh. Thus we need to record their freshness in the program environment.
	void postprocessProgramEnv() { recordFreshPartsListVersions(); }	
		
	int getType() { return 3; }			
};

/*-------------------------------------------------------------------------------------------------------------------------------------
		            Environment Instructions to be Processed At Task Completion or Program End
-------------------------------------------------------------------------------------------------------------------------------------*/

/* This is the base class for all types of instructions that tell how the completion of a task should affect the overall
 * program environment. For example, if a task updates a data item having multiple versions in the program environment 
 * then versions that are not updated should be marked stale.
 */
class TaskEndEnvInstruction {
  protected:
	TaskItem *envItem;
  public:
	TaskEndEnvInstruction(TaskItem *envItem) { this->envItem = envItem; }
	void execute() {
		updateProgramEnv();
		doAdditionalProcessing();
	}
	virtual void updateProgramEnv() = 0;
	virtual void doAdditionalProcessing() = 0;
};

/* instruction for recording updates in stale/fresh versions list for a data item at task completion
 */
class ChangeNotifyInstruction : public TaskEndEnvInstruction {
  public:
	ChangeNotifyInstruction(TaskItem *envItem) : TaskEndEnvInstruction(envItem) {}
	void updateProgramEnv() {};
	void doAdditionalProcessing() {};
};

#endif
