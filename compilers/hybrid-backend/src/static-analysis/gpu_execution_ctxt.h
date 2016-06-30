#ifndef _H_gpu_execution_ctxt
#define _H_gpu_execution_ctxt

/* When a task has been mapped to the hybrid model, the parts of its computation flow that are dedicated for GPU 
 * execution need to be treated differently from the rests. Recall that in the course of the task execution the 
 * flow of control may enter and leave the GPU many times and have interleaving computations and communicaitons
 * happening in the hosts and the network level. Furthermore depending on the nature of the LPS partitions mapped
 * to the GPU and what GPU PPS they have been mapped to, the generated GPU kernels, GPU LPU offloading logic, etc.
 * will be different. This library holds the classes that maintain information about how GPU execution should be
 * done for different mapping contexts. 
 */

#include "data_flow.h"
#include "data_access.h"
#include "sync_stat.h"
#include "../syntax/ast_expr.h"
#include "../semantics/task_space.h"
#include "../utils/list.h"
#include "../utils/hashtable.h"
#include "../codegen/space_mapping.h"

#include <deque>
#include <fstream>

/* The execution logic we have chosen for GPU LPUs is that the host will generate the LPUs in batch and ship them in 
 * and out of the GPUs. Sometimes the batch of LPUs shipped to the GPU may be multiplexed to arbitrary PPUs of the 
 * intended PPS. Some other times, what LPUs executed by what PPU needs to be controlled precisely (for example LPUs 
 * of subpartitioned LPSes have such requirement). Code generation for these two scenarios need to be done differently. 
 * The following enum, specifies the type for a particular GPU execution context.
 */
enum GpuContextType { 	LOCATION_SENSITIVE_LPU_DISTR_CONTEXT, 
		 	LOCATION_INDIPENDENT_LPU_DISTR_CONTEXT };

/* Data synchronization is a cardinal concern in translating a task's sub-flow that is intended for GPU execution.
 * There is simply no primitive to synchronize update made in different SMs within the confinement of a single kernel.
 * As a result, the sub-flow may need be translated as a series of kernel in the presence of data dependencies among
 * constituent compute stages. The situation can get further complicated when the dependencies are repeated. This 
 * class embodies the portion of a GPU context sub-flow that are grouped inside a single repeat block where the repeat
 * iterations will be done at the host level and within each iteration a group of kernels will be launced in the GPU.   
 */
class KernelGroupConfig {
  protected:
	// an identifier to be used during code generation
	int groupId;
	// tells if the kernel group does repeat or not
	bool repeatingKernels;
	// the condition to repeat on for a repetitive kernel group
	Expr *repeatCondition;
	// original list of flow stages from the source code that are included in the kernel group
	List<FlowStage*> *contextSubflow;
	// As we mentioned earlier, the stages from the source code cannot be executed just as they are due to the
	// synchronization limitation in the GPGPU platform. Therefore, we need to translate the contextSubflow into
	// something that we can execute in the GPU as a series of kernel calls. This represents the translation
	// of the context-sub-flow of the above. 
	List<CompositeStage*> *kernelConfigs;
  public:
	KernelGroupConfig(int groupId, List<FlowStage*> *contextSubflow);
	KernelGroupConfig(int groupId, RepeatCycle *repeatCycle);
	int getGroupId() { return groupId; }
	List<CompositeStage*> *getKernelDefinitions() { return kernelConfigs; }
	void describe(int indentLevel);

	// function that generates kernel configurations from the context subflow 
	void generateKernelConfig(PCubeSModel *pcubesModel, Space *contextLps);

  private:
	// a recursive DFS based kernel configurations construction process used by the public function of the same
	// name from above 
	void generateKernelConfig(std::deque<FlowStage*> *stageQueue,
			int gpuTransitionLevel, 
			Space *contextLps, 
			List<CompositeStage*> *currentConfigList, 
			CompositeStage *configUnderConstruct,
			List<SyncRequirement*> *configSyncSignals);	
};

/* This class represents a particular sub-flow in a task's computation flow that should be executed in the GPU */
class GpuExecutionContext {
  protected:
	// the GPU entrance point LPS for the current context
	Space *contextLps;
	// sequence of top level flow stages -- there might be flow stages nested in them -- that form the current
	// context
	List<FlowStage*> *contextFlow;
	// type of LPU distribution to be used for the current context
	GpuContextType contextType;
	// two properties for maintaining detail information about data accesses happenned inside the current context
	Hashtable<VariableAccess*> *varAccessLog;
	List<const char*> *epochDependentVarAccesses;
	// generated configurations of groups of kernel that will execute the logic of the context flow in the GPU
	List<KernelGroupConfig*> *kernelConfigList;
  public:
	// A static access point to all GPU execution contexts of a task is maintained here so that they can be 
	// accessed during code generation process. This is needed as LPU traversal process for execution contexts  
	// differs based on their context types requiring the generation of flow stage invocation code happen under 
	// the guidence of the appropriate context  
	static Hashtable<GpuExecutionContext*> *gpuContextMap;
  public:
	GpuExecutionContext(int topmostGpuPps, List<FlowStage*> *contextFlow);
	Space *getContextLps() { return contextLps; }
	GpuContextType getContextType() { return contextType; }
	List<KernelGroupConfig*> *getKernelConfigList() { return kernelConfigList; }

	// the context ID, which is the index of the first flow stage within the context, is used for searching the 
	// context during code generation
	int getContextId();
	
	// a name based on the context ID is used to name the generated GPU code executor class for this context
	const char *getContextName();
	static const char *generateContextName(int contextId);

	// data access information retrieval functions
	List<const char*> *getVariableAccessList();
	List<const char*> *getModifiedVariableList();
	List<const char*> *getEpochDependentVariableList() { return epochDependentVarAccesses; }
	List<const char*> *getEpochIndependentVariableList();

	// this routine generates CUDA kernels and surrounding offloading functions for task sub-flow of the execution 
	// context
	void generateKernelConfigs(PCubeSModel *pcubesModel);

	// this routine is used to generate LPU generation and traversal code inside the generated task::run function
	// based on the GPU context type 
	void generateInvocationCode(std::ofstream &stream, int indentation, Space *callingCtxtLps);

	// function that generates the offloading CUDA kernel from a kernel configuration
	void generateGpuKernel(CompositeStage *kernelDef, 
			std::ofstream &programFile, PCubeSModel *pcubesModel);

	void describe(int indent);
  private:
	// It can happen that the computation flow dives into a lower level LPS in the GPU directly from a host
	// level LPS without going through the nesting of the upper level GPU LPS that has been mapped to some higher
	// GPU PPS. Even in those scenarios, we take the first LPS in the path to the entry stage's LPS that has been
	// mapped to the GPU as the context LPS. We will rather do the lower level LPU generation within the 
	// generated kernels as opposed to ship in smaller LPUs to the kernels. Furthermore, our data part allocation
	// scheme for GPU LPUs also demand that host to GPU context switching should happen at the topmost LPS mapped
	// to the GPU. 
	Space *getContextLps(int topmostGpuPps, Space *entryStageLps);

	// We need to know what variables have been accessed and how inside the sub-flow to decide data state in/out
	// requirement for the current context. This function does the analysis  
	void performVariableAccessAnalysis();

	// These are two auxiliary functions used by the generateInvocationCode routine. Remember that the get-next-
	// Lpu LPU generation routine is a recursive process that goes up and down in the LPS hierarchy in search for
	// new LPUs for the GPU context LPS. The flexibility of the IT language allows the programmer to dive from a
	// several level higher up LPS in the host into the GPU context's LPS directly. As a result, if we just grab 
	// the LPUs generated for the context LPSes in batches and ship them to the GPU card without further 
	// consideration then the LPUs in a batch may have a mixture of upper level LPUs as their ancestors as opposed
	// to all being derived from the same upper level LPU. This is not problematic from the correctness perspective, 
	// but such arbritrary nature of LPU multiplexing requires that we stage-in much more metadata information per
	// LPU in the GPU card. Rather, we adopt the simpler strategy that we generate the LPUs from the calling 
	// context LPS to down until the immediate parent LPS of the gpu execution context LPS in the host then invoke
	// the code for offloading GPU LPUs for the contex LPS. This simpler strategy will ensure that all LPUs that 
	// get executed as part of a single batch have the same ancester LPUs at the host level LPSes.
	// function for generating the LPU offloading code 
	const char *spewOffloadingContextCode(int indentation);
	// function for wrapping up the offloading code inside upper level LPSes LPU traversal
	void wrapOffloadingCodeInLargerContext(std::ofstream &stream, int indentation, 
			List<Space*> *transitLpsList, 
			int index, const char *offloadingCode);	

	// This is a helper routine that is used during GPU kernel generation, to copy GPU card memory data in and out
	// of the shared memory of the SMs. The primary concern here is to distribute threads and warps in a way that
	// reduces non-coalesced global memory accesses. In the future we should incorporate concerns such as shared
	// memory bank conflicts avoidance and improving parallelism in the data copying logic.
	// The function returns an indent string to be appended before any statement placed inside the generated loops.  
	const char *generateDataCopyingLoopHeaders(std::ofstream &stream, 
			ArrayDataStructure *array, 
			int indentLevel, bool warpLevel);
	// this function is used to generate a single element transfer instruction between the GPU card memory and the
	// shared memory of an SM for an array. The transfer-direction parameter indicates what memory should be read
	// and what should be written (1 = read from card and write to SM; otherwise, do vice versa).
	void generateElementTransferStmt(std::ofstream &stream, 
			ArrayDataStructure *array, 
			const char *indentPrefix, 
			bool warpLevel, int transferDirection);
};

#endif
