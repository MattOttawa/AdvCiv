#pragma once

#ifndef KMOD_PATHFINDER_H
#define KMOD_PATHFINDER_H

/*	advc.pf, advc.104b: Refactored, moving the parts specific to group movement
	into a derived class in GroupPathFinder.h.
	Header-only in order to avoid explicit instantiations.
	Comments in the KmodPathFinder member function definitions are from K-Mod unless they
	state otherwise; all comments elsewhere are from AdvCiv unless they state otherwise. */


enum PathNodeState // Replacing FAStarListType; cf. PathNodeBase::isState.
{
	PATHNODE_UNINITIALIZED, // default
	PATHNODE_OPEN,
	PATHNODE_CLOSED
};

/*	FAStarNode minus some data that KmodPathFinder doesn't use.
	Instead of the all-purpose m_iData1, m_iData2 members, the path length
	is now always part of the node data. Classes derived from KmodPathFinder that
	require additional data should derive a node class from PathNode.
	No constructors should be added; PathNodeMap handles the memory allocation
	through memset (as in K-Mod). This also means that all data members will have
	the initial value 0. */
template<class Node> // (CRT pattern)
class PathNodeBase
{
public:
	PathNodeBase(); // public - to avoid a compiler warning (c4610), but w/o implementation.
	// Tbd.: try CvPlot* m_pPlot -- why would we need to look up a node that we already have?
	PlotNumTypes m_ePlot; // Was (x,y). A single id makes NodeMap more efficient.
	// Keeping these public (for now) for interchangeability with FAStarNode
	int m_iTotalCost;
	int m_iKnownCost;
	int m_iHeuristicCost;
	int m_iPathLength; // FAStarNode::m_iData2 in K-Mod
	Node* m_pParent;
	Node* m_apChildren[NUM_DIRECTION_TYPES];
	short m_iNumChildren;
	/*	Replaces m_bOnStack and m_eFAStarListType. K-Mod comment (edited for clarity):
		"I'd like to use NO_FASTARLIST as a signal that the node is uninitialised,
		but unfortunately the default value for m_eFAStarListType is FASTARLIST_OPEN
		because FASTARLIST_OPEN==0. Hence KmodPathFinder uses m_bOnStack to mean
		that the node is both ready to be used and connected.
		m_eFAStarListType is initialised manually."
		m_eFAStarListType was set by K-Mod but never read. A single boolean is
		indeed sufficient, but multiple states are safer to use and may allow for
		performance optimizations in the future. */
	__forceinline bool isState(PathNodeState eState) const
	{
		return (m_iState == eState);
	}
	__forceinline void setState(PathNodeState eState)
	{
		m_iState = static_cast<char>(eState);
	}
private:
	char m_iState;
};

class PathNode : public PathNodeBase<PathNode> {};

/*	Combines the CvPathSettings class in K-Mod with the FAStarFunc declarations
	originally in CvGameCoreUtils.h.
	(One could regard invalid steps as having infinite cost,
	so the name "StepMetric" isn't really too narrow.)
	The template parameter of KmodPathFinder should be derived from this class.
	Derived classes have to shadow some of the member functions (those that
	otherwise show an FErrorMsg at runtime). Those functions aren't pure virtual
	b/c this class is only intended for compile-time polymorphism, and
	virtual function calls come with a small performance penalty. (Some functions
	get called very frequently by KmodPathFinder.) */
template<class Node = PathNode>
class StepMetricBase
{
public:
	/*	KmodPathFinder will only consider paths of this length or shorter.
		It's up to updatePathData to compute the length and to store it
		at the given node. */
	inline int getMaxPath() const { return m_iMaxPath; }
	// Derived classes should probably not change this
	inline int initialPathLength() const { return 1; }
protected:
	/*	Derived classes have to have a 0-argument constructor that will get called
		when KmodPathFinder is instantiated. */
	StepMetricBase(int iMaxPath = -1)
	:	m_iMaxPath(iMaxPath < 0 ? MAX_INT : iMaxPath)
	{}
	int m_iMaxPath;
public:
	/*	isValidStep, canStepThrough and isValidDest, collectively, need to
		ensure the following:
		-	For two adjacent plots p and q that both aren't the path destination
			and a path w from the start to p,
			isValidStep(p, q) && canStepThrough(p)
			needs to be true if and only if the step from p to q can be added to w.
		-	If q is the path destination, then the conjunction above needs to be
			true if and only if the step _could_ be added to w if some plot other
			than p and q were the (valid; see below) path destination.
			(For a single path generator call, it wouldn't matter how the
			destination is treated, but the result can become cached and will
			then matter for subsequent calls with a different destination.)
		-	For the path start s and destination d and all plots p adjacent to d,
			isValidStep(p, d) && isValidDest(s, d)
			needs to be true if and only if the step from p to d can be added to
			any path that reaches p from s.
		-	isValidStep is the most frequently called function among these three,
			so it should perform as little work as possible.
		Remarks:
		-	I've come up with this specification based on karadoc's code.
			I wish it were less complicated, but I do want it to be fast.
		-	More broadly speaking, canStepThrough should check whether a plot
			can be entered if we assume that it isn't the destination, and
			isValidStep should check any conditions that depend on the kFrom plot. */
	bool isValidStep(CvPlot const& kFrom, CvPlot const& kTo) const
	{
		FErrorMsg("Should've been hidden by a derived-class member");
		return false;
	}
	/*	kNode is the path finder node associated with kPlot. (The
		group step metric will require some data from that node.)
		The node data should not be used to enforce the path length limit;
		KmodPathFinder handles that. */
	bool canStepThrough(CvPlot const& kPlot, Node const& kNode) const
	{
		FErrorMsg("Should've been hidden by a derived-class member");
		return false;
	}
	inline bool isValidDest(CvPlot const& kStart, CvPlot const& kDest) const
	{
		FErrorMsg("Should've been hidden by a derived-class member");
		return false;
	}
	/*	Cost of the step from kFrom to kTo. Should return a positive value.
		The step checker functions above ensure that the step is allowed;
		so there's no reason to return "infinity" (MAX_INT).
		kParentNode is the node associated with kFrom. (The cost function
		of the group path finder needs this info.) */
	int cost(CvPlot const& kFrom, CvPlot const& kTo, Node const& kParentNode) const
	{
		FErrorMsg("Should've been hidden by a derived-class member");
		return -1;
	}
	/*	Heuristic cost (pessimistic estimate) of moving from kFrom to kTo.
		I don't know how well KmodPathFinder would cope with an inadmissable
		heuristic, i.e. one that can overestimate the cost. */
	int heuristicCost(CvPlot const& kFrom, CvPlot const& kTo) const
	{
		FErrorMsg("Should've been hidden by a derived-class member");
		return -1;
	}
	/*	Called when the step from kParent to kNode gets added to a path.
		Should set the length of the path leading to kNode based on the
		path length stored at kParent, and set any other path data that
		a derived class may want to keep track of.
		The new path length shouldn't be smaller than the old (kParent)
		path length.
		Should return true if any kNode data was changed, false otherwise. */
	inline bool updatePathData(Node& kNode, Node const& kParent) const
	{
		kNode.m_iPathLength = kParent.m_iPathLength + 1; // uniform
		return true;
	}
	/*	Called on the start node before generating a path.
		Derived classes that shadow this function should still call this function;
		i.e. they should leave the initialization of path length to this class. */
	inline void initializePathData(Node& kNode) const
	{
		kNode.m_iPathLength = initialPathLength();
	}
	/*	Called before generating a path if the start node is already initialized
		from a previous path finder call. Returning false will cause the
		path finder's node data to be reset. Don't check kStart.m_iPathLength;
		KmodPathFinder handles that. */
	inline bool canReuseInitialPathData(Node const& kStart) const
	{
		return true;
	}
};

/*	The class parameter should be derived from StepMetricBase. Policy-based design,
	in part, to avoid the overhead of dynamic dispatch. */
template<class StepMetric, class Node = PathNode>
class KmodPathFinder
{
protected:

	/*	Replaces
		typedef std::vector<Node*> OpenList;
		with a wrapper class that sets the correct PathNodeState.
		Tbd.: vector::erase is expensive. Try std::list -- though it could be
		that the fast vector traversal is more important. */
	class OpenList
	{
	public:
		typedef typename std::vector<Node*>::iterator iterator;
		typedef typename std::vector<Node*>::const_iterator const_iterator;
		inline const_iterator begin() const
		{
			return m_pNodes.begin();
		}
		inline const_iterator end() const
		{
			return m_pNodes.end();
		}
		inline iterator begin()
		{
			return m_pNodes.begin();
		}
		inline iterator end()
		{
			return m_pNodes.end();
		}
		// Does not change the state of the nodes
		inline void clear()
		{
			m_pNodes.clear();
		}
		// These function do change the state of the nodes (hence the names)
		inline void open(Node& kNode)
		{
			m_pNodes.push_back(&kNode);
			// Inefficient to add the same node multiple times
			FAssert(!kNode.isState(PATHNODE_OPEN)); // Tbd.: don't keep this assert permanently
			kNode.setState(PATHNODE_OPEN);
		}
		inline void close(iterator pos)
		{
			Node& kNode = **pos;
			m_pNodes.erase(pos);
			FAssert(kNode.isState(PATHNODE_OPEN));
			kNode.setState(PATHNODE_CLOSED);
		}
	private:
		std::vector<Node*> m_pNodes;
	};
	/*struct OpenList_sortPred {
		bool operator()(FAStarNode const*& kpLeft, FAStarNode const*& kpRight) {
			return (kpLeft->m_iTotalCost < kpRight->m_iTotalCost);
		}
	};*/ // (K-Mod)
	/*	^Looks like K-Mod has already tried a heap (std::priority_queue). Or maybe
		it was abandoned w/o a test. Would have to re-heap after recalculateHeuristics.
		And the functor should take m_stepMetric.getMaxPath() as a contructor argument
		so that nodes within that limit can receive absolute priority. But, given the
		modest number of open nodes, I doubt that a heap will be worthwhile. */

	/*	Map from plots to nodes. Replacing naked array in K-Mod.
		Historical note: Before K-Mod 1.45,
		stdext::hash_map<int,boost::shared_ptr<FAStarNode> > had been used. */
	class NodeMap
	{
	public:
		inline NodeMap(PlotNumTypes eMaxPlots) : m_eMaxPlots(eMaxPlots)
		{
			m_data = new byte[numBytes()];
			reset();
		}
		inline ~NodeMap()
		{
			delete[] m_data;
		}
		inline Node& get(PlotNumTypes ePlot)
		{
			return reinterpret_cast<Node*>(m_data)[ePlot];
		}
		inline void reset()
		{
			/*	advc: Might be able to save time here by resetting only those
				nodes that have -possibly- been initialized. There's a section
				"Beating memset" by Dan Higgins in the book mentioned in FAStarNode.h. */
			PROFILE_FUNC();
			memset(m_data, 0, numBytes());
		}
	private:
		byte* m_data;
		PlotNumTypes m_eMaxPlots;

		inline int numBytes()
		{
			return sizeof(Node) * m_eMaxPlots;
		}
	};

	StepMetric m_stepMetric;
public:
	/*	It's up to the derived classes to define a function for setting up m_stepMetric.
		This constructor will only call the StepMetric default constructor. */
	KmodPathFinder();
	virtual ~KmodPathFinder();
	void resetNodes();
	bool generatePath(CvPlot const& kStart, CvPlot const& kDest);
	// Note: Should prefer the version above, i.e. don't pass plots by coordinates.
	bool generatePath(int iStartX, int iStartY, int iDestX, int iDestY)
	{
		return generatePath(
				m_kMap.getPlot(iStartX, iStartY),
				m_kMap.getPlot(iDestX, iDestY));
	}
	bool isPathComplete() const { return (m_pEndNode != NULL); }
	int getPathLength() const // advc: Was "getPathTurns"; too specific.
	{
		if (m_pEndNode == NULL)
		{
			FAssert(m_pEndNode != NULL);
			return 0;
		}
		return m_pEndNode->m_iPathLength;
	}
	CvPlot* getPathFirstPlot() const; // tbd.: could return CvPlot&

protected:
	CvMap const& m_kMap;
	OpenList m_openList;
	NodeMap* m_pNodeMap;
	Node* m_pEndNode;
	// <advc> Replacing (x,y) coordinates
	PlotNumTypes m_eStart;
	PlotNumTypes m_eDest;
	CvPlot const* m_pStart;
	CvPlot const* m_pDest; // </advc>

	void addStartNode();
	void recalculateHeuristics();
	bool processNode();
	void forwardPropagate(Node& kHead, int iCostDelta);
	// advc: Moved into NodeMap
	//FAStarNode& GetNode(int x, int y) { return node_data[y * map_width + x]; }
	// advc: Not needed anymore
	//bool validateNodeMap();
};

// (Comments below are from K-Mod unless stated otherwise)

template<class StepMetric, class Node>
KmodPathFinder<StepMetric,Node>::KmodPathFinder()
	:	m_eStart(NO_PLOT_NUM), m_eDest(NO_PLOT_NUM), m_pStart(NULL), m_pDest(NULL),
	/*	[...] Ideally the pathfinder would be initialised with a given CvMap
		and then not refer to any global objects. [...] */
	// advc: We can do that:
	m_kMap(GC.getMap()), m_pEndNode(NULL), m_pNodeMap(NULL
	/*	advc: KmodPathFinder sometimes gets instantiated w/o ultimately getting used.
		Therefore allocate memory as late as possible. */
	/*new NodeMap(m_kMap.numPlots())*/)
{}

template<class StepMetric, class Node>
KmodPathFinder<StepMetric,Node>::~KmodPathFinder()
{
	if (m_pNodeMap != NULL)
		delete m_pNodeMap;
}

template<class StepMetric, class Node>
bool KmodPathFinder<StepMetric,Node>::generatePath(
	CvPlot const& kStart, CvPlot const& kDest)
{
	PROFILE_FUNC();

	m_pEndNode = NULL;
	// advc.104b: Pass kStart for team path finder
	if (!m_stepMetric.isValidDest(kStart, kDest))
		return false;

	if (m_pNodeMap == NULL)
		m_pNodeMap = new NodeMap(m_kMap.numPlots());

	if (&kStart != m_pStart)
	{
		/*	Note: It may be possible to salvage some of the old data to
			get more speed. E.g. if the moves recorded on the node match the group
			[advc: or other step metric data], just delete everything that isn't a
			direct descendant of the new start. and then subtract the start cost
			and moves from all the remaining nodes. */
		resetNodes();
	}

	bool bRecalcHeuristics = true;
	if (m_pDest == &kDest)
		bRecalcHeuristics = false;

	m_pStart = &kStart;
	m_pDest = &kDest;
	m_eStart = m_kMap.plotNum(kStart);
	m_eDest = m_kMap.plotNum(kDest);
	{
		Node& kStartNode = m_pNodeMap->get(m_eStart);
		if (!kStartNode.isState(PATHNODE_UNINITIALIZED))
		{
			if (m_stepMetric.canReuseInitialPathData(kStartNode))
			{	// advc: Currently ensured by &kStart!=m_pStart
				FAssert(kStartNode.m_iPathLength == m_stepMetric.initialPathLength());
			}
			else
			{
				resetNodes();
				FAssert(kStartNode.isState(PATHNODE_UNINITIALIZED));
			}
		}
		if (kStartNode.isState(PATHNODE_UNINITIALIZED))
		{
			addStartNode();
			bRecalcHeuristics = true;
		}
		/*	advc (note): What if kStartNode is closed?
			For a fixed start, processNode guarantees (I think) that,
			for every destination, some node on the shortest path
			to that destination remains open. */
	}
	{
		Node& kDestNode = m_pNodeMap->get(m_eDest);
		if (!kDestNode.isState(PATHNODE_UNINITIALIZED))
			m_pEndNode = &kDestNode;
		/*	advc (note): If kDestNode is closed, then it could be that we've
			been unable to move through it on a previous call, but, this time,
			we only need to enter it, and isValidDest says that we can. */
	}
	if (bRecalcHeuristics)
		recalculateHeuristics();

	while (processNode())
	{
		// nothing
	}

	if (m_pEndNode != NULL &&
		(m_pEndNode->m_iPathLength <= m_stepMetric.getMaxPath()))
	{
		return true;
	}
	return false;
}

template<class StepMetric, class Node>
void KmodPathFinder<StepMetric,Node>::resetNodes()
{
	//memset(&node_data[0] 0, sizeof(*node_data) * map_width * map_height); // K-Mod
	if (m_pNodeMap != NULL)
		m_pNodeMap->reset();
	m_openList.clear();
	m_pEndNode = NULL;
}

template<class StepMetric, class Node>
void KmodPathFinder<StepMetric,Node>::addStartNode()
{
	Node& kStartNode = m_pNodeMap->get(m_eStart);
	kStartNode.m_ePlot = m_eStart;
	//pathAdd(NULL, pStartNode, ASNC_INITIALADD, &settings, NULL); // K-Mod
	m_stepMetric.initializePathData(kStartNode);
	kStartNode.m_iKnownCost = 0;
	/*// This means nothing. But maybe one day I'll use it.
	kStartNode.m_eFAStarListType = FASTARLIST_OPEN;
	// This means the node is connected and ready to be used.
	kStartNode.m_bOnStack = true;*/ // (K-Mod)
	// advc: Now handled by OpenList. See also the comment at the m_iState declaration.
	m_openList.open(kStartNode);
}

template<class StepMetric, class Node>
void KmodPathFinder<StepMetric,Node>::recalculateHeuristics()
{
	// Recalculate heuristic cost for all open nodes
	for (OpenList::iterator it = m_openList.begin(); it != m_openList.end(); ++it)
	{
		Node& kNode = **it;
		int iHeuristicCost = m_stepMetric.heuristicCost(
				m_kMap.getPlotByIndex(kNode.m_ePlot), *m_pDest);
		kNode.m_iHeuristicCost = iHeuristicCost;
		kNode.m_iTotalCost = iHeuristicCost + kNode.m_iKnownCost;
	}
}

template<class StepMetric, class Node>
bool KmodPathFinder<StepMetric,Node>::processNode()
{
	OpenList::iterator itBest = m_openList.end();
	{
		int iLowestCost = (m_pEndNode != NULL ? m_pEndNode->m_iKnownCost : MAX_INT);
		for (OpenList::iterator it = m_openList.begin(); it != m_openList.end(); ++it)
		{
			if ((*it)->m_iTotalCost < iLowestCost &&
				(*it)->m_iPathLength <= m_stepMetric.getMaxPath())
			{
				itBest = it;
				iLowestCost = (*it)->m_iTotalCost;
			}
		}
	}

	// If we didn't find a suitable node to process, then quit.
	if (itBest == m_openList.end())
		return false;
	Node& kParent = **itBest;
	/*	Note: Needs to be done before pushing new entries,
		otherwise the iterator will be invalid. */
	m_openList.close(itBest);
	FAssert(&m_pNodeMap->get(kParent.m_ePlot) == &kParent);
	CvPlot const& kParentPlot = m_kMap.getPlotByIndex(kParent.m_ePlot);
	// (advc: This clutter will go away when I cache plot adjacency lists)
	int const iParentX = kParentPlot.getX();
	int const iParentY = kParentPlot.getY();

	// Open a new node for each direction coming off the chosen node
	FOR_EACH_ENUM(Direction)
	{
		CvPlot const* pChildPlot = plotDirection(iParentX, iParentY, eLoopDirection);
		if (pChildPlot == NULL)
			continue;
		PlotNumTypes const eChildPlot = m_kMap.plotNum(*pChildPlot);
		if (kParent.m_pParent != NULL && eChildPlot == kParent.m_pParent->m_ePlot)
			continue; // don't backtrack
		// advc: Moved up; no functional difference.
		if (!m_stepMetric.isValidStep(kParentPlot, *pChildPlot))
			continue; // Can't get to the plot from here
		Node& kChild = m_pNodeMap->get(eChildPlot);
		bool const bNewNode = kChild.isState(PATHNODE_UNINITIALIZED);
		if (bNewNode) // (advc: No point in opening a node more than once)
		{
			// This path to the new node is valid. So we need to fill in the data.
			//pathAdd(parent_node, child_node, ASNC_NEWADD, &settings, NULL); // K-Mod
			kChild.m_ePlot = eChildPlot;
			m_stepMetric.updatePathData(kChild, kParent);
			kChild.m_iKnownCost = MAX_INT;
			kChild.m_iHeuristicCost = m_stepMetric.heuristicCost(
					*pChildPlot, *m_pDest);
			// Total cost will be set when the parent is set
			if (m_stepMetric.canStepThrough(*pChildPlot, kChild))
				m_openList.open(kChild);
			else
			{	// This node is a dead end
				/*	(advc: Which is to say, we can never enter it, not even on a
					later call to generatePath - except if it is the destination;
					that remains to be checked.) */
				kChild.setState(PATHNODE_CLOSED);
			}
		}
		FAssert(kChild.m_ePlot == eChildPlot);

		if (pChildPlot == m_pDest)
		{
			// We've found our destination but still need to finish our calculations
			m_pEndNode = &kChild;
		}
		if (kParent.m_iKnownCost >= kChild.m_iKnownCost)
			continue; // There must already be a faster route to the child

		int const iNewCost = kParent.m_iKnownCost + m_stepMetric.cost(
				//parent_node, child_node, 666, &settings, NULL); // K-Mod
				kParentPlot, *pChildPlot, kParent);
		FAssert(iNewCost > 0);
		if (iNewCost >= kChild.m_iKnownCost)
			continue;
		// New minus old; negative value.
		int const iCostDelta = iNewCost - kChild.m_iKnownCost;

		kChild.m_iKnownCost = iNewCost;
		kChild.m_iTotalCost = kChild.m_iKnownCost + kChild.m_iHeuristicCost;

		// Remove child from the list of the previous parent
		if (kChild.m_pParent != NULL)
		{
			FAssert(!bNewNode);
			Node& kOldParent = *kChild.m_pParent;
			#ifdef FASSERT_ENABLE
			int iTemp = kOldParent.m_iNumChildren;
			#endif
			/*	kOldParent just lost one of its children.
				We have to break the news to them.
				This would easier if we had STL instead of bog arrays.
				[advc: Could use a vector, but since karadoc has already
				worked it out ...]*/
			for (int j = 0; j < kOldParent.m_iNumChildren; j++)
			{
				if (kOldParent.m_apChildren[j] == &kChild)
				{	// Found it
					for (j++; j < kOldParent.m_iNumChildren; j++)
						kOldParent.m_apChildren[j - 1] = kOldParent.m_apChildren[j];
					// Not necessary, but easy enough to keep things neat.
					kOldParent.m_apChildren[j - 1] = 0;
					kOldParent.m_iNumChildren--;
				}
			}
			FAssert(kOldParent.m_iNumChildren == iTemp - 1);
			// Recalculate movement points
			//pathAdd(parent_node, child_node, ASNC_PARENTADD_UP, &settings, NULL);
			m_stepMetric.updatePathData(kChild, kParent);
		}

		// Add child to the list of the new parent
		FAssertBounds(0, NUM_DIRECTION_TYPES, kParent.m_iNumChildren);
		kParent.m_apChildren[kParent.m_iNumChildren] = &kChild;
		kParent.m_iNumChildren++;
		kChild.m_pParent = &kParent;

		// Update the new (reduced) costs for all the grandchildren.
		FAssert(kChild.m_iNumChildren == 0 || !bNewNode);
		forwardPropagate(kChild, iCostDelta);
		FAssert(kChild.m_iKnownCost > kParent.m_iKnownCost);
	}
	return true;
}

template<class StepMetric, class Node>
void KmodPathFinder<StepMetric,Node>::forwardPropagate(Node& kHead, int iCostDelta)
{
	//FAssert(iCostDelta < 0 || kHead.m_iNumChildren == 0);
	/*	Note: There are some legitimate cases in which the iCostDelta can be positive.
		For example, suppose a shorter path is found to the parent plot, but the path
		involves resting on less attractive plots. And suppose the addition moves
		saved by the shorter path are then spent anyway to take the final step
		onto the destination...
		In that case, although the path the parent plot has been upgraded,
		the path to the destination is actually degraded (ie. it has a higher total cost).
		Can't think of a way to solve this problem. (I don't think it's very important.) */

	// Change the known cost of all children by iCostDelta, recursively.
	for (int i = 0; i < kHead.m_iNumChildren; i++)
	{
		Node& kLoopChild = *kHead.m_apChildren[i];
		FAssert(kLoopChild.m_pParent == &kHead);

		// Recalculate movement points
		//int iNewDelta = iCostDelta;
		//pathAdd(head, head->m_apChildren[i], ASNC_PARENTADD_UP, &settings, NULL); // K-Mod
		bool const bChanged = m_stepMetric.updatePathData(kLoopChild, kHead);

		// If the moves don't match, we may need to recalculate the path cost.
		//if (iOldMoves != kLoopChild.m_iData1)
		/*	Strictly, the cost shouldn't depend on our path history,
			but it does - because I wanted to use the path history
			for path symmetry breaking. But anyway, according to the profiler,
			this is only going to cost us about a milisecond per turn. */
		int iStepCost = m_stepMetric.cost(
				//head, head->m_apChildren[i], 666, &settings, NULL // K-Mod
				m_kMap.getPlotByIndex(kHead.m_ePlot),
				m_kMap.getPlotByIndex(kLoopChild.m_ePlot),
				kHead);
		int iNewDelta = kHead.m_iKnownCost + iStepCost - kLoopChild.m_iKnownCost;
		//FAssert(iNewDelta <= 0);

		kLoopChild.m_iKnownCost += iNewDelta;
		kLoopChild.m_iTotalCost += iNewDelta;

		FAssert(kLoopChild.m_iKnownCost > kHead.m_iKnownCost);
		/*	advc (tbd.?): iNewDelta is never 0 here in tests. Often it's -4.
			Perhaps that makes sense as a result of symmetry breaking.
			No need then, though, to check and return bChange in updatePathData. */
		if (bChanged || iNewDelta != 0)
			forwardPropagate(kLoopChild, iNewDelta);
	}
}

template<class StepMetric, class Node>
CvPlot* KmodPathFinder<StepMetric,Node>::getPathFirstPlot() const
{
	if (m_pEndNode == NULL)
	{
		FAssert(m_pEndNode != NULL);
		return NULL;
	}

	Node* pNode = m_pEndNode;

	if (pNode->m_pParent == NULL)
		return &m_kMap.getPlotByIndex(pNode->m_ePlot);

	while (pNode->m_pParent->m_pParent != NULL)
	{
		pNode = pNode->m_pParent;
	}

	return &m_kMap.getPlotByIndex(pNode->m_ePlot);
}

#endif
