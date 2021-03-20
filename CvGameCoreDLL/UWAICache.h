#pragma once

#ifndef UWAI_CACHE_H
#define UWAI_CACHE_H

class UWAICache;
class MilitaryBranch;
class CvCity;
class TeamPathFinders;
class FDataStreamBase;

/*	advc.104: Cached data for UWAI::Player; about the player owning the cache,
	but also (subjective) info about other players. Updated once per player turn,
	for the most part. Written into savegames. Some team-level data
	(for UWAI::Team) is stored too, but only in the team leader's cache
	(or at least it only gets updated there). That data gets moved whenever a
	team's leader changes - a bit messy, but a separate cache for team data
	would come with some red tape.
	Also handles the updating of cached values, i.e. some of UWAI's heuristics
	are part of this class. (I think I'd prefer to move them elsewhere.) */

// Interface of UWAICache::City for use outside of the UWAI component
class UWAICity
{
public:
	inline int getAssetScore() const { return m_iAssetScore; }
	inline bool canReach() const { return (getDistance() >= 0); }
	inline bool canReachByLand() const { return m_bReachByLand; } // from a primary area
	inline bool canReachByLandFromCapital() const { return m_bCapitalArea; }
	/*	-1 if unreachable, 0 for cities of the cache owner's team
		(and never for cities of other teams). */
	inline int getDistance() const { return m_iDistance; }

protected:
	int m_iDistance;
	int m_iAssetScore;
	bool m_bCapitalArea;
	bool m_bReachByLand;

	UWAICity()
	:	m_iDistance(-1), m_iAssetScore(-1), m_bReachByLand(false), m_bCapitalArea(false)
	{}
};


class UWAICache
{
public:

	class City;

	UWAICache();
	~UWAICache();
	/*	Call order during (de-)initialization for CvTeam(AI), CvPlayer(AI):
		+	When Civ 4 is launched, constructors are called.
		+	When Civ 4 is exited, destructors are called.
		+	When starting a new game, init is called.
		+	When returning to the main menu, reset is called.
		+	When saving a game, write is called.
		+	When loading a saved game, read is called.
		read, init and the constructor use reset to clear the data.
		Reusing classes like this is error-prone, and I see no good reason for it,
		but there's (probably) no changing it because the EXE is involved.
		I'll do sth. slightly simpler with UWAICache (read/ write: as above):
		+	The constructor sets the few POD members to a clean state at launch.
		+	The destructor ensures that all memory is deallocated before exiting.
		+	uninit frees memory when the owner is defeated (for performance).
		+	Nothing is done upon returning to the main menu.
		+	The reset function is named "clear" and is also used before cache updates.
		+	init calls clear (and does some other things) when a new game is started. */
	void init(PlayerTypes eOwner);
	void uninit();
	void update();
	void write(FDataStreamBase* pStream) const;
	void read(FDataStreamBase* pStream);
	int numReachableCities(PlayerTypes ePlayer) const
	{
		return m_aiReachableCities.get(ePlayer);
	}
	int numCities() const { return (int)m_cityList.size(); }
	void sortCitiesByAttackPriority();
	inline City& cityAt(int iAt) const
	{
		FAssertBounds(0, m_cityList.size(), iAt);
		return *m_cityList[iAt];
	};
	// (Use CvCity::plotNum to look up a given CvCity)
	City* lookupCity(PlotNumTypes ePlot) const
	{
		std::map<PlotNumTypes,City*>::const_iterator pos = m_cityMap.find(ePlot);
		if (pos == m_cityMap.end())
			return NULL;
		return pos->second;
	}
	static CvCity& cvCityById(PlotNumTypes ePlot);

	// Any trait that grants an unconditional combat bonus via a free promotion
	bool hasOffensiveTrait() const { return m_bOffensiveTrait; }
	// Any trait that boosts city defense via a free promotion
	bool hasDefensiveTrait() const { return m_bDefensiveTrait; }
	bool canScrubFallout() const { return m_bCanScrub; }
	// Units of the cache owner targeting the territory of ePlayer
	int targetMissionCount(PlayerTypes ePlayer) const
	{
		return m_aiTargetMissions.get(ePlayer);
	}
	/*	Long-term military threat by ePlayer between 0 and 1. Can see it as
		the probability of them hurting the cache owner substantially with
		an invasion sometime within the next 50 to 100 turns. */
	scaled threatRating(PlayerTypes ePlayer) const { return m_arThreatRating.get(ePlayer); }
	// If the free civ ePlayer were to capitulate to the cache owner ...
	int vassalTechScore(PlayerTypes ePlayer) const { return m_aiVassalTechScore.get(ePlayer); }
	int vassalResourceScore(PlayerTypes ePlayer) const { return m_aiVassalResourceScore.get(ePlayer); }
	// advc.035:
	int numPlotsLostAtWar(TeamTypes eTeam) const { return m_aiPlotsLostAtWar.get(eTeam); }
	int numAdjacentLandPlots(PlayerTypes ePlayer) const { return m_aiAdjLandPlots.get(ePlayer); }
	scaled relativeNavyPower(PlayerTypes ePlayer) const
	{
		FErrorMsg("Not implemented; work in progress.");
		return m_arRelativeNavyPow.get(ePlayer);
	}
	int pastWarScore(TeamTypes eTeam) const { return m_aiPastWarScore.get(eTeam); }
	// Trade value that has been paid to us for declaring war on eTeam
	int bountyAgainst(TeamTypes eTeam) const { return m_aiBounty.get(eTeam); }
	// Identity of the sponsor who made the above payment
	PlayerTypes sponsorAgainst(TeamTypes eTeam) const { return m_aeSponsorPerTarget.get(eTeam); }
	int warUtilityIgnoringDistraction(TeamTypes eTeam) const
	{
		return leaderCache().m_aiWarUtilityIgnoringDistraction.get(eTeam);
	}
	// Necessary (but not sufficient) condition for agreeing to a joint war
	bool canBeHiredAgainst(TeamTypes eTeam) const
	{
		return leaderCache().m_abCanBeHiredAgainst.get(eTeam);
	}
	void setCanBeHiredAgainst(TeamTypes eTeam, bool b);
	void updateCanBeHiredAgainst(TeamTypes eTeam, int iWarUtility, int iUtilityThresh);
	bool canTrainDeepSeaCargo() const { return m_bHaveDeepSeaTransports; }
	bool canTrainAnyCargo() const { return m_bHaveAnyTransports; }
	bool isFocusOnPeacefulVictory() const { return m_bFocusOnPeacefulVictory; }

	/*	Power values per military branch. The caller must not modify
		the MilitaryBranch data. Should work on copies instead. */
	inline std::vector<MilitaryBranch*> const& getPowerValues() const { return m_militaryPower; }
	// Counts only combatants
	int numNonNavalUnits() const { return m_iNonNavalUnits; }
	// Includes national wonders (which City::updateAssetScore does not count)
	scaled totalAssetScore() const { return m_rTotalAssets; }
	/*	Number of citizens that are angry now and wouldn't be if it weren't for
		the war weariness against eEnemy. */
	scaled angerFromWarWeariness(PlayerTypes eEnemy) const { return m_arWarAnger.get(eEnemy); }
	scaled goldValueOfProduction() const { return m_rGoldPerProduction; }
	void cacheCitiesAfterRead();
	void reportUnitCreated(UnitTypes eUnit);
	void reportUnitDestroyed(UnitTypes eUnit);
	void reportWarEnding(TeamTypes eEnemy,
			CLinkList<TradeData> const* pWeReceive = NULL,
			CLinkList<TradeData> const* pWeGive = NULL);
	void reportCityCreated(CvCity& kCity);
	void reportCityDestroyed(CvCity const& kCity)
	{	// No checks upfront; make sure we're not keeping any dangling pointer.
		remove(kCity);
	}
	/*	Would prefer to pass a CvDeal instance, but no suitable one is available
		at the call location */
	void reportSponsoredWar(CLinkList<TradeData> const& kWeReceive,
			PlayerTypes eSponsor, TeamTypes eTarget);
	bool isReadyToCapitulate(TeamTypes eMaster) const;
	void setReadyToCapitulate(TeamTypes eMaster, bool b);
	 // When forming a Permanent Alliance
	void addTeam(PlayerTypes eOtherLeader);
	// Moves data that is stored only at the team leader
	void onTeamLeaderChanged(PlayerTypes formerLeaderId);
	/*  public b/c this needs to be done ahead of the normal update when a
		colony is created (bootstrapping problem) */
	void updateTypicalUnits();

private:
	// bBeforeUpdate: Only clear data that is recomputed by the update function
	void clear(bool bBeforeUpdate = false);
	void createMilitaryBranches();
	void deleteMilitaryBranches();
	void deleteUWAICities();
	void updateCities(TeamTypes eTeam, TeamPathFinders* pPathFinders);
	void add(CvCity& kCity);
	void add(City& kCacheCity);
	void remove(CvCity const& kCity);
	TeamPathFinders* createTeamPathFinders() const;
	static void deleteTeamPathFinders(TeamPathFinders& kPathFinders);
	void resetTeamPathFinders(TeamPathFinders& kPathFinders, TeamTypes eWarTarget) const;
	void updateTraits();
	void updateTargetMissionCounts();
	void updateThreatRatings();
	void updateVassalScores();
	void updateAdjacentLand();
	void updateLostTilesAtWar(); // advc.035
	void updateRelativeNavyPower();
	void updateTargetMissionCount(PlayerTypes ePlayer);
	scaled calculateThreatRating(PlayerTypes eRival) const;
	scaled teamThreat(TeamTypes eRival) const;
	scaled longTermPower(TeamTypes eTeam, bool bDefensive = false) const;
	void updateVassalScore(PlayerTypes eRival);
	void reportUnit(UnitTypes eUnit, int iChange);
	void updateTotalAssetScore();
	void updateWarAnger();
	void updateGoldPerProduction();
	scaled goldPerProdBuildings();
	scaled goldPerProdSites();
	scaled goldPerProdVictory();
	void updateWarUtility();
	void updateWarUtilityIgnDistraction(TeamTypes eTarget);
	void updateCanScrub();
	void updateTransports();
	bool calculateFocusOnPeacefulVictory();
	int shipSpeed() const;
	// To supply team-on-team data
	UWAICache const& leaderCache() const;
	UWAICache& leaderCache();

	PlayerTypes m_eOwner;
	std::vector<City*> m_cityList;
	// I've tried stdext::hash_map for both of these. That was a little bit slower.
	std::map<PlotNumTypes,City*> m_cityMap;
	std::vector<MilitaryBranch*> m_militaryPower;

	int m_iNonNavalUnits;
	scaled m_rTotalAssets;
	scaled m_rGoldPerProduction;
	bool m_bOffensiveTrait, m_bDefensiveTrait;
	bool m_bCanScrub;
	bool m_bHaveDeepSeaTransports, m_bHaveAnyTransports;
	bool m_bFocusOnPeacefulVictory;
	std::set<TeamTypes> m_readyToCapitulateTo;
  
	CivPlayerMap<scaled> m_arWarAnger;
	CivPlayerMap<int> m_aiReachableCities;
	CivPlayerMap<int> m_aiTargetMissions;
	CivPlayerMap<scaled> m_arThreatRating;
	CivPlayerMap<int> m_aiVassalTechScore;
	CivPlayerMap<int> m_aiVassalResourceScore;
	CivPlayerMap<bool> m_abPlayerLocated;
	// (CvTeamAI::AI_calculateAdjacentLandPlots is too slow and per team)
	CivPlayerMap<int> m_aiAdjLandPlots;
	CivPlayerMap<scaled> m_arRelativeNavyPow;

	CivTeamMap<int> m_aiPlotsLostAtWar; // advc.035
	CivTeamMap<int> m_aiPastWarScore;
	CivTeamMap<int> m_aiBounty; // Maps targets to sponsorship values
	CivTeamMap<PlayerTypes,NO_PLAYER> m_aeSponsorPerTarget; // Maps targets to sponsors
	CivTeamMap<int,MIN_INT> m_aiWarUtilityIgnoringDistraction;
	CivTeamMap<bool,true> m_abCanBeHiredAgainst; // Are we willing to be hired?

public:
	/* Information to be cached about a CvCity and scoring functions useful
	   for computing war utility. */
	class City : public UWAICity
	{
	public:
		City(PlayerTypes eCacheOwner, CvCity& kCity, TeamPathFinders* pPathFinders);
		// for reading from savegame:
		City() : m_pCity(NULL), m_iTargetValue(-1), m_ePlot(NO_PLOT_NUM) {}
		inline bool isOwnTeamCity() const { return (m_iDistance == 0); }
		inline int getTargetValue() const { return m_iTargetValue; }
		/* A mix of target value and distance. Target value alone would
		   ignore opportunistic attacks. */
		scaled attackPriority() const;
		inline CvCity& city() const { return *m_pCity; }
		inline PlotNumTypes getID() const { return m_ePlot; }
		void cacheCvCity();
		void write(FDataStreamBase* pStream) const;
		void read(FDataStreamBase* pStream);

	private:
		void updateDistance(TeamPathFinders* pPathFinders, PlayerTypes eCacheOwner);
		void updateAssetScore(PlayerTypes eCacheOwner);

		int m_iTargetValue;
		PlotNumTypes m_ePlot;
		CvCity* m_pCity; // Retrieving this based on m_ePlot wastes too much time
	};
};

#endif
