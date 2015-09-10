#include "part_distribution.h"
#include "../utils/list.h"
#include "../utils/binary_search.h"
#include "../part-management/part_tracking.h"
#include "../part-management/part_folding.h"
#include <iostream>
#include <vector>

using namespace std;

//------------------------------------------------------- Container ----------------------------------------------------------/

Container::Container(int id, LpsDimConfig config) {
	this->id = id;
	this->config = config;
	this->segmentTags = vector<int>();
	this->parent = NULL;
}

void Container::addSegmentTag(int tag) {
	int existingTagPosition = binsearch::locateKey(segmentTags, tag);
	if (existingTagPosition == KEY_NOT_FOUND) {
		int locationOfInsert = binsearch::locatePointOfInsert(segmentTags, tag);
		segmentTags.insert(segmentTags.begin() + locationOfInsert, tag);
	}
}

void Container::addAllSegmentTags(std::vector<int> tags) {
	for (int i = 0; i < tags.size(); i++) {
		addSegmentTag(tags.at(i));
	}
}

int *Container::getCurrentLevelPartId(int dataDimensions) {
	int *partId = new int[dataDimensions];
	int myLps = config.getLpsId();
	Container *current = this;
	while (current->config.getLpsId() == myLps) {
		partId[current->config.getDimNo()] = current->id;
		current = current->parent;
		if (current == NULL) break;
	}
	return partId;
}

vector<int*> *Container::getPartId(int dataDimensions) {

	vector<int*> *partIdVector = new vector<int*>;
	int *partId = new int[dataDimensions];
	int lastLps = config.getLpsId();
	Container *current = this;

	// level is -1 for the root container; so the recursion should terminate at that point
	while (current->config.getLevel() != -1) {
		int currentContainerId = current->id;
		int currentLps = current->config.getLpsId();
		int currentDimNo = current->config.getDimNo();
		if (currentLps == lastLps) {
			partId[currentDimNo] = currentContainerId;
		} else {
			partIdVector->insert(partIdVector->begin(), partId);
			partId = new int[dataDimensions];
			partId[currentDimNo] = currentContainerId;
		}
		current = current->parent;
		if (current == NULL) break;
	}
	return partIdVector;
}

bool Container::hasSegmentTag(int tag) {
	int location = binsearch::locateKey(segmentTags, tag);
	return (location != KEY_NOT_FOUND);
}

PartFolding *Container::foldContainerForSegment(int segmentTag, std::vector<LpsDimConfig> dimOrder, bool foldBack) {
	if (!hasSegmentTag(segmentTag)) return NULL;
	if (foldBack) return foldBackContainer(NULL);
	PartFolding *partFolding = new PartFolding(id, config.getDimNo(), config.getLevel());
	return partFolding;
}

PartFolding *Container::foldBackContainer(PartFolding *foldingUnderConstruct) {

	// level is -1 for the root container that should be skipped during folding
	if (config.getLevel() == -1) return foldingUnderConstruct;

	PartFolding *partFolding = new PartFolding(id, config.getDimNo(), config.getLevel());
	if (foldingUnderConstruct != NULL) {
		partFolding->addDescendant(foldingUnderConstruct);
	}
	return (parent != NULL) ? parent->foldBackContainer(partFolding) : partFolding;
}

//--------------------------------------------------------- Branch -----------------------------------------------------------/

Branch::Branch(LpsDimConfig branchConfig, Container *firstEntry) {
	this->branchConfig = branchConfig;
	descendants = vector<Container*>();
	descendants.push_back(firstEntry);
	descendentIds.push_back(firstEntry->getId());
}

Branch::~Branch() {
	while(descendants.size() > 0) {
		Container *container = descendants.at(descendants.size() - 1);
		descendants.pop_back();
		delete container;
	}
}

void Branch::addEntry(Container *descendant) {
	int key = descendant->getId();
	int location = binsearch::locatePointOfInsert(descendentIds, key);
	descendants.insert(descendants.begin() + location, descendant);
	descendentIds.insert(descendentIds.begin() + location, key);
}

Container *Branch::getEntry(int id) {
	int location = binsearch::locateKey(descendentIds, id);
	if (location != KEY_NOT_FOUND) {
		return descendants.at(location);
	}
	return NULL;
}

List<Container*> *Branch::getContainersForSegment(int segmentTag) {
	List<Container*> *containerList = new List<Container*>;
	for (int i = 0; i < descendants.size(); i++) {
		Container *container = descendants.at(i);
		if (container->hasSegmentTag(segmentTag)) {
			containerList->Append(container);
		}
	}
	return containerList;
}

void Branch::replaceDescendant(Container *descendant) {
	int descendantId = descendant->getId();
	int location = binsearch::locateKey(descendentIds, descendantId);
	descendants.erase(descendants.begin() + location);
	descendants.insert(descendants.begin() + location, descendant);
}

//--------------------------------------------------- Branching Container ----------------------------------------------------/

BranchingContainer::~BranchingContainer() {
	while (branches->NumElements() > 0) {
		Branch *branch = branches->Nth(0);
		branches->RemoveAt(0);
		delete branch;
	}
	delete branches;
}

Branch *BranchingContainer::getBranch(int lpsId) {
	for (int i = 0; i < branches->NumElements(); i++) {
		Branch *branch = branches->Nth(i);
		if (branch->getConfig().getLpsId() == lpsId) {
			return branch;
		}
	}
	return NULL;
}

void BranchingContainer::insertPart(vector<LpsDimConfig> dimOrder, int segmentTag, List<int*> *partId, int position) {

	LpsDimConfig dimConfig = dimOrder.at(position);
	int lpsId = dimConfig.getLpsId();
	int containerId = partId->Nth(dimConfig.getLevel())[dimConfig.getDimNo()];
	bool lastEntry = (position == dimOrder.size() - 1);
	Container *nextContainer = NULL;
	Branch *branch = getBranch(lpsId);
	if (branch != NULL) {
		nextContainer = branch->getEntry(containerId);
	}
	if (nextContainer == NULL) {
		nextContainer = (lastEntry) ? new Container(containerId, dimConfig)
				: new BranchingContainer(containerId, dimConfig);
		nextContainer->addSegmentTag(segmentTag);
		if (branch == NULL) {
			branches->Append(new Branch(dimConfig, nextContainer));
		} else {
			branch->addEntry(nextContainer);
		}
	} else {
		BranchingContainer *intermediate = dynamic_cast<BranchingContainer*>(nextContainer);
		HybridBranchingContainer *hybrid = dynamic_cast<HybridBranchingContainer*>(nextContainer);
		if (lastEntry && intermediate != NULL && hybrid == NULL) {
			hybrid = HybridBranchingContainer::convertIntermediate(intermediate, segmentTag);
			branch->replaceDescendant(hybrid);
			nextContainer = hybrid;
		} else if (!lastEntry && intermediate == NULL) {
			hybrid = HybridBranchingContainer::convertLeaf(nextContainer, segmentTag);
			branch->replaceDescendant(hybrid);
			nextContainer = hybrid;
		} else if (hybrid != NULL) {
			hybrid->addSegmentTag(segmentTag, lastEntry);
		} else {
			nextContainer->addSegmentTag(segmentTag);
		}
	}
	nextContainer->setParent(this);
	if (!lastEntry) {
		BranchingContainer *nextLevel = reinterpret_cast<BranchingContainer*>(nextContainer);
		nextLevel->insertPart(dimOrder, segmentTag, partId, position + 1);
	}
}

Container *BranchingContainer::getContainer(List<int*> *pathToContainer, vector<LpsDimConfig> dimOrder, int position) {
	LpsDimConfig dimConfig = dimOrder.at(position);
	int lpsId = dimConfig.getLpsId();
	int containerId = pathToContainer->Nth(dimConfig.getLevel())[dimConfig.getDimNo()];
	bool lastEntry = (position == dimOrder.size() - 1);
	Branch *branch = getBranch(lpsId);
	if (branch == NULL) return NULL;
	Container *container = branch->getEntry(containerId);
	if (lastEntry || container == NULL) return container;
	BranchingContainer *nextLevel = reinterpret_cast<BranchingContainer*>(container);
	return nextLevel->getContainer(pathToContainer, dimOrder, position + 1);
}

List<Container*> *BranchingContainer::listDescendantContainersForLps(int lpsId, int segmentTag) {
	List<Container*> *containerList = new List<Container*>;
	Branch *branch = getBranch(lpsId);
	if (branch == NULL) {
		return containerList;
	}
	List<Container*> *containersOnBranch = branch->getContainersForSegment(segmentTag);
	for (int i = 0; i < containersOnBranch->NumElements(); i++) {
		Container *nextContainer = containersOnBranch->Nth(i);
		BranchingContainer *nextBranch = dynamic_cast<BranchingContainer*>(nextContainer);
		if (nextBranch == NULL || nextBranch->getBranch(lpsId) == NULL) {
			containerList->Append(nextContainer);
		} else {
			List<Container*> *nextBranchList = nextBranch->listDescendantContainersForLps(lpsId, segmentTag);
			containerList->AppendAll(nextBranchList);
			delete nextBranchList;
		}
	}
	delete containersOnBranch;
	return containerList;
}

PartFolding *BranchingContainer::foldContainerForSegment(int segmentTag, std::vector<LpsDimConfig> dimOrder, bool foldBack) {

	if (!hasSegmentTag(segmentTag)) return NULL;
	int position = 0;
	while (!dimOrder.at(position).isEqual(config)) position++;
	if (position == dimOrder.size() - 1) {
		HybridBranchingContainer *hybrid = dynamic_cast<HybridBranchingContainer*>(this);
		if (hybrid != NULL) {
			Container *leafContainer = hybrid->getLeaf();
			return leafContainer->foldContainerForSegment(segmentTag, dimOrder, foldBack);
		} else {
			return Container::foldContainerForSegment(segmentTag, dimOrder, foldBack);
		}
	}

	PartFolding *folding = new PartFolding(id, config.getDimNo(), config.getLevel());
	foldContainer(segmentTag, folding->getDescendants(), dimOrder, position + 1);

	if (folding->getDescendants()->NumElements() == 0) {
		delete folding;
		return NULL;
	}
	return (foldBack && parent != NULL) ? parent->foldBackContainer(folding) : folding;
}

void BranchingContainer::foldContainer(int segmentTag,
		List<PartFolding*> *fold, std::vector<LpsDimConfig> dimOrder, int position) {

	LpsDimConfig nextConfig = dimOrder.at(position);
	Branch *branch = getBranch(nextConfig.getLpsId());
	if (branch != NULL) {
		List<Container*> *containerList = branch->getContainersForSegment(segmentTag);
		int nextPosition = position + 1;

		for (int i = 0; i < containerList->NumElements(); i++) {

			Container *container = containerList->Nth(i);
			BranchingContainer *nextBranch = dynamic_cast<BranchingContainer*>(container);
			HybridBranchingContainer *hybrid = dynamic_cast<HybridBranchingContainer*>(container);
			PartFolding *foldElement = NULL;

			if (nextPosition < dimOrder.size() - 1) {
				PartFolding *subFold = new PartFolding(container->getId(), nextConfig.getDimNo(), nextConfig.getLevel());
				nextBranch->foldContainer(segmentTag, subFold->getDescendants(), dimOrder, nextPosition);
				if (subFold->getDescendants()->NumElements() > 0) {
					foldElement = subFold;
				} else delete subFold;
			} else {
				container = (hybrid != NULL) ? hybrid->getLeaf() : container;
				foldElement = container->foldContainerForSegment(segmentTag, dimOrder, false);
			}

			if (foldElement == NULL) continue;

			// if this is the first sub-fold then we add it in the list right away
			if (fold->NumElements() == 0) {
				fold->Append(foldElement);
			// otherwise we first check if we can coalesce the current sub-fold with the previous to make the representation
			// more compact
			} else {
				PartFolding *previousElement = fold->Nth(fold->NumElements() - 1);
				int containerId = container->getId();
				if (previousElement->getIdRange().max == containerId - 1
						&& foldElement->isEqualInContent(previousElement)) {
					previousElement->coalesce(Range(containerId, containerId));
					delete foldElement;
				} else {
					fold->Append(foldElement);
				}
			}
		}
	}
}

//----------------------------------------------- Hybrid Branching Container -------------------------------------------------/

HybridBranchingContainer *HybridBranchingContainer::convertLeaf(Container *leafContainer, int branchSegmentTag) {

	LpsDimConfig leafConfig = leafContainer->getConfig();
	int leafId = leafContainer->getId();
	BranchingContainer *intermediate = new BranchingContainer(leafId, leafConfig);
	intermediate->addSegmentTag(branchSegmentTag);

	// Note that all segment tags from the leaf container have been inserted in the intermediate container but the
	// converse is not done in the next function. This is because, a leaf container lies within the hybrid but the
	// intermediate part of the hybrid works as a normal branching container, exposed to the hierarchy. When a search
	// for a leaf container with a particular segment Id has been issued, we should be able to locate the hybrid that
	// may contain it. If we do not copy segment tags of the leaf in the branch container then we may miss valid leaf
	// containers residing within hybrid containers.
	intermediate->addAllSegmentTags(leafContainer->getSegmentTags());

	return new HybridBranchingContainer(intermediate, leafContainer);
}

HybridBranchingContainer *HybridBranchingContainer::convertIntermediate(BranchingContainer *branchContainer,
		int terminalSegmentTag) {
	LpsDimConfig intermediateConfig = branchContainer->getConfig();
	int intermediateId = branchContainer->getId();
	Container *leaf = new Container(intermediateId, intermediateConfig);
	leaf->setParent(branchContainer->getParent());
	leaf->addSegmentTag(terminalSegmentTag);
	return new HybridBranchingContainer(branchContainer, leaf);
}

void HybridBranchingContainer::addSegmentTag(int segmentTag, bool leafLevelTag) {
	if (leafLevelTag) leaf->addSegmentTag(segmentTag);
	Container::addSegmentTag(segmentTag);
}
