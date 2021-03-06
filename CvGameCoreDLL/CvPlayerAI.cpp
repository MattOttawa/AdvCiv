// playerAI.cpp

#include "CvGameCoreDLL.h"
#include "CvPlayerAI.h"
#include "CitySiteEvaluator.h"
#include "CoreAI.h"
#include "CvCityAI.h"
#include "CvUnitAI.h"
#include "CvSelectionGroupAI.h"
#include "CvDeal.h"
#include "UWAIAgent.h" // advc.104
#include "RiseFall.h" // advc.705
#include "PlotRange.h"
#include "CvArea.h"
#include "CvDiploParameters.h"
#include "CvInfo_City.h"
#include "CvInfo_Terrain.h"
#include "CvInfo_GameOption.h"
#include "CvInfo_Civics.h"
#include "BBAILog.h"
#include "CvPopupInfo.h"
#include "FAStarNode.h"
#include <iterator> // advc.036
#include <queue> // k146

//#define GREATER_FOUND_RANGE			(5)
#define CIVIC_CHANGE_DELAY			(20) // was 25
#define RELIGION_CHANGE_DELAY		(15)

// statics ... (advc.003u: Mostly moved to CvPlayer)

bool CvPlayerAI::areStaticsInitialized()
{
	return CvPlayer::areStaticsInitialized(); // advc.003u
}


DllExport CvPlayerAI& CvPlayerAI::getPlayerNonInl(PlayerTypes ePlayer)
{
	return AI_getPlayer(ePlayer);
}


CvPlayerAI::CvPlayerAI(/* advc.003u: */ PlayerTypes eID) : CvPlayer(eID)
{
	m_aiNumTrainAIUnits = new int[NUM_UNITAI_TYPES];
	m_aiNumAIUnits = new int[NUM_UNITAI_TYPES];
	m_aiSameReligionCounter = new int[MAX_PLAYERS];
	m_aiDifferentReligionCounter = new int[MAX_PLAYERS];
	m_aiFavoriteCivicCounter = new int[MAX_PLAYERS];
	m_aiBonusTradeCounter = new int[MAX_PLAYERS];
	m_aiPeacetimeTradeValue = new int[MAX_PLAYERS];
	m_aiPeacetimeGrantValue = new int[MAX_PLAYERS];
	m_aiGoldTradedTo = new int[MAX_PLAYERS];
	m_aiAttitudeExtra = new int[MAX_PLAYERS];

	m_abFirstContact = new bool[MAX_PLAYERS];

	m_aaiContactTimer = new int*[MAX_PLAYERS];
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		m_aaiContactTimer[i] = new int[NUM_CONTACT_TYPES];
	}

	m_aaiMemoryCount = new int*[MAX_PLAYERS];
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		m_aaiMemoryCount[i] = new int[NUM_MEMORY_TYPES];
	}

	m_aiAverageYieldMultiplier = new int[NUM_YIELD_TYPES];
	m_aiAverageCommerceMultiplier = new int[NUM_COMMERCE_TYPES];
	m_aiAverageCommerceExchange = new int[NUM_COMMERCE_TYPES];

	m_pUWAI = new UWAI::Civ(); // advc.104
	m_aiBonusValue = NULL;
	m_aiBonusValueTrade = NULL; // advc.036
	m_aiUnitClassWeights = NULL;
	m_aiUnitCombatWeights = NULL;
	//m_aiCloseBordersAttitude = new int[MAX_PLAYERS];
	m_aiCloseBordersAttitude.resize(MAX_PLAYERS); // K-Mod

	m_aiAttitude.resize(MAX_PLAYERS); // K-Mod

	AI_reset(true);
}


CvPlayerAI::~CvPlayerAI()
{
	AI_uninit();

	SAFE_DELETE_ARRAY(m_aiNumTrainAIUnits);
	SAFE_DELETE_ARRAY(m_aiNumAIUnits);
	SAFE_DELETE_ARRAY(m_aiSameReligionCounter);
	SAFE_DELETE_ARRAY(m_aiDifferentReligionCounter);
	SAFE_DELETE_ARRAY(m_aiFavoriteCivicCounter);
	SAFE_DELETE_ARRAY(m_aiBonusTradeCounter);
	SAFE_DELETE_ARRAY(m_aiPeacetimeTradeValue);
	SAFE_DELETE_ARRAY(m_aiPeacetimeGrantValue);
	SAFE_DELETE_ARRAY(m_aiGoldTradedTo);
	SAFE_DELETE_ARRAY(m_aiAttitudeExtra);
	SAFE_DELETE_ARRAY(m_abFirstContact);
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		SAFE_DELETE_ARRAY(m_aaiContactTimer[i]);
	}
	SAFE_DELETE_ARRAY(m_aaiContactTimer);

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		SAFE_DELETE_ARRAY(m_aaiMemoryCount[i]);
	}
	SAFE_DELETE_ARRAY(m_aaiMemoryCount);

	SAFE_DELETE_ARRAY(m_aiAverageYieldMultiplier);
	SAFE_DELETE_ARRAY(m_aiAverageCommerceMultiplier);
	SAFE_DELETE_ARRAY(m_aiAverageCommerceExchange);
	//SAFE_DELETE_ARRAY(m_aiCloseBordersAttitude); // disabled by K-Mod
	SAFE_DELETE(m_pUWAI); // advc.104
}


void CvPlayerAI::AI_init()
{
	AI_reset(false); // Reset serialized data

	// advc: Caller should guarantee this
	FAssert(GC.getInitCore().getSlotStatus(getID()) == SS_TAKEN || GC.getInitCore().getSlotStatus(getID()) == SS_COMPUTER);

	//--------------------------------
	// Init other game data
	// (advc.104: Personality weight calculation now triggered by CvPlayer::setPersonalityType)
	//AI_setCivicTimer(((getMaxAnarchyTurns() == 0) ? (GC.getDefineINT("MIN_REVOLUTION_TURNS") * 2) : CIVIC_CHANGE_DELAY) / 2);  // This was commented out by the BtS expansion
	AI_setReligionTimer(1);
	AI_setCivicTimer((getMaxAnarchyTurns() == 0) ? 1 : 2);
	AI_initStrategyRand(); // K-Mod
	AI_updateCacheData(); // K-Mod
	// <advc.104>
	if(isEverAlive() && !isBarbarian() && !isMinorCiv())
		m_pUWAI->init(getID()); // </advc.104>
}

// advc.104: Body cut from AI_init
void CvPlayerAI::AI_updatePersonality()
{
	FAssert(getPersonalityType() != NO_LEADER);
	CvLeaderHeadInfo const& kPersonality = GC.getInfo(getPersonalityType());
	AI_setPeaceWeight(kPersonality.getBasePeaceWeight() +
			GC.getGame().getSorenRandNum(kPersonality.getPeaceWeightRand(), "AI Peace Weight"));
	AI_setEspionageWeight(kPersonality.getEspionageWeight()
			// K-Mod. (I've changed the meaning of this value)
			*GC.getInfo(COMMERCE_ESPIONAGE).getAIWeightPercent() / 100);
}


void CvPlayerAI::AI_uninit()
{
	SAFE_DELETE_ARRAY(m_aiBonusValue);
	SAFE_DELETE_ARRAY(m_aiBonusValueTrade); // advc.036
	SAFE_DELETE_ARRAY(m_aiUnitClassWeights);
	SAFE_DELETE_ARRAY(m_aiUnitCombatWeights);
}


void CvPlayerAI::AI_reset(bool bConstructor)
{
	int iI;

	AI_uninit();

	m_iPeaceWeight = 0;
	m_iEspionageWeight = 0;
	m_iAttackOddsChange = 0;
	m_iCivicTimer = 0;
	m_iReligionTimer = 0;
	m_iExtraGoldTarget = 0;
	m_iCityTargetTimer = 0; // K-Mod

	// CHANGE_PLAYER, 06/08/09, jdog5000: START
	if (bConstructor || getNumUnits() == 0)
	{
		for (iI = 0; iI < NUM_UNITAI_TYPES; iI++)
		{
			m_aiNumTrainAIUnits[iI] = 0;
			m_aiNumAIUnits[iI] = 0;
		}
	} // CHANGE_PLAYER: END

	for (iI = 0; iI < MAX_PLAYERS; iI++)
	{
		m_aiSameReligionCounter[iI] = 0;
		m_aiDifferentReligionCounter[iI] = 0;
		m_aiFavoriteCivicCounter[iI] = 0;
		m_aiBonusTradeCounter[iI] = 0;
		m_aiPeacetimeTradeValue[iI] = 0;
		m_aiPeacetimeGrantValue[iI] = 0;
		m_aiGoldTradedTo[iI] = 0;
		m_aiAttitudeExtra[iI] = 0;
		// <advc.079>
		if(iI < MAX_CIV_PLAYERS)
		{
			m_aeLastBrag[iI] = NO_UNIT;
			m_aeLastWarn[iI] = NO_TEAM; // </advc.079>
			m_abTheyFarAhead[iI] = m_abTheyBarelyAhead[iI] = false; // advc.130c
		}
		m_abFirstContact[iI] = false;
		for (int iJ = 0; iJ < NUM_CONTACT_TYPES; iJ++)
		{
			m_aaiContactTimer[iI][iJ] = 0;
		}
		for (int iJ = 0; iJ < NUM_MEMORY_TYPES; iJ++)
		{
			m_aaiMemoryCount[iI][iJ] = 0;
		}

		if (!bConstructor && getID() != NO_PLAYER)
		{
			PlayerTypes eLoopPlayer = (PlayerTypes) iI;
			CvPlayerAI& kLoopPlayer = GET_PLAYER(eLoopPlayer);
			kLoopPlayer.m_aiSameReligionCounter[getID()] = 0;
			kLoopPlayer.m_aiDifferentReligionCounter[getID()] = 0;
			kLoopPlayer.m_aiFavoriteCivicCounter[getID()] = 0;
			kLoopPlayer.m_aiBonusTradeCounter[getID()] = 0;
			kLoopPlayer.m_aiPeacetimeTradeValue[getID()] = 0;
			kLoopPlayer.m_aiPeacetimeGrantValue[getID()] = 0;
			kLoopPlayer.m_aiGoldTradedTo[getID()] = 0;
			kLoopPlayer.m_aiAttitudeExtra[getID()] = 0;
			// <advc.079>
			if(getID() < MAX_CIV_PLAYERS)
			{
				kLoopPlayer.m_aeLastBrag[getID()] = NO_UNIT;
				kLoopPlayer.m_aeLastWarn[getID()] = NO_TEAM; // </advc.079>
				// advc.130c:
				kLoopPlayer.m_abTheyFarAhead[getID()] = kLoopPlayer.m_abTheyBarelyAhead[getID()] = false;
			}
			kLoopPlayer.m_abFirstContact[getID()] = false;
			for (int iJ = 0; iJ < NUM_CONTACT_TYPES; iJ++)
			{
				kLoopPlayer.m_aaiContactTimer[getID()][iJ] = 0;
			}
			for (int iJ = 0; iJ < NUM_MEMORY_TYPES; iJ++)
			{
				kLoopPlayer.m_aaiMemoryCount[getID()][iJ] = 0;
			}
		}
	}

	for (iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		m_aiAverageYieldMultiplier[iI] = 0;
	}
	for (iI = 0; iI< NUM_COMMERCE_TYPES; iI++)
	{
		m_aiAverageCommerceMultiplier[iI] = 0;
		m_aiAverageCommerceExchange[iI] = 0;
	}
	m_iAverageGreatPeopleMultiplier = 0;
	m_iAverageCulturePressure = 0;
	//m_iAveragesCacheTurn = -1;

	m_eStrategyHash = NO_AI_STRATEGY;
	//m_iStrategyHashCacheTurn = -1;
	// BBAI
	m_iStrategyRand = MAX_UNSIGNED_INT; // was 0 (K-Mod)
	m_eVictoryStageHash = NO_AI_VICTORY_STAGE;
	//m_iVictoryStrategyHashCacheTurn = -1;
	// BBAI end

	m_bWasFinancialTrouble = false;
	m_iTurnLastProductionDirty = -1;

	//m_iUpgradeUnitsCacheTurn = -1;
	//m_iUpgradeUnitsCachedExpThreshold = 0;
	m_iUpgradeUnitsCachedGold = 0;
	m_iAvailableIncome = 0; // K-Mod

	m_aiAICitySites.clear();

	FAssert(m_aiBonusValue == NULL);
	m_aiBonusValue = new int[GC.getNumBonusInfos()];
	m_aiBonusValueTrade = new int[GC.getNumBonusInfos()]; // advc.036
	for (iI = 0; iI < GC.getNumBonusInfos(); iI++)
	{
		m_aiBonusValue[iI] = -1;
		m_aiBonusValueTrade[iI] = -1; // advc.036
	}

	FAssert(m_aiUnitClassWeights == NULL);
	m_aiUnitClassWeights = new int[GC.getNumUnitClassInfos()];
	for (iI = 0; iI < GC.getNumUnitClassInfos(); iI++)
	{
		m_aiUnitClassWeights[iI] = 0;
	}

	FAssert(m_aiUnitCombatWeights == NULL);
	m_aiUnitCombatWeights = new int[GC.getNumUnitCombatInfos()];
	for (iI = 0; iI < GC.getNumUnitCombatInfos(); iI++)
	{
		m_aiUnitCombatWeights[iI] = 0;
	}

	/*for (iI = 0; iI < MAX_PLAYERS; iI++) {
		m_aiCloseBordersAttitude[iI] = 0;
		if (!bConstructor && getID() != NO_PLAYER)
			GET_PLAYER((PlayerTypes) iI).m_aiCloseBordersAttitude[getID()] = 0;
	}*/ // BtS
	// K-Mod
	m_aiCloseBordersAttitude.assign(MAX_PLAYERS, 0);
	m_aiAttitude.assign(MAX_PLAYERS, 0);
	// K-Mod end
	m_bDangerFromSubmarines = false; // advc.651
}


int CvPlayerAI::AI_getFlavorValue(FlavorTypes eFlavor) const
{
	if(isHuman())
		return 0; // K-Mod
	//return GC.getInfo(getPersonalityType()).getFlavorValue(eFlavor);
	// <advc.109> Replacing the above
	int r = GC.getInfo(getPersonalityType()).getFlavorValue(eFlavor);
	if(GET_TEAM(getTeam()).AI_isLonely())
	{
		// Lonely civs need to focus on science so that they can meet partners
		if(eFlavor == FLAVOR_SCIENCE)
			r += 3;
		else if(r > 1)
			r /= 2;
	}
	return r; // </advc.109>
}

// K-Mod
/*	advc (note): Now called on all active players after loading a savegame;
	so that the data doesn't necessarily have to be serialized. */
void CvPlayerAI::AI_updateCacheData()
{
	// AI_updateCloseBorderAttitude();
	// AI_updateAttitude(); // attitude of this player is relevant to other players too, so this needs to be done elsewhere.
	AI_updateNeededExplorers(); // advc.opt
	AI_calculateAverages();
	AI_updateVictoryStageHash();
	//if (!isHuman()) // advc.104: Human victory strategies are interesting to know for UWAI.
	{
		AI_updateStrategyHash();
		//AI_updateGoldToUpgradeAllUnits();
	}
	// note. total upgrade gold is currently used in AI_hurry, which is used by production-automated.
	// Therefore, we need to get total upgrade gold for human players as well as AI players.
	AI_updateGoldToUpgradeAllUnits();
	AI_updateAvailableIncome(); // K-Mod
	AI_updateDangerFromSubmarines(); // advc.651
	// <advc.139>
	if(isBarbarian())
		return;
	std::vector<double> cityValues;
	FOR_EACH_CITYAI(c, *this)
		cityValues.push_back(c->AI_cityValue());
	int i = 0;
	FOR_EACH_CITYAI_VAR(c, *this)
	{
		c->AI_setCityValPercent(::round(100 *
				(1 - ::percentileRank(cityValues, cityValues[i]))));
		c->AI_updateSafety();
		i++;
	} // </advc.139>
} // K-Mod end


void CvPlayerAI::AI_doTurnPre()
{
	PROFILE_FUNC();

	FAssertMsg(getPersonalityType() != NO_LEADER, "getPersonalityType() is not expected to be equal with NO_LEADER");
	FAssertMsg(getLeaderType() != NO_LEADER, "getLeaderType() is not expected to be equal with NO_LEADER");
	FAssertMsg(getCivilizationType() != NO_CIVILIZATION, "getCivilizationType() is not expected to be equal with NO_CIVILIZATION");
	//AI_invalidateCloseBordersAttitude();

	AI_doCounter();

	/*  K-Mod note: attitude cache is not refreshed here because there are still some attitude-affecting changes to come, in CvTeamAI::AI_doCounter
		for efficiency, I've put AI_updateCloseBorderAttitude in CvTeam::doTurn rather than here. */

	AI_updateBonusValue();
	// K-Mod. Update commerce weight before calculating great person weight
	AI_updateCommerceWeights(); // (perhaps this should be done again after AI_doCommerce, or when sliders change?)
	// GP weights can take a little bit of time, so lets only do it once every 3 turns.
	if ((GC.getGame().getGameTurn() + AI_getStrategyRand(0))%3 == 0)
		AI_updateGreatPersonWeights();
	// K-Mod end

	AI_doEnemyUnitData();

	AI_ClearConstructionValueCache(); // K-Mod

	if (isHuman())
		return;

	AI_doResearch();
	AI_doCommerce();
	AI_doMilitary();
	AI_doCivics();
	AI_doReligion();
	AI_doCheckFinancialTrouble();
}


void CvPlayerAI::AI_doTurnPost()
{
	PROFILE_FUNC();

	if (isHuman() || isMinorCiv())
		return;

	if (isBarbarian())
	{
		AI_foldDeals(); // advc.036
		return;
	}

	AI_doDiplo();
	/*  UNOFFICIAL_PATCH, Bugfix, 06/16/09, jdog5000:
		Moved per alexman's suggestion */
	//AI_doSplit();

	for (int i = 0; i < GC.getNumVictoryInfos(); ++i)
		AI_launch((VictoryTypes)i);
}


void CvPlayerAI::AI_doTurnUnitsPre()
{
	PROFILE_FUNC();

	AI_updateFoundValues();

	if (AI_getCityTargetTimer() == 0 && GC.getGame().getSorenRandNum(8, "AI Update Area Targets") == 0) // K-Mod added timer check.
	{
		AI_updateAreaTargets();
	} // <advc.102>
	FOR_EACH_GROUP_VAR(gr, *this)
	{
		if(gr->getNumUnits() > 0)
			gr->setInitiallyVisible(gr->getPlot().isVisibleToWatchingHuman());
	} // </advc.102>

	if (isHuman() || isBarbarian())
		return;

	// Uncommented by K-Mod, and added warplan condition.
	if (AI_isDoStrategy(AI_STRATEGY_CRUSH) /* advc.105: */ && AI_isFocusWar())
			//GET_TEAM(getTeam()).getAnyWarPlanCount(true) > 0)
		AI_convertUnitAITypesForCrush();
}


void CvPlayerAI::AI_doTurnUnitsPost()
{
	PROFILE_FUNC();

	if (!isHuman() || isOption(PLAYEROPTION_AUTO_PROMOTION))
	{
		FOR_EACH_UNITAI_VAR(pLoopUnit, *this)
			pLoopUnit->AI_promote();
	}

	if (isHuman() ||
		/*  advc.300: Disbanding now handled in CvGame::killBarb.
			No more Barbarian upgrades. */
		isBarbarian())
	{
		return;
	}
	// BETTER_BTS_AI_MOD, Gold AI, 02/24/10, jdog5000: START
	//bool bAnyWar = (GET_TEAM(getTeam()).getAnyWarPlanCount(true) > 0);
	int iStartingGold = getGold();
	/* BBAI code
	int iTargetGold = AI_goldTarget();
	int iUpgradeBudget = (AI_getGoldToUpgradeAllUnits() / (bAnyWar ? 1 : 2));
	iUpgradeBudget = std::min(iUpgradeBudget, iStartingGold - ((iTargetGold > iUpgradeBudget) ? (iTargetGold - iUpgradeBudget) : iStartingGold/2)); */
	// K-Mod. Note: AI_getGoldToUpgradeAllUnits() is actually one of the components of AI_goldTarget()
	int iCostPerMil = AI_unitCostPerMil(); // used for scrap decisions.
	int iUpgradeBudget = 0;
	//if(GET_TEAM(getTeam()).getAnyWarPlanCount(true) > 0)
	if (AI_isFocusWar()) // advc.105
	{
		// when at war, upgrades get priority
		iUpgradeBudget = std::min(getGold(), AI_goldTarget(true));
	}
	else
	{
		int iMaxBudget = AI_goldTarget(true);
		iUpgradeBudget = std::min(iMaxBudget, getGold() * iMaxBudget / std::max(1, AI_goldTarget(false)));
	}
	// K-Mod end

	// Always willing to upgrade 1 unit if we have the money
	iUpgradeBudget = std::max(iUpgradeBudget,1);
	// BETTER_BTS_AI_MOD: END

	CvPlot const* pLastUpgradePlot = NULL;
	for (int iPass = 0; iPass < 4; iPass++)
	{
		FOR_EACH_UNITAI_VAR(pLoopUnit, *this)
		{
			bool bNoDisband = false;
			bool bValid = false;

			switch (iPass)
			{
			case 0:
				// BBAI note:  Effectively only for galleys, triremes, and ironclads ... unit types which are limited in
				// what terrain they can operate in
				if (AI_unitImpassableCount(pLoopUnit->getUnitType()) > 0)
					bValid = true;
				break;
			case 1:
			{
				CvPlot const& kUnitPlot = pLoopUnit->getPlot();
				if (kUnitPlot.isCity())
				{
					if (kUnitPlot.getBestDefender(getID()) == pLoopUnit)
					{
						bNoDisband = true;
						bValid = true;
						pLastUpgradePlot = &kUnitPlot;
					}
					// <advc.650>
					if(GC.getInfo(pLoopUnit->getUnitType()).getNukeRange() >= 0)
						bValid = false; // </advc.650>
					// try to upgrade units which are in danger... but don't get obsessed
					if (!bValid && pLastUpgradePlot != &kUnitPlot &&
						AI_isAnyPlotDanger(kUnitPlot, 1, false))
					{
						bNoDisband = true;
						bValid = true;
						pLastUpgradePlot = &kUnitPlot;
					}
				}
				break;
			}
			case 2:
				/*if (pLoopUnit->cargoSpace() > 0)
					bValid = true;*/ // BtS
				// Only normal transports
				if (pLoopUnit->cargoSpace() > 0 && pLoopUnit->specialCargo() == NO_SPECIALUNIT)
				{
					bValid = iStartingGold - getGold() < iUpgradeBudget;
				}
				// Also upgrade escort ships
				if (pLoopUnit->AI_getUnitAIType() == UNITAI_ESCORT_SEA)
				{
					bValid = iStartingGold - getGold() < iUpgradeBudget;
				}

				break;
			case 3:
				//bValid = true; // BtS
				bValid = iStartingGold - getGold() < iUpgradeBudget;
				break;
			default:
				FAssert(false);
				break;
			}

			if (!bValid)
				continue; // advc

			bool bKilled = false;
			if (!bNoDisband)
			{
				//if (pLoopUnit->canFight()) // BtS
				if (pLoopUnit->getUnitCombatType() != NO_UNITCOMBAT) // K-Mod - bug fix for the rare case of a barb city spawning on top of an animal
				{
					int iExp = pLoopUnit->getExperience();
					CvCityAI const* pPlotCity = pLoopUnit->getPlot().AI_getPlotCity();
					if (pPlotCity != NULL && pPlotCity->getOwner() == getID())
					{
						int iCityExp = 0;
						iCityExp += pPlotCity->getFreeExperience();
						iCityExp += pPlotCity->getDomainFreeExperience(pLoopUnit->getDomainType());
						iCityExp += pPlotCity->getUnitCombatFreeExperience(pLoopUnit->getUnitCombatType());
						if (iCityExp > 0)
						{
							/*if ((iExp == 0) || (iExp < (iCityExp + 1) / 2)) {
								if ((pLoopUnit->getDomainType() != DOMAIN_LAND) || pLoopUnit->getPlot().plotCount(PUF_isMilitaryHappiness, -1, -1, getID()) > 1) {
									if ((calculateUnitCost() > 0) && (AI_getPlotDanger( pLoopUnit->plot(), 2, false) == 0)) {
										pLoopUnit->kill(false);
										bKilled = true;
										pLastUpgradePlot = NULL;
									}
								}
							}*/
							// K-Mod
							if (iExp < iCityExp && GC.getGame().getGameTurn() - pLoopUnit->getGameTurnCreated() > 8)
							{
								int iDefenders = pLoopUnit->getPlot().plotCount(PUF_canDefendGroupHead, -1, -1,
										getID(), NO_TEAM, PUF_isCityAIType);
								if (iDefenders > pPlotCity->AI_minDefenders() &&
									!AI_isAnyPlotDanger(*pLoopUnit->plot(), 2, false))
								{
									if (iCostPerMil > AI_maxUnitCostPerMil(pLoopUnit->area(), 100) +
										2*iExp + 4*std::max(0, pPlotCity->AI_neededDefenders() - iDefenders))
									{
										if (gUnitLogLevel > 2) logBBAI("    %S scraps %S, with %d exp, and %d / %d spending.", getCivilizationDescription(0), pLoopUnit->getName(0).GetCString(), iExp, iCostPerMil, AI_maxUnitCostPerMil(pLoopUnit->area(), 100));
										pLoopUnit->scrap();
										pLoopUnit->doDelayedDeath(); // I could have just done kill(), but who knows what extra work scrap() wants to do for us?
										bKilled = true;
										pLastUpgradePlot = NULL;
										iCostPerMil = AI_unitCostPerMil(); // recalculate
									}
								}
							}
							// K-Mod end
						}
					}
				}
			}
			if (!bKilled)
			{
				pLoopUnit->AI_upgrade(); // CAN DELETE UNIT!!!
			}
		}
	}

	/*if (isBarbarian()) // advc.300 (moved up)
		return;*/

	// BETTER_BTS_AI_MOD, AI Logging, 02/24/10, jdog5000
	if (gPlayerLogLevel > 2 && iStartingGold - getGold() > 0)
		logBBAI("    %S spends %d on unit upgrades out of budget of %d, %d gold remaining", getCivilizationDescription(0), iStartingGold - getGold(), iUpgradeBudget, getGold());

	// UF bugfix: AI_doSplit moved here per alexman's suggestion
	AI_doSplit();
	// K-Mod note: the reason for moving it here is that player turn ordering can get messed up if a
	// new player is created, recycling an old player number, while no one else's turn is 'active'.
	// I can't help but think there is a more natural solution to this problem...
}

// advc (note): UWAI bypasses this function
void CvPlayerAI::AI_doPeace()  // advc: refactored
{
	PROFILE_FUNC();

	FAssert(!isHuman());
	FAssert(isMajorCiv());

	bool abContacted[MAX_TEAMS] = { false };
	for (PlayerIter<FREE_MAJOR_CIV,ENEMY_OF> it(getTeam()); it.hasNext(); ++it)
	{
		CvPlayerAI const& kTarget = *it;
		PlayerTypes const eTarget = kTarget.getID();
		if(AI_getContactTimer(eTarget, CONTACT_PEACE_TREATY) > 0)
			continue;
		if(kTarget.isHuman() && abContacted[kTarget.getTeam()])
			continue;
		if(!AI_willOfferPeace(eTarget)) // advc: Moved checks into new function
			continue;

		if(canTradeItem(eTarget, TradeData(TRADE_SURRENDER), /* bTestDenial=*/true))
		{
			// advc.104h: Moved into new function
			AI_offerCapitulation(eTarget);
			return;
		}
		// advc (comment): Peace offered with a 5-10% probability
		if(GC.getGame().getSorenRandNum(GC.getInfo(
			getPersonalityType()).getContactRand(CONTACT_PEACE_TREATY),
			"AI Diplo Peace Treaty") != 0)
		{
			continue;
		}
		TradeData item(TRADE_PEACE_TREATY);
		// advc: Replacing two canTradeItem calls
		if(getTradeDenial(eTarget, item) != NO_DENIAL ||
			kTarget.getTradeDenial(getID(), item) != NO_DENIAL)
		{
			continue;
		}
		int iOurValue = GET_TEAM(getTeam()).AI_endWarVal(kTarget.getTeam());
		int iTheirValue = GET_TEAM(eTarget).AI_endWarVal(getTeam());
		// <advc.134a> Human discount
		if(kTarget.isHuman())
		{
			double const discountFactor = 1.2;
			if(iOurValue > 0)
				iOurValue = ::round(iOurValue * discountFactor);
			else iOurValue = ::round(iOurValue / discountFactor);
		} // </advc.134a>
		// advc.104h:
		abContacted[kTarget.getTeam()] = AI_negotiatePeace(eTarget, iTheirValue, iOurValue);
	}
}

// <advc.134a>
bool CvPlayerAI::AI_upholdPeaceOffer(PlayerTypes eHuman, CvDiploParameters const& kOffer) const
{
	int iOurBenefit = AI_dealVal(eHuman, kOffer.getTheirOfferList());
	int iTheirBenefit = GET_PLAYER(eHuman).AI_dealVal(getID(), kOffer.getOurOfferList());
	if(getUWAI.isEnabled())
	{	/*  Need some padding b/c UWAI::Team::endWarVal is always 0
			for at least one side. */
		int iPadding = ::round(uwai().utilityToTradeVal(5));
		iOurBenefit += iPadding;
		iTheirBenefit += iPadding;
	}
	// Neither uphold highly generous offers, nor terrible ones.
	return (2 * iOurBenefit >= iTheirBenefit && 2 * iTheirBenefit > iOurBenefit);
} // </advc.134a>

/*  <advc.104h> Cut and pasted (and refactored) from doPeace b/c UWAI needs
	this subroutine. While decisions of war and peace are made by teams, an
	individual member of the team has to pay for peace (or receive payment);
	therefore not a CvTeamAI function. */
bool CvPlayerAI::AI_negotiatePeace(PlayerTypes eOther, int iTheirBenefit, int iOurBenefit)
{
	FAssert(!isHuman());
	CvPlayerAI& kOther = GET_PLAYER(eOther);
	TechTypes eBestReceiveTech = NO_TECH;
	TechTypes eBestGiveTech = NO_TECH;
	int iReceiveGold = 0;
	int iGiveGold = 0;
	CvCity const* pBestReceiveCity = NULL;
	CvCity const* pBestGiveCity = NULL;

	if(iTheirBenefit > iOurBenefit) // They need to give us sth.
	{
		iOurBenefit += AI_negotiatePeace(getID(), eOther, iTheirBenefit - iOurBenefit,
				&iReceiveGold, &eBestReceiveTech, &pBestReceiveCity);
	}
	else if(iOurBenefit > iTheirBenefit) // We need to give them sth.
	{
		iTheirBenefit += AI_negotiatePeace(eOther, getID(), iOurBenefit - iTheirBenefit,
				&iGiveGold, &eBestGiveTech, &pBestGiveCity);
	}
	/*  K-Mod: "ratio of endWar values has to be somewhat evened out by reparations
		for peace to happen" */
	/*if(iTheirBenefit < iOurBenefit*3/5 ||
			(kOther.isHuman() && iOurBenefit < iTheirBenefit*4/5) ||
			(!kOther.isHuman() && iOurBenefit < iTheirBenefit*3/5))
		return false; */
	// <advc.134a> Handle low human benefit separately (after filling the lists)
	if((!kOther.isHuman() && 5 * iTheirBenefit < 3 * iOurBenefit) ||
		5 * iOurBenefit < (kOther.isHuman() ? 4 : 3) * iTheirBenefit)
	{
		return false;
	} // </advc.134a>
	// <advc.039> Don't want to announce odd amounts of gold
	if (GC.getDefineBOOL(CvGlobals::ANNOUNCE_REPARATIONS) && !kOther.isHuman())
	{
		if (iReceiveGold > 10)
			AI_roundTradeVal(iReceiveGold);
		if (iGiveGold > 10)
			AI_roundTradeVal(iGiveGold);
	} // </advc.039>
	CLinkList<TradeData> weGive;
	CLinkList<TradeData> theyGive;
	TradeData peaceTreaty(TRADE_PEACE_TREATY);
	weGive.insertAtEnd(peaceTreaty);
	theyGive.insertAtEnd(peaceTreaty);
	if(eBestGiveTech != NO_TECH)
		weGive.insertAtEnd(TradeData(TRADE_TECHNOLOGIES, eBestGiveTech));
	if(eBestReceiveTech != NO_TECH)
		theyGive.insertAtEnd(TradeData(TRADE_TECHNOLOGIES, eBestReceiveTech));
	if(iGiveGold != 0)
		weGive.insertAtEnd(TradeData(TRADE_GOLD, iGiveGold));
	if(iReceiveGold != 0)
		theyGive.insertAtEnd(TradeData(TRADE_GOLD, iReceiveGold));
	if(pBestGiveCity != NULL)
		weGive.insertAtEnd(TradeData(TRADE_CITIES, pBestGiveCity->getID()));
	if(pBestReceiveCity != NULL)
		theyGive.insertAtEnd(TradeData(TRADE_CITIES, pBestReceiveCity->getID()));
	// <advc.134a>
	if(kOther.isHuman() && iTheirBenefit < iOurBenefit)
	{
		/*  Really can't make an attractive offer w/o considering all tradeable items,
			including map and gpt. */
		// Remove gold from the proposal to allow AI_counterPropose to set a different amount
		for(CLLNode<TradeData>* pNode = weGive.head(); pNode != NULL; pNode = weGive.next(pNode))
		{
			if(pNode->m_data.m_eItemType == TRADE_GOLD)
			{
				weGive.deleteNode(pNode);
				break;
			}
		}
		kOther.AI_counterPropose(getID(), weGive, theyGive, true, false);
		iOurBenefit = AI_dealVal(eOther, theyGive);
		iTheirBenefit = kOther.AI_dealVal(getID(), weGive);
		if(5 * iTheirBenefit < 3 * iOurBenefit)
			return false;
	} // </advc.134a>
	if(kOther.isHuman())
	{
		AI_changeContactTimer(eOther, CONTACT_PEACE_TREATY,
				// advc.134a: Times 4/3 (too tedious to change in XML)
				(4 * GC.getInfo(getPersonalityType()).
				getContactDelay(CONTACT_PEACE_TREATY)) / 3);
		CvDiploParameters* pDiplo = new CvDiploParameters(getID());
		pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_OFFER_PEACE"));
		pDiplo->setAIContact(true);
		pDiplo->setOurOfferList(theyGive);
		pDiplo->setTheirOfferList(weGive);
		gDLL->beginDiplomacy(pDiplo, eOther);
		//advc.test, advc.134a:  (seems to work reliably now)
		//FAssertMsg(false, "AI sent peace offer; if it doesn't appear, it could be b/c of a change in circumstances, or advc.134a isn't working as intended");
	}
	else GC.getGame().implementDeal(getID(), eOther, weGive, theyGive);
	return true;
}

// Auxiliary function to reduce the amount of duplicate or dual code
int CvPlayerAI::AI_negotiatePeace(PlayerTypes eRecipient, PlayerTypes eGiver,
	int iDelta, int* iGold, TechTypes* eBestTech, CvCity const** pBestCity)
{
	int r = 0;
	CvPlayerAI& kRecipient = GET_PLAYER(eRecipient);
	CvPlayerAI& kGiver = GET_PLAYER(eGiver);
	// <advc.104h> Pay gold if peace vals are similar. (Tbd.: gold per turn)
	if(iDelta <= kGiver.AI_maxGoldTrade(eRecipient, true /* advc.134a */ ) &&
		iDelta < 100 * kGiver.getCurrentEra())
	{
		if(kGiver.canTradeItem(eRecipient, TradeData(TRADE_GOLD, iDelta), true))
		{
			*iGold = iDelta;
			return iDelta;
		}
	} // </advc.104h>
	if (kGiver.canPossiblyTradeItem(eRecipient, TRADE_TECHNOLOGIES)) // advc.opt
	{
		int iBestValue = MIN_INT;
		FOR_EACH_ENUM(Tech)
		{
			if(kGiver.canTradeItem(eRecipient, TradeData(
				TRADE_TECHNOLOGIES, eLoopTech), true))
			{
				/*  advc.104h: Pick the best fit instead of a random tech.
					Random tech has the advantage that any tech is tried eventually;
					disadvantage: can take many attempts at peace negotiation until
					a deal is struck.
					Make the choice more and more random the longer the war lasts
					by adding a random number between 0 and war duration to the
					power of 1.65. */
				int iValue = -std::abs(GET_TEAM(kRecipient.getTeam()).AI_techTradeVal(
						eLoopTech, kGiver.getTeam(), true, true) - iDelta) +
						GC.getGame().getSorenRandNum(::round(
						std::pow((double)GET_TEAM(kRecipient.getTeam()).
						AI_getAtWarCounter(kGiver.getTeam()), 1.65)), "AI Peace Trading");
				// BtS code:
				//iValue = (1 + GC.getGame().getSorenRandNum(10000, "AI Peace Trading (Tech #1)"));
				if(iValue > iBestValue)
				{
					iBestValue = iValue;
					*eBestTech = eLoopTech;
				}
			}
		}
	}
	if(*eBestTech != NO_TECH)
	{
		// advc.104h: techTradeVal reduced by passing peaceDeal=true
		r += GET_TEAM(kRecipient.getTeam()).AI_techTradeVal(*eBestTech,
				kGiver.getTeam(), true, true);
	}

	// K-Mod note: ideally we'd use AI_goldTradeValuePercent()
	// to work out the gold's value gained by the receiver and the gold's value lost by the giver.
	// Instead, we just pretend that the receiver gets 1 point per gold, and the giver loses nothing.
	// (effectively a value of 2)
	{
		int iTradeGold = std::min(iDelta - r, kGiver.AI_maxGoldTrade(eRecipient,
				true)); // advc.134a
		if(iTradeGold > 0)
		{
			if(kGiver.canTradeItem(eRecipient, TradeData(
				TRADE_GOLD, iTradeGold), true))
			{
				*iGold = iTradeGold;
				r += iTradeGold;
			}
		}
	}
	// advc.104h: Don't add a city to the trade if it's already almost square
	if(iDelta - r > 0.2 * iDelta &&
		kGiver.canPossiblyTradeItem(eRecipient, TRADE_CITIES)) // advc.opt
	{
		CvCityAI const* pBestCityAI = NULL;
		int iBestValue = 0;
		FOR_EACH_CITYAI(pLoopCity, kGiver)
		{
			if(kGiver.canTradeItem(eRecipient, TradeData(
				TRADE_CITIES, pLoopCity->getID()), true)
				/*  <advc.104h> BtS checks this (only if we're the ones giving
					away the city). Apply the check regardless of who owns the
					city, but make it less strict. */
				//&& (eRecipient == getID() || kRecipient.AI_cityTradeVal(*pLoopCity) <= iDelta - r)
				&& kRecipient.AI_cityTradeVal(*pLoopCity) <=
				iDelta - r + 0.2 * iDelta) // </advc.104h>
			{
				int iValue = pLoopCity->getPlot().calculateCulturePercent(eRecipient);
				if(iValue > iBestValue)
				{
					iBestValue = iValue;
					pBestCityAI = pLoopCity;
				}
			}
		}
		if(pBestCityAI != NULL)
		{
			r += kRecipient.AI_cityTradeVal(*pBestCityAI);
			*pBestCity = pBestCityAI;
		}
	}
	return r;
}

void CvPlayerAI::AI_offerCapitulation(PlayerTypes eTo)
{
	CLinkList<TradeData> ourList;
	CLinkList<TradeData> theirList;
	ourList.insertAtEnd(TradeData(TRADE_SURRENDER));
	if(!GET_PLAYER(eTo).isHuman())
	{
		GC.getGame().implementDeal(getID(), eTo, ourList, theirList);
		return;
	} // advc.134a:
	GET_PLAYER(eTo).AI_counterPropose(getID(), ourList, theirList, true, false, 0.9);
	AI_changeContactTimer(eTo, CONTACT_PEACE_TREATY,
			// advc.134a: Double the delay
			2 * GC.getInfo(getPersonalityType()).
			getContactDelay(CONTACT_PEACE_TREATY));
	CvDiploParameters* pDiplo = new CvDiploParameters(getID());
	pDiplo->setDiploComment((DiploCommentTypes)
			GC.getInfoTypeForString("AI_DIPLOCOMMENT_OFFER_PEACE"));
	pDiplo->setAIContact(true);
	pDiplo->setOurOfferList(theirList);
	pDiplo->setTheirOfferList(ourList);
	gDLL->beginDiplomacy(pDiplo, eTo);
	//advc.test, advc.134a:
	FAssertMsg(false, "AI sent capitulation offer; if it doesn't appear, it could be b/c of a change in circumstances, or advc.134a isn't working as intended");
}
// </advc.104h>
// advc: Cut from AI_doPeace
bool CvPlayerAI::AI_willOfferPeace(PlayerTypes eTo) const
{
	CvPlayer const& kTo = GET_PLAYER(eTo);
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
	if(!kTo.isAlive() || eTo == getID() || !kOurTeam.isAtWar(kTo.getTeam()) ||
			// Don't contact if ...
			!canContact(eTo, true) || // either side refuses to talk
			kOurTeam.isHuman() || // a human on our team is doing the talking
			// a human on their team
			(!kTo.isHuman() && (GET_TEAM(kTo.getTeam())).isHuman()) ||
			// they're human and we're not the team leader
			(kTo.isHuman() && kOurTeam.getLeaderID() != getID()) ||
			// war too recent
			kOurTeam.AI_getAtWarCounter(kTo.getTeam()) <= 10
			/*  advc.134a: Allow earlier peace in later eras
				(only relevant if UWAI disabled) */
			- getCurrentEra() + 1)
		return false;
	return true;
}


void CvPlayerAI::AI_updateFoundValues(bool bStartingLoc)  // advc: refactored
{
	PROFILE_FUNC();
	// <advc.303>
	if(isBarbarian())
		return;
	// </advc.303>
	FOR_EACH_AREA_VAR(pLoopArea)
		pLoopArea->setBestFoundValue(getID(), 0);

	if(bStartingLoc)
	{
		for(int iI = 0; iI < GC.getMap().numPlots(); iI++)
			GC.getMap().getPlotByIndex(iI).setFoundValue(getID(), -1);
		return;
	}
	CitySiteEvaluator citySiteEval(*this);
	AI_invalidateCitySites(/*AI_getMinFoundValue()*/-1); // K-Mod
	// <advc.108>
	int iCities = getNumCities();
	CvPlot const* pStartPlot = getStartingPlot(); // </advc.108>
	for(int iI = 0; iI < GC.getMap().numPlots(); iI++)
	{
		CvPlot& kLoopPlot = GC.getMap().getPlotByIndex(iI);
		if(!kLoopPlot.isRevealed(getTeam()))
			//&& !AI_isPrimaryArea(kLoopPlot.getArea()))
		/*  K-Mod: Clear out any junk found values.
			(I've seen legacy AI code which makes use of the found values of unrevealed plots.
			It shouldn't use those values at all, but if it does use them,
			I'd prefer them not to be junk!) */
		{
			kLoopPlot.setFoundValue(getID(), 0);
			continue;
		}
		short iValue = GC.getPythonCaller()->AI_foundValue(getID(), kLoopPlot);
		if(iValue == -1)
		{	// K-Mod:
			iValue = citySiteEval.evaluate(kLoopPlot);
			// <advc.108> Slight preference for the assigned starting plot
			if(iCities <= 0 && pStartPlot != NULL && &kLoopPlot == pStartPlot &&
					// Unless it doesn't have fresh water
					kLoopPlot.isFreshWater())
				iValue = ::intToShort(::round(1.05 * iValue)); // </advc.108>
		}
		kLoopPlot.setFoundValue(getID(), iValue);
		if(iValue > kLoopPlot.getArea().getBestFoundValue(getID()))
			kLoopPlot.getArea().setBestFoundValue(getID(), iValue);
	}
	int iMaxCityCount = 4;
	// K-Mod - because humans don't always walk towards the AI's top picks..
	if(isHuman())
		iMaxCityCount = 6;
	AI_updateCitySites(-1, iMaxCityCount); // advc: -1 now means the default number
}


void CvPlayerAI::AI_updateAreaTargets()
{
	bool bResetTimer = AI_getCityTargetTimer() > 4; // K-Mod. (reset the timer if it is above the minimum time)

	FOR_EACH_AREA_VAR(pLoopArea)
	{
		if (!pLoopArea->isWater())
		{
			if(!GET_TEAM(getTeam()).AI_isSneakAttackReady() && // advc.104p
					GC.getGame().getSorenRandNum(3, "AI Target City") == 0)
				pLoopArea->AI_setTargetCity(getID(), NULL);
			else pLoopArea->AI_setTargetCity(getID(), AI_findTargetCity(*pLoopArea));
			bResetTimer = bResetTimer || pLoopArea->AI_getTargetCity(getID()); // K-Mod
		}
	}
	// K-Mod. (guarantee a short amount of time before randomly updating again)
	if (bResetTimer)
		AI_setCityTargetTimer(4);
	// K-Mod end
}

// Returns priority for unit movement (lower values move first...)
// This function has been heavily edited for K-Mod
int CvPlayerAI::AI_movementPriority(CvSelectionGroupAI const& kGroup) const // advc.003u: param was CvSelectionGroup*
{
	// If the group is trying to do a stack attack, let it go first!
	// (this is required for amphibious assults; during which a low priority group can be ordered to attack before its turn.)
	if (kGroup.AI_isGroupAttack())
		return -1;

	const CvUnit* pHeadUnit = kGroup.getHeadUnit();

	if (pHeadUnit == NULL)
		return MAX_INT;

	if (pHeadUnit->isSpy())
	{
		return 0;
	}

	if (pHeadUnit->getDomainType() != DOMAIN_LAND)
	{
		if (pHeadUnit->bombardRate() > 0)
			return 1;

		if (pHeadUnit->hasCargo())
		{
			if (pHeadUnit->specialCargo() != NO_SPECIALUNIT)
				return 2;
			else return 3;
		}

		if (pHeadUnit->getDomainType() == DOMAIN_AIR)
		{
			// give recon units top priority. Unfortunately, these are not easy to identify because there is no air_recon AI type.
			if (pHeadUnit->airBombBaseRate() == 0 && !pHeadUnit->canAirDefend())
				return 0;

			// non recon units (including nukes!)
			if (pHeadUnit->canAirDefend() || pHeadUnit->evasionProbability() > 10)
				return 4;
			else return 5;
		}

		if (pHeadUnit->canFight())
		{
			if (pHeadUnit->collateralDamage() > 0)
				return 6;
			else return 7;
		}
		else
			return 8;
	}

	FAssert(pHeadUnit->getDomainType() == DOMAIN_LAND);

	if (pHeadUnit->AI_getUnitAIType() == UNITAI_WORKER) {
		// <advc.001> Free Worker at game start: found a city first
		if(getNumCities() <= 0)
			return 210; // </advc.001>
		return 9;
	}

	if (pHeadUnit->AI_getUnitAIType() == UNITAI_EXPLORE)
		return 10;

	if (pHeadUnit->bombardRate() > 0)
		return 11;

	if (pHeadUnit->collateralDamage() > 0)
		return 12;

	if (kGroup.isStranded())
		return 505;

	if (pHeadUnit->canFight())
	{
		int iPriority = 65; // allow + or - 50

		iPriority += (GC.getGame().getBestLandUnitCombat()*100 - pHeadUnit->currCombatStr(NULL, NULL) + 10) / 20; // note: currCombatStr has a factor of 100 built in.

		if (kGroup.getNumUnits() > 1)
		{
			iPriority--;
			if (kGroup.getNumUnits() > 4)
				iPriority--;
		}
		else if (kGroup.AI_getMissionAIType() == MISSIONAI_GUARD_CITY)
			iPriority++;

		if (pHeadUnit->withdrawalProbability() > 0 || pHeadUnit->noDefensiveBonus())
		{
			iPriority--;
			if (pHeadUnit->withdrawalProbability() > 20)
				iPriority--;
		}

		switch (pHeadUnit->AI_getUnitAIType())
		{
		case UNITAI_ATTACK_CITY:
		case UNITAI_ATTACK:
			iPriority--;
		case UNITAI_COUNTER:
			iPriority--;
		case UNITAI_PILLAGE:
		case UNITAI_RESERVE:
			iPriority--;
		case UNITAI_CITY_DEFENSE:
		case UNITAI_CITY_COUNTER:
		case UNITAI_CITY_SPECIAL:
			break;

		case UNITAI_COLLATERAL:
			FAssertMsg(false, "AI_movementPriority: unit AI type doesn't seem to match traits.");
			break;

		default:
			break;
		}
		return iPriority;
	}
	/* old code
	if (pHeadUnit->canFight()) {
		if (pHeadUnit->withdrawalProbability() > 20)
			return 9;
		if (pHeadUnit->withdrawalProbability() > 0)
			return 10;
		iCurrCombat = pHeadUnit->currCombatStr(NULL, NULL);
		iBestCombat = (GC.getGame().getBestLandUnitCombat() * 100);
		if (pHeadUnit->noDefensiveBonus()) {
			iCurrCombat *= 3;
			iCurrCombat /= 2;
		}
		if (pHeadUnit->AI_isCityAIType())
			iCurrCombat /= 2;
		if (iCurrCombat > iBestCombat)
			return 11;
		else if (iCurrCombat > (iBestCombat * 4) / 5)
			return 12;
		else if (iCurrCombat > (iBestCombat * 3) / 5)
			return 13;
		else if (iCurrCombat > (iBestCombat * 2) / 5)
			return 14;
		else if (iCurrCombat > (iBestCombat * 1) / 5)
			return 15;
		else return 16;
	}*/
	return 200;
}


void CvPlayerAI::AI_unitUpdate()
{
	PROFILE_FUNC();

	FAssert(m_groupCycle.getLength() == m_selectionGroups.getCount());

	if (!hasBusyUnit())
	{
		CLLNode<int>* pCurrUnitNode = headGroupCycleNode();

		while (pCurrUnitNode != NULL)
		{
			CvSelectionGroupAI* pLoopSelectionGroup = AI_getSelectionGroup(pCurrUnitNode->m_data);
			pCurrUnitNode = nextGroupCycleNode(pCurrUnitNode);

			if (pLoopSelectionGroup->AI_isForceSeparate())
			{
				// do not split groups that are in the midst of attacking
				if (pLoopSelectionGroup->isForceUpdate() || !pLoopSelectionGroup->AI_isGroupAttack())
				{
					pLoopSelectionGroup->AI_separate();	// pointers could become invalid...
				}
			}
		}

		/*if (isHuman()) {
			pCurrUnitNode = headGroupCycleNode();
			while (pCurrUnitNode != NULL) {
				CvSelectionGroup* pLoopSelectionGroup = getSelectionGroup(pCurrUnitNode->m_data);
				pCurrUnitNode = nextGroupCycleNode(pCurrUnitNode);
				if (pLoopSelectionGroup->AI_update())
					break; // pointers could become invalid...
			}
		}
		else*/ // BtS
		if (!isHuman()) /*  K-Mod - automated actions are no longer done from this function.
							Instead, they are done in CvGame::updateMoves */ {
			/*tempGroupCycle.clear();
			finalGroupCycle.clear();
			pCurrUnitNode = headGroupCycleNode();
			while (pCurrUnitNode != NULL) {
				tempGroupCycle.insertAtEnd(pCurrUnitNode->m_data);
				pCurrUnitNode = nextGroupCycleNode(pCurrUnitNode);
			}
			iValue = 0;
			while (tempGroupCycle.getLength() > 0) {
				pCurrUnitNode = tempGroupCycle.head();
				while (pCurrUnitNode != NULL) {
					pLoopSelectionGroup = getSelectionGroup(pCurrUnitNode->m_data);
					FAssertMsg(pLoopSelectionGroup != NULL, "selection group node with NULL selection group");
					if (AI_movementPriority(pLoopSelectionGroup) <= iValue) {
						finalGroupCycle.insertAtEnd(pCurrUnitNode->m_data);
						pCurrUnitNode = tempGroupCycle.deleteNode(pCurrUnitNode);
					}
					else pCurrUnitNode = tempGroupCycle.next(pCurrUnitNode);
				}
				iValue++;
			}
			pCurrUnitNode = finalGroupCycle.head();
			while (pCurrUnitNode != NULL) {
				pLoopSelectionGroup = getSelectionGroup(pCurrUnitNode->m_data);
				if (NULL != pLoopSelectionGroup) { // group might have been killed by a previous group update
					if (pLoopSelectionGroup->AI_update())
						break; // pointers could become invalid...
				}
				pCurrUnitNode = finalGroupCycle.next(pCurrUnitNode);
			}*/ // BtS
			// K-Mod. It seems to me that all we're trying to do is run AI_update
			// on the highest priority groups until one of them becomes busy.
			// ... if only they had used the STL, this would be a lot easier.
			bool bRepeat = true;
			do
			{
				std::vector<std::pair<int, int> > groupList;

				pCurrUnitNode = headGroupCycleNode();
				while (pCurrUnitNode != NULL)
				{
					CvSelectionGroupAI* pLoopSelectionGroup = AI_getSelectionGroup(pCurrUnitNode->m_data);
					int iPriority = AI_movementPriority(*pLoopSelectionGroup);
					groupList.push_back(std::make_pair(iPriority, pCurrUnitNode->m_data));

					pCurrUnitNode = nextGroupCycleNode(pCurrUnitNode);
				}
				FAssert(groupList.size() == getNumSelectionGroups());

				std::sort(groupList.begin(), groupList.end());
				for (size_t i = 0; i < groupList.size(); i++)
				{
					CvSelectionGroupAI* pLoopSelectionGroup = AI_getSelectionGroup(groupList[i].second);
					// I think it's probably a good idea to activate automissions here, so that the move priority is respected even for commands issued on the previous turn.
					// (This will allow reserve units to guard workers that have already moved, rather than trying to guard workers who are about to move to a different plot.)
					if (pLoopSelectionGroup && !pLoopSelectionGroup->autoMission())
					{
						if (pLoopSelectionGroup->isBusy() || pLoopSelectionGroup->isCargoBusy() || pLoopSelectionGroup->AI_update())
						{
							bRepeat = false;
							break;
						}
					}
				}

				// one last trick that might save us a bit of time...
				// if the number of selection groups has increased, then lets try to take care of the new groups right away.
				// (there might be a faster way to look for the new groups, but I don't know it.)
				bRepeat = bRepeat && m_groupCycle.getLength() > (int)groupList.size();
				// the repeat will do a stack of redundant checks,
				// but I still expect it to be much faster than waiting for the next turnslice.
				// Note: I use m_groupCycle rather than getNumSelectionGroups just in case there is a bug which causes the two to be out of sync.
				// (otherwise, if getNumSelectionGroups is bigger, it could cause an infinite loop.)
			} while (bRepeat);
			// K-Mod end
		}
	}
}


void CvPlayerAI::AI_makeAssignWorkDirty()
{
	FOR_EACH_CITYAI_VAR(pLoopCity, *this)
		pLoopCity->AI_setAssignWorkDirty(true);
}


void CvPlayerAI::AI_assignWorkingPlots()
{	// <advc.rom2>
	if(isAnarchy())
		return; // </advc.rom2>
	FOR_EACH_CITYAI_VAR(pLoopCity, *this)
		pLoopCity->AI_assignWorkingPlots();
}


void CvPlayerAI::AI_updateAssignWork()
{
	FOR_EACH_CITYAI_VAR(pLoopCity, *this)
		pLoopCity->AI_updateAssignWork();
}

#if 0 // advc: Unfinished K-Mod and BBAI code
void CvPlayerAI::AI_doCentralizedProduction()
{
	PROFILE_FUNC();

	CvCity* pLoopCity;
	int iLoop;
	int iI;

	if (isHuman())
	{
		return;
	}

	if (isBarbarian())
	{
		return;
	}

	// Choose which cities should build floating defenders
	// evaluate military cities score and rank.
	// choose military cities that aren't building anything, or wait for good military cities to finish building.
	/*
	bool bCrushStrategy = kPlayer.AI_isDoStrategy(AI_STRATEGY_CRUSH);
	int iNeededFloatingDefenders = (isBarbarian() || bCrushStrategy) ?  0 : kPlayer.AI_getTotalFloatingDefendersNeeded(pArea);
 	int iTotalFloatingDefenders = (isBarbarian() ? 0 : kPlayer.AI_getTotalFloatingDefenders(pArea));

	UnitTypeWeightArray floatingDefenderTypes;
	floatingDefenderTypes.push_back(std::make_pair(UNITAI_CITY_DEFENSE, 125));
	floatingDefenderTypes.push_back(std::make_pair(UNITAI_CITY_COUNTER, 100));
	//floatingDefenderTypes.push_back(std::make_pair(UNITAI_CITY_SPECIAL, 0));
	floatingDefenderTypes.push_back(std::make_pair(UNITAI_RESERVE, 100));
	floatingDefenderTypes.push_back(std::make_pair(UNITAI_COLLATERAL, 100));

	if (iTotalFloatingDefenders < ((iNeededFloatingDefenders + 1) / (bGetBetterUnits ? 3 : 2)))
	if (!bExempt && AI_chooseLeastRepresentedUnit(floatingDefenderTypes))
	{
		if (gCityLogLevel >= 2) logBBAI("      City %S uses choose floating defender 1", getName().GetCString());
		return;
	}
	*/

	// Cycle through all buildings looking for limited buildings (wonders)
	// Identify the cities that will benefit the most. If the best city is not busy, build the wonder.
	/*
	for (iI = 0; iI < GC.getNumBuildingClassInfos(); iI++)
	{
		if (!(GET_PLAYER(getOwner()).isBuildingClassMaxedOut(((BuildingClassTypes)iI), GC.getInfo((BuildingClassTypes)iI).getExtraPlayerInstances())))
		{
			BuildingTypes eLoopBuilding = (BuildingTypes)GC.getInfo(getCivilizationType()).getCivilizationBuildings(iI);
			bool bWorld = CvBuildingClassInfo::isWorldWonder((BuildingClassTypes)iI);
			bool bNational = isNationalWonderClass((BuildingClassTypes)iI);

						if (canConstruct(eLoopBuilding))
						{
							// city loop.
							iValue = AI_buildingValueThreshold(eLoopBuilding);

							if (GC.getInfo(eLoopBuilding).getFreeBuildingClass() != NO_BUILDINGCLASS)
							{
								BuildingTypes eFreeBuilding = (BuildingTypes)GC.getInfo(getCivilizationType()).getCivilizationBuildings(GC.getInfo(eLoopBuilding).getFreeBuildingClass());
								if (NO_BUILDING != eFreeBuilding)
								{
									iValue += (AI_buildingValue(eFreeBuilding, iFocusFlags) * (GET_PLAYER(getOwner()).getNumCities() - GET_PLAYER(getOwner()).getBuildingClassCountPlusMaking((BuildingClassTypes)GC.getInfo(eLoopBuilding).getFreeBuildingClass())));
								}
							}

							if (iValue > 0)
							{
								iTurnsLeft = getProductionTurnsLeft(eLoopBuilding, 0);

								if (CvBuildingClassInfo::isWorldWonder((BuildingClassTypes)iI))
								{
									if (iProductionRank <= std::min(3, ((GET_PLAYER(getOwner()).getNumCities() + 2) / 3)))
									{
										if (bAsync)
										{
											iTempValue = GC.getASyncRand().get(GC.getInfo(getPersonalityType()).getWonderConstructRand(), "Wonder Construction Rand ASYNC");
										}
										else
										{
											iTempValue = GC.getGame().getSorenRandNum(GC.getInfo(getPersonalityType()).getWonderConstructRand(), "Wonder Construction Rand");
										}

										if (bAreaAlone)
										{
											iTempValue *= 2;
										}
										iValue += iTempValue;
									}
								}

								if (bAsync)
								{
									iValue *= (GC.getASyncRand().get(25, "AI Best Building ASYNC") + 100);
									iValue /= 100;
								}
								else
								{
									iValue *= (GC.getGame().getSorenRandNum(25, "AI Best Building") + 100);
									iValue /= 100;
								}

								iValue += getBuildingProduction(eLoopBuilding);


								bool bValid = ((iMaxTurns <= 0) ? true : false);
								if (!bValid)
								{
									bValid = (iTurnsLeft <= GC.getGame().AI_turnsPercent(iMaxTurns, GC.getInfo(GC.getGame().getGameSpeedType()).getConstructPercent()));
								}
								if (!bValid)
								{
									for (int iHurry = 0; iHurry < GC.getNumHurryInfos(); ++iHurry)
									{
										if (canHurryBuilding((HurryTypes)iHurry, eLoopBuilding, true))
										{
											if (AI_getHappyFromHurry((HurryTypes)iHurry, eLoopBuilding, true) > 0)
											{
												bValid = true;
												break;
											}
										}
									}
								}

								if (bValid
										&& iTurnsLeft < MAX_INT) // advc.004x
								{
									FAssert((MAX_INT / 1000) > iValue);
									iValue *= 1000;
									iValue /= std::max(1, (iTurnsLeft + 3));

									iValue = std::max(1, iValue);

									if (iValue > iBestValue)
									{
										iBestValue = iValue;
										eBestBuilding = eLoopBuilding;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return eBestBuilding;
	*/


// K-Mod end, the rest is some unfinished code from BBAI

	// BBAI TODO: Temp testing
	if (getID() % 2 == 1)
	{
		return;
	}

	// Determine number of cities player can use building wonders currently
	int iMaxNumWonderCities = 1 + getNumCities()/5;
	bool bIndustrious = (getMaxPlayerBuildingProductionModifier() > 0);
	bool bAtWar = (GET_TEAM(getTeam()).getAtWarCount(true) > 0);

	if (bIndustrious)
	{
		iMaxNumWonderCities += 1;
	}

	// Dagger?
	// Power?
	// Research?

	if (bAtWar)
	{
		int iWarSuccessRating = GET_TEAM(getTeam()).AI_getWarSuccessRating();
		if (iWarSuccessRating < -90)
		{
			iMaxNumWonderCities = 0;
		}
		else
		{
			if (iWarSuccessRating < 30)
			{
				iMaxNumWonderCities -= 1;
			}
			if (iWarSuccessRating < -50)
			{
				iMaxNumWonderCities /= 2;
			}
		}
	}

	if (isMinorCiv() && GET_TEAM(getTeam()).getHasMetCivCount(false) > 1)
	{
		iMaxNumWonderCities /= 2;
	}

	iMaxNumWonderCities = std::min(iMaxNumWonderCities, getNumCities());

	// Gather city statistics
	// Could rank cities based on gold, production here, could be O(n) instead of O(n^2)
	int iWorldWonderCities = 0;
	int iLimitedWonderCities = 0;
	int iNumDangerCities = 0;
	FOR_EACH_CITY(pLoopCity, *this)
	{
		if (pLoopCity->isProductionBuilding())
		{
			if (GC.getInfo(pLoopCity->getProductionBuilding()).isLimited())
			{
				iLimitedWonderCities++;
				if (GC.getInfo(pLoopCity->getProductionBuilding()).isWorldWonder())
					iWorldWonderCities++;
			}
		}

		if (pLoopCity->isProductionProject())
		{
			if (GC.getInfo(pLoopCity->getProductionProject()).isLimited())
			{
				iLimitedWonderCities++;
				if (GC.getInfo(pLoopCity->getProductionProject()).isWorldProject())
					iWorldWonderCities++;
			}
		}
	}

	// Check for any global wonders to build
	for (iI = 0; iI < GC.getNumBuildingClassInfos(); iI++)
	{
		if (GC.getInfo((BuildingClassTypes)iI).isWorldWonder())
		{

			//canConstruct(
		}
	}

	// Check for any projects to build
	for (iI = 0; iI < GC.getNumProjectInfos(); iI++)
	{

	}

	// Check for any national/team wonders to build
}
#endif

void CvPlayerAI::AI_makeProductionDirty()
{
	FAssert(!isHuman());
	FOR_EACH_CITY_VAR(pLoopCity, *this)
		pLoopCity->setChooseProductionDirty(true);
}

// BETTER_BTS_AI_MOD, War tactics AI, 05/16/10, jdog5000: START
void CvPlayerAI::AI_conquerCity(CvCityAI& kCity)  // advc: style changes, advc.003u: param was CvCity*
{
	if(!canRaze(kCity))
	{
		CvEventReporter::getInstance().cityAcquiredAndKept(getID(), &kCity);
		return;
	}

	CvPlayer const& kPreviousOwner = GET_PLAYER(kCity.getPreviousOwner());
	CvTeam const& kPreviousTeam = GET_TEAM(kCity.getPreviousOwner());
	CvGame& g = GC.getGame();

	bool bCultureVictory = false; // advc.116
	bool bRaze = false;
	// Reasons to always raze
	if(2 * kCity.getCulture(kPreviousOwner.getID()) >
		kCity.getCultureThreshold(g.culturalVictoryCultureLevel()))
	{
		int iHighCultureCount = 1;
		FOR_EACH_CITY(pLoopCity, kPreviousOwner)
		{
			if(2*pLoopCity->getCulture(kPreviousOwner.getID()) >
					pLoopCity->getCultureThreshold(g.culturalVictoryCultureLevel()))
			{
				iHighCultureCount++;
				// <advc.116> Count them all before deciding what to do
			}
		}
		int iVictTarget = g.culturalVictoryNumCultureCities();
		// Razing won't help if they have many high-culture cities
		if(iHighCultureCount == iVictTarget || iHighCultureCount == iVictTarget + 1)
		{
			bCultureVictory = true;
			// Don't raze if they're unlikely to reconquer it
			double powRatio = kPreviousTeam.getPower(true) /
					(0.1 + GET_TEAM(getTeam()).getPower(false));
			if(powRatio > 0.80)
				bRaze = true;
			if(!bRaze)
			{
				int iAttStr = AI_localAttackStrength(kCity.plot(),
						kPreviousTeam.getID(), DOMAIN_LAND, 4);
				int iDefStr = AI_localDefenceStrength(kCity.plot(), getTeam(),
						DOMAIN_LAND, 3);
				if(5 * iAttStr > 4 * iDefStr)
					bRaze = true;
				if(bRaze) logBBAI("  Razing enemy cultural victory city");
			}
		} // </advc.116>
	} // <advc.ctr>
	if(!isBarbarian() && !kCity.isHolyCity() && !kCity.isEverOwned(getID()) &&
		!kCity.hasActiveWorldWonder() && AI_isAwfulSite(kCity))
	{
		bRaze = true;
	} // </advc.ctr>

	if(!bRaze)
	{
		int iCloseness = kCity.AI_playerCloseness(getID());
		// Reasons to not raze
		if(!bCultureVictory) // advc.116
		{
			if(getNumCities() <= 1 || (getNumCities() < 5 && iCloseness > 0))
			{
				if (gPlayerLogLevel >= 1) logBBAI("    Player %d (%S) decides not to raze %S because they have few cities", getID(), getCivilizationDescription(0), kCity.getName().GetCString());
			}
			else if(AI_atVictoryStage(AI_VICTORY_DOMINATION3) &&
					GET_TEAM(getTeam()).AI_isPrimaryArea(kCity.getArea()))
			{
				// Do not raze, going for domination
				if (gPlayerLogLevel >= 1) logBBAI("    Player %d (%S) decides not to raze %S because they're going for domination", getID(), getCivilizationDescription(0), kCity.getName().GetCString());
			}  // <advc>
			CvEventReporter::getInstance().cityAcquiredAndKept(getID(), &kCity);
			return;
		}
		//else // </advc>
		int iRazeValue = 0;
		if(isBarbarian())
		{
			if(!kCity.isHolyCity() && !kCity.hasActiveWorldWonder() &&
				kCity.getPreviousOwner() != BARBARIAN_PLAYER &&
				kCity.getOriginalOwner() != BARBARIAN_PLAYER)
			{
				iRazeValue += GC.getInfo(getPersonalityType()).getRazeCityProb();
				//iRazeValue -= iCloseness;
				// <advc.300>
				int iDeltaEraPop = 1 + std::max(3,
						(int)GET_PLAYER(kCity.getOwner()).getCurrentEra()) -
						kCity.getPopulation();
				iRazeValue *= iDeltaEraPop;
				// The BtS raze roll; now used exclusively for Barbarians
				if(g.getSorenRandNum(100, "advc.300") < iRazeValue)
					bRaze = true;
				// </advc.300>
			}
		}
		else
		{
			bool bFinancialTrouble = AI_isFinancialTrouble();
			bool bBarbCity = (kCity.getPreviousOwner() == BARBARIAN_PLAYER &&
					kCity.getOriginalOwner() == BARBARIAN_PLAYER);
			bool bPrevOwnerBarb = (kCity.getPreviousOwner() == BARBARIAN_PLAYER);
			bool bTotalWar = (kPreviousTeam.getNumCities() > 0 && // advc.116
					// K-Mod
					GET_TEAM(getTeam()).AI_getWarPlan(kPreviousTeam.getID()) == WARPLAN_TOTAL);

			/* BtS code (advc: Deleted most of it)
			if (GET_TEAM(getTeam()).countNumCitiesByArea(pCity->getArea()) == 0)
			{	// Conquered city in new continent/island
				int iBestValue;
				if (pCity->getArea().getNumCities() == 1 && AI_getNumAreaCitySites(pCity->getArea().getID(), iBestValue) == 0)
				{	// Probably small island
					// ...
				}
				else
				{
					// At least medium sized island
					// ...
				}
			}
			else ... */ /* K-Mod disabled. I don't see the point treating
				the above as a special case. */

			// advc.116: Commented this out too: Why raze islands?
				// K-Mod (moved this from above)
				/*int iUnused;
				if (pCity->getArea().getNumCities() == 1 && AI_getNumAreaCitySites(pCity->getArea().getID(), iUnused) == 0) {
					// Probably small island
					if (iCloseness == 0)
						iRazeValue += 20;
				} */
			// K-Mod end
			/*  <advc.116> Some advantages of conquering Barbarian cities aren't
				fully covered by the code below I believe. Also, razing Barbarian cities
				makes the AI look bad - why conquer the city in the first place? */
			if(bBarbCity)
				iRazeValue -= 7; // </advc.116>
			// Distance related aspects
			if (iCloseness > 0)
				iRazeValue -= iCloseness;
			else
			{
				iRazeValue += 15; // advc.116: was 40
				// <cdtw.1>
				if(isAVassal())
				{
					iRazeValue -= 5;
					if(GET_TEAM(getTeam()).isCapitulated())
						iRazeValue -= 10;
				} // </cdtw.1>
				CvCity* pNearestTeamAreaCity = GC.getMap().findCity(
						kCity.getX(), kCity.getY(), NO_PLAYER,
						getTeam(), true, false, NO_TEAM, NO_DIRECTION, &kCity);

				// K-Mod
				if (pNearestTeamAreaCity == NULL &&
					/*  <advc.300> The +15 is bad enough if we don't have to
						worry about attacks against the city */
					kCity.getPreviousOwner() != BARBARIAN_PLAYER &&
					kCity.getArea().countCivCities() > 1) // </advc.300>
				{
					if (bTotalWar &&
						GET_TEAM(kCity.getPreviousOwner()).AI_isPrimaryArea(kCity.getArea()))
					{
						iRazeValue += 5;
					}
					else iRazeValue += 13; // advc.116: was +30
				}
				// K-Mod end
				else if(pNearestTeamAreaCity != NULL) // advc.300
				{
					int iDistance = plotDistance(kCity.getX(), kCity.getY(),
							pNearestTeamAreaCity->getX(), pNearestTeamAreaCity->getY());
					iDistance -= DEFAULT_PLAYER_CLOSENESS + 2;
					// <advc.116> World size adjustment, upper cap
					iDistance *= 60;
					iDistance /= GC.getMap().maxTypicalDistance();
					iDistance = std::min(20, iDistance); // </advc.116>
					if (iDistance > 0)
						iRazeValue += iDistance * (bBarbCity ? 3 : 2); // advc.116: Was 8 : 5
				}
				/*  <advc.116> If we expect to conquer most of the area, then
					non-closeness (alone) really shouldn't be a reason to raze. */
				if(!bPrevOwnerBarb)
				{
					CvArea const& kArea = kCity.getArea();
					double ourLocalPowRatio = kArea.getPower(getID()) / (getPower() + 0.1);
					int iTheirLocalCities = 0;
					int iTheirGlobalCities = 0;
					for (PlayerIter<CIV_ALIVE> it; it.hasNext(); ++it)
					{
						CvPlayerAI const& kEnemy = *it;
						if(GET_TEAM(getTeam()).AI_getWarPlan(kEnemy.getTeam()) != NO_WARPLAN)
						{
							iTheirLocalCities += kArea.getCitiesPerPlayer(kEnemy.getID());
							iTheirGlobalCities += kEnemy.getNumCities();
						}
					}
					double theirLocalPowRatio = iTheirLocalCities / (double)
							std::max(1, iTheirGlobalCities);
					double ourGlobalPowRatio = std::min(2.0, 100.0 /
							(GET_TEAM(getTeam()).AI_getEnemyPowerPercent(true) + 0.1));
					double subtr = 0;
					// Medium-term perspective
					if(ourGlobalPowRatio >= 1.25 * theirLocalPowRatio &&
						ourLocalPowRatio > 0.09) // Enough for a bridgehead
					{
						subtr += std::sqrt((double)std::min(3, iTheirLocalCities)) *
								60 * ourGlobalPowRatio * ourLocalPowRatio *
								(1 - theirLocalPowRatio);
					}
					iRazeValue = std::max(0, iRazeValue - std::min(20, ::round(subtr)));
				} // </advc.116>
			}
			// <advc.116>
			if(bFinancialTrouble)
			{
				iRazeValue += (kCity.calculateDistanceMaintenanceTimes100(getID()) +
						kCity.calculateDistanceMaintenanceTimes100(getID())) /
						100; // Replacing:
						//std::max(0, (70 - 15 * pCity->getPopulation()));
			}
			iRazeValue -= 3 * kCity.getPopulation();
			// </advc.116>
			// (disabled by K-Mod)
			// Scale down distance/maintenance effects for organized.
			/*if (iRazeValue > 0) {
				for (iI = 0; iI < GC.getNumTraitInfos(); iI++) {
					// ... (advc: body deleted)
				}
			}*/

			// Non-distance related aspects
			iRazeValue += GC.getInfo(getPersonalityType()).getRazeCityProb()
					/ 5; // advc.116
			iRazeValue -= AI_atVictoryStage(AI_VICTORY_DOMINATION2) ? 20 : 0; // K-Mod

			if (getStateReligion() != NO_RELIGION)
			{
				if (kCity.isHasReligion(getStateReligion()))
				{
					if (GET_TEAM(getTeam()).hasShrine(getStateReligion()))
					{
						iRazeValue -= 50;
						if (gPlayerLogLevel >= 1) logBBAI("      Reduction for state religion with shrine");
					}
					else
					{
						iRazeValue -= 10;
						if (gPlayerLogLevel >= 1) logBBAI("      Reduction for state religion");
					}
				}
			} // K-Mod
			else
			{
				// Free religion does not mean we hate everyone equally...
				iRazeValue -= 5;
			} // K-Mod end

			for (int iI = 0; iI < GC.getNumReligionInfos(); iI++)
			{
				if (kCity.isHolyCity((ReligionTypes)iI))
				{
					logBBAI("      Reduction for holy city");
					if(getStateReligion() == iI)
						iRazeValue -= 150;
					else iRazeValue -= 5 + g.calculateReligionPercent((ReligionTypes)iI);
				}
			}

			// K-Mod
			// corp HQ value.
			for (int iI = 0; iI < GC.getNumCorporationInfos(); iI++)
			{
				if (kCity.isHeadquarters((CorporationTypes)iI))
				{
					logBBAI("      Reduction for corp headquarters");
					iRazeValue -= 10 + 100 * g.countCorporationLevels(
							(CorporationTypes)iI) / g.getNumCities();
				}
			}
			// great people
			iRazeValue -= 5 * kCity.getNumGreatPeople(); // advc.116: was 2 * ...
			iRazeValue += bBarbCity ? 5 : 0;
			// K-Mod end

			iRazeValue -= 15 * kCity.getNumActiveWorldWonders();

			for (CityPlotIter it(kCity); it.hasNext(); ++it)
			{
				if (it->getBonusType(getTeam()) != NO_BONUS)
				{
					iRazeValue -= std::max(2, AI_bonusVal(it->getBonusType(
							getTeam()), 1, true));
					// advc.116: Don't halve the bonus value
				}

			}
			// <advc.116>
			PlayerTypes eCulturalOwner = kCity.findHighestCulture();
			if(eCulturalOwner != NO_PLAYER)
			{
				CvPlayer& kCulturalOwner = GET_PLAYER(eCulturalOwner);
				if(!kCulturalOwner.isBarbarian() &&
					!::atWar(getTeam(), kCulturalOwner.getTeam()))
				{
					if(eCulturalOwner == getID())
						iRazeValue -= 15;
					else
					{	// Avoid penalty to relations (and may want to liberate)
						int iMultiplier = std::max(0,
								AI_getAttitude(eCulturalOwner) - ATTITUDE_ANNOYED);
						// 10 for Cautious, 20 for Pleased, 30 for Friendly
						iRazeValue -= 10 * iMultiplier;
					}
				}
			}
			// Don't raze remote cities conquered in early game
			int iTargetCities = GC.getInfo(GC.getMap().getWorldSize()).
					getTargetNumCities();
			if(iTargetCities * 0.75 >= (double)getNumCities())
				iRazeValue -= (iTargetCities - getNumCities()) * 5;
			// </advc.116>

			// More inclined to raze if we're unlikely to hold it
			/*if(GET_TEAM(getTeam()).getPower(false)*10 <
					kPreviousTeam.getPower(true)*8) {
				int iTempValue = 20;
				iTempValue *= (kPreviousTeam.getPower(true) -
						GET_TEAM(getTeam()).getPower(false));
				iTempValue /= std::max(100, GET_TEAM(getTeam()).getPower(false));
				logBBAI("      Low power, so boost raze odds by %d", std::min( 75, iTempValue));
				iRazeValue += std::min(75, iTempValue);
			}*/
			// <advc.116> Razing doesn't really help when losing a war. Replacement:
			iRazeValue += ::range((GET_TEAM(getTeam()).
					AI_getEnemyPowerPercent(true) - 100) / 6, -10, 10);
			// </advc.116>
			if(gPlayerLogLevel >= 1)
			{
				if (bBarbCity) logBBAI("      %S is a barb city", kCity.getName().GetCString());
				if (bPrevOwnerBarb) logBBAI("      %S was last owned by barbs", kCity.getName().GetCString());
				logBBAI("      %S has area cities %d, closeness %d, bFinTrouble %d", kCity.getName().GetCString(), GET_TEAM(getTeam()).countNumCitiesByArea(kCity.getArea()), iCloseness, bFinancialTrouble);
			}

			// advc.116: Replacing the dice roll below. Hardly any randomness this way.
			iRazeValue += g.getSorenRandNum(6, "");
		} // End of !isBarbarian()

		if(gPlayerLogLevel >= 1)
		{
			//logBBAI("    Player %d (%S) has odds %d to raze city %S", getID(), getCivilizationDescription(0), iRazeValue, kCity.getName().GetCString());
			// advc.116: Replacing the above
			if(!bRaze) logBBAI("    Player %d (%S) has raze value %d for city %S", getID(), getCivilizationDescription(0), iRazeValue, kCity.getName().GetCString());
		}

		if (iRazeValue > 0)
		{
			// advc.116: Commented out
			//if (g.getSorenRandNum(100, "AI Raze City") < iRazeValue)
			bRaze = true;
		}
	}

	if(bRaze)
	{	// K-Mod moved the log message up - otherwise it will crash due to pCity being deleted!
		logBBAI("    Player %d (%S) decides to to raze city %S!!!", getID(), getCivilizationDescription(0), kCity.getName().GetCString());
		kCity.doTask(TASK_RAZE);
	}
	else CvEventReporter::getInstance().cityAcquiredAndKept(
			//g.getActivePlayer(), &kCity);
			getID(), &kCity); // UNOFFICIAL_PATCH, 06/14/09, Maniac & jdog5000
} // BETTER_BTS_AI_MOD: END

// <advc.130q> About 7 or 8 is high (important city), below 1 is low
double CvPlayerAI::AI_razeMemoryScore(CvCity const& c) const
{
	int const iCultureLevel = c.calculateCultureLevel(getID());
	if(iCultureLevel <= 0)
		return 0;
	double relativeTileCulture = c.getPlot().calculateCulturePercent(getID()) / 100.0;
	return relativeTileCulture * iCultureLevel *
			(c.getPopulation() + c.getHighestPopulation()) /
			// 'c' isn't razed yet but already conquered
			std::max((double)c.getPopulation(), std::sqrt((double)getTotalPopulation()));
} // </advc.130q>

bool CvPlayerAI::AI_acceptUnit(CvUnit const& kUnit) const // advc: param was CvUnit*
{
	if (isHuman())
		return true;

	if (AI_isFinancialTrouble())
	{
		if (kUnit.AI_getUnitAIType() == UNITAI_WORKER)
		{
			if (AI_neededWorkers(kUnit.getArea()) > 0)
				return true;
		}

		if (kUnit.AI_getUnitAIType() == UNITAI_WORKER_SEA)
			return true;

		if (kUnit.AI_getUnitAIType() == UNITAI_MISSIONARY)
			return true; //XXX

		// K-Mod
		switch (kUnit.AI_getUnitAIType())
		{
		case UNITAI_PROPHET:
		case UNITAI_ARTIST:
		case UNITAI_SCIENTIST:
		case UNITAI_GENERAL:
		case UNITAI_MERCHANT:
		case UNITAI_ENGINEER:
		case UNITAI_GREAT_SPY:
			return true;
		default:
			break;
		}
		// K-Mod end
		return false;
	}

	return true;
}


bool CvPlayerAI::AI_captureUnit(UnitTypes eUnit, CvPlot* pPlot) const
{
	FAssert(!isHuman());

	if (pPlot->getTeam() == getTeam())
		return true;

	CvCity* pNearestCity = GC.getMap().findCity(pPlot->getX(), pPlot->getY(), NO_PLAYER, getTeam());
	if (pNearestCity != NULL)
	{
		if (plotDistance(pPlot->getX(), pPlot->getY(), pNearestCity->getX(), pNearestCity->getY()) <= 4)
		{
			return true;
		}
	}

	return false;
}


DomainTypes CvPlayerAI::AI_unitAIDomainType(UnitAITypes eUnitAI) const
{
	switch (eUnitAI)
	{
	case UNITAI_UNKNOWN:
		return NO_DOMAIN;
		break;

	case UNITAI_ANIMAL:
	case UNITAI_SETTLE:
	case UNITAI_WORKER:
	case UNITAI_ATTACK:
	case UNITAI_ATTACK_CITY:
	case UNITAI_COLLATERAL:
	case UNITAI_PILLAGE:
	case UNITAI_RESERVE:
	case UNITAI_COUNTER:
	case UNITAI_PARADROP:
	case UNITAI_CITY_DEFENSE:
	case UNITAI_CITY_COUNTER:
	case UNITAI_CITY_SPECIAL:
	case UNITAI_EXPLORE:
	case UNITAI_MISSIONARY:
	case UNITAI_PROPHET:
	case UNITAI_ARTIST:
	case UNITAI_SCIENTIST:
	case UNITAI_GENERAL:
	case UNITAI_MERCHANT:
	case UNITAI_ENGINEER:
	case UNITAI_GREAT_SPY: // K-Mod
	case UNITAI_SPY:
	case UNITAI_ATTACK_CITY_LEMMING:
		return DOMAIN_LAND;
		break;

	case UNITAI_ICBM:
		return DOMAIN_IMMOBILE;
		break;

	case UNITAI_WORKER_SEA:
	case UNITAI_ATTACK_SEA:
	case UNITAI_RESERVE_SEA:
	case UNITAI_ESCORT_SEA:
	case UNITAI_EXPLORE_SEA:
	case UNITAI_ASSAULT_SEA:
	case UNITAI_SETTLER_SEA:
	case UNITAI_MISSIONARY_SEA:
	case UNITAI_SPY_SEA:
	case UNITAI_CARRIER_SEA:
	case UNITAI_MISSILE_CARRIER_SEA:
	case UNITAI_PIRATE_SEA:
		return DOMAIN_SEA;
		break;

	case UNITAI_ATTACK_AIR:
	case UNITAI_DEFENSE_AIR:
	case UNITAI_CARRIER_AIR:
	case UNITAI_MISSILE_AIR:
		return DOMAIN_AIR;
		break;

	default:
		FAssert(false);
		break;
	}

	return NO_DOMAIN;
}


int CvPlayerAI::AI_yieldWeight(YieldTypes eYield, const CvCity* pCity) const // K-Mod added city argument
{	/*if (eYield == YIELD_PRODUCTION) {
		int iProductionModifier = 100 + (30 * std::max(0, GC.getGame().getCurrentEra() - 1) / std::max(1, (GC.getNumEraInfos() - 2)));
		return (GC.getInfo(eYield).getAIWeightPercent() * iProductionModifier) / 100;
	}
	return GC.getInfo(eYield).getAIWeightPercent();*/ // BtS
	// K-Mod. In the past, this function was always bundled with some code to boost the value of production and food...
	// For simplicity and consistency, I've brought the adjustments into this function.
	PROFILE_FUNC();

	int iWeight = GC.getInfo(eYield).getAIWeightPercent();
	// Note: to reduce confusion, I'd changed all of the xml yield AIWeightPercent values to be 100. (originally they were f:100, p:110, and c:80)
	// I've adjusted the weights here to compenstate for the changes to the xml weights.
	switch (eYield)
	{
	case YIELD_FOOD:
		if (pCity)
		{
			iWeight *= (pCity->happyLevel() - pCity->unhappyLevel(1) >= pCity->foodDifference()) ?
					//370 : 250; (K-Mod)
					320 : 225; // advc.110: Replacing the above (was 300:200 in BtS)
			iWeight /= 100;
		}
		else
		{
			iWeight *= 275; // advc.110: 300 in K-Mod, 250 in BtS
			iWeight /= 100;
		}
		// <advc.110> <advc.115b>
		if(AI_atVictoryStage(AI_VICTORY_DIPLOMACY4))
			iWeight += 25; // </advc.115b>
		// Gradually reduce weight of food in the second half of the game
		else iWeight -= ::round(60 * std::max(0.0,
				GC.getGame().gameTurnProgress() - 0.5));
		// </advc.110>
		break;
	case YIELD_PRODUCTION:
		iWeight *= 220; // advc.110: 270 in K-Mod, 200 in BtS
		iWeight /= 100;
		// <advc.110>
		if(!canPopRush())
			iWeight += 7; // </advc.110>
		break;
	case YIELD_COMMERCE:
		if (AI_isFinancialTrouble())
			iWeight *= 2;
		break;
	}
	return iWeight;
	// K-Mod end
}


int CvPlayerAI::AI_commerceWeight(CommerceTypes eCommerce, CvCityAI const* pCity) const // advc.003u: param was const CvCity*
{
	PROFILE_FUNC();
	int iWeight;

	iWeight = GC.getInfo(eCommerce).getAIWeightPercent();

	//XXX Add something for 100%/0% type situations
	switch (eCommerce)
	{
	case COMMERCE_RESEARCH:
		if (AI_avoidScience())
		{
			if (isNoResearchAvailable())
			{
				iWeight = 0;
			}
			else
			{
				iWeight /= 8;
			}
		}
		break;
	case COMMERCE_GOLD:
		if (getCommercePercent(COMMERCE_GOLD) >= 80) // originally == 100
		{
			//avoid strikes
			//if (getGoldPerTurn() < -getGold()/100)
			if (getGold()+80*calculateGoldRate() < 0) // k146
			{
				iWeight += 15;
			}
		}
		else if (getCommercePercent(COMMERCE_GOLD) < 25) // originally == 0 (bbai)
		{
			//put more money towards other commerce types
			int iGoldPerTurn = calculateGoldRate(); // K-Mod (the original code used getGoldPerTurn, which is faster, but the wrong number.)
			if (iGoldPerTurn > -getGold()/40)
			{
				iWeight -= 25 - getCommercePercent(COMMERCE_GOLD); // originally 15 (bbai)
				iWeight -= (iGoldPerTurn > 0 && getCommercePercent(COMMERCE_GOLD) == 0) ? 10 : 0; // K-Mod. just a bit extra. (I'd like to compare getGold to AI_goldTarget; but it's too expensive.)
			}
		}
		// K-Mod.
		// (note. This factor is helpful for assigning merchant specialists in the right places, but it causes problems
		// when building wealth. Really we dont' want to scale all gold commerce like this, we only want to scale it when
		// it is independant of the commerce slider.)
		if (pCity != NULL)
		{
			iWeight *= pCity->getTotalCommerceRateModifier(COMMERCE_GOLD);
			iWeight /= AI_averageCommerceMultiplier(COMMERCE_GOLD);
		}
		//
		break;
	case COMMERCE_CULTURE:
		if (pCity != NULL)
		{
			iWeight = pCity->AI_getCultureWeight(); // K-Mod
			FAssert(iWeight >= 0);
		}
		else
		{
			// weight multipliers changed for K-Mod
			if (AI_atVictoryStage(AI_VICTORY_CULTURE3) || getCommercePercent(COMMERCE_CULTURE) >= 90)
			{
				iWeight *= 4;
			}
			else if (AI_atVictoryStage(AI_VICTORY_CULTURE2) || getCommercePercent(COMMERCE_CULTURE) >= 70)
			{
				iWeight *= 3;
			}
			else if (AI_atVictoryStage(AI_VICTORY_CULTURE1) || getCommercePercent(COMMERCE_CULTURE) >= 50)
			{
				iWeight *= 2;
			}
			iWeight *= AI_averageCulturePressure();
			iWeight /= 100;

			// K-Mod
			if(AI_isFocusWar()) // advc.105
			//if(GET_TEAM(getTeam()).getAnyWarPlanCount(true) > 0)
			{
				if (AI_atVictoryStage(AI_VICTORY_CULTURE1))
				{
					iWeight *= 2;
					iWeight /= 3;
				}
				else
				{
					iWeight /= 2;
				}
			}
			// K-Mod end
		}
		break;
	case COMMERCE_ESPIONAGE:
		{
			iWeight = AI_getEspionageWeight(); // K-Mod. (Note: AI_getEspionageWeight use to mean something else.)
		}
		break;

	default:
		break;
	}

	return iWeight;
}

// K-Mod.
// Commerce weight is used a lot for AI decisions, and some commerce weights calculations are quite long.
// Some I'm going to cache some of the values and update them at the start of each turn.
// Currently this function caches culture weight for each city, and espionage weight for the player.
void CvPlayerAI::AI_updateCommerceWeights()  // advc: minor style changes
{
	PROFILE_FUNC();
	// <advc.003m>
	if(isBarbarian())
	{
		FOR_EACH_CITYAI_VAR(c, *this)
			c->AI_setCultureWeight(0);
		AI_setEspionageWeight(0);
		return;
	} // </advc.003m>

	CvGame const& g = GC.getGame();

	// City culture weight.
	int const iLegendaryCulture = g.getCultureThreshold((CultureLevelTypes)(GC.getNumCultureLevelInfos() - 1));
	int const iVictoryCities = g.culturalVictoryNumCultureCities();

	// Use culture slider to decide whether a human player is going for cultural victory
	bool bUseCultureRank = AI_atVictoryStage(AI_VICTORY_CULTURE2) || getCommercePercent(COMMERCE_CULTURE) >= 40;
	bool bC3 = AI_atVictoryStage(AI_VICTORY_CULTURE3) || getCommercePercent(COMMERCE_CULTURE) >= 70;
	bool bWarPlans = AI_isFocusWar(); // advc.105
			//GET_TEAM(getTeam()).getAnyWarPlanCount(true) > 0;

	std::vector<std::pair<int,int> > city_countdown_list; // (turns, city id)
	{
		int iGoldCommercePercent = bUseCultureRank ? AI_estimateBreakEvenGoldPercent() : getCommercePercent(COMMERCE_GOLD);
		// Note: we only need the gold commerce percent if bUseCultureRank;
		// but I figure that I might as well put in a useful placeholder value just in case.

		FOR_EACH_CITY(pLoopCity, *this)
		{
			int iCountdown = -1;
			if (bUseCultureRank)
			{
				// K-Mod. Try to tell us what the culture would be like if we were to turn up the slider. (cf. AI_calculateCultureVictoryStage)
				// (current culture rate, minus what we're current getting from raw commerce, plus what we would be getting from raw commerce at maximum percentage.)
				int iEstimatedRate = pLoopCity->getCommerceRate(COMMERCE_CULTURE);
				iEstimatedRate += (100 - iGoldCommercePercent - getCommercePercent(COMMERCE_CULTURE)) * pLoopCity->getYieldRate(YIELD_COMMERCE) * pLoopCity->getTotalCommerceRateModifier(COMMERCE_CULTURE) / 10000;
				iCountdown = (iLegendaryCulture - pLoopCity->getCulture(getID())) / std::max(1, iEstimatedRate);
			}
			city_countdown_list.push_back(std::make_pair(iCountdown, pLoopCity->getID()));
		}
	}
	std::sort(city_countdown_list.begin(), city_countdown_list.end());

	int const iGameTurn = g.getGameTurn();
	FAssert(city_countdown_list.size() == getNumCities());
	for (size_t i = 0; i < city_countdown_list.size(); i++)
	{
		CvCityAI* pCity = AI_getCity(city_countdown_list[i].second);

		// COMMERCE_CULTURE AIWeightPercent is set to 30% in the current xml.
		int iWeight = GC.getInfo(COMMERCE_CULTURE).getAIWeightPercent();

		int iPressureFactor = pCity->AI_culturePressureFactor();
		if (AI_atVictoryStage(AI_VICTORY_CULTURE2))
			iPressureFactor = std::min(300, iPressureFactor); // don't let culture pressure dominate our decision making about where to put our culture.
		int iPressureWeight = iWeight * (iPressureFactor-100) / 100;

		if (pCity->getCulture(getID()) >= iLegendaryCulture)
		{
			iWeight /= 10;
		}
		else if (bUseCultureRank)
		{
			int iCultureRateRank = i+1; // an alias, used for clarity.

			// if one of the currently best cities, then focus hard
			if (iCultureRateRank <= iVictoryCities)
			{
				if (bC3)
				{
					// culture3
					iWeight *= 2 + 2*iCultureRateRank + (iCultureRateRank == iVictoryCities ? 1 : 0);
				}
				else
				{
					// culture2
					iWeight *= 6 + iCultureRateRank;
					iWeight /= 2;
				}
				// scale the weight, just in case we're playing on a mod with a different number of culture cities.
				iWeight *= 3;
				iWeight /= iVictoryCities;
			}
			// if one of the 3 close to the top, then still emphasize culture some
			else if (iCultureRateRank <= iVictoryCities + 3)
			{
				//iWeight *= bC3 ? 4 : 3;
				iWeight *= 3;
			}
			else
			{
				iWeight *= 2;
			}
		}
		else if (AI_atVictoryStage(AI_VICTORY_CULTURE1) || getCommercePercent(COMMERCE_CULTURE) >= 30)
		{
			iWeight *= 2;
		}
		// K-Mod. Devalue culture in cities which really have no use for it.
		else if (iPressureFactor < 110)
		{
			// if we're not running a culture victory strategy
			// and if we're well into the game, and this city has ample culture, and we don't have any culture pressure...
			// then culture probably isn't worth much to us at all.

			// don't reduce the value if we are still on-track for a potential cultural victory.
			if (getCurrentEra() > GC.getNumEraInfos()/2 && pCity->getCultureLevel() >= 3)
			{
				int iCultureProgress = pCity->getCultureTimes100(getID()) / std::max(1, iLegendaryCulture);
				int iTimeProgress = 100 * iGameTurn / g.getEstimateEndTurn();
				iTimeProgress *= iTimeProgress;
				iTimeProgress /= 100;

				if (iTimeProgress > iCultureProgress)
				{
					int iReductionFactor = 100 + std::min(10, 110 - iPressureFactor) * (iTimeProgress - iCultureProgress) / 2;
					FAssert(iReductionFactor >= 100 && iReductionFactor <= 100 + 10 * 100/2);
					iWeight *= 100;
					iWeight /= iReductionFactor;
				}
			}
		}

		if (bWarPlans)
		{
			if (AI_atVictoryStage(AI_VICTORY_CULTURE1))
			{
				iWeight *= 2;
				iWeight /= 3;
			}
			else
			{
				iWeight /= 2;
			}
		}

		iWeight += iPressureWeight;

		// Note: value bonus for the first few points of culture is determined elsewhere
		// so that we don't screw up the evaluation of limited culture bonuses such as national wonders.
		pCity->AI_setCultureWeight(iWeight);
	} // end culture

	//
	// Espionage weight
	//
	{	// <k146>
		/*  For human players, what amount of espionage weight indicates that we
			care about a civ? */
		int iWeightThreshold = 0;
		if (isHuman())
		{
			int iTotalWeight = 0;
			int iTeamCount = 0;
			for (int iTeam = 0; iTeam < MAX_CIV_TEAMS; ++iTeam)
			{
				if (GET_TEAM(getTeam()).isHasMet((TeamTypes)iTeam) &&
						GET_TEAM((TeamTypes)iTeam).isAlive())
				{
					iTotalWeight += getEspionageSpendingWeightAgainstTeam((TeamTypes)iTeam);
					iTeamCount++;
				}
			}

			if (iTeamCount > 0)
			{
				/*  pretty arbitrary. But as an example, 90 points on 1 player out
					of 10 -> threshold is 5. */
				iWeightThreshold = iTotalWeight / (iTeamCount + 8);
			}
		} // </k146>

		int iWeight = GC.getInfo(COMMERCE_ESPIONAGE).getAIWeightPercent();

		int iEspBehindWeight = 0;
		int iEspAttackWeight = 0;
		int iAllTeamTotalPoints = 0;
		int iTeamCount = 0;
		int iLocalTeamCount = 0;
		int iTotalUnspent = 0;

		CvTeamAI& kOurTeam = GET_TEAM(getTeam());
		for (int iI = 0; iI < MAX_CIV_TEAMS; iI++)
		{
			TeamTypes eLoopTeam = (TeamTypes)iI;
			CvTeamAI& kLoopTeam = GET_TEAM(eLoopTeam);
			if (!kLoopTeam.isAlive() || eLoopTeam == getTeam() || !kOurTeam.isHasMet(eLoopTeam) ||
					kLoopTeam.isVassal(getTeam()) || kOurTeam.isVassal(eLoopTeam))
				continue;
			iAllTeamTotalPoints += kLoopTeam.getEspionagePointsEver();
			iTeamCount++;

			int iTheirPoints = kLoopTeam.getEspionagePointsAgainstTeam(getTeam());
			int iOurPoints = kOurTeam.getEspionagePointsAgainstTeam(eLoopTeam);
			iTotalUnspent += iOurPoints;
			int iAttitude = ::range(kOurTeam.AI_getAttitudeVal(eLoopTeam), -12, 12);
			iTheirPoints -= (iTheirPoints*iAttitude)/(2*12);

			if (iTheirPoints > iOurPoints && (!isHuman() ||
					getEspionageSpendingWeightAgainstTeam(eLoopTeam) >
					iWeightThreshold)) // k146
			{
				iEspBehindWeight += 1;
				AttitudeTypes eAttitude = /* <advc.120> */ (kLoopTeam.isHuman() ?
						kOurTeam.AI_getAttitude(eLoopTeam) :
						kLoopTeam.AI_getAttitude(getTeam())); /* </advc.120> */
				if (eAttitude <= ATTITUDE_CAUTIOUS
						&& kOurTeam.AI_hasCitiesInPrimaryArea(eLoopTeam)
						// advc.120: Don't worry so much about espionage while at war
						&& !kOurTeam.isAtWar(eLoopTeam))
					iEspBehindWeight += 1;
			}
			if (kOurTeam.AI_hasCitiesInPrimaryArea(eLoopTeam))
			{
				iLocalTeamCount++;
				if (kOurTeam.AI_getAttitude(eLoopTeam) <= ATTITUDE_ANNOYED ||
					kOurTeam.AI_getWarPlan(eLoopTeam) != NO_WARPLAN)
				{
					iEspAttackWeight += 1;
				}
				if (AI_isDoStrategy(AI_STRATEGY_BIG_ESPIONAGE) &&
						GET_PLAYER(kLoopTeam.getLeaderID()).getTechScore() > getTechScore())
				{
					iEspAttackWeight += 1;
				}
			}
		}
		if (iTeamCount == 0)
		{
			/*  can't put points on anyone - but accumulating "total points ever" is
				still better than nothing. */
			iWeight /= 10;
		}
		else
		{
			int const iOurPtsEver = kOurTeam.getEspionagePointsEver(); // advc
			iAllTeamTotalPoints /= std::max(1, iTeamCount); // Get the average total points
			iWeight *= std::min(iOurPtsEver + 3*iAllTeamTotalPoints + 16*iGameTurn,
					5*iAllTeamTotalPoints);
			iWeight /= std::max(1, 4 * iAllTeamTotalPoints);
			// lower weight if we have spent less than a third of our total points
			if (getCommercePercent(COMMERCE_ESPIONAGE) == 0) // ...only if we aren't explicitly trying to get espionage.
			{
				iWeight *= 100 - 270*std::max(iTotalUnspent -
						std::max(2*iOurPtsEver/3, 8*iGameTurn), 0)/std::max(1,iOurPtsEver);
				iWeight /= 100;
			}
			//
			if (AI_isDoStrategy(AI_STRATEGY_BIG_ESPIONAGE))
			{
				iWeight *= 3;
				iWeight /= 2;
			}
			iWeight *= 2*(iEspBehindWeight+iEspAttackWeight) + 3*iTeamCount/4 + 2;

			iWeight *= isHuman() ? 100 : GC.getInfo(getPersonalityType()).
					getEspionageWeight();
			iWeight /= (iLocalTeamCount + iTeamCount)/2 + 2;
			iWeight /= 100;

			if (AI_isDoStrategy(AI_STRATEGY_ESPIONAGE_ECONOMY) ||
					(isHuman() && getCommercePercent(COMMERCE_ESPIONAGE) >= 60))
			{
				iWeight = std::max(iWeight, GC.getInfo(COMMERCE_RESEARCH).
						getAIWeightPercent());
			}
			else
			{
				iWeight *= 100 + 2*getCommercePercent(COMMERCE_ESPIONAGE);
				iWeight /= 110;
			}
		}
		AI_setEspionageWeight(iWeight);
	} // end espionage
} // K-Mod


short CvPlayerAI::AI_foundValue(int iX, int iY, int iMinRivalRange, bool bStartingLoc) const
{
	CitySiteEvaluator eval(*this, iMinRivalRange, bStartingLoc);
	return eval.evaluate(iX, iY);
}

bool CvPlayerAI::AI_isAreaAlone(CvArea const& kArea) const
{
	// <advc.131> Don't cheat with visibility early on (hurts more than it helps)
	if(getCurrentEra() <= GC.getGame().getStartEra())
	{
		int iRevealed = kArea.getNumRevealedTiles(getTeam());
		int iTotal = kArea.getNumTiles();
		if(3 * iRevealed < iTotal) // Default assumption: not alone
			return false;
		if(4 * iRevealed < 3 * iTotal)
			return (GET_TEAM(getTeam()).getHasMetCivCount() <= 0);
	} // </advc.131>
	return ((kArea.getNumCities() - GET_TEAM(BARBARIAN_TEAM).countNumCitiesByArea(kArea)) ==
			GET_TEAM(getTeam()).countNumCitiesByArea(kArea));
}


bool CvPlayerAI::AI_isCapitalAreaAlone() const
{
	CvCity* pCapitalCity = getCapitalCity();
	if (pCapitalCity != NULL)
		return AI_isAreaAlone(pCapitalCity->getArea());
	return false;
}


bool CvPlayerAI::AI_isPrimaryArea(CvArea const& kArea) const
{
	if (kArea.isWater())
		return false;

	if (kArea.getCitiesPerPlayer(getID()) > 2)
		return true;

	CvCity* pCapitalCity = getCapitalCity(); // advc
	if (pCapitalCity != NULL)
	{
		if (pCapitalCity->isArea(kArea))
			return true;
	}

	return false;
}


int CvPlayerAI::AI_militaryWeight(CvArea const* pArea) const
{
	return pArea != NULL
		? pArea->getPopulationPerPlayer(getID()) + pArea->getCitiesPerPlayer(getID()) + 1
		: getTotalPopulation() + getNumCities() + 1; // K-Mod: count all areas
}

// This function has been edited by Mongoose, then by jdog5000, and then by me (karadoc). Some changes are marked, others are not.
int CvPlayerAI::AI_targetCityValue(CvCity const* pCity, bool bRandomize, bool bIgnoreAttackers) const  // advc: const CvCity*, some style changes
{
	PROFILE_FUNC();
	FAssertMsg(pCity != NULL, "City is not assigned a valid value");

	CvGame& g = GC.getGame();
	CvPlayerAI const& kOwner = GET_PLAYER(pCity->getOwner());
	//int iValue = 1 + pCity->getPopulation() * (50 + pCity->calculateCulturePercent(getID())) / 100;
	int iValue = 5 + pCity->getPopulation() * (100 + pCity->calculateCulturePercent(getID())) / 150; // K-Mod (to dilute the other effects)

	if (pCity->isCoastal())
		iValue += 2;

	// advc.104d: Replacing the BBAI code below (essentially with K-Mod code)
	iValue += AI_cityWonderVal(*pCity);
	/*iValue += 4*pCity->getNumActiveWorldWonders();
	for (int iI = 0; iI < GC.getNumReligionInfos(); iI++) {
		if (pCity->isHolyCity((ReligionTypes)iI)) {
			iValue += 2 + ((GC.getGame().calculateReligionPercent((ReligionTypes)iI)) / 5);
			if (kOwner.getStateReligion() == iI)
				iValue += 2;
			if (getStateReligion() == iI)
				iValue += 8;
		}
	}*/
	// <cdtw.2>
	if(pCity->getPlot().defenseModifier(pCity->getTeam(), false) <=
		GC.getDefineINT(CvGlobals::CITY_DEFENSE_DAMAGE_HEAL_RATE))
	{
		if(AI_isDoStrategy(AI_STRATEGY_AIR_BLITZ) || AI_isDoStrategy(AI_STRATEGY_LAND_BLITZ))
			iValue += 6;
		else if(AI_isDoStrategy(AI_STRATEGY_FASTMOVERS))
			iValue += 3;
		else iValue++;
	} // </cdtw.2>
	if (pCity->isEverOwned(getID()))
	{
		iValue += 4;
		if (pCity->getOriginalOwner() == getID())
			iValue += 2;
	}

	if (!bIgnoreAttackers)
		iValue += std::min(8, (AI_adjacentPotentialAttackers(*pCity->plot()) + 2) / 3);

	for (CityPlotIter it(*pCity); it.hasNext(); ++it)
	{
		CvPlot const& kPlot = *it;
		if (kPlot.getBonusType(getTeam()) != NO_BONUS &&
			kPlot.getWorkingCity() == pCity) // advc.104d
		{
			iValue += std::max(1, AI_bonusVal(kPlot.getBonusType(getTeam()),
					1, true) / 5);
		}
		if (kPlot.getOwner() == getID())
			iValue++;
		if (kPlot.isAdjacentPlayer(getID(), true))
			iValue++;
	}
	bool bThwartVictory = false; // advc.104d
	if (kOwner.AI_atVictoryStage(AI_VICTORY_CULTURE3))
	{
		if (pCity->getCultureLevel() >= g.culturalVictoryCultureLevel() - 1)
		{
			iValue += 10; // advc.104d: Was 15; will reduce distance penalty instead.
			if (kOwner.AI_atVictoryStage(AI_VICTORY_CULTURE4))
			{
				iValue += 15; // advc.104d: was 25
				if (pCity->getCultureLevel() >= (g.culturalVictoryCultureLevel()) ||
						// K-Mod:
						pCity->findCommerceRateRank(COMMERCE_CULTURE) <=
						g.culturalVictoryNumCultureCities())
					iValue += 50; // advc.104d: Was 60 in K-Mod, 10 in BBAI.
			}
			bThwartVictory = true; // advc.104d
		}
	}

	if (kOwner.AI_atVictoryStage(AI_VICTORY_SPACE3))
	{
		if (pCity->isCapital())
		{	// <advc.104d>
			if(!AI_atVictoryStage4()) // Don't worry yet if we're ahead
			{
				iValue += 5; // Was 10; will reduce distance penalty instead.
				bThwartVictory = true;
			} // </advc.104d>
			if (kOwner.AI_atVictoryStage(AI_VICTORY_SPACE4))
			{
				iValue += 10; // was 20
				if (GET_TEAM(pCity->getTeam()).getVictoryCountdown(g.getSpaceVictory()) >= 0)
					iValue += 100; // was 30
				bThwartVictory = true; // advc.104d
			}
		} // <advc.104d> Against Space3, taking any high-production cities helps.
		else if(!kOwner.AI_atVictoryStage(AI_VICTORY_SPACE4) &&
			!AI_atVictoryStage4() &&
			pCity->findYieldRateRank(YIELD_PRODUCTION) < std::min(5, kOwner.getNumCities() / 4))
		{
			iValue += 3;
			bThwartVictory = true;
		} // </advc.104d>
	} // <advc.104d> Target the capital area of civs aiming at a peaceful victory
	if(kOwner.AI_atVictoryStage(AI_VICTORY_CULTURE2 | AI_VICTORY_SPACE2) &&
		!kOwner.AI_atVictoryStage3() && !AI_atVictoryStage3())
	{
		CvCity* pTargetCapital = kOwner.getCapitalCity();
		if(pTargetCapital != NULL && pTargetCapital->sameArea(*pCity))
			bThwartVictory = true;
	} // </advc.104d>

	CvMap const& m = GC.getMap();
	CvCity* pNearestCity = m.findCity(pCity->getX(), pCity->getY(), getID());
	if (pNearestCity != NULL)
	{
		// Now scales sensibly with map size, on large maps this term was incredibly dominant in magnitude
		int iTempValue = 30;
		int iPathDist = m.calculatePathDistance(pNearestCity->plot(), pCity->plot());
		// <advc.104d>
		if(bThwartVictory)
			iPathDist /= 2; // </advc.104d>
		iTempValue *= std::max(1, m.maxStepDistance() * 2 - iPathDist);
		iTempValue /= std::max(1, m.maxStepDistance() * 2);
		iValue += iTempValue;
	}

	if (bRandomize)
		iValue += g.getSorenRandNum((pCity->getPopulation() / 2) + 1, "AI Target City Value");

	// K-Mod.
	//if (pCity->getHighestPopulation() < 1)
	// Usually this means the city would be auto-razed.
	// (We can't use isAutoRaze for this, because that assumes the city is already captured.)
	// dlph.29 (bugfix):
	if (pCity->getHighestPopulation() == 1 && !g.isOption(GAMEOPTION_NO_CITY_RAZING))
		iValue = (iValue + 2) / 3;
	// K-Mod end
	return iValue;
}

/*  <advc.104d> Adopted from K-Mod's AI_warSpoilsValue. I'm not calling
	AI_cityWonderVal from there though b/c advc.104 bypasses startWarVal,
	i.e. AI_warSpoilsValue is legacy code. Also, AI_warSpoilsVal is a team-level
	function, but I need this to be a civ-level function so that the
	city owner's state religion can be factored in. */
int CvPlayerAI::AI_cityWonderVal(CvCity const& c) const
{
	CvGame& g = GC.getGame();
	int r = 0;
	for(int i = 0; i < GC.getNumReligionInfos(); i++)
	{
		ReligionTypes eReligion = (ReligionTypes)i;
		if(c.isHolyCity(eReligion))
		{
			int iReligionCities = g.countReligionLevels(eReligion);
			/*  Account for cities we may still convert to our state religion.
				Hard to estimate. If we have a lot of cities, some of them may
				still not have the religion. Or if they do, we can use these
				cities to build missionaries to convert foreign cities. */
			if(getStateReligion() == eReligion)
				iReligionCities += ::round(getNumCities() / 2.5);
			/* K-Mod comment: "the -4 at the end is mostly there to offset the
				'wonder' value that will be added later. I don't want to double-count
				the value of the shrine, and the religion [the holy city?]
				without the shrine isn't worth much anyway." */
			r += std::max(0, iReligionCities / (c.hasShrine(eReligion) ? 1 : 2) - 4);
		}
	}
	for(int i = 0; i < GC.getNumCorporationInfos(); i++)
	{
		CorporationTypes corp = (CorporationTypes)i;
		if(c.isHeadquarters(corp))
			r += std::max(0, 2 * g.countCorporationLevels(corp) - 4);
	}
	r += 4 * c.getNumActiveWorldWonders(MAX_INT, getID());
	return r;
} // </advc.104d>


CvCityAI* CvPlayerAI::AI_findTargetCity(CvArea const& kArea) const // advc.003u: return CvCityAI; advc: some style changes
{
	CvCityAI* pBestCity = NULL;
	int iBestValue = 0;
	for (int iI = 0; iI < MAX_CIV_PLAYERS; iI++)
	{
		CvPlayerAI const& kTargetPlayer = GET_PLAYER((PlayerTypes)iI);
		if(!kTargetPlayer.isAlive())
			continue;
		// <advc.001>
		if(isBarbarian() && kArea.isBorderObstacle(kTargetPlayer.getTeam()))
			continue; // </advc.001>
		if(!GET_TEAM(getTeam()).AI_mayAttack(kTargetPlayer.getTeam()))
			continue;
		FOR_EACH_CITYAI_VAR(pLoopCity, kTargetPlayer)
		{
			if (pLoopCity->isArea(kArea))
			{
				int iValue = AI_targetCityValue(pLoopCity, true);
				if (iValue > iBestValue)
				{
					iBestValue = iValue;
					pBestCity = pLoopCity;
				}
			}
		}
	}
	return pBestCity;
}

/*	advc: Merged AI_getAnyPlotDamnger (now named "AI_isAnyPlotDanger") into AI_getPlotDanger.
	I haven't bothered with AI_isPlotThreatened and AI_getWaterDanger, which are also similar. */
int CvPlayerAI::AI_getPlotDanger(/* BtS parameters: */ CvPlot const& kPlot, int iRange, bool bTestMoves,
	/*  Stop counting at iLimit, i.e. the return value can be at most iLimit.
		When pLowHPCounter is used, stop only when pLowHPCounter also reaches iLimit. */
	int iLimit,
	bool bCheckBorder, // K-Mod
	// <advc.104>
	/*  Counts enemy units in range with at most iMaxHP hit points. Not counted if NULL.
		(This function's return value includes all units regardless of their health.) */
	LowHPCounter* pLowHPCounter,
	// Unless NO_PLAYER, count only danger from eAttackPlayer.
	PlayerTypes eAttackPlayer) const
{
	FAssert(iLimit > 0);
	// Not supported (just out of convenience; not needed I think)
	FAssert(!bCheckBorder || pLowHPCounter == NULL); // </advc.104>
	FAssert(iRange != 0); // advc: Call AI_countDangerousUnits instead
	PROFILE_FUNC();

	if(iRange == -1)
		iRange = DANGER_RANGE;
	/*if (bTestMoves && isTurnActive()) {
		if (iRange <= DANGER_RANGE && pPlot->getActivePlayerNoDangerCache())
			return false; }*/ // BBAI
	/*if(bTestMoves && isSafeRangeCacheValid() && iRange <= kPlot.getActivePlayerSafeRangeCache())
		return false;*/ // K-Mod
	// advc.opt: Since the SafeRangeCache is disabled, let's not waste any time with this.

	TeamTypes const eTeam = getTeam();
	int r = 0;
	bCheckBorder = (bCheckBorder &&
			// advc: Cities were excluded in AI_getAnyPlotDanger, but not in AI_getPlotDanger.
			// K-Mod: "Cities need to be excluded for some legacy AI code"
			// K-Mod: "Still count border danger in cities - because I want it for AI_cityThreat"
			(!kPlot.isCity() || iLimit > 1) &&
			/*	advc.300: Let civs not worry about Barbarian borders and vice versa
				No need then to check for border danger at peacetime. */
			GET_TEAM(getTeam()).getNumWars(false) > 0 && !isBarbarian() &&
			//bool bCheckBorder = (!isHuman() && !kPlot.isCity());
			/*  K-Mod. I don't want auto-workers on the frontline.
				So count border danger for humans too, unless the plot is defended. */
			(!isHuman() || !kPlot.plotCheck(PUF_canDefend, -1, -1, getID(), NO_TEAM))); // advc.opt: was plotCount
	if(bCheckBorder)
	{
		//if (iRange >= DANGER_RANGE && kPlot.isTeamBorderCache(eTeam))
		// K-Mod. border danger doesn't count anything further than range 2.
		if(iRange >= BORDER_DANGER_RANGE && kPlot.getBorderDangerCache(eTeam))
		{
			// <advc>
			if (iLimit == 1)
				return 1;
			r++; // As in K-Mod, border danger can add at most 1 to the danger count.
			// (K-Mod: "I don't think two border tiles are really more dangerous than one border tile.")
			bCheckBorder = false; // </advc>
		}
	}
	CvArea const& kPlotArea = kPlot.getArea();
	// advc: The iterator is a little slower and we don't benefit here from a spiral pattern
	//for (SquareIter it(kPlot, iRange); it.hasNext(); ++it) { CvPlot const& p = *it;
	for (int iDX = -iRange; iDX <= iRange; iDX++)
	for (int iDY = -iRange; iDY <= iRange; iDY++)
	{
		CvPlot const* pp = plotXY(&kPlot, iDX, iDY);
		if (pp == NULL) continue;
		CvPlot const& p = *pp;
		if (p.isArea(kPlotArea))
		{
			if (bCheckBorder)
			{
				//if (p.getTeam() != NO_TEAM && GET_TEAM(getTeam()).isAtWar(p.getTeam())
				// <advc.001i>
				PlayerTypes const eRevealedLoopPlayer = p.getRevealedOwner(eTeam);
				if (eRevealedLoopPlayer != NO_PLAYER &&
					GET_TEAM(getTeam()).isAtWar(TEAMID(eRevealedLoopPlayer)) &&
					// </advc.001i>
					eRevealedLoopPlayer != BARBARIAN_PLAYER) // advc.300
				{
					TeamTypes const eRevealedLoopTeam = TEAMID(eRevealedLoopPlayer);
					int const iDistance = stepDistance(&kPlot, &p);
					//if (iDistance == 1)
					// <advc> Let's also cache border danger when kPlot itself is hostile
					BOOST_STATIC_ASSERT(BORDER_DANGER_RANGE >= 1);
					bool const bInRange = (iDistance < BORDER_DANGER_RANGE);
					bool const bInRangeThroughRoute = (iDistance == BORDER_DANGER_RANGE &&
							// advc.001i:
							p./*isRoute()*/getRevealedRouteType(eTeam) != NO_ROUTE);
					if (bInRange || bInRangeThroughRoute) // </advc>
					{
						kPlot.setBorderDangerCache(eTeam, true);
						// p is in enemy territory, so this is fine.
						p.setBorderDangerCache(eTeam, true);
						// BBAI: "Border cache is reversible, set for both team and enemy."
						/*	K-Mod: reversible my arse.
							only set the cache for eLoopTeam if kPlot is owned by us!
							(ie. owned by their enemy) */
						// <advc.001i> Only if eLoopTeam knows that we own it
						TeamTypes eActualLoopTeam = p.getTeam();
						if (eActualLoopTeam != NO_TEAM)
						{
							PlayerTypes eRevealedPlotOwner = kPlot.getRevealedOwner(
									eActualLoopTeam);
							if (eRevealedPlotOwner != NO_PLAYER &&
								TEAMID(eRevealedPlotOwner) == eTeam && // </advc.001i>
								(bInRange || (kPlot.//isRoute()
								// advc.001i:
								getRevealedRouteType(eActualLoopTeam) != NO_ROUTE &&
								kPlot.getTeam() == eTeam)))
							{
								p.setBorderDangerCache(eActualLoopTeam, true);
								kPlot.setBorderDangerCache(eActualLoopTeam, true);
							}
						}  // <advc>
						r++;
						if (r >= iLimit)
							return iLimit;
						// Count at most 1 for border danger
						bCheckBorder = false; 
					}
				}
			}
			if (p.isUnit()) // Redundant but fast (inlined)
			{
				// Code moved into auxiliary function
				r += AI_countDangerousUnits(p, kPlot, bTestMoves, iLimit, pLowHPCounter, eAttackPlayer);
				if (r >= iLimit)
				{
					if (pLowHPCounter == NULL || pLowHPCounter->get() >= iLimit)
						return iLimit;
				}
			} // </advc>
		}
		/*	<advc.030> Same-area no longer rules out a (visible) submarine -
			but is this really ever going to be a problem? */
		/*else if (p.isUnit() && kPlotArea.canBeEntered(p.getArea()))
		{
			r += AI_countDangerousUnits(p, kPlot, bTestMoves, 1, eAttackPlayer, pLowHPCounter);
			// ... (copy from above)
		}*/ // </advc.030>
	}

	/*	The test moves case is a strict subset of the more general case,
		either is appropriate for setting the cache.  However, since the test moves
		case is called far more frequently, it is more important and the cache
		value being true is only assumed to mean that the plot is safe in the
		test moves case. */
	/*if (bTestMoves) {
		if (isTurnActive()) {
			if (!GC.getGame().isMPOption(MPOPTION_SIMULTANEOUS_TURNS) && GC.getGame().getNumGameTurnActive() == 1)
				kPlot.setActivePlayerNoDangerCache(true);
		}
	}*/ // BBAI
	/*	K-Mod. The above bbai code is flawed in that it flags the plot as safe regardless
		of what iRange is and then reports that the plot is safe for any iRange <= DANGER_RANGE. */
	/*if (isSafeRangeCacheValid() && iRange > kPlot.getActivePlayerSafeRangeCache())
		kPlot.setActivePlayerSafeRangeCache(iRange);*/ // advc.opt: SafeRangeCache is disabled
	return std::min(r, iLimit); // advc.104: May have counted past the limit
}

// advc: from AI_getAnyPlotDanger
int CvPlayerAI::AI_countDangerousUnits(CvPlot const& kAttackerPlot, CvPlot const& kDefenderPlot,
	bool bTestMoves, int iLimit, /* <advc.104> */ LowHPCounter* pLowHPCounter,
	PlayerTypes eAttackPlayer) const // </advc.104>
{
	TeamTypes const eTeam = getTeam();
	// <advc.128>
	if (!kAttackerPlot.isVisible(eTeam))
	{
		if(isHuman() || !AI_cheatDangerVisibility(kAttackerPlot))
			return 0;
	} // </advc.128>
	int r = 0;
	TeamTypes const eOurMaster = GET_TEAM(eTeam).getMasterTeam(); // advc.opt
	for (CLLNode<IDInfo> const* pUnitNode = kAttackerPlot.headUnitNode();
		pUnitNode != NULL; pUnitNode = kAttackerPlot.nextUnitNode(pUnitNode))
	{
		CvUnit const& kUnit = *::getUnit(pUnitNode->m_data);
		// advc.opt: Team check changed to masterTeam
		if (GET_TEAM(kUnit.getTeam()).getMasterTeam() == eOurMaster)
		{
			if (!kUnit.alwaysInvisible() &&
				kUnit.getInvisibleType() == NO_INVISIBLE)
			{
				FAssertMsg(r == 0, "Hostile units shouldn't be able to coexist in a plot");
				return r;
			}
		} 
		// <advc.104>
		if (((eAttackPlayer != NO_PLAYER && kUnit.getCombatOwner(
			eTeam, kAttackerPlot) == eAttackPlayer) || // </advc.104>
			/*	advc.001: Was kUnit.getPlot(). Only matters if kUnit is alwaysHostile
				and kDefenderPlot is a city or fort. */
			kUnit.isEnemy(eTeam, kDefenderPlot)) &&
			// advc.315: was pLoopUnit->canAttack()
			AI_canBeAttackedBy(kUnit) &&
			!kUnit.isInvisible(eTeam, false) &&
			kUnit.canMoveOrAttackInto(kDefenderPlot,
			false, true)) // advc.001k
		{
			if (bTestMoves)
			{
				int const iDistance = stepDistance(&kAttackerPlot, &kDefenderPlot);
				// <advc.128> Take the time to compute a path in important cases
				if (iDistance <= 3 && (isHuman() ||
					// Prevent sneak attacks by human Woodsmen and Guerilla
					(kUnit.isHuman() && kAttackerPlot.isVisible(eTeam) &&
					getCurrentEra() <= 1)))
				{
					if (!kUnit.generatePath(&kDefenderPlot,
						MOVE_MAX_MOVES | MOVE_IGNORE_DANGER, false, NULL, 1, true))
					{
						continue;
					}
				}
				else // </advc.128>
				{
					int iAttackerRange = kUnit.baseMoves();
					if (kAttackerPlot.isValidRoute(&kUnit, /* advc.001i: */ false))
						iAttackerRange++;
					if (iAttackerRange < iDistance)
						continue;
				}
			}
			r++;
			// <advc.104>
			if (pLowHPCounter != NULL &&
				kUnit.currHitPoints() <= pLowHPCounter->getHPThreshold())
			{
				pLowHPCounter->increment();
				if (pLowHPCounter->get() >= iLimit)
					return iLimit;
				continue; // Keep counting low-health units
			} // </advc.104>
			if (r >= iLimit)
				return iLimit;
		}
	}
	return std::min(r, iLimit); // advc.104: May have counted past the limit
}

// Never used ...
/*int CvPlayerAI::AI_getUnitDanger(CvUnit* pUnit, int iRange, bool bTestMoves, bool bAnyDanger) const
{
	// advc: Unused code deleted (for now)
}*/
// BETTER_BTS_AI_MOD: END

int CvPlayerAI::AI_getWaterDanger(CvPlot* pPlot, int iRange, bool bTestMoves) const  // advc: some style changes
{
	PROFILE_FUNC();

	if (iRange == -1)
		iRange = DANGER_RANGE;

	int iCount = 0;
	for (SquareIter it(*pPlot, iRange); it.hasNext(); ++it)
	{
		CvPlot const& p = *it;
		if (!p.isWater() || /* advc.opt: */ !p.isUnit() ||
			!p.isAdjacentToArea(p.getArea()))
		{
			continue;
		}
		CLLNode<IDInfo> const* pUnitNode = p.headUnitNode();
		// <advc.128>
		if (pUnitNode != NULL && !p.isVisible(getTeam()))
		{
			if(isHuman() || !AI_cheatDangerVisibility(p))
				continue;
		} // </advc.128>
		for (; pUnitNode != NULL; pUnitNode = p.nextUnitNode(pUnitNode))
		{
			CvUnit* pEnemyUnit = ::getUnit(pUnitNode->m_data);
			if (!pEnemyUnit->isEnemy(getTeam(), *pPlot))
				continue;

			// advc.315: was pLoopUnit->canAttack()
			if (AI_canBeAttackedBy(*pEnemyUnit) && !pEnemyUnit->isInvisible(getTeam(), false))
				iCount++;
		}
	}
	return iCount;
}

// rewritten for K-Mod
bool CvPlayerAI::AI_avoidScience() const
{
	if (isCurrentResearchRepeat() && (!isHuman() || getCommercePercent(COMMERCE_RESEARCH) <= 20))
		return true;

	if (isNoResearchAvailable())
		return true;

	return false;
}

/*  advc.110: Body cut from AI_isFinancialTrouble. Returns the difference between
	funds and safety threshold. */
int CvPlayerAI::AI_financialTroubleMargin() const
{
	if(isBarbarian()) // Based on BETTER_BTS_AI_MOD, 06/12/09, jdog5000 (Barbarian AI)
		return 100;
	/*if (getCommercePercent(COMMERCE_GOLD) > 50) {
		int iNetCommerce = 1 + getCommerceRate(COMMERCE_GOLD) + getCommerceRate(COMMERCE_RESEARCH) + std::max(0, getGoldPerTurn());
		int iNetExpenses = calculateInflatedCosts() + std::min(0, getGoldPerTurn());*/
	int iNetCommerce = AI_getAvailableIncome(); // K-Mod
	int iNetExpenses = calculateInflatedCosts() + std::max(0, -getGoldPerTurn()); // unofficial patch
	// <advc.110> No trouble at the start of the game
	if(iNetExpenses <= 0 && !isFoundedFirstCity())
		return false; // </advc.110>
	int iFundedPercent = (100 * (iNetCommerce - iNetExpenses)) / std::max(1, iNetCommerce);
	int iSafePercent = 35; // was 40
	// <advc.110> Don't want the AI to expand rapidly in the early game
	int iEra = getCurrentEra();
	if(iEra <= 0)
		iSafePercent = 60;
	else if(iEra == 1)
		iSafePercent = 50;
	else if(iEra == 2)
		iSafePercent = 40;
	/*	Koshling: We're never in financial trouble if we can run at current deficits
		for more than 50 turns and stay in healthy territory (EraGoldThreshold) */
	// advc: Take the era number times 1.5
	int iEraGoldThreshold = 100 * (1 + (3 * iEra) / 2);
	if(getGold() > iEraGoldThreshold && (iNetCommerce-iNetExpenses >= 0 ||
			getGold() + 50 * (iNetCommerce-iNetExpenses) > iEraGoldThreshold))
		return 100; // Would be nicer to increase iFundedPercent based on getGold
	// </advc.110>

	if(AI_avoidScience())
		iSafePercent -= 8;

	//if (GET_TEAM(getTeam()).getAnyWarPlanCount(true))
	if(AI_isFocusWar()) // advc.105
		iSafePercent -= 10; // was 12

	if(isCurrentResearchRepeat())
		iSafePercent -= 10;
	// K-Mod
	int iCitiesTarget = std::max(1, GC.getInfo(GC.getMap().getWorldSize()).getTargetNumCities());
	if (getNumCities() < iCitiesTarget)
	{
		// note: I'd like to use (AI_getNumCitySites() > 0) as well, but that could potentially cause the site selection to oscillate.
		iSafePercent -= 14 * (iCitiesTarget - getNumCities()) / iCitiesTarget;
	} // K-Mod end

	return iFundedPercent - iSafePercent;
}

// XXX
bool CvPlayerAI::AI_isFinancialTrouble() const
{
	return (AI_financialTroubleMargin() < 0); // advc.rom4
}

// This function has been heavily edited for K-Mod
int CvPlayerAI::AI_goldTarget(bool bUpgradeBudgetOnly) const
{
	int iGold = 0;

	// K-Mod.
	int iUpgradeBudget = AI_getGoldToUpgradeAllUnits();

	if (iUpgradeBudget > 0)
	{
		const CvTeamAI& kTeam = GET_TEAM(getTeam());
		bool bAnyWar = AI_isFocusWar(); // advc.105
				//kTeam.getAnyWarPlanCount(true) > 0;

		if (!bAnyWar)
		{
			iUpgradeBudget /= (AI_isFinancialTrouble() ||
					AI_atVictoryStage(AI_VICTORY_CULTURE4) ? 10 : 4);
		}
		else
		{
			int iSuccessRating = kTeam.AI_getWarSuccessRating();
			if (iSuccessRating < -10 || (iSuccessRating < 10 && kTeam.AI_getNumWarPlans(WARPLAN_ATTACKED_RECENT) > 0))
				iUpgradeBudget *= 2; // cf. iTargetTurns in AI_doCommerce
			else if (iSuccessRating > 50 || AI_isFinancialTrouble())
			{
				iUpgradeBudget /= 2;
			}
		}

		if (bUpgradeBudgetOnly)
			return iUpgradeBudget;

		iGold += iUpgradeBudget;
	}
	// K-Mod end
	if (GC.getGame().getElapsedGameTurns() >= 40
			|| getNumCities() > 3) // UNOFFICIAL_PATCH, Bugfix, 02/24/10, jdog5000
	{
		/*int iMultiplier = 0;
		iMultiplier += GC.getInfo(GC.getGame().getGameSpeedType()).getResearchPercent();
		iMultiplier += GC.getInfo(GC.getGame().getGameSpeedType()).getTrainPercent();
		iMultiplier += GC.getInfo(GC.getGame().getGameSpeedType()).getConstructPercent();
		iMultiplier /= 3;
		iGold += ((getNumCities() * 3) + (getTotalPopulation() / 3));
		iGold += (GC.getGame().getElapsedGameTurns() / 2);*/ // BtS
		// K-mod. Does slower research mean we need to keep more gold? Does slower building?
		// Surely the raw turn count is the one that needs to be adjusted for speed!
		if (!AI_atVictoryStage(AI_VICTORY_CULTURE4))
		{
			int iStockPile = 3*std::min(8, getNumCities()) + std::min(120, getTotalPopulation())/3;
			iStockPile += 100*GC.getGame().getElapsedGameTurns() / (2*GC.getInfo(GC.getGame().getGameSpeedType()).getResearchPercent());
			if (AI_getFlavorValue(FLAVOR_GOLD) > 0)
			{
				iStockPile *= 10 + AI_getFlavorValue(FLAVOR_GOLD);
				iStockPile /= 8;
			}
			// note: currently the highest flavor_gold is 5.
			iGold += iStockPile;
		}
		// K-Mod end

		/*iGold *= iMultiplier;
		iGold /= 100;*/

		/*if (bAnyWar) {
			iGold *= 3;
			iGold /= 2;
		} */ // BtS - K-Mod: I don't think we need this anymore.

		if (AI_avoidScience())
		{
			iGold *= 3; // was 10
		}

		//iGold += (AI_getGoldToUpgradeAllUnits() / (bAnyWar ? 1 : 2)); // obsolete (K-Mod)

		/*CorporationTypes eActiveCorporation = NO_CORPORATION;
		for (int iI = 0; iI < GC.getNumCorporationInfos(); iI++) {
			if (getHasCorporationCount((CorporationTypes)iI) > 0) {
				eActiveCorporation = (CorporationTypes)iI;
				break;
			}
		}
		if (eActiveCorporation != NO_CORPORATION) {
			int iSpreadCost = std::max(0, GC.getInfo(eActiveCorporation).getSpreadCost() * (100 + calculateInflationRate()));
			iSpreadCost /= 50;
			iGold += iSpreadCost;
		}*/ // BtS
		// K-Mod
		int iSpreadCost = 0;

		if (!isNoCorporations())
		{
			for (CorporationTypes eCorp = (CorporationTypes)0; eCorp < GC.getNumCorporationInfos(); eCorp = (CorporationTypes)(eCorp+1))
			{
				if (getHasCorporationCount(eCorp) > 0)
				{
					int iExecs = countCorporationSpreadUnits(NULL, eCorp, true);
					if (iExecs > 0)
					{
						int iTempCost = GC.getInfo(eCorp).getSpreadCost();
						iTempCost *= iExecs + 1; // execs + 1 because actual spread cost can be higher than the base cost.
						iSpreadCost = std::max(iSpreadCost, iTempCost);
					}
				}
			}
		}
		if (iSpreadCost > 0)
		{
			iSpreadCost *= 100 + calculateInflationRate();
			iSpreadCost /= 100;
			iGold += iSpreadCost;
		}
		// K-Mod end
	}

	return iGold + AI_getExtraGoldTarget();
}

// <k146>
// Functors used by AI_bestTech. (I wish we had lambdas.)
template <typename A, typename B>
struct PairSecondEq : public std::binary_function<std::pair<A, B>,std::pair<A, B>,bool>
{
	PairSecondEq(B t) : _target(t) {}

	bool operator()(const std::pair<A, B>& o1)
	{
		return o1.second == _target;
	}
private:
	B _target;
};
template <typename A, typename B>
struct PairFirstLess : public std::binary_function<std::pair<A, B>,std::pair<A, B>,bool>
{
	bool operator()(const std::pair<A, B>& o1, const std::pair<A, B>& o2)
	{
		return o1.first < o2.first;
	}
}; // </k146>

// edited by K-Mod and BBAI
TechTypes CvPlayerAI::AI_bestTech(int iMaxPathLength, bool bFreeTech, bool bAsync, TechTypes eIgnoreTech, AdvisorTypes eIgnoreAdvisor,
	PlayerTypes eFromPlayer) const // advc.144
{
	PROFILE("CvPlayerAI::AI_bestTech");

	CvTeam& kTeam = GET_TEAM(getTeam());

	std::vector<int> viBonusClassRevealed(GC.getNumBonusClassInfos(), 0);
	std::vector<int> viBonusClassUnrevealed(GC.getNumBonusClassInfos(), 0);
	std::vector<int> viBonusClassHave(GC.getNumBonusClassInfos(), 0);
	// k146 (comment): Find make lists of which bonuses we have / don't have / can see. This is used for tech evaluation
	for (int iI = 0; iI < GC.getNumBonusInfos(); iI++)
	{
		TechTypes eRevealTech = (TechTypes)GC.getInfo((BonusTypes)iI).getTechReveal();
		BonusClassTypes eBonusClass = (BonusClassTypes)GC.getInfo((BonusTypes)iI).getBonusClassType();
		if (eRevealTech != NO_TECH)
		{
			if ((kTeam.isHasTech(eRevealTech)))
			{
				viBonusClassRevealed[eBonusClass]++;
			}
			else
			{
				viBonusClassUnrevealed[eBonusClass]++;
			}

			if (getNumAvailableBonuses((BonusTypes)iI) > 0)
			{
				viBonusClassHave[eBonusClass]++;
			}
			else if (AI_countOwnedBonuses((BonusTypes)iI) > 0)
			{
				viBonusClassHave[eBonusClass]++;
			}
		}
	}

#ifdef DEBUG_TECH_CHOICES
	CvWString szPlayerName = getName();
	DEBUGLOG("AI_bestTech:%S\n", szPlayerName.GetCString());
#endif
	// <k146>
	/*  Instead of choosing a tech anywhere inside the max path length with no
		adjustments for how deep the tech is, we'll evaluate all techs inside
		the max path length; but instead of just picking the highest value one
		irrespective of depth, we'll use the evaluatations to add value to the
		prereq techs, and then choose the best depth 1 tech at the end. */
	std::vector<std::pair<int,TechTypes> > techs; // (value, tech) pairs)
	//std::vector<TechTypes> techs;
	//std::vector<int> values;
	// cumulative number of techs for each depth of the search. (techs_to_depth[0] == 0)
	std::vector<int> techs_to_depth;

	int iTechCount = 0;
	for (int iDepth = 0; iDepth < iMaxPathLength; ++iDepth)
	{
		techs_to_depth.push_back(iTechCount);

		for (TechTypes eTech = (TechTypes)0; eTech < GC.getNumTechInfos();
				eTech=(TechTypes)(eTech+1))
		{
			const CvTechInfo& kTech = GC.getInfo(eTech);
			const std::vector<std::pair<int,TechTypes> >::iterator
					// Evaluated techs before the current depth
					tech_search_end = techs.begin()+techs_to_depth[iDepth];
			if (eTech == eIgnoreTech)
				continue;
			if (eIgnoreAdvisor != NO_ADVISOR && kTech.getAdvisorType() == eIgnoreAdvisor)
				continue;
			if (!canEverResearch(eTech))
				continue;
			if (kTeam.isHasTech(eTech))
				continue;
			// <advc.144>
			if(eFromPlayer != NO_PLAYER)
			{
				if(!GET_PLAYER(eFromPlayer).canTradeItem(getID(), TradeData(
					TRADE_TECHNOLOGIES, eTech)))
				{
					continue;
				}
			} // </advc.144>
			if (GC.getInfo(eTech).getEra() > (getCurrentEra() + 1))
				continue; // too far in the future to consider. (This condition is only for efficiency.)

			if (std::find_if(techs.begin(), tech_search_end, PairSecondEq<int, TechTypes>(eTech)) != tech_search_end)
				continue; // already evaluated

			// Check "or" prereqs
			bool bMissingPrereq = false;
			for (int p = 0; p < GC.getNUM_OR_TECH_PREREQS(); ++p)
			{
				TechTypes ePrereq = (TechTypes)kTech.getPrereqOrTechs(p);
				if (ePrereq != NO_TECH)
				{
					if (kTeam.isHasTech(ePrereq) || std::find_if(techs.begin(), tech_search_end, PairSecondEq<int, TechTypes>(ePrereq)) != tech_search_end)
					{
						bMissingPrereq = false; // we have a prereq
						break;
					}
					bMissingPrereq = true; // A prereq exists, and we don't have it.
				}
			}
			if (bMissingPrereq)
				continue; // We don't have any of the "or" prereqs

			// Check "and" prereqs
			for (int p = 0; p < GC.getNUM_AND_TECH_PREREQS(eTech); ++p)
			{
				TechTypes ePrereq = (TechTypes)kTech.getPrereqAndTechs(p);
				if (ePrereq != NO_TECH)
				{
					if (!GET_TEAM(getTeam()).isHasTech(ePrereq) && std::find_if(techs.begin(), tech_search_end, PairSecondEq<int, TechTypes>(ePrereq)) == tech_search_end)
					{
						bMissingPrereq = true;
						break;
					}
				}
			}
			if (bMissingPrereq)
				continue; // We're missing at least one "and" prereq
			//

			/*  Otherwise, all the prereqs are either researched, or on our list
				from lower depths. We're ready to evaluate this tech and add it
				to the list. */
			int iValue = AI_techValue(eTech, iDepth+1, iDepth == 0 && bFreeTech, bAsync,
					viBonusClassRevealed, viBonusClassUnrevealed, viBonusClassHave,
					eFromPlayer); // advc.144

			techs.push_back(std::make_pair(iValue, eTech));
			//techs.push_back(eTech);
			//values.push_back(iValue);
			++iTechCount;

			if (!bAsync && iDepth == 0 && gPlayerLogLevel >= 3) logBBAI("      Player %d (%S) consider tech %S with value %d", getID(), getCivilizationDescription(0), GC.getInfo(eTech).getDescription(), iValue); // advc.007: Don't log when bAsync
		}
	}
	// We need this to ensure techs_to_depth[1] exists.
	techs_to_depth.push_back(iTechCount);

	FAssert(techs_to_depth.size() == iMaxPathLength+1);
	//FAssert(techs.size() == values.size());

#ifdef USE_OLD_TECH_STUFF
	bool bPathways = false && getID() < GC.getGame().getCivPlayersEverAlive()/2; // testing (temp)
	bool bNewWays = true || getID() < GC.getGame().getCivPlayersEverAlive()/2; // testing (temp)

	if (!bPathways)
	{
	/*  Ok. All techs have been evaluated up to the given search depth.
		Now we just have to add a percentage the deep tech values to their
		prereqs. First, lets calculate what the percentage should be!
		Note: the fraction compounds for each depth level.
		eg. 1, 1/3, 1/9, 1/27, etc. */
	if (iMaxPathLength > 1 && iTechCount > techs_to_depth[1])
	{
		int iPrereqPercent = bNewWays ? 50 : 0;
		iPrereqPercent += (AI_getFlavorValue(FLAVOR_SCIENCE) > 0) ? 5 +
				AI_getFlavorValue(FLAVOR_SCIENCE) : 0;
		iPrereqPercent += AI_isDoStrategy(AI_STRATEGY_ECONOMY_FOCUS) ? 10 : 0;
		iPrereqPercent += AI_atVictoryStage(AI_VICTORY_SPACE1) ? 5 : 0;
		iPrereqPercent += AI_atVictoryStage(AI_VICTORY_SPACE2) ? 10 : 0;
		iPrereqPercent += AI_isDoStrategy(AI_STRATEGY_BIG_ESPIONAGE) ? -5 : 0;
		iPrereqPercent += kTeam.getAnyWarPlanCount(true) > 0 ? -10 : 0;
		// more modifiers to come?

		iPrereqPercent = range(iPrereqPercent, 0, 80);

		/*  I figure that if I go through the techs in reverse order to add
			value to their prereqs, I don't double-count or miss anything.
			Is that correct? */
		int iDepth = iMaxPathLength-1;
		for (int i = iTechCount-1; i >= techs_to_depth[1]; --i)
		{
			const CvTechInfo& kTech = GC.getInfo(techs[i].second);

			if (i < techs_to_depth[iDepth])
			{
				--iDepth;
			}
			FAssert(iDepth > 0);

			/*  We only want to award points to the techs directly below this
				level. We don't want, for example, Chemistry getting points
				from Biology when we haven't researched Scientific Method. */
			const std::vector<std::pair<int,TechTypes> >::iterator
					prereq_search_begin = techs.begin()+techs_to_depth[iDepth-1];
			const std::vector<std::pair<int,TechTypes> >::iterator
					prereq_search_end = techs.begin()+techs_to_depth[iDepth];
			/*  Also; for the time being, I only want to add value to prereqs
				from the best following tech, rather than from all following
				techs. (The logic is that we will only research one thing at a
				time anyway; so although opening lots of options is good, we
				shouldn't overvalue it.) */
			std::vector<int> prereq_bonus(techs.size(), 0);

			FAssert(techs[i].first*iPrereqPercent/100 > 0 &&
					techs[i].first*iPrereqPercent/100 < 100000);

			for (int p = 0; p < GC.getNUM_OR_TECH_PREREQS(); ++p)
			{
				TechTypes ePrereq = (TechTypes)kTech.getPrereqOrTechs(p);
				if (ePrereq != NO_TECH)
				{
					std::vector<std::pair<int,TechTypes> >::iterator tech_it =
							std::find_if(prereq_search_begin, prereq_search_end,
							PairSecondEq<int,TechTypes>(ePrereq));
					if (tech_it != prereq_search_end)
					{
						const size_t prereq_i = tech_it - techs.begin();
						//values[index] += values[i]*iPrereqPercent/100;
						prereq_bonus[prereq_i] = std::max(prereq_bonus[prereq_i],
								techs[i].first*iPrereqPercent/100);

						if (gPlayerLogLevel >= 3)
						{
							logBBAI("      %S adds %d to %S (depth %d)",
									GC.getInfo(techs[i].second).
									getDescription(),
									techs[i].first*iPrereqPercent/100,
									GC.getInfo(techs[prereq_i].second).
									getDescription(), iDepth-1);
						}
					}
				}
			}
			for (int p = 0; p < GC.getNUM_AND_TECH_PREREQS(); ++p)
			{
				TechTypes ePrereq = (TechTypes)kTech.getPrereqAndTechs(p);
				if (ePrereq != NO_TECH)
				{
					std::vector<std::pair<int,TechTypes> >::iterator tech_it =
							std::find_if(prereq_search_begin, prereq_search_end,
							PairSecondEq<int, TechTypes>(ePrereq));
					if (tech_it != prereq_search_end)
					{
						const size_t prereq_i = tech_it - techs.begin();

						//values[prereq_i] += values[i]*iPrereqPercent/100;
						prereq_bonus[prereq_i] = std::max(prereq_bonus[prereq_i],
								techs[i].first*iPrereqPercent/100);
						if (gPlayerLogLevel >= 3)
						{
							logBBAI("      %S adds %d to %S (depth %d)",
									GC.getInfo(techs[i].second).
									getDescription(),
									techs[i].first*iPrereqPercent/100,
									GC.getInfo(techs[prereq_i].second).
									getDescription(), iDepth-1);
						}
					}
				}
			}

			// Apply the prereq_bonuses
			for (size_t p = 0; p < prereq_bonus.size(); ++p)
			{
				/*  Kludge: Under this system, dead-end techs (such as rifling)
					can be avoided due to the high value of follow-on techs.
					So here's what we're going to do... */
				techs[p].first += std::max(prereq_bonus[p],
						(techs[p].first - 80)*iPrereqPercent*3/400);
				/*  Note, the "-80" is meant to represent removing the
					random value bonus. cf. AI_techValue (divided by ~5 turns).
					(bonus is 0-80*cities) */
			}
		}
	}
	// All the evaluations are now complete. Now we just have to find the best tech.
	std::vector<std::pair<int, TechTypes> >::iterator tech_it = std::max_element(techs.begin(), (bNewWays ? techs.begin()+techs_to_depth[1] : techs.end()),PairFirstLess<int, TechTypes>());
	if (tech_it == (bNewWays ? techs.begin()+techs_to_depth[1] : techs.end()))
	{
		FAssert(iTechCount == 0);
		return NO_TECH;
	}
	TechTypes eBestTech = tech_it->second;
	FAssert(canResearch(eBestTech));

	if (gPlayerLogLevel >= 1)
	{
		logBBAI("  Player %d (%S) selects tech %S with value %d", getID(), getCivilizationDescription(0), GC.getInfo(eBestTech).getDescription(), tech_it->first);
	}

	return eBestTech;
	}
#endif

	/*  Yet another version!
		pathways version
		We've evaluated all the techs up to the given depth. Now we want to
		choose the highest value pathway. eg. suppose iMaxPathLength = 3; we
		will then look for the best three techs to research, in order.
		It could be three techs for which we already have all prereqs, or it
		could be techs leading to new techs.

		Algorithm:
		* Build list of techs at each depth.
		* Sort lists by value at each depth.
		We don't want to consider every possible set of three techs.
		Many combos can be disregarded easily.
		For explanation purposes, assume a depth of 3.
		* The 3rd highest value at depth = 0 is a threshold for the next depth.
		  No techs lower than the threshold need to be considered in any combo.
		* The max of the old threshold and the (max_depth-cur_depth)th value
		  becomes the new threshold. eg. at depth=1, the 2nd highest value
		  becomes the new threshold (if it is higher than the old).
		* All techs above the threshold are viable end points.
		* For each end point, pick the highest value prereqs which allow us to
		  reach the endpoint.
		* If that doesn't fill all the full path, pick the highest value
		  available techs. (eg. if our end-point is not a the max depth, we can
		  pick an arbitrary tech at depth=0) */

	// Sort the techs at each depth:
	FAssert(techs_to_depth[0] == 0); // No techs before depth 0.
	// max depth is after all techs
	FAssert(techs.size() == techs_to_depth[techs_to_depth.size()-1]);
	for (size_t i = 1; i < techs_to_depth.size(); ++i)
	{
		std::sort(techs.begin()+techs_to_depth[i-1],
				techs.begin()+techs_to_depth[i],
				std::greater<std::pair<int,TechTypes> >());
	}

	// First deal with the trivial cases...
	// no Techs
	if (techs.empty())
		return NO_TECH;
	// path length of 1.
	if (iMaxPathLength < 2)
	{
		FAssert(techs.size() > 0);
		return techs[0].second;
	}
	// ... and the case where there are not enough techs in the list.
	if ((int)techs.size() < iMaxPathLength)
	{
		return techs[0].second;
	}

	// Create a list of possible tech paths.
	std::vector<std::pair<int,std::vector<int> > > tech_paths; // (total_value, path)
	// Note: paths are a vector of indices referring to `techs`.
	/*  Paths are in reverse order, for convinience in constructing them.
		(ie. the first tech to research is at the end of the list.) */

	// Initial threshold
	FAssert(techs_to_depth.size() > 1);
	int iThreshold = techs[std::min(iMaxPathLength-1,
			(int)techs.size()-1)].first;
	// Note: this works even if depth=0 isn't big enough.

	double fDepthRate = 0.62; // advc: 0.8 in k146

	for (int end_depth = 0; end_depth < iMaxPathLength; ++end_depth)
	{
		// Note: at depth == 0, there are no prereqs, so we only need to consider the best option.
		for (int i = (end_depth == 0 ? iMaxPathLength - 1 :
			techs_to_depth[end_depth]); i < techs_to_depth[end_depth+1]; ++i)
		{
			if (techs[i].first < iThreshold)
				break; /* Note: the techs are sorted, so if we're below the
						  threshold, we're done. */

			// This is a valid end point. So start a new tech path.
			tech_paths.push_back(std::make_pair(techs[i].first,
					std::vector<int>()));
			tech_paths.back().second.push_back(i);
			// A set of techs that will be in our path
			std::set<TechTypes> techs_in_path;
			// A queue of techs that we still need to check prereqs for
			std::queue<TechTypes> techs_to_check;

			techs_in_path.insert(techs[i].second);
			if (end_depth != 0)
			{
				techs_to_check.push(techs[i].second);
			}
			while (!techs_to_check.empty() && (int)techs_in_path.size()
					<= iMaxPathLength)
			{
				bool bMissingPrereq = false;

				// AndTech prereqs:
				for (int p = 0; p < GC.getNUM_AND_TECH_PREREQS(techs_to_check.front()) &&
						!bMissingPrereq; ++p)
				{
					TechTypes ePrereq = (TechTypes)GC.getInfo(
							techs_to_check.front()).getPrereqAndTechs(p);
					if (!kTeam.isHasTech(ePrereq) &&
							techs_in_path.find(ePrereq) == techs_in_path.end())
					{
						bMissingPrereq = true;
						// find the tech. (Lambda would be nice...)
						//std::find_if(techs.begin(), techs.end(),[](std::pair<int, TechTypes> &t){return t.second == ePrereq;});
						/*  really we should use current depth instead of end_depth;
							but that's harder... */
						for (int j = 0; j < techs_to_depth[end_depth]; ++j)
						{
							if (techs[j].second == ePrereq)
							{
								// add it to the path.
								tech_paths.back().first = (int)(fDepthRate *
										tech_paths.back().first);
								tech_paths.back().first += techs[j].first;
								tech_paths.back().second.push_back(j);
								techs_in_path.insert(ePrereq);
								techs_to_check.push(ePrereq);
								bMissingPrereq = false;
								break;
							}
						}
					}
				}
				if (bMissingPrereq)
				{
					// This path is invalid, because we can't get the prereqs.
					break;
				}
				// OrTechs:
				int iBestOrIndex = -1;
				int iBestOrValue = -1;
				for (int p = 0; p < GC.getNUM_OR_TECH_PREREQS(); ++p)
				{
					TechTypes ePrereq = (TechTypes)GC.getInfo(
							techs_to_check.front()).getPrereqOrTechs(p);
					if (ePrereq == NO_TECH)
						continue;

					if (!kTeam.isHasTech(ePrereq) &&
							techs_in_path.find(ePrereq) == techs_in_path.end())
					{
						bMissingPrereq = true;
						// find the tech.
						for (int j = 0; j < techs_to_depth[end_depth]; ++j)
						{
							if (techs[j].second == ePrereq)
							{
								if (techs[j].first > iBestOrValue)
								{
									iBestOrIndex = j;
									iBestOrValue = techs[j].first;
								}
							}
						}
					}
					else
					{
						// We have one of the orPreqs.
						iBestOrIndex = -1;
						iBestOrValue = -1;
						bMissingPrereq = false;
						break;
					}
				}
				// Add the best OrPrereq to the path
				if (iBestOrIndex >= 0)
				{
					FAssert(bMissingPrereq);

					tech_paths.back().first = (int)(fDepthRate *
							tech_paths.back().first);
					tech_paths.back().first += techs[iBestOrIndex].first;
					tech_paths.back().second.push_back(iBestOrIndex);
					techs_in_path.insert(techs[iBestOrIndex].second);
					techs_to_check.push(techs[iBestOrIndex].second);
					bMissingPrereq = false;
				}

				if (bMissingPrereq)
				{
					break; // failured to add prereqs to the path
				}
				else
				{
					techs_to_check.pop(); // prereqs are satisfied
				}
			} // end techs_to_check (prereqs loop)

			/*  If we couldn't add all the prereqs (eg. too many),
				abort the path. */
			if ((int)techs_in_path.size() > iMaxPathLength ||
					!techs_to_check.empty())
			{
				tech_paths.pop_back();
				continue;
			}

			/*  If we haven't already filled the path with prereqs,
				fill the remaining slots with the highest value unused techs. */
			if (((int)techs_in_path.size() < iMaxPathLength))
			{
				/*  todo: consider backfilling the list with deeper techs if
					we've matched their prereqs already. */
				for (int j = 0; j < techs_to_depth[1] && (int)techs_in_path.size() <
						iMaxPathLength; j++)
				{
					if (techs_in_path.count(techs[j].second) == 0)
					{
						techs_in_path.insert(techs[j].second);
						int k = 0;
						/*  Note: since this tech isn't a prereqs, it can go any-
							where in our path. Try to research highest values first. */
						for (; k < (int)tech_paths.back().second.size(); k++)
						{
							if (techs[j].first < techs[tech_paths.back().
									second[k]].first)
							{

								// Note: we'll need to recalculate the total value.
								tech_paths.back().second.insert(
										tech_paths.back().second.begin()+k, j);
								break;
							}
						}
						if (k == tech_paths.back().second.size())
						{
							// haven't added it yet
							tech_paths.back().second.push_back(j);
						}
					}
				}
				// Recalculate total value;
				tech_paths.back().first = 0;
				for (int k = 0; k < (int)tech_paths.back().second.size(); ++k)
				{
					tech_paths.back().first = (int)(fDepthRate *
							tech_paths.back().first);
					tech_paths.back().first +=
							techs[tech_paths.back().second[k]].first;
				}
			}
		} // end loop through techs at given end depth

		/*  TODO: at this point we should update the threshold for the
			next depth... But I don't want to do that until the back-fill stage
			is fixed to consider deeper techs. */
	} // end loop through possible end depths

	/*  Return the tech corresponding to the back (first step) of the tech path
		with the highest value. */
	std::vector<std::pair<int, std::vector<int> > >::iterator best_path_it =
			std::max_element(tech_paths.begin(), tech_paths.end(),
			PairFirstLess<int, std::vector<int> >());

	if (best_path_it == tech_paths.end())
	{
		FAssertMsg(0, "Failed to create a tech path");
		return NO_TECH;
	}

	TechTypes eBestTech = techs[best_path_it->second.back()].second;
	if (gPlayerLogLevel >= 1)
	{
		logBBAI("  Player %d (%S) selects tech %S with value %d. (Aiming for %S)",
				getID(), getCivilizationDescription(0),
				GC.getInfo(eBestTech).getDescription(),
				techs[best_path_it->second.back()].first,
				GC.getInfo(techs[best_path_it->second.front()].second).
				getDescription());
	}
	FAssert(!isResearch() || getAdvancedStartPoints() < 0 ||
			canResearch(eBestTech, false, bFreeTech));
	// </k146>
	return eBestTech;
}

// This function has been mostly rewritten for K-Mod.
// Note: many of the values used in this function are arbitrary; but adjusted them all to get closer to having a common scale.
// The scale before research time is taken into account is roughly 4 = 1 commerce per turn. Afterwards it is arbitrary.
// (Compared to the original numbers, this is * 1/100 * 7 * 4. 28/100)
int CvPlayerAI::AI_techValue(TechTypes eTech, int iPathLength, bool bFreeTech,
	bool bAsync, const std::vector<int>& viBonusClassRevealed,
	const std::vector<int>& viBonusClassUnrevealed,
	const std::vector<int>& viBonusClassHave,
	PlayerTypes eFromPlayer) const // advc.144
{
	PROFILE_FUNC();
	FAssert(viBonusClassRevealed.size() == GC.getNumBonusClassInfos());
	FAssert(viBonusClassUnrevealed.size() == GC.getNumBonusClassInfos());
	FAssert(viBonusClassHave.size() == GC.getNumBonusClassInfos());
	// advc.001: Was just long; see Erik's comment in CvUnitAI::AI_sacrificeValue.
	long long iValue = 1; // K-Mod. (the int was overflowing in parts of the calculation)

	CvCity* pCapitalCity = getCapitalCity();
	const CvTeamAI& kTeam = GET_TEAM(getTeam());
	const CvTechInfo& kTechInfo = GC.getInfo(eTech); // K-Mod

	bool bCapitalAlone = (GC.getGame().getElapsedGameTurns() > 0) ? AI_isCapitalAreaAlone() : false;
	bool bFinancialTrouble = AI_isFinancialTrouble();
	bool bAdvancedStart = getAdvancedStartPoints() >= 0;

	int iHasMetCount = kTeam.getHasMetCivCount(true);
	int iCoastalCities = countNumCoastalCities();

	int iCityCount = getNumCities();
	int iCityTarget = GC.getInfo(GC.getMap().getWorldSize()).getTargetNumCities();
	// <k146>
	int iRandomFactor = 0; // Amount of random value in the answer.
	int iRandomMax = 0;   // Max random value. (These randomness trackers aren't actually used, and may not even be accurate.)
	//if (iPathLength <= 1) // Don't include random bonus for follow-on tech values.
	{/*  </k146> advc.131: Decrease the random summand (was 80), but increase the
		random multiplier at the end of the function. */
		int const iRandPlusMax = 26; //80
		iRandomFactor = (bAsync ?
				GC.getASyncRand().get(iRandPlusMax*iCityCount, "AI Research ASYNC") :
				GC.getGame().getSorenRandNum(iRandPlusMax*iCityCount, "AI Research"));
		iRandomMax = iRandPlusMax*iCityCount;
		iValue += iRandomFactor;
	}
	if(!bFreeTech) // k146
		iValue += kTeam.getResearchProgress(eTech)/4;
	// advc.007: Might need this again
	//FAssert(!canResearch(eTech) || std::wcscmp(kTechInfo.getDescription(), L"Feudalism") != 0);
	// Map stuff
	if (kTechInfo.isExtraWaterSeeFrom())
	{
		if (iCoastalCities > 0)
		{
			iValue += 28;
			if (bCapitalAlone)
				iValue += 112;
		}
	}

	/*if (kTechInfo.isMapCentering())
	{
		// K-Mod. The benefits of this are marginal at best.
	}*/

	if (kTechInfo.isMapVisible())
	{
		iValue += (3*GC.getMap().getLandPlots() + GC.getMap().numPlots())/400; // (3 * 1100 + 4400)/400 = 14. ~3 commerce/turn
		// Note, the world is usually thoroughly explored by the time of satellites. So this is low value.
		// If we wanted to evaluate this properly, we'd need to calculate at how much of the world is still unexplored.
	}

	// Expand trading options
	//if (kTechInfo.isMapTrading())
	if (kTechInfo.isMapTrading() && !kTeam.isMapTrading()) // K-Mod
	{
		// K-Mod. increase the bonus for each known civ that we can't already tech trade with
		int iNewTrade = 0;
		int iExistingTrade = 0;
		for (TeamIter<MAJOR_CIV,OTHER_KNOWN_TO> it(getTeam()); it.hasNext(); ++it)
		{
			const CvTeamAI& kLoopTeam = *it;
			// <advc.136> Not going to learn much from our vassals' maps
			if (kLoopTeam.isVassal(getTeam()))
				continue; // </advc.136>
			if (!kLoopTeam.isMapTrading())
			{
				if (kLoopTeam.AI_mapTrade(getTeam()) == NO_DENIAL &&
					kTeam.AI_mapTrade(kLoopTeam.getID()) == NO_DENIAL)
				{
					iNewTrade += kLoopTeam.getAliveCount();
				}
			}
			else iExistingTrade += kLoopTeam.getAliveCount();
		}
		// The value could be scaled based on how much map we're missing; but I don't want to waste time calculating that.
		int iBase = (3*GC.getMap().getLandPlots() + GC.getMap().numPlots()) / 300; // ~ (3 * 1100 + 4400). ~ 4.5 commerce/turn
		iValue += iBase/2;
		if (iNewTrade > 0)
		{
			if (bCapitalAlone) // (or rather, have we met anyone from overseas)
			{
				iValue += 5*iBase; // a stronger chance of getting the map for a different island
			}

			if (iExistingTrade == 0 && iNewTrade > 1)
			{
				iValue += 3*iBase; // we have the possibility of being a map broker.
			}
			iValue += iBase*(iNewTrade + 1);
		}
		// K-Mod end
	}

	if (kTechInfo.isTechTrading() && !GC.getGame().isOption(GAMEOPTION_NO_TECH_TRADING) &&
		!kTeam.isTechTrading()) // K-Mod
	{
		// K-Mod. increase the bonus for each known civ that we can't already tech trade with
		int iBaseValue = getTotalPopulation() * 3;

		int iNewTrade = 0;
		int iExistingTrade = 0;
		for (TeamIter<MAJOR_CIV,OTHER_KNOWN_TO> it(getTeam()); it.hasNext(); ++it)
		{
			const CvTeamAI& kLoopTeam = *it;
			if (!kLoopTeam.isTechTrading())
			{
				if (kLoopTeam.AI_techTrade(NO_TECH, getTeam()) == NO_DENIAL &&
					kTeam.AI_techTrade(NO_TECH, kLoopTeam.getID()) == NO_DENIAL)
				{
					iNewTrade += kLoopTeam.getAliveCount();
				}
			}
			else iExistingTrade += kLoopTeam.getAliveCount();
		}
		iValue += iBaseValue * std::max(0, 3*iNewTrade - iExistingTrade);
		// K-Mod end
	}

	if (kTechInfo.isGoldTrading() && !kTeam.isGoldTrading())
	{
		// K-Mod. The key value of gold trading is to facilitate tech trades.
		int iBaseValue =  (!GC.getGame().isOption(GAMEOPTION_NO_TECH_TRADING) && !kTeam.isTechTrading())
			// <k146>
			? (getTotalPopulation() + 10)
			: (getTotalPopulation()/3 + 3); // </k146>
		int iNewTrade = 0;
		int iExistingTrade = 0;
		for (TeamIter<MAJOR_CIV,OTHER_KNOWN_TO> it(getTeam()); it.hasNext(); ++it)
		{
			CvTeamAI const& kLoopTeam = *it;
			if (!kLoopTeam.isGoldTrading())
			{
				if (kLoopTeam.AI_techTrade(NO_TECH, getTeam()) == NO_DENIAL &&
					kTeam.AI_techTrade(NO_TECH, kLoopTeam.getID()) == NO_DENIAL)
				{
					iNewTrade += kLoopTeam.getAliveCount();
				}
			}
			else iExistingTrade += kLoopTeam.getAliveCount();
		}
		iValue += iBaseValue * std::max(
				iNewTrade, // k146: was 0
				3*iNewTrade - iExistingTrade);
	}

	if (kTechInfo.isOpenBordersTrading())
	{
		// (Todo: copy the NewTrade / ExistingTrade method from above.)
		if (iHasMetCount > 0)
		{
			iValue += 12 * iCityCount;
			if (iCoastalCities > 0)
				iValue += 4 * iCityCount;
		}
	}

	// K-Mod. Value pact trading based on how many civs are willing, and on how much we think we need it!
	if (kTechInfo.isDefensivePactTrading() && !kTeam.isDefensivePactTrading())
	{
		int iNewTrade = 0;
		int iExistingTrade = 0;
		for (TeamIter<FREE_MAJOR_CIV,KNOWN_POTENTIAL_ENEMY_OF> it(getTeam()); it.hasNext(); ++it)
		{
			CvTeamAI const& kLoopTeam = *it;
			if (!kLoopTeam.isDefensivePactTrading())
			{
				if (kLoopTeam.AI_defensivePactTrade(getTeam()) == NO_DENIAL &&
					kTeam.AI_defensivePactTrade(kLoopTeam.getID()) == NO_DENIAL)
				{
					iNewTrade += kLoopTeam.getAliveCount();
				}
			}
			else iExistingTrade += kLoopTeam.getAliveCount();
		}
		if (iNewTrade > 0)
		{
			int iPactValue = 3;
			if (AI_isDoStrategy(AI_STRATEGY_ALERT1))
				iPactValue += 1;
			if (AI_isDoStrategy(AI_STRATEGY_ALERT2))
				iPactValue += 1;
			/*  advc.115b: Commented out. Diplo victory isn't that peaceful, and
				pact improves relations with one civ, but worsens them with others. */
			/*if (AI_atVictoryStage(AI_VICTORY_DIPLOMACY2))
				iPactValue += 2;*/
			if (AI_atVictoryStage(AI_VICTORY_CULTURE3 | AI_VICTORY_SPACE3 | AI_VICTORY_DIPLOMACY3))
				iPactValue += 1;

			iValue += iNewTrade * iPactValue * 28;
		}
	}
	// K-Mod end

	if (kTechInfo.isPermanentAllianceTrading() && GC.getGame().isOption(GAMEOPTION_PERMANENT_ALLIANCES))
	{
		//iValue += 56;
		iValue += 8*iCityTarget; // k146: Replacing the above
		// todo: check for friendly civs (advc: ... that don't already have PA trading)
	}

	if (kTechInfo.isVassalStateTrading() && !(GC.getGame().isOption(GAMEOPTION_NO_VASSAL_STATES)))
	{
		//iValue += 56;
		iValue += 8*iCityTarget; // k146: Replacing the above
		// todo: check anyone is small enough to vassalate. Check for conquest / domination strategies.
	}

	// Tile improvement abilities
	if (kTechInfo.isBridgeBuilding())
	{
		iValue += 6 * iCityCount;
	}

	if (kTechInfo.isIrrigation())
	{
		iValue += 16 * iCityCount;
	}

	if (kTechInfo.isIgnoreIrrigation())
	{
		iValue += 6 * iCityCount; // K-Mod. Ignore irrigation isn't so great.
	}

	if (kTechInfo.isWaterWork())
	{
		//iValue += 6 * iCoastalCities * getAveragePopulation();
		// <advc.131> Need large populations before water tiles become worthwhile
		iValue += iCoastalCities * std::min(45, (int)
				std::ceil(std::pow((double)getAveragePopulation(), 2) / 2));
		// </advc.131>
	}

	//iValue += (kTechInfo.getFeatureProductionModifier() * 2);
	//iValue += (kTechInfo.getWorkerSpeedModifier() * 4);
	iValue += kTechInfo.getFeatureProductionModifier() * 2 * std::max(iCityCount, iCityTarget) / 25;
	iValue += kTechInfo.getWorkerSpeedModifier() * 2 * std::max(iCityCount, iCityTarget/2) / 25;

	//iValue += (kTechInfo.getTradeRoutes() * (std::max((getNumCities() + 2), iConnectedForeignCities) + 1) * ((bFinancialTrouble) ? 200 : 100));
	/*  K-Mod. A very rough estimate assuming each city has ~2 trade routes;
		new local trade routes worth ~2 commerce, and foreign worth ~6. */
	if (kTechInfo.getTradeRoutes() != 0)
	{
		int iConnectedForeignCities = AI_countPotentialForeignTradeCities(
				true, AI_getFlavorValue(FLAVOR_GOLD) == 0);
		// <advc.131>
		int iSites = std::min(AI_getNumCitySites(), iCityCount);
		int iSettler = std::min(AI_totalUnitAIs(UNITAI_SETTLE), iSites);
		// </advc.131>
		int iAddedCommerce = 2*iCityCount*kTechInfo.getTradeRoutes() +
				4*range(iConnectedForeignCities-2*iCityCount, 0,
				iCityCount*kTechInfo.getTradeRoutes())
				+ 2 * iSettler + (iSites - iSettler); // advc.131
		iValue += iAddedCommerce * (bFinancialTrouble ? 6 : 4) *
				AI_averageYieldMultiplier(YIELD_COMMERCE)/100;
		//iValue += (2*iCityCount*kTechInfo.getTradeRoutes() + 4*range(iConnectedForeignCities-2*getNumCities(), 0, iCityCount*kTechInfo.getTradeRoutes())) * (bFinancialTrouble ? 6 : 4);
	}
	// K-Mod end


	if (kTechInfo.getHealth() != 0)
	{
		iValue += 24 * iCityCount * AI_getHealthWeight(kTechInfo.getHealth(), 1) / 100;
	}
	if (kTechInfo.getHappiness() != 0)
	{
		// (this part of the evaluation was completely missing from the original bts code)
		iValue += 40 * iCityCount * AI_getHappinessWeight(kTechInfo.getHappiness(), 1) / 100;
	}

	FOR_EACH_ENUM(Route)
	{
		//iValue += -(GC.getInfo((RouteTypes)iJ).getTechMovementChange(eTech) * 100);
		// K-Mod. Note: techMovementChange is O(10)
		if (GC.getInfo(eLoopRoute).getTechMovementChange(eTech))
		{
			int iTemp = 4 * (std::max(iCityCount, iCityTarget) - iCoastalCities/2);
			if (bCapitalAlone)
				iTemp = iTemp * 2/3;
			//if (kTeam.getAnyWarPlanCount(true))
			if(AI_isFocusWar()) // advc.105
				iTemp *= 2;
			iValue -= GC.getInfo(eLoopRoute).getTechMovementChange(eTech) * iTemp;
		}
		// K-Mod end
	}

	FOR_EACH_ENUM(Domain)
	{
		iValue += kTechInfo.getDomainExtraMoves(eLoopDomain) *
				(eLoopDomain == DOMAIN_LAND ? 16 : 6) * iCityCount;
	}

	// K-Mod. Extra specialist commerce. (Based on my civic evaluation code)
	bool bSpecialistCommerce = false;
	int iTotalBonusSpecialists = -1;
	int iTotalCurrentSpecialists = -1;
	FOR_EACH_ENUM(Commerce)
	{
		bSpecialistCommerce = kTechInfo.getSpecialistExtraCommerce(eLoopCommerce) != 0;
	}

	if (bSpecialistCommerce)
	{
		// If there are any bonuses, we need to count our specialists.
 		// (The value from the bonuses will be applied later.)
		iTotalBonusSpecialists = iTotalCurrentSpecialists = 0;

		FOR_EACH_CITY(pLoopCity, *this)
		{
			iTotalBonusSpecialists += pLoopCity->getNumGreatPeople();
			iTotalBonusSpecialists += pLoopCity->totalFreeSpecialists();

			iTotalCurrentSpecialists += pLoopCity->getNumGreatPeople();
			iTotalCurrentSpecialists += pLoopCity->getSpecialistPopulation();
		}
	}
	// K-Mod end
	// <k146>
	FOR_EACH_ENUM(Commerce)
 	{
 		int iCommerceValue = 0;

 		// Commerce for specialists
 		if (bSpecialistCommerce)
 		{
			// If there are any bonuses, we need to count our specialists.
 			// (The value from the bonuses will be applied later.)
			iTotalBonusSpecialists = iTotalCurrentSpecialists = 0;
 			iCommerceValue += 4*AI_averageCommerceMultiplier(eLoopCommerce)*
					(kTechInfo.getSpecialistExtraCommerce(eLoopCommerce) *
					std::max((getTotalPopulation()+12*iTotalBonusSpecialists) /
					12, iTotalCurrentSpecialists));
 		}

 		// Flexible commerce. (This is difficult to evaluate accurately without checking a lot of things - which I'm not going to do right now.)
 		if (kTechInfo.isCommerceFlexible(eLoopCommerce))
 		{
			iCommerceValue += 40 + 4 * iCityCount;
 			/*iValue += 4 * iCityCount * (3*AI_averageCulturePressure()-200) / 100;
 			if (i == COMMERCE_CULTURE && AI_atVictoryStage(AI_VICTORY_CULTURE2))
 				iValue += 280;*/ // BtS
 		}

 		if (iCommerceValue)
 		{
 			iCommerceValue *= AI_commerceWeight(eLoopCommerce);
 			iCommerceValue /= 100;

 			iValue += iCommerceValue;
 		}
	} // </k146>
	if (kTechInfo.isAnyTerrainTrade()) // advc.003t
	{
		FOR_EACH_ENUM(Terrain)
		{
			if (!kTechInfo.isTerrainTrade(eLoopTerrain))
				continue;
			if (GC.getInfo(eLoopTerrain).isWater())
			{
				if (pCapitalCity != NULL)
				{
					//iValue += (countPotentialForeignTradeCities(pCapitalCity->getArea()) * 100);
					// K-Mod. Note: this could be coastal trade or ocean trade.
					// AI_countPotentialForeignTradeCities doesn't count civs we haven't met. Hopefully that will protect us from overestimating new routes.
					int iCurrentForeignRoutes = AI_countPotentialForeignTradeCities(true, AI_getFlavorValue(FLAVOR_GOLD) == 0);
					int iNewForeignRoutes = AI_countPotentialForeignTradeCities(false, AI_getFlavorValue(FLAVOR_GOLD) == 0) - iCurrentForeignRoutes;

					int iNewOverseasRoutes = std::max(0, AI_countPotentialForeignTradeCities(
							false, AI_getFlavorValue(FLAVOR_GOLD) == 0, pCapitalCity->area()) -
							iCurrentForeignRoutes);
					/*  advc.131: Subtract 1 from city count (capital alone is
						not going to have domestic trade routes). */
					int iLocalRoutes = std::max(0, (iCityCount-1)*3 - iCurrentForeignRoutes);

					// TODO: multiply by average commerce multiplier?

					// 4 for upgrading local to foreign. 2 for upgrading to overseas. Divide by 3 if we don't have coastal cities.
					iValue += (std::min(iLocalRoutes, iNewForeignRoutes) * 16 +
							std::min(iNewOverseasRoutes, iCityCount*3) * 8) /
							(iCoastalCities > 0 ? 1 : 3);
					// K-Mod end
				}

				iValue += 8;
			}
			else iValue += 280; // ??? (what could it be?)
		}
	}
	if (kTechInfo.isRiverTrade())
	{
		iValue += (bCapitalAlone || iHasMetCount == 0 ? 2 : 4) * (std::max(iCityCount, iCityTarget/2) - iCoastalCities/2); // K-Mod.
		// Note: the value used here is low, because it's effectively double-counting what we already got from coastal trade.
	}

	/* ------------------ Tile Improvement Value  ------------------ */
	FOR_EACH_ENUM(Improvement)
	{
		FOR_EACH_ENUM(Yield)
		{
			int iTempValue = 0;

			/*iTempValue += (GC.getInfo((ImprovementTypes)iJ).getTechYieldChanges(eTech, iK) * getImprovementCount((ImprovementTypes)iJ) * 50);*/ // BtS
			// Often, an improvement only becomes viable after it gets the tech bonus.
			// So it's silly to score the bonus proportionally to how many of the improvements we already have.
			iTempValue += GC.getInfo(eLoopImprovement).getTechYieldChanges(eTech, eLoopYield)
					* std::max(getImprovementCount(eLoopImprovement)
					+iCityCount, // k146
					3*iCityCount/2) * 4;
			// This new version is still bork, but at least it won't be worthless.
			iTempValue *= AI_averageYieldMultiplier(eLoopYield);
			iTempValue /= 100;

			iTempValue *= AI_yieldWeight(eLoopYield);
			iTempValue /= 100;

			iValue += iTempValue;
		}
	}
	int iBuildValue = 0;
	FOR_EACH_ENUM(Build)
	{
		if (GC.getInfo(eLoopBuild).getTechPrereq() != eTech)
			continue; // advc

		ImprovementTypes const eBuildImprovement = GC.getInfo(eLoopBuild).getImprovement();
		ImprovementTypes eFinalImprovement = CvImprovementInfo::finalUpgrade(eBuildImprovement);
		// Note: finalImprovementUpgrade returns -1 for looping improvements; and that isn't handled well here.

		if (eBuildImprovement == NO_IMPROVEMENT)
			iBuildValue += 8*iCityCount;
		else
		{
			int iAccessibility = 0;
			int iBestFeatureValue = 0; // O(100)

			const CvImprovementInfo& kBuildImprovement = GC.getInfo(eBuildImprovement);
			{
				// iAccessibility represents the number of this improvement we expect to have per city.
				iAccessibility += ((kBuildImprovement.isHillsMakesValid()) ? 150 : 0);
				iAccessibility += ((kBuildImprovement.isFreshWaterMakesValid()) ? 150 : 0);
				iAccessibility += ((kBuildImprovement.isRiverSideMakesValid()) ? 150 : 0);
				if (kBuildImprovement.isAnyTerrainMakesValid()) // advc.003t
				{
					FOR_EACH_ENUM(Terrain)
					{
						iAccessibility += (kBuildImprovement.getTerrainMakesValid(eLoopTerrain) ? 75 : 0);
					}
				}
				if (kBuildImprovement.isAnyFeatureMakesValid()) // advc.003t
				{
					FOR_EACH_ENUM(Feature)
					{
						if (!kBuildImprovement.getFeatureMakesValid(eLoopFeature))
							continue;
						iAccessibility += 75;
						int iTempValue = 0;
						FOR_EACH_ENUM(Yield)
						{
							iTempValue += GC.getInfo(eLoopFeature).getYieldChange(eLoopYield) *
									AI_yieldWeight(eLoopYield) * AI_averageYieldMultiplier(eLoopYield) / 100;
						}
						if (iTempValue > iBestFeatureValue)
							iBestFeatureValue = iTempValue;
					}
				}
			}

			if (eFinalImprovement == NO_IMPROVEMENT)
				eFinalImprovement = eBuildImprovement;
			CvImprovementInfo const& kFinalImprovement = GC.getInfo(eFinalImprovement);
			if (iAccessibility > 0)
			{  /* <advc.036> Don't need to improve every tile; just those
				  we expect to work in the near future. */
				if(pCapitalCity != NULL)
				{
					iAccessibility = std::min(iAccessibility, 100 *
							(1 + getTotalPopulation() / getNumCities()));
				} // </advc.036>
				int iYieldValue = iBestFeatureValue;

				FOR_EACH_ENUM(Yield)
				{
					int iTempValue = 0;

					// use average of final and initial improvements
					iTempValue += kBuildImprovement.getYieldChange(eLoopYield) * 100;
					iTempValue += kBuildImprovement.getRiverSideYieldChange(eLoopYield) * 50;
					iTempValue += kBuildImprovement.getHillsYieldChange(eLoopYield) * 75;
					iTempValue += kBuildImprovement.getIrrigatedYieldChange(eLoopYield) * 80;

					iTempValue += kFinalImprovement.getYieldChange(eLoopYield) * 100;
					iTempValue += kFinalImprovement.getRiverSideYieldChange(eLoopYield) * 50;
					iTempValue += kFinalImprovement.getHillsYieldChange(eLoopYield) * 75;
					iTempValue += kFinalImprovement.getIrrigatedYieldChange(eLoopYield) * 80;

					iTempValue /= 2;

					iTempValue *= AI_averageYieldMultiplier(eLoopYield);
					iTempValue /= 100;

					iTempValue *= AI_yieldWeight(eLoopYield);
					iTempValue /= 100;

					iYieldValue += iTempValue;
				}

				// The following are not yields, but close enough.
				iYieldValue += kFinalImprovement.isActsAsCity() ? 100 : 0;
				iYieldValue += kFinalImprovement.isCarriesIrrigation() ? 100 : 0;

				if (getCurrentEra() > GC.getGame().getStartEra())
					iYieldValue -= 100; // compare to a hypothetical low-value improvement
				iBuildValue += std::max(0, iYieldValue) * iAccessibility *
						(kFinalImprovement.isWater() ? iCoastalCities :
						std::max(2, iCityCount)) / 2500;
				// Note: we're converting from O(100)*O(100) to O(4)
			}

			FOR_EACH_ENUM(Bonus)
			{
				const CvBonusInfo& kBonusInfo = GC.getInfo(eLoopBonus);

				if (!kFinalImprovement.isImprovementBonusMakesValid(eLoopBonus) &&
					!kFinalImprovement.isImprovementBonusTrade(eLoopBonus))
					continue;

				bool bRevealed = kTeam.isBonusRevealed(eLoopBonus);

				int iNumBonuses = bRevealed
						? AI_countOwnedBonuses(eLoopBonus) // actual count
						: std::max(1, 2*getNumCities() / std::max(1, 3*iCityTarget)); // a guess
				// <advc.131> Don't jump for fish when settled one off the coast
				if(iNumBonuses > 0 && GC.getInfo(eBuildImprovement).
					isWater() && iCoastalCities <= 0)
					iNumBonuses = iNumBonuses / 2; // </advc.131>
				//iNumBonuses += std::max(0, (iCityTarget - iCityCount)*(kFinalImprovement.isWater() ? 2 : 3)/8); // future expansion
				if (iNumBonuses <= 0 || (!bRevealed && kBonusInfo.getTechReveal() != eTech))
					continue; // advc
				int iBonusValue = 0;

				TechTypes eConnectTech = (TechTypes)kBonusInfo.getTechCityTrade();
				if((kTeam.isHasTech(eConnectTech) || eConnectTech == eTech) &&
					!kTeam.isBonusObsolete(eLoopBonus) &&
					kBonusInfo.getTechObsolete() != eTech) {
						// note: this is in addition to the getTechCityTrade evaluation lower in this function.
						iBonusValue += AI_bonusVal(eLoopBonus, 1, true) * iCityCount;
						if (bRevealed)
							iBonusValue += (iNumBonuses-1) * iBonusValue / 10;
						else iBonusValue /= 2;
				}
				int iYieldValue = 0;
				FOR_EACH_ENUM(Yield)
				{
					int iTempValue = 0;

					iTempValue += kFinalImprovement.getImprovementBonusYield(eLoopBonus, eLoopYield) * 100;
					iTempValue += kFinalImprovement.getIrrigatedYieldChange(eLoopYield) * 75;

					iTempValue *= AI_yieldWeight(eLoopYield);
					iTempValue /= 100;
					iTempValue *= AI_averageYieldMultiplier(eLoopYield);
					iTempValue /= 100;

					iYieldValue += iTempValue;
				}
				// advc.036: Apply this to later-era start too
				if (getCurrentEra() > 0)//GC.getGame().getStartEra())
				{
					iYieldValue -= 100; // compare to a hypothetical low-value improvement
					// Bonuses might be outside of our city borders.
					/*  advc.036: Moved this into the getCurrentEra branch. It's unlikely
						that owned resources are outside of city radii in the early game.
						Also, make it times 3/4 or 2/3 (depending on bRevealed) instead of
						2/3 or 1/2. I don't think unworkable resources are that common. */
					iYieldValue *= (bRevealed ? 3 : 2) * iNumBonuses;
					iYieldValue /= (bRevealed ? 4 : 3);
				}
				if (kFinalImprovement.isWater())
					iYieldValue = iYieldValue * 2/3;
				/*  <advc.036> Since iBonusVal no longer overestimates early-game health,
				high-yield tiles need to be appreciated more. */
				if (iYieldValue > 0)
				{
					// This makes iYieldValue=225 a fixpoint
					iYieldValue = ::round(std::pow((double)iYieldValue, 1.5) / 15);
				}
				/*  At the start of the game, extra yields are very important,
					but we need to be sure that the tile isn't blocked by a feature
					before increasing iYieldValue further. Will have to check
					each tile -- OK, performance isn't an issue this early on. */
				if (pCapitalCity != NULL && getNumCities() == 1)
				{
					bool bValid = false;
					for (CityPlotIter it(*pCapitalCity, false); it.hasNext(); ++it)
					{
						if (it->getBonusType() == eLoopBonus)
						{
							FeatureTypes eLoopFeature = it->getFeatureType();
							if (eLoopFeature == NO_FEATURE || GET_TEAM(getTeam()).
								hasTechToClear(eLoopFeature, eTech))
							{
								bValid = true;
								break;
							}
						}
					}
					if (bValid)
						iYieldValue = ::round(iYieldValue * 1.5);
				} // </advc.036>
				// Convert to O(4)
				iYieldValue /= 25;

				iBuildValue += iBonusValue + std::max(0, iYieldValue);
			}
		}

		RouteTypes const eRoute = GC.getInfo(eLoopBuild).getRoute();
		if (eRoute != NO_ROUTE)
		{
			int iRouteValue = 4 * (iCityCount - iCoastalCities/3) + (bAdvancedStart ? 2 : 0);
			if (getBestRoute() == NO_ROUTE)
				iRouteValue *= 4;
			else if (GC.getInfo(getBestRoute()).getValue() < GC.getInfo(eRoute).getValue())
				iRouteValue *= 2;

			FOR_EACH_ENUM(Yield)
			{
				int iTempValue = 0;

				// epic bonus for epic effect
				iTempValue += getNumCities() * GC.getInfo(eRoute).getYieldChange(eLoopYield) * 32;
				FOR_EACH_ENUM(Improvement)
				{
					iTempValue += GC.getInfo(eLoopImprovement).getRouteYieldChanges(eRoute, eLoopYield) *
							std::max(getImprovementCount(eLoopImprovement), 3*getNumCities()/2) * 4;
				}
				iTempValue *= AI_averageYieldMultiplier(eLoopYield);
				iTempValue /= 100;

				iTempValue *= AI_yieldWeight(eLoopYield);
				iTempValue /= 100;

				//iBuildValue += iTempValue;
				iRouteValue += iTempValue; // K-Mod
			}
			// K-Mod. Devalue it if we don't have the resources. (based on my code for unit evaluation)
			bool bMaybeMissing = false; // if we don't know if we have the bonus or not
			bool bDefinitelyMissing = false; // if we can see the bonuses, and we know we don't have any.

			for (int i = 0; i < GC.getNUM_ROUTE_PREREQ_OR_BONUSES(); i++)
			{
				BonusTypes ePrereqBonus = (BonusTypes)GC.getInfo(eRoute).getPrereqOrBonus(i);
				if (ePrereqBonus != NO_BONUS)
				{
					if (hasBonus(ePrereqBonus))
					{
						bDefinitelyMissing = false;
						bMaybeMissing = false;
						break;
					}
					else
					{
						if (kTeam.isBonusRevealed(ePrereqBonus) &&
							AI_countOwnedBonuses(ePrereqBonus) == 0)
						{
							bDefinitelyMissing = true;
						}
						else bMaybeMissing = true;
					}
				}
			}
			BonusTypes ePrereqBonus = (BonusTypes)GC.getInfo(eRoute).getPrereqBonus();
			if (ePrereqBonus != NO_BONUS && !hasBonus(ePrereqBonus))
			{
				if ((kTeam.isHasTech((TechTypes)(GC.getInfo(ePrereqBonus).getTechReveal())) ||
					kTeam.isForceRevealedBonus(ePrereqBonus)) &&
					AI_countOwnedBonuses(ePrereqBonus) == 0)
				{
					bDefinitelyMissing = true;
				}
				else bMaybeMissing = true;
			}
			if (bDefinitelyMissing)
				iRouteValue /= 3;
			else if (bMaybeMissing)
				iRouteValue /= 2;
			iBuildValue += iRouteValue;
			// K-Mod end
		}
	}

	//the way feature-remove is done in XML is pretty weird
	//I believe this code needs to be outside the general BuildTypes loop
	//to ensure the feature-remove is only counted once rather than once per build
	//which could be a lot since nearly every build clears jungle...

	FOR_EACH_ENUM(Feature)
	{
		bool bFeatureRemove = false;
		int iChopProduction = 0;
		FOR_EACH_ENUM(Build)
		{
			if (GC.getInfo(eLoopBuild).getFeatureTech(eLoopFeature) == eTech)
			{
				bFeatureRemove = true;
				// I'll assume it's the same for all builds
				iChopProduction = GC.getInfo(eLoopBuild).getFeatureProduction(eLoopFeature);
				break;
			}
		}

		if (bFeatureRemove)
		{	// k146: Divisor was 8, K-Mod sets it to 4, advc: 7.
			int iChopValue = iChopProduction / 7;
			FeatureTypes eFeature = eLoopFeature; // advc
			CvFeatureInfo const& kFeature = GC.getInfo(eFeature);
			if (kFeature.getHealthPercent() < 0 || kFeature.getYieldChange(YIELD_FOOD) +
				kFeature.getYieldChange(YIELD_PRODUCTION) +
				kFeature.getYieldChange(YIELD_COMMERCE) < 0)
			{
				// (k146 wants to set this to 8; advc: leaving it at 6)
				iChopValue += 6;
				/*  k146: Want to add value for the new city sites which become
					available due to this tech - but it isn't easy to evaluate.
					Perhaps we should go as far as counting features at our
					planned sites - but even that isn't exactly what we want. */
				iBuildValue += 28; // advc: instead of 40
			}
			iBuildValue += 4 + iChopValue * (countCityFeatures(eFeature) + 4);
			/*  <advc.129> Very early game: Is the feature blocking a resource?
				(especially Silver, which can now appear on Grassland Forest) */
			if (pCapitalCity != NULL && getNumCities() <= 2)
			{
				FOR_EACH_CITYAI(c, *this)
					iBuildValue += 30 * c->AI_countOvergrownBonuses(eFeature);
			} // </advc.129>
		}
	}

	iValue += iBuildValue;

	// does tech reveal bonus resources
	FOR_EACH_ENUM(Bonus)
	{
		CvBonusInfo const& kLoopBonus = GC.getInfo(eLoopBonus);
		if (kLoopBonus.getTechReveal() == eTech)
		{
			int iRevealValue = 8;
			iRevealValue += AI_bonusVal(eLoopBonus, 1, true) * iCityCount * 2/3;
			BonusClassTypes eBonusClass = (BonusClassTypes)kLoopBonus.getBonusClassType();
			int iBonusClassTotal = (viBonusClassRevealed[eBonusClass] +
					viBonusClassUnrevealed[eBonusClass]);
			//iMultiplier is basically a desperation value
			//it gets larger as the AI runs out of options
			//Copper after failing to get horses is +66%
			//Iron after failing to get horse or copper is +200%
			//but with either copper or horse, Iron is only +25%
			int iMultiplier = 0;
			if (iBonusClassTotal > 0)
			{
				iMultiplier = (viBonusClassRevealed[eBonusClass] -
						viBonusClassHave[eBonusClass]);
				iMultiplier *= 100;
				iMultiplier /= iBonusClassTotal;

				iMultiplier *= (viBonusClassRevealed[eBonusClass] + 1);
				iMultiplier /= ((viBonusClassHave[eBonusClass] *
						iBonusClassTotal) + 1);
			}
			iMultiplier *= std::min(3, getNumCities());
			iMultiplier /= 3;
			iRevealValue *= 100 + iMultiplier;
			iRevealValue /= 100;

			// K-Mod
			// If we don't yet have the 'enable' tech, reduce the value of the reveal.
			if (kLoopBonus.getTechCityTrade() != eTech &&
				!kTeam.isHasTech((TechTypes)kLoopBonus.getTechCityTrade()))
			{
				iRevealValue /= 3;
			}
			// K-Mod end

			iValue += iRevealValue;
		}
		// K-Mod: Value for enabling resources that are already revealed
		else if (kLoopBonus.getTechCityTrade() == eTech &&
			(kTeam.isHasTech((TechTypes)kLoopBonus.getTechReveal()) ||
			kTeam.isForceRevealedBonus(eLoopBonus)))
		{
			int iOwned = AI_countOwnedBonuses(eLoopBonus);
			if (iOwned > 0)
			{
				int iEnableValue = 4;
				iEnableValue += AI_bonusVal(eLoopBonus, 1, true) * iCityCount;
				iEnableValue *= (iOwned > 1) ? 120 : 100;
				iEnableValue /= 100;

				iValue += iEnableValue;
			}
		}
		// K-Mod end
	}

	/* ------------------ Unit Value  ------------------ */
	bool bEnablesUnitWonder;
	iValue += AI_techUnitValue( eTech, iPathLength, bEnablesUnitWonder);

	if (bEnablesUnitWonder
			&& getTotalPopulation() > 5) // k146
	{	// <k146>
		const int iBaseRand = std::max(10, 110-30*iPathLength); // 80, 50, 20, 10
		int iWonderRandom = (bAsync ?
				GC.getASyncRand().get(iBaseRand, "AI Research Wonder Unit ASYNC") :
				GC.getGame().getSorenRandNum(iBaseRand, "AI Research Wonder Unit"));
		int iFactor = 100 * std::min(iCityCount, iCityTarget) /
				std::max(1, iCityTarget);
		iValue += (iWonderRandom + (bCapitalAlone ? 50 : 0)) * iFactor / 100;
		iRandomMax += iBaseRand * iFactor / 100;
		// </k146>
		iRandomFactor += iWonderRandom * iFactor / 100;
	}


	/* ------------------ Building Value  ------------------ */
	bool bEnablesWonder
			/*  advc.001n: AI_techBuildingValue now guaranteed to assign a value,
				but wasn't previously, and caller shouldn't rely on it. */
			= false;
	iValue += AI_techBuildingValue(eTech, bAsync, bEnablesWonder); // changed by K-Mod
	iValue -= AI_obsoleteBuildingPenalty(eTech, bAsync); // K-Mod!

	// K-Mod. Scale the random wonder bonus based on leader personality.
	/*  k146 (note): the value of the building itself was already counted by
		AI_techBuildingValue. This extra value is just because we like wonders. */
	if (bEnablesWonder && getTotalPopulation() > 5)
	{
		const int iBaseRand = std::max(10, 110-30*iPathLength); // 80, 50, 20, 10 (was 300)
		int iWonderRandom = (bAsync ?
				GC.getASyncRand().get(iBaseRand, "AI Research Wonder Building ASYNC") :
				GC.getGame().getSorenRandNum(iBaseRand, "AI Research Wonder Building"));
		int iFactor = 10 + GC.getInfo(getPersonalityType()).getWonderConstructRand(); // note: highest value of iWonderConstructRand 50 in the default xml.
		iFactor += AI_atVictoryStage(AI_VICTORY_CULTURE1) ? 15 : 0;
		iFactor += AI_atVictoryStage(AI_VICTORY_CULTURE2) ? 10 : 0;
		iFactor /= bAdvancedStart ? 4 : 1;
		iFactor = iFactor * std::min(iCityCount, iCityTarget) / std::max(1, iCityTarget);
		iFactor += 50; // k146: This puts iFactor around 100, roughly.
		iValue += (40 // k146: was 200
				+ iWonderRandom) * iFactor / 100;
		iRandomMax += iBaseRand * iFactor / 100;
		iRandomFactor += iWonderRandom * iFactor / 100;
	}
	// K-Mod end

	bool bEnablesProjectWonder = false;
	// k146: Code moved into auxiliary function
	iValue += AI_techProjectValue(eTech, iPathLength, bEnablesProjectWonder);
	if (bEnablesProjectWonder)
	{
		int iWonderRandom = (bAsync ?
				GC.getASyncRand().get(56, "AI Research Wonder Project ASYNC") :
				GC.getGame().getSorenRandNum(56, "AI Research Wonder Project"));
		iValue += iWonderRandom;

		iRandomMax += 56;
		iRandomFactor += iWonderRandom;
	}


	/* ------------------ Process Value  ------------------ */
	bool bGoodProcess = false;
	FOR_EACH_ENUM(Process)
	{
		CvProcessInfo const& kLoopProcess = GC.getInfo(eLoopProcess);
		if (kLoopProcess.getTechPrereq() != eTech)
			continue; // advc

		iValue += 28;

		FOR_EACH_ENUM(Commerce)
		{
			int iTempValue = (kLoopProcess.getProductionToCommerceModifier(eLoopCommerce) * 4);

			if (iTempValue <= 0) // K-Mod (check out what would happen to "bGoodProcess" without this bit.  "oops.")
				continue;

			iTempValue *= AI_commerceWeight(eLoopCommerce);
			iTempValue /= 100;

			if (eLoopCommerce == COMMERCE_GOLD || eLoopCommerce == COMMERCE_RESEARCH)
				bGoodProcess = true;
			else if (eLoopCommerce == COMMERCE_CULTURE && AI_atVictoryStage(AI_VICTORY_CULTURE1))
				iTempValue *= 2; // k146: was 3

			iValue += iTempValue * iCityCount / 100;
		}
	}
	// <cdtw.3>
	bool bHighUnitCost = false;
	// After some testing and tweaking, this still doesn't seem useful:
	/*if(bGoodProcess && !bFinancialTrouble && !AI_isFocusWar() && getCurrentEra() < 2 &&
			GET_TEAM(getTeam()).AI_getTotalWarOddsTimes100() <= 400) {
		int iNetCommerce = 1 + getCommerceRate(COMMERCE_GOLD) +
				getCommerceRate(COMMERCE_RESEARCH) + std::max(0, getGoldPerTurn());
		int iUnitCost = calculateUnitCost();
		if(iUnitCost * 20 > iNetCommerce)
			bHighUnitCost = true;
	}*/ // </cdtw.3>
	if (bGoodProcess && (bFinancialTrouble /* cdtw.3: */ || bHighUnitCost))
	{
		bool bHaveGoodProcess = false;
		FOR_EACH_ENUM(Process)
		{
			CvProcessInfo const& kLoopProcess = GC.getInfo(eLoopProcess);
			if (kTeam.isHasTech((TechTypes)kLoopProcess.getTechPrereq()))
			{
				bHaveGoodProcess = (kLoopProcess.getProductionToCommerceModifier(COMMERCE_GOLD) +
						kLoopProcess.getProductionToCommerceModifier(COMMERCE_RESEARCH) > 0);
				if (bHaveGoodProcess)
					break;
			}
		}
		if (!bHaveGoodProcess)
		{
			iValue += (8*iCityCount // k146: was 6*...
				/ (bHighUnitCost ? 2 : 1)); // cdtw.3
		}
	}
	/* ------------------ Civic Value  ------------------ */
	FOR_EACH_ENUM(Civic)
	{
		CvCivicInfo const& kLoopCivic = GC.getInfo(eLoopCivic);
		if (kLoopCivic.getTechPrereq() == eTech
			&& !canDoCivics(eLoopCivic)) // k146
		{
			CivicTypes eCivic = getCivics((CivicOptionTypes)kLoopCivic.getCivicOptionType());

			int iCurrentCivicValue = AI_civicValue(eCivic); // Note: scale of AI_civicValue is 1 = commerce/turn
			int iNewCivicValue = AI_civicValue(eLoopCivic);
			if (iNewCivicValue > iCurrentCivicValue)
			{
				//iValue += std::min(2400, (2400 * (iNewCivicValue - iCurrentCivicValue)) / std::max(1, iCurrentCivicValue));
				/*  <k146> This should be 4 *, but situational values like civics
					are more interesting than static values */
				iValue += 5 * (iNewCivicValue - iCurrentCivicValue);
				/*  just a little something extra for the early game...
					to indicate that we may have undervalued the civic's
					long term appeal. */
				iValue += 4 * std::min(iCityCount, iCityTarget); // </k146>
			}

			if (eCivic == GC.getInfo(getPersonalityType()).getFavoriteCivic())
			{
				/*  k146: favourite civic is already taken into account in the
					civic evaluation above. */
				iValue += 6*iCityCount;
			}
			else iValue += 4*iCityCount;
		}
	}

	if (iPathLength <= 2)
	{
		bool bFirst = GC.getGame().countKnownTechNumTeams(eTech) == 0; // K-Mod

		// K-Mod. Make a list of corporation HQs that this tech enables.
		// (note: for convenience, I've assumed that each corp has at most one type of HQ building.)
		std::vector<BuildingTypes> corpHQs(GC.getNumCorporationInfos(), NO_BUILDING);
		FOR_EACH_ENUM(Building)
		{
			CvBuildingInfo const& kLoopBuilding = GC.getInfo(eLoopBuilding);
			if (kLoopBuilding.getFoundsCorporation() != NO_CORPORATION)
			{
				if (kLoopBuilding.isTechRequired(eTech))
				{
					FAssert(kLoopBuilding.getFoundsCorporation() < (int)corpHQs.size());
					corpHQs[kLoopBuilding.getFoundsCorporation()] = eLoopBuilding;
				}
			}
		}
		// K-Mod end (the corpHQs map is used in the next section)

		// note: K-mod has moved this section from out of the first-discoverer block
		FOR_EACH_ENUM(Corporation)
		{
			/*if (GC.getInfo(eLoopCorporation).getTechPrereq() == eTech) {
				if (!GC.getGame().isCorporationFounded(eLoopCorporation)) {
					iValue += 100 + (bAsync ? GC.getASyncRand().get(2400, "AI Research Corporation ASYNC") : GC.getGame().getSorenRandNum(2400, "AI Research Corporation"));
				}
			}*/ // BtS
			// K-Mod
			if (!GC.getGame().isCorporationFounded(eLoopCorporation))
			{
				int iMissingTechs = 0;
				bool bCorpTech = false;

				if (GC.getInfo(eLoopCorporation).getTechPrereq() == eTech)
				{
					bCorpTech = bFirst; // only good if we are the first to get the tech
					// missing tech stays at 0, because this is a special case. (ie. no great person required)
				}
				else if (corpHQs[eLoopCorporation] != NO_BUILDING)
				{
					CvBuildingInfo const& kHQBuilding = GC.getInfo(corpHQs[eLoopCorporation]);
					TechTypes eHQTech = kHQBuilding.getPrereqAndTech();
					if (eHQTech == eTech || kTeam.isHasTech(eHQTech) || canResearch(eHQTech))
					{
						bCorpTech = true;
						// Count the required techs. (cf. isTechRequiredForBuilding)

						iMissingTechs += (!kTeam.isHasTech(eHQTech) ? 1 : 0);

						for (int i = 0; i < GC.getNUM_BUILDING_AND_TECH_PREREQS(); i++)
						{
							iMissingTechs += (!kTeam.isHasTech(kHQBuilding.getPrereqAndTechs(i)) ? 1 : 0);
						}

						SpecialBuildingTypes eSpecial = kHQBuilding.getSpecialBuildingType();
						iMissingTechs += ((eSpecial != NO_SPECIALBUILDING &&
								!kTeam.isHasTech((TechTypes)GC.getInfo(eSpecial).getTechPrereq()))
								? 1 : 0);

						FAssert(iMissingTechs > 0);
					}
				}
				if (bCorpTech)
				{
					int iCorpValue = AI_corporationValue(eLoopCorporation); // roughly 100x commerce / turn / city.
					iCorpValue = iCorpValue * iCityCount * 2 / 100; // rescaled
					if (iMissingTechs == 0)
						iCorpValue = 3 * iCorpValue / 2; // this won't happen in standard BtS - but it might in some mods.
					else iCorpValue /= iMissingTechs;

					if (iCorpValue > 0)
					{
						if (isNoCorporations())
							iCorpValue /= 4;

						if (AI_getFlavorValue(FLAVOR_GOLD) > 0)
						{
							iCorpValue *= 20 + AI_getFlavorValue(FLAVOR_GOLD);
							iCorpValue /= 20;
							iValue += iCorpValue/2;
							iValue += (bAsync ?
									GC.getASyncRand().get(iCorpValue, "AI Research Corporation ASYNC") :
									GC.getGame().getSorenRandNum(iCorpValue, "AI Research Corporation"));
							iRandomMax += iCorpValue;
						}
						else
						{
							iValue += iCorpValue/3;
							iValue += (bAsync ?
									GC.getASyncRand().get(4*iCorpValue/3, "AI Research Corporation ASYNC") :
									GC.getGame().getSorenRandNum(4*iCorpValue/3, "AI Research Corporation"));
							iRandomMax += 4*iCorpValue/3;
						}
					}
				}
			} // K-Mod end
		}

		//if (GC.getGame().countKnownTechNumTeams(eTech) == 0)
		// K-Mod
		if (bFirst)
		{
			int iRaceModifier = 0; // 100 means very likely we will be first, -100 means very unlikely. 0 is 'unknown'.
			{
				int iTotalPlayers = 0;
				int iCount = 0;
				for (PlayerTypes i = (PlayerTypes)0; i < MAX_CIV_PLAYERS; i = (PlayerTypes)(i+1))
				{
					const CvPlayer& kLoopPlayer = GET_PLAYER(i);
					if (kLoopPlayer.getTeam() != getTeam() && kLoopPlayer.isAlive())
					{
						iTotalPlayers++; // count players even if we haven't met them. (we know they're out there...)
						if (kTeam.isHasMet(kLoopPlayer.getTeam()) && (iPathLength <= 1 != kLoopPlayer.canResearch(eTech)))
						{
							iCount++; // if path is <= 1, count civs who can't research it. if path > 1, count civs who can research it.
						}
					}
				}
				iRaceModifier = (iPathLength <= 1 ? 100 : -100) * iCount / std::max(1, iTotalPlayers);
			}
			FAssert(iRaceModifier >= -100 && iRaceModifier <= 100);
		// K-Mod end
			int iReligionValue = 0;
			int iPotentialReligions = 0;
			int iAvailableReligions = 0; // K-Mod
			FOR_EACH_ENUM(Religion)
			{
				TechTypes eReligionTech = (TechTypes)GC.getInfo(eLoopReligion).getTechPrereq();
				/*if (kTeam.isHasTech(eReligionTech)) {
					if (!(GC.getGame().isReligionSlotTaken((ReligionTypes)iJ)))
						iPotentialReligions++;
				}*/ // BtS
				// K-Mod. iPotentialReligions will only be non-zero during the first few turns of advanced start games. Otherwise it is always zero.
				// Presumably that's what the original developers intended... so I'm going to leave that alone, and create a new value: iAvailableReligions.
				if (!GC.getGame().isReligionSlotTaken(eLoopReligion))
				{
					iAvailableReligions++;
					if (kTeam.isHasTech(eReligionTech))
						iPotentialReligions++;
				}
				// K-Mod end

				if (eReligionTech == eTech)
				{
					if (!GC.getGame().isReligionSlotTaken(eLoopReligion))
					{
						int iRoll = 150; // k146: was 400

						if (!GC.getGame().isOption(GAMEOPTION_PICK_RELIGION))
						{
							ReligionTypes eFavorite = (ReligionTypes)GC.getInfo(getLeaderType()).
									getFavoriteReligion();
							if (eFavorite != NO_RELIGION)
							{
								if (eLoopReligion == eFavorite)
									iRoll = iRoll * 3/2;
								else iRoll = iRoll * 2/3;
							}
						}

						iRoll *= 200 + iRaceModifier;
						iRoll /= 200;
						if (iRaceModifier > 10 && AI_getFlavorValue(FLAVOR_RELIGION) > 0)
							iReligionValue += iRoll * (iRaceModifier-10) / 300;

						iReligionValue += (bAsync ?
								GC.getASyncRand().get(iRoll, "AI Research Religion ASYNC") :
								GC.getGame().getSorenRandNum(iRoll, "AI Research Religion"));
						// Note: relation value will be scaled down by other factors in the next section.
						iRandomMax += iRoll; // (Note: this doesn't include factors used later.)
					}
				}
			}

			if (iReligionValue > 0)
			{
				if (AI_atVictoryStage(AI_VICTORY_CULTURE1))
				{
					iReligionValue += 50; // k146: was 100

					if (countHolyCities() < 1)
					{
						//iReligionValue += 200;
						// k146: Replacing the line above
						iReligionValue += (iCityCount > 1 ? 100 : 0);
					}
				}
				else iReligionValue /= 1 + countHolyCities() + (iPotentialReligions > 0 ? 1 : 0);

				if (countTotalHasReligion() == 0 && iPotentialReligions == 0)
				{
					bool bNeighbouringReligions = false;
					for (PlayerTypes i = (PlayerTypes)0; !bNeighbouringReligions && i < MAX_CIV_PLAYERS; i = (PlayerTypes)(i+1))
					{
						const CvPlayer& kLoopPlayer = GET_PLAYER(i);
						if (kLoopPlayer.isAlive() && kTeam.isHasMet(kLoopPlayer.getTeam()) &&
							kLoopPlayer.getStateReligion() != NO_RELIGION &&
							// <advc.001>
							((getTeam() != kLoopPlayer.getTeam() && kTeam.AI_hasSharedPrimaryArea(kLoopPlayer.getTeam())) ||
							(getTeam() == kLoopPlayer.getTeam() && getID() != kLoopPlayer.getID() && AI_hasSharedPrimaryArea(kLoopPlayer.getID()))))
						/*  The case where both civs are on the same team previously led to a
							failed assertion in CvTeamAI::hasSharedPrimaryArea.
							Added a new clause that checks if the civs (not their teams)
							share a primary area. Rationale: If a team member on the same
							continent has already founded a religion, we should be less inclined
							to found another (will soon spread to us). </advc.001> */
						{
							bNeighbouringReligions = true;
						}
					}
					if (!bNeighbouringReligions)
					{
						iReligionValue += 20;
						if (AI_getFlavorValue(FLAVOR_RELIGION))
							iReligionValue += 28 + 4 * AI_getFlavorValue(FLAVOR_RELIGION);
						if (GC.getGame().getElapsedGameTurns() >= 32 * GC.getInfo(GC.getGame().getGameSpeedType()).getResearchPercent() / 100)
							iReligionValue += 60;
						if (AI_atVictoryStage(AI_VICTORY_CULTURE1))
							iReligionValue += 84;
					}

					if (iAvailableReligions <= 4 || AI_getFlavorValue(FLAVOR_RELIGION) > 0)
					{
						iReligionValue *= 2;
						iReligionValue += 56 + std::max(0, 6 - iAvailableReligions)*28;
					}
					else
					{
						iReligionValue = iReligionValue*3/2;
						iReligionValue += 28;
					}
				}

				if (AI_isDoStrategy(AI_STRATEGY_DAGGER))
				{
					iReligionValue /= 2;
				}
				iValue += iReligionValue * std::min(iCityCount, iCityTarget) / std::max(1, iCityTarget);
			}

			// K-Mod note: I've moved corporation value outside of this block. (because you don't need to be first to the tech to get the corp!)

			if (getTechFreeUnit(eTech) != NO_UNIT)
			{
				// K-Mod
				int iRoll = 2 * (100 + AI_getGreatPersonWeight((UnitClassTypes)GC.getInfo(eTech).getFirstFreeUnitClass()));
				// I've diluted the weight because free great people doesn't have the negative effect of making it harder to get more great people
				iRoll *= 200 + iRaceModifier;
				iRoll /= 200;
				if (iRaceModifier > 20 && AI_getFlavorValue(FLAVOR_SCIENCE) + AI_getFlavorValue(FLAVOR_GROWTH) > 0)
					iValue += iRoll * (iRaceModifier-10) / 400;
				iValue += (bAsync ?
						GC.getASyncRand().get(iRoll, "AI Research Great People ASYNC") :
						GC.getGame().getSorenRandNum(iRoll, "AI Research Great People"));
				iRandomMax += iRoll;
				// K-Mod end
			}

			//iValue += (kTechInfo.getFirstFreeTechs() * (200 + (bCapitalAlone ? 400 : 0) + (bAsync ? GC.getASyncRand().get(3200, "AI Research Free Tech ASYNC") : GC.getGame().getSorenRandNum(3200, "AI Research Free Tech"))));
			// K-Mod. Very rough evaluation of free tech.
			if (kTechInfo.getFirstFreeTechs() > 0)
			{	// <k146>
				//int iRoll = 1600; // previous base value - which we'll now calculate based on costs of currently researchable tech
				int iBase = 100;

				{ // cf. free tech in AI_buildingValue
					int iTotalTechCost = 0;
					int iMaxTechCost = 0;
					int iTechCount = 0;

					FOR_EACH_ENUM(Tech)
					{
						if (canResearch(eLoopTech, false, true))
						{
							int iTechCost = kTeam.getResearchCost(eLoopTech);
							iTotalTechCost += iTechCost;
							iTechCount++;
							iMaxTechCost = std::max(iMaxTechCost, iTechCost);
						}
					}
					if (iTechCount > 0)
					{
						int iTechValue =  ((iTotalTechCost / iTechCount) + iMaxTechCost)/2;
						iBase += iTechValue * 20 / GC.getInfo(GC.getGame().getGameSpeedType()).getResearchPercent();
					}
				} // I'm expecting iRoll to be ~600.
				iBase *= 200 + iRaceModifier + (AI_getFlavorValue(FLAVOR_SCIENCE) ? 50 : 0);
				iBase /= 200;
				int iTempValue = iBase; // value for each free tech
				if (iRaceModifier > 20 && iRaceModifier < 100) // save the free tech if we have no competition!
					iTempValue += iBase * (iRaceModifier-20) / 200;
				//int iTempValue = (iRaceModifier >= 0 ? 196 : 98) + (bCapitalAlone ? 28 : 0); // some value regardless of race or random.
				iTempValue += (bAsync ?
						GC.getASyncRand().get(iBase, "AI Research Free Tech ASYNC") :
						GC.getGame().getSorenRandNum(iBase, "AI Research Free Tech"));
				// </k146>
				iValue += iTempValue * kTechInfo.getFirstFreeTechs();
				iRandomMax += iBase * kTechInfo.getFirstFreeTechs();
			}
			// K-Mod end
		}
	}

	iValue += kTechInfo.getAIWeight();

	/*if (!isHuman()) {
		int iFlavorValue = 0;
		for (int iJ = 0; iJ < GC.getNumFlavorTypes(); iJ++)
			iFlavorValue += AI_getFlavorValue((FlavorTypes)iJ) * kTechInfo.getFlavorValue(iJ) * 5;
		iValue += iFlavorValue * std::min(iCityCount, iCityTarget) / std::max(1, iCityTarget);
	}*/ // <advc.020> Replacing the above
	double flavorFactor = 1.0;
	FOR_EACH_ENUM(Flavor)
	{
		flavorFactor += 0.22 * AI_getFlavorValue(eLoopFlavor) *
				kTechInfo.getFlavorValue(eLoopFlavor) * std::min(iCityCount, iCityTarget) /
				(100.0 * std::max(1, iCityTarget));
	}
	if(iValue > 0)
	{
		/*  Don't want a higher overall value on account of flavor. Therefore,
			divide by a sort of median flavor multiplier (1.03).
			(Although e.g. tech-for-gold trades aren't made based on
			AI_techValue, so I don't think this is crucial.)*/
		iValue = ::round(iValue * flavorFactor / 1.03);
	} // </advc.020>
	if (kTechInfo.isRepeat())
		iValue /= 4;
	// <k146>
	if (bFreeTech)
	{	// <advc.004x> Don't adjust to research turns during anarchy
		int iTurnsLeft = getResearchTurnsLeft(eTech, false);
		if(iTurnsLeft >= 0) // </advc.004x>
		{
			iValue += iTurnsLeft / 5;
			iValue *= 1000; // roughly normalise with usual value. (cf. code below)
			iValue /= 10 * GC.getInfo(GC.getGame().getGameSpeedType()).getResearchPercent();
		}
	} // </k146>
	else if (iValue > 0)
	{
		// this stops quick speed messing up.... might want to adjust by other things too... (Firaxis comment)
		int iAdjustment = GC.getInfo(GC.getGame().getGameSpeedType()).getResearchPercent();
		int iTurnsLeft = getResearchTurnsLeftTimes100((eTech), false);
		bool bCheapBooster = (iTurnsLeft >= 0 && // advc.004x
				iTurnsLeft < (2 * iAdjustment) &&
				eFromPlayer == NO_PLAYER && // advc.144
				(0 == (bAsync ? GC.getASyncRand().get(5, "AI Choose Cheap Tech") :
				GC.getGame().getSorenRandNum(5, "AI Choose Cheap Tech"))));
		/*  <advc.004x> Shouldn't normally be called during anarchy, but if it is,
			assume a usual time to research. */
		if(iTurnsLeft < 0)
			iTurnsLeft = 10; // </advc.004x>
		/*  <advc.144> Will get it immediately, but eFromPlayer is less likely to
			give it to us if it's an expensive tech. */
		if(eFromPlayer != NO_PLAYER)
			iTurnsLeft /= 2; // </advc.144>
		//iValue *= 100000;
		iValue *= 1000; // k146
		iValue /= (iTurnsLeft + (bCheapBooster ? 1 : 5) * iAdjustment);
	}

	//Tech Groundbreaker
	//if (!GC.getGame().isOption(GAMEOPTION_NO_TECH_TRADING))
	if (!GC.getGame().isOption(GAMEOPTION_NO_TECH_TRADING) && !isBarbarian() // K-Mod
		&& iPathLength <= 1 // k146
		// advc.144: Ask for a tech that we need, not one we could trade with.
		&& eFromPlayer == NO_PLAYER)
	{
		if (kTechInfo.isTechTrading() || kTeam.isTechTrading())
		{
			// K-Mod. We should either consider 'tech ground breaking' for all techs this turn, or none at all - otherwise it will just mess things up.
			// Also, if the value adjustment isn't too big, it should be ok to do this most of the time.
			if (AI_getStrategyRand(GC.getGame().getGameTurn()) %
				std::max(1, GC.getInfo(getPersonalityType()).getContactRand(CONTACT_TRADE_TECH)) == 0)
			{
				int iAlreadyKnown = 0;
				int iPotentialTrade = 0;
				for (TeamTypes i = (TeamTypes)0; i < MAX_CIV_TEAMS; i = (TeamTypes)(i+1))
				{
					const CvTeamAI& kLoopTeam = GET_TEAM(i);
					if (i != getTeam() && kLoopTeam.isAlive() && kTeam.isHasMet(i))
					{
						if (kLoopTeam.isHasTech(eTech) || (canSeeResearch(kLoopTeam.getLeaderID()) && GET_PLAYER(kLoopTeam.getLeaderID()).getCurrentResearch() == eTech))
							iAlreadyKnown++;
						else if (!kTeam.isAtWar(i) && kLoopTeam.AI_techTrade(NO_TECH, getTeam()) == NO_DENIAL && kTeam.AI_techTrade(NO_TECH, i) == NO_DENIAL)
							iPotentialTrade++;
					}
				}

				int iTradeModifier = iPotentialTrade * (GC.getGame().isOption(GAMEOPTION_NO_TECH_BROKERING) ?
						30 : 15); // k146: was 20:12
				iTradeModifier *= 200 - GC.getInfo(getPersonalityType()).getTechTradeKnownPercent();
				iTradeModifier /= 150;
				iTradeModifier /= 1 + 2 * iAlreadyKnown;
				iValue *= 100 + std::min(100, iTradeModifier);
				iValue /= 100;
			}
			// K-Mod end
		}
	}

	if (AI_atVictoryStage(AI_VICTORY_CULTURE3))
	{
		int iCVValue = AI_cultureVictoryTechValue(eTech);
		/*iValue *= (iCVValue + 10);
		iValue /= ((iCVValue < 100) ? 400 : 100);*/ // BtS
		// K-Mod. I don't think AI_cultureVictoryTechValue is accurate enough to play such a large role.
		iValue *= (iCVValue + 100);
		iValue /= 200;
		// K-Mod end
	}

	/*  K-Mod, 12/sep/10, Karadoc
		Use a random _factor_ at the end. */
	// advc: Renamed from iRandomFactor, which is already defined (though unused).
	int iFinalRandomFactor = (bAsync ?
			GC.getASyncRand().get( // advc (comment): Should randomness be used at all when recommending tech (bAsync)?
			200, "AI Research factor ASYNC") : // k146: was 100
			GC.getGame().getSorenRandNum(
			200, "AI Research factor")); // k146: was 100
	// k146: was 950+...
	iValue *= (900 + iFinalRandomFactor); // between 90% and 110%
	iValue /= 1000;

	/*iValue = range(iValue, 0, MAX_INT);
	return iValue;*/ // K-Mod end
	// <advc.001> The above won't work for long long
	iValue = std::max(0LL, iValue);
	return ::longLongToInt(iValue); // </advc.001>
}

// K-mod. This function returns the (positive) value of the buildings we will lose by researching eTech.
// (I think it's crazy that this stuff wasn't taken into account in original BtS)
int CvPlayerAI::AI_obsoleteBuildingPenalty(TechTypes eTech, bool bConstCache) const
{
	int iTotalPenalty = 0;
	CvCivilization const& kCiv = getCivilization(); // advc.003w
	for (int i = 0; i < kCiv.getNumBuildings(); i++)
	{
		BuildingTypes eLoopBuilding = kCiv.buildingAt(i);
		SpecialBuildingTypes eSpecial = GC.getInfo(eLoopBuilding).getSpecialBuildingType();
		bool bObsolete = (GC.getInfo(eLoopBuilding).getObsoleteTech() == eTech ||
				(eSpecial != NO_SPECIALBUILDING &&
				GC.getInfo(eSpecial).getObsoleteTech() == eTech));
		if (!bObsolete || getBuildingClassCount(kCiv.buildingClassAt(i)) <= 0)
			continue;

		FOR_EACH_CITYAI(pLoopCity, *this)
		{
			int n = pLoopCity->getNumActiveBuilding(eLoopBuilding);
			if (n > 0)
			{
				iTotalPenalty += n * pLoopCity->AI_buildingValue(eLoopBuilding, 0, 0, bConstCache,
						true, false, /*bObsolete=*/true); // advc.004c
			}
		}
	}
	// iScale is set more or less arbitrarily based on trial and error.
	int iScale = 50;

	if (AI_getFlavorValue(FLAVOR_SCIENCE) > 0)
		iScale -= 10 + AI_getFlavorValue(FLAVOR_SCIENCE);
	iScale = std::max(10, iScale);

	iTotalPenalty *= iScale;
	iTotalPenalty /= 100;

	return iTotalPenalty;
}

// K-Mod. The original code had some vague / ad-hoc calculations to evalute the buildings enabled by techs.
// It was completely separate from CvCity::AI_buildingValue, and it ignored _most_ building functionality.
// I've decided it would be better to use the thorough calculations already present in AI_buildingValue.
// This should make the AI a bit smarter and the code more easy to update. But it might run slightly slower.
//
// Note: There are some cases where this new kind of evaluation is significantly flawed. For example,
// Assembly Line enables factories and coal power plants; since these two buildings are highly dependant on
// one another, their individual evaluation will greatly undervalue them. Another example: catherals can't
// be built in every city, but this function will evaluate them as if they could, thus overvaluing them.
// But still, the original code was far worse - so I think I'll just tolerate such flaws for now.
// The scale is roughly 4 = 1 commerce per turn.
int CvPlayerAI::AI_techBuildingValue(TechTypes eTech, bool bConstCache, bool& bEnablesWonder) const
{
	PROFILE_FUNC();
	/*  advc.004x (comment): I had changed this to !isAnarchy()||isHuman(), but
		now that humans aren't prompted to choose research during anarchy,
		the original assertion should hold. */
	FAssertMsg(!isAnarchy(), "AI_techBuildingValue should not be used while in anarchy. Results will be inaccurate.");
	/*  advc.001n: Don't rely on caller to initialize this. Initialization was
		still present in the BBAI code, must've been lost in the K-Mod rewrite. */
	bEnablesWonder = false;
	int iTotalValue = 0;
	CvGame const& g = GC.getGame();
	CvGameSpeedInfo const& kGameSpeed = GC.getInfo(g.getGameSpeedType());
	std::vector<const CvCityAI*> relevant_cities; // (this will be populated when we find a building that needs to be evaluated)

	CvCivilization const& kCiv = getCivilization(); // advc.003w
	for (int i = 0; i < kCiv.getNumBuildings(); i++)
	{
		BuildingTypes eLoopBuilding = kCiv.buildingAt(i);
		CvBuildingInfo const& kLoopBuilding = GC.getInfo(eLoopBuilding);
		if (!kLoopBuilding.isTechRequired(eTech))
			continue; // this building class is not relevent

		
		if (GET_TEAM(getTeam()).isObsoleteBuilding(eLoopBuilding))
			continue; // already obsolete

		if (kLoopBuilding.isWorldWonder())
		{
			if (g.isBuildingClassMaxedOut(kCiv.buildingClassAt(i)) ||
				kLoopBuilding.getProductionCost() < 0)
			{
				continue; // either maxed out, or it's a special building that we don't want to evaluate here.
			}
			if (kLoopBuilding.getPrereqAndTech() == eTech)
				bEnablesWonder = true; // a buildable world wonder
		}

		bool bLimitedBuilding = (kLoopBuilding.isLimited() ||
				kLoopBuilding.getProductionCost() < 0);

		// Populate the relevant_cities list if we haven't done so already.
		if (relevant_cities.empty())
		{	// advc (comment): g.getStartTurn() wouldn't work well for colonial vassals
			int iEarliestTurn = MAX_INT;
			FOR_EACH_CITY(pLoopCity, *this)
				iEarliestTurn = std::min(iEarliestTurn, pLoopCity->getGameTurnAcquired());
			int iCutoffTurn = (g.getGameTurn() + iEarliestTurn) / 2 + kGameSpeed.getVictoryDelayPercent() * 30 / 100;
			// iCutoffTurn corresponds 50% of the time since our first city was aquired, with a 30 turn (scaled) buffer.
			FOR_EACH_CITYAI(pLoopCity, *this)
			{
				if (pLoopCity->getGameTurnAcquired() < iCutoffTurn || pLoopCity->isCapital())
					relevant_cities.push_back(pLoopCity);
			}

			if (relevant_cities.empty())
			{
				FAssertMsg(isBarbarian(), "No relevant cities in AI_techBuildingValue");
				return 0;
			}
		}
		//

		// Perform the evaluation.
		int iBuildingValue = 0;
		for (size_t i = 0; i < relevant_cities.size(); i++)
		{
			if (relevant_cities[i]->canConstruct(eLoopBuilding, false, false, true, true) ||
				/*  (ad-hoc). National wonders often require something which is
					unlocked by the same tech. So I'll disregard the construction
					requirements. */
				(kLoopBuilding.isNationalWonder() &&
				!relevant_cities[i]->isNationalWondersMaxed())) // advc.131
			{
				if (bLimitedBuilding) // TODO: don't assume 'limited' means 'only one'.
				{
					iBuildingValue = std::max(iBuildingValue,
							relevant_cities[i]->AI_buildingValue(eLoopBuilding, 0, 0, bConstCache));
				}
				else iBuildingValue += relevant_cities[i]->AI_buildingValue(eLoopBuilding, 0, 0, bConstCache);
			}
		}
		if (iBuildingValue > 0)
		{
			// Scale the value based on production cost and special production multipliers.
			if (kLoopBuilding.getProductionCost() > 0)
			{
				int iMultiplier = 0;
				for (TraitTypes i = (TraitTypes)0; i < GC.getNumTraitInfos(); i=(TraitTypes)(i+1))
				{
					if (hasTrait(i))
					{
						iMultiplier += kLoopBuilding.getProductionTraits(i);
						if (kLoopBuilding.getSpecialBuildingType() != NO_SPECIALBUILDING)
						{
							iMultiplier += GC.getInfo(kLoopBuilding.getSpecialBuildingType()).getProductionTraits(i);
						}
					}
				}
				if (kLoopBuilding.isAnyBonusProductionModifier()) // advc.003t
				{
					for (BonusTypes i = (BonusTypes)0; i < GC.getNumBonusInfos(); i=(BonusTypes)(i+1))
					{
						if (hasBonus(i))
							iMultiplier += kLoopBuilding.getBonusProductionModifier(i);
					}
				}
				int iScale = 15 * (3 + getCurrentEra()); // hammers (ideally this would be based on the average city yield or something like that.)
				iScale += AI_isCapitalAreaAlone() ? 30 : 0; // can afford to spend more on infrastructure if we are alone.
				// decrease when at war
				//if (GET_TEAM(getTeam()).getAnyWarPlanCount(true) > 0)
				if(AI_isFocusWar()) // advc.105
					iScale = iScale * 2/3;
				// increase scale for limited wonders, because they don't need to be built in every city.
				if (kLoopBuilding.isLimited())
					iScale *= std::min((int)relevant_cities.size(), GC.getInfo(GC.getMap().getWorldSize()).getTargetNumCities());
				// adjust for game speed
				iScale = iScale * kGameSpeed.getBuildPercent() / 100;
				// use the multiplier we calculated earlier
				iScale = iScale * (100 + iMultiplier) / 100;
				//
				iBuildingValue *= 100;
				iBuildingValue /= std::max(33, 100 * kLoopBuilding.getProductionCost() / std::max(1, iScale));
			}
			//
			iTotalValue += iBuildingValue;
		}
	} // <advc.131>
	bool const bVeryEarly = (getNumCities() == 1 && getCapitalCity() != NULL &&
			getCapitalCity()->getPopulation() < 4); // </advc.131>
	int iScale = (AI_isDoStrategy(AI_STRATEGY_ECONOMY_FOCUS)
			/* <advc.131> */ && !bVeryEarly) /* </advc.131> */ ? 180 : 100;
	/*if (getNumCities() == 1 && getCurrentEra() == g.getStartEra())
		iScale/=2;*/ // I expect we'll want to be building mostly units until we get a second city.
	// <advc.131> Replacing the above
	if(bVeryEarly)
		iScale = ::round(iScale / (5.0 - getCapitalCity()->getPopulation()));
	// </advc.131>
	iTotalValue *= iScale;
	iTotalValue /= 120;

	return iTotalValue;
}
// K-Mod end

// This function has been mostly rewritten for K-Mod
// The final scale is roughly 4 = 1 commerce per turn.
int CvPlayerAI::AI_techUnitValue(TechTypes eTech, int iPathLength, bool& bEnablesUnitWonder) const
{
	PROFILE_FUNC();
	const CvTeamAI& kTeam = GET_TEAM(getTeam()); // K-Mod

	bool bWarPlan = AI_isFocusWar(); // advc.105
			//(kTeam.getAnyWarPlanCount(true) > 0);
	if (!bWarPlan)
	{
		// Aggressive players will stick with war techs
		if (kTeam.AI_getTotalWarOddsTimes100() > 400)
		{
			bWarPlan = true;
		}
	}
	// <k146> Get some basic info.
	bool bLandWar = false;
	bool bAnyAssault = false;
	{
		FOR_EACH_AREA(pLoopArea)
		{
			if (AI_isPrimaryArea(*pLoopArea))
			{
				switch (pLoopArea->getAreaAIType(getTeam()))
				{
					case AREAAI_OFFENSIVE:
					case AREAAI_DEFENSIVE:
					case AREAAI_MASSING:
						bLandWar = true;
						break;
					case AREAAI_ASSAULT:
					case AREAAI_ASSAULT_MASSING:
					case AREAAI_ASSAULT_ASSIST:
						bAnyAssault = true;
						break;
					default:
						break;
				};
			}
		}
	} // </k146>
	int const iHasMetCount = kTeam.getHasMetCivCount(true);
	int const iCoastalCities = countNumCoastalCities();
	CvCity* const pCapitalCity = getCapitalCity();
	bool const bCapitalAlone = AI_isCapitalAreaAlone();

	int iValue = 0;

	bEnablesUnitWonder = false;
	CvCivilization const& kCiv = getCivilization(); // advc.003w 
	for (int i = 0; i < kCiv.getNumUnits(); i++)
	{
		UnitTypes eLoopUnit = kCiv.unitAt(i);
		CvUnitInfo& kLoopUnit = GC.getInfo(eLoopUnit);
		if (!kLoopUnit.isTechRequired(eTech))
			continue;
		UnitClassTypes eLoopClass = kCiv.unitClassAt(i);
		/*  <k146> Raw value moved here, so that it is adjusted by missing
			techs / resources */
		//iValue += 200;
		int iTotalUnitValue = 200; // </k146>

		int iNavalValue = 0;
		int iOffenceValue = 0;
		int iDefenceValue = 0;
		/*  k146: Total military value. Offence and defence are added to this
			after all roles have been considered. */
		int iMilitaryValue = 0;
		int iUtilityValue = 0;

		if (kCiv.isUnique(eLoopUnit))
		{
			iTotalUnitValue += 600;
		}

		if (kLoopUnit.getPrereqAndTech() == eTech ||
				kTeam.isHasTech((TechTypes)kLoopUnit.getPrereqAndTech()) ||
				canResearch((TechTypes)kLoopUnit.getPrereqAndTech()))
		{
			// (note, we already checked that this tech is required for the unit.)
			for (UnitAITypes eAI = (UnitAITypes)0; eAI < NUM_UNITAI_TYPES; eAI = (UnitAITypes)(eAI+1))
			{
				int iWeight = 0;
				if (eAI == kLoopUnit.getDefaultUnitAIType())
				{
					// Score the default AI type as a direct competitor to the current best.
					iWeight = std::min(250, 70 * // k146
							AI_unitValue(eLoopUnit, eAI, 0) /
							std::max(1, AI_bestAreaUnitAIValue(eAI, 0)));
				}
				else if (kLoopUnit.getUnitAIType(eAI)) // only consider types which are flagged in the xml. (??)
				{
					// For the other AI types, only score based on the improvement over the current best.
					int iTypeValue = AI_unitValue(eLoopUnit, eAI, 0);
					if (iTypeValue > 0)
					{
						int iOldValue = AI_bestAreaUnitAIValue(eAI, 0);
						iWeight = std::min(150, // k146
								100 * (iTypeValue - iOldValue) / std::max(1, iOldValue));
					}
				}
				if (iWeight <= 0)
					continue;
				FAssert(iWeight <= 250); // k146

				switch (eAI)
				{
				case UNITAI_UNKNOWN:
				case UNITAI_ANIMAL:
					break;

				case UNITAI_SETTLE:
					iUtilityValue = std::max(iUtilityValue, 12*iWeight);
					break;

				case UNITAI_WORKER:
					iUtilityValue = std::max(iUtilityValue, 8*iWeight);
					break;

				case UNITAI_ATTACK:
					iOffenceValue = std::max(iOffenceValue, (bWarPlan ? 7 : 4)*iWeight + (AI_isDoStrategy(AI_STRATEGY_DAGGER) ? 5*iWeight : 0));
					iMilitaryValue += (bWarPlan ? 3 : 1)*iWeight; // k146
					break;

				case UNITAI_ATTACK_CITY:
					iOffenceValue = std::max(iOffenceValue, (bWarPlan ? 8 : 4)*iWeight + (AI_isDoStrategy(AI_STRATEGY_DAGGER) ? 6*iWeight : 0));
					iMilitaryValue += (bWarPlan ? 1 : 0)*iWeight; // k146
					break;

				case UNITAI_COLLATERAL:
					iDefenceValue = std::max(iDefenceValue, (bWarPlan ? 6 :
							3) // k146: was 4
							*iWeight + (AI_isDoStrategy(AI_STRATEGY_ALERT1) ? 2 : 0)*iWeight);
					// k146:
					iMilitaryValue += (bWarPlan ? 1 : 0)*iWeight + (bLandWar ? 2 : 0)*iWeight;
					break;

				case UNITAI_PILLAGE:
					iOffenceValue = std::max(iOffenceValue, (bWarPlan ?
							4 : // k146: was 2
							1)*iWeight);
					// <k146>
					iMilitaryValue += (bWarPlan ? 2 : 0)*iWeight +
							(bLandWar ? 2 : 0)*iWeight; // </k146>
					break;

				case UNITAI_RESERVE:
					iDefenceValue = std::max(iDefenceValue, (bWarPlan ?
							4 : 3 // k146: was 2:1
							)*iWeight);
					// <k146>
					iMilitaryValue += (iHasMetCount > 0 ? 2 : 1)*iWeight +
							(bWarPlan ? 2 : 0); // </k146>
					break;

				case UNITAI_COUNTER:
					iDefenceValue = std::max(iDefenceValue, (bWarPlan ?
							6 // k146: was 5
							: 3)*iWeight
							// k146:
							+ (AI_isDoStrategy(AI_STRATEGY_ALERT1) ? 2 : 0)*iWeight);
					iOffenceValue = std::max(iOffenceValue, (bWarPlan ?
							4 : // k146: was 2
							1)*iWeight + (AI_isDoStrategy(AI_STRATEGY_DAGGER) ? 3*iWeight : 0));
					// <k146>
					iMilitaryValue += (AI_isDoStrategy(AI_STRATEGY_ALERT1) ||
							bWarPlan ? 1 : 0)*iWeight + (bLandWar ? 1 : 0)*iWeight;
					// </k146>
					break;

				case UNITAI_PARADROP:
					iOffenceValue = std::max(iOffenceValue, (bWarPlan ? 6 : 3)*iWeight);
					break;

				case UNITAI_CITY_DEFENSE:
					// k146:
					iDefenceValue = std::max(iDefenceValue, (bWarPlan ? 8 : 6)*iWeight + (bCapitalAlone ? 0 : 2)*iWeight);
					// <k146>
					iMilitaryValue += (iHasMetCount > 0 ? 4 : 1)*iWeight +
							(AI_isDoStrategy(AI_STRATEGY_ALERT1) ||
							bWarPlan ? 3 : 0)*iWeight; // </k146>
					break;

				case UNITAI_CITY_COUNTER:
					iDefenceValue = std::max(iDefenceValue, (bWarPlan ? 8 :
							5)*iWeight); // k146: was 4)*...
					break;

				case UNITAI_CITY_SPECIAL:
					iDefenceValue = std::max(iDefenceValue, (bWarPlan ? 8 : 4)*iWeight);
					break;

				case UNITAI_EXPLORE:
					iUtilityValue = std::max(iUtilityValue, (bCapitalAlone ? 1 : 2)*iWeight);
					break;

				case UNITAI_MISSIONARY:
					//iTotalUnitValue += (getStateReligion() != NO_RELIGION ? 6 : 3)*iWeight;
					// K-Mod. There might be one missionary for each religion. Be careful not to overvalue them!
					{
						for (ReligionTypes i = (ReligionTypes)0; i < GC.getNumReligionInfos(); i = (ReligionTypes)(i+1))
						{
							if (kLoopUnit.getReligionSpreads(i) && getHasReligionCount(i) > 0)
							{
								iUtilityValue = std::max(iUtilityValue, (getStateReligion() == i ? 6 : 3)*iWeight);
							}
						}
					}
					// K-Mod end
					break;

				case UNITAI_PROPHET:
				case UNITAI_ARTIST:
				case UNITAI_SCIENTIST:
				case UNITAI_GENERAL:
				case UNITAI_MERCHANT:
				case UNITAI_ENGINEER:
				case UNITAI_GREAT_SPY: // K-Mod
					break;

				case UNITAI_SPY:
					//iMilitaryValue += (bWarPlan ? 100 : 50);
					// K-Mod
					if (iHasMetCount > 0) {
						iUtilityValue = std::max(iUtilityValue, (bCapitalAlone ? 1 : 2)*iWeight/2
								// <k146>
								+ (AI_isDoStrategy(AI_STRATEGY_BIG_ESPIONAGE) ?
								2 : 0)* iWeight); // </k146>
					}
					// K-Mod end
					break;

				case UNITAI_ICBM:
					//iMilitaryValue += ((bWarPlan) ? 200 : 100);
					// K-Mod
					if (!GC.getGame().isNoNukes())
					{
						iOffenceValue = std::max(iOffenceValue, (bWarPlan ? 2 : 1)*iWeight + (GC.getGame().isNukesValid() ? 2*AI_nukeWeight() : 0)*iWeight/100);
						FAssert(!GC.getGame().isNukesValid() ||
								kTeam.isCapitulated() || // advc.143b
								AI_nukeWeight() > 0);
					}
					// K-Mod end
					break;

				case UNITAI_WORKER_SEA:
					if (iCoastalCities > 0)
					{
						// note, workboat improvements are already counted in the improvement section
					}
					break;

				case UNITAI_ATTACK_SEA:
					// BBAI TODO: Boost value for maps where Barb ships are pestering us
					if (iCoastalCities > 0)
					{
						//iMilitaryValue += ((bWarPlan) ? 200 : 100);
						// K-Mod
						iOffenceValue = std::max(iOffenceValue,
								(bWarPlan ? 2 : 1) *
								(100 + kLoopUnit.getCollateralDamage()/2) *
								iWeight / 100);
					}
					iNavalValue = std::max(iNavalValue, 1*iWeight);
					break;

				case UNITAI_RESERVE_SEA:
					if (iCoastalCities > 0)
					{
						//iMilitaryValue += ((bWarPlan) ? 100 : 50);
						iOffenceValue = std::max(iOffenceValue, (bWarPlan ? 10 : 5) * (10 + kLoopUnit.getCollateralDamage()/20) * iWeight / 100);// K-Mod
						// K-Mod note: this naval value stuff seems a bit flakey...
					}
					iNavalValue = std::max(iNavalValue, 1*iWeight);
					break;

				case UNITAI_ESCORT_SEA:
					if (iCoastalCities > 0)
					{
						iOffenceValue = std::max(iOffenceValue, (bWarPlan ? 2 : 1)*iWeight/2);
					}
					iNavalValue = std::max(iNavalValue, 1*iWeight);
					break;

				case UNITAI_EXPLORE_SEA:
					if (iCoastalCities > 0)
					{
						//iTotalUnitValue += (bCapitalAlone ? 18 : 6)*iWeight;
						iUtilityValue = std::max(iUtilityValue,
								(4 + // k146: was 6+
								(bCapitalAlone ? 4 : 0) + (iHasMetCount > 0 ? 0 :
								4))*iWeight); // k146: was 6))...
					}
					break;

				case UNITAI_ASSAULT_SEA:
					if (iCoastalCities > 0)
					{
						iOffenceValue = std::max(iOffenceValue, (bWarPlan || bCapitalAlone ? 4 : 2)*iWeight);
					}
					iNavalValue = std::max(iNavalValue, 2*iWeight);
					break;

				case UNITAI_SETTLER_SEA:
					if (iCoastalCities > 0)
					{
						iUtilityValue = std::max(iUtilityValue,
								// k146:
								(bWarPlan ? 0 : 1)*iWeight + (bCapitalAlone ? 1 : 0)*iWeight);
					}
					iNavalValue = std::max(iNavalValue, 2*iWeight);
					break;

				case UNITAI_MISSIONARY_SEA:
					if (iCoastalCities > 0)
					{
						iUtilityValue = std::max(iUtilityValue, 1*iWeight);
					}
					break;

				case UNITAI_SPY_SEA:
					if (iCoastalCities > 0)
					{
						iUtilityValue = std::max(iUtilityValue, 1*iWeight);
					}
					break;

				case UNITAI_CARRIER_SEA:
					if (iCoastalCities > 0)
					{
						iOffenceValue = std::max(iOffenceValue, (bWarPlan ? 2 : 1)*iWeight/2);
					}
					break;

				case UNITAI_MISSILE_CARRIER_SEA:
					if (iCoastalCities > 0)
					{
						iOffenceValue = std::max(iOffenceValue, (bWarPlan ? 2 : 1)*iWeight/2);
					}
					break;

				case UNITAI_PIRATE_SEA:
					if (iCoastalCities > 0)
					{
						iOffenceValue = std::max(iOffenceValue, 1*iWeight);
					}
					iNavalValue = std::max(iNavalValue, 1*iWeight);
					break;

				case UNITAI_ATTACK_AIR:
					//iMilitaryValue += ((bWarPlan) ? 1200 : 800);
					// K-Mod
					iOffenceValue = std::max(iOffenceValue, (bWarPlan ? 10 : 5) * (100 + kLoopUnit.getCollateralDamage()*std::min(5, kLoopUnit.getCollateralDamageMaxUnits())/5) * iWeight / 100);
					break;

				case UNITAI_DEFENSE_AIR:
					//iMilitaryValue += ((bWarPlan) ? 1200 : 800);
					iDefenceValue = std::max(iDefenceValue, (bWarPlan ? 10 : 6)*iWeight + (bCapitalAlone ? 3 : 0)*iWeight);
					break;

				case UNITAI_CARRIER_AIR:
					if (iCoastalCities > 0)
					{
						iOffenceValue = std::max(iOffenceValue, (bWarPlan ? 2 : 1)*iWeight);
					}
					iNavalValue = std::max(iNavalValue, 4*iWeight);
					break;

				case UNITAI_MISSILE_AIR:
					iOffenceValue = std::max(iOffenceValue, (bWarPlan ? 2 : 1)*iWeight);
					break;

				default:
					FAssertMsg(false, "Missing UNITAI type in AI_techUnitValue");
					break;
				}
			}
			iTotalUnitValue += iUtilityValue;
			// <advc.131> Haven't met anyone yet, and not yet sure if alone.
			if(iHasMetCount <= 0 && !bCapitalAlone && !GC.getGame().isOption(GAMEOPTION_RAGING_BARBARIANS))
			{
				iDefenceValue /= 2;
				iOffenceValue /= 3;
			} // </advc.131>
			// <k146>
			iMilitaryValue += iOffenceValue + iDefenceValue;
			if (kLoopUnit.getBombardRate() > 0 &&
					!AI_isDoStrategy(AI_STRATEGY_ECONOMY_FOCUS))
			{	// block moved from UNITAI_ATTACK_CITY:
				iMilitaryValue += std::min(iOffenceValue, 100); // was straight 200
				// </k146>
				if (AI_calculateTotalBombard(DOMAIN_LAND) == 0)
				{
					iMilitaryValue += 600; // k146: was 800
					if (AI_isDoStrategy(AI_STRATEGY_DAGGER))
					{
						iMilitaryValue += 400; // was 1000
					}
				}
			}

			if (kLoopUnit.getUnitAIType(UNITAI_ASSAULT_SEA) && iCoastalCities > 0)
			{
				int iAssaultValue = 0;
				UnitTypes eExistingUnit = NO_UNIT;
				if (AI_bestAreaUnitAIValue(UNITAI_ASSAULT_SEA, NULL, &eExistingUnit) == 0)
				{
					iAssaultValue += 100; // k146: was 250
				}
				else if (eExistingUnit != NO_UNIT)
				{
					iAssaultValue += 500 * // k146: was 1000*
							std::max(0, AI_unitImpassableCount(eLoopUnit) - AI_unitImpassableCount(eExistingUnit));

					int iNewCapacity = kLoopUnit.getMoves() * kLoopUnit.getCargoSpace();
					int iOldCapacity = GC.getInfo(eExistingUnit).getMoves() * GC.getInfo(eExistingUnit).getCargoSpace();

					iAssaultValue += (200 * // k146: was 800*
							(iNewCapacity - iOldCapacity)) / std::max(1, iOldCapacity);
				}

				if (iAssaultValue > 0)
				{
					// k146: (bAnyAssault calculation removed; now done earlier)
					if (bAnyAssault)
					{
						iTotalUnitValue += iAssaultValue * 4;
					}
					else
					{
						iTotalUnitValue += iAssaultValue;
					}
				}
			}

			if (iNavalValue > 0)
			{
				if (pCapitalCity != NULL)
				{
					// BBAI TODO: A little odd ... naval value is 0 if have no colonies.
					iNavalValue *= 2 * (getNumCities() -
							pCapitalCity->getArea().getCitiesPerPlayer(getID()));
					iNavalValue /= getNumCities();

					iTotalUnitValue += iNavalValue;
				}
			}
			// k146: Disabled
			/*if (AI_totalUnitAIs((UnitAITypes)kLoopUnit.getDefaultUnitAIType()) == 0)
			{	// do not give bonus to seagoing units if they are worthless
				if (iTotalUnitValue > 0)
					iTotalUnitValue *= 2;
				if (kLoopUnit.getDefaultUnitAIType() == UNITAI_EXPLORE) {
					if (pCapitalCity != NULL)
						iTotalUnitValue += (AI_neededExplorers(pCapitalCity->getArea()) * 400);
				}
				if (kLoopUnit.getDefaultUnitAIType() == UNITAI_EXPLORE_SEA) {
					iTotalUnitValue += 400;
					iTotalUnitValue += ((GC.getGame().countCivTeamsAlive() - iHasMetCount) * 200);
				}
			}*/

			if (kLoopUnit.getUnitAIType(UNITAI_SETTLER_SEA) &&
				pCapitalCity != NULL)
			{
				UnitTypes eExistingUnit = NO_UNIT;
				int iBestAreaValue = 0;
				AI_getNumAreaCitySites(pCapitalCity->getArea(), iBestAreaValue);

				//Early Expansion by sea
				if (AI_bestAreaUnitAIValue(UNITAI_SETTLER_SEA, NULL, &eExistingUnit) == 0)
				{
					CvArea* pWaterArea = pCapitalCity->waterArea();
					if (pWaterArea != NULL)
					{
						int iBestOtherValue = 0;
						AI_getNumAdjacentAreaCitySites(iBestOtherValue, *pWaterArea,
								pCapitalCity->area());
						if (iBestAreaValue == 0
							// k146: Give us a chance to see our land first.
							&& GC.getGame().getElapsedGameTurns() > 20)
						{
							iTotalUnitValue += 2000;
						}
						else if (iBestAreaValue < iBestOtherValue)
							iTotalUnitValue += 1000;
						else if (iBestOtherValue > 0)
							iTotalUnitValue += 500;
					}
				}
				// Landlocked expansion over ocean
				else if (eExistingUnit != NO_UNIT)
				{
					if (AI_unitImpassableCount(eLoopUnit) <
						AI_unitImpassableCount(eExistingUnit) &&
						iBestAreaValue < AI_getMinFoundValue())
					{
						iTotalUnitValue += AI_atVictoryStage(AI_VICTORY_DOMINATION2) ? 2000 : 500;
					}
				}
			}

			if (iMilitaryValue > 0)
			{
				if (iHasMetCount == 0 || AI_isDoStrategy(AI_STRATEGY_ECONOMY_FOCUS))
					iMilitaryValue /= 2;

				// K-Mod
				if (AI_isDoStrategy(AI_STRATEGY_GET_BETTER_UNITS) && kLoopUnit.getDomainType() == DOMAIN_LAND)
				{
					iMilitaryValue += 3 * GC.AI_getGame().AI_combatValue(eLoopUnit);
					iMilitaryValue *= 3;
					iMilitaryValue /= 2;
				}

				// This multiplier stuff is basically my version of the BBAI code I disabled further up.
				int iMultiplier = 100;
				if (AI_atVictoryStage(AI_VICTORY_CONQUEST1 | AI_VICTORY_DOMINATION2) ||
					AI_isDoStrategy(AI_STRATEGY_ALERT1))
				{
					iMultiplier += 25;
					if (AI_atVictoryStage(AI_VICTORY_CONQUEST2 | AI_VICTORY_DOMINATION3))
					{
						iMultiplier += 25;
						if (AI_atVictoryStage(AI_VICTORY_CONQUEST3 | AI_VICTORY_DOMINATION4))
							iMultiplier += 25;
					}
				}
				if (AI_isDoStrategy(AI_STRATEGY_ALERT1 | AI_STRATEGY_TURTLE) &&
					(kLoopUnit.getUnitAIType(UNITAI_CITY_DEFENSE) ||
					kLoopUnit.getUnitAIType(UNITAI_CITY_SPECIAL) ||
					kLoopUnit.getUnitAIType(UNITAI_CITY_COUNTER) ||
					kLoopUnit.getUnitAIType(UNITAI_COLLATERAL)))
				{
					iMultiplier += AI_isDoStrategy(AI_STRATEGY_ALERT2) ? 70 : 30;
					if (iPathLength <= 1 && AI_isDoStrategy(AI_STRATEGY_TURTLE))
						iMultiplier += 75;
				}
				iMilitaryValue *= iMultiplier;
				iMilitaryValue /= 100;

				if (bCapitalAlone && iMultiplier <= 100) // I've moved this block from above, and added the multiplier condition.
				{
					iMilitaryValue *= 2;
					iMilitaryValue /= 3;
				}
				// K-Mod end

				iTotalUnitValue += iMilitaryValue;
			}
		}
		// <k146>
		if(kLoopUnit.isWorldUnit() && !GC.getGame().isUnitClassMaxedOut(eLoopClass))
			bEnablesUnitWonder = true; // </k146>
		// K-Mod
		// Decrease the value if we are missing other prereqs.
		{
			int iMissingTechs = 0;
			if (!kTeam.isHasTech((TechTypes)kLoopUnit.getPrereqAndTech()))
				iMissingTechs++;

			for (int iI = 0; iI < GC.getNUM_UNIT_AND_TECH_PREREQS(eLoopUnit); iI++)
			{
				if (!kTeam.isHasTech(kLoopUnit.getPrereqAndTechs(iI)))
				{
					iMissingTechs++;
					if (!canResearch(kLoopUnit.getPrereqAndTechs(iI)))
						iMissingTechs++;
				}
			}
			FAssert(iMissingTechs > 0);
			iTotalUnitValue /= std::max(1, iMissingTechs);
		}

		// Decrease the value if we don't have the resources
		bool bMaybeMissing = false; // if we don't know if we have the bonus or not
		bool bDefinitelyMissing = false; // if we can see the bonuses, and we know we don't have any.
		// k146: if the 'maybe missing' resource is revealed by eTech
		bool bWillReveal = false;

		for (int iI = 0; iI < GC.getNUM_UNIT_PREREQ_OR_BONUSES(eLoopUnit); ++iI)
		{
			BonusTypes ePrereqBonus = kLoopUnit.getPrereqOrBonuses(iI);
			if (ePrereqBonus != NO_BONUS)
			{
				if (hasBonus(ePrereqBonus))
				{
					bDefinitelyMissing = false;
					bMaybeMissing = false;
					break;
				}
				else
				{
					if ((kTeam.isHasTech((TechTypes)(GC.getInfo(ePrereqBonus).getTechReveal())) ||
						kTeam.isForceRevealedBonus(ePrereqBonus)) &&
						AI_countOwnedBonuses(ePrereqBonus) == 0)
					{
						bDefinitelyMissing = true;
					}
					else
					{
						bMaybeMissing = true;
						// <k146>
						bDefinitelyMissing = false;
						if (GC.getInfo(ePrereqBonus).getTechReveal() == eTech)
						{
							bWillReveal = true;
						} // </k146>
					}
				}
			}
		}
		BonusTypes ePrereqBonus = kLoopUnit.getPrereqAndBonus();
		if (ePrereqBonus != NO_BONUS && !hasBonus(ePrereqBonus))
		{
			if ((kTeam.isHasTech((TechTypes)GC.getInfo(ePrereqBonus).getTechReveal()) ||
				kTeam.isForceRevealedBonus(ePrereqBonus)) &&
				AI_countOwnedBonuses(ePrereqBonus) == 0)
			{
				bDefinitelyMissing = true;
			}
			else
			{
				bMaybeMissing = true;
				// <k146>
				if (GC.getInfo(ePrereqBonus).getTechReveal() == eTech)
				{
					// assuming that we aren't also missing an "or" prereq.
					bWillReveal = true;
				} // </k146>
			}
		}
		if (bDefinitelyMissing)
		{
			iTotalUnitValue /= 4; // advc.131: was /=3
		}
		else if (bMaybeMissing)
		{	// <k146>
			if (bWillReveal)
			{
				// We're quite optimistic... mostly because otherwise we'd risk undervaluing axemen in the early game! (Kludge, sorry.)
				/*  <advc.036> Need to reduce the value of military tech compared
					with food tech in the early game. Was *=4 and /=5. */
				iTotalUnitValue *= ::range(2 * getNumCities(), 1, 4);
				iTotalUnitValue /= 6; // </advc.036>
			}
			else
			{
				//iTotalUnitValue = 2*iTotalUnitValue/3;
				iTotalUnitValue = iTotalUnitValue/2; // better get the reveal tech first
			} // </k146>
		}
		// K-Mod end

		iValue += iTotalUnitValue;
	}

	// K-Mod. Rescale to match AI_techValue
	iValue *= (4 * (getNumCities()
			+ 2)); // k146
	// k146: was /=100. This entire calculation is quite arbitrary. :(
	iValue /= 120;
	//
	return iValue;
}

/*  <k146> A (very rough) estimate of the value of projects enabled by eTech.
	Note: not a lot of thought has gone into this. I've basically just copied
	the original code [from AI_techValue] and tweaked it a little bit.
	The scale is roughly 4 = 1 commerce per turn. */
int CvPlayerAI::AI_techProjectValue(TechTypes eTech, int iPathLength,
	bool& bEnablesProjectWonder) const
{
	int iValue = 0;
	bEnablesProjectWonder = false;
	FOR_EACH_ENUM2(Project, eProject)
	{
		CvProjectInfo const& kProject = GC.getInfo(eProject);
		// <advc> Compacted
		if(kProject.getTechPrereq() != eTech)
			continue;
		if(getTotalPopulation() > 5 && kProject.isWorldProject() &&
			!GC.getGame().isProjectMaxedOut(eProject))
		{
			bEnablesProjectWonder = true;
		}
		iValue += 280;
		if((VictoryTypes)kProject.getVictoryPrereq() == NO_VICTORY)
			continue; // </advc>
		// Victory condition values need to be scaled by number of cities to compete with other tech.
		// Total value with be roughly iBaseValue * cities.
		int iBaseValue = 0;
		if (!kProject.isSpaceship())
		{
			// Apollo
			iBaseValue += (AI_atVictoryStage(AI_VICTORY_SPACE2) ? 50 : 2);
		}
		else
		{
			// Space ship parts (changed by K-Mod)
			// Note: ideally this would take into account the production cost of each item,
			// and the total number / production of a completed space-ship, and a
			// bunch of other things. But I think this is good enough for now.
			if (AI_atVictoryStage(AI_VICTORY_SPACE2))
			{
				iBaseValue += 40;
				if (AI_atVictoryStage(AI_VICTORY_SPACE3))
				{
					iBaseValue += 40;
					if (kProject.getMaxTeamInstances() > 0)
						iBaseValue += (kProject.getMaxTeamInstances()-1) * 10;
				}
			}
		}
		if (iBaseValue > 0)
		{
			iValue += iBaseValue * (3 + std::max(getNumCities(),
					GC.getInfo(GC.getMap().getWorldSize()).getTargetNumCities()));
		}
	}
	return iValue;
} // </k146>

int CvPlayerAI::AI_cultureVictoryTechValue(TechTypes eTech) const
{
	if (eTech == NO_TECH)
		return 0;

	int iValue = 0;

	if (GC.getInfo(eTech).isDefensivePactTrading())
		iValue += 50;

	if (GC.getInfo(eTech).isCommerceFlexible(COMMERCE_CULTURE))
		iValue += 100;

	//units
	bool bAnyWarplan = AI_isFocusWar(); // advc.105
			//(GET_TEAM(getTeam()).getAnyWarPlanCount(true) > 0);
	int iBestUnitValue = 0;
	CvCivilization const& kCiv = getCivilization(); // advc.003w 
	for (int i = 0; i < kCiv.getNumUnits(); i++)
	{
		UnitTypes eLoopUnit = kCiv.unitAt(i);
		if (GC.getInfo(eLoopUnit).isTechRequired(eTech))
		{
			int iTempValue = (GC.getInfo(eLoopUnit).getCombat() * 100) /
					std::max(1, (GC.getGame().getBestLandUnitCombat()));
			iTempValue *= bAnyWarplan ? 2 : 1;

			iValue += iTempValue / 3;
			iBestUnitValue = std::max(iBestUnitValue, iTempValue);
		}
	}
	iValue += std::max(0, iBestUnitValue - 15);

	//cultural things
	for (int i = 0; i < kCiv.getNumBuildings(); i++)
	{
		BuildingTypes eLoopBuilding = kCiv.buildingAt(i);
		CvBuildingInfo& kLoopBuilding = GC.getInfo(eLoopBuilding);
		if (kLoopBuilding.isTechRequired(eTech))
		{
			if (kCiv.isUnique(eLoopBuilding))
				iValue += 100;
			iValue += (150 * (kLoopBuilding.getCommerceChange(COMMERCE_CULTURE)
					//) * 20);
					// UNOFFICIAL_PATCH, Bugfix, 05/25/10, Fuyu & jdog5000:
					+ kLoopBuilding.getObsoleteSafeCommerceChange(COMMERCE_CULTURE))) / 20;
			iValue += kLoopBuilding.getCommerceModifier(COMMERCE_CULTURE) * 2;
		}
	}

	//important civics
	FOR_EACH_ENUM(Civic)
	{
		if (GC.getInfo(eLoopCivic).getTechPrereq() == eTech)
			iValue += GC.getInfo(eLoopCivic).getCommerceModifier(COMMERCE_CULTURE) * 2;
	}

	return iValue;
}

void CvPlayerAI::AI_chooseFreeTech()
{
	clearResearchQueue();

	TechTypes eBestTech = GC.getPythonCaller()->AI_chooseTech(getID(), true); 

	if (eBestTech == NO_TECH)
		eBestTech = AI_bestTech(1, true);

	if (eBestTech != NO_TECH)
		GET_TEAM(getTeam()).setHasTech(eBestTech, true, getID(), true, true);
}


void CvPlayerAI::AI_chooseResearch()
{
	clearResearchQueue();

	if(getCurrentResearch() == NO_TECH &&
		// advc.156:
		GC.getDefineINT(CvGlobals::RESEARCH_MODIFIER_EXTRA_TEAM_MEMBER) > -5)
	{
		for(int iPass = 0; iPass < 2; iPass++)
		{
			for (MemberIter it(getTeam()); it.hasNext(); ++it)
			{
				CvPlayer const& kOtherMember = *it;
				if(kOtherMember.getID() == getID())
					continue;
				// advc.156: Priority for human members
				if(((iPass == 0) == kOtherMember.isHuman()) &&
						kOtherMember.getCurrentResearch() != NO_TECH &&
						canResearch(kOtherMember.getCurrentResearch()))
					pushResearch(kOtherMember.getCurrentResearch());
			}
		}
	}

	if (getCurrentResearch() != NO_TECH)
		return;

	TechTypes eBestTech = GC.getPythonCaller()->AI_chooseTech(getID(), false);

	if (eBestTech == NO_TECH)
	{	// <k146>
		int iResearchDepth = (isHuman() || isBarbarian() ||
				AI_atVictoryStage(AI_VICTORY_CULTURE3) ||
				AI_isDoStrategy(AI_STRATEGY_ESPIONAGE_ECONOMY))
			? 1 : 3;
		eBestTech = AI_bestTech(iResearchDepth); // </k146>
	}
	if (eBestTech != NO_TECH)
	{	/*  advc.004x: Don't kill popup when AI chooses tech for human
			(instead prod the human each turn to pick a tech him/herself) */
		pushResearch(eBestTech, false, false);
	}
}

DiploCommentTypes CvPlayerAI::AI_getGreeting(PlayerTypes ePlayer) const
{
	DiploCommentTypes eDefaultGreeting = (DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_GREETINGS");
	if (TEAMID(ePlayer) == getTeam())
		return eDefaultGreeting; // advc
	// <advc.079>
	// Checks moved up
	bool bBrag = (GET_PLAYER(ePlayer).getPower() < getPower() &&
			AI_getAttitude(ePlayer) < ATTITUDE_PLEASED);
	if(bBrag)
	{
		int iDemandRand = GC.getInfo(getPersonalityType()).
				getContactRand(CONTACT_DEMAND_TRIBUTE);
		if(iDemandRand < 400)
		{
			if(!GET_PLAYER(ePlayer).canSeeTech(getID()) ||
				!GET_PLAYER(ePlayer).canTradeNetworkWith(getID()))
			{
				UnitTypes eBragUnit = AI_getBestAttackUnit();
				if(eBragUnit != NO_UNIT)
				{
					/*double prBrag = 0;
					if(iDemandRand > 0) // e.g. 25 -> 73.1%; 1000 -> 16.9%
						prBrag = 0.01 * (80 - std::pow((double)iDemandRand, 0.6));
					std::vector<long> inputs;
					inputs.push_back(eBragUnit);
					inputs.push_back(ePlayer);
					if(::hash(inputs, getID()) < prBrag) */
					// ^Don't randomize this after all; instead check <400 above.
					{	// If we haven't bragged before, do it promptly.
						if(eBragUnit != m_aeLastBrag[ePlayer])
						{
							const_cast<CvPlayerAI*>(this)->m_aeLastBrag[ePlayer] = eBragUnit;
							return (DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_UNIT_BRAG");
						} // else: Use BtS code below
					} // Never brag about this unit to ePlayer
					//else bBrag = false;
				}
				else bBrag = false;
			}
		}
		else bBrag = false;
	} // </advc.079>
	TeamTypes eWorstEnemy = GET_TEAM(getTeam()).AI_getWorstEnemy();
	if (eWorstEnemy != NO_TEAM && eWorstEnemy != TEAMID(ePlayer) &&
		GET_TEAM(ePlayer).isHasMet(eWorstEnemy) &&
		0 == GC.getASyncRand().get(4 /* <advc.079> */ +
		(m_aeLastWarn[ePlayer] != eWorstEnemy ? -2 : 2)))
	{
		const_cast<CvPlayerAI*>(this)->m_aeLastWarn[ePlayer] = eWorstEnemy;
		// </advc.079>
		if (GET_PLAYER(ePlayer).AI_hasTradedWithTeam(eWorstEnemy) &&
				!atWar(TEAMID(ePlayer), eWorstEnemy))
			return (DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_WORST_ENEMY_TRADING");
		else return (DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_WORST_ENEMY");
	}
	else if (getNumNukeUnits() > 0 && GC.getASyncRand().get(4) == 0)
		return (DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_NUKES");
	else if (/* <advc.079> */ bBrag && /* </advc.079> */ GC.getASyncRand().get(4) == 0)
		return (DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_UNIT_BRAG");
	return eDefaultGreeting;
}

// return true if we are willing to talk to ePlayer
bool CvPlayerAI::AI_isWillingToTalk(PlayerTypes ePlayer, /* advc.104l: */ bool bAsync) const
{
	FAssert(getPersonalityType() != NO_LEADER);
	FAssert(ePlayer != getID());
	// <advc.104i>
	CvGame const& g = GC.getGame();
	if(ePlayer == g.getActivePlayer())
	{
		/*  The EXE keeps calling this function when the diplo screen is already up,
			and some of the new code (isPeaceDealPossible) is expensive. */
		if(gDLL->getDiplomacyPlayer() == getID())
			return true;
		if(GET_PLAYER(ePlayer).isHuman() // i.e. not Auto Play
				&& g.isOption(GAMEOPTION_ALWAYS_WAR))
			return false;
	} // </advc.104i>

	// <advc.003n> In particular, don't call AI_surrenderTrade on Barbarians.
	if(ePlayer == BARBARIAN_PLAYER)
		return false; // </advc.003n>

	// K-Mod
	const CvTeamAI& kOurTeam = GET_TEAM(getTeam());
	const CvTeam& kTheirTeam = GET_TEAM(ePlayer);
	if(kTheirTeam.getID() == getTeam() || kTheirTeam.isVassal(getTeam()) ||
			kOurTeam.isVassal(kTheirTeam.getID()))
		return true;
	/*if(GET_TEAM(getTeam()).isHuman()) // BtS code
		return false;*/
	if(isHuman())
		return true; // humans will speak to anyone, apparently.
	// K-Mod end

	// advc.104i: Moved the !atWar and isAVassal checks up
	if(!::atWar(getTeam(), kTheirTeam.getID()))
		return (AI_getMemoryCount(ePlayer, MEMORY_STOPPED_TRADING_RECENT) <= 0);
	if (isAVassal()
			|| kTheirTeam.isAVassal()) // advc.104i: Get this out of the way too
		return false;
	// K-Mod
	if(kOurTeam.isHuman()) // ie. we are an AI player, but our team is human
		return false; // let the human speak for us.
	// K-Mod end
	// <advc.104i>
	int iTurnsAtWar = kOurTeam.AI_getAtWarCounter(kTheirTeam.getID());
	if(getUWAI.isEnabled())
	{
		// advc.104l: Synchronized code mustn't read from the war evaluator cache
		int iWillTalk = uwai().willTalk(ePlayer, iTurnsAtWar, bAsync);
		if(iWillTalk < 0)
			return false;
		if(iWillTalk > 0)
			return true;
		// iWillTalk==0: RTT as in BtS
	}
	else // </advc.104i>
	{
		// K-Mod
		if(kOurTeam.AI_refusePeace(kTheirTeam.getID()))
		{
			if(!GET_PLAYER(ePlayer).canTradeItem(getID(), TradeData(TRADE_SURRENDER, true)))
				return false; // if they can't (or won't) capitulate, don't talk to them.
		} // K-Mod end
	}
	// advc.104i: Moved into a new function
	int iRefuseDuration = AI_refuseToTalkTurns(ePlayer);
	return (iTurnsAtWar >= iRefuseDuration);
}

// advc.104i: Cut from AI_isWillingToTalk
int CvPlayerAI::AI_refuseToTalkTurns(PlayerTypes ePlayer) const
{
	FAssert(GET_TEAM(getTeam()).isAtWar(TEAMID(ePlayer)));
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
	CvTeamAI const& kTheirTeam = GET_TEAM(ePlayer);
	int r = GC.getInfo(getPersonalityType()).getRefuseToTalkWarThreshold() *
			(kOurTeam.AI_isChosenWar(kTheirTeam.getID()) ? 2 : 1);
	int iOurSuccess = 1 + kOurTeam.AI_getWarSuccess(kTheirTeam.getID());
	int iTheirSuccess = 1 + kTheirTeam.AI_getWarSuccess(getTeam());
	if (iTheirSuccess > iOurSuccess * 2
		/* <advc.001> */ && iTheirSuccess >= GC.getWAR_SUCCESS_CITY_CAPTURING())
	{
			/*  Otherwise, killing a single stray unit can be enough to lower
				the refuse duration to three turns (ratio 5:1). </advc.001> */
		r *= 20 + (80 * iOurSuccess * 2) / iTheirSuccess;
		r /= 100;
	}
	return r;
}


// XXX what if already at war???
// Returns true if the AI wants to sneak attack...
/*  advc (comment) ... which is to say: set WARPLAN_LIMITED w/o prior preparation,
	but don't declare war until units reach the border (perhaps they never do if no
	proper attack stack ready); whereas demandRebukedWar immediately declares war
	(which also sets WARPLAN_LIMITED). */
bool CvPlayerAI::AI_demandRebukedSneak(PlayerTypes ePlayer) const
{
	FAssertMsg(!isHuman(), "isHuman did not return false as expected");
	FAssertMsg(ePlayer != getID(), "shouldn't call this function on ourselves");

	FAssert(!isAVassal());
	FAssert(!(GET_TEAM(getTeam()).isHuman()));

	if (GC.getGame().getSorenRandNum(100, "AI Demand Rebuked") < GC.getInfo(getPersonalityType()).getDemandRebukedSneakProb())
	{	// <advc.104m>
		if(getUWAI.isEnabled())
		{
			GET_TEAM(getTeam()).uwai().respondToRebuke(TEAMID(ePlayer), true);
			return false; // respondToRebuke handles any changes to war plans
		} // </advc.104m>
		//if (GET_TEAM(getTeam()).getPower(true) > GET_TEAM(GET_PLAYER(ePlayer).getTeam()).getDefensivePower(getTeam()))
		// K-Mod. Don't start a war if we're already busy; and use AI_startWarVal to evaluate, rather than just power.
		// The 50 value is arbitrary. zero would probably be fine. 50 war rating is also arbitrary, but zero would be too low!
		const CvTeamAI& kTeam = GET_TEAM(getTeam());
		if (kTeam.AI_getWarPlan(GET_PLAYER(ePlayer).getTeam()) == NO_WARPLAN  &&
				(!kTeam.AI_isAnyWarPlan() || kTeam.AI_getWarSuccessRating() > 50) &&
				// advc.001n (comment): This should be synchronized code, so no need to set bConstCache.
				kTeam.AI_startWarVal(GET_PLAYER(ePlayer).getTeam(), WARPLAN_LIMITED) > 50)
		// K-Mod end
			return true;
	}

	return false;
}

// XXX what if already at war???
// Returns true if the AI wants to declare war...
bool CvPlayerAI::AI_demandRebukedWar(PlayerTypes ePlayer) const
{
	FAssertMsg(!isHuman(), "isHuman did not return false as expected");
	FAssertMsg(ePlayer != getID(), "shouldn't call this function on ourselves");

	FAssert(!isAVassal());
	FAssert(!(GET_TEAM(getTeam()).isHuman()));

	// needs to be async because it only happens on the computer of the player who is in diplomacy...
	if (GC.getASyncRand().get(100, "AI Demand Rebuked ASYNC") <
		GC.getInfo(getPersonalityType()).getDemandRebukedWarProb())
	{
		// <advc.104m>
		if(getUWAI.isEnabled())
		{
			GET_TEAM(getTeam()).uwai().respondToRebuke(TEAMID(ePlayer), false);
			return false; // respondToRebuke handles any changes to war plans
		} // </advc.104m>
		if (GET_TEAM(getTeam()).getPower(true) > GET_TEAM(ePlayer).getDefensivePower(getTeam()))
		{
			if (GET_TEAM(getTeam()).AI_isLandTarget(TEAMID(ePlayer), true))
				return true;
		}
	}

	return false;
}


// XXX maybe make this a little looser (by time...)
bool CvPlayerAI::AI_hasTradedWithTeam(TeamTypes eTeam) const
{
	if(getCurrentEra() <= GC.getGame().getStartEra()) // advc.079
	{
		for (MemberIter it(eTeam); it.hasNext(); ++it)
		{
			CvPlayer const& kTeamMember = *it;
			if (AI_getPeacetimeGrantValue(kTeamMember.getID()) +
				AI_getPeacetimeTradeValue(kTeamMember.getID()) > 0)
			{
				return true;
			}
		}
	} // advc.079: Addressing the XXX comment above
	else return canStopTradingWithTeam(eTeam);
	return false;
}

// static
AttitudeTypes CvPlayerAI::AI_getAttitudeFromValue(int iAttitudeVal)
{
	// <advc.148> Get the thresholds from XML
	if (iAttitudeVal >= GC.getDefineINT(CvGlobals::RELATIONS_THRESH_FRIENDLY))
		return ATTITUDE_FRIENDLY;
	if (iAttitudeVal >= GC.getDefineINT(CvGlobals::RELATIONS_THRESH_PLEASED))
		return ATTITUDE_PLEASED;
	if (iAttitudeVal <= GC.getDefineINT(CvGlobals::RELATIONS_THRESH_FURIOUS))
		return ATTITUDE_FURIOUS;
	if (iAttitudeVal <= GC.getDefineINT(CvGlobals::RELATIONS_THRESH_ANNOYED))
		return ATTITUDE_ANNOYED; // </advc.148>
	return ATTITUDE_CAUTIOUS;
}

// K-Mod  // advc: Renamed from "updateAttitudeCache"; "update" pretty much implies "cache".
void CvPlayerAI::AI_updateAttitude()
{
	// advc.003n: Was done for all players
	for (PlayerIter<MAJOR_CIV> it; it.hasNext(); ++it)
	{
		AI_updateAttitude(it->getID(),
				false); // advc.130e: Sufficient to upd. worst enemy once in the end
	}
	GET_TEAM(getTeam()).AI_updateWorstEnemy(); // advc.130e
}

// note: most of this function has been moved from CvPlayerAI::AI_getAttitudeVal
void CvPlayerAI::AI_updateAttitude(PlayerTypes ePlayer, /* advc.130e: */ bool bUpdateWorstEnemy)
{
	PROFILE_FUNC();
	FAssertBounds(0, MAX_CIV_PLAYERS, ePlayer);
	FAssert(GET_TEAM(getTeam()).isMajorCiv() && GET_TEAM(ePlayer).isMajorCiv()); // advc.003n

	CvPlayerAI const& kPlayer = GET_PLAYER(ePlayer);

	if (!GC.getGame().isFinalInitialized() || ePlayer == getID() ||
		!isAlive() || !kPlayer.isAlive() || !GET_TEAM(getTeam()).isHasMet(kPlayer.getTeam()))
	{
		m_abTheyFarAhead[ePlayer] = m_abTheyBarelyAhead[ePlayer] = false; // advc.130c
		m_aiAttitude[ePlayer] = 0;
		return;
	} // <advc.130c> Are they (ePlayer) 150% ahead in score?
	CvGame const& g = GC.getGame();
	int iTheirScore = g.getPlayerScore(ePlayer);
	int iOurScore = g.getPlayerScore(getID());
	m_abTheyFarAhead[ePlayer] = (iTheirScore * 10 > iOurScore * 15);
	m_abTheyBarelyAhead[ePlayer] = (iTheirScore > iOurScore &&
			iTheirScore - iOurScore < 25); // </advc.130c>
	// <advc.sha> Now computed in subroutines
	int iAttitude = AI_getFirstImpressionAttitude(ePlayer);
	iAttitude += AI_getTeamSizeAttitude(ePlayer);
	//iAttitude += AI_getLostWarAttitude(ePlayer);
	iAttitude += AI_getRankDifferenceAttitude(ePlayer);
	// </advc.sha>
	/*  advc.130c: Disable rank bonus for civs in the bottom half of the leaderboard.
		Caveat in case this code fragment is re-activated: ranks start at 0. */
	/*if ((GC.getGame().getPlayerRank(getID()) >= (GC.getGame().getCivPlayersEverAlive() / 2)) &&
		  (GC.getGame().getPlayerRank(ePlayer) >= (GC.getGame().getCivPlayersEverAlive() / 2)))
		iAttitude++;*/
	iAttitude += AI_getCloseBordersAttitude(ePlayer);
	iAttitude += AI_getPeaceAttitude(ePlayer);
	iAttitude += AI_getSameReligionAttitude(ePlayer);
	iAttitude += AI_getDifferentReligionAttitude(ePlayer);
	iAttitude += AI_getBonusTradeAttitude(ePlayer);
	iAttitude += AI_getOpenBordersAttitude(ePlayer);
	iAttitude += AI_getDefensivePactAttitude(ePlayer);
	iAttitude += AI_getRivalDefensivePactAttitude(ePlayer);
	iAttitude += AI_getRivalVassalAttitude(ePlayer);
	iAttitude += AI_getExpansionistAttitude(ePlayer); // advc.130w
	iAttitude += AI_getShareWarAttitude(ePlayer);
	iAttitude += AI_getFavoriteCivicAttitude(ePlayer);
	iAttitude += AI_getTradeAttitude(ePlayer);
	iAttitude += AI_getRivalTradeAttitude(ePlayer);

	FOR_EACH_ENUM(Memory)
		iAttitude += AI_getMemoryAttitude(ePlayer, eLoopMemory);

	//iAttitude += AI_getColonyAttitude(ePlayer); // advc.130r
	iAttitude += AI_getAttitudeExtra(ePlayer);
	/*  advc.sha: Moved to the end and added parameter; the partial result for
		iAttitude now factors in. */
	iAttitude += AI_getWarAttitude(ePlayer, iAttitude);

	m_aiAttitude[ePlayer] = range(iAttitude, -100, 100);
	// <advc.130e>
	if(bUpdateWorstEnemy)
		GET_TEAM(getTeam()).AI_updateWorstEnemy(); // </advc.130e>
}

// for making minor adjustments
void CvPlayerAI::AI_changeCachedAttitude(PlayerTypes ePlayer, int iChange)
{
	FAssert(ePlayer >= 0 && ePlayer < MAX_PLAYERS);
	m_aiAttitude[ePlayer] += iChange;
} // K-Mod end

/*  <advc.130w> Gained and lost cities may change expansionist hate and perhaps other
	modifiers too. This function should be called on the city owner. CvPlot param
	b/c the city may have been razed. */
void CvPlayerAI::AI_updateCityAttitude(CvPlot const& kCityPlot)
{
	if(isBarbarian())
		return;
	for(int i = 0; i < MAX_CIV_PLAYERS; i++)
	{
		CvPlayerAI& kOtherCiv = GET_PLAYER((PlayerTypes)i);
		if(kOtherCiv.isAlive() && kOtherCiv.getID() != getID() && !kOtherCiv.isMinorCiv() &&
				kCityPlot.isRevealed(kOtherCiv.getTeam()) &&
				GET_TEAM(kOtherCiv.getTeam()).isHasMet(getTeam()))
			kOtherCiv.AI_updateAttitude(getID());
	}
} // </advc.130w>

// K-Mod note: the bulk of this function has been moved into CvPlayerAI::AI_updateAttitude.
int CvPlayerAI::AI_getAttitudeVal(PlayerTypes ePlayer, bool bForced) const
{
	// <advc.006> All other attitude functions also check this
	if(getID() == ePlayer)
	{
		FAssert(getID() != ePlayer);
		return 100;
	} // </advc.006>  <advc.130u>
	/*  Moved up from the bForced block - I don't think we should ever care about the
		true attitudes within a team */
	if (getTeam() == TEAMID(ePlayer))
		return 100;
	if (isHuman())
	{
		if (GET_TEAM(ePlayer).isHuman() || GET_PLAYER(ePlayer).isHuman())
			return GC.getDefineINT(CvGlobals::RELATIONS_THRESH_ANNOYED) - 1;
		// Cautious or Annoyed depending on ePlayer's attitude
		return ::range(GET_PLAYER(ePlayer).AI_getAttitudeVal(getID()),
				GC.getDefineINT(CvGlobals::RELATIONS_THRESH_ANNOYED) - 1, 0);
		return 0;
	} // </advc.130u>
	// <advc.003n> Moved out of the bForced branch and assert added
	if(isBarbarian() || ePlayer == BARBARIAN_PLAYER)
	{
		FAssert(false);
		return -100;
	} // </advc.003n>
	if (bForced)
	{
		if (GET_TEAM(getTeam()).isVassal(TEAMID(ePlayer))
				/*&& !GET_TEAM(getTeam()).isCapitulated()))*/) // advc.130v: commented out
			return 100;
		// <advc.130v>
		if(GET_TEAM(getTeam()).isCapitulated()) // Same val as master, but at most Cautious.
		{
			return std::min(0, GET_TEAM(getMasterTeam()).AI_getAttitudeVal(TEAMID(ePlayer),
					/*	advc: Some Python call can happen here before teams are loaded;
						don't assert that the team is non-empty. */ true, false));
		}
		if(GET_TEAM(ePlayer).isCapitulated())
		{
			if(GET_TEAM(ePlayer).getMasterTeam() == getMasterTeam())
				return GC.getDefineINT(CvGlobals::RELATIONS_THRESH_PLEASED) + 1;
			return GET_TEAM(getTeam()).AI_getAttitudeVal(GET_PLAYER(ePlayer).getMasterTeam(),
					true, false); // advc: bAssert=false
		} // </advc.130v>
	}
	return m_aiAttitude[ePlayer];
}


int CvPlayerAI::AI_calculateStolenCityRadiusPlots(PlayerTypes ePlayer,
		bool bOnlyNonWorkable) const // advc.147
{
	PROFILE_FUNC();
	FAssert(ePlayer != getID());
	int iCount = 0;
	/*for (int iI = 0; iI < GC.getMap().numPlots(); iI++) {
		CvPlot* pLoopPlot = GC.getMap().plotByIndex(iI);
		if (pLoopPlot->getOwner() == ePlayer) {
			if (pLoopPlot->isPlayerCityRadius(getID()))
				iCount++;
		}
	}*/ // <advc.147> Replacing the above
	/*  Note that this function is only called once per turn. And I'm not sure
		if the new code is slower. */
	FOR_EACH_CITY(pCity, *this)
	{
		CvCity const& c = *pCity;
		int iStolenLoop = 0;
		int iUpperBound = std::max(6, (c.getPopulation() + c.getHighestPopulation()) / 2);
		for (CityPlotIter it(c, false); it.hasNext(); ++it)
		{
			CvPlot const& p = *it;
			/*  Don't count tiles in the outer ring as stolen when borders
				haven't expanded yet. */
			if (::plotDistance(&p, c.plot()) > c.getCultureLevel())
				continue;
			if (p.getOwner() == ePlayer &&
				(!bOnlyNonWorkable || p.getCityRadiusCount() <= 1))
			{
				iStolenLoop++;
				if (iStolenLoop >= iUpperBound)
					break;
				if (p.getNonObsoleteBonusType(
					/*  bOnlyNonWorkable implies that we're interested in tiles
						ePlayer would like to be able to work */
					bOnlyNonWorkable ? TEAMID(ePlayer) : getTeam()) != NO_BONUS)
				{
					iStolenLoop++;
				}
				if(iStolenLoop >= iUpperBound)
					break;
			}
		}
		iCount += iStolenLoop;
	} // </advc.147>
	return iCount;
}

// K-Mod  // advc: Renamed from "updateCloseBordersAttitudeCache"
void CvPlayerAI::AI_updateCloseBorderAttitude()
{
	/*  advc.opt: Was looping over all players. (Setting it to 0 for defeated players
		seems prudent though.) */
	for (PlayerIter<EVER_ALIVE> it(getTeam()); it.hasNext(); ++it)
		AI_updateCloseBorderAttitude(it->getID());
}

// Most of this function has been moved directly from AI_getCloseBorderAttitude
void CvPlayerAI::AI_updateCloseBorderAttitude(PlayerTypes ePlayer)
{
	PROFILE_FUNC();

	const CvTeamAI& kOurTeam = GET_TEAM(getTeam());
	TeamTypes eTheirTeam = TEAMID(ePlayer);

	if (!isAlive() || !GET_PLAYER(ePlayer).isAlive() ||
		getTeam() == eTheirTeam || kOurTeam.isVassal(eTheirTeam) ||
		GET_TEAM(eTheirTeam).isVassal(getTeam()) ||
		!kOurTeam.isHasMet(eTheirTeam)) // advc.opt
	{
		m_aiCloseBordersAttitude[ePlayer] = 0;
		return;
	}
	// <advc.130v>
	int const iLimit = GC.getInfo(getPersonalityType()).getCloseBordersAttitudeChange();
	if(iLimit == 0)
	{
		m_aiCloseBordersAttitude[ePlayer] = 0;
		return;
	} // </advc.130v>
	// <advc.147>
	int const iLand = getTotalLand();
	if(iLand < 5)
	{
		m_aiCloseBordersAttitude[ePlayer] = 0;
		return;
	}
	double theySteal_weight = 8;
	double weSteal_weight = 5;
	double adj_weight = 5;
	// </advc.147>
	// <advc.035>
	double flip_weight = 6;
	int iWeOwnCulturally = 0;
	if(GC.getDefineBOOL(CvGlobals::OWN_EXCLUSIVE_RADIUS))
	{
		bool bWar = kOurTeam.isAtWar(eTheirTeam);
		if(!bWar)
		{
			std::vector<CvPlot*> contested;
			::contestedPlots(contested, kOurTeam.getID(), eTheirTeam);
			for(size_t i = 0; i < contested.size(); i++)
				if(contested[i]->getSecondOwner() == getID())
					iWeOwnCulturally++;
		}
		weSteal_weight = 0;
		if(!bWar)
			theySteal_weight *= 1.5;
	} // </advc.035>
	// <advc.147>
	double const landFactor = ::dRange(GC.getInfo(GC.getMap().getWorldSize()).
			getTargetNumCities() / std::sqrt((double)iLand), 0.3, 1.0);
	theySteal_weight *= landFactor;
	weSteal_weight *= landFactor;
	adj_weight *= landFactor;
	flip_weight *= landFactor; // advc.035
	int iTheySteal = AI_calculateStolenCityRadiusPlots(ePlayer);
	int iWeSteal = 0;
	if(weSteal_weight > 0.01) // A little costly; don't do it if we don't have to.
		iWeSteal = GET_PLAYER(ePlayer).AI_calculateStolenCityRadiusPlots(getID(), true);
	// </advc.147>
	int iPercent = ::round(std::min(65.0, iTheySteal * theySteal_weight +
			flip_weight * iWeOwnCulturally // advc.035
			+ weSteal_weight * iWeSteal)); // advc.147
	// K-Mod. I've rewritten AI_isLandTarget. The condition I'm using here is equivalent to the original function.
	/*if (kOurTeam.AI_hasCitiesInPrimaryArea(eTheirTeam) && kOurTeam.AI_calculateAdjacentLandPlots(eTheirTeam) >= 8) // K-Mod end
		iPercent += 40;*/
	// <advc.147> Replacing the above
	if (kOurTeam.AI_hasCitiesInPrimaryArea(eTheirTeam))
	{
		int iAdjLand = kOurTeam.AI_calculateAdjacentLandPlots(eTheirTeam);
		if(iAdjLand > 5)
			iPercent += std::min(40, ::round(iAdjLand * adj_weight));
	} // </advc.147>
	// <advc.130v>
	if(GET_TEAM(ePlayer).isCapitulated())
		iPercent /= 2; // </advc.130v>
	/*  A bit messy to access other entries of the borders attitude cache while
		updating, but shouldn't have any noticeable side-effect. */
	for (PlayerIter<ALIVE,VASSAL_OF> it(TEAMID(ePlayer)); it.hasNext(); ++it)
	{
		CvPlayerAI const& kVassal = *it;
		if(GET_TEAM(kVassal.getID()).isCapitulated() && kVassal.getTeam() != getTeam())
		{
			int iFromVassal = (m_aiCloseBordersAttitude[kVassal.getID()] * 100) / iLimit;
			if(iFromVassal > 0)
				iPercent += iFromVassal;
		}
	}
	iPercent = std::min(iPercent, 100); // </advc.130v>
	// bbai  // advc.147: Disabling this
	/*if (AI_isDoStrategy(AI_VICTORY_CONQUEST3))
		iPercent = std::min(120, (3 * iPercent) / 2);*/
	m_aiCloseBordersAttitude[ePlayer] = (iLimit * iPercent) / 100;
}
// K-Mod end

// K-Mod note: the bulk of this function has been moved to AI_updateCloseBorderAttitude
int CvPlayerAI::AI_getCloseBordersAttitude(PlayerTypes ePlayer) const
{
	return m_aiCloseBordersAttitude[ePlayer];
}

// <advc.sha>
int CvPlayerAI::warSuccessAttitudeDivisor() const
{
	return GC.getWAR_SUCCESS_CITY_CAPTURING() + 2 * getCurrentEra();
} // </advc.sha>

int CvPlayerAI::AI_getWarAttitude(PlayerTypes ePlayer, /* advc.sha: */ int iPartialSum) const
{
	int iAttitude = 0;

	if(!::atWar(getTeam(), TEAMID(ePlayer)))
		return 0; // advc
	// advc.130g: Was hardcoded as iAttitude-=3; no functional change.
	iAttitude += GC.getDefineINT(CvGlobals::AT_WAR_ATTITUDE_CHANGE);

	if (GC.getInfo(getPersonalityType()).getAtWarAttitudeDivisor() != 0)
	{
		int iAttitudeChange = (GET_TEAM(getTeam()).AI_getAtWarCounter(GET_PLAYER(ePlayer).getTeam()) / GC.getInfo(getPersonalityType()).getAtWarAttitudeDivisor());
		// <advc.sha> Factor war success into WarAttitude
		iAttitudeChange = ((-iAttitudeChange) +
				// Mean of time-based penalty and a penalty based on success and era
				GET_TEAM(ePlayer).AI_getWarSuccess(getTeam()) /
				warSuccessAttitudeDivisor()) / -2; // </advc.sha>
		int iLimit = abs(GC.getInfo(getPersonalityType()).
				getAtWarAttitudeChangeLimit()); // advc
		iAttitude += range(iAttitudeChange, -iLimit, iLimit);
	}
	// <advc.sha> Bring it down to 0 at most
	if(iPartialSum > MIN_INT && iPartialSum + iAttitude >= 0)
		iAttitude -= iPartialSum + iAttitude + 1; // </advc.sha>
	return iAttitude;
}


int CvPlayerAI::AI_getPeaceAttitude(PlayerTypes ePlayer) const
{
	if(GC.getInfo(getPersonalityType()).getAtPeaceAttitudeDivisor() == 0)
		return 0;

	int iDivisor = GC.getInfo(getPersonalityType()).getAtPeaceAttitudeDivisor();
	// <advc.130a>
	if(iDivisor == 0)
		return 0;
	/*  <advc.130a> Use the minimum of peace counter and has-met counter, but
		reduce the divisor based on game progress, meaning that civs who meet
		late have to wait only 30 turns instead of 60. */
	int iMetAndAtPeace = GET_TEAM(getTeam()).AI_getAtPeaceCounter(TEAMID(ePlayer)); // as in BtS
	iMetAndAtPeace = std::min(iMetAndAtPeace, GET_TEAM(getTeam()).AI_getHasMetCounter(TEAMID(ePlayer)));
	iDivisor = ::round(iDivisor * std::max(0.5, 1 - GC.getGame().gameTurnProgress()));
	/*  Rounded down as in BtS; at least iDivisor turns need to pass before
		diplo improves. */
	int iAttitudeChange = iMetAndAtPeace / iDivisor;
	int iLimit = abs(GC.getInfo(getPersonalityType()).getAtPeaceAttitudeChangeLimit()); // as in BtS
	// </advc.130a>
	return range(iAttitudeChange, -iLimit, iLimit);
}


int CvPlayerAI::AI_getSameReligionAttitude(PlayerTypes ePlayer) const
{
	int iAttitude = 0;

	if ((getStateReligion() != NO_RELIGION) && (getStateReligion() == GET_PLAYER(ePlayer).getStateReligion()))
	{
		iAttitude += GC.getInfo(getPersonalityType()).getSameReligionAttitudeChange();

		if (hasHolyCity(getStateReligion()))
		{
			iAttitude++;
		}

		if (GC.getInfo(getPersonalityType()).getSameReligionAttitudeDivisor() != 0)
		{
			int iAttitudeChange = (AI_getSameReligionCounter(ePlayer) / GC.getInfo(getPersonalityType()).getSameReligionAttitudeDivisor());
			// <advc.130x>
			int iLimit = AI_ideologyDiploLimit(ePlayer, 0);
			iAttitude += range(iAttitudeChange, -iLimit, iLimit); // </advc.130x>
		}
	}

	return iAttitude;
}


int CvPlayerAI::AI_getDifferentReligionAttitude(PlayerTypes ePlayer) const
{
	// <advc.130n> Functional changes only start at iTimeKnown=...
	CvPlayer const& kPlayer = GET_PLAYER(ePlayer);
	if(getStateReligion() == NO_RELIGION || kPlayer.getStateReligion() == NO_RELIGION ||
			getStateReligion() == kPlayer.getStateReligion())
		return 0;
	CvLeaderHeadInfo& lh = GC.getInfo(getPersonalityType());
	int r = lh.getDifferentReligionAttitudeChange();
	int div = lh.getDifferentReligionAttitudeDivisor();
	if(div != 0)
	{
		int iLimit = AI_ideologyDiploLimit(ePlayer, 1); // advc.130x
		r += range(AI_getDifferentReligionCounter(ePlayer) / div, -iLimit, iLimit);
	}
	int iTimeKnown = GET_TEAM(getTeam()).AI_getReligionKnownSince(kPlayer.getStateReligion());
	if(iTimeKnown < 0)
		return 0;
	iTimeKnown = GC.getGame().getGameTurn() - iTimeKnown;
	FAssert(iTimeKnown >= 0);
	if(div != 0)
	{
		// max b/c they're negative values
		r = std::max(r, (2 * iTimeKnown) / (3 * div));
	}
	if(r < 0)
	{
		// No change to this, but apply it after the iTimeKnown cap:
		if(hasHolyCity(getStateReligion()))
			r--;
	} // </advc.130n>
	return r;
}


int CvPlayerAI::AI_getBonusTradeAttitude(PlayerTypes ePlayer) const
{
	if (!atWar(getTeam(), GET_PLAYER(ePlayer).getTeam()))
	{
		if (GC.getInfo(getPersonalityType()).getBonusTradeAttitudeDivisor() != 0)
		{
			int iAttitudeChange = (AI_getBonusTradeCounter(ePlayer) / GC.getInfo(getPersonalityType()).getBonusTradeAttitudeDivisor());
			return range(iAttitudeChange, -(abs(GC.getInfo(getPersonalityType()).getBonusTradeAttitudeChangeLimit())), abs(GC.getInfo(getPersonalityType()).getBonusTradeAttitudeChangeLimit()));
		}
	}

	return 0;
}


int CvPlayerAI::AI_getOpenBordersAttitude(PlayerTypes ePlayer) const
{
	if (!atWar(getTeam(), GET_PLAYER(ePlayer).getTeam()))
	{
		if (GC.getInfo(getPersonalityType()).getOpenBordersAttitudeDivisor() != 0)
		{
			int iAttitudeChange = (GET_TEAM(getTeam()).AI_getOpenBordersCounter(GET_PLAYER(ePlayer).getTeam()) / GC.getInfo(getPersonalityType()).getOpenBordersAttitudeDivisor());
			return range(iAttitudeChange, -(abs(GC.getInfo(getPersonalityType()).getOpenBordersAttitudeChangeLimit())), abs(GC.getInfo(getPersonalityType()).getOpenBordersAttitudeChangeLimit()));
		}
	}

	return 0;
}


int CvPlayerAI::AI_getDefensivePactAttitude(PlayerTypes ePlayer) const
{
	if (getTeam() != GET_PLAYER(ePlayer).getTeam() && (GET_TEAM(getTeam()).isVassal(GET_PLAYER(ePlayer).getTeam()) || GET_TEAM(GET_PLAYER(ePlayer).getTeam()).isVassal(getTeam())))
	{	// advc.130m:
		if(!GET_TEAM(getTeam()).isCapitulated() && !GET_TEAM(ePlayer).isCapitulated())
			return GC.getInfo(getPersonalityType()).getDefensivePactAttitudeChangeLimit();
	}

	if (!atWar(getTeam(), GET_PLAYER(ePlayer).getTeam())
		// advc.130p: Only apply the bonus when currently allied
		&& GET_TEAM(getTeam()).isDefensivePact(TEAMID(ePlayer)))
	{
		if (GC.getInfo(getPersonalityType()).getDefensivePactAttitudeDivisor() != 0)
		{
			int iAttitudeChange = (GET_TEAM(getTeam()).AI_getDefensivePactCounter(GET_PLAYER(ePlayer).getTeam()) / GC.getInfo(getPersonalityType()).getDefensivePactAttitudeDivisor());
			return range(iAttitudeChange, -(abs(GC.getInfo(getPersonalityType()).getDefensivePactAttitudeChangeLimit())), abs(GC.getInfo(getPersonalityType()).getDefensivePactAttitudeChangeLimit()));
		}
	}

	return 0;
}


int CvPlayerAI::AI_getRivalDefensivePactAttitude(PlayerTypes ePlayer) const
{
	return AI_rivalPactAttitude(ePlayer, false); // advc.130t
	// BtS formula:
	/*if(!GET_TEAM(ePlayer).isDefensivePact(getTeam()))
		return (-4 * GET_TEAM(ePlayer).getDefensivePactCount(TEAMID(ePlayer))) /
				std::max(1, (GC.getGame().countCivTeamsAlive() - 2)); */
}

// <advc.130w>
int CvPlayerAI::AI_getExpansionistAttitude(PlayerTypes ePlayer) const
{
	CvPlayer const& kOther = GET_PLAYER(ePlayer);
	double foreignCities = 0;
	FOR_EACH_CITY(pCity, kOther)
	{
		CvCity const& c = *pCity;
		if(!c.isRevealed(getTeam()))
			continue;
		TeamTypes eHighestCultureTeam = c.getPlot().findHighestCultureTeam();
		if(eHighestCultureTeam != TEAMID(ePlayer))
		{
			double claimFactor = 1;
			/*  Doesn't capture the case where they raze our city to make room
				for one of theirs, but that's covered by raze memory attitude. */
			if(eHighestCultureTeam == getTeam() && c.isEverOwned(getID()))
				claimFactor += 2;
			// Weighted by 2 times the difference between highest and ePlayer's
			foreignCities += dRange(0.02 *
					(c.getPlot().calculateTeamCulturePercent(eHighestCultureTeam)
					- c.getPlot().calculateTeamCulturePercent(TEAMID(ePlayer))),
					0.0, 1.0) * claimFactor;
		}
	}
	/*  1 city could just be one placed a bit too close to a foreign capital
		in the early game */
	if(foreignCities <= 1)
		return 0;
	int iCivCities = GC.getGame().getNumCivCities() -
			GET_TEAM(BARBARIAN_TEAM).getNumCities();
	double citiesPerCiv = std::max((double)
			GC.getInfo(GC.getMap().getWorldSize()).getTargetNumCities(),
			iCivCities / (double)GC.getGame().getCivTeamsEverAlive());
	EraTypes iGameEra = GC.getGame().getCurrentEra();
	if(iGameEra <= 2)
		citiesPerCiv = std::min(citiesPerCiv, 3.0 * (1 + iGameEra));
	double personalFactor = AI_expansionistHate(ePlayer);
	return -std::min(4, ::round(personalFactor * 2.4 * foreignCities / citiesPerCiv));
}

int CvPlayerAI::AI_getRivalVassalAttitude(PlayerTypes ePlayer) const
{
	if(GET_PLAYER(ePlayer).getMasterTeam() == getMasterTeam())
		return 0;
	CvGame const& g = GC.getGame();
	int iEverAlive = g.getCivTeamsEverAlive();
	double avgCities = g.getNumCities() / (double)iEverAlive;
	double totalVassalSzFactor = 0;
	for(int i = 0; i < MAX_CIV_TEAMS; i++)
	{
		CvTeam const& t = GET_TEAM((TeamTypes)i);
		if(t.isAlive() && t.isVassal(TEAMID(ePlayer)) && t.isCapitulated())
			totalVassalSzFactor += ::dRange(2.5 * t.getNumCities() / avgCities, 0.5, 1.5);
	}
	return AI_rivalPactAttitude(ePlayer, true) // advc.130t
			- ::round(AI_expansionistHate(ePlayer) * 5 * totalVassalSzFactor /
			std::sqrt((double)iEverAlive));
	// BtS formula:
	/*if(GET_TEAM(ePlayer).getVassalCount(getTeam()) > 0)
		return (-6 * GET_TEAM(ePlayer).getPower(true)) /
				std::max(1, GC.getGame().countTotalCivPower());*/
	// </advc.130w>
}

// <advc.130t>
int CvPlayerAI::AI_rivalPactAttitude(PlayerTypes ePlayer, bool bVassalPacts) const
{
	if(GET_PLAYER(ePlayer).getMasterTeam() == getMasterTeam())
		return 0;
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
	double score = 0;
	int iTotal = 0;
	for(int i = 0; i < MAX_CIV_TEAMS; i++)
	{
		CvTeam const& t = GET_TEAM((TeamTypes)i);
		if(!t.isAlive() || t.isMinorCiv() || !kOurTeam.isHasMet(t.getID()) ||
				// If we're at war, then apparently the DP isn't deterring us
				kOurTeam.isAtWar(t.getID()) ||
				(bVassalPacts ? t.isCapitulated() : t.isAVassal()) ||
				t.getID() == getTeam() || t.getID() == TEAMID(ePlayer))
			continue;
		iTotal++;
		if((bVassalPacts && t.isVassal(TEAMID(ePlayer))) ||
				(!bVassalPacts && GET_TEAM(ePlayer).isDefensivePact(t.getID()) &&
				!kOurTeam.isDefensivePact(t.getID()) &&
				kOurTeam.getPower(true) / (double)t.getPower(true) > 0.7))
		{
			// Score up to -4 per pact -- too much? Cap at ATTITUDE_FRIENDLY-2?
			score += std::max(0, std::min((int)ATTITUDE_FRIENDLY - 1,
					(GC.getInfo(getPersonalityType()).
					getDeclareWarThemRefuseAttitudeThreshold() + 1) -
					/*  Calling AI_getAttitude doesn't lead to infinite recursion
						because the result is read from cache. */
					kOurTeam.AI_getAttitude(t.getID())));
			if(kOurTeam.AI_getWorstEnemy() == t.getID())
				score += 1;
		}
	}
	if(iTotal == 0)
		return 0;
	// Moderate impact of the number of civs: only one over sqrt(iTotal)
	return -std::min(5, ::round((2 * score) / std::sqrt((double)iTotal)));
} // </advc.130t>

// <advc.130w>
double CvPlayerAI::AI_expansionistHate(PlayerTypes ePlayer) const
{
	CvLeaderHeadInfo& lh = GC.getInfo(getPersonalityType());
	double r = (5 + AI_getPeaceWeight() - lh.getWarmongerRespect()) / 12.0;
	double powRatio = GET_TEAM(getTeam()).getPower(true) / (double)
			GET_TEAM(ePlayer).getPower(true);
	r = std::min(r, powRatio);
	return ::dRange(r, 0.0, 1.0);
} // </advc.130w>

int CvPlayerAI::AI_getShareWarAttitude(PlayerTypes ePlayer) const
{
	// <advc.130m> Ended up rewriting this function entirely
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
	if(kOurTeam.isCapitulated() || GET_TEAM(ePlayer).isCapitulated() ||
			atWar(getTeam(), TEAMID(ePlayer)))
		return 0;
	CvLeaderHeadInfo& lh = GC.getInfo(getPersonalityType());
	int div = lh.getShareWarAttitudeDivisor();
	if(div == 0)
		return 0;
	bool bShareAny = kOurTeam.AI_shareWar(TEAMID(ePlayer));
	double nonSharedModifier = 1;
	if(kOurTeam.getNumWars() > 0)
	{
		int iWSThresh = ::round((3.0 * warSuccessAttitudeDivisor()) / 4);
		for(int i = 0; i < MAX_CIV_TEAMS; i++)
		{
			CvTeamAI const& t = GET_TEAM((TeamTypes)i);
			/*  NB: AtWarCount is the number of teams we're at war with, whereas
				AtWarCounter is the number of turns we've been at war. */
			if(t.isAlive() && !t.isMinorCiv() && !t.isAtWar(TEAMID(ePlayer)) &&
					kOurTeam.AI_getAtWarCounter(t.getID()) >= 5 &&
					kOurTeam.AI_getWarSuccess(t.getID()) +
					t.AI_getWarSuccess(kOurTeam.getID()) > iWSThresh &&
					(kOurTeam.AI_getWarPlan(t.getID()) != WARPLAN_DOGPILE ||
					kOurTeam.AI_getAtWarCounter(t.getID()) >= 15) &&
					GET_TEAM(ePlayer).AI_isLandTarget(t.getID()) &&
					t.AI_isLandTarget(kOurTeam.getID()))
			{
				int iWSRating = kOurTeam.AI_getWarSuccessRating();
				int const iWSRatingThresh = 25;
				if(iWSRating < iWSRatingThresh)
				{
					nonSharedModifier += (iWSRating - iWSRatingThresh -
							kOurTeam.AI_getAtWarCounter(t.getID())) / 100.0;
				}
			}
		}
	}
	int r = 0;
	if(bShareAny) // No functional change here
		r += lh.getShareWarAttitudeChange();
	int iTurnsShared = kOurTeam.AI_getShareWarCounter(TEAMID(ePlayer));
	int iTheirContribution = kOurTeam.AI_getSharedWarSuccess(TEAMID(ePlayer));
	int iLimit = abs(lh.getShareWarAttitudeChangeLimit());
	iLimit = std::min(iLimit, 1 + iTurnsShared / div);
	r += range((int)((iTheirContribution * std::max(0.0, nonSharedModifier)) /
			// This divisor seems to produce roughly the result I have in mind
			(3.5 * GC.getWAR_SUCCESS_CITY_CAPTURING() * div)), 0, iLimit);
	return r;
	// </advc.130m>
}


int CvPlayerAI::AI_getFavoriteCivicAttitude(PlayerTypes ePlayer) const
{
	int iAttitude = 0;
	if (GC.getInfo(getPersonalityType()).getFavoriteCivic() != NO_CIVIC)
	{
		if (isCivic((CivicTypes)(GC.getInfo(getPersonalityType()).getFavoriteCivic())) && GET_PLAYER(ePlayer).isCivic((CivicTypes)(GC.getInfo(getPersonalityType()).getFavoriteCivic())))
		{
			iAttitude += GC.getInfo(getPersonalityType()).getFavoriteCivicAttitudeChange();

			if (GC.getInfo(getPersonalityType()).getFavoriteCivicAttitudeDivisor() != 0)
			{
				int iAttitudeChange = (AI_getFavoriteCivicCounter(ePlayer) / GC.getInfo(getPersonalityType()).getFavoriteCivicAttitudeDivisor());
				// <advc.130x>
				int iLimit = AI_ideologyDiploLimit(ePlayer, 2);
				iAttitude += range(iAttitudeChange, -iLimit, iLimit); // </advc.130x>
			}
		}
	}

	return iAttitude;
}

// <advc.130p> No BtS code left here
int CvPlayerAI::AI_getTradeAttitude(PlayerTypes ePlayer) const
{
	double r = AI_getPeacetimeGrantValue(ePlayer);
	double tradeValDiff = AI_getPeacetimeTradeValue(ePlayer) -
			(5/6.0) * GET_PLAYER(ePlayer).AI_getPeacetimeTradeValue(getID());
	r += std::max(0.0, tradeValDiff);
	if (r > 0)
		r = 11.5 * std::pow((double)r, 2/3.0); // Diminishing returns
	return range(::round(r * GET_TEAM(ePlayer).AI_recentlyMetMultiplier(getTeam()))
			/ AI_peacetimeTradeValDivisor(false), 0, PEACETIME_TRADE_RELATIONS_LIMIT);
	/*  (Adjustment to game progress now happens when recording grant and trade values;
		decay happens explicitly in AI_doCounter.) */
}


int CvPlayerAI::AI_peacetimeTradeValDivisor(bool bRival) const
{
	int const tuningFactor = 95;
	return (tuningFactor * 1000) / ::range(GC.getInfo(getPersonalityType()).
			getMemoryAttitudePercent(bRival ? MEMORY_EVENT_BAD_TO_US :
			MEMORY_EVENT_GOOD_TO_US) * (bRival ? -1 : 1), 1, 1000);
}


int CvPlayerAI::AI_getRivalTradeAttitude(PlayerTypes ePlayer) const
{
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
	TeamTypes eTeam = TEAMID(ePlayer);
	int r = kOurTeam.AI_getEnemyPeacetimeGrantValue(eTeam) +
			(kOurTeam.AI_getEnemyPeacetimeTradeValue(eTeam) / 2);
	if (r > 0)
		r = ::round(11.5 * std::pow((double)r, 2/3.0)); // Diminishing returns
	// OB hurt our feelings also
	TeamTypes eEnemy = kOurTeam.AI_getWorstEnemy();
	/*  Checking enemyValue here should - hopefully - fix an oscillation problem,
		but I don't quite know what I'm doing ...
		(Checking this in CvTeamAI::AI_getWorstEnemy would be bad for performance.) */
	if(kOurTeam.AI_enmityValue(eEnemy) <= 0)
		eEnemy = NO_TEAM;
	double dualDealCounter = 0; // cf. CvDeal::isDual
	if(eEnemy != NO_TEAM)
	{
		if(GET_TEAM(eTeam).isOpenBorders(eEnemy))
		{
			dualDealCounter += std::min(20,
					std::min(kOurTeam.AI_getHasMetCounter(eTeam),
					GET_TEAM(eTeam).AI_getOpenBordersCounter(eEnemy)));
			if(kOurTeam.isAtWar(eEnemy))
				/*  Don't like that enemy units can move through the borders of
					ePlayer and heal there, but only moderate increase b/c we
					can't well afford to make further enemies when already in a war. */
				dualDealCounter *= 1.5;
		}
		/*  Resource trades are handled by CvDeal::doTurn (I wrote the code below
			before realizing that) */
		/*dualDealCounter += std::min(20.0,
				std::min((double)kOurTeam.AI_getHasMetCounter(ePlayer),
				GET_PLAYER(ePlayer).AI_getBonusTradeCounter(eEnemy) / 2.0));*/
		// <advc.130v> Blame it on the master instead
		if(GET_TEAM(ePlayer).isCapitulated())
			dualDealCounter /= 2; // </advc.130v>
		/*  Our enemy could be liked by everyone else, but we can't afford to hate
			everyone. */
		int const iCounterBound = 15;
		if(dualDealCounter > iCounterBound)
		{
			int iOB = 0;
			int iTotal = 0;
			/*  Vassals have their own OB agreements and can agree to embargos;
				count them fully here. */
			for(int i = 0; i < MAX_CIV_TEAMS; i++)
			{
				CvTeam const& t = GET_TEAM((TeamTypes)i);
				if(t.isAlive() && t.getID() != eEnemy && t.isHasMet(getTeam()))
				{
					iTotal++;
					if(t.isOpenBorders(eEnemy))
						iOB++;
				}
			}
			double nonOBRatio = (iTotal - iOB) / ((double)iTotal);
			dualDealCounter = std::max((double)iCounterBound, nonOBRatio);
		}
		// <advc.130v>
		int iVassalDealCounter = 0;
		for(int i = 0; i < MAX_CIV_TEAMS; i++)
		{
			CvTeamAI const& t = GET_TEAM((TeamTypes)i);
			if(t.isCapitulated() && t.getID() != getTeam() &&
				eEnemy != TEAMID(ePlayer) && // Master mustn't be the enemy
				t.isVassal(TEAMID(ePlayer)) && t.isAlive() && !t.isMinorCiv() &&
				t.isOpenBorders(eEnemy))
			{
				int iFromVassal = t.AI_getOpenBordersCounter(eEnemy) / 2;
				if(iFromVassal > 0)
					iVassalDealCounter += iFromVassal;
			}
		}
		dualDealCounter += std::min(iVassalDealCounter, 10);
		// </advc.130v>
	}
	r = (::round(r * GET_TEAM(ePlayer).AI_recentlyMetMultiplier(getTeam())) +
			75 * ::round(dualDealCounter)) / AI_peacetimeTradeValDivisor(true);
	return -range(r, 0, PEACETIME_TRADE_RELATIONS_LIMIT);
	/*  (Adjustment to game progress now happens when recording grant and trade values;
		decay happens explicitly in AI_doCounter.) */
}

int CvPlayerAI::AI_getBonusTradeCounter(TeamTypes eTo) const {

	int r = 0;
	for (MemberIter it(eTo); it.hasNext(); ++it)
		r += AI_getBonusTradeCounter(it->getID());
	return r;
} // </advc.130p>


int CvPlayerAI::AI_getMemoryAttitude(PlayerTypes ePlayer, MemoryTypes eMemory) const
{
	// <advc.130s>
	if(eMemory == MEMORY_ACCEPTED_JOIN_WAR)
	{
		CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
		static bool bJOIN_WAR_DIPLO_BONUS = GC.getDefineBOOL("ENABLE_JOIN_WAR_DIPLO_BONUS");
		if(!bJOIN_WAR_DIPLO_BONUS ||
			(kOurTeam.getNumWars() > 0 && !kOurTeam.AI_shareWar(TEAMID(ePlayer))) ||
			kOurTeam.isAtWar(TEAMID(ePlayer)))
		{
			return 0;
		}
	} // </advc.130s>
	/* <advc.130j> Was 100. Effect halved b/c diplo actions now counted twice.
		195 (instead of 200) to make sure that 0.5 gets rounded up. BtS rounded down,
		but this rarely mattered. Now rounding down would often result in no effect
		on relations. */
	double div = 195;
	if(eMemory == MEMORY_DECLARED_WAR) // Finer granularity for DoW
		div = 295; // </advc.130j>
	int iAttitudePercent = GC.getInfo(getPersonalityType()).getMemoryAttitudePercent(eMemory);
	/*	<advc.553> Too lazy to change this in XML.
		(Or rather: Not yet sure if I've gotten the numbers right.) */
	if (iAttitudePercent > 0 && eMemory == MEMORY_TRADED_TECH_TO_US)
		iAttitudePercent = 4 + (iAttitudePercent * 4) / 5; // </advc.553>
	return ::round((AI_getMemoryCount(ePlayer, eMemory) * iAttitudePercent) / div);
}

// advc.130r: Now handled through MemoryAttitude
/*int CvPlayerAI::AI_getColonyAttitude(PlayerTypes ePlayer) const {
	int iAttitude = 0;
	if (getParent() == ePlayer)
		iAttitude += GC.getInfo(getPersonalityType()).getFreedomAppreciation();
	return iAttitude;
}*/

// <advc.sha> Some changes to DaveMcW's code.
// BEGIN: Show Hidden Attitude Mod 01/22/2009
int CvPlayerAI::AI_getFirstImpressionAttitude(PlayerTypes ePlayer) const
{
	CvPlayerAI& kPlayer = GET_PLAYER(ePlayer);
	CvHandicapInfo& h = GC.getInfo(kPlayer.getHandicapType());
	CvLeaderHeadInfo& lh = GC.getInfo(getPersonalityType());
	int iAttitude = lh.getBaseAttitude();
	iAttitude += h.getAttitudeChange();
	if(!kPlayer.isHuman())
	{	// <advc.130b>
		// Original peace weight modifier
		int iPeaceWeightModifier = 4 - abs(AI_getPeaceWeight() -
				kPlayer.AI_getPeaceWeight());
		double personalityModifier = iPeaceWeightModifier * 0.45;
		// Original warmonger respect modifier
		int iRespectModifier = std::min(lh.getWarmongerRespect(),
				GC.getInfo(kPlayer.getPersonalityType()).
				getWarmongerRespect());
		personalityModifier += iRespectModifier * 0.75;
		personalityModifier += h.getAIAttitudeChangePercent() / 100.0; // advc.148
		/*  <advc.104x> Low UWAI_PERSONALITY_PERCENT makes the peace weights and
			respect values more similar; don't want that to increase the relations bonus. */
		static int const iUWAI_PERSONALITY_PERCENT = GC.getDefineINT("UWAI_PERSONALITY_PERCENT");
		personalityModifier *= iUWAI_PERSONALITY_PERCENT / 100.0;
		iAttitude += ::round(personalityModifier);
		// </advc.130b>
	}
	return iAttitude;
}

int CvPlayerAI::AI_getTeamSizeAttitude(PlayerTypes ePlayer) const
{
	return -std::max(0, (GET_TEAM(ePlayer).getNumMembers() -
			GET_TEAM(getTeam()).getNumMembers()));
}

// <advc.130c>
int CvPlayerAI::AI_getRankDifferenceAttitude(PlayerTypes ePlayer) const
{
	// Cached separately to avoid updating the whole cache w/e scores change
	if(m_abTheyFarAhead[ePlayer])
		return 0; // No hate if they're way ahead
	// Don't count ranks of unknown civs
	int iRankDifference = AI_knownRankDifference(ePlayer);
	CvLeaderHeadInfo& lh = GC.getInfo(getPersonalityType());
	CvGame const& g = GC.getGame();
	/*  This was "+ 1" in BtS, which is arguably a bug.
		Continue using CivPlayersEverAlive although iRankDifference is now based
		only on known civs. */
	double maxRankDifference = g.getCivPlayersEverAlive() - 1;
	int iBase = 0;
	double multiplier = 0;
	// If we're ranked worse than they are:
	if(iRankDifference > 0)
	{
		iBase = lh.getWorseRankDifferenceAttitudeChange();
		/*  Want multiplier to be 1 when the rank difference is 35% of CivPlayersEverAlive,
			and near 0 when greater than 50% of CivPlayersEverAlive. */
		multiplier = 1 - (2.0 * std::abs(
				iRankDifference - 0.35 * maxRankDifference) / maxRankDifference);
		if(multiplier < 0)
			multiplier = 0;
	}
	/*  If we're ranked better, as in BtS, the modifier is proportional
		to the relative rank difference. */
	else
	{
		iBase = lh.getBetterRankDifferenceAttitudeChange();
		multiplier = -iRankDifference / maxRankDifference;
	}
	int r = ::round(iBase * multiplier);
	// Don't hate them if they're still in the first era
	if(r < 0 && (GET_PLAYER(ePlayer).getCurrentEra() <= g.getStartEra() ||
			/*  nor if the score difference is small (otherwise attitude
				changes too frequently in the early game) */
			m_abTheyBarelyAhead[ePlayer]))
		return 0;
	// Don't like them if we're still in the first era
	if(r > 0 && (getCurrentEra() <= g.getStartEra() ||
			GET_PLAYER(ePlayer).AI_atVictoryStage3()))
		return 0;
	if(r < 0 && AI_atVictoryStage3() && !GET_PLAYER(ePlayer).AI_atVictoryStage4())
		return 0;
	return r;
} // </advc.130c>

int CvPlayerAI::AI_getLostWarAttitude(PlayerTypes ePlayer) const
{
	FAssertMsg(false, "this function is obsolete");
	if(GET_TEAM(ePlayer).AI_getWarSuccess(getTeam()) >
			GET_TEAM(getTeam()).AI_getWarSuccess(TEAMID(ePlayer)))
		return GC.getInfo(getPersonalityType()).getLostWarAttitudeChange();
	return 0;
} // END: Show Hidden Attitude Mod </advc.sha>

// <advc.130c>
int CvPlayerAI::AI_knownRankDifference(PlayerTypes eOther) const
{
	CvGame& g = GC.getGame();
	int iOurRank = g.getPlayerRank(getID());
	int iTheirRank = g.getPlayerRank(eOther);
	int iDelta = iOurRank - iTheirRank;
	// We know who's ranked better, but (perhaps) not the exact rank difference.
	if(iDelta < -1)
		iDelta = -1;
	if(iDelta > 1)
		iDelta = 1;
	for (PlayerIter<MAJOR_CIV,KNOWN_TO> it(getTeam()); it.hasNext(); ++it)
	{
		CvPlayer const& kThirdCiv = *it;
		/*  This would deny eOther info about civs that we have met and they haven't --
			not crucial I think. */
		/*if(!GET_TEAM(eOther).isHasMet(TEAMID(kThirdCiv.getID())))
			continue;*/
		int iThirdRank = g.getPlayerRank(kThirdCiv.getID());
		if(iDelta < 0 && iThirdRank > iOurRank && iThirdRank < iTheirRank)
			iDelta--;
		else if(iDelta > 0 && iThirdRank < iOurRank && iThirdRank > iTheirRank)
			iDelta++;
	}
	return iDelta;
} // </advc.130c>

PlayerVoteTypes CvPlayerAI::AI_diploVote(const VoteSelectionSubData& kVoteData, VoteSourceTypes eVoteSource, bool bPropose)
{
	PROFILE_FUNC();

	int iI;
	CvGame& g = GC.getGame(); // advc
	VoteTypes eVote = kVoteData.eVote;

	CvTeamAI& kOurTeam = GET_TEAM(getTeam()); // K-Mod

	if (g.isTeamVote(eVote))
	{
		if (g.isTeamVoteEligible(getTeam(), eVoteSource))
			return (PlayerVoteTypes)getTeam();
		// <advc.148>
		int iBestValue = 0;
		if(GC.getInfo(eVote).isVictory())
			iBestValue = 9; // was 7 </advc.148>
		PlayerVoteTypes eBestTeam = PLAYER_VOTE_ABSTAIN;
		int iSecondBestVal = iBestValue; // advc.115b
		for (iI = 0; iI < MAX_TEAMS; iI++)
		{
			if(!GET_TEAM((TeamTypes)iI).isAlive() ||
					!g.isTeamVoteEligible((TeamTypes)iI, eVoteSource))
				continue; // advc
			if (kOurTeam.isVassal((TeamTypes)iI))
				return (PlayerVoteTypes)iI;

			int iValue = kOurTeam.AI_getAttitudeVal((TeamTypes)iI);
			/*  <advc.130v> Capitulated vassals don't just vote for
				their master, but also for civs that the master likes.
				(Human masters aren't going to like anyone.) */
			if(kOurTeam.isCapitulated())
			{
				iValue = GET_TEAM(kOurTeam.getMasterTeam()).
						AI_getAttitudeVal((TeamTypes)iI);
			} // </advc.130v>
			if(iValue > iSecondBestVal) // advc.115b
			{
				if (iValue > iBestValue)
				{
					iSecondBestVal = iBestValue; // advc.115b
					iBestValue = iValue;
					eBestTeam = (PlayerVoteTypes)iI;
				}
				else iSecondBestVal = iValue; // advc.115b
			}
		} // <advc.115b>
		if(iBestValue == iSecondBestVal || (eBestTeam != getMasterTeam() &&
				kOurTeam.AI_anyMemberAtVictoryStage4()))
			return PLAYER_VOTE_ABSTAIN; // </advc.115b>
		return eBestTeam;
	}
	else
	{
		TeamTypes eSecretaryGeneral = g.getSecretaryGeneral(eVoteSource);
		// BETTER_BTS_AI_MOD, Diplomacy AI, 12/30/08, jdog5000: START
		/*if (!bPropose) {
			if (eSecretaryGeneral != NO_TEAM) {
				if (eSecretaryGeneral == getTeam() ||(GET_TEAM(getTeam()).AI_getAttitude(eSecretaryGeneral) == ATTITUDE_FRIENDLY))
					return PLAYER_VOTE_YES;
			}
		}*/ // BtS
		// Remove blanket auto approval for friendly secretary
		bool bFriendlyToSecretary = false;
		// dlph.25: (The bRepeal code is really advc code)
		bool bRepeal = false;
		if (!bPropose)
		{	// dlph.25:
			bRepeal = (g.getVoteOutcome(eVote) == PLAYER_VOTE_YES);
			if (eSecretaryGeneral != NO_TEAM)
			{
				if (eSecretaryGeneral == getTeam()
					// <advc.130v>
					|| (GET_TEAM(getTeam()).isVassal(eSecretaryGeneral)
					&& GET_TEAM(getTeam()).isCapitulated())) // </advc.130v>
				{	// <dlph.25>
					if(bRepeal)
						return PLAYER_VOTE_NO;
					else // </dlph.25>
						return PLAYER_VOTE_YES;
				}
				else
				{
					bFriendlyToSecretary = (kOurTeam.AI_getAttitude(eSecretaryGeneral) == ATTITUDE_FRIENDLY);
				}
			}
		}
		// BETTER_BTS_AI_MOD: END

		bool bDefy = false;
		bool bValid = true;

		//if (bValid) // advc: Redundant; and some other style changes in the loop below.
		for (iI = 0; iI < GC.getNumCivicInfos(); iI++)
		{
			if (!GC.getInfo(eVote).isForceCivic(iI) || isCivic((CivicTypes)iI))
				continue;

			CivicTypes eBestCivic = AI_bestCivic((CivicOptionTypes)(GC.getInfo((CivicTypes)iI).getCivicOptionType()));
			if (eBestCivic == NO_CIVIC || eBestCivic == ((CivicTypes)iI))
				continue;

			int iBestCivicValue = AI_civicValue(eBestCivic);
			int iNewCivicValue = AI_civicValue((CivicTypes)iI);

			// BETTER_BTS_AI_MOD, Diplomacy AI, 12/30/08, jdog5000: START
			/*if (iBestCivicValue > ((iNewCivicValue * 120) / 100)) {
				bValid = false;
			if (iBestCivicValue > ((iNewCivicValue * (140 + (GC.getGame().getSorenRandNum(120, "AI Erratic Defiance (Force Civic)"))) / 100)))*/ // BtS
			// Increase threshold of voting for friend's proposal
			if (bFriendlyToSecretary)
			{
				iNewCivicValue *= 6;
				iNewCivicValue /= 5;
			}
			if (iBestCivicValue > (iNewCivicValue * 120) / 100)
			{
				bValid = false;
				// Increase odds of defiance, particularly on AggressiveAI
				if (iBestCivicValue > iNewCivicValue * (140 +
						g.getSorenRandNum(g.isOption(GAMEOPTION_AGGRESSIVE_AI) ?
						60 : 80, "AI Erratic Defiance (Force Civic)")) / 100)
			// BETTER_BTS_AI_MOD: END
					bDefy = true;
				break;
			}
		}

		if (bValid)
		{
			if (GC.getInfo(eVote).getTradeRoutes() > 0)
			{
				// BETTER_BTS_AI_MOD, Diplomacy AI, 12/30/08, jdog5000: START
				if (bFriendlyToSecretary)
				{	// <dlph.25>
					if(bRepeal)
						return PLAYER_VOTE_NO;
					else // </dlph.25>
						return PLAYER_VOTE_YES;
				} // BETTER_BTS_AI_MOD: END
				if (getNumCities() > (g.getNumCities() * 2) / (g.countCivPlayersAlive() + 1))
				{
					bValid = false;
				}
			}
		}

		if (bValid)
		{
			if (GC.getInfo(eVote).isNoNukes())
			{
				int iVoteBanThreshold = 0;
				iVoteBanThreshold += kOurTeam.getNukeInterception() / 3;
				iVoteBanThreshold += GC.getInfo(getPersonalityType()).getBuildUnitProb();
				iVoteBanThreshold *= std::max(1, GC.getInfo(getPersonalityType()).getWarmongerRespect());
				if (g.isOption(GAMEOPTION_AGGRESSIVE_AI))
				{
					iVoteBanThreshold *= 2;
				}

				bool bAnyHasSdi = false;
				for (iI = 0; iI < MAX_TEAMS; iI++)
				{
					if (GET_TEAM((TeamTypes)iI).isAlive() && iI != getTeam())
					{
						if (GET_TEAM((TeamTypes)iI).getNukeInterception() > 0)
						{
							bAnyHasSdi = true;
							break;
						}
					}
				}

				if (!bAnyHasSdi && kOurTeam.getNukeInterception() > 0 && kOurTeam.getNumNukeUnits() > 0)
				{
					iVoteBanThreshold *= 2;
				}

				// BETTER_BTS_AI_MOD, Diplomacy AI, 12/30/08, jdog5000: START
				if (bFriendlyToSecretary)
				{	// <dlph.25>
					if(bRepeal) {
						iVoteBanThreshold *= 3;
						iVoteBanThreshold /= 2;
					}
					else { // </dlph.25>
						iVoteBanThreshold *= 2;
						iVoteBanThreshold /= 3;
					}
				} // BETTER_BTS_AI_MOD: END

				bValid = (g.getSorenRandNum(100, "AI nuke ban vote") > iVoteBanThreshold);

				if (AI_isDoStrategy(AI_STRATEGY_OWABWNW))
				{
					bValid = false;
				}
				else if ((kOurTeam.getNumNukeUnits() / std::max(1, kOurTeam.getNumMembers())) < (g.countTotalNukeUnits() / std::max(1, g.countCivPlayersAlive())))
				{
					bValid = false;
				}
				if (!bValid && AI_getNumTrainAIUnits(UNITAI_ICBM) > 0)
				{
					if (g.getSorenRandNum(AI_isDoStrategy(AI_STRATEGY_OWABWNW) ? 2 : 3, "AI Erratic Defiance (No Nukes)") == 0)
					{
						bDefy = true;
					}
				}
			}
		}

		if (bValid)
		{
			if (GC.getInfo(eVote).isFreeTrade())
			{	// BETTER_BTS_AI_MOD, Diplomacy AI, 12/30/08, jdog5000: START
				if (bFriendlyToSecretary)
				{	// <dlph.25>
					if(bRepeal)
						return PLAYER_VOTE_NO;
					else // </dlph.25>
						return PLAYER_VOTE_YES;
				} // BETTER_BTS_AI_MOD: END

				int iOpenCount = 0;
				int iClosedCount = 0;
				for (iI = 0; iI < MAX_TEAMS; iI++)
				{
					if (!GET_TEAM((TeamTypes)iI).isAlive() || iI == getTeam())
						continue; // advc
					if (kOurTeam.isOpenBorders((TeamTypes)iI))
						iOpenCount += GET_TEAM((TeamTypes)iI).getNumCities();
					else iClosedCount += GET_TEAM((TeamTypes)iI).getNumCities();
				}
				if (iOpenCount >= (getNumCities() * getTradeRoutes()))
					bValid = false;
				if (iClosedCount == 0)
					bValid = false;
			}
		}

		if (bValid)
		{
			if (GC.getInfo(eVote).isOpenBorders())
			{	// BETTER_BTS_AI_MOD, Diplomacy AI, 12/30/08, jdog5000: START
				if (bFriendlyToSecretary)
				{	// <dlph.25>
					if(bRepeal)
						return PLAYER_VOTE_NO;
					else // </dlph.25>
						return PLAYER_VOTE_YES;
				} // BETTER_BTS_AI_MOD: END

				bValid = true;
				for (iI = 0; iI < MAX_CIV_TEAMS; iI++)
				{
					if (iI != getTeam())
					{
						if (GET_TEAM((TeamTypes)iI).isVotingMember(eVoteSource))
						{
							if (NO_DENIAL != kOurTeam.AI_openBordersTrade((TeamTypes)iI))
							{
								bValid = false;
								break;
							}
						}
					}
				}
			}
			else if (GC.getInfo(eVote).isDefensivePact())
			{	// BETTER_BTS_AI_MOD, Diplomacy AI, 12/30/08, jdog5000: START
				if (bFriendlyToSecretary)
				{	// <dlph.25>
					if(bRepeal)
						return PLAYER_VOTE_NO;
					else // </dlph.25>
						return PLAYER_VOTE_YES;
				} // BETTER_BTS_AI_MOD: END

				bValid = true;
				for (iI = 0; iI < MAX_CIV_TEAMS; iI++)
				{
					if (iI == getTeam())
						continue; // advc
					if (GET_TEAM((TeamTypes)iI).isVotingMember(eVoteSource))
					{
						if (NO_DENIAL != kOurTeam.AI_defensivePactTrade((TeamTypes)iI))
						{
							bValid = false;
							break;
						}
					}
				}
			}
			else if (GC.getInfo(eVote).isForcePeace())
			{
				FAssert(kVoteData.ePlayer != NO_PLAYER);
				TeamTypes ePeaceTeam = TEAMID(kVoteData.ePlayer);
				// advc (comment): These are named from ePeaceTeam's perspective
				int iWarsWinning = 0;
				int iWarsLosing = 0;
				int iChosenWar = 0;
// K-Mod version of end-war vote decision.
// Note: this code is based on BBAI code. I'm not sure which bits are from BBAI and which are from BtS.
// So I haven't marked individual changes or preserved the original code. The way "winning" and "losing" are calculated are mine.
/*  advc.104n (comment): UWAI can mostly do this better and, if enabled, will
	overwrite all the K-Mod results except bThisPlayerWinning. */
				bool bLosingBig = false;
				bool bWinningBig = false;
				bool bThisPlayerWinning = false;

				int iSuccessScale = GET_PLAYER(kVoteData.ePlayer).getNumMilitaryUnits() * GC.getDefineINT(CvGlobals::WAR_SUCCESS_ATTACKING) / 5;
				bool const bAggressiveAI = g.isOption(GAMEOPTION_AGGRESSIVE_AI);
				if (bAggressiveAI)
				{
					iSuccessScale *= 3;
					iSuccessScale /= 2;
				}

				// Is ePeaceTeam winning wars?
				int iPeaceTeamPower = GET_TEAM(ePeaceTeam).getPower(true);

				for (iI = 0; iI < MAX_CIV_TEAMS; iI++)
				{	// advc: War party other than ePeaceTeam; could be our team.
					CvTeamAI const& kOtherTeam = GET_TEAM((TeamTypes)iI);
					if (!kOtherTeam.isAlive() || kOtherTeam.getID() == ePeaceTeam)
						continue;
					if (kOtherTeam.isAVassal() || !kOtherTeam.isAtWar(ePeaceTeam))
						continue;

					int iPeaceTeamSuccess = GET_TEAM(ePeaceTeam).AI_getWarSuccess(kOtherTeam.getID());
					int iOtherTeamSuccess = kOtherTeam.AI_getWarSuccess(ePeaceTeam);
					int iOtherTeamPower = kOtherTeam.getPower(true);
					if (iPeaceTeamSuccess * iPeaceTeamPower > (iOtherTeamSuccess + iSuccessScale) * iOtherTeamPower)
					{	// Have to be ahead by at least a few victories to count as win
						iWarsWinning++;
						if (iPeaceTeamSuccess * iPeaceTeamPower / std::max(1, (iOtherTeamSuccess + 2*iSuccessScale) * iOtherTeamPower) >= 2)
							bWinningBig = true;
					}
					else if (iOtherTeamSuccess * iOtherTeamPower > (iPeaceTeamSuccess + iSuccessScale) * iPeaceTeamPower)
					{	// Have to have non-trivial loses
						iWarsLosing++;
						if (iOtherTeamSuccess * iOtherTeamPower / std::max(1, (iPeaceTeamSuccess + 2*iSuccessScale) * iPeaceTeamPower) >= 2)
							bLosingBig = true;
						if (kOtherTeam.getID() == getTeam())
							bThisPlayerWinning = true;
					}
					else if (GET_TEAM(ePeaceTeam).AI_getAtWarCounter(kOtherTeam.getID()) < 10)
					{	// Not winning, just recently attacked, and in multiple wars, be pessimistic
						// Counts ties from no actual battles
						if (GET_TEAM(ePeaceTeam).getNumWars() > 1 &&
								!GET_TEAM(ePeaceTeam).AI_isChosenWar(kOtherTeam.getID()))
							iWarsLosing++;
					}
					if (GET_TEAM(ePeaceTeam).AI_isChosenWar(kOtherTeam.getID()))
						iChosenWar++;
				} // <advc.104n>
				if(getUWAI.isEnabled())
				{	// Overwrite everything computed above except bThisPlayerWinning
					bLosingBig = bWinningBig = false;
					iWarsWinning = iWarsLosing = iChosenWar = 0;
				/*  iChosenWar: Shouldn't matter if chosen; trust the war utility computation
					-- except when we're considering to propose peace with ourselves.
					If we've just started a war, and want to continue that war,
					the proposal can still be advantageous when it allows us to end
					some other war. In this case, we should not propose peace with
					ourselves, but peace with that other team that we don't want to
					be at war with.
					The proper solution would be to pick the best proposal instead of
					a random "valid" one. This is just a temporary fix: */
					if(getTeam() == ePeaceTeam && bPropose)
					{
						for(int i = 0; i < MAX_CIV_TEAMS; i++)
						{
							CvTeamAI const& kEnemy = GET_TEAM((TeamTypes)i);
							if(kEnemy.isAlive() && kEnemy.isAtWar(ePeaceTeam) &&
									GET_TEAM(ePeaceTeam).AI_getAtWarCounter(kEnemy.getID()) < 5 &&
									!kEnemy.AI_isChosenWar(ePeaceTeam))
								return PLAYER_VOTE_NO;
						}
					}
					int u = -GET_TEAM(ePeaceTeam).uwai().uEndAllWars(eVoteSource);
					if(u < -10)
						iWarsLosing = 1;
					else if(u > 10)
						iWarsWinning = 1;
					if(u > 75)
						bWinningBig = true;
					else if(u < -75)
						bLosingBig = true;
				} // </advc.104n>

				if (ePeaceTeam == getTeam())
				{
					int iPeaceRand = GC.getInfo(getPersonalityType()).getBasePeaceWeight();
					// Note, base peace weight ranges between 0 and 10.
					iPeaceRand /= (bAggressiveAI ? 2 : 1);
					iPeaceRand /= (AI_atVictoryStage(AI_VICTORY_CONQUEST2) ? 2 : 1);
					// <advc.104n>
					double prPeace = 0;
					if(iPeaceRand > 0)
						prPeace = 1 - (1.0 / std::max(1, iPeaceRand));
					// </advc.104n>
					if(bLosingBig &&
							//(GC.getGame().getSorenRandNum(iPeaceRand, "AI Force Peace to avoid loss") || bPropose))
							/*  advc.104n: The BtS code can have us randomly vote
								against our own proposal. */
							::hash(g.getGameTurn(), getID()) < prPeace)
						bValid = true; // Non-warmongers want peace to escape loss
					//else if (!bLosingBig && (iChosenWar > iWarsLosing))
					else if (!bLosingBig && (iChosenWar > iWarsLosing ||
							(AI_atVictoryStage(AI_VICTORY_CONQUEST3) // K-Mod
							// advc.104n: Military victory is covered by war utility
							&& !getUWAI.isEnabled())))
						bValid = false; // If chosen to be in most wars, keep it going
					else bValid = (iWarsLosing > iWarsWinning); // If losing most wars, vote for peace

					if (!bValid && !bLosingBig && bWinningBig)
					{
						// Can we continue this war with defiance penalties?
						if (!AI_isFinancialTrouble())
						{
							if (!g.getSorenRandNum(iPeaceRand, "AI defy Force Peace!"))
								bDefy = true;
						}
					}
				}
				else if (eSecretaryGeneral == getTeam() && !bPropose)
					bValid = true;
				else if (GET_TEAM(ePeaceTeam).isAtWar(getTeam()))
				{
					bool bWeWantToEndWar = ((kOurTeam.AI_endWarVal(ePeaceTeam) >
							(3*GET_TEAM(ePeaceTeam).AI_endWarVal(getTeam()))/2));
					/*  <advc.118> If we don't want to negotiate, then apparently
						we don't want to end the war */
					if(!AI_isWillingToTalk(GET_TEAM(ePeaceTeam).getLeaderID()))
						bWeWantToEndWar = false; // </advc.118>

					bValid = bWeWantToEndWar;
					if (bValid)
					{
						bValid = bWinningBig || (iWarsWinning > iWarsLosing) ||
								(kOurTeam.getNumWars(true, true) > 1);
					}

					if (!getUWAI.isEnabled()) // advc.104n
					{
						if(!bValid && bThisPlayerWinning &&
							(iWarsLosing >= iWarsWinning) && !bPropose && !isAVassal())
						{
							if (kOurTeam.getNumWars(true) == 1 || bLosingBig)
							{
								// Can we continue this war with defiance penalties?
								if (!AI_isFinancialTrouble())
								{
									int iDefyRand = GC.getInfo(getPersonalityType()).getBasePeaceWeight();
									iDefyRand /= (bAggressiveAI ? 2 : 1);
									if (g.getSorenRandNum(iDefyRand, "AI Erratic Defiance (Force Peace)") == 0)
										bDefy = true;
								}
							}
						}
						// <advc.104n>
					}
					else
					{
						int u = -GET_TEAM(getTeam()).uwai().uEndWar(ePeaceTeam);
						bValid = (u < 0);
						CvCity* pCapital = getCapitalCity();
						bDefy = false;
						if(pCapital != NULL)
						{
							double cityRatio = 1;
							// Based on CvPlayer::setDefiedResolution
							ReligionTypes eReligion = g.getVoteSourceReligion(eVoteSource);
							if(eReligion != NO_RELIGION)
							{
								cityRatio = getHasReligionCount(eReligion) /
										(double)getNumCities();
							}
							/*  NB: Low city ratio doesn't just mean that defying
								won't hurt much - also makes it unlikely that our
								side will win the vote. */
							CvLeaderHeadInfo const& lh = GC.getInfo(getPersonalityType());
							int iDefyCost = ::round(5 * (12 +
									pCapital->getDefyResolutionAngerTimer() +
									lh.getBasePeaceWeight() / 2 +
									lh.getDiplomacyVictoryWeight() / 10) * cityRatio);
							if(AI_isFinancialTrouble())
								iDefyCost *= 2;
							double const prDefy = (u - iDefyCost) / 45.0;
							if(::bernoulliSuccess(prDefy, "advc.104n"))
								bDefy = true;
						}
					} // </advc.104n>

					if (!bValid && !bDefy && !bPropose)
					{
						if((kOurTeam.AI_getAttitude(eSecretaryGeneral) > GC.getInfo(getPersonalityType()).getVassalRefuseAttitudeThreshold()))
						{
							// Influence by secretary
							if (NO_DENIAL == kOurTeam.AI_makePeaceTrade(ePeaceTeam, eSecretaryGeneral))
							{
								bValid = true;
							}
							else if (eSecretaryGeneral != NO_TEAM && kOurTeam.isVassal(eSecretaryGeneral))
							{
								bValid = true;
							}
						}
					}
				}
				else
				{
					if (kOurTeam.AI_getWarPlan(ePeaceTeam) != NO_WARPLAN)
					{
						// Keep planned enemy occupied
						bValid = false;
					}
					else if (kOurTeam.AI_shareWar(ePeaceTeam) && !kOurTeam.isVassal(ePeaceTeam))
					{
						// Keep ePeaceTeam at war with our common enemies
						bValid = false;
					}
					else if(iWarsLosing > iWarsWinning)
					{
						// Feel pity for team that is losing (if like them enough to not declare war on them)
						bValid = (kOurTeam.AI_getAttitude(ePeaceTeam)
								> /* advc.118, advc.001: Was ">=", but that's not how
									 DeclareWarThemRefuseAttitudeThreshold works */
								GC.getInfo(getPersonalityType()).getDeclareWarThemRefuseAttitudeThreshold());
					}
					else
						if(iWarsLosing < iWarsWinning) // advc.118
					{
						// Stop a team that is winning (if don't like them enough to join them in war)
						bValid = (kOurTeam.AI_getAttitude(ePeaceTeam) < GC.getInfo(getPersonalityType()).getDeclareWarRefuseAttitudeThreshold());
					}

					/*  <advc.118> Also vote for peace if neither side is winning,
						and we like the peace team and all its adversaries.
						Need to like them so much that we couldn't be brought
						to declare war on them. */
					else
					{
						AttitudeTypes iDWTR = (AttitudeTypes)GC.getInfo(
								getPersonalityType()).getDeclareWarThemRefuseAttitudeThreshold();
						if(!bValid && kOurTeam.AI_getAttitude(ePeaceTeam) > iDWTR) {
							bValid = true;
							for(int i = 0; i < MAX_CIV_TEAMS; i++)
							{
								TeamTypes tt = (TeamTypes)i; CvTeam& t = GET_TEAM(tt);
								if(t.isAlive() && tt != ePeaceTeam &&
										!t.isAVassal() && t.isAtWar(ePeaceTeam) &&
										kOurTeam.AI_getAttitude(tt) <= iDWTR)
								{
									bValid = false;
									break;
								}
							}
						}
					} // </advc.118>

					if (!bValid)
					{
						if (bFriendlyToSecretary && !kOurTeam.isVassal(ePeaceTeam))
						{
							// Influence by secretary
							bValid = true;
						}
					}
				}
// K-Mod end
			}
			else if (GC.getInfo(eVote).isForceNoTrade())
			{
				FAssert(kVoteData.ePlayer != NO_PLAYER);
				TeamTypes eEmbargoTeam = GET_PLAYER(kVoteData.ePlayer).getTeam();
				// <advc.130f> Try to honor commitments
				if(isAnyDealTooRecentToCancel(eEmbargoTeam))
					return PLAYER_VOTE_NO; // </advc.130f>
				if (eSecretaryGeneral == getTeam() && !bPropose)
				{
					bValid = true;
				}
				else if (eEmbargoTeam == getTeam())
				{
					bValid = false;
					if (!isNoForeignTrade())
					{
						bDefy = true;
					}
				}
				// BETTER_BTS_AI_MOD, Diplomacy AI, 12/30/08, jdog5000: START
				else
				{
					if (bFriendlyToSecretary)
					{
						return PLAYER_VOTE_YES;
					}
					else if (canStopTradingWithTeam(eEmbargoTeam))
					{
						bValid = (NO_DENIAL == AI_stopTradingTrade(eEmbargoTeam, kVoteData.ePlayer));
						if (bValid)
						{
							bValid = (kOurTeam.AI_getAttitude(eEmbargoTeam) <= ATTITUDE_CAUTIOUS);
						}
					}
					else
					{
						bValid = (kOurTeam.AI_getAttitude(eEmbargoTeam) < ATTITUDE_CAUTIOUS);
					}
				} // BETTER_BTS_AI_MOD: END
			}
			else if (GC.getInfo(eVote).isForceWar())
			{
				FAssert(kVoteData.ePlayer != NO_PLAYER);
				TeamTypes eWarTeam = GET_PLAYER(kVoteData.ePlayer).getTeam();

				if (eSecretaryGeneral == getTeam() && !bPropose)
				{
					bValid = true;
				}
				// BETTER_BTS_AI_MOD, Diplomacy AI, 12/30/08, jdog5000: START
				/*else if (eWarTeam == getTeam())
					bValid = false;
				else if (GET_TEAM(eWarTeam).isAtWar(getTeam()))*/ // BtS
				else if (eWarTeam == getTeam() || kOurTeam.isVassal(eWarTeam))
				{
					// Explicit rejection by all who will definitely be attacked
					bValid = false;
				} // <dlph.25/advc> Try to honor peace treaty
				else if(kOurTeam.isForcePeace(eWarTeam))
					bValid = false; // </dlph.25/advc>
				else if (kOurTeam.AI_getWarPlan(eWarTeam) != NO_WARPLAN)
				{	// BETTER_BTS_AI_MOD: END
					bValid = true;
				}
				else
				{	// BETTER_BTS_AI_MOD, Diplomacy AI, 07/20/09, jdog5000: START
					/*bValid = (bPropose || NO_DENIAL == GET_TEAM(getTeam()).AI_declareWarTrade(eWarTeam, eSecretaryGeneral));
					if (bValid)
						bValid = (GET_TEAM(getTeam()).AI_getAttitude(eWarTeam) < ATTITUDE_CAUTIOUS);*/ // BtS
					if (!bPropose && isAVassal())
					{
						// Vassals always deny war trade requests and thus previously always voted no
						bValid = false;

						if (!kOurTeam.AI_isAnyWarPlan())
						{
							if (eSecretaryGeneral == NO_TEAM || kOurTeam.AI_getAttitude(eSecretaryGeneral) > GC.getInfo(getPersonalityType()).getDeclareWarRefuseAttitudeThreshold())
							{
								if (eSecretaryGeneral != NO_TEAM && kOurTeam.isVassal(eSecretaryGeneral))
								{
									bValid = true;
								}
								//else if ((isAVassal() ? GET_TEAM(getTeam()).getCurrentMasterPower(true) : GET_TEAM(getTeam()).getPower(true)) > GET_TEAM(eWarTeam).getDefensivePower())
								else if (GET_TEAM(kOurTeam.getMasterTeam()).getPower(true) > GET_TEAM(eWarTeam).getDefensivePower())
								{
									bValid = true;
								}
							}
						}
					}
					else
					{
						bValid = (bPropose || NO_DENIAL == kOurTeam.AI_declareWarTrade(eWarTeam, eSecretaryGeneral));
					}

					if (bValid || /* advc.104n: */ getUWAI.isEnabled())
					{
						if(!getUWAI.isEnabled()) // advc.104n
						{
							// advc.104y:
							int iNoWarOdds = kOurTeam.AI_noWarProbAdjusted(eWarTeam);
							bValid = ((iNoWarOdds < 30) || (g.getSorenRandNum(100, "AI War Vote Attitude Check (Force War)") > iNoWarOdds));
						// <advc.104n>
						}
						if(getUWAI.isEnabled())
						{
							int u = GET_TEAM(getTeam()).uwai().uJointWar(eWarTeam, eVoteSource);
							bValid = (bValid && u > 0);
							bDefy = false;
							/*  The war will be trouble too, but checking fin.
								trouble is a safeguard against defying repeatedly. */
							if(!AI_isFinancialTrouble())
							{
								double pr = (-u - 125 + 5 * GC.getInfo(getPersonalityType()).
										getBasePeaceWeight()) / 100.0;
								if(::bernoulliSuccess(pr, "advc.104n"))
									bDefy = true;
							}
						} // </advc.104n>
					}
					/*else{ // Consider defying resolution
						if (!isAVassal()) {
							if (eSecretaryGeneral == NO_TEAM || GET_TEAM(getTeam()).AI_getAttitude(eWarTeam) > GET_TEAM(getTeam()).AI_getAttitude(eSecretaryGeneral)) {
								if(GET_TEAM(getTeam()).AI_getAttitude(eWarTeam) > GC.getInfo(getPersonalityType()).getDefensivePactRefuseAttitudeThreshold()) {
									int iDefyRand = GC.getInfo(getPersonalityType()).getBasePeaceWeight();
									iDefyRand /= (GC.getGame().isOption(GAMEOPTION_AGGRESSIVE_AI) ? 2 : 1);
									if (GC.getGame().getSorenRandNum(iDefyRand, "AI Erratic Defiance (Force War)") > 0)
										bDefy = true;
					} } } }*/
					// BETTER_BTS_AI_MOD: END
				}
			}
			else if (GC.getInfo(eVote).isAssignCity())
			{
				bValid = false;
				FAssert(kVoteData.ePlayer != NO_PLAYER);
				CvPlayer& kPlayer = GET_PLAYER(kVoteData.ePlayer);
				CvCity* pCity = kPlayer.getCity(kVoteData.iCityId);
				if (NULL != pCity)
				{
					if (NO_PLAYER != kVoteData.eOtherPlayer && kVoteData.eOtherPlayer != pCity->getOwner())
					{
						// BETTER_BTS_AI_MOD, Diplomacy AI, 10/03/09, jdog5000: START
						if (!bPropose && eSecretaryGeneral == getTeam() || GET_PLAYER(kVoteData.eOtherPlayer).getTeam() == getTeam())
						{
							bValid = true;
						}
						else if (kPlayer.getTeam() == getTeam())
						{
							bValid = false;
							// BBAI TODO: Wonders, holy city, aggressive AI?
							// advc.118:
							if ((kPlayer.getNumCities() <= 1 && kPlayer.getID() == getID()) ||
									g.getSorenRandNum(3, "AI Erratic Defiance (Assign City)") == 0)
								bDefy = true;
						}
						else
						{
							bValid = (AI_getAttitude(kVoteData.ePlayer) < AI_getAttitude(kVoteData.eOtherPlayer));
						}
						// BETTER_BTS_AI_MOD: END
					}
				}
			}
		}
		//if (bDefy && canDefyResolution(eVoteSource, kVoteData))
		// BETTER_BTS_AI_MOD, Diplomacy AI, 12/30/08, jdog5000: Don't defy resolutions from friends
		if (bDefy && !bFriendlyToSecretary && canDefyResolution(eVoteSource, kVoteData))
		{
			return PLAYER_VOTE_NEVER;
		}

		return (bValid ? PLAYER_VOTE_YES : PLAYER_VOTE_NO);
	}

}

int CvPlayerAI::AI_dealVal(PlayerTypes eFromPlayer, CLinkList<TradeData> const& kList, bool bIgnoreAnnual, int iChange,  // advc: Take the list as a reference; some style changes
	bool bIgnoreDiscount, // advc.550a
	bool bIgnorePeace) const // advc.130p
{
	FAssertMsg(eFromPlayer != getID(), "shouldn't call this function on ourselves");

	int iValue = 0;
	TeamTypes eFromTeam = TEAMID(eFromPlayer);
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
	if(/* <advc.130p> */ !bIgnorePeace && /* </advc.130p> */ atWar(getTeam(), eFromTeam))
	{	// <advc.104i>
		if(getUWAI.isEnabled())
		{
			if(isHuman() || GET_PLAYER(eFromPlayer).isHuman())
				iValue += kOurTeam.uwai().endWarVal(eFromTeam);
			else FAssert(false); // AI-AI peace is handled only by UWAI
		}
		else // </advc.104>
			iValue += kOurTeam.AI_endWarVal(eFromTeam);
	}

	// advc.104o: Can't be hired for more than one war at a time
	int iWars = 0;
	// <advc.036>
	int iHealthBonuses = 0;
	int iHappyBonuses = 0; // </advc.036>
	for (CLLNode<TradeData> const* pNode = kList.head(); pNode != NULL; pNode = kList.next(pNode))
	{
		FAssert(!pNode->m_data.m_bHidden);
		/*  <advc.130p> Replacing the checks in the individual cases. Previously,
			OB and DP were not counted as annual, but GPT and resources were;
			now it's the other way around. */
		TradeableItems eItemType = pNode->m_data.m_eItemType;
		if((bIgnoreAnnual && CvDeal::isAnnual(eItemType) && eItemType != TRADE_GOLD_PER_TURN &&
				eItemType != TRADE_RESOURCES) || (bIgnorePeace && (eItemType == TRADE_PEACE ||
				eItemType == TRADE_PEACE_TREATY)))
			continue;
		switch(eItemType) // </advc.130p>
		{
		case TRADE_TECHNOLOGIES:
			iValue += kOurTeam.AI_techTradeVal((TechTypes)pNode->m_data.m_iData, eFromTeam,
					bIgnoreDiscount); // advc.550a
			break;
		case TRADE_RESOURCES:
		{	// <advc.036>
			BonusTypes eBonus = (BonusTypes)pNode->m_data.m_iData;
			CvBonusInfo const& kBonus = GC.getInfo(eBonus);
			if(kBonus.getHappiness() > 0)
				iHappyBonuses += iChange;
			if(kBonus.getHealth() > 0)
				iHealthBonuses += iChange; // </advc.036>
			iValue += AI_bonusTradeVal(eBonus, eFromPlayer, iChange,
					// <advc.036>
					(std::abs(iHappyBonuses) > 1 && kBonus.getHappiness() > 0) ||
					(std::abs(iHealthBonuses) > 1 && kBonus.getHealth() > 0));
					// </advc.036>
			break;
		}
		case TRADE_CITIES:
		{
			CvCityAI* pCity = GET_PLAYER(eFromPlayer).AI_getCity(pNode->m_data.m_iData);
			if (pCity != NULL)
				iValue += AI_cityTradeVal(*pCity, /* advc.ctr: */ getID());
			break;
		}
		case TRADE_GOLD:
			iValue += (pNode->m_data.m_iData * AI_goldTradeValuePercent()) / 100;
			break;
		case TRADE_GOLD_PER_TURN:
			iValue += AI_goldPerTurnTradeVal(pNode->m_data.m_iData);
			break;
		case TRADE_MAPS:
			iValue += kOurTeam.AI_mapTradeVal(eFromTeam);
			break;
		case TRADE_SURRENDER:
			iValue += kOurTeam.AI_surrenderTradeVal(eFromTeam);
			break;
		case TRADE_VASSAL:
			iValue += kOurTeam.AI_vassalTradeVal(eFromTeam);
			break;
		case TRADE_OPEN_BORDERS:
			iValue += kOurTeam.AI_openBordersTradeVal(eFromTeam);
			break;
		case TRADE_DEFENSIVE_PACT:
			iValue += kOurTeam.AI_defensivePactTradeVal(eFromTeam);
			break;
		case TRADE_PEACE:
			iValue += kOurTeam.AI_makePeaceTradeVal((TeamTypes)pNode->m_data.m_iData, eFromTeam);
			break;
		case TRADE_WAR:
			// <advc.104o>
			if(getUWAI.isEnabled())
			{
				iWars++;
				if(iWars > 1)
					iValue += 1000000; // More than they can pay
			} // </advc.104o>
			iValue += kOurTeam.AI_declareWarTradeVal((TeamTypes)pNode->m_data.m_iData, eFromTeam);
			break;
		case TRADE_EMBARGO:
			iValue += AI_stopTradingTradeVal((TeamTypes)pNode->m_data.m_iData, eFromPlayer);
			break;
		case TRADE_CIVIC:
			iValue += AI_civicTradeVal((CivicTypes)pNode->m_data.m_iData, eFromPlayer);
			break;
		case TRADE_RELIGION:
			iValue += AI_religionTradeVal((ReligionTypes)pNode->m_data.m_iData, eFromPlayer);
			break;
		}
	}

	return iValue;
}


bool CvPlayerAI::AI_goldDeal(CLinkList<TradeData> const& kList) const // advc: Take the list as a reference
{
	for (CLLNode<TradeData> const* pNode = kList.head(); pNode != NULL; pNode = kList.next(pNode))
	{
		FAssert(!pNode->m_data.m_bHidden);
		switch (pNode->m_data.m_eItemType)
		{
		case TRADE_GOLD:
		case TRADE_GOLD_PER_TURN:
			return true;
			break;
		}
	}

	return false;
}

// <advc.705>
bool CvPlayerAI::isAnnualDeal(CLinkList<TradeData> const& itemList) const
{
	for(CLLNode<TradeData> const* pNode = itemList.head(); pNode != NULL; pNode = itemList.next(pNode))
	{
		if(!CvDeal::isAnnual(pNode->m_data.m_eItemType))
			return false;
	}
	return true;
} // </advc.705>

/// \brief AI decision making on a proposal it is given
///
/// In this function the AI considers whether or not to accept another player's proposal.
/// This is used when considering proposals from the human player made in the
/// diplomacy window as well as a couple other places.
bool CvPlayerAI::AI_considerOffer(PlayerTypes ePlayer,
	// advc: Renamed, turned into references (b/c caller ensures non-NULL). Also some other style changes in the body.
	CLinkList<TradeData> const& kTheyGive, CLinkList<TradeData> const& kWeGive,
	int iChange, /* advc.133: */ int iDealAge,
	bool bHypothetical) // advc.130o
{
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam()); // K-Mod
	CvPlayerAI const& kPlayer = GET_PLAYER(ePlayer);
	bool bHuman = kPlayer.isHuman();
	FAssertMsg(ePlayer != getID(), "shouldn't call this function on ourselves");

	bool const bOurGoldDeal = AI_goldDeal(kWeGive);
	if (AI_goldDeal(kTheyGive) && bOurGoldDeal)
		return false;

	if (iChange > -1)
	{
		for (CLLNode<TradeData> const* pNode = kWeGive.head(); pNode; pNode = kWeGive.next(pNode))
		{
			if (getTradeDenial(ePlayer, pNode->m_data) != NO_DENIAL)
				return false;
		} /* <advc.036> Denial from the human pov should prevent resource trades.
			 This matters for "Care to negotiate". */
		if(bHuman)
		{
			for (CLLNode<TradeData> const* pNode = kTheyGive.head(); pNode != NULL;
					pNode = kTheyGive.next(pNode))
			{
				TradeData tdata = pNode->m_data;
				if(tdata.m_eItemType == TRADE_RESOURCES &&
						kPlayer.getTradeDenial(getID(), tdata) != NO_DENIAL)
					return false;
			}
		} // </advc.036>
		// <advc.026>
		else if(bOurGoldDeal && !AI_checkMaxGold(kWeGive, ePlayer))
			return false; // </advc.026>
	}
	/*if (kPlayer.getTeam() == getTeam())
		return true;*/
	// advc.155: Replacing the above
	bool bSameTeam = (getTeam() == kPlayer.getTeam());
	// K-Mod. Refuse all war-time offers unless it's part of a peace deal
	if (::atWar(getTeam(), kPlayer.getTeam()))
	{
		/*  <advc> Could treat Cease Fire here. The K-Mod code below always
			refuses. */
		/*if(kTheyGive.getLength() + kWeGive.getLength() <= 0)
			;*/ // </advc>
		bool bEndWar = false;
		CLLNode<TradeData> const* pNode;
		for (pNode = kTheyGive.head(); !bEndWar && pNode; pNode = kTheyGive.next(pNode))
		{
			if (CvDeal::isEndWar(pNode->m_data.m_eItemType))
				bEndWar = true;
		}
		for (pNode = kWeGive.head(); !bEndWar && pNode; pNode = kWeGive.next(pNode))
		{
			if (CvDeal::isEndWar(pNode->m_data.m_eItemType))
				bEndWar = true;
		}
		if (!bEndWar)
			return false;
	} // K-Mod end

	// BETTER_BTS_AI_MOD, Diplomacy AI, 10/23/09, jdog5000: START
	// Don't always accept giving deals, TRADE_VASSAL and TRADE_SURRENDER come with strings attached
	bool bVassalTrade = false;
	if (iChange > -1) // K-Mod
	{
		for (CLLNode<TradeData> const* pNode = kTheyGive.head(); pNode != NULL;
			pNode = kTheyGive.next(pNode))
		{
			if (pNode->m_data.m_eItemType == TRADE_VASSAL)
			{
				bVassalTrade = true;
				for (int iTeam = 0; iTeam < MAX_CIV_TEAMS; iTeam++)
				{
					if (GET_TEAM((TeamTypes)iTeam).isAlive())
					{
						if (iTeam != getTeam() && iTeam != kPlayer.getTeam() &&
							::atWar(kPlayer.getTeam(), (TeamTypes)iTeam) &&
							!::atWar(getTeam(), (TeamTypes)iTeam))
						{
							if (kOurTeam.AI_declareWarTrade((TeamTypes)iTeam, kPlayer.getTeam(), false) != NO_DENIAL)
							{
								return false;
							}
						}
					}
				}
			}
			else if (pNode->m_data.m_eItemType == TRADE_SURRENDER)
			{
				bVassalTrade = true;
				if (!kOurTeam.AI_acceptSurrender(kPlayer.getTeam()))
					return false;
			}
		}
	}
	// K-Mod. This is our opportunity for canceling a vassal deal.
	else
	{
		for (CLLNode<TradeData> const* pNode = kWeGive.head(); pNode != NULL;
			pNode = kWeGive.next(pNode))
		{
			if (pNode->m_data.m_eItemType == TRADE_VASSAL || pNode->m_data.m_eItemType == TRADE_SURRENDER)
			{
				bVassalTrade = true;

				// The trade denial calculation for vassal trades is actually a bit nuanced.
				// Rather than trying to restructure it, or write new code and risk inconsistencies, I'm just going use it.
				if (kOurTeam.AI_surrenderTrade(kPlayer.getTeam(), pNode->m_data.m_eItemType == TRADE_SURRENDER ? 125
						/*  advc.112, advc.143: Changed iPowerMultiplier from 75
							to 100, which is the default. Multiplier for cancellation
							now set inside AI_surrenderTrade. */
						: 100) != NO_DENIAL)
					return false;
				// note: AI_vassalTrade calls AI_surrenderTrade after doing a bunch of war checks and so on. So we don't need that.
				// CvPlayer::getTradeDenial, unfortunately, will reject any vassal deal by an AI player on a human team - we don't want that here.
				// (regarding the power multiplier, cf. values used in getTradeDenial)
			}
		}
	}
	// K-Mod end
	// <advc.705>
	CvGame& g = GC.getGame();
	bool bPossibleCollusion = g.isOption(GAMEOPTION_RISE_FALL) &&
			bHuman && g.getRiseFall().
			isCooperationRestricted(getID()); // </advc.705>
	if (!bVassalTrade)
	{
		if (kWeGive.getLength() == 0 && kTheyGive.getLength() > 0)
		{	// <advc.705>
			if(bPossibleCollusion && !g.getRiseFall().isSquareDeal(kWeGive, kTheyGive, getID()))
			{
				g.getRiseFall().substituteDiploText(true);
				return false;
			}
			else // </advc.705>
				return true;
		}
	}
	// BETTER_BTS_AI_MOD: END
	// K-Mod
	if (iChange < 0)
	{
		// There are only a few cases where the AI will allow ongoing trade with its worst enemy...
		if (kOurTeam.AI_getWorstEnemy() == kPlayer.getTeam() && !kOurTeam.isVassal(kPlayer.getTeam()))
		{
			if (kWeGive.getLength() > 1 || kWeGive.head()->m_data.m_eItemType != TRADE_GOLD_PER_TURN)
			{
				// this trade isn't one of the special cases.
				return false;
			}
		}
	}
	// K-Mod end

	/*  <advc.132> Don't agree to accept two civics of the same column.
		Would be better to rule this out through denial (e.g. "must be joking"),
		but the trade screen isn't mod-able. */
	if(!AI_checkCivicReligionConsistency(kWeGive) ||
			!AI_checkCivicReligionConsistency(kTheyGive))
		return false; // </advc.132>
	/*  advc: Renamed. Was called iOurValue. (Which, elsewhere in this class,
		is the value that we assign to what they _give_.) */
	int iTheyReceive = kPlayer.AI_dealVal(getID(), kWeGive, false, iChange);
	// <advc.705>
	int iPessimisticVal = (bPossibleCollusion ? g.getRiseFall().pessimisticDealVal(
			getID(), iTheyReceive, kWeGive) : -1); // </advc.705>
	int iWeReceive = AI_dealVal(ePlayer, kTheyGive, false, iChange);
	int iThreshold = -1; // advc.155: Declaration moved up
	if (iTheyReceive > 0 && kTheyGive.getLength() == 0 && iWeReceive == 0)
	{	// <advc.130v> Vassal mustn't force a peace treaty on its master
		if(kOurTeam.isAVassal() && !kOurTeam.isVassal(TEAMID(ePlayer)))
			return false; // </advc.130v>
		// K-Mod
		// Don't give any gifts to civs that you are about to go to war with.
		if (kOurTeam.AI_getWarPlan(kPlayer.getTeam()) != NO_WARPLAN)
			return false;
		// Don't cancel gift deals to vassals that you like, unless you need the gift back.
		if (iChange < 0 && GET_TEAM(ePlayer).isVassal(getTeam()))
		{
			/*if (iOurValue > AI_dealVal(ePlayer, pOurList, false, 1))
				return true;
			else return false; */
			// Simply comparing deal values doesn't work because the value from voluntary vassals gets halved.
			// Will have to use a kludge:
			if (kWeGive.getLength() == 1 && kWeGive.head()->m_data.m_eItemType == TRADE_RESOURCES)
			{
				BonusTypes eBonus = (BonusTypes)kWeGive.head()->m_data.m_iData;
				if (kPlayer.AI_bonusVal(eBonus, -1) > AI_bonusVal(eBonus, 1))
					return true;
				else
					return false;
			}
		}
		// K-Mod end

		/*  <advc.133> isVassalTributeDeal can't distinguish between freebies from
			a granted help request and those from vassal tribute. That's fine,
			cancel gifts too. */
		if (iChange <= 0 && kOurTeam.getMasterTeam() != TEAMID(ePlayer) &&
				CvDeal::isVassalTributeDeal(&kWeGive))
			return false; // </advc.133>
		// <advc.130o>
		bool bDemand = false;
		bool bAccept = true;
		bool bVassal = kOurTeam.isVassal(kPlayer.getTeam()); // </advc.130o>
		if(bVassal && CvDeal::isVassalTributeDeal(&kWeGive))
		{
			if (AI_getAttitude(ePlayer, false) <= GC.getInfo(getPersonalityType()).getVassalRefuseAttitudeThreshold()
				//&& kOurTeam.getAtWarCount(true) == 0
				&& !AI_isFocusWar() // advc.105: Replacing the above
				&& GET_TEAM(ePlayer).getDefensivePactCount() == 0)
			{
				iTheyReceive *= (kOurTeam.getPower(false) + 10);
				iTheyReceive /= (GET_TEAM(ePlayer).getPower(false) + 10);
			}
			else return true;
		}
		else // <advc.130o>
		{
			// Moved up
			int iMemory = AI_getMemoryCount(ePlayer, MEMORY_MADE_DEMAND_RECENT);
			if (iMemory > 0)
				return false;
			/*  Was hard-coded, but the XML value is CAUTIOUS for all leaders,
				so no functional change. */
			AttitudeTypes eNoHelpThresh = (AttitudeTypes)GC.getInfo(
					getPersonalityType()).getNoGiveHelpAttitudeThreshold();
			bDemand = (AI_getAttitude(ePlayer) <= eNoHelpThresh);
			if (bDemand) // </advc.130o>
			{
				FAssert(bHuman);
				if (kOurTeam.getPower(false) > (GET_TEAM(ePlayer).getPower(false) * 4) / 3)
				{
					// advc.130o: Don't return just yet
					bAccept = false;
				}
			} // <advc.144>
			else // <advc.130v>
			{
				if (AI_getAttitude(ePlayer, false) <= eNoHelpThresh)
					return false; // </advc.130v>
				if (!bSameTeam && // advc.155
						!bVassal && // advc.130v
						::hash(GC.getGame().getGameTurn(), getID()) <
						AI_prDenyHelp())
					return false; // </advc.144>
			}
			// <advc.104m>
			if (bAccept && bDemand && getUWAI.isEnabled() && /* advc.155: */ !bSameTeam)
				bAccept = uwai().considerDemand(ePlayer, iTheyReceive);
			// </advc.104m>
		}
		// advc.130o: Do this only if UWAI hasn't already handled the offer
		if (!bDemand || (!getUWAI.isEnabled() && bAccept))
		{
			iThreshold = kOurTeam.AI_getHasMetCounter(kPlayer.getTeam()) + 50;
			iThreshold *= 2;
			// <advc.155>
			if (bSameTeam)
				iThreshold *= 5; // </advc.155>
			else if (!bVassal) // advc.130v
			{
				if (GET_TEAM(ePlayer).AI_isLandTarget(getTeam()))
					iThreshold *= 3;
				iThreshold *= (GET_TEAM(ePlayer).getPower(false) + 100);
				iThreshold /= (kOurTeam.getPower(false) + 100);
			} // <advc.144>
			int iRandExtra = ::round((::hash(g.getGameTurn(), getID()) - 0.5) *
					iThreshold * (bVassal ? 0.1 : 0.2));
			iThreshold += iRandExtra; // </advc.144>
			iThreshold -= kPlayer.AI_getPeacetimeGrantValue(getID());
			bAccept = (iTheyReceive < iThreshold); // advc.130o: Don't return yet
			// <advc.144>
			if (bAccept && !bDemand && /* advc.130v: */ !bVassal &&
					getUWAI.isEnabled())
				bAccept = uwai().considerGiftRequest(ePlayer, iTheyReceive);
			// </advc.144>
		} // <advc.130o>
		if (!bHypothetical)
		{
			if (bDemand)
			{
				/*  Moved here from CvPlayer::handleDiploEvent b/c that handler
					can't distinguish whether the AI accepts a human demand */
				if (bAccept)
				{
					/*  ePlayer is human; could still let the proxy AI remember,
						but that gets confusing in R&F games when a human demands
						tribute from a civ and later takes control of that civ.
						So don't do this after all: */
					  //GET_PLAYER(ePlayer).AI_rememberEvent(getID(), MEMORY_ACCEPT_DEMAND);
					AI_rememberEvent(ePlayer, MEMORY_MADE_DEMAND);
				}
				else
				{
					// See above
					  //GET_PLAYER(ePlayer).AI_rememberEvent(getID(), MEMORY_REJECTED_DEMAND);
					/*  Here's where we don't increase MEMORY_MADE_DEMAND b/c the
						demand isn't accepted. CvPlayer::handleDiploEvent deals
						with the MADE_DEMAND_RECENT memory. */
				}
			} // <advc.155> Previously handled by CvPlayer::handleDiploEvent
			else if (bAccept && bSameTeam)
				AI_changeMemoryCount(ePlayer, MEMORY_MADE_DEMAND_RECENT, 1);
			// </advc.155>
		}
		return bAccept; // </advc.130o>
	} // <advc.036>
	if (!AI_checkResourceLimits(kWeGive, kTheyGive, ePlayer, iChange))
		return false; // </advc.036>
	// <advc.705>
	if (g.isOption(GAMEOPTION_RISE_FALL) && bHuman)
	{
		if (bPossibleCollusion)
		{
			double thresh = g.getRiseFall().dealThresh(isAnnualDeal(kWeGive));
			if (iChange < 0)
				thresh = std::min(thresh, std::max(0.4, thresh - 0.15));
			if (iPessimisticVal / (iWeReceive + 0.01) < thresh)
			{
				return (g.getRiseFall().isSquareDeal(kWeGive, kTheyGive, getID()) ||
						g.getRiseFall().isNeededWarTrade(kWeGive));
			}
		}
		if (!kOurTeam.isGoldTrading() && !GET_TEAM(ePlayer).isGoldTrading())
			iTheyReceive = ::round(0.9 * iTheyReceive);
	} // </advc.705>
	if (iChange < 0)
	{
		// <advc.133>
		int const iSpeedDivisor = GC.getInfo(g.getGameSpeedType()).getGoldenAgePercent();
		int iDealAgeAdjusted = std::max(0,
				((iDealAge - GC.getDefineINT(CvGlobals::PEACE_TREATY_LENGTH) / 2) *
				100) / iSpeedDivisor);
		// 125 flat in K-Mod, 110 in BtS
		int iTolerance = std::max(120, 150 - iDealAgeAdjusted);
		// <advc.155>
		if(bSameTeam)
		{
			if(iTheyReceive < iThreshold)
				return true;
			iTolerance = 150;
		} // </advc.155>
		if(iTolerance * iWeReceive >= iTheyReceive * 100)
			return true;
		// Additional conditions; to avoid bothering human players.
		if(!bHuman)
			return false;
		double relativeUnfairness = (iTolerance * iTheyReceive) / (100.0 * std::max(1, iWeReceive));
		double relativeLoss = (iTheyReceive - iWeReceive) /
				std::max(1.0, estimateYieldRate(YIELD_COMMERCE));
		// Reduce frequency of diplo popups in large games
		double hasMetFactor = 1 / std::pow((double)GET_TEAM(kPlayer.getTeam()).getHasMetCivCount(), 0.8);
		double const magicScaleFactor = 2;
		// Randomly delay cancellation b/c trade values might fluctuate
		double prCancel = magicScaleFactor * relativeUnfairness * relativeLoss * hasMetFactor;
		// Don't randomly cancel deals that are unimportant and not too unfair
		if (prCancel < 0.1)
			return true;
		// If it's clearly a bad deal, then a random delay could seem strange.
		if (prCancel > 0.5)
			return false;
		// Aim at more long-lasting deals in slow games
		prCancel = (prCancel * 100) / iSpeedDivisor;
		/*  (If cancellation were checked more than once per turn,
			::hash would have to be used instead.) */
		return !::bernoulliSuccess(prCancel, "advc.036 (cancel)");
		// </advc.133>
	}
	// <advc.136b>
	if (iWeReceive < 2 * GC.getDefineINT(CvGlobals::DIPLOMACY_VALUE_REMAINDER) &&
		/*  NB: bVassalTrade is currently only true if ePlayer offers to become
			a vassal, not when this player considers becoming a vassal. (fixme?) */
			!bVassalTrade && !kOurTeam.isAtWar(TEAMID(ePlayer)) &&
			!AI_goldDeal(kTheyGive) && (kTheyGive.getLength() <= 0 ||
			!CvDeal::isDual(kTheyGive.head()->m_data.m_eItemType)))
		return false;
	// </advc.136b>

	if (iWeReceive >= iTheyReceive)
		return true;
	// <advc.036>
	if (bHuman && iWeReceive + m_iSingleBonusTradeTolerance >= iTheyReceive &&
			kWeGive.getLength() == 1 && kTheyGive.getLength() == 1 &&
			kWeGive.head()->m_data.m_eItemType == TRADE_RESOURCES &&
			kTheyGive.head()->m_data.m_eItemType == TRADE_RESOURCES)
		return true;
	// </advc.036>
	return false;
}

double CvPlayerAI::AI_prDenyHelp() const
{
	double contactRandGiveHelp = GC.getInfo(getPersonalityType()).
			getContactRand(CONTACT_GIVE_HELP);
	double const r = std::min(std::sqrt(contactRandGiveHelp) / 100.0, 0.5);
	return r;
}

// K-Mod. Helper fuction for AI_counterPropose. (lambas would be really nice here, but we can't have nice things.)
bool maxValueCompare(const std::pair<TradeData const*, int>& a, const std::pair<TradeData const*, int>& b)
{
	return a.second > b.second;
}


bool CvPlayerAI::AI_counterPropose(PlayerTypes ePlayer, const CLinkList<TradeData>* pTheirList, const CLinkList<TradeData>* pOurList, CLinkList<TradeData>* pTheirInventory, CLinkList<TradeData>* pOurInventory, CLinkList<TradeData>* pTheirCounter, CLinkList<TradeData>* pOurCounter,
		double leniency) const // advc.705: Applied to everything that's added to iValueForThem
{
	PROFILE_FUNC(); // advc.opt
	bool bTheirGoldDeal = AI_goldDeal(*pTheirList);
	bool bOurGoldDeal = AI_goldDeal(*pOurList);

	if (bOurGoldDeal && bTheirGoldDeal)
		return false;
	/*  <advc.036> Check trade denial. Should only be needed when renegotiating
		a canceled deal with a human. */
	if (GET_PLAYER(ePlayer).isHuman())
	{
		for (CLLNode<TradeData> const* pNode = pOurList->head(); pNode != NULL;
				pNode = pOurList->next(pNode))
		{
			if(getTradeDenial(ePlayer, pNode->m_data) != NO_DENIAL)
				return false;
		}
		for (CLLNode<TradeData> const* pNode = pTheirList->head(); pNode != NULL;
				pNode = pTheirList->next(pNode))
		{
			if (GET_PLAYER(ePlayer).getTradeDenial(getID(), pNode->m_data) != NO_DENIAL)
				return false;
		}
	} // </advc.036>
	/*iHumanDealWeight = AI_dealVal(ePlayer, pTheirList);
	iAIDealWeight = GET_PLAYER(ePlayer).AI_dealVal(getID(), pOurList);*/ // BtS
	// K-Mod note: the original code had the human and AI weights the wrong way around.
	// I found this confusing, so I've corrected it throughout this function.
	// (Under normal usage of this fuction, the AI player counters the human proposal - so "they" are human, not us.)
	int iValueForUs = AI_dealVal(ePlayer, *pTheirList);
	int iValueForThem = /* advc.705: */ ::round(leniency *
			GET_PLAYER(ePlayer).AI_dealVal(getID(), *pOurList));
	/*  advc.001l: Moved into balanceDeal; has to be called on whichever side
		receives gold. */
	//int iGoldValuePercent = AI_goldTradeValuePercent();

	pTheirCounter->clear();
	pOurCounter->clear();

	// K-Mod. Refuse all war-time offers unless it's part of a peace deal
	if (atWar(getTeam(), GET_PLAYER(ePlayer).getTeam()))
	{
		// Check to see if there is already an end-war item on the table
		bool bEndWar = false;
		for (CLLNode<TradeData> const* pNode = pTheirList->head();
			!bEndWar && pNode != NULL; pNode = pTheirList->next(pNode))
		{
			if (CvDeal::isEndWar(pNode->m_data.m_eItemType))
			{
				bEndWar = true;
				break;
			}
		}
		for (CLLNode<TradeData> const* pNode = pOurList->head();
			!bEndWar && pNode != NULL; pNode = pOurList->next(pNode))
		{
			if (CvDeal::isEndWar(pNode->m_data.m_eItemType))
			{
				bEndWar = true;
				break;
			}
		}
		// see if someone is willing to surrender
		if (!bEndWar)
		{
			TradeData item(TRADE_SURRENDER);
			if (canTradeItem(ePlayer, item, true))
			{
				pOurCounter->insertAtEnd(item);
				bEndWar = true;
			}
			else if (GET_PLAYER(ePlayer).canTradeItem(getID(), item, true))
			{
				pTheirCounter->insertAtEnd(item);
				bEndWar = true;
			}
		}
		// last chance: try a peace treaty.
		if (!bEndWar)
		{
			TradeData item(TRADE_PEACE_TREATY);
			if (canTradeItem(ePlayer, item, true) && GET_PLAYER(ePlayer).canTradeItem(getID(), item, true))
			{
				//pOurCounter->insertAtEnd(item);
				//pTheirCounter->insertAtEnd(item);
				// unfortunately, there is some weirdness in the game engine which causes it to flip its wig
				// if trade items are added to both sides of a peace deal... so we have to do it like this:
				if (iValueForThem > iValueForUs)
					pTheirCounter->insertAtEnd(item);
				else pOurCounter->insertAtEnd(item);
				bEndWar = true;
			}
			else return false; // if there is no peace, there will be no trade
		}
	}
	// <advc.132> Don't accept two civics of the same column
	if(!AI_checkCivicReligionConsistency(*pOurList) ||
			!AI_checkCivicReligionConsistency(*pTheirList))
		return false; // </advc.132>
	// <advc.036>
	if(!AI_checkResourceLimits(*pOurList, *pTheirList, ePlayer, 1))
		return false;
	/*  The above makes sure that not too many non-surplus resources are already
		in either list, but we also need to know how many more can be added to
		the list that needs further items. */
	CLinkList<TradeData> const* pGiverList = (iValueForThem < iValueForUs ?
			pOurList : pTheirList);
	CvPlayer const& kGiver = (iValueForThem < iValueForUs ? *this : GET_PLAYER(ePlayer));
	CvPlayer const& kRecipient = (kGiver.getID() == ePlayer ? *this : GET_PLAYER(ePlayer));
	// Count how many more can be traded
	int iHappyLeft, iHealthLeft;
	iHappyLeft = iHealthLeft = 2;
	if(GET_TEAM(kGiver.getTeam()).isCapitulated())
		iHappyLeft = iHealthLeft = MAX_INT;
	else
	{
		for(CLLNode<TradeData> const* pNode = pGiverList->head(); pNode != NULL;
			pNode = pGiverList->next(pNode))
		{
			if(pNode->m_data.m_eItemType != TRADE_RESOURCES)
				continue;
			BonusTypes eBonus = (BonusTypes)pNode->m_data.m_iData;
			CvBonusInfo& kBonus = GC.getInfo(eBonus);
			if(!kRecipient.isHuman())
			{
				/*  AI mustn't accept too many resources of the same kind
					(happy or health) at once */
				if(kBonus.getHappiness() > 0)
					iHappyLeft--;
				if(kBonus.getHealth() > 0)
					iHealthLeft--;
			}
			else
			{
				/*  Non-capitulated AI mustn't give too many non-surplus resources
					of the same kind (happy or health) at once */
				if(kGiver.getNumAvailableBonuses(eBonus) <= 1)
				{
					if(kBonus.getHappiness() > 0)
						iHappyLeft--;
					if(kBonus.getHealth() > 0)
						iHealthLeft--;
				}
			}
		}
		iHappyLeft = std::max(0, iHappyLeft);
		iHealthLeft = std::max(0, iHealthLeft);
	} // </advc.036>
	// <advc.136b>
	if (!isHuman() && iValueForUs > iValueForThem &&
			iValueForUs < 2 * GC.getDefineINT(CvGlobals::DIPLOMACY_VALUE_REMAINDER))
		return false; // </advc.136b>
	// <advc.026>
	if (bOurGoldDeal && !AI_checkMaxGold(*pOurList, ePlayer))
		return false; // </advc.026>
	bool bDeal = false; // advc.036: Moved up
	// When counterProposing, we want balance the deal - but if we can't balance it, we want to make sure it favours us; not them.
	// So if their value is greater, we don't mind suggesting items which take them past balance.
	// But if our value is greater, we will never suggest adding items which overshoot the balance.
	if (iValueForThem > iValueForUs)
	{	// advc: Moved into auxiliary function
		bDeal = AI_balanceDeal(bOurGoldDeal, pTheirInventory, ePlayer,
			iValueForThem, iValueForUs, pTheirCounter, pOurList, 1, true,
			iHappyLeft, iHealthLeft, pTheirList->getLength()); // advc.036
	}
	else if (iValueForUs > iValueForThem)
	{	// advc: Moved into auxiliary function
		bDeal = GET_PLAYER(ePlayer).AI_balanceDeal(bTheirGoldDeal, pOurInventory,
				getID(), iValueForUs, iValueForThem, pOurCounter, pTheirList,
				leniency, false,
				iHappyLeft, iHealthLeft, pOurList->getLength()); // advc.036
	}
	/*return ((iValueForThem <= iValueForUs) && ((pOurList->getLength() > 0) || (pOurCounter->getLength() > 0) || (pTheirCounter->getLength() > 0)));*/ // BtS

	// K-Mod. This function now needs to handle AI - AI deals, and human auto-counters to AI suggested deals.
	if (pOurList->getLength() == 0 && pOurCounter->getLength() == 0 && pTheirCounter->getLength() == 0)
		return false;
	// <advc.036> Don't double-check when balanceDeal says we should accept
	if (bDeal)
		return true; // </advc.036>
	if (GET_PLAYER(ePlayer).isHuman())
	{
		// AI counting a human proposal:
		// do not compromise the AI's value, and don't even consider the human's value.
		bDeal = iValueForUs >= iValueForThem;
		FAssert(!isHuman());
	}
	else
	{
		if (isHuman())
		{
			// Human civ auto-negotiating an AI proposal before it is put to the player for confirmation:
			// let the AI show a little bit of leniency
			// don't bother putting it to the player if it is really bad value
			bDeal = //4*iValueForUs >= iValueForThem && // advc.550b
				100*iValueForThem >= GET_PLAYER(ePlayer).AI_tradeAcceptabilityThreshold(getID())*iValueForUs;
			/*  <advc.550b> About the really bad deals: 4 to 1 is OK
				when the human doesn't give away tech. */
			bool bTech = false;
			for(CLLNode<TradeData> const* pNode = pOurCounter->head(); pNode != NULL;
				pNode = pOurCounter->next(pNode))
			{
				if(pNode->m_data.m_eItemType == TRADE_TECHNOLOGIES)
				{
					bTech = true;
					break;
				}
			}
			if(!bTech && 4 * iValueForUs < iValueForThem)
				bDeal = false;
			if(bTech  && 3 * iValueForUs < iValueForThem * 2)
				bDeal = false;
			// </advc.550b>
		}
		else
		{
			// Negotiations between two AIs:
			bDeal = 100*iValueForUs >= AI_tradeAcceptabilityThreshold(ePlayer)*iValueForThem
				&& 100*iValueForThem >= GET_PLAYER(ePlayer).AI_tradeAcceptabilityThreshold(getID())*iValueForUs;

		}
	} // <advc.705>
	CvGame const& g = GC.getGame();
	if (g.isOption(GAMEOPTION_RISE_FALL) && GET_PLAYER(ePlayer).isHuman())
	{
		if (g.getRiseFall().isCooperationRestricted(getID()))
		{
			CLinkList<TradeData> const& ourListCounter = (pOurList->getLength() <= 0 ?
					*pOurCounter : *pOurList);
			double thresh = g.getRiseFall().dealThresh(isAnnualDeal(ourListCounter));
			if(bDeal && g.getRiseFall().pessimisticDealVal(getID(), iValueForThem,
				ourListCounter) / (iValueForUs + 0.01) < thresh)
			{
				bDeal = ((g.getRiseFall().isSquareDeal(*pOurList, *pTheirList, getID()) &&
						 g.getRiseFall().isSquareDeal(*pOurCounter, *pTheirCounter, getID())) ||
						 g.getRiseFall().isNeededWarTrade(*pOurList));
			}
		}
		double const secondAttempt = 0.9;
		if (!bDeal && !GET_TEAM(getTeam()).isGoldTrading() &&
			!GET_TEAM(ePlayer).isGoldTrading() &&
			std::abs(leniency - secondAttempt) > 0.01)
		{
			return AI_counterPropose(ePlayer, pTheirList, pOurList,
					pTheirInventory, pOurInventory, pTheirCounter, pOurCounter,
					secondAttempt);
		}
	} // </advc.705>
	return bDeal;
	// K-Mod end
}

/*  advc: Moved and refactored from AI_counterPropose in order to reduce
	code duplication.
	ePlayer is the owner of pInventory, the one who needs to give more.
	The callee is the one who needs to receive more.
	bGenerous means that the callee is willing to accept an unfavorable trade.
	The comments in the function body are from K-Mod.
	advc.036: Returning true means force-accept. */
bool CvPlayerAI::AI_balanceDeal(bool bGoldDeal, CLinkList<TradeData> const* pInventory,
	PlayerTypes ePlayer, int& iGreaterVal, int& iSmallerVal,
	CLinkList<TradeData>* pCounter, CLinkList<TradeData> const* pList,
	double leniency, // advc.705
	bool bGenerous,
	int iHappyLeft, int iHealthLeft, int iOtherListLength) const // advc.036
{
	PROFILE_FUNC(); // advc.opt
	CvPlayerAI const& kPlayer = GET_PLAYER(ePlayer);
	// First, try to make up the difference with gold.
	int iGoldValuePercent = kPlayer.AI_goldTradeValuePercent(); // advc.001l
	CLLNode<TradeData>* pGoldPerTurnNode = NULL;
	CLLNode<TradeData>* pGoldNode = NULL;
	if (!bGoldDeal)
	{
		for (CLLNode<TradeData>* pNode = pInventory->head(); pNode != NULL;
			pNode = pInventory->next(pNode))
		{
			TradeData data = pNode->m_data;
			if (data.m_bOffering || data.m_bHidden)
				continue;
			/*  <advc.104> Avoid a potentially costly TradeDenial check for
				items that aren't covered by the switch block anyway. */
			TradeableItems ti = data.m_eItemType;
			if (ti != TRADE_GOLD && ti != TRADE_GOLD_PER_TURN)
				continue; // </advc.104>
			if (kPlayer.getTradeDenial(getID(), data) != NO_DENIAL)
				continue;
			FAssert(kPlayer.canTradeItem(getID(), data));
			switch(ti)
			{
			case TRADE_GOLD:
				pGoldNode = pNode;
				break;
			case TRADE_GOLD_PER_TURN:
				pGoldPerTurnNode = pNode;
				break;
			}
		}
	}
	if (pGoldNode != NULL)
	{
		int iGoldData = ((iGreaterVal - iSmallerVal) * 100 +
				// bGenerous: round up or down
				(bGenerous ? (iGoldValuePercent - 1) : 0)) /
				iGoldValuePercent;
		// <advc.026>
		if (isHuman() && !bGenerous)
			AI_roundTradeVal(iGoldData);
		int iMaxGold = ((isHuman() && bGenerous) ?
				kPlayer.AI_maxGoldTradeGenerous(getID()) :
				kPlayer.AI_maxGoldTrade(getID()));
		if (iMaxGold >= iGoldData) // Replacing: </advc.026>
						// if(kPlayer.AI_maxGoldTrade(getID()) >= iGoldData)
		{
			pGoldNode->m_data.m_iData = iGoldData;
			iSmallerVal += (iGoldData * iGoldValuePercent) / 100;
			pCounter->insertAtEnd(pGoldNode->m_data);
			pGoldNode = NULL;
		}
	}
	/*  <advc.036> Try gpt before resources when trading with a human with
		negative income. Based on the "try one more time" code chunk below. */
	if (isHuman() && pGoldPerTurnNode != NULL && iGreaterVal > iSmallerVal &&
		calculateGoldRate() < 0)
	{
		int iGoldAvailable = kPlayer.AI_maxGoldPerTurnTrade(getID());
		// <advc> Code moved into new function AI_tradeValToGold
		bool bEnoughGold = false;
		int iGoldData = AI_tradeValToGold(iGreaterVal - iSmallerVal, bGenerous,
				iGoldAvailable, &bEnoughGold);
		if (bEnoughGold && iGoldData > 0) // </advc>
		{
			int iGPTTradeVal = AI_goldPerTurnTradeVal(iGoldData);
			pGoldPerTurnNode->m_data.m_iData = iGoldData;
			iSmallerVal += iGPTTradeVal;
			pCounter->insertAtEnd(pGoldPerTurnNode->m_data);
			pGoldPerTurnNode = NULL;
		}
	} // </advc.036>
	std::pair<TradeData const*, int> final_item(NULL, 0); // An item we may or may not use to finalise the deal. (See later)
	// <advc.036>
	bool bSingleResource = (iOtherListLength <= 1 && (isHuman() || kPlayer.isHuman()) &&
			pList->getLength() == 1 &&
			pList->head()->m_data.m_eItemType == TRADE_RESOURCES);
	std::vector<std::pair<TradeData const*, int> > nonsurplusItems; // </advc.036>
	if (iGreaterVal > iSmallerVal)
	{
		// We were unable to balance the trade with just gold. So lets look at all the other items.
		// Exclude bonuses that we've already put on the table
		std::vector<bool> vbBonusDeal(GC.getNumBonusInfos(), false);
		for(CLLNode<TradeData> const* pNode = pList->head(); pNode != NULL; pNode = pList->next(pNode))
		{
			TradeData data = pNode->m_data;
			FAssert(!data.m_bHidden);
			if(data.m_eItemType == TRADE_RESOURCES)
				vbBonusDeal[pNode->m_data.m_iData] = true;
		}
		// We're only going to allow one city on the list. (For flavour reasons.)
		int iBestCityValue = 0;
		int iBestCityWeight = 0;
		CLLNode<TradeData> const* pBestCityNode = NULL;
		// Evaluate everything they're willing to trade.
		std::vector<std::pair<TradeData const*, int> > item_value_list; // (item*, value)
		CvGame const& g = GC.getGame();
		for (CLLNode<TradeData> const* pNode = pInventory->head(); pNode != NULL;
			pNode = pInventory->next(pNode))
		{
			TradeData data = pNode->m_data;
			if (data.m_bOffering || data.m_bHidden)
				continue;
			/*  <advc.104> Avoid a potentially costly TradeDenial check for
				items that aren't covered by the switch block anyway. */
			TradeableItems eItemType = data.m_eItemType;
			if (eItemType != TRADE_MAPS && eItemType != TRADE_TECHNOLOGIES &&
					eItemType != TRADE_RESOURCES && eItemType != TRADE_CITIES)
				continue; // </advc.104>
			if (kPlayer.getTradeDenial(getID(), data) != NO_DENIAL)
				continue;
			FAssert(kPlayer.canTradeItem(getID(), data));
			bool bNonsurplus = false; // advc.036
			int iItemValue = 0;
			switch(eItemType)
			{
			case TRADE_MAPS:
				iItemValue = GET_TEAM(getTeam()).AI_mapTradeVal(kPlayer.getTeam());
				break;
			case TRADE_TECHNOLOGIES:
				iItemValue = GET_TEAM(getTeam()).AI_techTradeVal((TechTypes)
						data.m_iData, kPlayer.getTeam());
				break;
			case TRADE_RESOURCES:
				if (!vbBonusDeal[data.m_iData])
				{
					// Don't ask for the last of a resource, or corporation resources; because we're not going to evaluate losses.
					// <advc.036> AI_bonusTradeVal evaluates losses now; moved up:
					BonusTypes eBonus = (BonusTypes)data.m_iData;
					CvBonusInfo& kBonus = GC.getInfo(eBonus);
					iItemValue = AI_bonusTradeVal(eBonus, ePlayer, 1,
							(kBonus.getHealth() > 0 && iHealthLeft == 1) ||
							(kBonus.getHappiness() > 0 && iHappyLeft == 1));
					/*  Bias AI-AI trades against resources that are of low value
						to the recipient; should rather try to trade these to
						someone else. */
					if (!isHuman() && !kPlayer.isHuman() &&
						// Don't skip items that fill the gap very well
						iGreaterVal - iSmallerVal - iItemValue > 10)
					{
						/*  I think randomness in this function could lead to
							OOS problems */
						std::vector<long> hashInputs;
						hashInputs.push_back(g.getGameTurn());
						hashInputs.push_back(eBonus);
						if(::hash(hashInputs, getID()) > iItemValue / 100.0)
							continue;
					}
					vbBonusDeal[data.m_iData] = true;
					bNonsurplus = true;
					if ((kPlayer.getNumTradeableBonuses(eBonus) > 1 &&
						kPlayer.AI_corporationBonusVal(eBonus, true) == 0)
						/*  Prefer to give non-surplus resources over gpt in
							AI-AI trades (i.e. no special treatment of nonsurplus
							resources needed). If a human is involved, gpt needs to
							be preferred b/c we don't want humans to minmax the gpt
							through trial and error. */
						|| (!isHuman() && !kPlayer.isHuman()))
					{
						bNonsurplus = false;
					}
					/*  Don't worry about iHappyLeft and iHealthLeft when there's
						just 1 item on the table */
					if (iOtherListLength > 1 || pList->getLength() > 1)
					{
						/*  Would be better to decrement the -left counters only
							once eBonus is added to pCounter, but that's compli-
							cated to implement. The important thing is to enforce
							the limits. */
						if (!kPlayer.isHuman())
						{
							/*  AI mustn't give too many non-surplus resources of
								a kind (happy, health) at once */
							if (kPlayer.getNumAvailableBonuses(eBonus) <= 1)
							{
								if(kBonus.getHappiness() > 0 && iHappyLeft <= 0)
									continue;
								else iHappyLeft--;
								if(kBonus.getHealth() > 0 && iHealthLeft <= 0)
									continue;
								else iHealthLeft--;
							}
						}
						else /* AI mustn't accept too many resources of
								a kind (happy, health) at once */
						{
							if (kBonus.getHappiness() > 0 && iHappyLeft <= 0)
								continue;
							else iHappyLeft--;
							if (kBonus.getHealth() > 0 && iHealthLeft <= 0)
								continue;
							else iHealthLeft--;
						}
					} // </advc.036>
				}
				break;
			case TRADE_CITIES:
				if (::atWar(kPlayer.getTeam(), getTeam()))
				{
					CvCityAI* pCity = kPlayer.AI_getCity(data.m_iData);
					if (pCity != NULL)
					{
						int iWeight = AI_targetCityValue(pCity, false);
						if (iWeight > iBestCityWeight)
						{
							int iValue = AI_cityTradeVal(*pCity);
							if (iValue > 0)
							{
								iBestCityValue = iValue;
								iBestCityWeight = iWeight;
								pBestCityNode = pNode;
							}
						}
					}
				}
				break;
			}
			if (iItemValue > 0 && (bGenerous || iGreaterVal >= iSmallerVal + iItemValue
				// <advc.036>
				|| (iGreaterVal + m_iSingleBonusTradeTolerance >= iItemValue &&
				pCounter->getLength() <= 0 && iOtherListLength <= 0 &&
				bSingleResource && eItemType == TRADE_RESOURCES)))
			{
				std::pair<TradeData const*,int> itemValue = std::make_pair(&pNode->m_data, iItemValue);
				if(bNonsurplus)
					nonsurplusItems.push_back(itemValue);
				else item_value_list.push_back(itemValue); // </advc.036>
			}
		}
		if (pBestCityNode != NULL)
		{
			item_value_list.push_back(std::make_pair(&pBestCityNode->m_data,
					iBestCityValue));
		}
		if (bGenerous)
		{
			// We want to get as close as we can to a balanced trade - but ensure that the deal favours us!
			// Find the best item, add it to the list; and repeat until we've closed the game in the trade values.
			while (iGreaterVal > iSmallerVal && !item_value_list.empty())
			{
				int value_gap = iGreaterVal - iSmallerVal;
				// Find the best item to put us ahead, but as close to fair as possible.
				// Note: We're not doing this for perfect balance. We're counter-proposing so that the deal favours us!
				//   If we wanted to get closer to a balanced deal, we just remove that first condition.
				//   (Maybe that's what we should be doing for AI-AI trades; but there are still flavour considersations...)
				std::vector<std::pair<TradeData const*, int> >::iterator it, best_it;
				for (best_it = it = item_value_list.begin();
						it != item_value_list.end(); it++)
				{
					if((it->second > value_gap && best_it->second < value_gap) ||
							std::abs(it->second - value_gap) <
							std::abs(best_it->second - value_gap))
						best_it = it;
				}
				// Only add the item if it will get us closer to balance.
				if(best_it->second <= 2*(iGreaterVal - iSmallerVal))
				{
					pCounter->insertAtEnd(*best_it->first);
					iSmallerVal += best_it->second;
					item_value_list.erase(best_it);
				}
				else
				{
					// If nothing on the list can bring us closer to balance; we'll try to balance it with gold.
					// But if that doesn't work, we may need to add this last item. So lets bookmark it.
					final_item = *best_it;
					break;
				}
			}
		}
		else
		{
			// Sort the values from largest to smallest
			std::sort(item_value_list.begin(), item_value_list.end(), maxValueCompare);
			// Use the list to balance the trade.
			for (std::vector<std::pair<TradeData const*, int> >::iterator it =
					item_value_list.begin(); it != item_value_list.end() &&
					iGreaterVal > iSmallerVal; it++) {
				int iItemValue = it->second;
				if (iGreaterVal >= iSmallerVal + iItemValue /* <advc.036> */ ||	
					(bSingleResource && iGreaterVal +
					m_iSingleBonusTradeTolerance >= iSmallerVal &&
					pCounter->getLength() <= 0 && iOtherListLength <= 0 &&
					it->first->m_eItemType == TRADE_RESOURCES)) // </advc.036>
				{
					pCounter->insertAtEnd(*it->first);
					iSmallerVal += /* advc.705: */ ::round(leniency *
							iItemValue);
				}
			}
		}
	} // <advc.036> Special treatment for one-for-one resource trades
	if (bSingleResource && iGreaterVal - iSmallerVal <= m_iSingleBonusTradeTolerance &&
			((pCounter->getLength() <= 0 && iOtherListLength == 1 &&
			pGoldPerTurnNode != NULL) ||
			(pCounter->getLength() == 1 &&
			pCounter->head()->m_data.m_eItemType == TRADE_RESOURCES)))
		return true; // </advc.036>
	// If their value is still higher, try one more time to make up the difference with gold.
	// If we're counter-proposing an AI deal, just get as close to the right value as we can.
	// But for humans, if they don't have enough gold then ask for one final item, to favour us.
	bool bAddFinalItem = false;
	if (iGreaterVal > iSmallerVal && (bGenerous ||
		// Consider the special case of a human player auto-counter-proposing a deal from an AI.
		// Humans are picky with trades. They turn down most AI deals. So to increase the chance of the human
		// ultimately accepting, lets see if the AI would allow the deal without any added gold. If the AI will
		// accept, then we won't add the gold. And this will be a rare example of the AI favouring the human.
		!kPlayer.isHuman() || 100*iSmallerVal >=
		AI_tradeAcceptabilityThreshold(ePlayer) * iGreaterVal))
	{
		if (pGoldNode != NULL)
		{
			int iGoldData = ((iGreaterVal - iSmallerVal) * 100 +
					// bGenerous: round up or down
					(bGenerous ? (iGoldValuePercent - 1) : 0)) /
					iGoldValuePercent;
			//int iGoldAvailable = kPlayer.AI_maxGoldTrade(getID());
			// <advc.026> Replacing the above
			if(isHuman() && !bGenerous)
				AI_roundTradeVal(iGoldData);
			int iGoldAvailable = ((bGenerous && isHuman()) ?
					kPlayer.AI_maxGoldTradeGenerous(getID()) :
					kPlayer.AI_maxGoldTrade(getID())); // </advc.026>
			if(bGenerous && kPlayer.isHuman() && iGoldData > iGoldAvailable)
				bAddFinalItem = true;
			else
			{
				iGoldData = std::min(iGoldData, iGoldAvailable);
				if(iGoldData > 0)
				{
					pGoldNode->m_data.m_iData = iGoldData;
					iSmallerVal +=
							::round(leniency * // advc.705
							((iGoldData * iGoldValuePercent) / 100));
					pCounter->insertAtEnd(pGoldNode->m_data);
					pGoldNode = NULL;
				}
			}
		}
		/*  <advc.001> If human asks for gold, then pGoldNode is NULL here,
			and the AI won't ask e.g. for a tech in exchange */
		else if(bGenerous && kPlayer.isHuman() && iGreaterVal > iSmallerVal &&
				(pList->getLength() <= 0 ||
				!CvDeal::isAnnual(pList->head()->m_data.m_eItemType)))
			bAddFinalItem = true; // </advc.001>
	}
	if (iGreaterVal > iSmallerVal)
	{
		if (pGoldPerTurnNode != NULL)
		{
			//int iGoldAvailable = kPlayer.AI_maxGoldPerTurnTrade(getID());
			// <advc.026> Replacing the above (and moved up)
			int iMaxGPT = ((isHuman() && bGenerous) ?
					kPlayer.AI_maxGoldPerTurnTradeGenerous(getID()) :
					kPlayer.AI_maxGoldPerTurnTrade(getID()));
			int iGoldAvailable = iMaxGPT; // </advc.026>
			// <advc> Code moved into new function AI_tradeValToGold
			bool bEnoughGold = false;
			int iGoldData = AI_tradeValToGold(iGreaterVal - iSmallerVal, bGenerous,
					iGoldAvailable, &bEnoughGold);
			if (!bEnoughGold && bGenerous && kPlayer.isHuman())
				bAddFinalItem = true;
			else if (iGoldData > 0) // </advc>
			{
				pGoldPerTurnNode->m_data.m_iData = iGoldData;
				iSmallerVal +=
						::round(leniency * // advc.705
						AI_goldPerTurnTradeVal(pGoldPerTurnNode->
						m_data.m_iData));
				pCounter->insertAtEnd(pGoldPerTurnNode->m_data);
				pGoldPerTurnNode = NULL;
			}
		}
		// <advc.001> See above at if(pGoldNode)...else
		else if (bGenerous && kPlayer.isHuman() && iGreaterVal > iSmallerVal &&
				(pList->getLength() <= 0 ||
				CvDeal::isAnnual(pList->head()->m_data.m_eItemType)))
			bAddFinalItem = true;
	} // When iGoldAvailable is too small but iMaxGPT isn't
	if (iSmallerVal >= iGreaterVal)
		bAddFinalItem = false; // </advc.001>
	// <advc.036> A mix of the two K-Mod algorithms for item selection
	else if (final_item.first == NULL)
	{
		while (iGreaterVal > iSmallerVal && !nonsurplusItems.empty())
		{
			int value_gap = iGreaterVal - iSmallerVal;
			std::vector<std::pair<TradeData const*, int> >::iterator it, best_it;
			for (best_it = it = nonsurplusItems.begin();
					it != nonsurplusItems.end(); it++)
			{
				if (!bGenerous && iSmallerVal + it->second > iGreaterVal)
					continue;
				if ((bGenerous && it->second > value_gap &&
						best_it->second < value_gap) ||
						std::abs(it->second - value_gap) <
						std::abs(best_it->second - value_gap))
					best_it = it;
			}
			if (best_it->second <= 2 * (iGreaterVal - iSmallerVal) ||
					bGenerous)
			{
				pCounter->insertAtEnd(*best_it->first);
				iSmallerVal += best_it->second;
			}
			nonsurplusItems.erase(best_it);
		}
	} // </advc.036>
	// When counter proposing a suggestion from a human, the AI will insist on having the better value.
	// So lets add the cheapest item still on our list.
	// We would have added the item already if it was going to be 'fair'. So we can be sure will favour us.
	if (bAddFinalItem && final_item.first != NULL)
	{
		FAssert(iGreaterVal > iSmallerVal && kPlayer.isHuman());
		pCounter->insertAtEnd(*final_item.first);
		iSmallerVal += final_item.second;
		FAssert(iSmallerVal >= iGreaterVal);
	}
	return false; // advc.036
}


int CvPlayerAI::AI_tradeValToGold(int iTradeVal, bool bOverpay, int iMaxGold,
		bool* bEnough) const
{
	PROFILE_FUNC();
	int r = 0;
	if(bOverpay)
	{
		while (AI_goldPerTurnTradeVal(r) < iTradeVal && r < iMaxGold)
			r++;
		if (bEnough != NULL)
			*bEnough = (AI_goldPerTurnTradeVal(r) >= iTradeVal);
	}
	else
	{
		while (AI_goldPerTurnTradeVal(r + 1) <= iTradeVal && r < iMaxGold)
			r++;
		if (bEnough != NULL)
			*bEnough = (AI_goldPerTurnTradeVal(r + 1) > iTradeVal);
	}
	return r;
}

/*  advc: This function is for AI-initiated trades. The caller has placed some items
	in kTheyGive and/or kWeGive - these have to be included -, and this function
	tries to sweeten the deal for whichever side needs it (or both).
	Based on K-Mod code in AI_doDiplo; karadoc's comment cut and pasted from there
	(can't say I really understand the final note):
	"unfortunately, the API is pretty clumsy for setting up this counter proposal.
	 please just bear with me. (Note: this would be faster if we just built the lists
	 directly, but by using the existing API, we are kind of future-proofing)" */
bool CvPlayerAI::AI_counterPropose(PlayerTypes ePlayer,
	CLinkList<TradeData>& kTheyGive,
	CLinkList<TradeData>& kWeGive,
	bool bTheyMayGiveMore, bool bWeMayGiveMore,
	/*  advc.705: Multiplier for the value counted for the items of the
		side that does not give more. */
	double generosity) const
{
	PROFILE_FUNC();
	FAssert(ePlayer != getID());
	FAssertMsg(bTheyMayGiveMore || !bWeMayGiveMore, "Probably better to call "
			"AI_counterPropose on ePlayer then");
	FAssert(bTheyMayGiveMore || bWeMayGiveMore);
	FAssert(generosity != 0);
	CLinkList<TradeData> ourInventory, theirInventory, ourCounter, theirCounter;
	if(bTheyMayGiveMore)
	{
		CvPlayer const& kPlayer = GET_PLAYER(ePlayer);
		// "all tradeable items"
		kPlayer.buildTradeTable(getID(), theirInventory);
		// "K-Mod function - set m_bOffering on each item offered"
		kPlayer.markTradeOffers(theirInventory, kTheyGive);
		// "hide what should be excluded"
		kPlayer.updateTradeList(getID(), theirInventory, kTheyGive, kWeGive);
		generosity = 1 / generosity; // advc.705
	}
	if(bWeMayGiveMore)
	{
		buildTradeTable(ePlayer, ourInventory);
		markTradeOffers(ourInventory, kWeGive);
		updateTradeList(ePlayer, ourInventory, kWeGive, kTheyGive);
	}
	if(AI_counterPropose(ePlayer, &kTheyGive, &kWeGive,
		&theirInventory, &ourInventory,
		&theirCounter, &ourCounter, generosity))
	{
		FAssert(bWeMayGiveMore || ourCounter.getLength() == 0);
		FAssert(bTheyMayGiveMore || theirCounter.getLength() == 0);
		kWeGive.concatenate(ourCounter);
		kTheyGive.concatenate(theirCounter);
		return true;
	}
	return false;
}

// K-Mod. Minimum percentage of trade value that this player will accept.
// ie. Accept trades if 100 * value_for_us >= residual * value_for_them .
int CvPlayerAI::AI_tradeAcceptabilityThreshold(PlayerTypes eTrader) const
{
	if (isHuman())
		return 75;

	int iDiscount = 25;
	iDiscount += 10 * AI_getAttitudeWeight(eTrader) / 100;
	// This puts us between 15 (furious) and 35 (friendly)
	// in some later version, I might make this personality based.

	// adjust for team rank.
	int iRankDelta = GC.getGame().getTeamRank(GET_PLAYER(eTrader).getTeam()) - GC.getGame().getTeamRank(getTeam());
	iDiscount += 10 * iRankDelta / std::max(6, GC.getGame().countCivTeamsAlive());

	if (GET_PLAYER(eTrader).isHuman())
	{
		// note. humans get no discount for trades that they propose.
		// The discount here only applies to deals that the AI offers to the human.
		iDiscount = iDiscount*2/3;
		// <advc.134b> Change disabled again:
			/*  No discount if recently begged or demanded
				advc.130j: Can now check >1 instead of >0, which seems like a better
				balance. AI doesn't offer discounts if *very* recently begged. */
			//if(AI_getMemoryCount(eTrader, MEMORY_MADE_DEMAND_RECENT) > 1)
			//	iDiscount = 0;
		// </advc.134b>
	}
	return 100 - iDiscount;
} // K-Mod end

// <advc.132>
bool CvPlayerAI::AI_checkCivicReligionConsistency(CLinkList<TradeData> const& tradeItems) const
{
	std::set<int> civicOptions;
	int iReligionChanges = 0;
	for(CLLNode<TradeData> const* pNode = tradeItems.head(); pNode != NULL; pNode = tradeItems.next(pNode))
	{
		if(pNode->m_data.m_eItemType == TRADE_CIVIC)
		{
			int iCivicOption = GC.getInfo((CivicTypes)pNode->m_data.m_iData).
					getCivicOptionType();
			if(civicOptions.find(iCivicOption) != civicOptions.end())
				return false;
			civicOptions.insert(iCivicOption);
		}
		else if(pNode->m_data.m_eItemType == TRADE_RELIGION)
		{
			iReligionChanges++;
			if(iReligionChanges > 1)
				return false;
		}
	}
	return true;
} // </advc.132>

// <advc.036>
bool CvPlayerAI::AI_checkResourceLimits(CLinkList<TradeData> const& kWeGive,
		CLinkList<TradeData> const& kTheyGive, PlayerTypes eThey, int iChange) const
{
	/*  Resources in lists mustn't overlap. TradeDenial should already ensure
		this, but I don't think that's totally reliable. */
	std::set<int> resources1;
	std::set<int> resources2;
	std::set<int>* apResources[] = { &resources1, &resources2 };
	CLinkList<TradeData> const* apTradeItems[] = {&kWeGive, &kTheyGive};
	CvPlayer const* apGiver[] = { this, &GET_PLAYER(eThey) };
	for(int i = 0; i < 2; i++)
	{
		int iNonSurplusHappy = 0;
		int iNonSurplusHealth = 0;
		int iHappy, iHealth;
		iHappy = iHealth = 0;
		for(CLLNode<TradeData> const* pNode = apTradeItems[i]->head(); pNode != NULL;
			pNode = apTradeItems[i]->next(pNode))
		{
			if(pNode->m_data.m_eItemType != TRADE_RESOURCES)
				continue;
			BonusTypes eBonus = (BonusTypes)pNode->m_data.m_iData;
			apResources[i]->insert(eBonus);
			CvBonusInfo& kBonus = GC.getInfo(eBonus);
			bool bAvail = (apGiver[i]->getNumAvailableBonuses(eBonus) - iChange > 0);
			if(kBonus.getHappiness() > 0)
			{
				iHappy++;
				if(!bAvail)
					iNonSurplusHappy++;
			}
			if(kBonus.getHealth() > 0)
			{
				iHealth++;
				if(!bAvail)
					iNonSurplusHealth++;
			}
		}
		/*  AI mustn't trade away too many non-surplus resources of a kind
			(happy, health) at once */
		if(!apGiver[i]->isHuman() && !GET_TEAM(apGiver[i]->getTeam()).isCapitulated() &&
				(iNonSurplusHappy > 2 || iNonSurplusHealth > 2))
			return false;
		/*  apGiver[(i + 1) % 2] is the recipient. AI mustn't accept too many
			resource of a kind (happy, health) at once. */
		if(!apGiver[(i + 1) % 2]->isHuman() && (iHappy > 2 || iHealth > 2))
			return false;
	}
	if(resources1.empty() || resources2.empty()) // for speed
		return true;
	std::vector<int> intersection;
	std::set_intersection(resources1.begin(), resources1.end(),
			resources2.begin(), resources2.end(), std::back_inserter(
			intersection));
	return intersection.empty();
} // </advc.036>


int CvPlayerAI::AI_maxGoldTrade(PlayerTypes ePlayer, /* advc.134a: */ bool bTeamTrade) const
{
	PROFILE_FUNC();
	FAssert(ePlayer != getID());
	int iTreasury = getGold(); // advc
	if(bTeamTrade && !isHuman() && GET_PLAYER(ePlayer).isHuman())
	{
		/*  <advc.134a> AI peace offers to humans (and any other trades offered
			to humans during a team turn) need to anticipate the gold spent during
			the AI player turn; otherwise, the AI may not have enough gold when
			the offer is checked (by the EXE) at the start of the human turn.
			Can't know for sure how much will be spent. */
		iTreasury /= 2;
		iTreasury += std::min(0, (3 * getGoldPerTurn()) / 2);
		iTreasury = std::max(0, iTreasury);
	} // </advc.134a>
	// <advc.104w>
	if(getUWAI.isEnabled() && GET_TEAM(ePlayer).isAtWar(getTeam()))
	{
		int r = std::max(0, iTreasury);
		// Don't tell them exactly how much we can afford
		return r - (r % (2 * GC.getDefineINT(CvGlobals::DIPLOMACY_VALUE_REMAINDER)));
	} // </advc.104w>
	if (isHuman() || (GET_PLAYER(ePlayer).getTeam() == getTeam()))
	{
		// <advc>
		return std::max(0, iTreasury);
	}
	/*else
	{*/ // </advc>
	/*iMaxGold = getTotalPopulation();
	iMaxGold *= (GET_TEAM(getTeam()).AI_getHasMetCounter(GET_PLAYER(ePlayer).getTeam()) + 10);
	iMaxGold *= GC.getInfo(getPersonalityType()).getMaxGoldTradePercent();
	iMaxGold /= 100;
	iMaxGold -= AI_getGoldTradedTo(ePlayer);
	iResearchBuffer = -calculateGoldRate() * 12;
	iResearchBuffer *= GC.getInfo(GC.getGame().getGameSpeedType()).getResearchPercent();
	iResearchBuffer /= 100;
	iMaxGold = std::min(iMaxGold, getGold() - iResearchBuffer);
	iMaxGold = std::min(iMaxGold, getGold());
	iMaxGold -= (iMaxGold % GC.getDefineINT("DIPLOMACY_VALUE_REMAINDER"));*/ // BtS

	// K-Mod. Similar, but with more personality, and with better handling of situations where the AI has lots of spare gold.
	int iTradePercent = GC.getInfo(getPersonalityType()).getMaxGoldTradePercent();
	int iMaxGold = getTotalPopulation();

	iMaxGold *= (GET_TEAM(getTeam()).AI_getHasMetCounter(GET_PLAYER(ePlayer).getTeam()) + 10);

	iMaxGold *= iTradePercent;
	iMaxGold /= 100;

	/*iMaxGold -= AI_getGoldTradedTo(ePlayer);
	iMaxGold = std::max(0, iMaxGold);*/
	//int iGoldRate = calculateGoldRate();
	// <advc.036> Replacing the above
	int iGoldRate = (AI_getAvailableIncome() - calculateInflatedCosts() -
			std::max(0, -getGoldPerTurn())) / 5; // </advc.036>
	if (iGoldRate > 0)
	{
		//PROFILE("AI_maxGoldTrade: gold rate adjustment"); // advc.003o
		int iTarget = AI_goldTarget();
		if (iTreasury < iTarget)
		{
			iGoldRate -= (iTarget - iTreasury)/3;
			iGoldRate = std::max(0, iGoldRate);
		}
		else iMaxGold += (iTreasury - iTarget) * iTradePercent / 100;
		iMaxGold += iGoldRate * 2 * iTradePercent / 100;
	}
	else
	{
		int iAdjustment = iGoldRate * 12;
		iAdjustment *= GC.getInfo(GC.getGame().getGameSpeedType()).getResearchPercent();
		iAdjustment /= 100;
		iMaxGold += iAdjustment;
	} // <advc.036>
	iMaxGold = AI_adjustTradeGoldToDiplo(iMaxGold, ePlayer);
	// Moved from above the gold rate adjustment
	iMaxGold -= AI_getGoldTradedTo(ePlayer); // </advc.036>
	iMaxGold = range(iMaxGold, 0, iTreasury);
	// <advc.550f>
	if(GET_PLAYER(ePlayer).isHuman() && GET_TEAM(getTeam()).isTechTrading() ||
		GET_TEAM(ePlayer).isTechTrading())
	{
		TechTypes cr = getCurrentResearch();
		double techProgr = (cr == NO_TECH ? 0 : GET_TEAM(getTeam()).
				getResearchProgress(cr) / (double)std::max(1,
				GET_TEAM(getTeam()).getResearchCost(cr)));
		double goldRatio = std::min(1.0, 1.4 *
				(1 - std::min(std::abs(techProgr - 0.75),
				std::abs(techProgr + 0.25))) - 0.33);
		iMaxGold = (int)(iMaxGold * goldRatio);
	} // </advc.550f>
	AI_roundTradeVal(iMaxGold);
	// K-Mod end
	return std::max(0, iMaxGold);
}

// <advc.036>
int CvPlayerAI::AI_adjustTradeGoldToDiplo(int iGold, PlayerTypes eTo) const
{
	double mult = 1;
	if(!GET_TEAM(eTo).isAtWar(getTeam()) && !GET_TEAM(getTeam()).isCapitulated())
	{
		AttitudeTypes att = AI_getAttitude(eTo);
		switch(att) {
		case ATTITUDE_FURIOUS: mult = 0.33; break;
		case ATTITUDE_ANNOYED: mult = 0.67; break;
		case ATTITUDE_PLEASED: mult = 1.2; break;
		case ATTITUDE_FRIENDLY: mult = 1.4; break;
		}
	}
	return ::round(iGold * mult);
}

void CvPlayerAI::AI_foldDeals() const
{
	/*  This is called on the Barbarian turn, i.e. at the end of a round, and goes
		through all gold-for-resource deals between AI civs. */
	// [i][j]: i pays gpt to j
	std::queue<CvDeal*> dealsPerPair[MAX_CIV_PLAYERS][MAX_CIV_PLAYERS];
	FOR_EACH_DEAL_VAR(d)
	{
		if(!d->isCancelable())
			continue;
		int len1 = d->getLengthFirstTrades();
		int len2 = d->getLengthSecondTrades();
		if(len1 != 1 || len2 != 1) // Only consider gpt-for-single-resource deals
			continue;
		CvPlayer* civ[2] = { &GET_PLAYER(d->getFirstPlayer()),
				&GET_PLAYER(d->getSecondPlayer()) };
		if(civ[0]->isHuman() || civ[1]->isHuman())
			continue;
		if(d->getFirstTrades() == NULL || d->getSecondTrades() == NULL)
			continue;
		CLinkList<TradeData> const* pList[2] = { d->getFirstTrades(), d->getSecondTrades() };
		int iGPTItems = 0;
		int iBonusItems = 0;
		bool bValid = true;
		int iGPTItemIndex = -1;
		for(int i = 0; i < 2; i++)
		{
			CLLNode<TradeData> const* pNode = pList[i]->head();
			if(pNode != NULL)
			{
				TradeableItems data = pNode->m_data.m_eItemType;
				if(data == TRADE_GOLD_PER_TURN)
				{
					iGPTItems++;
					iGPTItemIndex = i;
				}
				else if(data == TRADE_RESOURCES)
					iBonusItems++;
				else bValid = false;
			}
			if(!bValid)
				break;
		}
		if(bValid && iGPTItems == iBonusItems)
		{
			FAssert(iGPTItemIndex >= 0);
			dealsPerPair[civ[iGPTItemIndex]->getID()]
					[civ[(iGPTItemIndex + 1) % 2]->getID()].push(d);
		}
	}
	for(int i = 0; i < MAX_CIV_PLAYERS; i++)
	{
		CvPlayer& civ1 = GET_PLAYER((PlayerTypes)i);
		if(!civ1.isAlive())
			continue;
		for(int j = 0; j < MAX_CIV_PLAYERS; j++)
		{
			if(i >= j) // Process each pair of civs only once
				continue;
			CvPlayer& civ2 = GET_PLAYER((PlayerTypes)j);
			if(!civ2.isAlive())
				continue;
			std::queue<CvDeal*> q1 = dealsPerPair[civ1.getID()][civ2.getID()];
			std::queue<CvDeal*> q2 = dealsPerPair[civ2.getID()][civ1.getID()];
			while(!q1.empty() && !q2.empty())
			{
				AI_foldDeals(*q1.front(), *q2.front());
				q1.pop(); q2.pop();
			}
		}
	}
}

void CvPlayerAI::AI_foldDeals(CvDeal& d1, CvDeal& d2) const
{
	CvPlayer& civ1 = GET_PLAYER(d1.getFirstPlayer());
	CvPlayer& civ2 = GET_PLAYER(d1.getSecondPlayer());
	int gpt1 = -1, gpt2 = -1;
	BonusTypes bonus1 = NO_BONUS, bonus2 = NO_BONUS;
	if(d1.headFirstTradesNode()->m_data.m_eItemType == TRADE_GOLD_PER_TURN)
	{
		gpt1 = d1.headFirstTradesNode()->m_data.m_iData;
		bonus2 = (BonusTypes)d1.headSecondTradesNode()->m_data.m_iData;
	}
	else
	{
		gpt2 = d1.headSecondTradesNode()->m_data.m_iData;
		bonus1 = (BonusTypes)d1.headFirstTradesNode()->m_data.m_iData;
	}
	if(d2.headFirstTradesNode()->m_data.m_eItemType == TRADE_GOLD_PER_TURN)
	{
		if(d2.getFirstPlayer() == d1.getFirstPlayer())
		{
			gpt1 = d2.headFirstTradesNode()->m_data.m_iData;
			bonus2 = (BonusTypes)d2.headSecondTradesNode()->m_data.m_iData;
		}
		else
		{
			gpt2 = d2.headFirstTradesNode()->m_data.m_iData;
			bonus1 = (BonusTypes)d2.headSecondTradesNode()->m_data.m_iData;
		}
	}
	else
	{
		if(d2.getFirstPlayer() == d1.getFirstPlayer())
		{
			gpt2 = d2.headSecondTradesNode()->m_data.m_iData;
			bonus1 = (BonusTypes)d2.headFirstTradesNode()->m_data.m_iData;
		}
		else
		{
			gpt1 = d2.headSecondTradesNode()->m_data.m_iData;
			bonus2 = (BonusTypes)d2.headFirstTradesNode()->m_data.m_iData;
		}
	}
	FAssert(gpt1 > 0 && gpt2 > 0 && bonus1 != NO_BONUS && bonus2 != NO_BONUS);
	int delta = gpt1 - gpt2;
	// Got enough info for the new deal
	d1.killSilent(false, false);
	d2.killSilent(false, false);
	CLinkList<TradeData> give1;
	CLinkList<TradeData> give2;
	give1.insertAtEnd(TradeData(TRADE_RESOURCES, bonus1));
	give2.insertAtEnd(TradeData(TRADE_RESOURCES, bonus2));
	if(delta > 0)
		give1.insertAtEnd(TradeData(TRADE_GOLD_PER_TURN, delta));
	else if(delta < 0)
		give2.insertAtEnd(TradeData(TRADE_GOLD_PER_TURN, -delta));
	// Call counterPropose?
	/*CvDeal* pNewDeal =*/
	GC.getGame().implementAndReturnDeal(civ1.getID(), civ2.getID(), give1, give2, true);
	// Allow new deal to be canceled right away?
	/*if(pNewDeal != NULL)
		pNewDeal->setInitialGameTurn(GC.getGame().getGameTurn() - GC.getPEACE_TREATY_LENGTH());*/
} // </advc.036>


int CvPlayerAI::AI_maxGoldPerTurnTrade(PlayerTypes ePlayer) const
{
	FAssert(ePlayer != getID()); // advc: Some style changes in this function
	/*if (isHuman() || (GET_PLAYER(ePlayer).getTeam() == getTeam()))
		iMaxGoldPerTurn = (calculateGoldRate() + (getGold() / GC.getPEACE_TREATY_LENGTH()));*/
	// <advc.036>
	// Replacing the above:
	if(isHuman())
		return std::max(0, calculateGoldRate());
	// Don't pay gold to our capitulated vassal
	if(GET_TEAM(ePlayer).isVassal(getTeam()) && GET_TEAM(ePlayer).isCapitulated())
		return 0;
	/*  BtS caps the return value at calculateGoldRate, so the getGold()...
		part had no effect. The AI shouldn't make assumptions about human
		finances anyway. Let human use the gold slider to communicate how much
		gpt the AI can ask for in trade proposals. */
	int iAvailableGold = (AI_getAvailableIncome() - getGoldPerTurn() -
			calculateInflatedCosts()) / 3;
	// Included in AvailableIncome, but don't want to divide it by 3.
	iAvailableGold += getGoldPerTurn();
	int iGoldRate = calculateGoldRate();
	iAvailableGold = std::max(std::min(iGoldRate, iGoldRate / 3 +
			(getGold() / GC.getDefineINT(CvGlobals::PEACE_TREATY_LENGTH))), iAvailableGold);
	// </advc.036>
	//availableGold = calculateGoldRate(); // BtS behavior
	// <advc.104w>
	if(getUWAI.isEnabled() && GET_TEAM(ePlayer).isAtWar(getTeam()))
	{
		int r = std::max(0, iAvailableGold);
		return r - (r % 5);
	} // </advc.104w>
	int iMaxGoldPerTurn = getTotalPopulation();
	iMaxGoldPerTurn *= 4; // advc.036
	iMaxGoldPerTurn *= GC.getInfo(getPersonalityType()).
			getMaxGoldPerTurnTradePercent();
	iMaxGoldPerTurn /= 100;
	iMaxGoldPerTurn += std::min(0, getGoldPerTurnByPlayer(ePlayer));
	// <advc.036>
	iMaxGoldPerTurn = AI_adjustTradeGoldToDiplo(iMaxGoldPerTurn, ePlayer);
	int r = ::range(iMaxGoldPerTurn, 0, std::max(0, iAvailableGold));
	// This will only be relevant in the late game
	if(r > 10 * GC.getDefineINT(CvGlobals::DIPLOMACY_VALUE_REMAINDER))
		AI_roundTradeVal(r);
	return r; // </advc.036>
}


int CvPlayerAI::AI_goldPerTurnTradeVal(int iGoldPerTurn) const
{
	int iValue = iGoldPerTurn * GC.getDefineINT(CvGlobals::PEACE_TREATY_LENGTH);
	iValue *= AI_goldTradeValuePercent();
	iValue /= 100;

	return iValue;
}

// (very roughly 4x gold / turn / city)
int CvPlayerAI::AI_bonusVal(BonusTypes eBonus, int iChange, bool bAssumeEnabled,
	bool bTrade) const // advc.036
{
	PROFILE_FUNC(); // advc.opt
	int iValue = 0;
	int iBonusCount = getNumAvailableBonuses(eBonus);
	// <advc.036>
	int iTradeVal = 0;
	// More valuable if we have few resources for trade
	int iSurplus = 0;
	if(!bTrade) /*  Importing resources is not going to give us more resources
					to trade with b/c we can't wheel and deal */
	{
		for(int i = 0; i < GC.getNumBonusInfos(); i++)
			iSurplus += std::max(0, getNumTradeableBonuses((BonusTypes)i) - 1);
	}
	iTradeVal = ::round(4 / std::sqrt((double)std::max(1, std::max(iSurplus,
			2 * (iBonusCount + iChange))))); // </advc.036>
	if(iChange == 0 || (iChange == 1 && iBonusCount == 0) ||
		(iChange == -1 && iBonusCount == 1) ||
		iChange + iBonusCount < 1) // advc.036: Cover all strange cases here
	{
		//This is assuming the none-to-one or one-to-none case.
		iValue += AI_baseBonusVal(eBonus, /* advc.036: */ bTrade);
		iValue += AI_corporationBonusVal(eBonus, /* advc.036: */ bTrade);
		if(!bTrade)
			iValue = std::max(iValue, iTradeVal); // advc.036
		// K-Mod.
		if(!bAssumeEnabled)
		{
			// Decrease the value if there is some tech reason for not having the bonus..
			const CvTeam& kTeam = GET_TEAM(getTeam());
			//if (!kTeam.isBonusRevealed(eBonus))
			// note. the tech is used here as a kind of proxy for the civ's readiness to use the bonus.
			if (!kTeam.isHasTech((TechTypes)GC.getInfo(eBonus).getTechReveal()))
				iValue /= (bTrade ? 3 : 2); // advc.036: was /=2
			if (!kTeam.isHasTech((TechTypes)GC.getInfo(eBonus).getTechCityTrade()))
				iValue /= (bTrade ? 3 : 2); // advc.036: was /=2
		}
		// K-Mod end
	}
	else
	{
		iValue += AI_corporationBonusVal(eBonus, /* advc.036: */ true);
		//This is basically the marginal value of an additional instance of a bonus.
		//iValue += AI_baseBonusVal(eBonus) / 5;
		/*  advc.036: The potential for trades isn't that marginal, and the
			base value (for the first copy of a resource) is unhelpful. */
		iValue = std::max(iValue, iTradeVal);
	}
	return iValue;
}

//Value sans corporation
// (K-Mod note: very vague units. roughly 4x gold / turn / city.)
int CvPlayerAI::AI_baseBonusVal(BonusTypes eBonus, /* advc.036: */ bool bTrade) const  // advc: Some style changes
{
	//recalculate if not defined
	if(!bTrade && // advc.036
		m_aiBonusValue[eBonus] != -1)
	{
		return m_aiBonusValue[eBonus];
	}
	// <advc.036>
	if(bTrade && m_aiBonusValueTrade[eBonus] != -1)
		return m_aiBonusValueTrade[eBonus]; // </advc.036>

	if(GET_TEAM(getTeam()).isBonusObsolete(eBonus))
	{
		m_aiBonusValue[eBonus] = 0;
		m_aiBonusValueTrade[eBonus] = 0; // advc.036
		return m_aiBonusValue[eBonus];
	}
	PROFILE("CvPlayerAI::AI_baseBonusVal::recalculate");
	// <advc.036>
	double dValue = 0; // was int
	int iHappy = GC.getInfo(eBonus).getHappiness();
	int iHealth = GC.getInfo(eBonus).getHealth();
	int iBuildingHappy = 0;
	int iBuildingHealth = 0;
	CvCivilization const& kCiv = getCivilization(); // advc.003w
	for (int i = 0; i < kCiv.getNumBuildings(); i++)
	{
		BuildingTypes eBuilding = kCiv.buildingAt(i);
		int iBuildingClassCount = getBuildingClassCount(kCiv.buildingClassAt(i));
		if(iBuildingClassCount <= 0)
			continue;

		iBuildingHappy += iBuildingClassCount * GC.getInfo(eBuilding).
				getBonusHappinessChanges(eBonus);
		iBuildingHealth += iBuildingClassCount * GC.getInfo(eBuilding).
				getBonusHealthChanges(eBonus);
	}
	if(iBuildingHappy > 0.55 * getNumCities())
		iHappy++;
	if(iBuildingHealth > 0.55 * getNumCities())
		iHealth++;
	// What BtS did:
	/*iValue += (iHappy * 100);
	iValue += (iHealth * 100);*/
	/*  Weight appears to be 100 when a city needs extra happiness or health to grow,
		a bit more when citizens are already angry or food is being lost. Generally
		lower than 100 because not all cities will need happiness or health for growth.
		Resource tiles can generally be worked without extra happiness or health.
		Being able to work an extra non-resource tile should be worth some 10 gpt per city.
		Improvement and building yields increase over the course of the game, but the
		service life of a citizen becomes shorter, the food needed for growth increases
		and the natural tile yields tend to decrease (worst tiles saved for last). Also,
		when a city near the happiness cap receives another luxury, it may still be
		unable to grow for lack of health. Tbd.: Could check whether health or happiness is
		the bottleneck and adjust BonusVal accordingly.
		5 gpt per 1 happiness seems reasonable; can reduce that further in AI_bonusTradeVal.
		There's a division by 10 at the end of this function, and it's supposed to
		return 4 times the gpt per city, so everything still needs to be multiplied by 2: */
	double const scaleFactor = 2.1; // plus a little extra now that ExtraPop1 and ExtraPop2 no longer have equal weight
	/*  The weight functions always assign some value to happiness and health
		beyond what is needed for the current population, but the AI should
		secure bonuses some time before they're needed so that CvCityAI can
		tell when there is room for growth and prioritize food accordingly.
		2 is a bit much though, and 1 too little, so take a weighted avg. of the two. */
	int iExtraPop1 = 1, iExtraPop2 = 2;
	// Don't want iExtraPop to increase resource prices on the whole
	double extraPopFactor = std::max(0.5, 0.1 * (10 - (0.7 * iExtraPop1 +
			0.3 * iExtraPop2)));
	bool bAvailable = (getNumAvailableBonuses(eBonus) > 0);
	if(iHappy > 0)
	{
		/*  advc.912c: Better ignore getLuxuryModifier; don't want civs with a
			luxury modifier to trade for more luxuries, and don't want them to
			pay more more for luxuries. */
		double const civicsMod = 0;//getLuxuryModifier() / 200.0;
		// Considering to cancel a trade
		if(bTrade && bAvailable) { /* HappinessWeight can't handle iHappy=0;
									  increase extra pop instead */
			iExtraPop1 += iHappy;
			iExtraPop2 += iHappy;
		}
		dValue += scaleFactor *
			((0.7 + 0.5 * civicsMod) * AI_getHappinessWeight(iHappy,
			// Look farther ahead when not trading for a resource (trades are fickle)
			bTrade ? iExtraPop1 : iExtraPop2) +
			 (0.3 + 0.5 * civicsMod) * AI_getHappinessWeight(iHappy, iExtraPop2));
	}
	if(iHealth > 0)
	{
		if(bTrade && bAvailable)
		{
			iExtraPop1 += iHealth;
			iExtraPop2 += iHealth;
		}
		/*  Lower multiplier b/c bad health is not as painful as anger; and don't use
			higher iExtraPop when not trading. */
		dValue += (scaleFactor - 0.5) * 0.5 * (AI_getHealthWeight(iHealth, iExtraPop1) +
				AI_getHealthWeight(iHealth, iExtraPop2));
	}
	dValue *= extraPopFactor;
	// </advc.036>
	CvTeam& kTeam = GET_TEAM(getTeam());
	CvCity* pCapital = getCapitalCity();
	int iCityCount = getNumCities();
	int iCoastalCityCount = countNumCoastalCities();

	// find the first coastal city
	CvCity const* pCoastalCity = NULL;
	CvCity const* pUnconnectedCoastalCity = NULL;
	if(iCoastalCityCount > 0)
	{
		FOR_EACH_CITY(pLoopCity, *this)
		{
			if(!pLoopCity->isCoastal())
				continue;
			if(pLoopCity->isConnectedToCapital(getID()))
			{
				pCoastalCity = pLoopCity;
				break;
			}
			else if (pUnconnectedCoastalCity == NULL)
				pUnconnectedCoastalCity = pLoopCity;
		}
	}
	if(pCoastalCity == NULL && pUnconnectedCoastalCity != NULL)
		pCoastalCity = pUnconnectedCoastalCity;

	for (int i = 0; i < kCiv.getNumUnits(); i++)
	{
		UnitTypes eLoopUnit = kCiv.unitAt(i); // advc.003w
		CvUnitInfo& kLoopUnit = GC.getInfo(eLoopUnit);
		/* original bts code
		... */ // advc: deleted
		// K-Mod. Similar, but much better. (maybe)
		int iBaseValue = /* <advc.031> */ 10; // was 30
		int iPowerValue = kLoopUnit.getPowerValue();
		/*  iBaseValue gets multiplied by the number of cities when converted to
			gold per turn, but AI_foundValue does not do this, and a civ that
			never expands beyond 6 cities should still value access to e.g. nukes
			more than to Swordsmen, if only because gold is more readily available
			in the late game. */
		if (iPowerValue > 0)
			iBaseValue = std::max(iBaseValue, ::round(12 * std::pow((double)iPowerValue, 0.5)));
		// </advc.031>
		int iUnitValue = 0;
		if(kLoopUnit.getPrereqAndBonus() == eBonus)
			iUnitValue = iBaseValue;
		else
		{
			int iOrBonuses = 0;
			int iOrBonusesWeHave = 0; // excluding eBonus itself. (disabled for now. See comments below.)
			bool bOrBonus = false; // is eBonus one of the OrBonuses for this unit.
			for(int iJ = 0; iJ < GC.getNUM_UNIT_PREREQ_OR_BONUSES(eLoopUnit); iJ++)
			{
				BonusTypes ePrereqBonus = (BonusTypes)kLoopUnit.getPrereqOrBonuses(iJ);
				if(ePrereqBonus != NO_BONUS)
				{
					iOrBonuses++;
					// advc.036: Uncommented. Should be fine now.
					iOrBonusesWeHave += (ePrereqBonus != eBonus &&
							getNumAvailableBonuses(ePrereqBonus) > 0) ? 1 : 0;
					// @*#!  It occurs to me that using state-dependent stuff such as NumAvailableBonuses here could result in OOS errors.
					// This is because the code here can be trigged by local UI events, and then the value could be cached...
					// It's very frustrating - because including the effect from iOrBonusesWeHave was going to be a big improvment.
					// The only way I can think of working around this is to add a 'bConstCache' argument to this function...
					bOrBonus = bOrBonus || ePrereqBonus == eBonus;
				}
			}
			if(bOrBonus)
			{
				// 1: 1, 2: 2/3, 3: 1/2, ...
				iUnitValue = iBaseValue * 2 / (1+iOrBonuses+2*iOrBonusesWeHave);
			}
		}
		bool bCanTrain = false;
		if(iUnitValue > 0)
		{
			// devalue the unit if we wouldn't be able to build it anyway
			if(canTrain(eLoopUnit))
			{
				bCanTrain = true;
				/*  is it a water unit and no coastal cities or our coastal city
					cannot build because it's obsolete */
				if((kLoopUnit.getDomainType() == DOMAIN_SEA &&
						(pCoastalCity == NULL ||
						pCoastalCity->allUpgradesAvailable(eLoopUnit,
						0, eBonus) // advc.001u
						!= NO_UNIT)) ||
						/*  or our capital cannot build because it's obsolete
							(we can already build all its upgrades) */
						(pCapital != NULL &&
						pCapital->allUpgradesAvailable(eLoopUnit,
						0, eBonus) // advc.001u
						!= NO_UNIT))
					iUnitValue = 0; // its worthless
			}
			else
			{
				// there is some other reason why we can't build it. (maybe the unit is maxed out, or maybe we don't have the techs)
				iUnitValue /= 2;
				/*  <advc.036> Evaluation for trade should be short-term; if we
					can't use it right away, we shouldn't trade for it. */
				if(bTrade)
				{
					/*  This makes sure that we're not very close to the unit.
						Otherwise, a human could e.g. trade for Ivory one turn
						before finishing Construction. */
					TechTypes mainReq = (TechTypes)kLoopUnit.getPrereqAndTech();
					if(!isHuman() || (mainReq != NO_TECH &&
							!kTeam.isHasTech(mainReq) &&
							kTeam.getResearchProgress(mainReq) <= 0))
						iUnitValue /= 3;
				} // </advc.036>
			}
		}
		if(iUnitValue > 0)
		{
			// devalue units for which we already have a better replacement.
			UnitAITypes eDefaultAI = kLoopUnit.getDefaultUnitAIType();
			int iNewTypeValue = AI_unitValue(eLoopUnit, eDefaultAI, 0);
			int iBestTypeValue = AI_bestAreaUnitAIValue(eDefaultAI, 0);
			if(iBestTypeValue > 0)
			{
				int iNewValue = AI_unitValue(eLoopUnit, eDefaultAI, 0);
				iUnitValue = iUnitValue * std::max(0, std::min(100, 120*iNewValue / iBestTypeValue - 20)) / 100;
			}
			// devalue units which are related to our current era. (but not if it is still our best unit!)
			if(kLoopUnit.getPrereqAndTech() != NO_TECH)
			{
				int iDiff = GC.getInfo((TechTypes)(kLoopUnit.getPrereqAndTech())).getEra() - getCurrentEra();
				if (iDiff > 0
					//|| !bCanTrain || iNewTypeValue < iBestTypeValue) {
					/*  advc.036: The assignment below doesn't do anything
						if iDiff is 0, so alternative conditions have no effect.
						"but not if it is still our best unit!" sounds like it
						should be an AND. !bCanTrain is sufficiently penalized above. */
					&& iNewTypeValue < iBestTypeValue)
				{
					/*  advc.031: Replacing the line below b/c Horse was valued
						too highly in the late game */
					iUnitValue = iUnitValue/(1 + std::abs(iDiff));
					//iUnitValue = iUnitValue * 2/(2 + std::abs(iDiff));
				}
			}
			dValue += iUnitValue;
		} // K-Mod end
	}

	for (int i = 0; i < kCiv.getNumBuildings(); i++)
	{
		BuildingTypes eLoopBuilding = kCiv.buildingAt(i); // advc.003w
		BuildingClassTypes eBuildingClass = kCiv.buildingClassAt(i);
		CvBuildingInfo& kLoopBuilding = GC.getInfo(eLoopBuilding);

		int iTempValue = 0;
		if(kLoopBuilding.getPrereqAndBonus() == eBonus)
			iTempValue += 25; // advc.036: was 30

		for(int iJ = 0; iJ < GC.getNUM_BUILDING_PREREQ_OR_BONUSES(); iJ++)
		{
			if (kLoopBuilding.getPrereqOrBonuses(iJ) == eBonus)
				iTempValue += 15; // advc.036: was 20
		}
		iTempValue += kLoopBuilding.getBonusProductionModifier(eBonus) / 10;

		if(kLoopBuilding.getPowerBonus() == eBonus)
			iTempValue += 40; // advc.036: was 60

		FOR_EACH_ENUM(Yield)
		{
			// <advc> Easier to debug this way
			int iYieldMod = kLoopBuilding.getBonusYieldModifier(eBonus, eLoopYield);
			if(iYieldMod > 0) // </advc>
			{
				iTempValue += iYieldMod /
						/*  <advc.036> 25 for Ironworks is normally too much;
							need to take into account that we can build it in
							only one city. Then again, if we have very few cities,
							we may not be able to build it at all ...
							Too tedious to check getPrereqNumOfBuildingClass,
							so just use a constant divisor. */
						(kLoopBuilding.isNationalWonder() ? 5 : 2);
			}
			if (kLoopBuilding.getPowerBonus() == eBonus)
				iTempValue += kLoopBuilding.getPowerYieldModifier(eLoopYield);
		}
		if(iTempValue > 0) // advc.opt
		{
			// determine whether we have the tech for this building
			bool bHasTechForBuilding = true;
			if(!kTeam.isHasTech(kLoopBuilding.getPrereqAndTech()))
				bHasTechForBuilding = false;
			for(int iPrereqIndex = 0; bHasTechForBuilding && iPrereqIndex <
				GC.getNUM_BUILDING_AND_TECH_PREREQS(); iPrereqIndex++)
			{
				if (kLoopBuilding.getPrereqAndTechs(iPrereqIndex) != NO_TECH)
				{
					if (!kTeam.isHasTech(kLoopBuilding.getPrereqAndTechs(iPrereqIndex)))
						bHasTechForBuilding = false;
				}
			}

			bool bStateReligion = (NO_RELIGION != kLoopBuilding.getStateReligion());
			//check if function call is cached
			bool bCanConstruct = canConstruct(eLoopBuilding, false,
					/*bTestVisible*/ true, /*bIgnoreCost*/ true);
			/*  bCanNeverBuild when true is accurate, it may be false in some cases
				where we will never be able to build */
			bool bCanNeverBuild = (bHasTechForBuilding && !bCanConstruct && !bStateReligion);
			// if we can never build this, it is worthless
			if(bCanNeverBuild)
				iTempValue = 0;
			// double value if we can build it right now
			else if(bCanConstruct)
				iTempValue *= 2;
			// <advc.036> Don't trade for the bonus until we need it
			if(bTrade && !bCanConstruct)
				iTempValue = 0; // </advc.036>
			// if non-limited water building, weight by coastal cities
			if(kLoopBuilding.isWater() && !kLoopBuilding.isLimited())
			{
				iTempValue *= iCoastalCityCount;
				iTempValue /= std::max(1, iCityCount/2);
			} // <advc.036>
			int iMaking = getBuildingClassMaking(eBuildingClass);
			if(iMaking > 0)
			{
				iTempValue *= 20; // +100% for wonders, +43% to +100% otherwise.
				if(kLoopBuilding.isLimited())
					iTempValue /= 10;
				else iTempValue /= std::max(10, 15 - iMaking);
			} /* Even if it's from an earlier era, if we're constructing it,
				 then it's apparently useful. */
			else // </advc.036>
				if(kLoopBuilding.getPrereqAndTech() != NO_TECH)
			{
				int iDiff = abs(GC.getInfo(kLoopBuilding.getPrereqAndTech()).getEra() -
						getCurrentEra());
				if(iDiff == 0)
				{
					iTempValue *= 3;
					iTempValue /= 2;
				}
				else iTempValue /= iDiff;
			}
			dValue += iTempValue;
		}
	}

	for(int iI = 0; iI < GC.getNumProjectInfos(); iI++)
	{
		ProjectTypes eProject = (ProjectTypes) iI;
		CvProjectInfo& kLoopProject = GC.getInfo(eProject);
		int iTempValue = 0;
		iTempValue += kLoopProject.getBonusProductionModifier(eBonus) / 10;

		if(iTempValue <= 0)
			continue;

		bool bMaxedOut = (GC.getGame().isProjectMaxedOut(eProject) ||
				kTeam.isProjectMaxedOut(eProject));
		if(bMaxedOut)
			iTempValue = 0;
		else if(canCreate(eProject))
			iTempValue *= 2;

		if(kLoopProject.getTechPrereq() != NO_TECH)
		{
			int iDiff = abs(GC.getInfo((TechTypes)
					kLoopProject.getTechPrereq()).getEra() - getCurrentEra());
			if(iDiff == 0)
			{
				iTempValue *= 3;
				iTempValue /= 2;
			}
			else
			{
				iTempValue /= iDiff;
				// <advc.036>
				if(bTrade)
					iTempValue = 0; // <advc.036>
			}
		}
		dValue += iTempValue;
	}

	RouteTypes eBestRoute = getBestRoute();
	FOR_EACH_ENUM(Build)
	{
		RouteTypes eRoute = GC.getInfo(eLoopBuild).getRoute();
		if(eRoute == NO_ROUTE)
			continue;
		int iTempValue = 0;
		if(GC.getInfo(eRoute).getPrereqBonus() == eBonus)
			iTempValue += 80;
		for(int i = 0; i < GC.getNUM_ROUTE_PREREQ_OR_BONUSES(); i++)
		{
			if(GC.getInfo(eRoute).getPrereqOrBonus(i) == eBonus)
				iTempValue += 40;
		}
		// <advc.036> The usual tech checks
		TechTypes eTech = GC.getInfo(eLoopBuild).getTechPrereq();
		if(eTech != NO_TECH && !GET_TEAM(getTeam()).isHasTech(eTech))
		{
			if(bTrade)
				continue;
			iTempValue /= 2;
			int iTechEra = GC.getInfo(eTech).getEra();
			int iEraDiff = iTechEra - getCurrentEra();
			if(iEraDiff > 0)
				iTempValue /= iEraDiff;
		} // </advc.036>
		if (eBestRoute != NO_ROUTE &&
			/*  advc.opt: Was GC.getInfo(getBestRoute()), which iterates
				through all builds. */
			GC.getInfo(eBestRoute).getValue() <= GC.getInfo(eRoute).getValue())
		{
			dValue += iTempValue;
		}
		else dValue += iTempValue / 2;
	}

	/*int iCorporationValue = AI_corporationBonusVal(eBonus);
	iValue += iCorporationValue;
	if (iCorporationValue <= 0 && getNumAvailableBonuses(eBonus) > 0)
		iValue /= 3;*/

	dValue /= 10;
	// <advc.036>
	int iValue = ::round(dValue);
	iValue = std::max(0, iValue);
	/*  To address karadoc's "@*#!" comment in the middle of this function;
		coupled with a change in AI_updateBonusValue(BonusTypes). */
	if(GC.getGame().isNetworkMultiPlayer())
		return iValue;
	if(!bTrade)
	{
		m_aiBonusValue[eBonus] = iValue;
		return iValue;
	}
	m_aiBonusValueTrade[eBonus] = iValue;
	return iValue; // </advc.036>
}

int CvPlayerAI::AI_corporationBonusVal(BonusTypes eBonus, /* advc.036: */ bool bTrade) const
{
	int iValue = 0;
	int iCityCount = getNumCities();
	iCityCount += iCityCount / 6 + 1;

	for (int iCorporation = 0; iCorporation < GC.getNumCorporationInfos(); ++iCorporation)
	{
		int iCorpCount = getHasCorporationCount((CorporationTypes)iCorporation);
		if (iCorpCount > 0)
		{	/*  <advc.036> Human could spread the corp rapidly. I also want to push
				the price for humans a bit in order to stop the AI from offering 1-ofs
				as soon as a player founds a corporation. */
			if(bTrade && isHuman())
				iCorpCount = std::min(getNumCities(), ::round(1 + (iCorpCount * 4/3.0)));
			// </advc.036>
			iCorpCount += getNumCities() / 6 + 1;
			CvCorporationInfo& kCorp = GC.getInfo((CorporationTypes)iCorporation);
			for (int i = 0; i < GC.getNUM_CORPORATION_PREREQ_BONUSES(); ++i)
			{
				if (eBonus == kCorp.getPrereqBonus(i))
				{
					iValue += (50 * kCorp.getYieldProduced(YIELD_FOOD) * iCorpCount) / iCityCount;
					iValue += (50 * kCorp.getYieldProduced(YIELD_PRODUCTION) * iCorpCount) / iCityCount;
					iValue += (30 * kCorp.getYieldProduced(YIELD_COMMERCE) * iCorpCount) / iCityCount;

					iValue += (30 * kCorp.getCommerceProduced(COMMERCE_GOLD) * iCorpCount) / iCityCount;
					iValue += (30 * kCorp.getCommerceProduced(COMMERCE_RESEARCH) * iCorpCount) / iCityCount;
					//iValue += (12 * kCorp.getCommerceProduced(COMMERCE_CULTURE) * iCorpCount) / iCityCount;
					// K-Mod, I'd love to calculate this stuff properly, but because of the way trade currently operates...
					iValue += (20 * kCorp.getCommerceProduced(COMMERCE_CULTURE) * iCorpCount) / iCityCount;
					iValue += (20 * kCorp.getCommerceProduced(COMMERCE_ESPIONAGE) * iCorpCount) / iCityCount;

					//Disabled since you can't found/spread a corp unless there is already a bonus,
					//and that bonus will provide the entirety of the bonusProduced benefit.
					/*if (NO_BONUS != kCorp.getBonusProduced()) {
						if (getNumAvailableBonuses((BonusTypes)kCorp.getBonusProduced()) == 0)
							iBonusValue += (1000 * iCorpCount * AI_baseBonusVal((BonusTypes)kCorp.getBonusProduced())) / (10 * iCityCount);
					}*/
				}
			}
		}
	}

	/*iValue /= 100;	//percent
	iValue /= 10;	//match AI_baseBonusVal
	return iValue;*/
	// advc.036: To increase accuracy
	return ::round(iValue / 1000.0);
}

// advc.036:
int CvPlayerAI::AI_goldForBonus(BonusTypes eBonus, PlayerTypes eBonusOwner) const
{
	FAssert(isHuman() != GET_PLAYER(eBonusOwner).isHuman());
	return AI_tradeValToGold(AI_bonusTradeVal(eBonus, eBonusOwner, 1), isHuman(),
			isHuman() ? MAX_INT : AI_maxGoldPerTurnTrade(eBonusOwner));
}

// advc.036 (rewritten): Computes how much this CvPlayer is willing to pay to eFromPlayer for eBonus
int CvPlayerAI::AI_bonusTradeVal(BonusTypes eBonus, PlayerTypes eFromPlayer, int iChange,
	bool bExtraHappyOrHealth) const
{
	PROFILE_FUNC();
	FAssert(eFromPlayer != getID());
	bool bUseOurBonusVal = true;
	if(isHuman())
	{
		/*  If this CvPlayer is human and ePlayer an AI, then this function
			needs to say how much the human values eBonus in FromPlayer's
			estimation. Can't just use this->AI_bonusVal b/c this uses
			information that FromPlayer might not have. */
		CvBonusInfo& kBonus = GC.getInfo(eBonus);
		// I can live with cheating when it comes to strategic bonuses
		bUseOurBonusVal = (kBonus.getHappiness() == 0 && kBonus.getHealth() == 0);
		// Also don't worry about the value of additional copies (for corps)
		if(!bUseOurBonusVal && getNumAvailableBonuses(eBonus) + iChange >
			(iChange < 0 ? 0 : 1))
		{
			bUseOurBonusVal = true;
		}
		// Otherwise FromPlayer needs to know most of the human's territory
		if(!bUseOurBonusVal)
		{
			int iRevThresh = 2 * getNumCities() / 3;
			int iRevCount = 0;
			TeamTypes eFromTeam = TEAMID(eFromPlayer);
			FOR_EACH_CITY(c, *this)
			{
				if(c->isRevealed(eFromTeam))
				{
					iRevCount++;
					if(iRevCount >= iRevThresh)
					{
						bUseOurBonusVal = true;
						break;
					}
				}
			}
		}
	}
	CvPlayerAI const& kFromPlayer = GET_PLAYER(eFromPlayer);
	double ourVal = bUseOurBonusVal ? AI_bonusVal(eBonus, iChange, false, true) :
			// Use FromPlayer's value as a substitute
			kFromPlayer.AI_bonusVal(eBonus, 0, false, true);
	if(!isHuman() && kFromPlayer.isHuman())
	{
		/*  For now, only address the case of a human civ receiving two health
			resources or two happiness resources from an AI civ (and cancellation
			of such a deal). More than 2 is ruled out by the caller. */
		if(bExtraHappyOrHealth && iChange > 0)
			ourVal *= 0.75;
		if(bExtraHappyOrHealth && iChange < 0)
			ourVal /= 0.75;
	}
	ourVal *= getNumCities(); // bonusVal is per city
	/*  Don't pay fully b/c trade doesn't give us permanent access to the
		resource, and b/c it tends to be (and should be) a buyer's market. */
	if (ourVal > 0)
		ourVal = std::pow(ourVal, 0.7);
	/*  What else could FromPlayer do with the resource? Trade it to somebody else.
		I'm assuming that trade denial has already filtered out resources that
		ePlayer (really) needs domestically. */
	int iCities = 0;
	int iMetCivs = 0;
	int iOtherTakers = 0;
	/*  Can't check here if eBonus is strategic; use the strategic thresh minus 1
		as a compromise. */
	int iRefuseThresh = std::max(0, GC.getInfo(kFromPlayer.getPersonalityType()).
			getStrategicBonusRefuseAttitudeThreshold() - 1);
	for(int i = 0; i < MAX_CIV_PLAYERS; i++)
	{
		CvPlayerAI const& kLoopPlayer = GET_PLAYER((PlayerTypes)i);
		if(!kLoopPlayer.isAlive() || !GET_TEAM(getTeam()).isHasMet(kLoopPlayer.getTeam()) ||
			!GET_TEAM(eFromPlayer).isHasMet(kLoopPlayer.getTeam()))
		{
			continue;
		}
		/*  The trade partners don't necessarily know all those cities, but the
			city count wouldn't be difficult to estimate. */
		iCities += kLoopPlayer.getNumCities();
		iMetCivs++;
		if(kLoopPlayer.getID() == eFromPlayer || kLoopPlayer.getID() == getID())
			continue;
		/*  Based on a profiler test, too slow:
			(maybe b/c of the is-strategic check in AI_bonusTrade) */
		/*TradeData item;
		item.m_eItemType = TRADE_RESOURCES;
		item.m_iData = eBonus;
		if(kFromPlayer.canTradeItem(kLoopPlayer.getID(), item, true))
			otherTakers++;*/
		// Also a little slow b/c of advc.124
		//if(!kFromPlayer.canTradeNetworkWith(kLoopPlayer.getID())
		// Checking just the essentials is fine though; it's only a heuristic.
		if((kLoopPlayer.getCapitalCity() != NULL &&
			!kLoopPlayer.getCapitalCity()->isConnectedToCapital(eFromPlayer)) ||
			kLoopPlayer.getNumAvailableBonuses(eBonus) > 0)
		{
			continue;
		}
		if(!kFromPlayer.isHuman() && kFromPlayer.AI_getAttitude(
			kLoopPlayer.getID()) <= iRefuseThresh)
		{
			continue;
		}
		if(!kLoopPlayer.isHuman() && kLoopPlayer.AI_getAttitude(eFromPlayer) <=
			std::max(0, GC.getInfo(kLoopPlayer.getPersonalityType()).
			getStrategicBonusRefuseAttitudeThreshold() - 1))
		{
			continue;
		}
		iOtherTakers++;
	}
	int iAvail = kFromPlayer.getNumAvailableBonuses(eBonus);
	// Decrease marketVal if we have multiple spare copies
	iOtherTakers = ::range(iOtherTakers - iAvail + 2, 0, iOtherTakers);
	FAssert(iMetCivs >= 2);
	/*  Hard to say how much a third party needs eBonus, but it'd certainly
		factor in its number of cities. */
	double marketVal = (5.0 * iCities) / (3.0 * iMetCivs) +
			std::sqrt((double)iOtherTakers);
	double const marketWeight = 2/3.0;
	/*  Want luxuries and food resources to be cheap, but not necessarily
		strategic resources. At 1/3 weight for ourVal, crucial strategics
		are still going to be expensive. */
	double r = marketWeight * marketVal + (1 - marketWeight) * ourVal;
	/*  Trade denial already ensures that ePlayer doesn't really need the
		resource. Don't want to factor in ePlayer's AI_bonusVal; just a little
		nudge to make sure that the AI doesn't offer unneeded nonsurplus resources at
		the same price as surplus resources. Exception for human b/c I don't
		want humans to keep their surplus resources and sell unneeded nonsurplus
		resources instead for a slightly higher price. */
	if(!kFromPlayer.isHuman() && (iAvail - iChange == 0 ||
		iAvail == 0 || // when considering cancelation
		kFromPlayer.AI_corporationBonusVal(eBonus, true) > 0))
	{
		r++;
	}
	if(!isHuman()) // Never pay more than it's worth to us
		r = std::min(r, ourVal);
	r *= std::max(0, GC.getInfo(eBonus).getAITradeModifier() + 100) / 100.0;
	if(GET_TEAM(eFromPlayer).isVassal(getTeam()) && !GET_TEAM(eFromPlayer).isCapitulated())
		r *= 0.67; // advc.037: 0.5 in BtS
	int iR = ::round(r);
	/*  To make resource vs. resource trades more compatible. A multiple of 5
		would lead to a rounding error when gold is paid for a resource b/c
		2 gpt correspond to 1 tradeVal. */
	if(r >= 3 && !GET_TEAM(getTeam()).isGoldTrading() &&
		!GET_TEAM(eFromPlayer).isGoldTrading())
	{
		iR = ::roundToMultiple(iR, 4);
	}
	return iR *  GC.getDefineINT(CvGlobals::PEACE_TREATY_LENGTH);
}


DenialTypes CvPlayerAI::AI_bonusTrade(BonusTypes eBonus, PlayerTypes eToPlayer,
	int iChange) const // advc.133
{
	PROFILE_FUNC();

	CvPlayerAI const& kPlayer = GET_PLAYER(eToPlayer); // advc
	FAssertMsg(eToPlayer != getID(), "shouldn't call this function on ourselves");

	if (isHuman() && kPlayer.isHuman())
		return NO_DENIAL;

	/*  <advc.036> No wheeling and dealing, not even when it involves
		vassal/master or team mates. */
	if(iChange > 0 && kPlayer.getBonusExport(eBonus) + getBonusImport(eBonus) > 0)
	{
		/*  So that AI_buildTradeTable filters these out (b/c it's not easy
			to see when the AI is already exporting a resource; can be puzzling). */
		return DENIAL_JOKING;
	} // </advc.036>
	// advc.133:
	int iAvailThem = kPlayer.getNumAvailableBonuses(eBonus);
	// advc.036: Moved this clause up
	if (iAvailThem + iChange > 1 && kPlayer.AI_corporationBonusVal(eBonus, true) <= 0)
		return DENIAL_JOKING;
	// <advc.037> This used to apply to all vassals
	bool bVassal = GET_TEAM(getTeam()).isVassal(TEAMID(eToPlayer));
	if(bVassal && GET_TEAM(getTeam()).isCapitulated()) // </advc.037>
		return NO_DENIAL;

	if (::atWar(getTeam(), TEAMID(eToPlayer)))
	{	/*  advc.036 (comment): Doesn't help b/c there is no trade network while
			at war, and no feasible way to ensure that it will exist after
			making peace. */
		return NO_DENIAL;
	}
	//if (GET_PLAYER(ePlayer).getTeam() == getTeam())
	// advc.155: Commented out
	/*if (TEAMID(ePlayer) == getTeam() && kPlayer.isHuman()) // K-Mod
		return NO_DENIAL;*/
	// K-Mod (The above case should be tested for humans trying to give stuff to AI teammates
	// - otherwise the human won't know if the AI can actually use the resource.)
	/*if (TEAMID(ePlayer) == getTeam())
		return NO_DENIAL;*/
	// K-Mod end
	// <advc.036>
	int const iTradeValThresh = 3;
	int iValueForThem = -1;
	/*  advc.133: Don't bother checking the value-based clauses when considering
		cancelation. AI_considerOffer handles that. */
	if(iChange >= 0)
	{
		iValueForThem = kPlayer.AI_bonusVal(eBonus, iChange, false, true);
	// </advc.036>
		if (isHuman()
				/*  advc.036: No deal if they (AI) don't need the resource, but
					check attitude before giving the human that info. */
				&& iValueForThem >= iTradeValThresh)
			return NO_DENIAL;
	}
	if (GET_TEAM(getTeam()).AI_getWorstEnemy() == TEAMID(eToPlayer))
		return DENIAL_WORST_ENEMY;
	// advc.036: Commented out
	/*if (AI_corporationBonusVal(eBonus) > 0)
		return DENIAL_JOKING;*/

	bool bStrategic = false;
	bool bCrucialStrategic = false; // advc.036

	CvCity* pCapitalCity = getCapitalCity();
	FOR_EACH_ENUM2(Unit, eUnit)
	{
		if (GC.getInfo(eUnit).getPrereqAndBonus() == eBonus)
		{
			bStrategic = true;
			bCrucialStrategic = true; // advc.036
		}
		for (int i = 0; i < GC.getNUM_UNIT_PREREQ_OR_BONUSES(eUnit); i++)
		{
			if (GC.getInfo(eUnit).getPrereqOrBonuses(i) == eBonus)
			{
				bStrategic = true;
				/*  Could check if we have one of the alternative OR-prereqs
					(getCapitalCity()->hasBonus(...), but then a human could
					try to trade for both OR-prereqs at once (e.g. Copper and
					Iron, leaving us unable to train Axemen). */
				bCrucialStrategic = true;
			}
		}
		// <advc.001> Fuyu, Better AI: Strategic For Current Era, 22.07.2010
		// disregard obsolete units
		if (pCapitalCity != NULL && pCapitalCity->allUpgradesAvailable(eUnit) != NO_UNIT)
			continue; // </advc.001>
	}
	if(!isHuman() // advc.036: No longer guaranteed (b/c of the tradeValThresh clause)
		&& !bVassal) // advc.037
	{
		if(!bStrategic) // advc.opt
		{
			FOR_EACH_ENUM(Building)
			{
				if (GC.getInfo(eLoopBuilding).getPrereqAndBonus() == eBonus)
					bStrategic = true;
				for (int i = 0; i < GC.getNUM_BUILDING_PREREQ_OR_BONUSES(eLoopBuilding); i++)
				{
					if (GC.getInfo(eLoopBuilding).getPrereqOrBonuses(i) == eBonus)
						bStrategic = true;
				} // <advc.opt>
				if(bStrategic)
					break; // </advc.opt>
			} // XXX marble and stone???
		}
		AttitudeTypes eAttitude = AI_getAttitude(eToPlayer);
		if (bStrategic)
		{	// <advc.036>
			int iRefusalThresh = GC.getInfo(getPersonalityType()).
					getStrategicBonusRefuseAttitudeThreshold();
			if(iAvailThem - kPlayer.getBonusImport(eBonus) > 0)
				iRefusalThresh--;
			if(eAttitude <= iRefusalThresh) // </advc.036>
				return DENIAL_ATTITUDE;
		}
		if (GC.getInfo(eBonus).getHappiness() > 0)
		{	// advc.036: Treat Ivory as non-crucial
			bCrucialStrategic = false;
			if (eAttitude <= GC.getInfo(getPersonalityType()).getHappinessBonusRefuseAttitudeThreshold())
				return DENIAL_ATTITUDE;
		}
		if (GC.getInfo(eBonus).getHealth() > 0)
		{
			bCrucialStrategic = false; // advc.036
			if (eAttitude <= GC.getInfo(getPersonalityType()).getHealthBonusRefuseAttitudeThreshold())
				return DENIAL_ATTITUDE;
		}
	} // <advc.036>
	int iAvailUs = getNumAvailableBonuses(eBonus);
	/*  Doesn't seem necessary after all; the iValueForUs clauses rule out such
		trades anyway. By returning DENIAL_JOKING, crucial strategic resources
		could be excluded from the trade table (see changes in buildTradeTable),
		but it's perhaps confusing to show some non-surplus resources on the
		trade table and not others. */
	/*if(!isHuman() && bCrucialStrategic && iAvailUs - iChange <= 1 && (!bVassal ||
			iAvailThem + iChange > 1))
		return DENIAL_JOKING;
	*/// </advc.036>
	// <advc.133> See the previous 133-comment
	if(iChange < 0)
		return NO_DENIAL; // </advc.133>
	// <advc.036>
	if(!kPlayer.isHuman())
	{
		/*  Allow human to import resources that they probably don't need,
			but make sure that the AI doesn't. */
		if(iValueForThem < iTradeValThresh)
			return DENIAL_NO_GAIN;
		if(isHuman())
			return NO_DENIAL;
	}
	int iValueForUs = AI_corporationBonusVal(eBonus, true);
	/*  This guard ensures that iTradeVal isn't added in AI_bonusVal (is that really
		important?). The max isn't actually necessary; we know that iChange>=0. */
	if(iAvailUs - getBonusImport(eBonus) - std::max(0, iChange) <= 0)
		iValueForUs = AI_bonusVal(eBonus, 0, false, true);
	/*  Don't want city counts to matter much b/c large civs are supposed to
		export to small (tall) ones. But selling to tiny civs often doesn't make
		sense; could get a much better deal from some larger buyer. */
	iValueForUs += std::min(2 * iValueForUs,
			getNumCities() / std::max(1, kPlayer.getNumCities()));
	// <advc.037>
	if(bVassal)
	{
		if(iValueForUs >= 2 * (kPlayer.isHuman() ? iTradeValThresh : iValueForThem))
			return DENIAL_NO_GAIN;
		else return NO_DENIAL;
	} // </advc.037>
	// Replacing the JOKING clause further up
	if(kPlayer.isHuman() && (iAvailThem + iChange <= 1 ? /*  Don't presume value
			for human unless human only needs the resource for a corp */
			(iValueForUs >= iTradeValThresh +
			/*  bonusVal gives every city equal weight, but early on,
				it's mostly about the capital, which can grow fast. */
			std::min(2, (getNumCities() - 1) / 2)) :
			(3 * iValueForUs >= 2 * iValueForThem ||
			iValueForThem <= 0 ||
			(iValueForUs > 0 && iValueForThem - iValueForUs < iTradeValThresh) ||
			iValueForUs > iTradeValThresh + 2)))
		return DENIAL_NO_GAIN;
	// </advc.036>
	return NO_DENIAL;
}

/*  K-Mod note: the way this function is currently used is that it actually represents
	how much the current owner values _not giving the city to this player_.
	For example, if this player currently controls most of the city's culture,
	the value should be _lower_ rather than higher, so that the current owner
	is more likely to give up the city.
	Ideally the value of receiving the city and the cost of giving the city away would be
	separate things; but that's currently not how trades are made. */
/*  advc.ctr: Now this function computes three different things depending on the
	parameters.
	1)  If eToPlayer is this player: How much this player values acquiring kCity
		from its current owner.
	2)  If eToPlayer is a different player: How much this player values holding
		onto kCity and not giving it to eToPlayer.
	3)	If eToPlayer is NO_PLAYER: The sum of 1) and 2). That's useful for
		balancing out trade deals - which is the main purpose of this function.
	I'm keeping it in one function b/c all three require similar computations. */
int CvPlayerAI::AI_cityTradeVal(CvCityAI const& kCity, // advc.003u: param was CvCity*
	// <advc.ctr>
	PlayerTypes eToPlayer) const
{
	PROFILE_FUNC(); // (to be profiled)
	if (eToPlayer == NO_PLAYER)
	{
		return AI_cityTradeVal(kCity, getID()) +
				GET_PLAYER(kCity.getOwner()).AI_cityTradeVal(kCity, getID());
	}
	PlayerTypes const eOwner = kCity.getOwner();
	FAssert(eToPlayer != eOwner);
	CvPlayerAI const& kToPlayer = GET_PLAYER(eToPlayer);
	//bool bKeep = (eToPlayer != getID()); // Case 2) in the comment on top
	// </advc.ctr>
	CvGame const& g = GC.getGame(); // advc
	int iValue = 300;

	//iValue += (kCity.getPopulation() * 50); // advc: Call this iPopValue
	int iPopValue = kCity.getPopulation() * 20 + kCity.getHighestPopulation() * 30; // K-Mod
	iValue += iPopValue;

	iValue += 200 * kCity.getCultureLevel();
	iValue += (200 * kCity.getCultureLevel() * kCity.getCulture(getID())) /
			std::max(1, kCity.getCulture(eOwner)
			/*  advc.001: I think karadoc's intention was to add at most
				200 * CultureLevel, namely when the city owner has 0 culture,
				but without the addition below, the increment would actually be
				200 * CultureLevel * this player's city culture -- way too much. */
			+ kCity.getCulture(getID()));

	//iValue += (((((kCity.getPopulation() * 50) + g.getElapsedGameTurns() + 100) * 4) * kCity.getPlot().calculateCulturePercent(eOwner)) / 100);
	// K-Mod
	int iCityTurns = g.getGameTurn() - (kCity.getGameTurnFounded() +
			kCity.getGameTurnAcquired()) / 2;
	iCityTurns = iCityTurns * GC.getInfo(g.getGameSpeedType()).
			getVictoryDelayPercent() / 100;
	iValue += ((iPopValue + iCityTurns * 3 / 2 + 80) * 4 * (kCity.getPlot().
			calculateCulturePercent(/*eOwner*/eToPlayer) // advc.ctr
			+ 10)) / 110;
	// K-Mod end

	for (CityPlotIter it(kCity); it.hasNext(); ++it)
	{
		CvPlot const& kPlot = *it;
		/*if (pLoopPlot->getBonusType(getTeam()) != NO_BONUS)
			iValue += (AI_bonusVal(pLoopPlot->getBonusType(getTeam()), 1, true) * 10);*/ // BtS
		// K-Mod. Use average of our value for gaining the bonus, and their value for losing it.
		int iBonusValue = 0;
		if (kPlot.getBonusType(getTeam()) != NO_BONUS)
		{
			iBonusValue += /* advc.ctr: */ kToPlayer.
					AI_bonusVal(kPlot.getBonusType(getTeam()), 1, true);
		}
		// advc.ctr: That's (mostly) handled by the recursive call upfront now
		/*if (pLoopPlot->getBonusType(TEAMID(eOwner)) != NO_BONUS) {
			iBonusValue += GET_PLAYER(eOwner).AI_bonusVal(
					pLoopPlot->getBonusType(TEAMID(eOwner)), -1, true);
		}*/
		iBonusValue *= plotDistance(&kPlot, kCity.plot()) <= 1 ? 5 : 4;
		iValue += iBonusValue;
		// K-Mod end
	}

	if (!kCity.isEverOwned(getID()))
	{
		iValue *= 3;
		iValue /= 2;
	}

	/*  advc.104d: Moved the K-Mod code here into a new function
		cityWonderVal (with some tweaks). */
	iValue += AI_cityWonderVal(kCity) * 10;
	// <advc.139>
	if(kCity.AI_isEvacuating())
		iValue = ::round(0.33 * iValue); // </advc.139>
	return GET_TEAM(getTeam()).AI_roundTradeVal(iValue); // advc.104k
}


DenialTypes CvPlayerAI::AI_cityTrade(CvCityAI const& kCity, PlayerTypes eToPlayer) const // advc: refactored; advc.003u: param was CvCity*
{
	PROFILE_FUNC(); // advc.ctr (to be profiled)
	FAssert(kCity.getOwner() == getID());
	CvPlayerAI const& kToPlayer = GET_PLAYER(eToPlayer); // advc: Renamed from kPlayer/ ePlayer
	CvPlot const& kCityPlot = *kCity.plot();
	/*if(pCity->getLiberationPlayer(false) == ePlayer)
		return NO_DENIAL;*/
	// advc.ctr: Not so fast
	bool const bLib = (kCity.getLiberationPlayer(false, getTeam()) == eToPlayer);
	if (!bLib && !kToPlayer.isHuman() && kToPlayer.getTeam() != getTeam() &&
		!kCity.isEverOwned(eToPlayer))
	{
		// <advc.ctr> "We don't want to trade this" seems appropriate
		if(GET_PLAYER(eToPlayer).AI_isAwfulSite(kCity)) // Tbd.: AI_cityTradeVal should handle this
			return DENIAL_UNKNOWN; // </advc.ctr>
		// advc.ctr: Now covered by CvPlayer::canTradeItem
		/*if(kCityPlot.calculateCulturePercent(ePlayer) <= 0 && kPlayer.getNumCities() > 3) {
			if(kPlayer.AI_isFinancialTrouble())
				return DENIAL_UNKNOWN;
			CvCity* pNearestCity = GC.getMap().findCity(kCityPlot.getX(), kCityPlot.getY(), ePlayer,
					NO_TEAM, true, false, NO_TEAM, NO_DIRECTION, &kCity);
			if(pNearestCity == NULL || plotDistance(kCityPlot.getX(), kCityPlot.getY(),
					pNearestCity->getX(), pNearestCity->getY()) > 9)
				return DENIAL_UNKNOWN;
		}*/
	}
	if (isHuman())
		return NO_DENIAL;

	/*if(kPlayer.getTeam() == getTeam()) {
		if(pCity->calculateCulturePercent(getID()) > 50)
			return DENIAL_TOO_MUCH;
		return NO_DENIAL;
	}*/ // <advc.ctr> No special treatment for same team
	// Vassal-related conditions
	CvTeam const& kToTeam = GET_TEAM(kToPlayer.getTeam());
	CvTeam const& kOurTeam = GET_TEAM(getTeam());
	int const iFREE_VASSAL_POPULATION_PERCENT = GC.getDefineINT(CvGlobals::FREE_VASSAL_POPULATION_PERCENT);
	if (kOurTeam.isCapitulated())
	{
		if (getMasterTeam() != kToPlayer.getTeam())
			return DENIAL_VASSAL;
		if (!bLib && iFREE_VASSAL_POPULATION_PERCENT > 0)
		{
			// Make sure not to ruin our chances of breaking away (don't bother checking getTotalLand)
			int iMasterPop = (kOurTeam.isVassal(kToTeam.getID()) ?
					kToTeam.getTotalPopulation(false) + kCity.getPopulation() :
					GET_TEAM(getMasterTeam()).getTotalPopulation(false));
			if ((kOurTeam.getTotalPopulation(false) - kCity.getPopulation()) * 100 < iMasterPop)
				return DENIAL_TOO_MUCH;
		}
	}
	if (kToTeam.isCapitulated() && kToTeam.getMasterTeam() == getMasterTeam() &&
		iFREE_VASSAL_POPULATION_PERCENT > 0)
	{	// Don't (somehow) accidentally help free a non-rival capitulated vassal
		int iMasterPop = (kToTeam.isVassal(getTeam()) ?
				kOurTeam.getTotalPopulation(false) - kCity.getPopulation() :
				GET_TEAM(kToTeam.getMasterTeam()).getTotalPopulation(false));
		if ((kToTeam.getTotalPopulation(false) + kCity.getPopulation()) * 100 >= iMasterPop)
			return DENIAL_POWER_YOU;
	}

	bool const bWar = ::atWar(getTeam(), kToPlayer.getTeam());

	// Trade value conditions
	if (!bLib)
	{
		// Be more polite to non-rivals
		DenialTypes eNever = (getMasterTeam() == kToPlayer.getMasterTeam() ?
				DENIAL_TOO_MUCH : DENIAL_NEVER);
		int const iKeepCityVal = AI_cityTradeVal(kCity, eToPlayer);
		// This could lead to trouble when the capital is in disorder. Alternatives?
		int const iReferenceVal = AI_cityTradeVal(getCapitalCity()->AI(), eToPlayer);
		if (!bWar && 7 * iKeepCityVal > 4 * iReferenceVal)
			return eNever;
		if (getTeam() != kToPlayer.getTeam())
		{
			if (iKeepCityVal < 0)
				return DENIAL_UNKNOWN;
		}
	}

	if (bWar)
		return NO_DENIAL;

	// Don't delay victory
	if (getTeam() != kToPlayer.getTeam())
	{
		if (AI_atVictoryStage(AI_VICTORY_DOMINATION3))
			return DENIAL_VICTORY;
		if ((AI_atVictoryStage(AI_VICTORY_SPACE3) && kCity.isProductionProject()) ||
				// In part, to avoid revealing all the project sites in the endgame.
				(AI_atVictoryStage(AI_VICTORY_SPACE4) &&
				4 * kCity.getYieldRate(YIELD_PRODUCTION) > getCapitalCity()->getYieldRate(YIELD_PRODUCTION)))
			return DENIAL_VICTORY;
	}

	// Attitude-based conditions
	if (GET_TEAM(getTeam()).AI_getWorstEnemy() == kToPlayer.getTeam())
		return DENIAL_WORST_ENEMY;
	int iAttitudeThresh = AI_cityTradeAttitudeThresh(kCity, eToPlayer, bLib);
	int iForcedAttitude = AI_getAttitude(eToPlayer);
	/*  <advc.130v> Use the true attitude of capitulated vassals - unless its higher
		than the forced attitude. Not sure if capitulated vassals would otherwise
		give away cities. Not to rivals certainly - that's already addressed above.  */
	int iTrueAttitude = AI_getAttitude(eToPlayer, false);
	int iAttitude = std::min(iTrueAttitude, iForcedAttitude);
	// Let the player know when displayed (forced) attitude is misleading
	if (iAttitude <= iAttitudeThresh && iForcedAttitude > iAttitudeThresh)
		return DENIAL_TRUE_ATTITUDE; // </advc.130v>
	else if (iAttitudeThresh >= ATTITUDE_FRIENDLY)
	{	// Trade with non-rivals if Friendly
		if (getMasterTeam() == kToTeam.getMasterTeam())
			iAttitudeThresh = ATTITUDE_PLEASED;
		else return DENIAL_NEVER;
	}
	if (iAttitude <= iAttitudeThresh)
		return DENIAL_ATTITUDE;

	// Temporary obstacles
	if (!kToPlayer.isHuman())
	{
		// Don't accept cities with resistance from third-party culture
		if (!bLib && kCity.isOccupation() && kCity.getPlot().findHighestCulturePlayer() != eToPlayer)
			return DENIAL_RECENT_CANCEL;
		/*  Should perhaps instead check if there is _any_ danger; not sure how
			likely it is that the new owner has defensive units nearby ... */
		int iThirdPartyAttack = kToPlayer.AI_localAttackStrength(&kCityPlot, NO_TEAM);
		if (iThirdPartyAttack > 0)
		{
			int iToPlayerDefense = kToPlayer.AI_localDefenceStrength(&kCityPlot,
					kToPlayer.getTeam(), DOMAIN_LAND, 1);
			if (iToPlayerDefense < 2 * iThirdPartyAttack)
				return DENIAL_POWER_THEM;
		}
	}
	// </advc.ctr>
	return NO_DENIAL;
}

/*  <advc.ctr> For ceding kCity to eToPlayer, the attitude level of this AI player
	needs to greater than the return value. */
AttitudeTypes CvPlayerAI::AI_cityTradeAttitudeThresh(CvCity const& kCity,
	PlayerTypes eToPlayer, bool bLiberate) const
{
	if (bLiberate)
		return ATTITUDE_FURIOUS;
	CvLeaderHeadInfo const& kPersonality = GC.getInfo(getPersonalityType());
	int r = (kCity.getPlot().calculateCulturePercent(getID()) >=
			GC.getDefineINT(CvGlobals::CITY_TRADE_CULTURE_THRESH) ? 
			kPersonality.getNativeCityRefuseAttitudeThreshold() :
			kPersonality.getCityRefuseAttitudeThreshold());
	return (AttitudeTypes)r;
} // </advc.ctr>

/*  advc (comment): This player pays for the embargo and ePlayer stops trading
	with eTradeTeam. */
int CvPlayerAI::AI_stopTradingTradeVal(TeamTypes eTradeTeam, PlayerTypes ePlayer,
	bool bWarTrade) const // advc.104o  // advc: style changes
{
	FAssert(ePlayer != getID());
	FAssert(TEAMID(ePlayer) != getTeam());
	FAssert(eTradeTeam != getTeam());
	FAssert(GET_TEAM(eTradeTeam).isAlive());
	FAssert(!::atWar(eTradeTeam, TEAMID(ePlayer)));

	if(GET_TEAM(ePlayer).isCapitulated() && GET_TEAM(ePlayer).isVassal(getTeam()))
		return 0; // </advc.130f>
	int iValue = 50 + GC.getGame().getGameTurn() / 2;
	//iValue += GET_TEAM(eTradeTeam).getNumCities() * 5;
	// <advc.130f>
	iValue += std::max(GET_TEAM(getTeam()).getNumCities(),
			GET_TEAM(eTradeTeam).getNumCities()) * 4;
	int iModifier = 0;
	if(!bWarTrade)
	{
		int iPowRatioPercent = (100 * GET_TEAM(eTradeTeam).getPower(true)) /
				(GET_TEAM(ePlayer).getPower(false) +
				(GET_TEAM(eTradeTeam).isAtWar(getTeam()) ?
				GET_TEAM(getTeam()).getPower(false) / 4 : 0));
		if (iPowRatioPercent > 0)
			iModifier += ::round(std::pow((double)iPowRatioPercent, 1.9) / 100);
	}
	int iAlive = GC.getGame().countCivPlayersAlive();
	iModifier += 200 / iAlive - 10;
	AttitudeTypes towardTarget = GET_TEAM(ePlayer).AI_getAttitude(eTradeTeam);
	// </advc.130f>
	switch(towardTarget)
	{
	case ATTITUDE_FURIOUS:
		break;
	case ATTITUDE_ANNOYED:
		iModifier += 25;
		break;
	case ATTITUDE_CAUTIOUS:
		iModifier += 50;
		break;
	case ATTITUDE_PLEASED:
		iModifier += 100;
		break;
	case ATTITUDE_FRIENDLY:
		iModifier += 200;
		break;
	default:
		FAssert(false);
		break;
	} // <advc.130f>
	// When threshold relaxed as a special favor
	if(towardTarget > GC.getInfo(GET_PLAYER(ePlayer).
			getPersonalityType()).getStopTradingThemRefuseAttitudeThreshold())
		iModifier *= 2;
	// towardUs - i.e. toward the civ who pays for the embargo
	AttitudeTypes towardUs = GET_PLAYER(ePlayer).AI_getAttitude(getID());
	if(towardUs >= ATTITUDE_PLEASED)
		iModifier -= 25;
	if(towardUs >= ATTITUDE_FRIENDLY)
		iModifier -= 25;
	CvTeam const& kTeam = GET_TEAM(ePlayer); // The team that'll have to cancel OB
	if(kTeam.isOpenBorders(eTradeTeam))
	{
		iModifier += 50;
		if(kTeam.getNumWars() > 0 && GET_PLAYER(ePlayer).AI_isFocusWar() &&
				!kTeam.allWarsShared(getTeam(), false))
		{
			/*  <advc.104o> Will probably want to focus on the new war then.
				(Though with a human sponsor, we shouldn't be too sure.) */
			if(bWarTrade)
				iModifier += ((isHuman() && towardUs < ATTITUDE_FRIENDLY) ? 50 : 25);
			// </advc.104o>
			else iModifier += (towardUs < ATTITUDE_FRIENDLY ? 100 : 50);
		}
	} // Lower bound was 0% in BtS, now 50%
	iValue *= std::max(50, iModifier + 100); // </advc.130f>
	iValue /= 100;
	// advc.130f: Factored into iModifier now
	/*if (GET_TEAM(GET_PLAYER(ePlayer).getTeam()).isOpenBorders(eTradeTeam))
		iValue *= 2;*/

	if (GET_TEAM(ePlayer).isDefensivePact(eTradeTeam))
		iValue *= 3;
	FOR_EACH_DEAL(pLoopDeal)
	{
		// <advc.001q> BtS had only checked for ePlayer's team
		if (!pLoopDeal->isBetween(TEAMID(ePlayer), eTradeTeam))
			continue;
		/*  Should be pLoopDeal->isCancelable(ePlayer), but the param
			doesn't really have an effect, and I'm disabling this check anyway. */
		//if (pLoopDeal->isCancelable(getID()) && !(pLoopDeal->isPeaceDeal()))
		// </advc.001q>  // <advc.130f>
		if(pLoopDeal->isPeaceDeal() ||
				/*  OB don't have a proper trade value and have already been
					covered above */
				pLoopDeal->headFirstTradesNode() != NULL &&
				pLoopDeal->headFirstTradesNode()->m_data.m_eItemType ==
				TRADE_OPEN_BORDERS)
			continue; // </advc.130f>
		// advc: Rewrote this block to get rid of duplicate code
		if (pLoopDeal->getReceivesList(TEAMID(ePlayer)).getLength() > 0)
		{
			iValue += (GET_PLAYER(ePlayer).AI_dealVal(
					pLoopDeal->getOtherPlayer(ePlayer),
					pLoopDeal->getReceivesList(TEAMID(ePlayer))) *
					/*  advc.130f: Was ... 2 : 1.
						AI_dealVal is based on the peace treaty duration,
						but we're likely to lose the trade permanently.
						A gift is better than a trade, but what we give away
						is less valuable than what we get; 5:3 captures this
						better than 2:1 I think. */
					(pLoopDeal->getGivesList(ePlayer).getLength() <= 0 ? 5 : 3)) / 2;
		}
	}

	if (GET_TEAM(ePlayer).isVassal(getTeam()))
		iValue /= 2;
	// <advc.130f>
	double embargoLengthMult = GC.getInfo(GET_PLAYER(GET_TEAM(
			eTradeTeam).getLeaderID()).getPersonalityType()).
			getMemoryDecayRand(MEMORY_STOPPED_TRADING_RECENT) / 20.0;
	iValue = ::round(embargoLengthMult * iValue); // </advc.130f>
	return GET_TEAM(getTeam()).AI_roundTradeVal(iValue); // advc.104k
}

// advc (comment): This CvPlayer is supposed to stop trading with eTradeTeam at the request of ePlayer
DenialTypes CvPlayerAI::AI_stopTradingTrade(TeamTypes eTradeTeam, PlayerTypes ePlayer) const // advc: style changes
{
	FAssert(ePlayer != getID());
	FAssert(TEAMID(ePlayer) != getTeam());
	FAssert(eTradeTeam != getTeam());
	FAssert(GET_TEAM(eTradeTeam).isAlive());
	FAssert(!atWar(getTeam(), eTradeTeam));
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());

	if(kOurTeam.isVassal(eTradeTeam) ||
		/*  <advc.130f> (Should actually be unnecessary b/c of a check in
			CvPlayer::canStopTradingWithTeam) */
		GET_TEAM(eTradeTeam).isVassal(getTeam()))
	{
		/*  "It's out of our hands" b/c the vassal treaty can't be canceled. Need
			DENIAL_POWER_THEM for sth. else. */
		return DENIAL_VASSAL;
	}

	if(isHuman() || kOurTeam.isVassal(TEAMID(ePlayer)))
		return NO_DENIAL;

	bool bWar = kOurTeam.isAtWar(TEAMID(ePlayer));
	/*  Use capitals for a quick distance check
		(instead check AI_getCloseBordersAttitude?) */
	CvCity* pOurCapital = getCapitalCity();
	CvCity* pTargetCapital = GET_TEAM(eTradeTeam).getLeaderCapital(NO_TEAM);
	CvCity* pPlayerCapital = GET_PLAYER(ePlayer).getCapitalCity();
	if(pOurCapital != NULL && pTargetCapital != NULL && pPlayerCapital != NULL &&
			kOurTeam.isOpenBorders(eTradeTeam) &&
			!GET_TEAM(eTradeTeam).isAVassal() &&
			GET_TEAM(kOurTeam.getMasterTeam()).getPower(true) *
			(::stepDistance(pOurCapital->plot(), pTargetCapital->plot()) <
			::stepDistance(pPlayerCapital->plot(), pTargetCapital->plot()) ?
			130 : 200) < 100 * GET_TEAM(eTradeTeam).getPower(true) &&
			// When suing for peace: whom do we fear more?
			(!bWar || GET_TEAM(ePlayer).getPower(true) * 115 <
			GET_TEAM(eTradeTeam).getPower(true)) &&
			GET_TEAM(eTradeTeam).AI_isLandTarget(getTeam()))
		return DENIAL_POWER_THEM;

	if(bWar)
		return NO_DENIAL; // </advc.130f>

	AttitudeTypes eAttitude = kOurTeam.AI_getAttitude(TEAMID(ePlayer));
	AttitudeTypes eAttitudeThem = kOurTeam.AI_getAttitude(eTradeTeam);

	for (MemberIter it(getTeam()); it.hasNext(); ++it)
	{
		// <advc.130f>
		CvLeaderHeadInfo const& kPersonality = GC.getInfo(it->getPersonalityType());
		int iDelta = kPersonality.getStopTradingRefuseAttitudeThreshold() - eAttitude;
		if(iDelta >= 0) // </advc.130f>
			return DENIAL_ATTITUDE;
		int iThreshThem = kPersonality.getStopTradingThemRefuseAttitudeThreshold();
		// <advc.130f>
		if(iDelta <= -1 && iThreshThem < ATTITUDE_CAUTIOUS)
			iThreshThem++; // </advc.130f>
		if(eAttitudeThem > iThreshThem)
			return DENIAL_ATTITUDE_THEM;
	} // <advc.130f> Avoid canceling reparations
	if(GET_TEAM(getTeam()).isForcePeace(eTradeTeam)) // Check this upfront for performance
	{
		FOR_EACH_DEAL(d)
		{
			if(!d->isBetween(getTeam(), eTradeTeam))
				continue;
			bool bPeaceTreaty = false;
			bool bAnnualPayment = false;
			for(CLLNode<TradeData> const* pNode = d->headTradesNode(); pNode != NULL;
				pNode = d->nextTradesNode(pNode))
			{
				TradeableItems eItem = pNode->m_data.m_eItemType;
				if(eItem == TRADE_PEACE_TREATY)
					bPeaceTreaty = true;
				if(eItem == TRADE_RESOURCES || eItem == TRADE_GOLD_PER_TURN)
					bAnnualPayment = true;
			}
			if(bPeaceTreaty && bAnnualPayment)
				return DENIAL_RECENT_CANCEL;
		}
	} // </advc.130f>
	return NO_DENIAL;
}

/*  advc (comment): This CvPlayer asks ePlayer to switch to eCivic.
	What price should this CvPlayer be willing to pay for that? */
int CvPlayerAI::AI_civicTradeVal(CivicTypes eCivic, PlayerTypes ePlayer) const
{
	CvPlayerAI const& kPlayer = GET_PLAYER(ePlayer);
	int iValue = 0;
	//int iValue = (2 * (getTotalPopulation() + GET_PLAYER(ePlayer).getTotalPopulation())); // XXX
	// advc.132: Replacing the above
	int iAnarchyCost = kPlayer.AI_anarchyTradeVal(eCivic);

	CivicTypes eBestCivic = kPlayer.AI_bestCivic((CivicOptionTypes)(GC.getInfo(eCivic).getCivicOptionType()));
	if (eBestCivic != NO_CIVIC && eBestCivic != eCivic)
	{
		iValue += //std::max(0, // advc.132: Handle that below
				2 * (kPlayer.AI_civicValue(eBestCivic) - kPlayer.AI_civicValue(eCivic));
	}
	/*  advc.132: ePlayer charges less than the anarchyCost if they were going to
		switch anyway. */
	iValue = std::max(iValue + iAnarchyCost, iAnarchyCost / 2);
	if(GET_TEAM(ePlayer).isVassal(getTeam()))
		iValue /= 2;
	// <advc.132>
	if(!isCivic(eCivic))
		iValue *= 2; // </advc.132>
	return GET_TEAM(getTeam()).AI_roundTradeVal(iValue); // advc.104k
}

// advc (comment): This CvPlayer is supposed to adopt eCivic at the request of ePlayer
DenialTypes CvPlayerAI::AI_civicTrade(CivicTypes eCivic, PlayerTypes ePlayer) const
{
	if (isHuman())
	{
		return NO_DENIAL;
	}

	if (GET_TEAM(getTeam()).isVassal(GET_PLAYER(ePlayer).getTeam()))
	{
		return NO_DENIAL;
	}

	if (atWar(getTeam(), GET_PLAYER(ePlayer).getTeam()))
	{
		return NO_DENIAL;
	}

	if (TEAMID(ePlayer) == getTeam())
	{
		return NO_DENIAL;
	}
	CivicOptionTypes eCivicOption = (CivicOptionTypes)GC.getInfo(eCivic).getCivicOptionType(); // advc
	if (getCivicPercentAnger(getCivics(eCivicOption), true) > getCivicPercentAnger(eCivic))
	{
		return DENIAL_ANGER_CIVIC;
	}

	CivicTypes eFavoriteCivic = (CivicTypes)GC.getInfo(getPersonalityType()).getFavoriteCivic();
	if (eFavoriteCivic != NO_CIVIC)
	{
		if (isCivic(eFavoriteCivic) && eCivicOption == GC.getInfo(eFavoriteCivic).getCivicOptionType())
		{
			return DENIAL_FAVORITE_CIVIC;
		}
	}

	if (getCivilization().getInitialCivic(eCivicOption) == eCivic)
	{
		return DENIAL_JOKING;
	}

	if (AI_getAttitude(ePlayer) <= GC.getInfo(getPersonalityType()).getAdoptCivicRefuseAttitudeThreshold())
	{
		return DENIAL_ATTITUDE;
	}

	return NO_DENIAL;
}


int CvPlayerAI::AI_religionTradeVal(ReligionTypes eReligion, PlayerTypes ePlayer) const
{
	CvPlayerAI const& kPlayer = GET_PLAYER(ePlayer);
	//int iValue = (3 * (getTotalPopulation() + GET_PLAYER(ePlayer).getTotalPopulation())); // XXX
	// advc.132: Replacing the above
	int iValue = kPlayer.AI_anarchyTradeVal();

	ReligionTypes eBestReligion = GET_PLAYER(ePlayer).AI_bestReligion();
	if (eBestReligion != NO_RELIGION)
	{
		if (eBestReligion != eReligion)
		{
			//iValue += std::max(0, (GET_PLAYER(ePlayer).AI_religionValue(eBestReligion) - GET_PLAYER(ePlayer).AI_religionValue(eReligion)));
			// K-Mod. AI_religionValue has arbitrary units.
			// We need to give it some kind of scale for it to be meaningful in this function.
			iValue *= 100 + std::min(100, 100 * GET_PLAYER(ePlayer).AI_religionValue(eBestReligion) / std::max(1, GET_PLAYER(ePlayer).AI_religionValue(eReligion)));
			iValue /= 100;
			// K-Mod end
		}
	}

	if (GET_TEAM(GET_PLAYER(ePlayer).getTeam()).isVassal(getTeam()))
		iValue /= 2;
	// <advc.132>
	if(getStateReligion() != eReligion)
		iValue *= 2; // </advc.132>
	return GET_TEAM(getTeam()).AI_roundTradeVal(iValue); // advc.104k
}

// advc (comment): This CvPlayer is supposed to adopt eReligion at the request of ePlayer
DenialTypes CvPlayerAI::AI_religionTrade(ReligionTypes eReligion, PlayerTypes ePlayer) const  // advc: style changes
{
	if (isHuman())
		return NO_DENIAL;

	// <advc.132>
	if(GET_PLAYER(ePlayer).isStateReligion() && eReligion != GET_PLAYER(ePlayer).
			getStateReligion())
		return DENIAL_JOKING; // </advc.132>
	if (GET_TEAM(getTeam()).isVassal(GET_PLAYER(ePlayer).getTeam()))
		return NO_DENIAL;

	if (atWar(getTeam(), GET_PLAYER(ePlayer).getTeam()))
		return NO_DENIAL;

	if (GET_PLAYER(ePlayer).getTeam() == getTeam())
		return NO_DENIAL;

	if (getStateReligion() != NO_RELIGION &&
			// advc.132: Original code moved into isMajorReligion
			!isMajorReligion(eReligion))
		return DENIAL_MINORITY_RELIGION;

	if (AI_getAttitude(ePlayer) <= GC.getInfo(getPersonalityType()).
			getConvertReligionRefuseAttitudeThreshold())
		return DENIAL_ATTITUDE;

	return NO_DENIAL;
}


int CvPlayerAI::AI_unitImpassableCount(UnitTypes eUnit) const
{	// <advc.003t>
	if (!GC.getInfo(eUnit).isAnyTerrainImpassable() &&
		!GC.getInfo(eUnit).isAnyFeatureImpassable())
	{
		return 0; // </advc.003t>
	}
	int iCount = 0;
	FOR_EACH_ENUM(Terrain)
	{
		if (GC.getInfo(eUnit).getTerrainImpassable(eLoopTerrain))
		{
			TechTypes eTech = GC.getInfo(eUnit).getTerrainPassableTech(eLoopTerrain);
			if (eTech == NO_TECH || !GET_TEAM(getTeam()).isHasTech(eTech))
				iCount++;
		}
	}
	FOR_EACH_ENUM(Feature)
	{
		if (GC.getInfo(eUnit).getFeatureImpassable(eLoopFeature))
		{
			TechTypes eTech = GC.getInfo(eUnit).getFeaturePassableTech(eLoopFeature);
			if (eTech == NO_TECH || !GET_TEAM(getTeam()).isHasTech(eTech))
				iCount++;
		}
	}
	return iCount;
}

// K-Mod note: currently, unit promotions are considered in CvCityAI::AI_bestUnitAI rather than here.
int CvPlayerAI::AI_unitValue(UnitTypes eUnit, UnitAITypes eUnitAI, CvArea const* pArea) const  // advc: some style changes
{
	//PROFILE_FUNC(); // advc.003o

	int iTempValue=-1;

	FAssert(eUnit != NO_UNIT);
	FAssert(eUnitAI != NO_UNITAI);
	CvUnitInfo const& u = GC.getInfo(eUnit); // advc
	if (u.getDomainType() != AI_unitAIDomainType(eUnitAI))
	{
		if (eUnitAI != UNITAI_ICBM)//XXX
			return 0;
	}

	if (u.getNotUnitAIType(eUnitAI))
		return 0;

	bool bValid = u.getUnitAIType(eUnitAI);

	if (!bValid)
	{
		switch (eUnitAI)
		{
		case UNITAI_UNKNOWN:
			break;

		case UNITAI_ANIMAL:
			if (u.isAnimal())
				bValid = true;
			break;

		case UNITAI_SETTLE:
			if (u.isFound())
				bValid = true;
			break;

		case UNITAI_WORKER:
			FOR_EACH_ENUM(Build)
			{
				if (u.getBuilds(eLoopBuild))
				{
					bValid = true;
					break;
				}
			}
			break;

		case UNITAI_ATTACK:
			if (u.getCombat() > 0)
			{
				if (!u.isMostlyDefensive()) // advc.315
					bValid = true;
			}
			break;

		case UNITAI_ATTACK_CITY:
			if (u.getCombat() > 0)
			{
				if (!u.isMostlyDefensive()) // advc.315
				{
					if (!u.isNoCapture())
						bValid = true;
				}
			}
			break;

		case UNITAI_COLLATERAL:
			if (u.getCombat() > 0 && u.getCollateralDamage() > 0 &&
				!u.isMostlyDefensive()) // advc.315
			{
				bValid = true;
			}
			break;

		case UNITAI_PILLAGE:
			if (u.getCombat() > 0 /* advc.315: */ && !u.isMostlyDefensive())
				bValid = true;
			break;

		case UNITAI_RESERVE:
			if (u.getCombat() > 0 /* advc.315: */ && !u.isMostlyDefensive())
				bValid = true;
			break;

		case UNITAI_COUNTER:
			if (u.getCombat() > 0 /* advc.315: */ && !u.isMostlyDefensive())
			{
				if (u.getInterceptionProbability() > 0)
					bValid = true;

				if (!bValid /* <advc.003t> */ &&
					(u.isAnyUnitClassAttackModifier() ||
					u.isAnyTargetUnitClass())) // </advc.003t>
				{
					FOR_EACH_ENUM(UnitClass)
					{
						if (u.getUnitClassAttackModifier(eLoopUnitClass) > 0 ||
							u.getTargetUnitClass(eLoopUnitClass))
						{
							bValid = true;
							break;
						}
					}
				}
				if (!bValid)
				{
					FOR_EACH_ENUM(UnitCombat)
					{
						if (u.getUnitCombatModifier(eLoopUnitCombat) > 0 ||
							u.getTargetUnitCombat(eLoopUnitCombat))
						{
							bValid = true;
							break;
						}
					}
				}
				if (!bValid)
				{
					FOR_EACH_ENUM(Unit)
					{
						UnitClassTypes eUnitClass = u.getUnitClassType();
						CvUnitInfo const& kLoopUnit = GC.getInfo(eLoopUnit);
						if (eUnitClass != NO_UNITCLASS && kLoopUnit.getDefenderUnitClass(eUnitClass))
						{
							bValid = true;
							break;
						}
						UnitCombatTypes eUnitCombat = u.getUnitCombatType();
						if (eUnitCombat != NO_UNITCOMBAT && kLoopUnit.getDefenderUnitCombat(eUnitCombat))
						{
							bValid = true;
							break;
						}
					}
				}
			}
			break;

		case UNITAI_CITY_DEFENSE:
			if (u.getCombat() > 0 && !u.isNoDefensiveBonus() && u.getCityDefenseModifier() > 0)
				bValid = true;
			break;

		case UNITAI_CITY_COUNTER:
			if (u.getCombat() > 0 && !u.isNoDefensiveBonus())
			{
				if (u.getInterceptionProbability() > 0)
					bValid = true;
				if (!bValid /* advc.003t: */ && u.isAnyUnitClassDefenseModifier())
				{
					FOR_EACH_ENUM(UnitClass)
					{
						if (u.getUnitClassDefenseModifier(eLoopUnitClass) > 0)
						{
							bValid = true;
							break;
						}
					}
				}
				if (!bValid)
				{
					FOR_EACH_ENUM(UnitCombat)
					{
						if (u.getUnitCombatModifier(eLoopUnitCombat) > 0)
						{
							bValid = true;
							break;
						}
					}
				}
			}
			break;

		case UNITAI_CITY_SPECIAL:
			break;

		case UNITAI_PARADROP:
			if (u.getDropRange() > 0)
				bValid = true;
			break;

		case UNITAI_EXPLORE:
			// if (u.getCombat() > 0)
			if (!u.isNoRevealMap() && (u.getCombat() > 0 || u.isInvisible())) // K-Mod
			{
				if (AI_unitImpassableCount(eUnit) == 0)
					bValid = true;
			}
			break;

		case UNITAI_MISSIONARY:
			if (pArea == NULL)
				break;

			FOR_EACH_ENUM(Religion)
			{
				if (u.getReligionSpreads(eLoopReligion) <= 0)
					continue;
				int iNeededMissionaries = AI_neededMissionaries(*pArea, eLoopReligion);
				if (iNeededMissionaries > 0)
				{
					if (iNeededMissionaries > countReligionSpreadUnits(pArea, eLoopReligion))
					{
						bValid = true;
						break;
					}
				}
			}
			FOR_EACH_ENUM(Corporation)
			{
				if (u.getCorporationSpreads(eLoopCorporation) <= 0)
					continue;
				int iNeededMissionaries = AI_neededExecutives(*pArea, eLoopCorporation);
				if (iNeededMissionaries > 0)
				{
					if (iNeededMissionaries > countCorporationSpreadUnits(pArea, eLoopCorporation))
					{
						bValid = true;
						break;
					}
				}
			}
			break;

		case UNITAI_PROPHET:
		case UNITAI_ARTIST:
		case UNITAI_SCIENTIST:
		case UNITAI_GENERAL:
		case UNITAI_MERCHANT:
		case UNITAI_ENGINEER:
		case UNITAI_GREAT_SPY: // K-Mod
		case UNITAI_SPY:
			break;

		case UNITAI_ICBM:
			if (u.getNukeRange() != -1)
				bValid = true;
			break;

		case UNITAI_WORKER_SEA:
			FOR_EACH_ENUM(Build)
			{
				if (u.getBuilds(eLoopBuild))
				{
					bValid = true;
					break;
				}
			}
			break;

		case UNITAI_ATTACK_SEA:
			if (u.getCombat() > 0 && u.getCargoSpace() == 0 &&
				!u.isInvisible() && u.getInvisibleType() == NO_INVISIBLE)
			{
				bValid = true;
			}
			break;

		case UNITAI_RESERVE_SEA:
			if (u.getCombat() > 0 && u.getCargoSpace() == 0)
				bValid = true;
			break;

		case UNITAI_ESCORT_SEA:
			if (u.getCombat() > 0 && u.getCargoSpace() == 0 &&
				AI_unitImpassableCount(eUnit) == 0)
			{
				bValid = true;
			}
			break;

		case UNITAI_EXPLORE_SEA:
			// BETTER_BTS_AI_MOD, Unit AI, 12/09/09, jdog5000: START
			if (u.getCargoSpace() <= 1 && !u.isNoRevealMap())
				bValid = true; // BETTER_BTS_AI_MOD: END
			break;

		case UNITAI_ASSAULT_SEA:
		case UNITAI_SETTLER_SEA:
			if (u.getCargoSpace() > 0)
			{
				if (u.getSpecialCargo() == NO_SPECIALUNIT)
					bValid = true;
			}
			break;

		case UNITAI_MISSIONARY_SEA:
		case UNITAI_SPY_SEA:
		case UNITAI_CARRIER_SEA:
		case UNITAI_MISSILE_CARRIER_SEA:
			if (u.getCargoSpace() > 0 && u.getSpecialCargo() != NO_SPECIALUNIT)
			{
				FOR_EACH_ENUM(UnitAI)
				{
					// advc.001: was ...isCarrierUnitAIType(eUnitAI)
					if (GC.getInfo(u.getSpecialCargo()).isCarrierUnitAIType(eLoopUnitAI))
					{
						bValid = true;
						break;
					}
				}
			}
			break;

		case UNITAI_PIRATE_SEA:
			if (u.isAlwaysHostile() && u.isHiddenNationality())
				bValid = true;
			break;

		case UNITAI_ATTACK_AIR:
			if (u.getAirCombat() > 0)
			{
				if (!u.isSuicide())
					bValid = true;
			}
			break;

		case UNITAI_DEFENSE_AIR:
			if (u.getInterceptionProbability() > 0)
				bValid = true;
			break;

		case UNITAI_CARRIER_AIR:
			if (u.getAirCombat() > 0 && u.getInterceptionProbability() > 0)
				bValid = true;
			break;

		case UNITAI_MISSILE_AIR:
			if (u.getAirCombat() > 0 && u.isSuicide())
				bValid = true;
			break;

		case UNITAI_ATTACK_CITY_LEMMING:
			bValid = false;
			break;

		default:
			FAssert(false);
		}
	}

	if (!bValid)
		return 0;

	int const iCombatValue = GC.AI_getGame().AI_combatValue(eUnit);

	int iValue = 1;

	iValue += u.getAIWeight();

	switch (eUnitAI)
	{
	case UNITAI_UNKNOWN:
	case UNITAI_ANIMAL:
		break;

	case UNITAI_SETTLE:
		iValue += (u.getMoves() * 100);
		break;

	case UNITAI_WORKER:
		FOR_EACH_ENUM(Build)
		{
			if (u.getBuilds(eLoopBuild))
				iValue += 50;
		}
		iValue += (u.getMoves() * 100);
		break;

	case UNITAI_ATTACK:

		iValue += iCombatValue;
		iValue += ((iCombatValue * u.getWithdrawalProbability()) / 100);
		if (u.getCombatLimit() < 100)
			iValue -= (iCombatValue * (125 - u.getCombatLimit())) / 100;
		// K-Mod
		if (u.getMoves() > 1)
		{
			// (the bts / bbai bonus was too high)
			int iFastMoverMultiplier = AI_isDoStrategy(AI_STRATEGY_FASTMOVERS) ? 3 : 1;
			iValue += iCombatValue * iFastMoverMultiplier * u.getMoves() / 8;
		}
		if (u.isNoCapture())
			iValue -= iCombatValue * 30 / 100;
		// K-Mod end
		break;

	case UNITAI_ATTACK_CITY:
	{
		PROFILE("AI_unitValue, UNITAI_ATTACK_CITY evaluation");
		// Effect army composition to have more collateral/bombard units

		iTempValue = ((iCombatValue * iCombatValue) / 75) + (iCombatValue / 2);
		iValue += iTempValue;
		if (u.isNoDefensiveBonus())
		{
			//iValue -= iTempValue / 2;
			iValue -= iTempValue / 3; // K-Mod
		}
		if (u.getDropRange() > 0)
		{
			//iValue -= iTempValue / 2;
			// disabled by K-Mod (how is drop range a disadvantage?)
		}
		if (u.isFirstStrikeImmune())
		{
			iValue += (iTempValue * 8) / 100;
		}
		iValue += ((iCombatValue * u.getCityAttackModifier()) / 75); // bbai (was 100).
		// iValue += ((iCombatValue * u.getCollateralDamage()) / 400); // (moved)
		iValue += ((iCombatValue * u.getWithdrawalProbability()) / 150); // K-Mod (was 100)
		// iValue += ((iCombatValue * u.getMoves() * iFastMoverMultiplier) / 4);
		// K-Mod
		if (u.getMoves() > 1)
		{
			int iFastMoverMultiplier = AI_isDoStrategy(AI_STRATEGY_FASTMOVERS) ? 4 : 1;
			iValue += iCombatValue * iFastMoverMultiplier * u.getMoves() / 10;
		}
		// K-Mod end


		/* if (!AI_isDoStrategy(AI_STRATEGY_AIR_BLITZ)) {
			int iBombardValue = u.getBombardRate() * 8;
			//int iBombardValue = u.getBombardRate() * (u.isIgnoreBuildingDefense()?12 :8);
			if (iBombardValue > 0) {
				int iGoalTotalBombardRate = (AI_totalUnitAIs(UNITAI_ATTACK) + AI_totalUnitAIs(UNITAI_ATTACK_CITY)) * (getCurrentEra()+3) / 2;
				// Decrease the bombard target if we own every city in the area, or if we are fighting an overseas war
				if (pArea && (pArea->getNumCities() == pArea->getCitiesPerPlayer(getID()) ||
						(pArea->getAreaAIType(getTeam()) != AREAAI_NEUTRAL && !AI_isLandWar(pArea)))) {
					iGoalTotalBombardRate *= 2;
					iGoalTotalBombardRate /= 3;
				}
				// Note: this also counts UNITAI_COLLATERAL units, which only play defense
				int iTotalBombardRate = AI_calculateTotalBombard(DOMAIN_LAND);
				if (iTotalBombardRate <= iGoalTotalBombardRate) {
					iBombardValue *= (5*iGoalTotalBombardRate - 4*iTotalBombardRate);
					iBombardValue /= std::max(1, iGoalTotalBombardRate);
				}
				else {
					iBombardValue *= 3*iGoalTotalBombardRate+iTotalBombardRate;
					iBombardValue /= 4*std::max(1, iTotalBombardRate);
				}
				iValue += iBombardValue;
			}
		}*/
		// K-Mod. Bombard rate and collateral damage are both very powerful, but they have diminishing returns wrt the number of such units.
		// Units with these traits tend to also have a 'combat limit' of less than 100%. It is bad to have an army with a high proportion of
		// combat-limited units, but it is fine to have some. So as an ad hoc mechanism for evaluating the tradeoff between collateral damage
		// & bombard vs. combat limit, I'm going to estimate the number of combat limited attack units we already have and use this to adjust
		// the value of this unit. - The value of bombard is particularly inflated, to make sure at least _some_ siege units are built.
		// Note: The original bts bombard evaluation has been deleted.
		// The commented code above is K-Mod code from before the more recent changes; kept for comparison.
		int iSiegeValue = 0;
		iSiegeValue += iCombatValue * u.getCollateralDamage() * (4+u.getCollateralDamageMaxUnits()) / 600;
		if (u.getBombardRate() > 0 && !AI_isDoStrategy(AI_STRATEGY_AIR_BLITZ))
		{
			int iBombardValue = (u.getBombardRate()+3) * 6;
			// Decrease the bombard value if we own every city in the area, or if we are fighting an overseas war
			if (pArea &&
				(pArea->getNumCities() == pArea->getCitiesPerPlayer(getID()) ||
				(pArea->getAreaAIType(getTeam()) != AREAAI_NEUTRAL && !AI_isLandWar(*pArea))) &&
				AI_calculateTotalBombard(DOMAIN_SEA) > 0)
			{
				iBombardValue /= 2;
			}
			iSiegeValue += iBombardValue;
		}
		if (u.getCombatLimit() < 100)
		{
			PROFILE("AI_unitValue, UNITAI_ATTACK_CITY combat limit adjustment");
			// count the number of existing combat-limited units.
			int iLimitedUnits = 0;

			// Unfortunately, when counting units like this we can't distiguish between attack unit and collateral defensive units.
			// Most of the time, unitai_collateral units will be combat limited, and so we should subtract them from out limited unit tally.
			// But in some situations there are collateral damage units without combat limits (eg. Cho-Ko-Nu). When such units are in use,
			// we should not assume all unitai_collateral are limited. -- This whole business is an ugly kludge... I hope it works.
			int iNoLimitCollateral = 0;

			CvCivilization const& kCiv = getCivilization(); // advc.003w 
			for (int i = 0; i < kCiv.getNumUnits(); i++)
			{
				UnitTypes eLoopUnit = kCiv.unitAt(i);
				UnitClassTypes eUnitClass = kCiv.unitClassAt(i);
				const CvUnitInfo& kLoopInfo = GC.getInfo(eLoopUnit);

				if (kLoopInfo.getDomainType() == DOMAIN_LAND && kLoopInfo.getCombat() > 0 &&
					!kLoopInfo.isMostlyDefensive()) // advc.315
				{
					if (kLoopInfo.getCombatLimit() < 100)
						iLimitedUnits += getUnitClassCount(eUnitClass);
					else if (kLoopInfo.getCollateralDamage() > 0)
						iNoLimitCollateral = getUnitClassCount(eUnitClass);
				}
			}

			iLimitedUnits -= range(AI_totalUnitAIs(UNITAI_COLLATERAL) - iNoLimitCollateral / 2, 0, iLimitedUnits);
			FAssert(iLimitedUnits >= 0);
			int iAttackUnits = std::max(1, AI_totalUnitAIs(UNITAI_ATTACK) + AI_totalUnitAIs(UNITAI_ATTACK_CITY)); // floor value is just to avoid divide-by-zero
			/*  advc.006: +1 added and replaced iLimitedUnits with iAttackUnits
				in the 2nd clause b/c this assert kept failing for a capitulated
				Renaissance civ w/o access to Horses (not sure if that's what's
				causing the failed assertion). */
			FAssert(iAttackUnits+1 >= iLimitedUnits || iAttackUnits <= 3); // this is not strictly guaranteed, but I expect it to always be true under normal playing conditions.

			iValue *= std::max(1, iAttackUnits - iLimitedUnits);
			iValue /= iAttackUnits;

			iSiegeValue *= std::max(1, iAttackUnits - iLimitedUnits);
			iSiegeValue /= iAttackUnits + 2 * iLimitedUnits;
		}
		iValue += iSiegeValue;

		break;
	}

	case UNITAI_COLLATERAL:
		iValue += iCombatValue;
		//iValue += ((iCombatValue * u.getCollateralDamage()) / 50);
		iValue += iCombatValue * u.getCollateralDamage() * (1 + u.getCollateralDamageMaxUnits()) / 350; // K-Mod (max units is 6-8 in the current xml)
		iValue += ((iCombatValue * u.getMoves()) / 4);
		iValue += ((iCombatValue * u.getWithdrawalProbability()) / 25);
		iValue -= ((iCombatValue * u.getCityAttackModifier()) / 100);
		break;

	case UNITAI_PILLAGE:
		iValue += iCombatValue;
		//iValue += (iCombatValue * u.getMoves());
		iValue += iCombatValue * (u.getMoves()-1) / 2;
		break;

	case UNITAI_RESERVE:
		iValue += iCombatValue;
		// iValue -= ((iCombatValue * u.getCollateralDamage()) / 200); // disabled by K-Mod (collateral damage is never 'bad')
		FOR_EACH_ENUM(UnitCombat)
		{
			/*int iCombatModifier = u.getUnitCombatModifier(eLoopUnitCombat);
			iCombatModifier = (iCombatModifier < 40) ? iCombatModifier : (40 + (iCombatModifier - 40) / 2);
			iValue += (iCombatValue * iCombatModifier) / 100;*/
			iValue += ((iCombatValue * u.getUnitCombatModifier(eLoopUnitCombat) *
					AI_getUnitCombatWeight(eLoopUnitCombat)) / 12000);
		}
		//iValue += ((iCombatValue * u.getMoves()) / 2);
		// K-Mod
		if (u.getMoves() > 1)
			iValue += iCombatValue * u.getMoves() / (u.isNoDefensiveBonus() ? 7 : 5);

		iValue -= (u.isNoDefensiveBonus() ? iCombatValue / 4 : 0);
		if (u.isAnyFlankingStrikeUnitClass()) // advc.003t
		{
			FOR_EACH_ENUM(UnitClass)
			{
				iValue += iCombatValue * u.getFlankingStrikeUnitClass(eLoopUnitClass) *
						AI_getUnitClassWeight(eLoopUnitClass) / 20000; // (this is pretty small)
			}
		}
		// K-Mod end
		break;

	case UNITAI_COUNTER:
		iValue += (iCombatValue / 2);
		// <advc.003t>
		if (u.isAnyUnitClassAttackModifier() || u.isAnyUnitClassDefenseModifier() ||
			u.isAnyTargetUnitClass()) // </advc.003t>
		{
			FOR_EACH_ENUM(UnitClass)
			{
				iValue += (iCombatValue * u.getUnitClassAttackModifier(eLoopUnitClass) *
						AI_getUnitClassWeight(eLoopUnitClass)) /
						10000; // k146: was 7500
				// <k146>
				iValue += (iCombatValue * u.getUnitClassDefenseModifier(eLoopUnitClass) *
						AI_getUnitClassWeight(eLoopUnitClass)) / 10000;
				// </k146>
				iValue += (iCombatValue * (u.getTargetUnitClass(eLoopUnitClass) ? 50 : 0)) / 100;
			}
		}
		FOR_EACH_ENUM(UnitCombat)
		{
			/*int iCombatModifier = u.getUnitCombatModifier(eLoopUnitCombat);
			iCombatModifier = (iCombatModifier < 40) ? iCombatModifier : (40 + (iCombatModifier - 40) / 2);
			iValue += ((iCombatValue * iCombatModifier) / 100);*/
			iValue += (iCombatValue * u.getUnitCombatModifier(eLoopUnitCombat) *
					AI_getUnitCombatWeight(eLoopUnitCombat)) /
					7500; // k146: was 10000
			iValue += (iCombatValue * (u.getTargetUnitCombat(eLoopUnitCombat) ? 50 : 0)) / 100;
		}
		FOR_EACH_ENUM(Unit)
		{
			CvUnitInfo const& kLoopUnit = GC.getInfo(eLoopUnit);
			int eUnitClass = u.getUnitClassType();
			if (eUnitClass != NO_UNITCLASS && kLoopUnit.getDefenderUnitClass(eUnitClass))
				iValue += (50 * iCombatValue) / 100;

			int eUnitCombat = u.getUnitCombatType();
			if (eUnitCombat != NO_UNITCOMBAT && kLoopUnit.getDefenderUnitCombat(eUnitCombat))
				iValue += (50 * iCombatValue) / 100;
		}

		//iValue += (iCombatValue * u.getMoves()) / 2;
		// K-Mod
		if (u.getMoves() > 1)
			iValue += iCombatValue * u.getMoves() / 8; // k146: was /6
		// K-Mod end

		iValue += (iCombatValue * u.getWithdrawalProbability()) / 100;
		// BETTER_BTS_AI_MOD, War strategy AI, 03/20/10, jdog5000: START
		//iValue += (u.getInterceptionProbability() * 2);
		if (u.getInterceptionProbability() > 0)
		{
			iTempValue = u.getInterceptionProbability();

			iTempValue *= (25 + std::min(175, GET_TEAM(getTeam()).AI_getRivalAirPower()));
			iTempValue /= 100;

			iValue += iTempValue;
		} // BETTER_BTS_AI_MOD: END

		break;

	case UNITAI_CITY_DEFENSE:
		iValue += ((iCombatValue * 2) / 3);
		iValue += ((iCombatValue * u.getCityDefenseModifier()) / 75);
		// K-Mod. Value for collateral immunity
		FOR_EACH_ENUM(UnitCombat)
		{
			if (u.getUnitCombatCollateralImmune(eLoopUnitCombat))
			{
				iValue += iCombatValue*30/100;
				break;
			}
		}
		// k146: Other bonuses
		iValue += iCombatValue * u.getHillsDefenseModifier() / 200;
		// K-Mod end
		break;
	/*  <advc.005a> I've assigned these as favorite units in LeaderHead XML
		and don't want them to be treated exactly like City Counter. */
	case UNITAI_PARADROP:
		if(u.getDropRange() <= 0)
			iValue -= iCombatValue/3;
		else iValue += (iCombatValue * u.getDropRange() * 10) / 100;
		// Fall through to Counter (as was already the case in BtS)
	case UNITAI_CITY_SPECIAL:
		iValue += (iCombatValue * u.getInterceptionProbability()) / 100;
		// Fall through to Counter (no change)
		// </advc.005a>
	case UNITAI_CITY_COUNTER:
		iValue += iCombatValue / 2;
		iValue += (iCombatValue * u.getCityDefenseModifier()) / 100;
		if (u.isAnyUnitClassAttackModifier() || u.isAnyDefenderUnitClass()) // advc.003t
		{
			FOR_EACH_ENUM(UnitClass)
			{
				iValue += (iCombatValue * u.getUnitClassAttackModifier(eLoopUnitClass) *
						AI_getUnitClassWeight(eLoopUnitClass)) / 10000;
				iValue += (iCombatValue * (u.getDefenderUnitClass(eLoopUnitClass) ? 50 : 0)) / 100;
			}
		}
		FOR_EACH_ENUM(UnitCombat)
		{
			iValue += (iCombatValue * u.getUnitCombatModifier(eLoopUnitCombat) *
					AI_getUnitCombatWeight(eLoopUnitCombat)) / 10000;
			iValue += (iCombatValue * (u.getDefenderUnitCombat(eLoopUnitCombat) ? 50 : 0)) / 100;
		}

		// BETTER_BTS_AI_MOD, War strategy AI, 03/20/10, jdog5000: START
		//iValue += (u.getInterceptionProbability() * 3);
		if (u.getInterceptionProbability() > 0)
		{
			iTempValue = u.getInterceptionProbability();

			iTempValue *= (25 + std::min(125, GET_TEAM(getTeam()).AI_getRivalAirPower()));
			iTempValue /= 57; /* advc.005a: Was 50; hopefully compensated for by
								 the bonus for UNITAI_CITY_SPECIAL. */
			iValue += iTempValue;
		} // BETTER_BTS_AI_MOD: END
		break;

	case UNITAI_EXPLORE:
		iValue += (iCombatValue / 2);
		iValue += (u.getMoves() * 200);
		if (u.isNoBadGoodies())
			iValue += 100;
		// K-Mod note: spies are valid explorers, but the AI currently doesn't know to convert them to
		// UNITAI_SPY when the exploring is finished. Hence I won't yet give a value bonus for invisibility.
		break;

	case UNITAI_MISSIONARY:
		iValue += (u.getMoves() * 100);
		if (getStateReligion() != NO_RELIGION)
		{
			if (u.getReligionSpreads(getStateReligion()) > 0)
				iValue += (5 * u.getReligionSpreads(getStateReligion())) / 2;
		}
		FOR_EACH_ENUM(Religion)
		{
			if (u.getReligionSpreads(eLoopReligion) && hasHolyCity(eLoopReligion))
			{
				iValue += 80;
				break;
			}
		}
		// BETTER_BTS_AI_MOD, Victory Strategy AI, 03/08/10, jdog5000: (was AI_isDoStrategy)
		if (AI_atVictoryStage(AI_VICTORY_CULTURE2))
		{
			iTempValue = 0;
			FOR_EACH_ENUM(Religion)
			{
				if (u.getReligionSpreads(eLoopReligion))
				{
					iTempValue += (50 * getNumCities()) /
							(1 + getHasReligionCount(eLoopReligion));
				}
			}
			iValue += iTempValue;
		}
		FOR_EACH_ENUM(Corporation)
		{
			if (!hasHeadquarters(eLoopCorporation))
				continue;
			if (u.getCorporationSpreads(eLoopCorporation) > 0)
			{
				iValue += (5 * u.getCorporationSpreads(eLoopCorporation)) / 2;
				/*  UNOFFICIAL_PATCH, Bugfix, 06/03/09, jdog5000:
					Fix potential crash, probably would only happen in mods */
				if (pArea != NULL)
				{
					iValue += 300 / std::max(1,
							pArea->countHasCorporation(eLoopCorporation, getID()));
				}
			}
		}
		break;

	case UNITAI_PROPHET:
	case UNITAI_ARTIST:
	case UNITAI_SCIENTIST:
	case UNITAI_GENERAL:
	case UNITAI_MERCHANT:
	case UNITAI_ENGINEER:
	case UNITAI_GREAT_SPY: // K-Mod
		break;

	case UNITAI_SPY:
		iValue += (u.getMoves() * 100);
		if (u.isSabotage())
			iValue += 50;
		if (u.isDestroy())
			iValue += 50;
		if (u.isCounterSpy())
			iValue += 100;
		break;

	case UNITAI_ICBM:
		if (u.getNukeRange() != -1)
		{
			/*iTempValue = 40 + (u.getNukeRange() * 40);
			if (u.getAirRange() == 0)
				iValue += iTempValue;
			else iValue += (iTempValue * std::min(10, u.getAirRange())) / 10;
			iValue += (iTempValue * (60 + u.getEvasionProbability())) / 100;*/ // BtS
			// K-Mod
			iTempValue = 100 + (u.getNukeRange() * 40);
			if (u.getAirRange() > 0)
				iTempValue = iTempValue * std::min(8, u.getAirRange()) / 10;
			// estimate the expected loss from being shot down.
			{
				bool bWar = pArea && pArea->getAreaAIType(getTeam()) != AREAAI_NEUTRAL;
				const CvTeamAI& kTeam = GET_TEAM(getTeam());
				int iInterceptTally = 0;
				int iPowerTally = 0;
				for (TeamTypes i = (TeamTypes)0; i < MAX_CIV_TEAMS; i = (TeamTypes)(i+1))
				{
					const CvTeam& kLoopTeam = GET_TEAM(i);
					if (kLoopTeam.isAlive() && kTeam.isHasMet(i) && (!bWar || kTeam.AI_getWarPlan(i) != NO_WARPLAN))
					{
						int iPower = kLoopTeam.getPower(false);
						iPowerTally += iPower;
						iInterceptTally += kLoopTeam.getNukeInterception() * iPower;
					}
				}
				if (iInterceptTally > 0)
				{
					FAssert(iPowerTally > 0);
					iTempValue -= iTempValue * (iInterceptTally / iPowerTally) * (100 - u.getEvasionProbability()) / 10000;
				}
			}
			iValue += iTempValue;
			// K-Mod end
		}
		break;

	case UNITAI_WORKER_SEA:
		FOR_EACH_ENUM(Build)
		{
			if (u.getBuilds(eLoopBuild))
				iValue += 50;
		}
		iValue += u.getMoves() * 100;
		break;

	case UNITAI_ATTACK_SEA:
		iValue += iCombatValue;
		iValue += (iCombatValue * u.getMoves()) / 2;
		iValue += u.getBombardRate() * 4;
		break;

	case UNITAI_RESERVE_SEA:
		iValue += iCombatValue;
		iValue += iCombatValue * u.getMoves();
		break;

	case UNITAI_ESCORT_SEA:
		iValue += iCombatValue;
		iValue += iCombatValue * u.getMoves();
		iValue += u.getInterceptionProbability() * 3;
		if (u.getNumSeeInvisibleTypes() > 0)
			iValue += 200;
		// UNOFFICIAL_PATCH, General AI, 06/03/09, jdog5000: START
		// Boats which can't be seen don't play defense, don't make good escorts
		/*  <advc.028> They can defend now. Stats of subs and Stealth Destroyer
			aren't great for escorting, but there's other code to check this. */
		/*if (u.getInvisibleType() != NO_INVISIBLE)
			iValue /= 2;*/ // </advc.028>
		// UNOFFICIAL_PATCH: END
		break;

	case UNITAI_EXPLORE_SEA:
		{
			int iExploreValue = 100;
			// <advc.124> Make sure Caravel remains preferred over Galleon
			if(u.isRivalTerritory())
				iExploreValue += 75;
			if(u.canMoveImpassable())
				iExploreValue += 25; // </advc.124>
			if (pArea != NULL)
			{
				if (pArea->isWater() &&
					!GC.getGame().isOption(GAMEOPTION_NO_BARBARIANS)) // advc.124
				{
					/*if (pArea->getUnitsPerPlayer(BARBARIAN_PLAYER) > 0)
						iExploreValue += (2 * iCombatValue);*/
					// <advc.124> Replacing the above
					iExploreValue += iCombatValue;
					if(pArea->getUnitsPerPlayer(BARBARIAN_PLAYER) > 0)
						iExploreValue += iCombatValue; // </advc.124>
				}
			}
			iValue += (u.getMoves()
					// advc.017b: Could later change the AI type; so cargo isn't worthless.
					+ (u.getCargoSpace()+1) / 2)
					* iExploreValue;
			if (u.isAlwaysHostile())
				iValue /= 2;
			iValue /= (1 + AI_unitImpassableCount(eUnit));
		}
		break;

	case UNITAI_ASSAULT_SEA:
	case UNITAI_SETTLER_SEA:
	case UNITAI_MISSIONARY_SEA:
	case UNITAI_SPY_SEA:
		iValue += iCombatValue / 2;
		iValue += u.getMoves() * 200;
		iValue += u.getCargoSpace() * 300;
		/*  BETTER_BTS_AI_MOD, City AI, 05/18/09, jdog5000:
			Never build galley transports when ocean faring ones exist (issue mainly for Carracks) */
		iValue /= (1 + AI_unitImpassableCount(eUnit));
		break;

	case UNITAI_CARRIER_SEA:
		iValue += iCombatValue;
		iValue += u.getMoves() * 50;
		iValue += u.getCargoSpace() * 400;
		break;

	case UNITAI_MISSILE_CARRIER_SEA:
		iValue += iCombatValue * u.getMoves();
		iValue += (25 + iCombatValue) * (3 + u.getCargoSpace());
		break;

	case UNITAI_PIRATE_SEA:
		iValue += iCombatValue;
		iValue += iCombatValue * u.getMoves();
		break;

	case UNITAI_ATTACK_AIR:
		iValue += iCombatValue;
		iValue += (u.getCollateralDamage() * iCombatValue) / 100;
		iValue += 4 * u.getBombRate();
		iValue += (iCombatValue * (100 + 2 * u.getCollateralDamage()) * u.getAirRange()) / 100;
		break;

	case UNITAI_DEFENSE_AIR:
		iValue += iCombatValue;
		iValue += u.getInterceptionProbability() * 3;
		iValue += u.getAirRange() * iCombatValue;
		break;

	case UNITAI_CARRIER_AIR:
		iValue += iCombatValue;
		iValue += u.getInterceptionProbability() * 2;
		iValue += u.getAirRange() * iCombatValue;
		break;

	case UNITAI_MISSILE_AIR:
		iValue += iCombatValue;
		iValue += 4 * u.getBombRate();
		iValue += u.getAirRange() * iCombatValue;
		break;

	case UNITAI_ATTACK_CITY_LEMMING:
		iValue += iCombatValue;
		break;

	default:
		FAssert(false);
		break;
	}
	if (u.isAnyFreePromotions()) // advc.003t
	{
		if (iCombatValue > 0 &&
			(eUnitAI == UNITAI_ATTACK || eUnitAI == UNITAI_ATTACK_CITY) &&
			pArea != NULL)
		{
			AreaAITypes eAreaAI = pArea->getAreaAIType(getTeam());
			if (eAreaAI == AREAAI_ASSAULT || eAreaAI == AREAAI_ASSAULT_MASSING)
			{
				FOR_EACH_ENUM(Promotion)
				{
					if (u.getFreePromotions(eLoopPromotion))
					{
						if (GC.getInfo(eLoopPromotion).isAmphib())
						{
							iValue *= 133;
							iValue /= 100;
							break;
						}
					}
				}
			}
		}
	}

	return std::max(0, iValue);
}


int CvPlayerAI::AI_totalUnitAIs(UnitAITypes eUnitAI) const
{
	return (AI_getNumTrainAIUnits(eUnitAI) + AI_getNumAIUnits(eUnitAI));
}


int CvPlayerAI::AI_totalAreaUnitAIs(CvArea const& kArea, UnitAITypes eUnitAI) const
{
	return (kArea.getNumTrainAIUnits(getID(), eUnitAI) + kArea.getNumAIUnits(getID(), eUnitAI));
}


int CvPlayerAI::AI_totalWaterAreaUnitAIs(CvArea const& kArea, UnitAITypes eUnitAI) const
{	// <advc.081> ^Now only a wrapper
	std::vector<UnitAITypes> aeUnitAI;
	aeUnitAI.push_back(eUnitAI);
	return AI_totalWaterAreaUnitAIs(kArea, aeUnitAI);
}

// Checks multiple types at once
int CvPlayerAI::AI_totalWaterAreaUnitAIs(CvArea const& kArea,
		std::vector<UnitAITypes> const& aeUnitAI) const
{
	PROFILE_FUNC(); // advc

	int iCount = 0;
	for(size_t i = 0; i < aeUnitAI.size(); i++)
		iCount += AI_totalAreaUnitAIs(kArea, aeUnitAI[i]); // </advc.081>

	for (PlayerIter<MAJOR_CIV> it; it.hasNext(); ++it)
	{
		CvPlayer const& kOwner = *it;
		// <advc.opt> No need to go through cities that our ships can't enter
		if (!GET_TEAM(getTeam()).canPeacefullyEnter(kOwner.getTeam()))
			continue; // </advc.opt>

		FOR_EACH_CITY(pLoopCity, kOwner)
		{
			if (pLoopCity->waterArea() != &kArea)
				continue; // advc

			for(size_t j = 0; j < aeUnitAI.size(); j++) // advc.081
			{
				iCount += pLoopCity->getPlot().plotCount(PUF_isUnitAIType,
						aeUnitAI[j], -1, getID()); // advc.081
				if (kOwner.getID() == getID())
					iCount += pLoopCity->getNumTrainUnitAI(/* advc.081: */aeUnitAI[j]);
			}
		}
	}
	return iCount;
}


int CvPlayerAI::AI_countCargoSpace(UnitAITypes eUnitAI) const
{
	int iCount = 0;
	FOR_EACH_UNITAI(pLoopUnit, *this)
	{
		if (pLoopUnit->AI_getUnitAIType() == eUnitAI)
			iCount += pLoopUnit->cargoSpace();
	}
	return iCount;
}

/*  advc.opt: Now cached. advc.017b adds another call to AI_neededExplorers,
	and it gets called by several UnitAI members too. Probably no noticeable
	difference in performance, but who knows. */
int CvPlayerAI::AI_neededExplorers(CvArea const& kArea) const
{	// <advc.opt> Body moved into AI_neededExplorers_bulk
	std::map<int,int>::const_iterator r = m_neededExplorersByArea.find(kArea.getID());
	if(r == m_neededExplorersByArea.end())
		return 0; // Small area w/o cached value; no problem.
	return r->second;
}


void CvPlayerAI::AI_updateNeededExplorers()
{
	m_neededExplorersByArea.clear();
	FOR_EACH_AREA(a)
	{
		// That threshold should also be OK for land areas
		if(a->getNumTiles() > GC.getDefineINT(CvGlobals::LAKE_MAX_AREA_SIZE))
			m_neededExplorersByArea[a->getID()] = AI_neededExplorers_bulk(*a);
		// Nothing stored for small areas; AI_neededExplorers will return 0.
	}
}

// Body cut and pasted from AI_neededExplorers
int CvPlayerAI::AI_neededExplorers_bulk(CvArea const& kArea) const
{
	// <advc.305> Be sure not to build Work Boats for exploration
	if(isBarbarian())
		return 0; // </advc.305>

	int iNeeded = 0;

	if (kArea.isWater())
	{
		iNeeded = std::min(iNeeded + kArea.getNumUnrevealedTiles(getTeam()) / 400,
				std::min(2, (getNumCities() / 2) + 1));
		// <advc.017>
		if(GC.getInitCore().getMapScriptName().compare(L"Pangaea") == 0)
			iNeeded = (iNeeded + 1) / 2; // </advc.017>
	}
	else
	{
		iNeeded = std::min(iNeeded + (kArea.getNumUnrevealedTiles(getTeam()) / //150
				// 040: Exploration becomes cheaper as the game progresses
				std::max(150 - 20 * getCurrentEra(), 50)),
				std::min(3, ((getNumCities() / 3) + 2)));
	}

	if (iNeeded <= 0)
	{
		if (GC.getGame().countCivTeamsAlive() - 1 > GET_TEAM(getTeam()).
				getHasMetCivCount(true))
		{
			if (kArea.isWater())
			{
				if (GC.getMap().findBiggestArea(true) == &kArea)
					iNeeded++;
			}
			else
			{
				if (getCapitalCity() != NULL && getCapitalCity()->isArea(kArea))
				{
					for (int iPlayer = 0; iPlayer < MAX_CIV_PLAYERS; iPlayer++)
					{
						CvPlayerAI& kPlayer = GET_PLAYER((PlayerTypes)iPlayer);
						if (!kPlayer.isAlive() || kPlayer.getTeam() == getTeam())
							continue;

						if (!GET_TEAM(getTeam()).isHasMet(kPlayer.getTeam()))
						{
							if (kArea.getCitiesPerPlayer(kPlayer.getID()) > 0)
							{
								iNeeded++;
								break;
							}
						}
					}
				}
			}
		}
	}
	return iNeeded;
} // </advc.opt>

/*  <advc.017b> Checks to help CvUnitAI avoid oscillation between UnitAITypes
	(and to avoid inconsistencies between CvUnitAI and CvCityAI) */
bool CvPlayerAI::AI_isExcessSeaExplorers(CvArea const& kWaterArea, int iChange) const
{
	int iHave = AI_totalWaterAreaUnitAIs(kWaterArea, UNITAI_EXPLORE_SEA) -
			AI_getNumTrainAIUnits(UNITAI_EXPLORE_SEA);
	return (AI_neededExplorers(kWaterArea) < iHave + iChange);
}

// Note: only tested for eRole=UNITAI_EXPLORE_SEA
bool CvPlayerAI::AI_isOutdatedUnit(UnitTypes eUnit, UnitAITypes eRole, CvArea const* pArea) const
{
	int iValue = AI_unitValue(eUnit, eRole, pArea);
	UnitClassTypes eUnitClass = GC.getInfo(eUnit).getUnitClassType();
	CvCivilization const& kCiv = getCivilization();
	for (int i = 0; i < kCiv.getNumUnits(); i++)
	{
		UnitClassTypes eLoopUnitClass = kCiv.unitClassAt(i);
		if(eLoopUnitClass == eUnitClass ||
				getUnitClassCount(eLoopUnitClass) <= 0) // Avoid slow canTrain checks
			continue;

		UnitTypes eLoopUnit = kCiv.unitAt(i);
		int iLoopValue = AI_unitValue(eLoopUnit, eRole, pArea);
		if(75 * iLoopValue > 100 * iValue)
			return true;
	}
	return false;
} // </advc.017b>

// advc.042: Cut from CvPlayer.cpp
/*  K-Mod. I've rearranged some stuff in this function to fix a couple of minor bugs;
	and to make the code neater and less error prone. */
int CvPlayerAI::AI_countUnimprovedBonuses(CvArea const& kArea, CvPlot* pFromPlot,
	int iLookAhead) const
{
	PROFILE_FUNC();

	gDLL->getFAStarIFace()->ForceReset(&GC.getBorderFinder());

	int iCount = 0;
	CvMap const& m = GC.getMap(); // advc
	for (int iI = 0; iI < m.numPlots(); iI++)
	{
		CvPlot const& kPlot = m.getPlotByIndex(iI);
		if (kPlot.getOwner() == getID() && kPlot.isArea(kArea))
		{	// advc.042: Remaining checks moved into auxiliary function
			if(AI_isUnimprovedBonus(kPlot, pFromPlot, true))
				iCount++;
		}
	}
	// <advc.042> Anticipate first border expansion
	if(iLookAhead > 0)
	{
		iLookAhead *= GC.getInfo(GC.getGame().getGameSpeedType()).getTrainPercent();
		iLookAhead /= 100;
		FOR_EACH_CITY(c, *this)
		{
			if(c->getCultureLevel() > 1 || c->getCultureTurnsLeft() > iLookAhead)
				continue;
			for (CityPlotIter it(*c); it.hasNext(); ++it)
			{
				CvPlot const& p = *it;
				if(p.getOwner() == NO_PLAYER && p.isArea(kArea))
				{
					/*  Mustn't check path b/c Work Boat may not be able to enter
						the unowned tiles yet. If the area matches, it's a pretty
						safe bet that the resource is reachable.
						Also, the GeneratePath call considers only tiles owned
						by this player, and that could be important for performance. */
					if(AI_isUnimprovedBonus(p, pFromPlot, false))
						iCount++;
				}
			}
		}
	} // </advc.042>
	return iCount;
}

// advc.042: Cut from CvPlayer.cpp (w/o functional changes) b/c of the AI code at the end
int CvPlayerAI::AI_countOwnedBonuses(BonusTypes eBonus) const
{
	PROFILE_FUNC();

	// K-Mod. Shortcut.
	if (!GET_TEAM(getTeam()).isBonusRevealed(eBonus))
		return 0;
	// K-Mod end

	bool bAdvancedStart = (getAdvancedStartPoints() >= 0) && (getCurrentEra() < 3);

	int iCount = 0;

	//count bonuses outside city radius
	CvMap const& kMap = GC.getMap();
	for (int iI = 0; iI < kMap.numPlots(); iI++)
	{
		CvPlot const& kPlot = kMap.getPlotByIndex(iI);
		if (kPlot.getOwner() == getID() && !kPlot.isCityRadius())
		{
			if (kPlot.getBonusType(getTeam()) == eBonus)
				iCount++;
		}
		else if (bAdvancedStart && kPlot.isRevealed(getTeam()))
		{
			if (kPlot.getBonusType(getTeam()) == eBonus)
				iCount++;
		}
	}

	//count bonuses inside city radius or easily claimed
	FOR_EACH_CITYAI(pLoopCity, *this)
		iCount += pLoopCity->AI_countNumBonuses(eBonus, true, pLoopCity->getCommerceRate(COMMERCE_CULTURE) > 0, -1);

	return iCount;
}

/*  <advc.042> Mostly cut and pasted from BtS/K-Mod CvPlayer::isUnimprovedBonus
	(now CvPlayerAI::AI_isUnimprovedBonus; see above).
	Relies on caller to reset GC.getBorderFinder(). */
bool CvPlayerAI::AI_isUnimprovedBonus(CvPlot const& p, CvPlot* pFromPlot, bool bCheckPath) const
{
	if(p.isCity())
		return false;
	BonusTypes eNonObsoleteBonus = p.getNonObsoleteBonusType(getTeam());
	if(eNonObsoleteBonus == NO_BONUS || doesImprovementConnectBonus(
		p.getImprovementType(), eNonObsoleteBonus))
	{
		return false;
	}
	if(pFromPlot == NULL || !bCheckPath || gDLL->getFAStarIFace()->GeneratePath(
		&GC.getBorderFinder(), pFromPlot->getX(), pFromPlot->getY(), p.getX(), p.getY(),
		/*false*/ /* advc.001: Shouldn't allow diagonals in water. (A ship can
			move in between an Ice and a land tile that  touch corners. But I'm
			not sure if GeneratePath checks for isImpassable at all.) */
		!p.isWater(),
		getID(), true))
	{
		for(int i = 0; i < GC.getNumBuildInfos(); i++)
		{
			BuildTypes eBuild = ((BuildTypes)i);
			if(doesImprovementConnectBonus((ImprovementTypes)GC.getInfo(eBuild).
					getImprovement(), eNonObsoleteBonus) &&
					canBuild(&p, eBuild, false, !bCheckPath))
				return true;
		}
	}
	return false;
} // </advc.042>


int CvPlayerAI::AI_neededWorkers(CvArea const& kArea) const
{
	/*  advc.opt: This function is called each time a Worker moves or a city
		chooses production, and AI_countUnimprovedBonuses loops over all plots.
		In the single profiler run I did, the computing time was completely
		negligible though. */
	PROFILE_FUNC();

	// advc.113: Let's focus on the workable bonuses (CvCityAI::AI_getWorkersNeeded)
	int iCount = AI_countUnimprovedBonuses(kArea);// * 2;
	// <advc.040> Areas w/o cities are ignored by the loop below
	if (kArea.getCitiesPerPlayer(getID()) <= 0)
		iCount *= 2; // </advc.040>
	FOR_EACH_CITYAI(pLoopCity, *this)
	{
		if (pLoopCity->isArea(kArea))
			iCount += pLoopCity->AI_getWorkersNeeded() * 3;
	}
	/*  advc.113: Commented out. Training Workers ahead of time is
		not so unimportant. */
	/*if (iCount == 0)
		return 0;*/
	if(iCount < 5 * getNumCities()) // advc.113
	{
		// K-Mod. Some additional workers if for 'growth' flavour AIs who are still growing...
		if (/*  advc.113: Growth is about tall cities. Factor Growth flavor into
				the number of Workers per existing city, and add Workers for
				future cities regardless of flavor. */
			// AI_getFlavorValue(FLAVOR_GROWTH) > 0 &&
			AI_isPrimaryArea(kArea))
		{
			int foo=-1; // advc.113: Changed to floating point arithmetic
			double extraCities = std::min(GC.getInfo(GC.getMap().
					getWorldSize()).getTargetNumCities()*(4.0/3) - getNumCities(),
					(double)AI_getNumAreaCitySites(kArea, foo));
			extraCities = ::dRange(extraCities, 0.0, getNumCities()*(2.0/3));
			/*  advc.113: Was 3*iExtraCities, which can be a lot. In K-Mod,
				CvCityAI didn't actually train all those Workers, but now it might. */
			iCount += ::round(1.33 * extraCities);
		}
		// K-Mod end
		if(getBestRoute() != NO_ROUTE)
			iCount += kArea.getCitiesPerPlayer(getID()) / 2;
	}
	/*  advc.113: To account for future tasks other than new cities:
		population growth and new techs, and try to err on the side of too
		many Workers (b/c the AI is pretty bad at sharing them between cities). */
	iCount = (iCount * (100 + GC.getDefineINT(CvGlobals::WORKER_RESERVE_PERCENT))) / 100;
	iCount += 1;
	iCount /= 3;
	//iCount = std::min(iCount, (1 + getTotalPopulation()) / 2);
	// <advc.113> Current population should have no bearing on Workers
	int iCities = kArea.getCitiesPerPlayer(getID());
	iCount = std::min(iCount, (iCities <= 1 && getCurrentEra() > 0) ? 3 : ::round(2.15 * iCities));
	// Lower bound was 1 flat. Allow small islands to require 0 workers.
	return std::max(kArea.getNumRevealedTiles(getTeam()) > 3 ? 1 : 0, iCount);
	// </advc.113>
}


int CvPlayerAI::AI_neededMissionaries(CvArea const& kArea, ReligionTypes eReligion) const
{
	PROFILE_FUNC();
	// BETTER_BTS_AI_MOD, Victory Strategy AI, 03/08/10, jdog5000:
	bool bCultureVictory = AI_atVictoryStage(AI_VICTORY_CULTURE2);

	bool bHoly = hasHolyCity(eReligion);
	bool bState = (getStateReligion() == eReligion);
	bool bHolyState = ((getStateReligion() != NO_RELIGION) && hasHolyCity(getStateReligion()));
	int iCount = 0;
	//internal spread.
	if (bCultureVictory || bState || bHoly)
	{
		iCount = std::max(iCount, (kArea.getCitiesPerPlayer(getID()) -
				kArea.countHasReligion(eReligion, getID())));
		if (iCount > 0)
		{
			if (!bCultureVictory)
				iCount = std::max(1, iCount / (bHoly ? 2 : 4));
			return iCount;
		}
	}

	//external spread.
	if ((bHoly && bState) ||
		(bHoly && !bHolyState && (getStateReligion() != NO_RELIGION)))
	{
		iCount += kArea.getNumCities() * 2 - kArea.countHasReligion(eReligion) * 3;
		iCount /= 8;

		iCount = std::max(0, iCount);

		if (AI_isPrimaryArea(kArea))
			iCount++;
	}

	return iCount;
}


int CvPlayerAI::AI_neededExecutives(CvArea const& kArea, CorporationTypes eCorporation) const
{
	if (!hasHeadquarters(eCorporation))
		return 0;

	int iCount = (kArea.getCitiesPerPlayer(getID()) -
			kArea.countHasCorporation(eCorporation, getID())) * 2;
	iCount += kArea.getNumCities() - kArea.countHasCorporation(eCorporation);
	iCount /= 3;
	if (AI_isPrimaryArea(kArea))
		iCount++;
	return iCount;
}

// K-Mod. This function is used to replace the old (broken) "unit cost percentage" calculation used by the AI
int CvPlayerAI::AI_unitCostPerMil() const
{
	// original "cost percentage" = calculateUnitCost() * 100 / std::max(1, calculatePreInflatedCosts());
	// If iUnitCostPercentage is calculated as above, decreasing maintenance will actually decrease the max units.
	// If a builds a courthouse or switches to state property, it would then think it needs to get rid of units!
	// It makes no sense, and civs with a surplus of cash still won't want to build units. So lets try it another way...
	int iUnitCost = calculateUnitCost() * std::max(0, calculateInflationRate() + 100) / 100;
	if (iUnitCost <= getNumCities()/2) // cf with the final line
		return 0;

	int iTotalRaw = calculateTotalYield(YIELD_COMMERCE);

	int iFunds = iTotalRaw * AI_averageCommerceMultiplier(COMMERCE_GOLD) / 100;
	iFunds += getGoldPerTurn() - calculateInflatedCosts();
	iFunds += getCommerceRate(COMMERCE_GOLD) - iTotalRaw * AI_averageCommerceMultiplier(COMMERCE_GOLD) * getCommercePercent(COMMERCE_GOLD) / 10000;
	return std::max(0, iUnitCost-getNumCities()/2) * 1000 / std::max(1, iFunds); // # cities is there to offset early-game distortion.
}

// This function gives an approximate / recommended maximum on our unit spending. Note though that it isn't a hard cap.
// we might go as high has 20 point above the "maximum"; and of course, the maximum might later go down.
// So this should only be used as a guide.
int CvPlayerAI::AI_maxUnitCostPerMil(CvArea const* pArea, int iBuildProb) const
{
	if (isBarbarian())
		return 500;

	//if (GC.getGame().isOption(GAMEOPTION_ALWAYS_PEACE))
	if (!GET_TEAM(getTeam()).AI_isWarPossible()) // advc.001j
		return 20; // ??

	if (iBuildProb < 0)
		iBuildProb = GC.getInfo(getPersonalityType()).getBuildUnitProb() + 6; // a rough estimate.

	bool bTotalWar = (GET_TEAM(getTeam()).AI_getNumWarPlans(WARPLAN_TOTAL) > 0);
	// <advc.104s>
	if (!bTotalWar && getUWAI.isEnabled())
		bTotalWar = (GET_TEAM(getTeam()).AI_getNumWarPlans(WARPLAN_PREPARING_TOTAL) > 0);
	// </advc.104s>
	bool bAggressiveAI = GC.getGame().isOption(GAMEOPTION_AGGRESSIVE_AI);

	int iMaxUnitSpending = (bAggressiveAI ? 30 : 20) + iBuildProb*4/3;

	if (AI_atVictoryStage(AI_VICTORY_CONQUEST4))
	{
		iMaxUnitSpending += 30;
	}
	else if (AI_atVictoryStage(AI_VICTORY_CONQUEST3 | AI_VICTORY_DOMINATION3))
	{
		iMaxUnitSpending += 20;
	}
	else if (AI_atVictoryStage(AI_VICTORY_CONQUEST1))
	{
		iMaxUnitSpending += 10;
	}

	if (!bTotalWar)
	{
		iMaxUnitSpending += AI_isDoStrategy(AI_STRATEGY_ALERT1) ? 15 + iBuildProb / 3 : 0;
		iMaxUnitSpending += AI_isDoStrategy(AI_STRATEGY_ALERT2) ? 15 + iBuildProb / 3 : 0;
		// note. the boost from alert1 + alert2 matches the boost from total war. (see below).
	}

	if (AI_isDoStrategy(AI_STRATEGY_FINAL_WAR))
	{
		iMaxUnitSpending += 300;
	}
	else
	{
		iMaxUnitSpending += bTotalWar ? 30 + iBuildProb*2/3 : 0;
		if (pArea != NULL)
		{
			switch (pArea->getAreaAIType(getTeam()))
			{
			case AREAAI_OFFENSIVE:
				iMaxUnitSpending += 40;
				break;

			case AREAAI_DEFENSIVE:
				iMaxUnitSpending += 75;
				break;

			case AREAAI_MASSING:
				iMaxUnitSpending += 75;
				break;

			case AREAAI_ASSAULT:
				iMaxUnitSpending += 40;
				break;

			case AREAAI_ASSAULT_MASSING:
				iMaxUnitSpending += 70;
				break;

			case AREAAI_ASSAULT_ASSIST:
				iMaxUnitSpending += 35;
				break;

			case AREAAI_NEUTRAL:
				// think of 'dagger' as being prep for total war.
				FAssert(!bTotalWar);
				iMaxUnitSpending += AI_isDoStrategy(AI_STRATEGY_DAGGER) ? 20 + iBuildProb*2/3 : 0;
				break;
			default:
				FAssert(false);
			}
		}
		else
		{	//if (GET_TEAM(getTeam()).getAnyWarPlanCount(true))
			if(AI_isFocusWar()) // advc.105
				iMaxUnitSpending += 55;
			else if(!bTotalWar) // advc.105: AI_isFocusWar doesn't guarantee this
			{
				//FAssert(!bTotalWar);
				iMaxUnitSpending += AI_isDoStrategy(AI_STRATEGY_DAGGER) ? 20 + iBuildProb*2/3 : 0;
			}
		}
	}
	return iMaxUnitSpending;
}

// When nukes are enabled, this function returns a percentage factor of how keen this player is to build nukes.
// The starting value is around 100, which corresponds to quite a low tendency to build nukes.
int CvPlayerAI::AI_nukeWeight() const
{
	//PROFILE_FUNC(); // advc.003o

	if (!GC.getGame().isNukesValid() || GC.getGame().isNoNukes())
		return 0;

	const CvTeamAI& kTeam = GET_TEAM(getTeam());
	// <advc.143b>
	if(kTeam.isCapitulated())
		return 0; // </advc.143b>
	int iNukeWeight = 100;

	// Increase the weight based on how many nukes the world has made & used so far.
	int iHistory = 2*GC.getGame().getNukesExploded() + GC.getGame().countTotalNukeUnits() - GC.getInfo(getPersonalityType()).getBasePeaceWeight();
	iHistory *= 35; // 5% for each nuke, 10% for each exploded
	iHistory /= std::max(1,
			//GC.getInfo(GC.getMap().getWorldSize()).getDefaultPlayers()
			GC.getGame().getRecommendedPlayers()); // advc.137
	iHistory = std::min(iHistory, 300);
	if (iHistory > 0)
	{
		iHistory *= 90 + GC.getInfo(getPersonalityType()).getConquestVictoryWeight();
		iHistory /= 100;
	}

	iNukeWeight += iHistory;

	// increase the weight if we were the team who enabled nukes. (look for projects only. buildings can get stuffed.)
	for (ProjectTypes i = (ProjectTypes)0; i < GC.getNumProjectInfos(); i = (ProjectTypes)(i+1))
	{
		if (GC.getInfo(i).isAllowsNukes() && kTeam.getProjectCount(i) > 0)
		{
			iNukeWeight += std::max(0, 150 - iHistory/2);
			break;
		}
	}
	// increase the weight for total war, or for the home-stretch to victory, or for losing wars.
	if (kTeam.AI_anyMemberAtVictoryStage4())
		iNukeWeight = iNukeWeight*3/2;
	else if (kTeam.AI_getNumWarPlans(WARPLAN_TOTAL) > 0 ||
			kTeam.AI_getNumWarPlans(WARPLAN_PREPARING_TOTAL) > 0 || // advc.650
			kTeam.AI_getWarSuccessRating() < -20)
		iNukeWeight = iNukeWeight*4/3;

	return iNukeWeight;
}

bool CvPlayerAI::AI_isLandWar(CvArea const& kArea) const
{
	switch(kArea.getAreaAIType(getTeam()))
	{
	case AREAAI_OFFENSIVE:
	case AREAAI_MASSING:
	case AREAAI_DEFENSIVE:
		return true;
	default:
		return false;
	}
}
// K-Mod end

/*  advc.105: To replace some of the
	GET_TEAM(getTeam()).getAnyWarPlanCount(true) > 0
	checks. */
bool CvPlayerAI::AI_isFocusWar(CvArea const* pArea) const
{
	CvCity* pCapital = getCapitalCity();
	if(pCapital == NULL)
		return false;
	if(pArea == NULL)
		pArea = pCapital->area();
	/*  Chosen wars (ongoing or in preparation) are always worth focusing on;
		others only when on the defensive. (In CvTeamAI::AI_calculateAreaAIType,
		bTargets and bDeclaredTargets are computed in a similar way .) */
	return (GET_TEAM(getTeam()).AI_isAnyChosenWar() ||
			pArea->getAreaAIType(getTeam()) == AREAAI_DEFENSIVE ||
			AI_isDoStrategy(AI_STRATEGY_ALERT2));
} // </advc.105>

int CvPlayerAI::AI_adjacentPotentialAttackers(CvPlot const& kPlot, bool bTestCanMove) const // advc: param was CvPlot*
{
	int iCount = 0;
	CvArea const& kPlotArea = kPlot.getArea(); // advc.030
	for (int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
	{
		CvPlot* pLoopPlot = plotDirection(kPlot.getX(), kPlot.getY(), (DirectionTypes)iI);
		if(pLoopPlot == NULL)
			continue; // advc
		// <advc.030>
		CvArea const& kFromArea = pLoopPlot->getArea();
		//if (pLoopPlot->area() == pPlot->area())
		// Replacing the above (negated):
		if(!kPlotArea.canBeEntered(kFromArea))
			continue; // </advc.030>
		for (CLLNode<IDInfo> const* pUnitNode = pLoopPlot->headUnitNode(); pUnitNode != NULL;
			pUnitNode = pLoopPlot->nextUnitNode(pUnitNode))
		{
			CvUnitAI* pLoopUnit = ::AI_getUnit(pUnitNode->m_data);
			if (pLoopUnit->getOwner() != getID())
				continue; // advc
			//if (pLoopUnit->getDomainType() == ((pPlot->isWater()) ? DOMAIN_SEA : DOMAIN_LAND))
			// advc.030: Replacing the above
			if(kPlotArea.canBeEntered(kFromArea, pLoopUnit))
			{
				if (pLoopUnit->canAttack()
				/*  advc.315: This way, no units with OnlyAttackBarbarians are counted
					as potential attackers. Could check if the CenterUnit of pPlot
					is Barbarian, but only Explorer has OnlyAttackBarbarians and
					I don't think the AI will or should use Explorers for any
					coordinated attacks anyway. */
					&& !pLoopUnit->getUnitInfo().isMostlyDefensive())
				{
					if (!bTestCanMove || pLoopUnit->canMove())
					{
						if (!pLoopUnit->AI_isCityAIType())
							iCount++;
					}
				}
			}
		}
	}
	return iCount;
}


int CvPlayerAI::AI_totalMissionAIs(MissionAITypes eMissionAI, CvSelectionGroup* pSkipSelectionGroup) const
{
	PROFILE_FUNC();

	int iCount = 0;
	FOR_EACH_GROUPAI(pLoopSelectionGroup, *this)
	{
		if (pLoopSelectionGroup == pSkipSelectionGroup)
			continue;

		if (pLoopSelectionGroup->AI_getMissionAIType() == eMissionAI)
			iCount += pLoopSelectionGroup->getNumUnits();
	}
	return iCount;
}

/*  <advc.171> Cut from AI_missionaryValue b/c I want to add a war-plan check that
	is also needed for CvUnitAI::AI_spreadReligion. Checking the other stuff
	for AI_spreadReligion isn't necessary, but doesn't hurt and might save time. */
bool CvPlayerAI::AI_isTargetForMissionaries(PlayerTypes eTarget, ReligionTypes eReligion) const
{
	CvPlayer& kTarget = GET_PLAYER(eTarget);
	/*  Tbd.: AI_missionaryValue and AI_spreadReligion handle (team-)internal spread
		differently (they probably shouldn't) */
	FAssert(getTeam() != kTarget.getTeam());
	if(!kTarget.isAlive() || kTarget.getNumCities() <= 0)
		return false;
	if(GET_TEAM(getTeam()).AI_isSneakAttackReady(kTarget.getTeam())) // advc
		return false;
	if(!GET_TEAM(kTarget.getTeam()).canPeacefullyEnter(getTeam())) // advc.001j
		return false;
	if(kTarget.isNoNonStateReligionSpread() && kTarget.getStateReligion() != eReligion &&
			kTarget.getStateReligion() != NO_RELIGION)
		return false;
	return true;
} // </advc.171>


int CvPlayerAI::AI_missionaryValue(ReligionTypes eReligion, CvArea const* pArea   // advc: some style changes; params switched
	/*  advc.171: Param unused.
		Tbd.: Add a function AI_missionaryTargetValue(PlayerTypes eTarget)
		to replace AI_isTargetForMissionaries and the overlapping
		target player evaluations here and in CvUnitAI::AI_spreadReligion. */
	/*,PlayerTypes* peBestPlayer*/) const
{
	CvTeam& kTeam = GET_TEAM(getTeam());
	CvGame& kGame = GC.getGame();

	int iSpreadInternalValue = 100;
	int iSpreadExternalValue = 0;
	/*  BETTER_BTS_AI_MOD, Victory Strategy AI, 03/08/10, jdog5000:
		Obvious copy & paste bug */
	if (AI_atVictoryStage(AI_VICTORY_CULTURE1))
	{
		iSpreadInternalValue += 500;
		if (AI_atVictoryStage(AI_VICTORY_CULTURE2))
		{
			iSpreadInternalValue += 1500;
			if (AI_atVictoryStage(AI_VICTORY_CULTURE3))
			{
				iSpreadInternalValue += 3000;
			}
		}
	} // BETTER_BTS_AI_MOD: END

	// BETTER_BTS_AI_MOD, Missionary AI, 10/03/09, jdog5000: START
	// In free religion, treat all religions like state religions
	bool bStateReligion = (getStateReligion() == eReligion);
	if (!isStateReligion())
	{	// Free religion
		iSpreadInternalValue += 500;
		bStateReligion = true;
	}
	else if(bStateReligion)
		iSpreadInternalValue += 1000;
	else
	{
		iSpreadInternalValue += (500 * getHasReligionCount(eReligion)) /
				std::max(1, getNumCities());
	}

	int iGoldValue = 0;
	if (kTeam.hasHolyCity(eReligion))
	{
		iSpreadInternalValue += bStateReligion ? 1000 : 300;
		iSpreadExternalValue += bStateReligion ? 1000 : 150;
		if (kTeam.hasShrine(eReligion))
		{
			iSpreadInternalValue += bStateReligion ? 500 : 300;
			iSpreadExternalValue += bStateReligion ? 300 : 200;
			int iGoldMultiplier = kGame.getHolyCity(eReligion)->getTotalCommerceRateModifier(COMMERCE_GOLD);
			iGoldValue = 6 * iGoldMultiplier;
			// K-Mod. todo: use GC.getInfo(eReligion).getGlobalReligionCommerce((CommerceTypes)iJ)
		}
	}

	int iOurCitiesHave = 0;
	int iOurCitiesCount = 0;

	if (pArea == NULL)
	{
		iOurCitiesHave = kTeam.getHasReligionCount(eReligion);
		iOurCitiesCount = kTeam.getNumCities();
	}
	else
	{
		iOurCitiesHave = pArea->countHasReligion(eReligion, getID()) + countReligionSpreadUnits(pArea, eReligion,true);
		iOurCitiesCount = pArea->getCitiesPerPlayer(getID());
	}

	if (iOurCitiesHave < iOurCitiesCount)
	{
		iSpreadInternalValue *= 30 + ((100 * (iOurCitiesCount - iOurCitiesHave))/ iOurCitiesCount);
		iSpreadInternalValue /= 100;
		iSpreadInternalValue += iGoldValue;
	}
	else iSpreadInternalValue = 0;

	if (iSpreadExternalValue > 0)
	{
		int iBestPlayer = NO_PLAYER;
		int iBestValue = 0;
		for (int iPlayer = 0; iPlayer < MAX_CIV_PLAYERS; iPlayer++)
		{
			CvPlayer& kLoopPlayer = GET_PLAYER((PlayerTypes)iPlayer);
			if (kLoopPlayer.getTeam() == getTeam() ||
					// advc.171: Checks moved into auxiliary function
					!AI_isTargetForMissionaries(kLoopPlayer.getID(), eReligion))
				continue;

			int iCitiesCount = 0;
			int iCitiesHave = 0;
			int iMultiplier = AI_isDoStrategy(AI_STRATEGY_MISSIONARY) ? 60 : 25;
			// advc.171: Checked by AI_isTargetForMissionaries now
			//if (!kLoopPlayer.isNoNonStateReligionSpread() || kLoopPlayer.getStateReligion() == eReligion) {
			if (NULL == pArea)
			{
				iCitiesCount += 1 + (kLoopPlayer.getNumCities() * 75) / 100;
				iCitiesHave += std::min(iCitiesCount, kLoopPlayer.getHasReligionCount(eReligion));
			}
			else
			{
				int iPlayerSpreadPercent = (100 * kLoopPlayer.getHasReligionCount(eReligion)) / kLoopPlayer.getNumCities();
				iCitiesCount += pArea->getCitiesPerPlayer((PlayerTypes)iPlayer);
				iCitiesHave += std::min(iCitiesCount, (iCitiesCount * iPlayerSpreadPercent) / 75);
			}//}

			if (kLoopPlayer.getStateReligion() == NO_RELIGION)
			{
				// Paganism counts as a state religion civic, that's what's caught below
				if (kLoopPlayer.getStateReligionCount() > 0)
				{
					int iTotalReligions = kLoopPlayer.countTotalHasReligion();
					iMultiplier += 100 * std::max(0, kLoopPlayer.getNumCities() - iTotalReligions);
					iMultiplier += (iTotalReligions == 0) ? 100 : 0;
				}
			}
			// <advc.171>
			if(GET_TEAM(getTeam()).AI_isSneakAttackPreparing(kLoopPlayer.getTeam()))
				iMultiplier /= 2;
			// </advc.171>
			int iValue = (iMultiplier * iSpreadExternalValue * (iCitiesCount - iCitiesHave)) / std::max(1, iCitiesCount);
			iValue /= 100;
			iValue += iGoldValue;

			if (iValue > iBestValue)
			{
				iBestValue = iValue;
				iBestPlayer = iPlayer;
			}
		}

		if (iBestValue > iSpreadInternalValue)
		{
			/*if (peBestPlayer != NULL)
				*peBestPlayer = (PlayerTypes)iBestPlayer;*/ // advc.171
			return iBestValue;
		}
	}

	/*if (peBestPlayer != NULL)
		*peBestPlayer = getID();*/ // advc.171

	return iSpreadInternalValue;
	// BETTER_BTS_AI_MOD: END
}

// K-Mod note: the original BtS code for this was totally inconsistent with the calculation in AI_missionaryValue
// -- which is bad news since the results are compared directly.
// I've rewritten most of this function so that it is more sane and more comparable to the missionary value.
// The original code is deleted.
// Currently, the return value has units of roughly (and somewhat arbitrarily) 1000 * commerce per turn.
int CvPlayerAI::AI_executiveValue(CorporationTypes eCorporation, CvArea const* pArea, // advc: switched these two params
	PlayerTypes* peBestPlayer, bool bSpreadOnly) const
{
	PROFILE_FUNC();
	CvGame& kGame = GC.getGame();
	CvCorporationInfo& kCorp = GC.getInfo(eCorporation);

	int iSpreadExternalValue = 0;
	int iExistingExecs = 0;
	if (pArea != NULL)
	{
		iExistingExecs += bSpreadOnly ? 0 : std::max(0, countCorporationSpreadUnits(pArea, eCorporation, true) - 1);
		// K-Mod note, -1 just so that we can build a spare, perhaps to airlift to another area. ("-1" execs is ok.)
		// bSpreadOnly means that we are not calculating the value of building a new exec. Just the value of spreading.
	}

	if (GET_TEAM(getTeam()).hasHeadquarters(eCorporation))
	{
		int iHqValue = 0;
		CvCity* kHqCity = kGame.getHeadquarters(eCorporation);
		for (CommerceTypes i = (CommerceTypes)0; i < NUM_COMMERCE_TYPES; i=(CommerceTypes)(i+1))
		{
			if (kCorp.getHeadquarterCommerce(i))
				iHqValue += kCorp.getHeadquarterCommerce(i) * kHqCity->getTotalCommerceRateModifier(i) * AI_commerceWeight(i)/100;
		}

		iSpreadExternalValue += iHqValue;
	}

	int iBestPlayer = NO_PLAYER;
	int iBestValue = 0;
	for (int iPlayer = 0; iPlayer < MAX_PLAYERS; iPlayer++)
	{
		const CvPlayerAI& kLoopPlayer = GET_PLAYER((PlayerTypes)iPlayer);
		int iNumCities = pArea ? pArea->getCitiesPerPlayer((PlayerTypes)iPlayer) : kLoopPlayer.getNumCities();
		if (kLoopPlayer.isAlive() && iNumCities > 0)
		{
			if (GET_TEAM(getTeam()).canPeacefullyEnter(kLoopPlayer.getTeam()))
			{
				if (!kLoopPlayer.isNoCorporations() && (iPlayer == getID() || !kLoopPlayer.isNoForeignCorporations()))
				{
					int iAttitudeWeight;

					if (kLoopPlayer.getTeam() == getTeam())
						iAttitudeWeight = 100;
					else if (GET_TEAM(kLoopPlayer.getTeam()).isVassal(getTeam()))
						iAttitudeWeight = 50;
					else
						iAttitudeWeight = AI_getAttitudeWeight((PlayerTypes)iPlayer) - (isHuman() ? 100 : 75);
					// note: this is to discourage automated human units from spreading to the AI, not AI to human.

					// a rough check to save us some time.
					if (iAttitudeWeight <= 0 && iSpreadExternalValue <= 0)
						continue;

					int iCitiesHave = kLoopPlayer.countCorporations(eCorporation, pArea);

					if (iCitiesHave + iExistingExecs >= iNumCities)
						continue;

					int iCorpValue = kLoopPlayer.AI_corporationValue(eCorporation);
					int iValue = iSpreadExternalValue;
					iValue += (iCorpValue * iAttitudeWeight)/100;
					if (iValue > 0 && iCorpValue > 0 && iCitiesHave == 0 && iExistingExecs == 0)
					{
						// if the player will spread the corp themselves, then that's good for us.
						if (iAttitudeWeight >= 50)
						{
							// estimate spread to 2/3 of total cities.
							iValue *= (2*kLoopPlayer.getNumCities()+1)/3;
						}
						else
						{
							// estimate spread to 1/4 of total cities, rounded up.
							iValue *= (kLoopPlayer.getNumCities()+3)/4;
						}
					}
					if (iValue > iBestValue)
					{
						for (int iCorp = 0; iCorp < GC.getNumCorporationInfos(); iCorp++)
						{
							if (kGame.isCorporationFounded((CorporationTypes)iCorp) && kGame.isCompetingCorporation(eCorporation, (CorporationTypes)iCorp))
							{
								int iExtra = kLoopPlayer.countCorporations((CorporationTypes)iCorp, pArea);

								if (iExtra > 0 && iCorpValue > kLoopPlayer.AI_corporationValue((CorporationTypes)iCorp)*4/3) // (ideally, hq should be considered too)
									iExtra /= 2;

								iCitiesHave += iExtra;
							}
						}

						if (iCitiesHave + iExistingExecs >= iNumCities)
							continue;

						iBestValue = iValue;
						iBestPlayer = iPlayer;
					}
				}
			}
		}
	}

	if (peBestPlayer != NULL)
		*peBestPlayer = (PlayerTypes)iBestPlayer;

	// I'm putting in a fudge-factor of 10 just to bring the value up to scale with AI_missionaryValue.
	// This isn't something that i'm happy about, but it's easier than rewriting AI_missionaryValue.
	return 10 * iBestValue;
}

// This function has been completely rewritten for K-Mod. The original code has been deleted. (it was junk)
// Returns approximately 100 x gpt value of the corporation, for one city.
int CvPlayerAI::AI_corporationValue(CorporationTypes eCorporation, CvCityAI const* pCity) const // advc.003u: param was const CvCity*
{
	const CvTeamAI& kTeam = GET_TEAM(getTeam());
	CvCorporationInfo& kCorp = GC.getInfo(eCorporation);
	int iValue = 0;
	int iMaintenance = 0;

	int iBonuses = 0;
	for (int i = 0; i < GC.getNUM_CORPORATION_PREREQ_BONUSES(); ++i)
	{
		BonusTypes eBonus = (BonusTypes)kCorp.getPrereqBonus(i);
		if (NO_BONUS != eBonus)
		{
			if (pCity == NULL)
				iBonuses += AI_countOwnedBonuses(eBonus);
			else iBonuses += pCity->getNumBonuses(eBonus);
			// maybe use getNumAvailableBonuses ?
			if (!kTeam.isHasTech((TechTypes)GC.getInfo(eBonus).getTechReveal()) && !kTeam.isForceRevealedBonus(eBonus))
			{
				iBonuses++; // expect that we'll get one of each unrevealed resource
			}
		}
	}
	if (!GC.getGame().isCorporationFounded(eCorporation))
	{
		// expect that we'll be able to trade for at least 1 of the bonuses.
		iBonuses++;
	}

	for (int iI = (CommerceTypes)0; iI < NUM_COMMERCE_TYPES; ++iI)
	{
		int iTempValue = kCorp.getCommerceProduced((CommerceTypes)iI) * iBonuses;

		if (iTempValue != 0)
		{
			if (pCity == NULL)
				iTempValue *= AI_averageCommerceMultiplier((CommerceTypes)iI);
			else
				iTempValue *= pCity->getTotalCommerceRateModifier((CommerceTypes)iI);
			// I'd feel more comfortable if they named it "multiplier" when it included the base 100%.
			iTempValue /= 100;

			iTempValue *= GC.getInfo(GC.getMap().getWorldSize()).getCorporationMaintenancePercent();
			iTempValue /= 100;

			iTempValue *= AI_commerceWeight((CommerceTypes)iI, pCity);
			iTempValue /= 100;

			iValue += iTempValue;
		}

		iMaintenance += 100 * kCorp.getHeadquarterCommerce(iI);
	}

	for (int iI = 0; iI < NUM_YIELD_TYPES; ++iI)
	{
		int iTempValue = kCorp.getYieldProduced((YieldTypes)iI) * iBonuses;
		if (iTempValue != 0)
		{
			if (pCity == NULL)
				iTempValue *= AI_averageYieldMultiplier((YieldTypes)iI);
			else
				iTempValue *= pCity->getBaseYieldRateModifier((YieldTypes)iI);

			iTempValue /= 100;
			iTempValue *= GC.getInfo(GC.getMap().getWorldSize()).getCorporationMaintenancePercent();
			iTempValue /= 100;

			iTempValue *= AI_yieldWeight((YieldTypes)iI);
			iTempValue /= 100;

			iValue += iTempValue;
		}
	}

	// maintenance cost
	int iTempValue = kCorp.getMaintenance() * iBonuses;
	iTempValue *= GC.getInfo(GC.getMap().getWorldSize()).getCorporationMaintenancePercent();
	iTempValue /= 100;
	iTempValue += iMaintenance;
	// Inflation, population, and maintenance modifiers... lets just approximate them like this:
	iTempValue *= 2;
	iTempValue /= 3;

	iTempValue *= AI_commerceWeight(COMMERCE_GOLD);
	iTempValue /= 100;

	iValue -= iTempValue;

	// bonus produced by the corp
	BonusTypes eBonusProduced = (BonusTypes)kCorp.getBonusProduced();
	if (eBonusProduced != NO_BONUS)
	{
		//int iBonuses = getNumAvailableBonuses((BonusTypes)kCorp.getBonusProduced());
		iBonuses = pCity ? pCity->getNumBonuses(eBonusProduced) : AI_countOwnedBonuses(eBonusProduced);
		// pretend we have 1 bonus if it is not yet revealed. (so that we don't overvalue the corp before the resource gets revealed)
		iBonuses += !kTeam.isHasTech((TechTypes)GC.getInfo(eBonusProduced).getTechReveal()) ? 1 : 0;
		iValue += AI_baseBonusVal(eBonusProduced) * 25 / (1 + 2 * iBonuses * (iBonuses+3));
	}

	return iValue;
}

int CvPlayerAI::AI_areaMissionAIs(CvArea const& kArea, MissionAITypes eMissionAI,
	CvSelectionGroup* pSkipSelectionGroup) const
{
	PROFILE_FUNC();

	int iCount = 0;
	FOR_EACH_GROUPAI(pLoopSelectionGroup, *this)
	{
		if (pLoopSelectionGroup == pSkipSelectionGroup)
			continue;

		if (pLoopSelectionGroup->AI_getMissionAIType() == eMissionAI)
		{
			CvPlot* pMissionPlot = pLoopSelectionGroup->AI_getMissionAIPlot();
			if (pMissionPlot != NULL)
			{
				if (pMissionPlot->isArea(kArea))
					iCount += pLoopSelectionGroup->getNumUnits();
			}
		}
	}
	return iCount;
}


int CvPlayerAI::AI_plotTargetMissionAIs(CvPlot const* pPlot, MissionAITypes eMissionAI, CvSelectionGroup* pSkipSelectionGroup, int iRange) const
{
	int iClosestTargetRange;
	return AI_plotTargetMissionAIs(pPlot, &eMissionAI, 1, iClosestTargetRange, pSkipSelectionGroup, iRange);
}

int CvPlayerAI::AI_plotTargetMissionAIs(CvPlot const* pPlot, MissionAITypes eMissionAI, int& iClosestTargetRange, CvSelectionGroup* pSkipSelectionGroup, int iRange) const
{
	return AI_plotTargetMissionAIs(pPlot, &eMissionAI, 1, iClosestTargetRange, pSkipSelectionGroup, iRange);
}

int CvPlayerAI::AI_plotTargetMissionAIs(CvPlot const* pPlot, MissionAITypes* aeMissionAI, int iMissionAICount, int& iClosestTargetRange, CvSelectionGroup* pSkipSelectionGroup, int iRange) const  // advc: some style changes
{
	//PROFILE_FUNC(); // advc.003o
	int iCount = 0;
	iClosestTargetRange = MAX_INT;

	FOR_EACH_GROUPAI(pLoopSelectionGroup, *this)
	{
		if (pLoopSelectionGroup == pSkipSelectionGroup)
			continue;

		CvPlot* pMissionPlot = pLoopSelectionGroup->AI_getMissionAIPlot();
		if (pMissionPlot == NULL)
			continue;

		MissionAITypes eGroupMissionAI = pLoopSelectionGroup->AI_getMissionAIType();
		int iDistance = ::stepDistance(pPlot->getX(), pPlot->getY(),
			pMissionPlot->getX(), pMissionPlot->getY());

		if (iDistance <= iRange)
		{
			for (int iMissionAIIndex = 0; iMissionAIIndex < iMissionAICount; iMissionAIIndex++)
			{
				if (eGroupMissionAI == aeMissionAI[iMissionAIIndex] || aeMissionAI[iMissionAIIndex] == NO_MISSIONAI)
				{
					iCount += pLoopSelectionGroup->getNumUnits();

					if (iDistance < iClosestTargetRange)
					{
						iClosestTargetRange = iDistance;
					}
				}
			}
		}
	}
	return iCount;
}

// K-Mod

// Total defensive strength of units that can move iRange steps to reach pDefencePlot
/*  advc.159 (note): This is not simply the sum of the relevant combat strength values.
	The result should only be compared with AI_localAttackStrength, AI_localDefenceStrength,
	AI_cityTargetStrengthByPath or CvSelectionGroupAI::AI_sumStrength. */
int CvPlayerAI::AI_localDefenceStrength(const CvPlot* pDefencePlot, TeamTypes eDefenceTeam,  // advc: some style changes in the body
	DomainTypes eDomainType, int iRange, bool bAtTarget, bool bCheckMoves, bool bNoCache,
	bool bPredictPromotions) const // advc.139
{
	PROFILE_FUNC();

	int	iTotal = 0;

	FAssert(bAtTarget || !bCheckMoves); // it doesn't make much sense to check moves if the defenders are meant to stay put.
	FAssert(eDomainType != DOMAIN_AIR && eDomainType != DOMAIN_IMMOBILE); // advc: Air combat strength isn't counted
	for (SquareIter it(*pDefencePlot, iRange); it.hasNext(); ++it)
	{
		CvPlot const& p = *it;
		if (!p.isVisible(getTeam()))
			continue;

		int iPlotTotal = 0;
		for (CLLNode<IDInfo> const* pUnitNode = p.headUnitNode(); pUnitNode != NULL;
			pUnitNode = p.nextUnitNode(pUnitNode))
		{
			CvUnitAI const& kLoopUnit = *::AI_getUnit(pUnitNode->m_data);
			// <advc.opt>
			if (!kLoopUnit.canFight())
				continue; // </advc.opt>
			// advc (note): This doesn't respect hidden nationality
			if (kLoopUnit.getTeam() == eDefenceTeam ||
				(eDefenceTeam != NO_TEAM && GET_TEAM(kLoopUnit.getTeam()).isVassal(eDefenceTeam)) ||
				(eDefenceTeam == NO_TEAM && GET_TEAM(getTeam()).AI_mayAttack(kLoopUnit.getTeam())))
			{
				if (eDomainType != NO_DOMAIN && kLoopUnit.getDomainType() != eDomainType)
					continue;
				// <advc.opt>
				if (kLoopUnit.isDead())
					continue; // </advc.opt>
				if (bCheckMoves)
				{
					/*	unfortunately, we can't use the global pathfinder here
						- because the calling function might be waiting to use some pathfinding results
						So this check will have to be really rough. :( */
					int iMoves = kLoopUnit.baseMoves();
					if (p.isValidRoute(&kLoopUnit, /* advc.001i: */ false))
						iMoves++;
					if (it.currStepDist() > iMoves)
						continue; // can't make it. (maybe?)
				}
				// <advc.139>
				int iHP = kLoopUnit.currHitPoints();
				bool const bAssumePromo = (bPredictPromotions && kLoopUnit.isPromotionReady());
				if (bAssumePromo && kLoopUnit.getDamage() > 0)
				{
					iHP += kLoopUnit.promotionHeal();
					iHP = std::min(kLoopUnit.maxHitPoints(), iHP);
				} // </advc.139>
				/*  <advc.159> Call AI_currEffectiveStr instead of currEffectiveStr.
					Adjustments for first strikes are handled by that new function. */
				int const iUnitStr = kLoopUnit.AI_currEffectiveStr(
						bAtTarget ? pDefencePlot : &p, // </advc.159>
						NULL, false, 0, false, iHP, bAssumePromo); // advc.139
				iPlotTotal += iUnitStr;
			}
		}
		if (!bNoCache && !isHuman() && eDefenceTeam == NO_TEAM &&
			eDomainType == DOMAIN_LAND && !bCheckMoves &&
			(!bAtTarget || &p == pDefencePlot))
		{
			// while since we're here, we might as well update our memory.
			// (human players don't track strength memory)
			GET_TEAM(getTeam()).AI_setStrengthMemory(&p, iPlotTotal);
			FAssert(isTurnActive());
		}
		iTotal += iPlotTotal;
	}

	return iTotal;
}

// Total attack strength of units that can move iRange steps to reach pAttackPlot
// advc.159: See note above AI_localDefenceStrength  
int CvPlayerAI::AI_localAttackStrength(const CvPlot* pTargetPlot, TeamTypes eAttackTeam,  // advc: some style changes in the body
	DomainTypes eDomainType, int iRange, bool bUseTarget, bool bCheckMoves, bool bCheckCanAttack,
	int* piAttackerCount) const // advc.139
{
	PROFILE_FUNC();

	int iBaseCollateral = bUseTarget
		? estimateCollateralWeight(pTargetPlot, getTeam())
		: estimateCollateralWeight(0, getTeam());

	int	iTotal = 0;
	// <advc.139>
	if(piAttackerCount != NULL)
		*piAttackerCount = 0; // </advc.139>

	for (SquareIter it(*pTargetPlot, iRange); it.hasNext(); ++it)
	{
		CvPlot const& p = *it;
		if (!p.isVisible(getTeam()))
			continue;

		for (CLLNode<IDInfo> const* pUnitNode = p.headUnitNode(); pUnitNode != NULL;
			pUnitNode = p.nextUnitNode(pUnitNode))
		{
			CvUnitAI const& kLoopUnit = *::AI_getUnit(pUnitNode->m_data);
			if (kLoopUnit.getTeam() == eAttackTeam || (eAttackTeam == NO_TEAM &&
				::atWar(getTeam(), kLoopUnit.getTeam())))
			{
				if (eDomainType == NO_DOMAIN || kLoopUnit.getDomainType() == eDomainType)
				{
					if (!kLoopUnit.canAttack()) // bCheckCanAttack means something else.
						continue;
					// <advc.315> See comment in AI_adjacentPotentialAttackers
					if (kLoopUnit.getUnitInfo().isMostlyDefensive())
						continue; // </advc.315>
					if (bCheckMoves)
					{
						// can't use the global pathfinder here (see comment in AI_localDefenceStrength)
						int iMoves = kLoopUnit.baseMoves();
						if (p.isValidRoute(&kLoopUnit, /* advc.001i: */ false))
							iMoves++;
						if (it.currStepDist() > iMoves)
							continue; // can't make it. (maybe?)
					}
					if (bCheckCanAttack)
					{
						if (!kLoopUnit.canMove() ||
							//(pLoopUnit->isMadeAttack() && !pLoopUnit->isBlitz())
							kLoopUnit.isMadeAllAttacks() || // advc.164
							(bUseTarget && kLoopUnit.combatLimit() < 100 &&
							p.isWater() && !pTargetPlot->isWater()))
						{
							continue; // can't attack
						}
					}
					/*  <advc.159> Call AI_currEffectiveStr instead of currEffectiveStr.
						Adjustments for first strikes and collateral damage are handled
						by that new function. */
					int const iUnitStr = kLoopUnit.AI_currEffectiveStr(
							bUseTarget ? pTargetPlot : NULL, bUseTarget ? &kLoopUnit : NULL,
							true, iBaseCollateral, bCheckCanAttack); // </advc.159>
					iTotal += iUnitStr;
					// <advc.139>
					if (piAttackerCount != NULL && kLoopUnit.getDamage() < 30)
					{
						// 80 is Cannon; don't count the early siege units.
						if(kLoopUnit.combatLimit() >= 80)
							(*piAttackerCount)++;
					} // </advc.139>
				}
			}
		}
	}
	return iTotal;
} // K-Mod end

// BETTER_BTS_AI_MOD, General AI, 04/03/10, jdog5000: START
// K-Mod. I've changed this function to calculation the attack power of our groups, rather than just the number of units.
// I've also changed it to use a separate instance of the path finder, so that it doesn't clear the reset the existing path data.
//int CvPlayerAI::AI_cityTargetUnitsByPath(
int CvPlayerAI::AI_cityTargetStrengthByPath(CvCity const* pCity,  // advc: const CvCity*; some style changes
	CvSelectionGroup* pSkipSelectionGroup, int iMaxPathTurns) const
{
	PROFILE_FUNC();

	//int iCount = 0;
	int iTotalStrength = 0;
	KmodPathFinder temp_finder;

	FOR_EACH_GROUPAI(pLoopSelectionGroup, *this)
	{
		if (pLoopSelectionGroup == pSkipSelectionGroup ||
				pLoopSelectionGroup->getNumUnits() <= 0)
			continue;

		FAssert(pLoopSelectionGroup->plot() != NULL); // this use to be part of the above condition. I've turned it into an assert. K-Mod.
		CvPlot* pMissionPlot = pLoopSelectionGroup->AI_getMissionAIPlot();
		if (pMissionPlot == NULL)
			continue;

		int iDistance = ::stepDistance(pCity->getX(), pCity->getY(),
			pMissionPlot->getX(), pMissionPlot->getY());
		if (iDistance <= 1 && pLoopSelectionGroup->canFight())
		{	/*int iPathTurns;
			if (pLoopSelectionGroup->generatePath(pLoopSelectionGroup->plot(), pMissionPlot, 0, true, &iPathTurns, iMaxPathTurns)) {
				if (!pLoopSelectionGroup->canAllMove())
					iPathTurns++;
				if (iPathTurns <= iMaxPathTurns)
					iCount += pLoopSelectionGroup->getNumUnits();
			}*/ // BtS
			temp_finder.SetSettings(CvPathSettings(pLoopSelectionGroup, 0, iMaxPathTurns, GC.getMOVE_DENOMINATOR()));
			if (temp_finder.GeneratePath(pMissionPlot))
			{
				iTotalStrength += pLoopSelectionGroup->AI_sumStrength(pCity->plot(), DOMAIN_LAND);
			}
		}
	}
	//return iCount;
	return iTotalStrength;
}
/*  <advc.139> May have to update city safety status after one of our units
	has attacked (possible relief). kDefender can be dead, but all its data
	should still be acurate. */
void CvPlayerAI::AI_attackMadeAgainst(CvUnit const& kDefender)
{
	// Same checks as in AI_localAttackStrength essentially
	if (kDefender.getDomainType() != DOMAIN_LAND || !kDefender.canAttack() ||
		kDefender.getUnitInfo().isMostlyDefensive() || // advc.315
		kDefender.getPlot().isCity())
	{
		return;
	}
	int const iRange = kDefender.baseMoves() + (kDefender.getPlot().isValidRoute(
			&kDefender, /* advc.001i: */ false) ? 1 : 0);
	for (SquareIter it(kDefender, iRange); it.hasNext(); ++it)
	{
		CvPlot const& kPlot = *it;
		if (kPlot.getOwner() == getID() && kPlot.isCity())
		{
			CvCityAI& kCity = *kPlot.AI_getPlotCity();
			if (!kCity.AI_isSafe()) // It's only going to get safer
			{
				// advc.test:
				FAssertMsg(false, "Only to see how frequently this happens (or rather: just how rarely)");
				kCity.AI_updateSafety();
			}
		}
	}
} // </advc.139>

int CvPlayerAI::AI_unitTargetMissionAIs(CvUnit const* pUnit, MissionAITypes eMissionAI, CvSelectionGroup* pSkipSelectionGroup) const // advc: const CvUnit*
{
	return AI_unitTargetMissionAIs(pUnit, &eMissionAI, 1, pSkipSelectionGroup, -1);
}

int CvPlayerAI::AI_unitTargetMissionAIs(CvUnit const* pUnit, MissionAITypes* aeMissionAI, int iMissionAICount, CvSelectionGroup* pSkipSelectionGroup) const // advc: const CvUnit*
{
	return AI_unitTargetMissionAIs(pUnit, aeMissionAI, iMissionAICount, pSkipSelectionGroup, -1);
}

int CvPlayerAI::AI_unitTargetMissionAIs(CvUnit const* pUnit, MissionAITypes* aeMissionAI, int iMissionAICount, CvSelectionGroup* pSkipSelectionGroup, int iMaxPathTurns) const  // advc: const CvUnit*; some style changes
{
	PROFILE_FUNC();

	int iCount = 0;
	FOR_EACH_GROUPAI(pLoopSelectionGroup, *this)
	{
		if (pLoopSelectionGroup == pSkipSelectionGroup ||
				pLoopSelectionGroup->AI_getMissionAIUnit() != pUnit)
			continue;

		MissionAITypes eGroupMissionAI = pLoopSelectionGroup->AI_getMissionAIType();
		// advc.opt: Moved up b/c this ought to be cheaper than generatePath
		bool bValid = false;
		for (int iMissionAIIndex = 0; iMissionAIIndex < iMissionAICount; iMissionAIIndex++)
		{
			if (eGroupMissionAI == aeMissionAI[iMissionAIIndex] ||
				NO_MISSIONAI == aeMissionAI[iMissionAIIndex])
			{	/*  <advc.040> Kludge to address a problem with a transport
					waiting for a passenger that is already loaded */
				if((eGroupMissionAI == MISSIONAI_LOAD_SETTLER ||
					eGroupMissionAI == MISSIONAI_LOAD_SPECIAL ||
					eGroupMissionAI == MISSIONAI_LOAD_ASSAULT) &&
					pLoopSelectionGroup->getHeadUnit() != NULL &&
					pLoopSelectionGroup->getHeadUnit()->isCargo())
				{
					bValid = false;
					break;
				} // </advc.040>  <advc.opt>
				bValid = true;
				break;
			}
		}
		if(!bValid)
			continue; // </advc.opt>
		int iPathTurns = MAX_INT;
		if (iMaxPathTurns >= 0 && pUnit->plot() != NULL &&
			pLoopSelectionGroup->plot() != NULL)
		{
			pLoopSelectionGroup->generatePath(pLoopSelectionGroup->plot(),
					pUnit->plot(), 0, false, &iPathTurns);
			if (!pLoopSelectionGroup->canAllMove())
				iPathTurns++;
		}
		if (iMaxPathTurns == -1 || iPathTurns <= iMaxPathTurns)
			iCount += pLoopSelectionGroup->getNumUnits();
	}
	return iCount;
}
// BETTER_BTS_AI_MOD: END

int CvPlayerAI::AI_enemyTargetMissionAIs(MissionAITypes eMissionAI, CvSelectionGroup* pSkipSelectionGroup) const
{
	return AI_enemyTargetMissionAIs(&eMissionAI, 1, pSkipSelectionGroup);
}

int CvPlayerAI::AI_enemyTargetMissionAIs(MissionAITypes* aeMissionAI, int iMissionAICount, CvSelectionGroup* pSkipSelectionGroup) const
{
	PROFILE_FUNC();

	int iCount = 0;
	FOR_EACH_GROUPAI(pLoopSelectionGroup, *this)
	{
		if (pLoopSelectionGroup == pSkipSelectionGroup)
			continue;

		CvPlot* pMissionPlot = pLoopSelectionGroup->AI_getMissionAIPlot();
		if (pMissionPlot != NULL && pMissionPlot->isOwned())
		{
			MissionAITypes eGroupMissionAI = pLoopSelectionGroup->AI_getMissionAIType();
			for (int iMissionAIIndex = 0; iMissionAIIndex < iMissionAICount; iMissionAIIndex++)
			{
				if (eGroupMissionAI == aeMissionAI[iMissionAIIndex] || NO_MISSIONAI == aeMissionAI[iMissionAIIndex])
				{
					if (GET_TEAM(getTeam()).AI_isChosenWar(pMissionPlot->getTeam()))
					{
						iCount += pLoopSelectionGroup->getNumUnits();
						iCount += pLoopSelectionGroup->getCargo();
					}
				}
			}
		}
	}
	return iCount;
}


// BETTER_BTS_AI_MOD, General AI, 05/19/10, jdog5000: START
int CvPlayerAI::AI_enemyTargetMissions(TeamTypes eTargetTeam, CvSelectionGroup* pSkipSelectionGroup) const
{
	PROFILE_FUNC();

	int iCount = 0;
	FOR_EACH_GROUPAI(pLoopSelectionGroup, *this)
	{
		if (pLoopSelectionGroup == pSkipSelectionGroup)
			continue;

		CvPlot* pMissionPlot = pLoopSelectionGroup->AI_getMissionAIPlot();
		if (pMissionPlot == NULL)
			pMissionPlot = pLoopSelectionGroup->plot();

		if (pMissionPlot != NULL && pMissionPlot->isOwned() &&
			pMissionPlot->getTeam() == eTargetTeam)
		{
			if (::atWar(getTeam(), pMissionPlot->getTeam()) ||
				pLoopSelectionGroup->AI_isDeclareWar(*pMissionPlot))
			{
				iCount += pLoopSelectionGroup->getNumUnits();
				iCount += pLoopSelectionGroup->getCargo();
			}
		}
	}
	return iCount;
} // BETTER_BTS_AI_MOD: END


int CvPlayerAI::AI_wakePlotTargetMissionAIs(CvPlot* pPlot, MissionAITypes eMissionAI, CvSelectionGroup* pSkipSelectionGroup) const
{
	PROFILE_FUNC();

	FAssert(pPlot != NULL);

	int iCount = 0;
	FOR_EACH_GROUPAI_VAR(pLoopSelectionGroup, *this)
	{
		if (pLoopSelectionGroup == pSkipSelectionGroup)
			continue;

		MissionAITypes eGroupMissionAI = pLoopSelectionGroup->AI_getMissionAIType();
		if (eMissionAI == NO_MISSIONAI || eMissionAI == eGroupMissionAI)
		{
			CvPlot* pMissionPlot = pLoopSelectionGroup->AI_getMissionAIPlot();
			if (pMissionPlot != NULL && pMissionPlot == pPlot)
			{
				iCount += pLoopSelectionGroup->getNumUnits();
				pLoopSelectionGroup->setActivityType(ACTIVITY_AWAKE);
			}
		}
	}
	return iCount;
}

// K-Mod, I've added piBestValue, and tidied up some stuff.
CivicTypes CvPlayerAI::AI_bestCivic(CivicOptionTypes eCivicOption, int* piBestValue) const
{
	CivicTypes eBestCivic = NO_CIVIC;
	int iBestValue = MIN_INT;
	for (int iI = 0; iI < GC.getNumCivicInfos(); iI++)
	{
		if (GC.getInfo((CivicTypes)iI).getCivicOptionType() == eCivicOption)
		{
			if (canDoCivics((CivicTypes)iI))
			{
				int iValue = AI_civicValue((CivicTypes)iI);

				if (iValue > iBestValue)
				{
					iBestValue = iValue;
					eBestCivic = (CivicTypes)iI;
				}
			}
		}
	}
	if (piBestValue)
		*piBestValue = iBestValue;
	return eBestCivic;
}

// The bulk of this function has been rewritten for K-Mod. (some original code deleted, some edited by BBAI)
// Note: the value is roughly in units of commerce per turn.
// Also, this function could probably be made a bit more accurate and perhaps even faster if it calculated effects on a city-by-city basis,
// rather than averaging effects across all cities. (certainly this would work better for happiness modifiers.)
int CvPlayerAI::AI_civicValue(CivicTypes eCivic) const
{
	PROFILE_FUNC();

	if(eCivic == NO_CIVIC) // Circumvents crash bug in simultaneous turns MP games
		return 1;
	if (isBarbarian())
		return 1;

	CvCivicInfo const& kCivic = GC.getInfo(eCivic);
	CvTeamAI const& kTeam = GET_TEAM(getTeam()); // K-Mod
	CvGameAI const& kGame = GC.AI_getGame(); // K-Mod
	int const iCities = getNumCities();
	// K-Mod, sign for whether we should be considering gaining a bonus, or losing a bonus
	int const iS = (isCivic(eCivic) ? -1 : 1);

	bool bWarPlan = kTeam.AI_isAnyWarPlan();
	if (bWarPlan)
	{
		bWarPlan = false;
		int iEnemyWarSuccess = 0;
		for (int iTeam = 0; iTeam < MAX_CIV_TEAMS; iTeam++)
		{
			if (GET_TEAM((TeamTypes)iTeam).isAlive() && !GET_TEAM((TeamTypes)iTeam).isMinorCiv())
			{
				WarPlanTypes eWarPlan = kTeam.AI_getWarPlan((TeamTypes)iTeam); // advc
				if (eWarPlan != NO_WARPLAN)
				{
					if (eWarPlan == WARPLAN_TOTAL || eWarPlan == WARPLAN_PREPARING_TOTAL)
					{
						bWarPlan = true;
						break;
					}
					if (kTeam.AI_isLandTarget((TeamTypes)iTeam))
					{
						bWarPlan = true;
						break;
					}
					iEnemyWarSuccess += GET_TEAM((TeamTypes)iTeam).AI_getWarSuccess(getTeam());
				}
			}
		}

		if (!bWarPlan)
		{
			if (iEnemyWarSuccess > std::min(iCities, 4) * GC.getWAR_SUCCESS_CITY_CAPTURING())
			{
				// Lots of fighting, so war is real
				bWarPlan = true;
			}
			else if (iEnemyWarSuccess > std::min(iCities, 2) * GC.getWAR_SUCCESS_CITY_CAPTURING())
			{
				if (kTeam.AI_getEnemyPowerPercent() > 120)
				{
					bWarPlan = true;
				}
			}
		}
	}

	if (!bWarPlan)
	{
		// Aggressive players will stick with war civics
		if (kTeam.AI_getTotalWarOddsTimes100() > 200)
			bWarPlan = true;
	}

	//int iConnectedForeignCities = countPotentialForeignTradeCitiesConnected();
	int iTotalReligonCount = countTotalHasReligion();
	ReligionTypes eBestReligion = getStateReligion();
	int iBestReligionPopulation = 0;
	if (kCivic.isStateReligion())
	{
		ReligionTypes temp = AI_bestReligion(); // only calculate best religion if this is a religious civic
		if (temp != NO_RELIGION)
			eBestReligion = temp;

		if (eBestReligion != NO_RELIGION)
		{
			FOR_EACH_CITY(pLoopCity, *this)
				iBestReligionPopulation += pLoopCity->isHasReligion(eBestReligion) ? pLoopCity->getPopulation() : 0;
		}
	}
	int iBestReligionCities = eBestReligion == NO_RELIGION ? 0 : getHasReligionCount(eBestReligion);
	int iWarmongerFactor = 25000 / std::max(100, (100 + GC.getInfo(getPersonalityType()).getMaxWarRand()));
	// K-Mod note. max war rand is between 50 and 400, so I've renamed the above number from iWarmongerPercent to iWarmongerFactor.
	// I don't know what it is meant to be a percentage of. It's a number roughly between 56 and 167.

	int iMaintenanceFactor =  AI_commerceWeight(COMMERCE_GOLD) * std::max(0, calculateInflationRate() + 100) / 100; // K-Mod

	int iValue = (iCities * 6);

	iValue += (GC.getInfo(eCivic).getAIWeight() * iCities);

	// K-Mod: civic anger is counted somewhere else
	//iValue += (getCivicPercentAnger(eCivic) / 10);

	iValue += -(GC.getInfo(eCivic).getAnarchyLength() * iCities);

	//iValue += -(getSingleCivicUpkeep(eCivic, true)*80)/100;
	iValue -= getSingleCivicUpkeep(eCivic, true) * iMaintenanceFactor / 100; // K-Mod. (note. upkeep modifiers are included in getSingleCivicUpkeep.)

	CvCityAI const* pCapital = AI_getCapitalCity();
	iValue += ((kCivic.getGreatPeopleRateModifier() * iCities) / 10);
	iValue += ((kCivic.getGreatGeneralRateModifier() * getNumMilitaryUnits()) / 50);
	iValue += ((kCivic.getDomesticGreatGeneralRateModifier() * getNumMilitaryUnits()) / 100);
	/*iValue += -((kCivic.getDistanceMaintenanceModifier() * std::max(0, (iCities - 3))) / 8);
	iValue += -((kCivic.getNumCitiesMaintenanceModifier() * std::max(0, (iCities - 3))) / 8);*/ // BtS
	// K-Mod. After looking at a couple of examples, it's plain to see that the above maintenance estimates are far too big.
	// Surprisingly, it actually doesn't take much time to calculate the precise magnitude of the maintenance change. So that's what I'll do!
	if (kCivic.getNumCitiesMaintenanceModifier() != 0)
	{
		PROFILE("civicValue: NumCitiesMaintenance");
		int iTemp = 0;
		FOR_EACH_CITY(pLoopCity, *this)
		{
			iTemp += pLoopCity->calculateNumCitiesMaintenanceTimes100() *
				(pLoopCity->getMaintenanceModifier() + 100) / 100;
		}
		iTemp *= 100;
		iTemp /= std::max(1, getNumCitiesMaintenanceModifier() + 100);

		iTemp *= iMaintenanceFactor;
		iTemp /= 100;

		iValue -= iTemp * kCivic.getNumCitiesMaintenanceModifier() / 10000;
	}
	if (kCivic.getDistanceMaintenanceModifier() != 0)
	{
		PROFILE("civicValue: DistanceMaintenance");
		int iTemp = 0;
		FOR_EACH_CITY(pLoopCity, *this)
		{
			iTemp += pLoopCity->calculateDistanceMaintenanceTimes100() *
				(pLoopCity->getMaintenanceModifier() + 100) / 100;
		}
		iTemp *= 100;
		iTemp /= std::max(1, getDistanceMaintenanceModifier() + 100);

		iTemp *= iMaintenanceFactor;
		iTemp /= 100;

		iValue -= iTemp * kCivic.getDistanceMaintenanceModifier() / 10000;
	}
	// K-Mod end

	/*iValue += ((kCivic.getWorkerSpeedModifier() * AI_getNumAIUnits(UNITAI_WORKER)) / 15);
	iValue += ((kCivic.getImprovementUpgradeRateModifier() * iCities) / 50);
	iValue += (kCivic.getMilitaryProductionModifier() * iCities * iWarmongerPercent) / (bWarPlan ? 300 : 500);
	iValue += (kCivic.getBaseFreeUnits() / 2);
	iValue += (kCivic.getBaseFreeMilitaryUnits() / 2);
	iValue += ((kCivic.getFreeUnitsPopulationPercent() * getTotalPopulation()) / 200);
	iValue += ((kCivic.getFreeMilitaryUnitsPopulationPercent() * getTotalPopulation()) / 300);
	iValue += -(kCivic.getGoldPerUnit() * getNumUnits());
	iValue += -(kCivic.getGoldPerMilitaryUnit() * getNumMilitaryUnits() * iWarmongerPercent) / 200;*/ // BtS
	// K-Mod, just a bunch of minor accuracy improvements to these approximations.
	if (kCivic.getWorkerSpeedModifier() != 0)
	{
		int iWorkers = 0;
		FOR_EACH_CITYAI(pLoopCity, *this)
			iWorkers += 2 * pLoopCity->AI_getWorkersNeeded();
		iWorkers -= AI_getNumAIUnits(UNITAI_WORKER);
		if (iWorkers > 0)
			iValue += kCivic.getWorkerSpeedModifier() * iWorkers / 15;
	}
	iValue += kCivic.getImprovementUpgradeRateModifier() * iCities / 50; // this is a tough one... I'll leave it alone for now.
	// value for kCivic.getMilitaryProductionModifier() has been moved
	if (kCivic.getBaseFreeUnits() || kCivic.getBaseFreeMilitaryUnits() ||
		kCivic.getFreeUnitsPopulationPercent() || kCivic.getFreeMilitaryUnitsPopulationPercent() ||
		kCivic.getGoldPerUnit() || kCivic.getGoldPerMilitaryUnit())
	{
		int iFreeUnits = 0;
		int iFreeMilitaryUnits = 0;
		int iUnits = getNumUnits();
		int iMilitaryUnits = getNumMilitaryUnits();
		int iPaidUnits = iUnits;
		int iPaidMilitaryUnits = iMilitaryUnits;
		int iMilitaryCost = 0;
		int iUnitCost = 0;
		int iExtraCost = 0; // unused
		calculateUnitCost(iFreeUnits, iFreeMilitaryUnits, iPaidUnits, iPaidMilitaryUnits, iUnitCost, iMilitaryCost, iExtraCost);

		int iTempValue = 0;

		// units costs. (note goldPerUnit and goldPerMilitaryUnit are in units of 1/100 gold)
		int iCostPerUnit = (getGoldPerUnit() + (iS > 0 ? kCivic.getGoldPerUnit() : 0)) * getUnitCostMultiplier() / 100;
		int iFreeUnitDelta = iS * (std::min(iUnits, iFreeUnits + iS*(kCivic.getBaseFreeUnits() + kCivic.getFreeUnitsPopulationPercent() * getTotalPopulation()/100)) - std::min(iUnits, iFreeUnits));
		FAssert(iFreeUnitDelta >= 0);
		iTempValue += iFreeUnitDelta * iCostPerUnit * iMaintenanceFactor / 10000;
		iTempValue -= (iPaidUnits-iFreeUnitDelta) * kCivic.getGoldPerUnit() * iMaintenanceFactor / 10000;

		// military
		iCostPerUnit = getGoldPerMilitaryUnit() + (iS > 0 ? kCivic.getGoldPerMilitaryUnit() : 0);
		iFreeUnitDelta = iS * (std::min(iMilitaryUnits, iFreeMilitaryUnits + iS*(kCivic.getBaseFreeMilitaryUnits() + kCivic.getFreeMilitaryUnitsPopulationPercent() * getTotalPopulation()/100)) - std::min(iMilitaryUnits, iFreeMilitaryUnits));
		FAssert(iFreeUnitDelta >= 0);
		iTempValue += iFreeUnitDelta * iCostPerUnit * iMaintenanceFactor / 10000;
		iTempValue -= (iPaidMilitaryUnits-iFreeUnitDelta) * kCivic.getGoldPerMilitaryUnit() * iMaintenanceFactor / 10000;

		// adjust based on future expectations
		if (iTempValue < 0)
		{
			iTempValue *= 100 + iWarmongerFactor / (bWarPlan ? 3 : 6);
			iTempValue /= 100;
		}
		iValue += iTempValue;
	}
	// K-Mod end

	//iValue += ((kCivic.isMilitaryFoodProduction()) ? 0 : 0);
	// bbai
	int iMaxConscript = GC.getGame().getMaxConscript(eCivic);
	if (iMaxConscript > 0 && pCapital != NULL)
	{
		UnitTypes eConscript = pCapital->getConscriptUnit();
		if (eConscript != NO_UNIT)
		{
			// Nationhood
			int iCombatValue = kGame.AI_combatValue(eConscript);
			if (iCombatValue > 33)
			{
				int iTempValue = iCities + ((bWarPlan) ? 30 : 10);

				/* iTempValue *= range(kTeam.AI_getEnemyPowerPercent(), 50, 300);
				iTempValue /= 100; */ // disabled by K-Mod

				iTempValue *= iCombatValue;
				iTempValue /= 75;

				int iWarSuccessRating = kTeam.AI_getWarSuccessRating();
				if (iWarSuccessRating < -25)
				{
					iTempValue *= 75 + range(-iWarSuccessRating, 25, 100);
					iTempValue /= 100;
				}
				// K-Mod. (What's with all the "losing means we need drafting" mentality in the BBAI code? It's... not the right way to look at it.)
				// (NOTE: "conscript_population_per_cost" is actually "production_per_conscript_population". The developers didn't know what "per" means.)
				int iConscriptPop = std::max(1, GC.getInfo(eConscript).getProductionCost() /
						GC.getDefineINT(CvGlobals::CONSCRIPT_POPULATION_PER_COST));
				int iProductionFactor = 100 * GC.getInfo(eConscript).getProductionCost();
				iProductionFactor /= iConscriptPop * GC.getDefineINT(CvGlobals::CONSCRIPT_POPULATION_PER_COST);
				iTempValue *= iProductionFactor;
				iTempValue /= 100;
				iTempValue *= std::min(iCities, iMaxConscript*3);
				iTempValue /= iMaxConscript*3;
				// reduce the value if unit spending is already high.
				int iUnitSpending = AI_unitCostPerMil();
				int iMaxSpending = AI_maxUnitCostPerMil() + 5 - iS*5; // increase max by 1% if we're already on this civic, just for a bit of inertia.
				if (iUnitSpending > iMaxSpending)
				{
					iTempValue = std::max(0, iTempValue * (2 * iMaxSpending - iUnitSpending)/std::max(1, iMaxSpending));
				}
				else if (iProductionFactor >= 140) // cf. 'bGoodValue' in CvCityAI::AI_doDraft
				{
					// advc.017: was 2*iMaxSpending
					iTempValue *= ::round(1.35*iMaxSpending);
					iTempValue /= std::max(1, iMaxSpending + iUnitSpending);
				}
				// todo. put in something to do with how much happiness we can afford to lose.. or something like that.
				// K-Mod end

				iValue += iTempValue;
			}
		}
	}
	// bbai end
	// K-Mod. evaluation of my new unhealthiness modifier
	// advc.001: Shouldn't call AI_getHealthWeight with iHealth=0.
	if(kCivic.getUnhealthyPopulationModifier() != 0)
	{
		iValue += (iCities * 6 * iS * AI_getHealthWeight(
				-iS*kCivic.getUnhealthyPopulationModifier(), 1, true)) / 100;
	}
	// c.f	iValue += (iCities * 6 * AI_getHealthWeight(kCivic.getExtraHealth(), 1)) / 100;

	// If the GW threshold has been reached, increase the value based on GW anger
	if (kGame.getGlobalWarmingIndex() > 0)
	{
		// estimate the happiness boost...
		// suppose pop pollution is 1/3 of total, and current relative contribution is around 100%
		// and anger percent scales like 2* relative contribution...
		// anger percent reduction will then be around (kCivic.getUnhealthyPopulationModifier()*2/3)%

		// Unfortunately, since adopting this civic will lower the very same anger percent that is giving the civic value...
		// the civic will be valued lower as soon as it is adopted. :(
		// Fixing this problem (and others like it) is a bit tricky. For now, I'll just try to fudge around it.
		int iGwAnger = getGwPercentAnger();
		if (isCivic(eCivic)) // Fudge factor
		{
			iGwAnger *= 100;
			iGwAnger /= 100 - 2*kCivic.getUnhealthyPopulationModifier()/3;
			// Note, this fudge factor is actually pretty good at estimating what the GwAnger would have been.
		}
		int const iHappy = iS*ROUND_DIVIDE(-kCivic.getUnhealthyPopulationModifier()*iGwAnger*2,300);
		// <advc.001> Shouldn't call AI_getHappinessWeight with iHappy=0
		int iCleanValue = 0;
		if(iHappy != 0) // </advc.001>
			iCleanValue = (iCities * 12 * iS * AI_getHappinessWeight(iHappy, 1, true)) / 100;
		// This isn't a big reduction; and it should be the only part of this evaluation.
		// Maybe I'll add more later; such as some flavour factors.

		iValue += iCleanValue;
	}
// K-Mod end
	if (bWarPlan)
	{
		iValue += ((kCivic.getExpInBorderModifier() * getNumMilitaryUnits()) / 200);
	}
	iValue += ((kCivic.isBuildingOnlyHealthy()) ? (iCities * 3) : 0);
	//iValue += -((kCivic.getWarWearinessModifier() * getNumCities()) / ((bWarPlan) ? 10 : 50));
	// <cdtw.4> Replacing the above
	if(kCivic.getWarWearinessModifier() != 0)
	{
		// comment Dave_uk:
		/*  lets take into account how bad war weariness actually is for us.
			this is actually double counting since we already account for how
			happiness will be affected below, however, if war weariness is
			really killing us we need to take action and switch */
		int iTempValue = -kCivic.getWarWearinessModifier() * getNumCities();
		iTempValue *= std::max(getWarWearinessPercentAnger(), 100);
		iTempValue /= 100;
		iTempValue /= ((bWarPlan) ? 10 : 50);
		iValue += iTempValue;
	} // </cdtw.4>
	//iValue += (kCivic.getFreeSpecialist() * iCities * 12);
	// K-Mod. A rough approximation is ok, but perhaps not quite /that/ rough.
	// Here's some code that I wrote for specialists in AI_buildingValue. (note. building value uses 4x commerce; but here we just use 1x commerce)
	if (kCivic.getFreeSpecialist() != 0)
	{
		int iSpecialistValue = 5 * 100; // rough base value
		// additional bonuses
		for (CommerceTypes i = (CommerceTypes)0; i < NUM_COMMERCE_TYPES; i = (CommerceTypes)(i+1))
		{
			int c = getSpecialistExtraCommerce(i) + kCivic.getSpecialistExtraCommerce(i);
			if (c)
				iSpecialistValue += c * AI_commerceWeight(i);
		}
		iSpecialistValue += 2*std::max(0, AI_averageGreatPeopleMultiplier()-100);
		iValue += iCities * iSpecialistValue / 100;
	}
	// K-Mod end

	/*
	iValue += (kCivic.getTradeRoutes() * (std::max(0, iConnectedForeignCities - iCities * 3) * 6 + (iCities * 2)));
	iValue += -((kCivic.isNoForeignTrade()) ? (iConnectedForeignCities * 3) : 0); */

	// K-Mod - take a few more things into account for trade routes.
	// NOTE: this calculation makes a bunch of assumptions about about the yield type and magnitude from trade routes.
	if (kCivic.getTradeRoutes() != 0 || kCivic.isNoForeignTrade())
	{
		PROFILE("civicValue: trade routes");
		// As a rough approximation, let each foreign trade route give base 3 commerce, and local trade routes give 1.
		// Our civ can have 1 connection to each foreign city.

		int iTempValue = 0;
		int iConnectedForeignCities = AI_countPotentialForeignTradeCities(true, AI_getFlavorValue(FLAVOR_GOLD) == 0);

		int iTotalTradeRoutes = iCities * 3; // I'll save the proper count for when the estimate of yield / route improves...
		/*FOR_EACH_CITY(pLoopCity, *this)
			iTotalTradeRoutes += pLoopCity->getTradeRoutes();
		if (iS < 0)
			iTotalTradeRoutes -= kCivic.getTradeRoutes() * iCities; */

		FAssert(iTotalTradeRoutes >= iCities);

		if (kCivic.isNoForeignTrade())
		{
			// We should attempt the block one-way trade which other civs might be getting from us.
			// count how many foreign trade cities we are not connected to...
			int iDisconnectedForeignTrade = AI_countPotentialForeignTradeCities(false, AI_getFlavorValue(FLAVOR_GOLD) == 0) - iConnectedForeignCities;
			FAssert(iDisconnectedForeignTrade >= 0);
			// and estimate the value of blocking foreign trade to these cities. (we don't get anything from this, but we're denying our potential rivals.)
			iTempValue += std::min(iDisconnectedForeignTrade, iCities); // just 1 point per city.

			// estimate the number of foreign cities which are immune to "no foreign trade".
			int iSafeOverseasTrade = 0;
			for (TeamTypes i = (TeamTypes)0; i < MAX_CIV_TEAMS; i = (TeamTypes)(i+1))
			{
				if (i == getTeam() || !kTeam.isFreeTrade(i))
					continue;
				if (kTeam.isVassal(i) || GET_TEAM(i).isVassal(getTeam()))
					iSafeOverseasTrade += GET_TEAM(i).getNumCities();
			}
			iSafeOverseasTrade = std::min(iSafeOverseasTrade, iConnectedForeignCities);

			iTempValue -= std::min(iConnectedForeignCities - iSafeOverseasTrade, iTotalTradeRoutes) * 3/2; // only reduce by 1.5 rather than 2, because it is good to deny our rivals the trade.
			iConnectedForeignCities = iSafeOverseasTrade;
		}

		// Unfortunately, it's not easy to tell whether or not we'll foreign trade when we switch to this civic.
		// for example, if we are considering a switch from mercantilism to free market, we'd currently have "noForeignTrade", but if we actually make the switch then we wouldn't.
		//  ... So for simplicity I'm going to just assume that there is only one civic with noForeignTrade, and therefore the value of iConnectedForeignCities is unchanged.
		//CvCity* pCapital = getCapitalCity(); // advc: Redundant
		if (pCapital)
		{
			// add an estimate of our own overseas cities (even though they are not really "foreign".
			iConnectedForeignCities += iCities - pCapital->getArea().getCitiesPerPlayer(getID());
		}

		iTempValue += kCivic.getTradeRoutes() * std::max(0, iConnectedForeignCities - iTotalTradeRoutes) * 2 + iCities * 1;

		// Trade routes increase in value as cities grow, and build trade multipliers
		iTempValue *= 2*(getCurrentEra()+1) + GC.getNumEraInfos();
		iTempValue /= GC.getNumEraInfos();
		// commerce multipliers
		iTempValue *= AI_averageYieldMultiplier(YIELD_COMMERCE);
		iTempValue /= 100;
		iValue += iTempValue;
	}
	// K-Mod end

	// Corporations (K-Mod edition!)
	if (kCivic.isNoCorporations() || kCivic.isNoForeignCorporations() || kCivic.getCorporationMaintenanceModifier() != 0)
	{
		PROFILE("civicValue: corporation effects");

		int iPotentialCorpValue = 0; // an estimate of the value lost when "isNoCorporations" blocks us from founding a good corp.

		for (CorporationTypes eCorp = (CorporationTypes)0; eCorp < GC.getNumCorporationInfos(); eCorp=(CorporationTypes)(eCorp+1))
		{
			const CvCorporationInfo& kCorpInfo = GC.getInfo(eCorp);
			int iSpeculation = 0;
			if (!kGame.isCorporationFounded(eCorp))
			{
				// if this civic is going to block us from founding any new corporations
				// then we should try to estimate the cost of blocking such an opportunity.
				// (but we don't count corporations that are simply founded by a technology)
				if (AI_getFlavorValue(FLAVOR_GROWTH) + AI_getFlavorValue(FLAVOR_SCIENCE) > 0 && kCivic.isNoCorporations() && kCorpInfo.getTechPrereq() == NO_TECH)
				{
					// first, if this corp competes with a corp that we already have, then assume we don't want it.
					bool bConflict = false;
					for (CorporationTypes i = (CorporationTypes)0; i < GC.getNumCorporationInfos(); i=(CorporationTypes)(i+1))
					{
						if (kGame.isCompetingCorporation(eCorp, i) && kGame.isCorporationFounded(i) && countCorporations(i) > 0)
						{
							bConflict = true;
							break;
						}
					}

					if (!bConflict)
					{
						// Find the building that founds the corp, and check how many players have the prereq techs.
						// Note: I'm assuming that the conditions for this building are reasonable.
						// eg. if a great person is required, then it should be a great person that we are actually able to get!
						for (BuildingTypes i = (BuildingTypes)0; i < GC.getNumBuildingInfos(); i=(BuildingTypes)(i+1))
						{
							const CvBuildingInfo& kLoopBuilding = GC.getInfo(i);
							if (kLoopBuilding.getFoundsCorporation() == eCorp && kLoopBuilding.getProductionCost() < 0) // don't count buildings that can be constructed normally
							{
								bool bHasPrereq = true;
								if (canDoCivics(eCivic))
								{
									// Only check the tech prereqs for the corp if we already have the prereqs for this civic.
									// (This condition will help us research towards the corp tech rather than researching towards the civic that will block the corp.)
									bHasPrereq = (kTeam.isHasTech(kLoopBuilding.getPrereqAndTech()) ||
											canResearch(kLoopBuilding.getPrereqAndTech()));
									for (int iI = 0; bHasPrereq && iI < GC.getNUM_BUILDING_AND_TECH_PREREQS(); iI++)
									{
										bHasPrereq = (kTeam.isHasTech(kLoopBuilding.getPrereqAndTechs(iI)) ||
											canResearch(kLoopBuilding.getPrereqAndTechs(iI)));
									}
								}

								if (bHasPrereq)
								{
									iSpeculation = 2;
									// +1 for each other player we know with all the prereqs
									for (PlayerTypes j = (PlayerTypes)0; j < MAX_CIV_PLAYERS; j=(PlayerTypes)(j+1))
									{
										const CvTeam& kLoopTeam = GET_TEAM(GET_PLAYER(j).getTeam());
										if (kLoopTeam.getID() != getTeam() && kTeam.isHasMet(kLoopTeam.getID()))
										{
											bHasPrereq = kLoopTeam.isHasTech((TechTypes)kLoopBuilding.getPrereqAndTech());

											for (int iI = 0; bHasPrereq && iI < GC.getNUM_BUILDING_AND_TECH_PREREQS(); iI++)
											{
												bHasPrereq = kLoopTeam.isHasTech((TechTypes)kLoopBuilding.getPrereqAndTechs(iI));
											}
											if (bHasPrereq)
												iSpeculation++;
										}
									}
								}
								// assume there is only one building that founds the corporation
								break;
							}
						}
					}
				}

				if (iSpeculation == 0)
					continue; // no value from this corp, speculative or otherwise
			}

			bool bPlayerHQ = false;
			bool bTeamHQ = false;
			if (hasHeadquarters(eCorp) || iSpeculation > 0)
			{
				bPlayerHQ = true;
				bTeamHQ = true;
			}
			else if (kTeam.hasHeadquarters(eCorp))
			{
				bTeamHQ = true;
			}

			int iCorpValue = 0; // total civic value change due to the effect of this corporation.

			int iBonuses = 0;
			int iCorpCities = countCorporations(eCorp);
			int iMaintenance = 0;
			// If the HQ is ours, assume we will spread the corp. If it is not our, assume we don't care.
			// (Note: this doesn't take into account the posibility of competing corps. Sorry.)
			if (bTeamHQ)
			{
				iCorpCities = ((bPlayerHQ ?1 :2)*iCorpCities + iCities)/(bPlayerHQ ?2 :3);
			}

			for (int i = 0; i < GC.getNUM_CORPORATION_PREREQ_BONUSES(); ++i)
			{
				BonusTypes eBonus = (BonusTypes)kCorpInfo.getPrereqBonus(i);
				if (NO_BONUS != eBonus)
				{
					iBonuses +=	kTeam.isBonusRevealed(eBonus) ? AI_countOwnedBonuses(eBonus) : 1; // expect that we'll get at least one of each unrevealed bonus.
					// maybe use getNumAvailableBonuses ?
				}
			}
			iBonuses += bPlayerHQ ? 1 : 0;

			for (CommerceTypes i = (CommerceTypes)0; i < NUM_COMMERCE_TYPES; i = (CommerceTypes)(i+1))
			{
				int iTempValue = 0;

				// loss of the headquarter bonus from our cities.
				if (bTeamHQ &&
					((kCivic.isNoForeignCorporations() && !bPlayerHQ) ||
					kCivic.isNoCorporations()))
				{
					CvCity const* pHqCity = (iSpeculation == 0 ? kGame.getHeadquarters(eCorp) : pCapital);
					if (pHqCity)
						iTempValue -= pHqCity->getCommerceRateModifier(i) * kCorpInfo.getHeadquarterCommerce(i) * iCorpCities / 100;
				}

				// loss of corp commerce bonuses
				if (kCivic.isNoCorporations() || (kCivic.isNoForeignCorporations() && !bPlayerHQ))
				{
					iTempValue -= iCorpCities * ((AI_averageCommerceMultiplier(i) * kCorpInfo.getCommerceProduced(i))/100 * iBonuses * GC.getInfo(GC.getMap().getWorldSize()).getCorporationMaintenancePercent()) / 10000;
				}

				if (iTempValue != 0)
				{
					iTempValue *= AI_commerceWeight(i);
					iTempValue /= 100;

					iCorpValue += iTempValue;
				}

				iMaintenance += kCorpInfo.getHeadquarterCommerce(i) * iCorpCities;
			}

			if (kCivic.isNoCorporations() || (kCivic.isNoForeignCorporations() && !bPlayerHQ))
			{
				// loss of corp yield bonuses
				for (int iI = 0; iI < NUM_YIELD_TYPES; ++iI)
				{
					int iTempValue = -(iCorpCities * kCorpInfo.getYieldProduced((YieldTypes)iI) * iBonuses);
					iTempValue *= AI_averageYieldMultiplier((YieldTypes)iI);
					iTempValue /= 100;
					iTempValue *= GC.getInfo(GC.getMap().getWorldSize()).getCorporationMaintenancePercent();
					iTempValue /= 10000; // getYieldProduced is x100.

					iTempValue *= AI_yieldWeight((YieldTypes)iI);
					iTempValue /= 100;

					iCorpValue += iTempValue;
				}

				// loss of corp resource
				if (kCorpInfo.getBonusProduced() != NO_BONUS)
				{
					iCorpValue -= AI_bonusVal((BonusTypes)kCorpInfo.getBonusProduced(), 1, false) / 4;
				}
			}

			// loss of maintenance cost (money saved)
			int iTempValue = kCorpInfo.getMaintenance() * iBonuses * iCorpCities;
			iTempValue *= GC.getInfo(GC.getMap().getWorldSize()).getCorporationMaintenancePercent();
			iTempValue /= 10000;
			iTempValue += iMaintenance;
			// population, and maintenance modifiers... lets just approximate them like this:
			iTempValue *= 2;
			iTempValue /= 3;

			// (note, corp maintenance is not amplified by inflation, and so we don't use iMaintenanceFactor here.)
			iTempValue *= AI_commerceWeight(COMMERCE_GOLD);
			iTempValue /= 100;

			if (kCivic.isNoCorporations() || (kCivic.isNoForeignCorporations() && !bPlayerHQ))
			{
				iCorpValue += iTempValue;
			}
			else
			{
				iCorpValue += (-kCivic.getCorporationMaintenanceModifier() * iTempValue)/100;
			}

			FAssert(iSpeculation == 0 || (!kGame.isCorporationFounded(eCorp) && kCivic.isNoCorporations()));
			iCorpValue;
			if (iSpeculation == 0)
				iValue += iCorpValue;
			else
			{
				// Note: when speculating about disabling unfounded corps, we only count the losses. (If the corp is negative value, then we wouldn't found it!)
				iCorpValue *= 2;
				iCorpValue /= iSpeculation + 1; // iSpeculation == 2 + # of other civs with the prereqs.
				if (iCorpValue < iPotentialCorpValue)
					iPotentialCorpValue = iCorpValue;
			}
		}
		FAssert(iPotentialCorpValue <= 0);
		iValue += iPotentialCorpValue;
	}
	// K-Mod end


	if (kCivic.getCivicPercentAnger() != 0)
	{
		// K-Mod version. (the original bts code was ridiculous - it is now deleted)
		// value for negation of unhappiness. Again, let me point out how _stupid_ it is that "percent_anger_divisor" is 1000, not 100.
		iValue += (iCities * 12 * iS * AI_getHappinessWeight(iS*getCivicPercentAnger(eCivic, true)*100/GC.getPERCENT_ANGER_DIVISOR(), 2, true)) / 100;
		// value for putting pressure on other civs. (this should probably take into account the civics of other civs)
		iValue += kCivic.getCivicPercentAnger() * (iCities * iCities - iCities) / (5*kGame.getNumCities()); // the 5* on the end is because "percent" is really per mil.
	}

	if (kCivic.getExtraHealth() != 0)
		iValue += (iCities * 6 * iS * AI_getHealthWeight(iS*kCivic.getExtraHealth(), 1)) / 100;

	if (kCivic.getExtraHappiness() != 0) // New K-Mod effect
		iValue += (iCities * 10 * iS * AI_getHappinessWeight(iS *
				/*  (zero extra citizens... as a kind of kludge to get the
					value closer to where I want it) */
				kCivic.getExtraHappiness(), 0)) / 100;

	if (kCivic.getHappyPerMilitaryUnit() != 0)
		iValue += (iCities * 9 * iS * AI_getHappinessWeight(iS*kCivic.getHappyPerMilitaryUnit() * 3, 1))
				// <advc.912c>
				/ 200; // divisor was 100
	if (kCivic.getLuxuryModifier() > 0 && pCapital != NULL) {
		int iLuxMod = kCivic.getLuxuryModifier();
		int iHappyChangeCapital = (pCapital->getBonusGoodHappiness(true) * iLuxMod) / 100;
		iValue += (iCities * 9 * iS * AI_getHappinessWeight(iS *
				iHappyChangeCapital, 1)) / 100;
	} // </advc.912c>
	if (kCivic.getLargestCityHappiness() != 0)
		iValue += (16 * std::min(iCities, GC.getInfo(GC.getMap().getWorldSize()).getTargetNumCities()) * iS * AI_getHappinessWeight(iS*kCivic.getLargestCityHappiness(), 1)) / 100; // Higher weight, because it affects the best cities (was 14)

	if (kCivic.getWarWearinessModifier() != 0) // K-Mod. (original code deleted)
		iValue += (12 * iCities * iS * AI_getHappinessWeight(iS*ROUND_DIVIDE(getWarWearinessPercentAnger() * -getWarWearinessModifier(), GC.getPERCENT_ANGER_DIVISOR()), 1, true)) / 100;

	//iValue += (kCivic.getNonStateReligionHappiness() * (iTotalReligonCount - iHighestReligionCount) * 5);
	// K-Mod
	if (kCivic.getNonStateReligionHappiness() != 0)
		iValue += 12 * iCities * iS * AI_getHappinessWeight(iS * kCivic.getNonStateReligionHappiness() * iTotalReligonCount / std::max(1, iCities), 0) / 100;
	// K-Mod end

	// K-Mod. Experience and production modifiers
	{
		// Roughly speaking these are the approximations used in this section:
		// each population produces 1 hammer.
		// percentage of hammers spent on units is BuildUnitProb + 40 if at war
		// experience points are worth a production boost of 8% each, multiplied by the warmonger factor, and componded with actual production multipliers
		int iProductionShareUnits = GC.getInfo(getPersonalityType()).getBuildUnitProb();
		if (bWarPlan)
			iProductionShareUnits = (100 + iProductionShareUnits)/2;
		else if (AI_isDoStrategy(AI_STRATEGY_ECONOMY_FOCUS))
			iProductionShareUnits /= 2;

		int iProductionShareBuildings = 100 - iProductionShareUnits;

		int iTempValue = getTotalPopulation() * kCivic.getMilitaryProductionModifier() +
				iBestReligionPopulation * kCivic.getStateReligionUnitProductionModifier();

		int iExperience = getTotalPopulation() * kCivic.getFreeExperience() + iBestReligionPopulation * kCivic.getStateReligionFreeExperience();
		if (iExperience)
		{
			iExperience *= 8 * std::max(iWarmongerFactor, (bWarPlan ? 100 : 0));
			iExperience /= 100;
			iExperience *= AI_averageYieldMultiplier(YIELD_PRODUCTION);
			iExperience /= 100;

			iTempValue += iExperience;
		}

		iTempValue *= iProductionShareUnits;
		iTempValue /= 100;

		iTempValue += iBestReligionPopulation * kCivic.getStateReligionBuildingProductionModifier() * iProductionShareBuildings / 100;
		iTempValue *= AI_yieldWeight(YIELD_PRODUCTION);
		iTempValue /= 100;
		iValue += iTempValue / 100;

		/* old modifiers, (just for reference)
		iValue += (kCivic.getMilitaryProductionModifier() * iCities * iWarmongerFactor) / (bWarPlan ? 300 : 500);
		if (kCivic.getFreeExperience() > 0) {
			// Free experience increases value of hammers spent on units, population is an okay measure of base hammer production
			int iTempValue = (kCivic.getFreeExperience() * getTotalPopulation() * (bWarPlan ? 30 : 12))/100;
			iTempValue *= AI_averageYieldMultiplier(YIELD_PRODUCTION);
			iTempValue /= 100;
			iTempValue *= iWarmongerFactor;
			iTempValue /= 100;
			iValue += iTempValue;
		}
		iValue += ((kCivic.getStateReligionUnitProductionModifier() * iBestReligionCities) / 4);
		iValue += ((kCivic.getStateReligionBuildingProductionModifier() * iBestReligionCities) / 3);
		iValue += (kCivic.getStateReligionFreeExperience() * iBestReligionCities * ((bWarPlan) ? 6 : 2)); */
	}
	// K-Mod end

	if (kCivic.isStateReligion())
	{
		if (iBestReligionCities > 0)
		{
			/*iValue += iHighestReligionCount;
			iValue += ((kCivic.isNoNonStateReligionSpread()) ? ((iCities - iHighestReligionCount) * 2) : 0);
			iValue += (kCivic.getStateReligionHappiness() * iHighestReligionCount * 4);
			iValue += ((kCivic.getStateReligionGreatPeopleRateModifier() * iHighestReligionCount) / 20);
			iValue += (kCivic.getStateReligionGreatPeopleRateModifier() / 4);
			iValue += ((kCivic.getStateReligionUnitProductionModifier() * iHighestReligionCount) / 4);
			iValue += ((kCivic.getStateReligionBuildingProductionModifier() * iHighestReligionCount) / 3);
			iValue += (kCivic.getStateReligionFreeExperience() * iHighestReligionCount * ((bWarPlan) ? 6 : 2));*/ // BtS

			// K-Mod
			FAssert(eBestReligion != NO_RELIGION);
			iValue += (2 * iBestReligionPopulation - getTotalPopulation()) / 6;

			if (kCivic.isNoNonStateReligionSpread())
			{	// <advc.115b>
				if(isAVassal())
				{
					CvTeamAI const& kMaster = GET_TEAM(getMasterTeam());
					if(!kMaster.isHuman() && kMaster.AI_isAnyCloseToReligiousVictory())
						return -1000;
				} // </advc.115b>
				if (AI_atVictoryStage(AI_VICTORY_CULTURE2))
				{
					iValue -= 3 * std::max(0, iCities + iBestReligionCities - iTotalReligonCount);
				}
				else if (eBestReligion == GC.getInfo(getLeaderType()).getFavoriteReligion()
					&& /* advc.115b: Used to be a logical OR. Theocracy as the
						  standard civic from Medieval onwards hurts
						  religious strategies unduly. */
					(hasHolyCity(eBestReligion) && countHolyCities() == 1))
				{
					iValue += iCities * 2; // protection vs espionage? depriving foreign civs of religion revenue?
				}
			}

			// getStateReligionFreeExperience, getStateReligionBuildingProductionModifier, getStateReligionUnitProductionModifier; all moved elsewhere.

			if (kCivic.getStateReligionHappiness() != 0)
			{
				iValue += 10 * iCities * iS * AI_getHappinessWeight(iS * kCivic.getStateReligionHappiness(), 1) * iBestReligionPopulation / std::max(1, 100 * getTotalPopulation());
			}
			if (kCivic.getStateReligionGreatPeopleRateModifier() != 0)
			{
				// This is not going to be very good. I'm sorry about that. I can't think of a neat way to make it better.
				// (The best way would involve counting GGP, weighted by the AI value for great person type,
				// multiplied by the probability that the city will actually be able to produce a great person, and so on.
				// But I'm worried that would be too slow / complex.)

				std::vector<int> base_rates;
				FOR_EACH_CITY(pLoopCity, *this)
				{
					if (pLoopCity->isHasReligion(eBestReligion))
						base_rates.push_back(pLoopCity->getBaseGreatPeopleRate());
				}
				int iGpCities = std::min((int)base_rates.size(), 3);
				std::partial_sort(base_rates.begin(), base_rates.begin()+iGpCities, base_rates.end());

				int iTempValue = 0;
				for (int i = 0; i < iGpCities; i++)
					iTempValue += base_rates[i];

				iValue += 2 * kCivic.getStateReligionGreatPeopleRateModifier() * iTempValue / 100;
			}

			// apostolic palace
			for (VoteSourceTypes i = (VoteSourceTypes)0; i < GC.getNumVoteSourceInfos(); i = (VoteSourceTypes)(i+1))
			{
				if (kGame.isDiploVote(i))
				{
					if (kGame.getVoteSourceReligion(i) == eBestReligion && isLoyalMember(i))
					{
						int iTempValue = getTotalPopulation() * iBestReligionCities / std::max(1, iCities);

						if (getTeam() == kGame.getSecretaryGeneral(i))
							iTempValue *= 2;
						if (kTeam.isForceTeamVoteEligible(i))
							iTempValue /= 2;

						iValue += iTempValue;
					}
				}
			}

			// Value civic based on wonders granting state religion boosts
			for (CommerceTypes i = (CommerceTypes)0; i < NUM_COMMERCE_TYPES; i = (CommerceTypes)(i+1))
			{
				int iTempValue = iBestReligionCities * getStateReligionBuildingCommerce(i);
				if (iTempValue)
				{
					iTempValue *= AI_averageCommerceMultiplier(i);
					iTempValue /= 100;

					iTempValue *= AI_commerceWeight(i);
					iTempValue /= 100;

					iValue += iTempValue;
				}
			}
			// K-Mod end
		}
	}

	for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		int iTempValue = 0;
		//iTempValue += ((kCivic.getYieldModifier(iI) * iCities) / 2);
		// K-Mod (Still bogus, but I'd rather assume 25 yield/turn average than 50.)
		iTempValue += kCivic.getYieldModifier(iI) * iCities / 4;

		if (pCapital)
		{	// Bureaucracy
			// K-Mod
			int iTemp = (kCivic.getCapitalYieldModifier(iI) * pCapital->getBaseYieldRate((YieldTypes)iI));
			if (iTemp != 0)
			{
				switch (iI)
				{
				case YIELD_PRODUCTION:
					// For production, we inflate the value a little to account for the fact that it may help us win wonder races.
					iTemp /= 80;
					break;
				case YIELD_COMMERCE:
					// For commerce, the multiplier is compounded by the multipliers on individual commerce types.
					iTemp *= pCapital->AI_yieldMultiplier((YieldTypes)iI);
					iTemp /= 100 * std::max(1, pCapital->getBaseYieldRateModifier((YieldTypes)iI));
					break;
				default:
					iTemp /= 100;
					break;
				}
				iTempValue += iTemp;
			} // K-Mod end
		}
		iTempValue += ((kCivic.getTradeYieldModifier(iI) * iCities) / 11);
		/*  (K-Mod note: that denominator is bogus, but since no civics
			currently have this modifier anyway, I'm just going to leave it.) */

		for (int iJ = 0; iJ < GC.getNumImprovementInfos(); iJ++)
		{
			if(kCivic.getImprovementYieldChanges(iJ, iI) == 0)
				continue; // advc.opt
			iTempValue += (AI_averageYieldMultiplier((YieldTypes)iI) *
					(kCivic.getImprovementYieldChanges(iJ, iI) *
					(getImprovementCount((ImprovementTypes)iJ) + iCities/2))) /
					100;
		}/*if (iI == YIELD_FOOD)
			iTempValue *= 3;
		else if (iI == YIELD_PRODUCTION)
			iTempValue *= ((AI_avoidScience()) ? 6 : 2);
		else if (iI == YIELD_COMMERCE) {
			iTempValue *= ((AI_avoidScience()) ? 2 : 4);
			iTempValue /= 3;
		}*/ // BtS
		// K-Mod
		iTempValue *= AI_yieldWeight((YieldTypes)iI);
		iTempValue /= 100;
		// K-mod end
		iValue += iTempValue;
	}

	// K-Mod: count bonus specialists,
	// so that we can more accurately gauge the representation bonus
	int iTotalBonusSpecialists = -1;
	int iTotalCurrentSpecialists = -1;

	// only take the time to count them if the civic has a bonus for specialists
	bool bSpecialistCommerce = false;
	for (int iI = 0; !bSpecialistCommerce && iI < NUM_COMMERCE_TYPES; iI++)
	{
		bSpecialistCommerce = kCivic.getSpecialistExtraCommerce(iI) != 0;
	}

	if (bSpecialistCommerce)
	{
		iTotalBonusSpecialists = iTotalCurrentSpecialists = 0;

		FOR_EACH_CITY(pLoopCity, *this)
		{
			iTotalBonusSpecialists += pLoopCity->getNumGreatPeople();
			iTotalBonusSpecialists += pLoopCity->totalFreeSpecialists();

			iTotalCurrentSpecialists += pLoopCity->getNumGreatPeople();
			iTotalCurrentSpecialists += pLoopCity->getSpecialistPopulation();
		}
	}
	// K-Mod end

	for (int iI = 0; iI < NUM_COMMERCE_TYPES; iI++)
	{
		int iTempValue = 0;

		// K-Mod
		iTempValue += kCivic.getCommerceModifier(iI) * 100*getCommerceRate((CommerceTypes)iI) / AI_averageCommerceMultiplier((CommerceTypes)iI);
		if (pCapital != NULL)
		{
			iTempValue += kCivic.getCapitalCommerceModifier(iI) * pCapital->getBaseCommerceRate((CommerceTypes)iI);
		}

		// Representation
		//iTempValue += ((kCivic.getSpecialistExtraCommerce(iI) * getTotalPopulation()) / 15);
		// K-Mod
		if (bSpecialistCommerce)
		{
			iTempValue += AI_averageCommerceMultiplier((CommerceTypes)iI)*
					(kCivic.getSpecialistExtraCommerce(iI) *
					std::max((getTotalPopulation()+10*iTotalBonusSpecialists) / 10,
					iTotalCurrentSpecialists));
		} // K-Mod end

		iTempValue /= 100; // (for the 3 things above)

		if (iTempValue)
		{
			iTempValue *= AI_commerceWeight((CommerceTypes)iI);
			iTempValue /= 100;

			iValue += iTempValue;
		}
	}
	if (kCivic.isAnyBuildingHappinessChanges())
	{
		CvCivilization const& kCiv = getCivilization(); // advc.003w (also fixes a bug; NO_BUILDING check was missing)
		for (int i = 0; i < kCiv.getNumBuildings(); i++)
		{
			BuildingTypes eBuilding = kCiv.buildingAt(i);
			BuildingClassTypes eBuildingClass = kCiv.buildingClassAt(i);
			int iTempValue = kCivic.getBuildingHappinessChanges(eBuildingClass);
			/*if (iTempValue != 0) {
				// Nationalism
				if (!isNationalWonderClass((BuildingClassTypes)iI))
					iValue += (iTempValue * iCities)/2;
				iValue += (iTempValue * getBuildingClassCountPlusMaking((BuildingClassTypes)iI) * 2);
			}*/ // BtS
			// K-Mod
			if (iTempValue == 0)
				continue;

			int iExpectedBuildings = 0;
			if (canConstruct(eBuilding))
			{
				iExpectedBuildings = (iCities + 2*getBuildingClassCountPlusMaking(eBuildingClass))/3;
			}
			iValue += (10 * iExpectedBuildings * iS * AI_getHappinessWeight(iS * iTempValue, 1))/100;
		}
	}
	for (int iI = 0; iI < GC.getNumFeatureInfos(); iI++)
	{
		int iHappiness = kCivic.getFeatureHappinessChanges(iI);
		if (iHappiness != 0)
		{
			iValue += (iHappiness * countCityFeatures((FeatureTypes)iI) * 5);
		}
	}

	for (HurryTypes i = (HurryTypes)0; i < GC.getNumHurryInfos(); i=(HurryTypes)(i+1))
	{
		if (kCivic.isHurry(i))
		{
			/*int iTempValue = 0;
			if (GC.getInfo((HurryTypes)iI).getGoldPerProduction() > 0)
				iTempValue += ((((AI_avoidScience()) ? 50 : 25) * iCities) / GC.getInfo((HurryTypes)iI).getGoldPerProduction());
			iTempValue += (GC.getInfo((HurryTypes)iI).getProductionPerPopulation() * iCities * (bWarPlan ? 2 : 1)) / 5;
			iValue += iTempValue;*/ // BtS

			// K-Mod. I'm not attempting to made an accurate estimate of the value here - I just want to make it a little bit more nuanced than it was.
			int iTempValue = 0;
			const CvHurryInfo& kHurryInfo = GC.getInfo(i);

			if (kHurryInfo.getGoldPerProduction() > 0)
			{
				iTempValue = AI_averageCommerceMultiplier(COMMERCE_GOLD) *
						(AI_avoidScience() ? 2000 : 1000) * iCities / kHurryInfo.getGoldPerProduction();
				iTempValue /= std::max(1, (getHurryModifier() + 100) * AI_commerceWeight(COMMERCE_GOLD));
			}

			if (kHurryInfo.getProductionPerPopulation() > 0)
			{	// <advc.121b>
				int iMultiplier = (bWarPlan ? 4 : 3);
				if(pCapital != NULL &&
					pCapital->getArea().getAreaAIType(getTeam()) == AREAAI_DEFENSIVE &&
					AI_isFocusWar())
				{
					iMultiplier++;
				} // </advc.121>
				/*  if we had easy access to averages for getMaxFoodKeptPercent and getHurryAngerModifier,
					then I'd use them. - but I don't want to calculate them here. */
				iTempValue += //(bWarPlan ? 8 : 5) *
						(iMultiplier * // advc.121b: Replacing the above 8 : 5
						iCities * kGame.getProductionPerPopulation(i)) /
						std::max(1, getGrowthThreshold(getAveragePopulation()));
			}

			if (iTempValue > 0)
			{
				if (kHurryInfo.getProductionPerPopulation() && kHurryInfo.getGoldPerProduction())
					iTempValue /= 2;

				iValue += iTempValue;
			}
			// K-Mod end
		}
	}

	/* for (int iI = 0; iI < GC.getNumSpecialBuildingInfos(); iI++) {
		if (kCivic.isSpecialBuildingNotRequired(iI))
			iValue += ((iCities / 2) + 1); // XXX
	} */ // Disabled by K-Mod. This evaluation isn't accurate enough to be useful - but it does sometimes cause civs to switch to organized religion when they don't have a religion...

	for (int iI = 0; iI < GC.getNumSpecialistInfos(); iI++)
	{
		if (!kCivic.isSpecialistValid(iI))
			continue; // advc
		// K-Mod todo: the current code sucks. Fix it.
		int iTempValue = iCities * (AI_atVictoryStage(AI_VICTORY_CULTURE3) ? 10 : 1) + 6;
		iValue += iTempValue / 3; // advc.131: Was /2. (And yes, it's terrible.)
	}

	// K-Mod. When aiming for a diplomatic victory, consider the favourite civics of our friends!
	if (AI_atVictoryStage(AI_VICTORY_DIPLOMACY3))
	{
		for (PlayerTypes i = (PlayerTypes)0; i < MAX_CIV_PLAYERS; i=(PlayerTypes)(i+1))
		{
			const CvPlayerAI& kLoopPlayer = GET_PLAYER(i);

			if (!kLoopPlayer.isAlive() || kLoopPlayer.getTeam() == getTeam() || !kTeam.isHasMet(kLoopPlayer.getTeam()))
				continue;

			if (kLoopPlayer.isHuman())
				continue; // human players don't care about favourite civics. The AI should understand this.

			AttitudeTypes eAttitude = AI_getAttitude(i, false);
			if (eAttitude >= ATTITUDE_PLEASED)
			{
				const CvLeaderHeadInfo& kPersonality = GC.getInfo(kLoopPlayer.getPersonalityType());
				if (kPersonality.getFavoriteCivic() == eCivic && kLoopPlayer.isCivic(eCivic))
				{
					// (better to use getVotes; but that's more complex.)
					//iValue += kLoopPlayer.getTotalPopulation() * (2 + kPersonality.getFavoriteCivicAttitudeChangeLimit()) / 20;
					iValue += kLoopPlayer.getTotalPopulation() / 5; // lets keep it simple
				}
			}
		}
	} // K-Mod end

	if (GC.getInfo(getPersonalityType()).getFavoriteCivic() == eCivic &&
		iValue > 0) // advc.131: Just to make sure
	{
		if (!kCivic.isStateReligion() || iBestReligionCities > 0)
		{
			/*iValue *= 5;
			iValue /= 4;
			iValue += 20;*/
			/*	advc.131> The +20 might encourage Monarchy too much (AI_techValue).
				Also, early fav civics shouldn't need that much encouragement as
				they have few alternatives. */
			iValue *= 135;
			iValue /= 100; // </advc.131>
			iValue += 6 * iCities;
		}
	}

	/* if (AI_atVictoryStage(AI_VICTORY_CULTURE2) && (GC.getInfo(eCivic).isNoNonStateReligionSpread()))
		iValue /= 10;*/ // what the lol...

	return iValue;
}

ReligionTypes CvPlayerAI::AI_bestReligion() const
{
	int iBestValue = 0;
	ReligionTypes eBestReligion = NO_RELIGION;
	ReligionTypes eFavorite = (ReligionTypes)GC.getInfo(getLeaderType()).getFavoriteReligion();
	for (int iI = 0; iI < GC.getNumReligionInfos(); iI++)
	{
		if (canDoReligion((ReligionTypes)iI))
		{
			int iValue = AI_religionValue((ReligionTypes)iI);

			/*if (getStateReligion() == ((ReligionTypes)iI)) {
				iValue *= 4;
				iValue /= 3;
			}*/ // BtS
			// K-Mod
			if (iI == getStateReligion() && getReligionAnarchyLength() > 0)
			{
				if (AI_isFirstTech(getCurrentResearch()) ||
						//GET_TEAM(getTeam()).getAnyWarPlanCount(true))
						AI_isFocusWar()) // advc.105
					iValue = iValue*3/2;
				else iValue = iValue*4/3;
			}
			// K-Mod end

			if (eFavorite == iI)
			{
				iValue *= 5;
				iValue /= 4;
			}

			if (iValue > iBestValue)
			{
				iBestValue = iValue;
				eBestReligion = ((ReligionTypes)iI);
			}
		}
	}

	if ((NO_RELIGION == eBestReligion) || AI_isDoStrategy(AI_STRATEGY_MISSIONARY))
	{
		return eBestReligion;
	}

	/* int iBestCount = getHasReligionCount(eBestReligion);
	int iSpreadPercent = (iBestCount * 100) / std::max(1, getNumCities());
	int iPurityPercent = (iBestCount * 100) / std::max(1, countTotalHasReligion());
	if (iPurityPercent < 49) {
		if (iSpreadPercent > ((eBestReligion == eFavorite) ? 65 : 75)) {
			if (iPurityPercent > ((eBestReligion == eFavorite) ? 25 : 32))
				return eBestReligion;
		}
		return NO_RELIGION;
	} */ // disabled by K-Mod
	// K-Mod. Don't instantly convert to the first religion avaiable, unless it is your own religion.
	int iSpread = getHasReligionCount(eBestReligion) * 100 / std::min(GC.getInfo(GC.getMap().getWorldSize()).getTargetNumCities()*3/2+1, getNumCities()+1);
	if (getStateReligion() == NO_RELIGION && iSpread < 29 - AI_getFlavorValue(FLAVOR_RELIGION)
		&& (GC.getGame().getHolyCity(eBestReligion) == NULL || GC.getGame().getHolyCity(eBestReligion)->getTeam() != getTeam()))
	{
		return NO_RELIGION;
	}
	// K-Mod end

	return eBestReligion;
}

// This function has been completely rewritten for K-Mod
int CvPlayerAI::AI_religionValue(ReligionTypes eReligion) const
{
	PROFILE_FUNC();

	int iValue = 0;

	if (getHasReligionCount(eReligion) == 0)
		return 0;

	int iReligionFlavor = AI_getFlavorValue(FLAVOR_RELIGION);

	FOR_EACH_CITY(pLoopCity, *this)
	{
		iValue += pLoopCity->getReligionGrip(eReligion) * pLoopCity->getPopulation();
		// note. this takes into account current state religion and number of religious buildings.
	}
	iValue *= getHasReligionCount(eReligion);
	iValue /= getNumCities();

	// holy city modifier
	CvCity* pHolyCity = GC.getGame().getHolyCity(eReligion);
	if (pHolyCity != NULL && pHolyCity->getTeam() == getTeam())
	{
		iValue *= 110 + 5 * iReligionFlavor + (10+iReligionFlavor)*GC.getGame().calculateReligionPercent(eReligion)/10;
		iValue /= 100;
	}

	// diplomatic modifier
	int iTotalCivs = 0; // x2
	int iLikedReligionCivs = 0; // x2 for friendly
	for (int i = 0; i < MAX_CIV_PLAYERS; i++)
	{
		const CvPlayer& kLoopPlayer = GET_PLAYER((PlayerTypes)i);
		if (i != getID() && kLoopPlayer.isAlive() && GET_TEAM(getTeam()).isHasMet(kLoopPlayer.getTeam()) && !kLoopPlayer.isMinorCiv())
		{
			iTotalCivs += 2;
			if (kLoopPlayer.getStateReligion() == eReligion)
			{
				AttitudeTypes eAttitude = AI_getAttitude((PlayerTypes)i, false);
				if (eAttitude >= ATTITUDE_PLEASED)
				{
					iLikedReligionCivs++;
					if (eAttitude >= ATTITUDE_FRIENDLY)
						iLikedReligionCivs++;
				}
			}
		}
	}

	// * up to +100% boost for liked civs having this has their state religion. (depending on personality)
	// * less potential boost if we have aspirations of being a religious leader
	// * minimum boost for apostolic palace religion
	const CvLeaderHeadInfo& kPersonality = GC.getInfo(getPersonalityType());
	int iDiplomaticBase = 50 + 5 * range(kPersonality.getSameReligionAttitudeChangeLimit() - kPersonality.getDifferentReligionAttitudeChange(), 0, 10);
	// note. with the default xml, the highest the above number can get is 50 + 5*9 (Zara Yaqob). For most leaders its roughly 50 + 5*5. No one gets 100.
	// also, most civs with a high base modifier also have iReligionFlavor > 0; and so their final modifier will be reduced.
	int iDiplomaticModifier = 10 * iDiplomaticBase * iLikedReligionCivs / std::max(1, iTotalCivs);
	iDiplomaticModifier /= 10 + iReligionFlavor;
	// advc.131: AP matters even if diplo modifier already high
	//if (iDiplomaticModifier < iDiplomaticBase/3) {
	for (VoteSourceTypes i = (VoteSourceTypes)0; i < GC.getNumVoteSourceInfos(); i = (VoteSourceTypes)(i+1))
	{	/*  advc.131: Shouldn't check if we're a member now when deciding whether
			we should be one. */
		if (/*isLoyalMember(i) &&*/
				GC.getGame().isDiploVote(i) && GC.getGame().getVoteSourceReligion(i) == eReligion)
			iDiplomaticModifier += iDiplomaticBase/3; // advc.131: += instead of =
	}
	iValue *= 100 + iDiplomaticModifier;
	iValue /= 100;

	return iValue;
}

/// \brief Value of espionage mission at this plot.
///
/// Assigns value to espionage mission against ePlayer at pPlot, where iData can provide additional information about mission.
/*  K-Mod note: A rough rule of thumb for this evaluation is that
	depriving the enemy of 1 commerce is worth 1 point;
	gaining 1 commerce for ourself is worth 2 points. */
int CvPlayerAI::AI_espionageVal(PlayerTypes eTargetPlayer, EspionageMissionTypes eMission, CvPlot* pPlot, int iData) const
{
	TeamTypes eTargetTeam = GET_PLAYER(eTargetPlayer).getTeam();
	if(eTargetPlayer == NO_PLAYER ||
			!canDoEspionageMission(eMission, eTargetPlayer, pPlot, iData, NULL))
		return 0;
	// <advc> I'm removing the other pPlot checks in this function
	if(pPlot == NULL)
	{
		FAssert(pPlot != NULL);
		return 0;
	}
	CvCityAI const* pCity = pPlot->AI_getPlotCity(); // </advc>
	bool const bMalicious = AI_isMaliciousEspionageTarget(eTargetPlayer);
	bool const bDisorder = (pCity != NULL && pCity->isDisorder()); // advc.120b

	int iValue = 0;
	if (bMalicious && GC.getInfo(eMission).isDestroyImprovement())
	{
		if (pPlot->getOwner() == eTargetPlayer)
		{
			ImprovementTypes eImprovement = pPlot->getImprovementType();
			if (eImprovement != NO_IMPROVEMENT)
			{
				BonusTypes eBonus = pPlot->getNonObsoleteBonusType(eTargetTeam);
				if (NO_BONUS != eBonus)
				{
					iValue += 2*GET_PLAYER(eTargetPlayer).AI_bonusVal(eBonus, -1); // was 1*

					int iTempValue = 0;
					if (NULL != pPlot->getWorkingCity())
					{
						iTempValue += (pPlot->calculateImprovementYieldChange(eImprovement, YIELD_FOOD, pPlot->getOwner()) * 2);
						iTempValue += (pPlot->calculateImprovementYieldChange(eImprovement, YIELD_PRODUCTION, pPlot->getOwner()) * 1);
						iTempValue += (pPlot->calculateImprovementYieldChange(eImprovement, YIELD_COMMERCE, pPlot->getOwner()) * 2);
						iTempValue += GC.getInfo(eImprovement).getUpgradeTime() / 2;
						iValue += iTempValue;
					}
				}
			}
		}
	}

	if (bMalicious && GC.getInfo(eMission).getDestroyBuildingCostFactor() > 0)
	{
		FAssert(iData >= 0 && iData < GC.getNumBuildingInfos());
		if (canSpyDestroyBuilding(eTargetPlayer, (BuildingTypes)iData) && pCity != NULL &&
			pCity->getNumRealBuilding((BuildingTypes)iData) > 0)
		{
			CvBuildingInfo& kBuilding = GC.getInfo((BuildingTypes)iData);
			int iTurns = pCity->getProductionTurnsLeft((BuildingTypes)iData, 1);
			if (iTurns < MAX_INT && // advc.004x: Let's check iTurns _and_ bDisorder
					!bDisorder) // K-Mod: disorder messes up the evaluation of production and of building value. That's the only reason for this condition.
			{	// Note: I'm not allowing recursion in the building evaluation.
				// This may cause the cached value to be inaccurate, but it doesn't really matter, because the building is already built!
				// (AI_buildingValue gives units of 4x commerce/turn)
				iValue += kBuilding.getProductionCost() / 2;
				iValue += (2 + iTurns) * pCity->AI_buildingValue((BuildingTypes)
						iData, 0, 0, false, false) / 5;
			} // K-Mod end
		}
	}

	if (bMalicious && GC.getInfo(eMission).getDestroyProjectCostFactor() > 0)
	{
		FAssert(iData >= 0 && iData < GC.getNumProjectInfos());
		if (canSpyDestroyProject(eTargetPlayer, (ProjectTypes)iData))
		{
			CvProjectInfo& kProject = GC.getInfo((ProjectTypes)iData);
			iValue += getProductionNeeded((ProjectTypes)iData) * (kProject.getMaxTeamInstances() == 1 ? 8 : 6); // was 3 : 2
		}
	}

	if (bMalicious && GC.getInfo(eMission).getDestroyProductionCostFactor() > 0)
	{
		FAssert(pCity != NULL);
		if (pCity != NULL)
		{
			int iTempValue = pCity->getProduction();
			if (iTempValue > 0)
			{
				if (pCity->getProductionProject() != NO_PROJECT)
				{
					CvProjectInfo& kProject = GC.getInfo(pCity->getProductionProject());
					iValue += iTempValue * ((kProject.getMaxTeamInstances() == 1) ? 6 : 3);	// was 4 : 2
				}
				else if (pCity->isProductionBuilding())
				{
					if (GC.getInfo(pCity->getProductionBuilding()).isWorldWonder())
						iValue += 3 * iTempValue;
					iValue += iTempValue;
				}
				else iValue += iTempValue;
			}
		}
	}

	if (bMalicious && GC.getInfo(eMission).getDestroyUnitCostFactor() > 0 || GC.getInfo(eMission).getBuyUnitCostFactor() > 0)
	{
		CvUnit* pUnit = GET_PLAYER(eTargetPlayer).getUnit(iData);
		if (pUnit != NULL)
		{
			UnitTypes eUnit = pUnit->getUnitType();

			iValue += GET_PLAYER(eTargetPlayer).AI_unitValue(eUnit,
					GC.getInfo(eUnit).getDefaultUnitAIType(), pUnit->area());
			if (GC.getInfo(eMission).getBuyUnitCostFactor() > 0)
			{
				/*if (!canTrain(eUnit) || getProductionNeeded(eUnit) > iCost)
					iValue += AI_unitValue(eUnit, (UnitAITypes)GC.getInfo(eUnit).getDefaultUnitAIType(), pUnit->area());*/
				// K-Mod. AI_unitValue is a relative rating value. It shouldn't be used for this.
				// (The espionage mission is not enabled anyway, so I'm not going to put a lot of effort into it.)
				iValue += GC.getInfo(eUnit).getProductionCost() * (canTrain(eUnit) ? 4 : 8); // kmodx: Parentheses added; not sure if needed.
				//
			}
		}
	}

	if (bMalicious && GC.getInfo(eMission).getStealTreasuryTypes() > 0)
	{
		if(pCity != NULL)
		{
			/* int iGoldStolen = (GET_PLAYER(eTargetPlayer).getGold() * GC.getInfo(eMission).getStealTreasuryTypes()) / 100;
			iGoldStolen *= pPlot->getPlotCity()->getPopulation();
			iGoldStolen /= std::max(1, GET_PLAYER(eTargetPlayer).getTotalPopulation());
			iValue += ((GET_PLAYER(eTargetPlayer).AI_isFinancialTrouble() || AI_isFinancialTrouble()) ? 4 : 2) * (2 * std::max(0, iGoldStolen - iCost));*/
			// K-Mod
			int iGoldStolen = getEspionageGoldQuantity(eMission, eTargetPlayer, pCity);
			iValue += (GET_PLAYER(eTargetPlayer).AI_isFinancialTrouble() || AI_isFinancialTrouble() ? 6 : 4) * iGoldStolen;
		}
	}

	if (GC.getInfo(eMission).getCounterespionageNumTurns() > 0)
	{
		//iValue += 100 * GET_TEAM(getTeam()).AI_getAttitudeVal(eTargetTeam);

		// K-Mod (I didn't comment that line out, btw.)
		const int iEra = getCurrentEra();
		// advc.001: was logical '||'
		int iCounterValue = 5 + 3*iEra + (AI_atVictoryStage(AI_VICTORY_CULTURE3 | AI_VICTORY_SPACE3) ? 20 : 0);
		// this is pretty bogus. I'll try to come up with something better some other time.
		iCounterValue *= 50*iEra*(iEra+1)/2 + GET_TEAM(eTargetTeam).getEspionagePointsAgainstTeam(getTeam());
		iCounterValue /= std::max(1, 50*iEra*(iEra+1) + GET_TEAM(getTeam()).getEspionagePointsAgainstTeam(eTargetTeam))/2;
		iCounterValue *=
				// advc.130j:
				(int)ceil(AI_getMemoryCount(eTargetPlayer, MEMORY_SPY_CAUGHT) / 2.0)
				+ (GET_TEAM(getTeam()).isAtWar(eTargetTeam)?2 :0) + (AI_atVictoryStage(AI_VICTORY_CULTURE4 | AI_VICTORY_SPACE3)?2 : 0);
		iValue += iCounterValue;
	}

	if (bMalicious && GC.getInfo(eMission).getBuyCityCostFactor() > 0)
	{
		if (pCity != NULL)
		{
			iValue += AI_cityTradeVal(*pCity, /* advc.ctr: */ getID());
		}
	}

	if (GC.getInfo(eMission).getCityInsertCultureAmountFactor() > 0)
	{
		if (pCity != NULL && pCity->getOwner() != getID())
		{
			int iCultureAmount = GC.getInfo(eMission).getCityInsertCultureAmountFactor() * pPlot->getCulture(getID());
			iCultureAmount /= 100;
			/*if (pCity->calculateCulturePercent(getID()) > 40)
			iValue += iCultureAmount * 3;*/
			// K-Mod - both offensive & defensive use of spread culture mission. (The first "if" is really just for effeciency.)
			if (pCity->calculateCulturePercent(getID()) >= 8)
			{
				CvMap const& m = GC.getMap(); // advc
				const CvCity* pOurClosestCity = m.findCity(pPlot->getX(), pPlot->getY(), getID());
				if (pOurClosestCity != NULL)
				{
					int iDistance = pCity->cultureDistance(m.xDistance(pPlot->getX(), pOurClosestCity->getX()), m.yDistance(pPlot->getY(), pOurClosestCity->getY()));
					if (iDistance < 6)
					{	// advc:
						int iPressure = std::max(pCity->AI_culturePressureFactor() -
								100, pOurClosestCity->AI().AI_culturePressureFactor());
						int iMultiplier = std::min(2, (6 - iDistance) * iPressure / 500);
						iValue += iCultureAmount * iMultiplier;
					}
				}
			} // K-Mod end
		}
	}

	if (bMalicious && GC.getInfo(eMission).getCityPoisonWaterCounter() > 0)
	{
		if (pCity != NULL && /* advc.120b: */ !bDisorder)
		{
			int iCityHealth = pCity->goodHealth() - pCity->badHealth(false, 0);
			//int iBaseUnhealth = GC.getInfo(eMission).getCityPoisonWaterCounter();
			int iBaseUnhealth = GC.getInfo(eMission).getCityPoisonWaterCounter() - std::max(pCity->getOccupationTimer(), GET_PLAYER(eTargetPlayer).getAnarchyTurns());

			// K-Mod: fixing some "wtf".
			/*int iAvgFoodShortage = std::max(0, iBaseUnhealth - iCityHealth) - pCity->foodDifference();
			iAvgFoodShortage += std::max(0, iBaseUnhealth/2 - iCityHealth) - pCity->foodDifference();
			iAvgFoodShortage /= 2;
			if (iAvgFoodShortage > 0)
				iValue += 8 * iAvgFoodShortage * iAvgFoodShortage;*/
			int iAvgFoodShortage = (std::max(0, iBaseUnhealth - iCityHealth) + std::max(0, -iCityHealth))/2 - pCity->foodDifference(true, true);

			if (iAvgFoodShortage > 0)
			{
				iValue += 3 * iAvgFoodShortage * iBaseUnhealth;
				/*  advc.160: Prefer large cities w/o Granaries.
				Too little impact?
				iValue could be sth. like 100, threshold e.g. 30,
				food kept 12. */
				iValue += pCity->growthThreshold() - pCity->getFoodKept();
			}
			// K-Mod end
		}
	}

	if (bMalicious && GC.getInfo(eMission).getCityUnhappinessCounter() > 0)
	{
		if (NULL != pCity && /* advc.120b: */ !bDisorder)
		{
			int iCityCurAngerLevel = pCity->happyLevel() - pCity->unhappyLevel(0);
			//int iBaseAnger = GC.getInfo(eMission).getCityUnhappinessCounter();
			// K-Mod:
			int iBaseAnger = GC.getInfo(eMission).getCityUnhappinessCounter() - std::max(pCity->getOccupationTimer(), GET_PLAYER(eTargetPlayer).getAnarchyTurns());

			int iAvgUnhappy = iCityCurAngerLevel - iBaseAnger/2;
			if (iAvgUnhappy < 0)
			{
				iValue += 7 * abs(iAvgUnhappy) * iBaseAnger;// down from 14
			}
		}
	}

	/*if (bMalicious && GC.getInfo(eMission).getCityRevoltCounter() > 0)
	{
		// Handled elsewhere
	}*/

	if (GC.getInfo(eMission).getBuyTechCostFactor() > 0)
	{
		FAssert(iData >= 0 && iData < GC.getNumTechInfos());

		//if (iCost < GET_TEAM(getTeam()).getResearchLeft((TechTypes)iData) * 4 / 3)
		if (canStealTech(eTargetPlayer, (TechTypes)iData)) // K-Mod!
		{
			//int iTempValue = GET_TEAM(getTeam()).AI_techTradeVal((TechTypes)iData, GET_PLAYER(eTargetPlayer).getTeam());
			// K-Mod
			int iTempValue = GET_TEAM(getTeam()).AI_techTradeVal((TechTypes)iData, eTargetTeam);
			iTempValue *= AI_isDoStrategy(AI_STRATEGY_BIG_ESPIONAGE) ? 6 : 5;
			iTempValue /= 3;
			// K-Mod end

			iValue += iTempValue;
		}
	}

	if (bMalicious && GC.getInfo(eMission).getSwitchCivicCostFactor() > 0)
	{
		iValue += AI_civicTradeVal((CivicTypes)iData, eTargetPlayer);
	}

	if (bMalicious && GC.getInfo(eMission).getSwitchReligionCostFactor() > 0)
	{	// <advc.132>
		ReligionTypes eReligion = (ReligionTypes)iData;
		if(GET_PLAYER(eTargetPlayer).isMajorReligion(eReligion)) // </advc.132>
			iValue += AI_religionTradeVal(eReligion, eTargetPlayer);
	}

	/*if (bMalicious && GC.getInfo(eMission).getPlayerAnarchyCounter() > 0)
	{
		// AI doesn't use Player Anarchy
	}*/

	return iValue;
}

// K-Mod
bool CvPlayerAI::AI_isMaliciousEspionageTarget(PlayerTypes eTarget) const
{
	// advc.120b: was GET_PLAYER(eTarget).getTeam() == getTeam()
	if (GET_PLAYER(eTarget).getMasterTeam() == getMasterTeam()
			|| eTarget == BARBARIAN_PLAYER) // advc.003n
		return false;
	return
	(//AI_getAttitude(eTarget) <= (GC.getGame().isOption(GAMEOPTION_AGGRESSIVE_AI) ? ATTITUDE_PLEASED : ATTITUDE_CAUTIOUS) ||
			// <advc.120b>: Replacing the above:
			GC.getInfo(getPersonalityType()).getNoWarAttitudeProb(
			std::min(AI_getAttitude(eTarget) + 1, NUM_ATTITUDE_TYPES - 1)) < 100 ||
			// </advc.120b>
			GET_TEAM(getTeam()).AI_getWarPlan(TEAMID(eTarget)) != NO_WARPLAN ||
			(AI_atVictoryStage4() && GET_PLAYER(eTarget).AI_atVictoryStage4() &&
			!AI_atVictoryStage(AI_VICTORY_DIPLOMACY3)) // advc.120b: was DIPLOMACY1
	);
} // K-Mod end


int CvPlayerAI::AI_getPeaceWeight() const
{
	return m_iPeaceWeight;
}


void CvPlayerAI::AI_setPeaceWeight(int iNewValue)
{
	m_iPeaceWeight = iNewValue;
}

int CvPlayerAI::AI_getEspionageWeight() const
{
	if (GC.getGame().isOption(GAMEOPTION_NO_ESPIONAGE))
	{
		return 0;
	}
	return m_iEspionageWeight;
}

void CvPlayerAI::AI_setEspionageWeight(int iNewValue)
{
	m_iEspionageWeight = iNewValue;
}


int CvPlayerAI::AI_getAttackOddsChange() const
{
	return m_iAttackOddsChange;
}


void CvPlayerAI::AI_setAttackOddsChange(int iNewValue)
{
	m_iAttackOddsChange = iNewValue;
}


int CvPlayerAI::AI_getCivicTimer() const
{
	return m_iCivicTimer;
}


void CvPlayerAI::AI_setCivicTimer(int iNewValue)
{
	m_iCivicTimer = iNewValue;
	FAssert(AI_getCivicTimer() >= 0);
}


void CvPlayerAI::AI_changeCivicTimer(int iChange)
{
	AI_setCivicTimer(AI_getCivicTimer() + iChange);
}


int CvPlayerAI::AI_getReligionTimer() const
{
	return m_iReligionTimer;
}


void CvPlayerAI::AI_setReligionTimer(int iNewValue)
{
	m_iReligionTimer = iNewValue;
	FAssert(AI_getReligionTimer() >= 0);
}


void CvPlayerAI::AI_changeReligionTimer(int iChange)
{
	AI_setReligionTimer(AI_getReligionTimer() + iChange);
}

int CvPlayerAI::AI_getExtraGoldTarget() const
{
	return m_iExtraGoldTarget;
}

void CvPlayerAI::AI_setExtraGoldTarget(int iNewValue)
{
	m_iExtraGoldTarget = iNewValue;
}

int CvPlayerAI::AI_getNumTrainAIUnits(UnitAITypes eIndex) const
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < NUM_UNITAI_TYPES, "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_aiNumTrainAIUnits[eIndex];
}


void CvPlayerAI::AI_changeNumTrainAIUnits(UnitAITypes eIndex, int iChange)
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < NUM_UNITAI_TYPES, "eIndex is expected to be within maximum bounds (invalid Index)");
	m_aiNumTrainAIUnits[eIndex] = (m_aiNumTrainAIUnits[eIndex] + iChange);
	FAssert(AI_getNumTrainAIUnits(eIndex) >= 0);
}


int CvPlayerAI::AI_getNumAIUnits(UnitAITypes eIndex) const
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < NUM_UNITAI_TYPES, "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_aiNumAIUnits[eIndex];
}


void CvPlayerAI::AI_changeNumAIUnits(UnitAITypes eIndex, int iChange)
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < NUM_UNITAI_TYPES, "eIndex is expected to be within maximum bounds (invalid Index)");
	m_aiNumAIUnits[eIndex] = (m_aiNumAIUnits[eIndex] + iChange);
	FAssert(AI_getNumAIUnits(eIndex) >= 0);
}


int CvPlayerAI::AI_getSameReligionCounter(PlayerTypes eIndex) const
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < MAX_PLAYERS, "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_aiSameReligionCounter[eIndex];
}


void CvPlayerAI::AI_changeSameReligionCounter(PlayerTypes eIndex, int iChange)
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < MAX_PLAYERS, "eIndex is expected to be within maximum bounds (invalid Index)");
	m_aiSameReligionCounter[eIndex] = (m_aiSameReligionCounter[eIndex] + iChange);
	FAssert(AI_getSameReligionCounter(eIndex) >= 0);
}


int CvPlayerAI::AI_getDifferentReligionCounter(PlayerTypes eIndex) const
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < MAX_PLAYERS, "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_aiDifferentReligionCounter[eIndex];
}


void CvPlayerAI::AI_changeDifferentReligionCounter(PlayerTypes eIndex, int iChange)
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < MAX_PLAYERS, "eIndex is expected to be within maximum bounds (invalid Index)");
	m_aiDifferentReligionCounter[eIndex] = (m_aiDifferentReligionCounter[eIndex] + iChange);
	FAssert(AI_getDifferentReligionCounter(eIndex) >= 0);
}


int CvPlayerAI::AI_getFavoriteCivicCounter(PlayerTypes eIndex) const
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < MAX_PLAYERS, "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_aiFavoriteCivicCounter[eIndex];
}


void CvPlayerAI::AI_changeFavoriteCivicCounter(PlayerTypes eIndex, int iChange)
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < MAX_PLAYERS, "eIndex is expected to be within maximum bounds (invalid Index)");
	m_aiFavoriteCivicCounter[eIndex] = (m_aiFavoriteCivicCounter[eIndex] + iChange);
	FAssert(AI_getFavoriteCivicCounter(eIndex) >= 0);
}


int CvPlayerAI::AI_getBonusTradeCounter(PlayerTypes eIndex) const
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < MAX_PLAYERS, "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_aiBonusTradeCounter[eIndex];
}


void CvPlayerAI::AI_changeBonusTradeCounter(PlayerTypes eIndex, int iChange)
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < MAX_PLAYERS, "eIndex is expected to be within maximum bounds (invalid Index)");
	m_aiBonusTradeCounter[eIndex] = (m_aiBonusTradeCounter[eIndex] + iChange);
	FAssert(AI_getBonusTradeCounter(eIndex) >= 0);
}


int CvPlayerAI::AI_getPeacetimeTradeValue(PlayerTypes eIndex) const
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < MAX_PLAYERS, "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_aiPeacetimeTradeValue[eIndex];
}


int CvPlayerAI::AI_getPeacetimeGrantValue(PlayerTypes eIndex) const
{
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < MAX_PLAYERS, "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_aiPeacetimeGrantValue[eIndex];
}

// <advc.130p> Merged (and renamed) AI_changePeacetimeTradeValue and AI_changePeacetimeGrantValue
void CvPlayerAI::AI_processPeacetimeTradeValue(PlayerTypes eIndex, int iChange)
{
	AI_processPeacetimeValue(eIndex, iChange, false);
}


void CvPlayerAI::AI_processPeacetimeGrantValue(PlayerTypes eIndex, int iChange)
{
	AI_processPeacetimeValue(eIndex, iChange, true);
}

// Based on the old AI_changePeacetimeGrantValue
void CvPlayerAI::AI_processPeacetimeValue(PlayerTypes eIndex, int iChange,
		bool bGrant, bool bPeace, TeamTypes ePeaceTradeTarget, TeamTypes eWarTradeTarget)
{
	PROFILE_FUNC();
	FAssertBounds(0, MAX_CIV_PLAYERS, eIndex); // advc.003n
	if(iChange == 0)
		return;
	int* aiPeacetimeValue = (bGrant ? m_aiPeacetimeGrantValue : m_aiPeacetimeTradeValue);
	CvGame& g = GC.getGame();
	double val = (1000000.0 * iChange) / std::max(
			// Trade values are higher relative to game progress in slow games
			GC.getInfo(g.getGameSpeedType()).getResearchPercent() *
			// Larger maps have higher tech costs
			GC.getInfo(GC.getMap().getWorldSize()).getResearchPercent() *
			// Other AI modifiers have the opposite effect (see AI_peacetimeTradeMultiplier)
			GC.getInfo(g.getHandicapType()).getAIResearchPercent(), 1);
	/*  Times 1000 for increased precision, times 2 b/c "fair trade" is twice as
		sensitive as "traded with worst enemy" (it's this way too in BtS). */
	int iIncr = ::round(val * 2000 *
			/*  In BtS, trade and grant values were adjusted to the game progress
				when computing relations modifiers. Better do it here, based on the
				game progress at the time when the trade happens. */
			AI_peacetimeTradeMultiplier(eIndex));
	// <advc.130v>
	PlayerTypes eMaster = NO_PLAYER;
	if(GET_TEAM(eIndex).isCapitulated())
	{
		iIncr /= 2;
		eMaster = GET_TEAM(GET_PLAYER(eIndex).getMasterTeam()).getLeaderID();
	} // advc.130p: No PeacetimeTrade for reparations; only RivalTrade.
	if(!bPeace && ePeaceTradeTarget == NO_TEAM)
	{
		if(eMaster != NO_PLAYER)
			aiPeacetimeValue[eMaster] += iIncr;
		// </advc.130v>
		aiPeacetimeValue[eIndex] += iIncr;
		FAssert(aiPeacetimeValue[eIndex] >= 0 && iChange > 0);
	}
	if(iChange <= 0 || TEAMID(eIndex) == getTeam())
		return;
	for(int iI = 0; iI < MAX_CIV_TEAMS; iI++)
	{
		CvTeamAI& t = GET_TEAM((TeamTypes)iI);
		if(t.getID() != TEAMID(eIndex) && t.isAlive() &&
			(eMaster == NO_PLAYER || t.getID() != TEAMID(eMaster)) &&
			t.isHasMet(TEAMID(eIndex))) // unoffical patch bugfix, by Sephi.
		{
			// <advc.130p>
			bool bWorstEnemy = (t.AI_getWorstEnemy() == getTeam());
			// Special treatment for war enemies trading among each other
			bool bOtherWar = (t.isAtWar(TEAMID(eIndex)) || eWarTradeTarget == t.getID());
			bool bWar = t.isAtWar(getTeam());
			if(!bWorstEnemy && !(bWar && !bOtherWar))
				continue;
			// No anger among vassal-master when they make peace
			if(bPeace && GET_TEAM(eIndex).getMasterTeam() == t.getID())
				continue;
			/*  Don't mind if t's worst enemy is paid to make peace with a civ that
				t likes. Unless t is at war with its worst enemy because, then,
				the peace deals means that t loses a war ally. */
			if(ePeaceTradeTarget == t.getMasterTeam() || (ePeaceTradeTarget != NO_TEAM &&
					t.AI_getAttitude(ePeaceTradeTarget) >= ATTITUDE_PLEASED && !bWar &&
					!t.isAtWar(ePeaceTradeTarget))) // Should be implied by attitude, but let's make sure.
				continue; // </advc.130p>
			double mult = 2000;
			/*  Avoid oscillation of worst enemy, but still punish non-war party
				for helping a war enemy. */
			int iDelta = ::range(t.AI_getAttitudeVal(TEAMID(eIndex)) -
					(bOtherWar ? 2 : 0) -
					(t.AI_getAttitudeVal(t.AI_getWorstEnemy()) -
					(bWar ? 2 : 0)),
					0, 7);
			mult *= iDelta / 5.0;
			// <advc.130p>
			if(bPeace) // But count brokered peace in full
				mult *= 0.4;
			if(bWar && bWorstEnemy && !bOtherWar)
				mult *= 1.33;
			else if(bWorstEnemy && !bOtherWar)
				mult *= 0.9;
			else mult *= 0.67; // </advc.130p>
			int iEnemyIncr = ::round(val * mult * AI_peacetimeTradeMultiplier(NO_PLAYER, t.getID()));
			if(iEnemyIncr <= 0)
				continue;
			// <advc.130v>
			if(GET_TEAM(eIndex).isCapitulated() && !bGrant) // Vassals don't make gifts
			{
				iEnemyIncr /= 2;
				t.AI_changeEnemyPeacetimeTradeValue(TEAMID(eMaster), iEnemyIncr);
			} // </advc.130v>
			if(bGrant)
				t.AI_changeEnemyPeacetimeGrantValue(TEAMID(eIndex), iEnemyIncr);
			else t.AI_changeEnemyPeacetimeTradeValue(TEAMID(eIndex), iEnemyIncr);
		}
	}
}

/*  Adjust assets to the scale of trade values (formula determined
	through trial and error) */
double CvPlayerAI::AI_peacetimeTradeMultiplier(PlayerTypes eOtherPlayer, TeamTypes eOtherTeam) const
{
	FAssert(eOtherPlayer == NO_PLAYER || eOtherTeam == NO_TEAM);
	bool bTeam = (eOtherPlayer == NO_PLAYER);
	int iAssets = (bTeam ? GET_TEAM(getTeam()).getAssets() : getAssets());
	int iOtherAssets = (bTeam ? GET_TEAM(eOtherTeam).getAssets() :
			GET_PLAYER(eOtherPlayer).getAssets());
	FAssertMsg(iAssets + iOtherAssets > 0, "Negative base for std::pow");
	CvHandicapInfo const& h = GC.getInfo(GC.getGame().getHandicapType());
	/*  Far more AI assets if difficulty is high; trade values not affected
		by difficulty as much. */
	double r = 1000000.0 / (h.getAIGrowthPercent() * // advc.251
			h.getAITrainPercent() * h.getAIConstructPercent());
	r *= 3.5 / std::pow((iAssets + iOtherAssets) / 2.0, 1.33);
	return r;
}

void CvPlayerAI::AI_setPeacetimeTradeValue(PlayerTypes eIndex, int iVal)
{
	m_aiPeacetimeTradeValue[eIndex] = iVal;
}

void CvPlayerAI::AI_setPeacetimeGrantValue(PlayerTypes eIndex, int iVal)
{
	m_aiPeacetimeGrantValue[eIndex] = iVal;
} // </advc.130p>

// <advc.130k>
void CvPlayerAI::AI_setSameReligionCounter(PlayerTypes eIndex, int iValue)
{
	m_aiSameReligionCounter[eIndex] = iValue;
}


void CvPlayerAI::AI_setDifferentReligionCounter(PlayerTypes eIndex, int iValue)
{
	m_aiDifferentReligionCounter[eIndex] = iValue;
}


void CvPlayerAI::AI_setFavoriteCivicCounter(PlayerTypes eIndex, int iValue)
{
	m_aiFavoriteCivicCounter[eIndex] = iValue;
}


void CvPlayerAI::AI_setBonusTradeCounter(PlayerTypes eIndex, int iValue)
{
	m_aiBonusTradeCounter[eIndex] = iValue;
} // </advc.130k>

/*  <advc.130x> modes:
	0: same religion, 1 differing religion, 2 fav civic */
int CvPlayerAI::AI_ideologyDiploLimit(PlayerTypes eOther, int iMode) const
{
	if(iMode < 0 || iMode > 2)
	{
		FAssert(false);
		return -1;
	}
	CvLeaderHeadInfo& lh = GC.getInfo(getPersonalityType());
	int iLHLimit;
	switch(iMode)
	{
	case 0: iLHLimit = lh.getSameReligionAttitudeChangeLimit(); break;
	case 1: iLHLimit = lh.getDifferentReligionAttitudeChangeLimit(); break;
	case 2: iLHLimit = lh.getFavoriteCivicAttitudeChangeLimit(); break;
	default: FAssert(false); iLHLimit = 0;
	}
	iLHLimit = abs(iLHLimit);
	// Don't further reduce a limit of 1
	if(iLHLimit <= 1)
		return iLHLimit;
	int iTotal = 0;
	int iShared = 0;
	for(int i = 0; i < MAX_CIV_PLAYERS; i++)
	{
		CvPlayer const& kLoopPlayer = GET_PLAYER((PlayerTypes)i);
		if(!kLoopPlayer.isAlive() || kLoopPlayer.isMinorCiv() || kLoopPlayer.getID() == getID() ||
				kLoopPlayer.getID() == eOther || GET_TEAM(kLoopPlayer.getTeam()).isCapitulated() ||
				!GET_TEAM(getTeam()).isHasMet(kLoopPlayer.getTeam()))
			continue;
		iTotal++;
		if(iMode == 0 && getStateReligion() != NO_RELIGION &&
				getStateReligion() == kLoopPlayer.getStateReligion())
			iShared++;
		else if(iMode == 1 && getStateReligion() != NO_RELIGION &&
				kLoopPlayer.getStateReligion() != NO_RELIGION &&
				getStateReligion() != kLoopPlayer.getStateReligion())
			iShared++;
		else if(iMode == 2)
		{
			CivicTypes eFavCivic = (CivicTypes)lh.getFavoriteCivic();
			if(eFavCivic != NO_CIVIC && isCivic(eFavCivic) && kLoopPlayer.isCivic(eFavCivic))
				iShared++;
		}
	}
	double ratio = 0.5;
	/*  Since we're not counting us and them, the total can be 0. Treat this case
		as if 50% of the other civs were sharing the ideology. */
	if(iTotal > 0)
		ratio = iShared / (double)iTotal;
	// 0.51: To make sure we round up if the ratio is 100%
	return iLHLimit - ::round(0.51 * ratio * iLHLimit);
} // </advc.130x>


int CvPlayerAI::AI_getGoldTradedTo(PlayerTypes eIndex) const
{
	FAssertBounds(0, MAX_CIV_PLAYERS, eIndex); // advc.003n
	return m_aiGoldTradedTo[eIndex];
}


void CvPlayerAI::AI_changeGoldTradedTo(PlayerTypes eIndex, int iChange)
{
	FAssertBounds(0, MAX_CIV_PLAYERS, eIndex); // advc.003n
	m_aiGoldTradedTo[eIndex] = (m_aiGoldTradedTo[eIndex] + iChange);
	FAssert(AI_getGoldTradedTo(eIndex) >= 0);
}


int CvPlayerAI::AI_getAttitudeExtra(PlayerTypes eIndex) const
{
	FAssertBounds(0, MAX_CIV_PLAYERS, eIndex); // advc.003n
	return m_aiAttitudeExtra[eIndex];
}


void CvPlayerAI::AI_setAttitudeExtra(PlayerTypes eIndex, int iNewValue)
{
	FAssertBounds(0, MAX_CIV_PLAYERS, eIndex); // advc.003n
	int iChange = iNewValue - m_aiAttitudeExtra[eIndex]; // K-Mod
	m_aiAttitudeExtra[eIndex] = iNewValue;
	// K-Mod
	if (iChange)
		AI_changeCachedAttitude(eIndex, iChange);
	// K-Mod end
}


void CvPlayerAI::AI_changeAttitudeExtra(PlayerTypes eIndex, int iChange)
{
	AI_setAttitudeExtra(eIndex, (AI_getAttitudeExtra(eIndex) + iChange));
}


void CvPlayerAI::AI_setFirstContact(PlayerTypes eIndex, bool bNewValue)
{
	/*  advc.003j: This Vanilla Civ 4 function was called on DIPLOEVENT_CONTACT and
		DIPLOEVENT_FAILED_CONTACT (CvPlayer::handleDiploEvent) and in CvTeam::declareWar,
		but AI_isFirstContact was never called. Looks like these two functions were
		superseded by the team-level functions meet/makeHasMet and isHasMet. */
	FAssertMsg(false, "m_abFirstContact is obsolete");
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < MAX_PLAYERS, "eIndex is expected to be within maximum bounds (invalid Index)");
	m_abFirstContact[eIndex] = bNewValue;
}


bool CvPlayerAI::AI_isFirstContact(PlayerTypes eIndex) const
{
	FAssertMsg(false, "m_abFirstContact is obsolete"); // advc.003j
	FAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	FAssertMsg(eIndex < MAX_PLAYERS, "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_abFirstContact[eIndex];
}


int CvPlayerAI::AI_getContactTimer(PlayerTypes eIndex1, ContactTypes eIndex2) const
{
	// advc.003n: Can't contact Barbarians
	FAssert(!isBarbarian());
	FAssertBounds(0, MAX_CIV_PLAYERS, eIndex1);
	FAssertBounds(0, NUM_CONTACT_TYPES, eIndex2);
	return m_aaiContactTimer[eIndex1][eIndex2];
}


void CvPlayerAI::AI_changeContactTimer(PlayerTypes eIndex1, ContactTypes eIndex2, int iChange)
{
	// advc.003n: Can't contact Barbarians
	FAssert(!isBarbarian());
	FAssertBounds(0, MAX_CIV_PLAYERS, eIndex1);
	FAssertBounds(0, NUM_CONTACT_TYPES, eIndex2);
	m_aaiContactTimer[eIndex1][eIndex2] = (AI_getContactTimer(eIndex1, eIndex2) + iChange);
	FAssert(AI_getContactTimer(eIndex1, eIndex2) >= 0);
}


int CvPlayerAI::AI_getMemoryCount(PlayerTypes eIndex1, MemoryTypes eIndex2) const
{
	// advc.003n: No memory of and about Barbarians
	FAssert(!isBarbarian());
	FAssertBounds(0, MAX_CIV_PLAYERS, eIndex1);
	FAssertBounds(0, NUM_MEMORY_TYPES, eIndex2);
	return m_aaiMemoryCount[eIndex1][eIndex2];
}


void CvPlayerAI::AI_changeMemoryCount(PlayerTypes eIndex1, MemoryTypes eIndex2, int iChange)
{
	// advc: Code moved into AI_setMemory
	AI_setMemoryCount(eIndex1, eIndex2, m_aaiMemoryCount[eIndex1][eIndex2] + iChange);
}

/*  advc: Setter added b/c it's awkward to use AI_changeMemoryCount for
	setting memory counts. Code mostly cut from AI_changeMemoryCount. */
void CvPlayerAI::AI_setMemoryCount(PlayerTypes eAboutPlayer, MemoryTypes eMemoryType, int iValue) {

	FAssert(!isBarbarian()); // advc.003n: No memory of and about Barbarians
	FAssertBounds(0, MAX_CIV_PLAYERS, eAboutPlayer);
	FAssertBounds(0, NUM_MEMORY_TYPES, eMemoryType);
	FAssert(iValue >= 0);
	int iAttitude = AI_getMemoryAttitude(eAboutPlayer, eMemoryType); // K-Mod
	m_aaiMemoryCount[eAboutPlayer][eMemoryType] = iValue;
	AI_changeCachedAttitude(eAboutPlayer, AI_getMemoryAttitude(eAboutPlayer, eMemoryType) - iAttitude); // K-Mod
}

// <advc.130j>
void CvPlayerAI::AI_rememberEvent(PlayerTypes ePlayer, MemoryTypes eMemoryType)
{
	int delta = 2;
	// Need a finer granularity for DoW
	// (Tbd.: Don't use magic values for memory granularity)
	if(eMemoryType == MEMORY_DECLARED_WAR)
		delta = 3;
#if 0 // This disables the bulk of change 130j
	int sign = 0; // 0 means it's a neutral event, 1 is good, -1 bad
	/*  Random events should have the effects that the event dialogs say they have.
		(E.g. "... you gain +1 attitude with ...")
		Don't want votes for hostile civs to improve relations much, at least not
		until the AI can figure out when a vote matters (often it doesn't). */
	if(eMemoryType != MEMORY_EVENT_GOOD_TO_US && eMemoryType != MEMORY_EVENT_BAD_TO_US && eMemoryType != MEMORY_VOTED_FOR_US) {
		int iMemoryAttitude = GC.getInfo(getPersonalityType()).getMemoryAttitudePercent(eMemoryType);
		if(iMemoryAttitude < 0)
			sign = -1;
		if(iMemoryAttitude > 0)
			sign = 1;
	}
	// We're surprised by the actions of ePlayer
	if((sign < 0 && (AI_getAttitude(ePlayer) >= ATTITUDE_FRIENDLY ||
			// <advc.130o> Surprised by war despite tribute
			(eMemoryType == MEMORY_DECLARED_WAR && !isHuman() && GET_PLAYER(ePlayer).isHuman() &&
			AI_getMemoryCount(ePlayer, MEMORY_MADE_DEMAND) > 0))) || // </advc.130o>
			(sign > 0 && AI_getAttitude(ePlayer) <= ATTITUDE_ANNOYED))
		delta++;
	// Just what we expected from ePlayer
	if((sign > 0 && AI_getAttitude(ePlayer) >= ATTITUDE_FRIENDLY) ||
			(sign < 0 && AI_getAttitude(ePlayer) <= ATTITUDE_ANNOYED))
		delta--;
#endif
	// <advc.130y> Cap DoW penalty for vassals
	if(eMemoryType == MEMORY_DECLARED_WAR && (isAVassal() || GET_TEAM(ePlayer).isAVassal()))
		delta = std::min(3, delta); // </advc.130y>
	// <advc.130f>
	if(eMemoryType == MEMORY_HIRED_TRADE_EMBARGO && GET_TEAM(getTeam()).isAtWar(TEAMID(ePlayer)))
		delta--; // </advc.130f>
	AI_changeMemoryCount(ePlayer, eMemoryType, delta);
	// <advc.130l>
	static bool const bENABLE_130L = GC.getDefineBOOL("ENABLE_130L");
	if(!bENABLE_130L)
		return;
	int const iPairs = 6;
	MemoryTypes coupledRequests[iPairs][2] =
	{
		{MEMORY_GIVE_HELP, MEMORY_REFUSED_HELP},
		{MEMORY_ACCEPT_DEMAND, MEMORY_REJECTED_DEMAND},
		{MEMORY_ACCEPTED_RELIGION, MEMORY_DENIED_RELIGION},
		{MEMORY_ACCEPTED_CIVIC, MEMORY_DENIED_CIVIC},
		{MEMORY_ACCEPTED_JOIN_WAR, MEMORY_DENIED_JOIN_WAR},
		{MEMORY_ACCEPTED_STOP_TRADING, MEMORY_DENIED_STOP_TRADING},
		/*  All the others are AI requests, so this one is odd, and
			it decays very quickly anyway. */
		//{MEMORY_VOTED_FOR_US, MEMORY_VOTED_AGAINST_US},
		/*  Could let a +2 event erase e.g. half of a remembered -2 event,
			but a +2 event is reported as two +1 events to this function;
			the current implementation can't tell the difference. */
		//{MEMORY_EVENT_GOOD_TO_US, MEMORY_EVENT_BAD_TO_US},
		// Not worth complicating things for:
		//{MEMORY_DECLARED_WAR, MEMORY_INDEPENDENCE},
		// !! Update iPairs when adding/removing pairs !!
	};
	for(int i = 0; i < iPairs; i++)
	{
		for(int j = 0; j < 2; j++)
		{
			if(eMemoryType == coupledRequests[i][j])
			{
				MemoryTypes eOtherType = coupledRequests[i][(j + 1) % 2];
				AI_changeMemoryCount(ePlayer, eOtherType, -std::min(1,
						AI_getMemoryCount(ePlayer, eOtherType)));
			}
		}
	} // </advc.130l>
} // </advc.130j>

/*  advc.003n: This player is about to raze kCity. Other players remember that
	(as if the city had already been razed). Code cut from CvPlayer::raze
	and refactored a bit. Barbarians excluded (don't want them to use any AI memory). */
void CvPlayerAI::AI_processRazeMemory(CvCity const& kCity)
{
	if(isBarbarian() || isMinorCiv())
		return;
	PlayerTypes eHighestCulturePlayer = kCity.findHighestCulture();
	/*  <advc.130q> Count at least 1 raze memory when a city has grown, even if it
		has not produced any city culture. */
	if(eHighestCulturePlayer == NO_PLAYER && kCity.getHighestPopulation() > 1)
		eHighestCulturePlayer = kCity.getPlot().calculateCulturalOwner(true);
	// </advc.130q>
	if (eHighestCulturePlayer != NO_PLAYER && eHighestCulturePlayer != BARBARIAN_PLAYER)
	{
		CvPlayerAI& kHighestCulturePlayer = GET_PLAYER(eHighestCulturePlayer);
		if (kHighestCulturePlayer.getTeam() != getTeam() &&
			!kHighestCulturePlayer.isMinorCiv() &&
			/*	advc.099: A defeated player can now have the highest culture.
				No one remembers the razed city then. */
			kHighestCulturePlayer.isAlive())
		{
			// <advc.130v>
			// advc.130j: Base effect doubled (but don't call AI_rememberEvent)
			int iMemory = 2;
			// <advc.130q>
			if(kHighestCulturePlayer.AI_razeMemoryScore(kCity) < 3)
				iMemory = 1; // </advc.130q>
			if(GET_TEAM(getTeam()).isCapitulated())
			{
				iMemory /= 2;
				kHighestCulturePlayer.AI_changeMemoryCount(
						GET_TEAM(getMasterTeam()).getLeaderID(),
						MEMORY_RAZED_CITY, iMemory);
				if(iMemory <= 0) // At least count it for the vassal then
					iMemory = 1;
			} // </advc.130v>
			kHighestCulturePlayer.AI_changeMemoryCount(getID(), MEMORY_RAZED_CITY, iMemory);
		}
	}
	for(int i = 0; i < GC.getNumReligionInfos(); i++)
	{
		if(!kCity.isHolyCity((ReligionTypes)i))
			continue;
		for(int j = 0; j < MAX_CIV_PLAYERS; j++)
		{
			CvPlayerAI& kFaithful = GET_PLAYER((PlayerTypes)j);
			if(!kFaithful.isAlive() ||
					// advc.130v: To be consistent with the HighestCulturePlayer code
					kFaithful.getTeam() == getTeam() ||
					kFaithful.getStateReligion() != i)
				continue;
			// <advc.130v>
			int iMemory = 2; // advc.130j
			if(GET_TEAM(getTeam()).isCapitulated())
			{
				iMemory = 1;
				kFaithful.AI_changeMemoryCount(
						GET_TEAM(getMasterTeam()).getLeaderID(),
						MEMORY_RAZED_HOLY_CITY, iMemory);
			} // </advc.130v>
			kFaithful.AI_changeMemoryCount(getID(), MEMORY_RAZED_HOLY_CITY, iMemory);
		}
	}
}

// K-Mod. Note, unlike some other timers, this timer is internally stored as the turn number that the timer will expire.
// With this system, the timer doesn't have to be decremented every turn.
int CvPlayerAI::AI_getCityTargetTimer() const
{
	return std::max(0, m_iCityTargetTimer - GC.getGame().getGameTurn());
}


void CvPlayerAI::AI_setCityTargetTimer(int iTurns)
{
	m_iCityTargetTimer = GC.getGame().getGameTurn() + iTurns;
} // K-Mod end


int CvPlayerAI::AI_calculateGoldenAgeValue(bool bConsiderRevolution) const
{
	int iValue = 0;
	for (int iI = 0; iI <  NUM_YIELD_TYPES; ++iI)
	{
		/*iTempValue = (GC.getInfo((YieldTypes)iI).getGoldenAgeYield() * AI_yieldWeight((YieldTypes)iI));
		iTempValue /= std::max(1, (1 + GC.getInfo((YieldTypes)iI).getGoldenAgeYieldThreshold()));*/ // BtS
		// K-Mod
		int iTempValue = GC.getInfo((YieldTypes)iI).getGoldenAgeYield() * AI_yieldWeight((YieldTypes)iI) * AI_averageYieldMultiplier((YieldTypes)iI);
		iTempValue /= 1 + GC.getInfo((YieldTypes)iI).getGoldenAgeYieldThreshold();
		//
		iValue += iTempValue;
	}

	/*iValue *= getTotalPopulation();
	iValue *= GC.getGame().goldenAgeLength(); // original BtS code
	iValue /= 100;*/ // BtS
	// K-Mod
	iValue *= getTotalPopulation();
	iValue *= getGoldenAgeLength();
	iValue /= 10000;

	// K-Mod. Add some value if we would use the opportunity to switch civics
	// Note: this first "if" isn't necessary. It just saves us checking civics when we don't need to.
	if (bConsiderRevolution && getMaxAnarchyTurns() != 0 && !isGoldenAge() && getAnarchyModifier() + 100 > 0)
	{
		std::vector<CivicTypes> aeBestCivics(GC.getNumCivicOptionInfos());
		for (int iI = 0; iI < GC.getNumCivicOptionInfos(); iI++)
		{
			aeBestCivics[iI] = getCivics((CivicOptionTypes)iI);
		}

		for (int iI = 0; iI < GC.getNumCivicOptionInfos(); iI++)
		{
			int iCurrentValue = AI_civicValue(aeBestCivics[iI]);
			int iBestValue;
			CivicTypes eNewCivic = AI_bestCivic((CivicOptionTypes)iI, &iBestValue);

			// using a 10 percent thresold. (cf the higher threshold used in AI_doCivics)
			if (aeBestCivics[iI] != NO_CIVIC && 100*iBestValue > 110*iCurrentValue)
			{
				aeBestCivics[iI] = eNewCivic;
				if (gPlayerLogLevel > 0) logBBAI("      %S wants a golden age to switch to %S (value: %d vs %d)", getCivilizationDescription(0), GC.getInfo(eNewCivic).getDescription(0), iBestValue, iCurrentValue);
			}
		}

		int iAnarchyLength = getCivicAnarchyLength(&aeBestCivics[0]);
		if (iAnarchyLength > 0)
		{
			// we would switch; so what is the negation of anarchy worth?
			for (int iI = 0; iI < NUM_COMMERCE_TYPES; iI++)
			{
				int iTempValue = getCommerceRate((CommerceTypes)iI) * iAnarchyLength;
				iTempValue *= AI_commerceWeight((CommerceTypes)iI);
				iTempValue /= 100;
				iValue += iTempValue;
			}
			// production and GGP matter too, but I don't really want to try to evaluate them properly. Sorry.
			iValue += getTotalPopulation() * iAnarchyLength;
			// On the other hand, I'm ignoring the negation of maintenance cost.
		}
	}
	// K-Mod end
	return iValue;
}


void CvPlayerAI::AI_doCounter()  // advc: style changes
{	// <advc.003n>
	if(isBarbarian())
		return; // </advc.003n>

	CvGame& kGame = GC.getGame();
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
	CvLeaderHeadInfo& kPersonality = GC.getInfo(getPersonalityType());
	double decayFactor = 1 - GET_TEAM(getTeam()).AI_getDiploDecay(); // advc.130k
	// <advc.130k>
	for (PlayerIter<MAJOR_CIV,KNOWN_TO> it(getTeam()); it.hasNext(); ++it)
	{
		CvPlayerAI const& kPlayer = *it;
		PlayerTypes const ePlayer = kPlayer.getID();
		if (kPlayer.getTeam() == getTeam())
			continue;
		if (getStateReligion() != NO_RELIGION &&
			getStateReligion() == kPlayer.getStateReligion())
		{
			AI_changeSameReligionCounter(ePlayer,
					kOurTeam.AI_randomCounterChange());
		}
		else
		{
			AI_setSameReligionCounter(ePlayer, (int)
					(decayFactor * AI_getSameReligionCounter(ePlayer)));
		}
		if (getStateReligion() != NO_RELIGION &&
			kPlayer.getStateReligion() != NO_RELIGION &&
			getStateReligion() != kPlayer.getStateReligion() &&
			// Delay religion hate if just met
			kOurTeam.AI_getHasMetCounter(kPlayer.getTeam()) >
			// This is 10 (given the XML value in BtS)
			2 * -kPersonality.getDifferentReligionAttitudeDivisor())
		{
			AI_changeDifferentReligionCounter(ePlayer, kOurTeam.AI_randomCounterChange());
		}
		else AI_setDifferentReligionCounter(ePlayer, (int)(
				decayFactor * AI_getDifferentReligionCounter(ePlayer)));
		if (kPersonality.getFavoriteCivic() != NO_CIVIC)
		{
			CivicTypes eFavCivic = (CivicTypes)kPersonality.getFavoriteCivic();
			if(isCivic(eFavCivic) && kPlayer.isCivic(eFavCivic))
				AI_changeFavoriteCivicCounter(ePlayer, kOurTeam.AI_randomCounterChange());
			else AI_setFavoriteCivicCounter(ePlayer, (int)(
				decayFactor * AI_getFavoriteCivicCounter(ePlayer)));
		} // <advc.130p>
		AI_setPeacetimeGrantValue(ePlayer, (int)(
				decayFactor * AI_getPeacetimeGrantValue(ePlayer)));
		AI_setPeacetimeTradeValue(ePlayer, (int)(
				decayFactor * AI_getPeacetimeTradeValue(ePlayer)));
		// </advc.130p>
		// <advc.149>
		int iAttitudeDiv = kPersonality.getBonusTradeAttitudeDivisor();
		if(iAttitudeDiv <= 0)
			continue;
		//int iBonusImports = getNumTradeBonusImports(eCiv);
		// Same scale as the above, but taking into account utility.
		double bonusVal = AI_bonusImportValue(ePlayer);
		int iBonusTrade = AI_getBonusTradeCounter(ePlayer);
		if (bonusVal <= iBonusTrade / (1.2 * iAttitudeDiv))
		{
			/*  BtS decreases the BonusTradeCounter by 1 + civ.getNumCities() / 4,
				but let's just do exponential decay instead. */
			AI_setBonusTradeCounter(ePlayer, (int)(
					decayFactor * AI_getBonusTradeCounter(ePlayer)));
		}
		else
		{
			double incr = bonusVal;
			if (bonusVal > 0.01)
			{
				CvCity* pCapital = getCapitalCity();
				double capBonuses = 0;
				if (pCapital != NULL)
				{
					capBonuses = std::max(0.0,
							pCapital->countUniqueBonuses() - bonusVal);
				}
				double exportable = 0;
				FOR_EACH_ENUM(Bonus)
				{
					int iAvail = kPlayer.getNumAvailableBonuses(eLoopBonus)
							+ kPlayer.getBonusExport(eLoopBonus);
					if (iAvail > 1)
						exportable += std::min(iAvail - 1, 3);
				} /* Mean of capBonuses and a multiple of exportable, but
					 no more than 1.33 times capBonuses. */
				double weight1 = (capBonuses + std::min(capBonuses * (5/3.0),
						2.4 * std::max(bonusVal, exportable))) / 2;
				/*  Rather than changing attitudeDiv in XML for every leader,
					do the fine-tuning here. */
				double weight2 = 400.0 / iAttitudeDiv;
				if (weight1 >= weight2)
					incr = (bonusVal / weight1) * weight2;
			}
			AI_changeBonusTradeCounter(ePlayer, kOurTeam.AI_randomCounterChange(::round(
					1.25 * iAttitudeDiv * kPersonality.getBonusTradeAttitudeChangeLimit()),
					incr / 2)); // Halved b/c it's a binomial distrib w/ 2 trials
			// <advc.036>
			int iOldGoldTraded = AI_getGoldTradedTo(ePlayer);
			int iNewGoldTraded = (int)(decayFactor * iOldGoldTraded);
			AI_changeGoldTradedTo(ePlayer, iNewGoldTraded - iOldGoldTraded);
			// </advc.036>
		} // </advc.149> </advc.130k>
	}

	for (PlayerIter<MAJOR_CIV,KNOWN_TO> it(getTeam()); it.hasNext(); ++it) // advc.003n
	{
		FOR_EACH_ENUM(Contact)
		{
			if (AI_getContactTimer(it->getID(), eLoopContact) > 0)
				AI_changeContactTimer(it->getID(), eLoopContact, -1);
		}
	}

	int const iBaseWarAttitude = GC.getDefineINT(CvGlobals::AT_WAR_ATTITUDE_CHANGE); // advc.130g
	for (PlayerIter<MAJOR_CIV,KNOWN_TO> it(getTeam()); it.hasNext(); ++it) // advc.003n
	{
		CvPlayer const& kPlayer = *it;
		PlayerTypes const ePlayer = kPlayer.getID();
		FOR_EACH_ENUM2(Memory, eMem)
		{
			int c = AI_getMemoryCount(ePlayer, eMem);
			int iDecayRand = kPersonality.getMemoryDecayRand(eMem);
			if (c <= 0 || iDecayRand <= 0)
				continue;
			/*	<advc.130r>, advc.130j: This takes care of the scale factor (2 or 3)
				and leads to faster decay when multiple diplo actions are remembered. */
			double p = c / (double)iDecayRand;
			// Tech trade memory is exempt from the above change
			if (eMem == MEMORY_RECEIVED_TECH_FROM_ANY || eMem == MEMORY_TRADED_TECH_TO_US)
				p = 1 / (double)iDecayRand;
			// Moderate game speed adjustment
			p *= GC.getInfo(kGame.getGameSpeedType()).getGoldenAgePercent() / 100.0;
			// </advc.130r>
			// <advc.144> No decay of MADE_DEMAND_RECENT while peace treaty
			if (eMem == MEMORY_MADE_DEMAND_RECENT &&
				kOurTeam.isForcePeace(kPlayer.getTeam()))
			{
				continue;
			} // </advc.144>
			// <advc.130r>
			if (kOurTeam.isAtWar(kPlayer.getTeam()))
			{
				// No decay while war ongoing
				if(eMem == MEMORY_DECLARED_WAR)
					continue;
				// Limited decay while war ongoing
				if((eMem == MEMORY_CANCELLED_OPEN_BORDERS ||
					eMem == MEMORY_CANCELLED_DEFENSIVE_PACT ||
					eMem == MEMORY_CANCELLED_VASSAL_AGREEMENT) &&
					c <= 1)
				{
					continue;
				}
			}
			if(eMem == MEMORY_DECLARED_WAR_ON_FRIEND &&
				AI_atWarWithPartner(kPlayer.getTeam()))
			{
				continue;
			}
			if((eMem == MEMORY_STOPPED_TRADING || eMem == MEMORY_HIRED_TRADE_EMBARGO) &&
				AI_getMemoryCount(ePlayer, MEMORY_STOPPED_TRADING_RECENT) > 0)
			{
				continue;
			} // </advc.130r>
			// <advc.130o> Too lazy to reduce the MemoryDecay divisors for every leader in XML
			if (eMem == MEMORY_REJECTED_DEMAND || eMem == MEMORY_ACCEPT_DEMAND)
				p *= (100 / 60.0); // </advc.130o>  // <advc.144>
			if (eMem == MEMORY_GIVE_HELP)
				p *= (100 / 75.0); // </advc.144>  // <advc.553>
			if (eMem == MEMORY_TRADED_TECH_TO_US)
				p *= (100 / 25.0); // </advc.553>
			/*  <advc.145> Decay of accepted/denied civic/religion memory based on
				current civics and religion */
			// Fav. civic and religion are based on LeaderType, not PersonalityType.
			CivicTypes eFavCivic = (CivicTypes)GC.getInfo(getLeaderType()).getFavoriteCivic();
			double const abolishMultiplier = 4;
			if (eMem == MEMORY_ACCEPTED_CIVIC)
			{
				if (eFavCivic != NO_CIVIC &&
					(!kPlayer.isCivic(eFavCivic) || !isCivic(eFavCivic)))
				{
					p *= abolishMultiplier;
				}
			}
			if (eMem == MEMORY_ACCEPTED_RELIGION)
			{
				if(isStateReligion() && kPlayer.getStateReligion() != getStateReligion())
					p *= abolishMultiplier;
			}
			double const adoptMultiplier = 3.5;
			if (eMem == MEMORY_DENIED_CIVIC)
			{
				if(eFavCivic == NO_CIVIC || kPlayer.isCivic(eFavCivic) ||
					!isCivic(eFavCivic))
				{
					p *= adoptMultiplier;
				}
			}
			if (eMem == MEMORY_DENIED_RELIGION)
			{
				if(!isStateReligion() || kPlayer.getStateReligion() == getStateReligion())
					p *= adoptMultiplier;
			} // </advc.145>
			if (::bernoulliSuccess(std::min(p, 0.5), "Memory Decay")) // advc.130j
				AI_changeMemoryCount(ePlayer, eMem, -1);
		}
		// <advc.130g>
		int iRebuke = AI_getMemoryCount(ePlayer, MEMORY_REJECTED_DEMAND);
		if ((iRebuke > 0  && AI_getWarAttitude(ePlayer) < iBaseWarAttitude - 1 &&
			GET_TEAM(getTeam()).AI_isChosenWar(kPlayer.getTeam())) || isAVassal())
		{
			AI_changeMemoryCount(ePlayer, MEMORY_REJECTED_DEMAND, -iRebuke);
		} // </advc.130g>
	}
}

// <advc.149> Based on CvPlayer::getNumTradeBonusImports
double CvPlayerAI::AI_bonusImportValue(PlayerTypes eFrom) const
{
	double r = 0;
	FOR_EACH_DEAL(d)
	{
		if(!d->isBetween(getID(), eFrom))
			continue;
		bool bTribute = d->isVassalTributeDeal();
		/*if(bTribute)
			continue;*/ // Let's reduce the value instead
		for (CLLNode<TradeData> const* pNode = d->headGivesNode(eFrom); pNode != NULL;
			pNode = d->nextGivesNode(pNode, eFrom))
		{
			if (pNode->m_data.m_eItemType == TRADE_RESOURCES)
			{
				double bonusVal = GET_PLAYER(getID()).AI_bonusVal((BonusTypes)
						pNode->m_data.m_iData, -1);
				if(bTribute)
					bonusVal /= 2;
				r += bonusVal;
			}
		}
	} // The divisor should be the typical gpt value of a somewhat useful bonus
	return r / ::range(getNumCities(), 3, 10);
} // </advc.149>

void CvPlayerAI::AI_doMilitary()
{
	/*if (GET_TEAM(getTeam()).getAnyWarPlanCount(true) == 0) {
		while (AI_isFinancialTrouble() && (calculateUnitCost() > 0)) {
			if (!AI_disbandUnit(1, false))
				break;
		}
	}*/ // BtS
	// K-Mod
	PROFILE_FUNC();

	if (!isAnarchy() && AI_isFinancialTrouble() && GET_TEAM(getTeam()).AI_getWarSuccessRating() > -30)
	{
		int iCost = AI_unitCostPerMil();
		const int iMaxCost = AI_maxUnitCostPerMil();

		if (iCost > 0 && (iCost > iMaxCost || calculateGoldRate() < 0))
		{
			std::vector<std::pair<int, int> > unit_values; // <value, id>
			FOR_EACH_UNITAI(pLoopUnit, *this)
			{
				int iValue = AI_disbandValue(*pLoopUnit);
				if (iValue < MAX_INT)
					unit_values.push_back(std::make_pair(iValue, pLoopUnit->getID()));
			}
			std::sort(unit_values.begin(), unit_values.end());

			size_t iDisbandCount = 0;
			do
			{
				if (unit_values.size() <= iDisbandCount)
					break;

				CvUnit* pDisbandUnit = getUnit(unit_values[iDisbandCount].second);
				FAssert(pDisbandUnit);
				if (gUnitLogLevel > 2)
				{
					CvWString aiTypeString;
					getUnitAIString(aiTypeString, pDisbandUnit->AI_getUnitAIType());
					logBBAI("    %S scraps '%S' %S, at (%d, %d), with value %d, due to financial trouble.", getCivilizationDescription(0), aiTypeString.GetCString(), pDisbandUnit->getName(0).GetCString(), pDisbandUnit->getX(), pDisbandUnit->getY(), unit_values[iDisbandCount].first);
				}

				pDisbandUnit->scrap();
				pDisbandUnit->doDelayedDeath();
				iDisbandCount++;

				iCost = AI_unitCostPerMil();
			} while (iCost > 0 && (iCost > iMaxCost || calculateGoldRate() < 0) && AI_isFinancialTrouble()
					// <advc.110> Limit the number of units disbanded per turn
					&& ((int)iDisbandCount <= std::max(1, (int)getCurrentEra()) ||
					isStrike())); // </advc.110>
		}
	} // K-Mod end
	// <advc>
	CvGame& g = GC.getGame();
	CvLeaderHeadInfo const& lh = GC.getInfo(getPersonalityType());
	int const iAttackOddsChangeRand = lh.getAttackOddsChangeRand(); // </advc>
	AI_setAttackOddsChange(lh.getBaseAttackOddsChange() +
			g.getSorenRandNum(iAttackOddsChangeRand, "AI Attack Odds Change #1") +
			g.getSorenRandNum(iAttackOddsChangeRand, "AI Attack Odds Change #2")
			/ 2); // advc.114d
}


void CvPlayerAI::AI_doResearch()
{
	FAssertMsg(!isHuman(), "isHuman did not return false as expected");

	//if (getCurrentResearch() == NO_TECH)
	if (getCurrentResearch() == NO_TECH && isResearch() && !isAnarchy()) // K-Mod
	{
		AI_chooseResearch();
		//AI_forceUpdateStrategies(); //to account for current research.
	}
}

// This function has been heavily edited for K-Mod. (Most changes are unmarked. Lots of code has been rearranged / deleted.
void CvPlayerAI::AI_doCommerce()
{
	FAssertMsg(!isHuman(), "isHuman did not return false as expected");

	if (isBarbarian())
		return;

	const static int iCommerceIncrement = GC.getDefineINT("COMMERCE_PERCENT_CHANGE_INCREMENTS");

	int iGoldTarget = AI_goldTarget();
	/*  <advc.550f> Some extra gold for trade. Don't put this into AI_goldTarget
		for performance reasons, and b/c AI_maxGoldForTrade shouldn't take this
		extra budget into account. */
	if(!AI_isFinancialTrouble() && !AI_atVictoryStage4())
	{
		bool bTechTrading = GET_TEAM(getTeam()).isTechTrading();
		double partnerScore = 0;
		int iPartners = 0;
		for (PlayerIter<MAJOR_CIV,OTHER_KNOWN_TO> it(getTeam()); it.hasNext(); ++it)
		{
			CvPlayerAI const& kPartner = *it;
			// Don't stockpile gold for reparations; pay gpt instead.
			if(GET_TEAM(getTeam()).isAtWar(kPartner.getTeam()) ||
					GET_TEAM(kPartner.getTeam()).isVassal(getTeam()))
				continue;
			if(!bTechTrading && !GET_TEAM(kPartner.getTeam()).isTechTrading())
				continue;
			if(kPartner.getCurrentEra() < getCurrentEra() || (!kPartner.isHuman() &&
					kPartner.AI_getAttitude(getID()) <= GC.getInfo(kPartner.
					getPersonalityType()).getTechRefuseAttitudeThreshold()))
				continue;
			partnerScore += std::sqrt((double)kPartner.getTotalPopulation()) *
					kPartner.getCurrentEra() * 10;
			iPartners++;
		}
		if(iPartners > 0)
		{
			int iScore = ::round(partnerScore / std::sqrt((double)iPartners));
			iGoldTarget = std::max(iGoldTarget, iScore);
		}
	} // </advc.550f>
	int iTargetTurns = 4 * GC.getInfo(GC.getGame().getGameSpeedType()).getResearchPercent();
	iTargetTurns /= 100;
	iTargetTurns = std::max(3, iTargetTurns);
	// K-Mod. make it faster on the way down
	bool bFinancialTrouble = AI_isFinancialTrouble();
	if (!bFinancialTrouble && getGold() > iGoldTarget)
		iTargetTurns = (iTargetTurns + 1)/2;
	// K-Mod end

	bool bReset = false;

	if (isCommerceFlexible(COMMERCE_CULTURE))
	{
		if (getCommercePercent(COMMERCE_CULTURE) > 0)
		{
			setCommercePercent(COMMERCE_CULTURE, 0);
			bReset = true;
		}
	}

	if (isCommerceFlexible(COMMERCE_ESPIONAGE))
	{
		if (getCommercePercent(COMMERCE_ESPIONAGE) > 0)
		{
			setCommercePercent(COMMERCE_ESPIONAGE, 0);
			bReset = true;
		}
	}

	if (bReset)
		AI_assignWorkingPlots();

	const bool bFirstTech = AI_isFirstTech(getCurrentResearch());
	const bool bNoResearch = isNoResearchAvailable();

	if (isCommerceFlexible(COMMERCE_CULTURE))
	{
		if (AI_atVictoryStage(AI_VICTORY_CULTURE4))
		{
			setCommercePercent(COMMERCE_CULTURE, 100);
		}
		else if (getNumCities() > 0)
		{
			int iIdealPercent = 0;
			FOR_EACH_CITY(pLoopCity, *this)
			{
				if (pLoopCity->getCommerceHappinessPer(COMMERCE_CULTURE) > 0)
				{
					iIdealPercent += ((pLoopCity->angryPopulation() * 100) /
							pLoopCity->getCommerceHappinessPer(COMMERCE_CULTURE));
				}
			}

			iIdealPercent /= getNumCities();

			// K-Mod
			int iCap = 20;
			if (AI_atVictoryStage(AI_VICTORY_CULTURE2) && !bFirstTech)
			{
				iIdealPercent+=5;
				iCap += 10;
			}
			if (AI_atVictoryStage(AI_VICTORY_CULTURE3) && !bFirstTech)
			{
				iIdealPercent+=5;
				iCap += 20;
			}
			iIdealPercent += (AI_averageCulturePressure() - 100)/10;
			iIdealPercent = std::max(iIdealPercent, (AI_averageCulturePressure() - 100)/5);
			if (AI_avoidScience())
			{
				iCap += 30;
				iIdealPercent *= 2;
			}
			iIdealPercent -= iIdealPercent % iCommerceIncrement;
			iIdealPercent = std::min(iIdealPercent, iCap);
			// K-Mod end

			setCommercePercent(COMMERCE_CULTURE, iIdealPercent);
		}
	}

	if (isCommerceFlexible(COMMERCE_RESEARCH) && !bNoResearch)
	{
		setCommercePercent(COMMERCE_RESEARCH, 100-getCommercePercent(COMMERCE_CULTURE));
		if (!AI_avoidScience() && !AI_atVictoryStage(AI_VICTORY_CULTURE4))
		{
			// If we can afford to run full science for the rest of our current research,
			// then reduce our gold target so that we can burn through our gold.
			int iGoldRate = calculateGoldRate();
			if (iGoldRate < 0)
			{
				TechTypes eCurrentResearch = getCurrentResearch();
				if (eCurrentResearch != NO_TECH)
				{
					int iResearchTurnsLeft = getResearchTurnsLeft(eCurrentResearch, true);
					if(iResearchTurnsLeft >= 0 && // advc.004x
							getGold() >= iResearchTurnsLeft * -iGoldRate)
						iGoldTarget /= 5;
				}
			}
		}
	}

	// Note, in the original code has gold marked as inflexible, but was effectly adjusted by changing the other commerce percentages. This is no longer the case.
	if (isCommerceFlexible(COMMERCE_GOLD))
	{
		// note: as we increase the gold rate, research will be reduced before culture.
		// (The order defined in CommerceTypes is the order that the types will be decreased to accomdate changes.)
		bool bValid = true;
		while (bValid && getCommercePercent(COMMERCE_GOLD) < 100 &&
			getGold() + iTargetTurns * calculateGoldRate() < iGoldTarget)
		{
			bValid = changeCommercePercent(COMMERCE_GOLD, iCommerceIncrement);
		}
	}

	/*  K-Mod - evaluate potential espionage missions to determine espionage weights,
		even if espionage isn't flexible. (loosely based on bbai) */
	if (!GC.getGame().isOption(GAMEOPTION_NO_ESPIONAGE) &&
			GET_TEAM(getTeam()).getHasMetCivCount(true) > 0 &&
			(isCommerceFlexible(COMMERCE_ESPIONAGE) ||
			getCommerceRate(COMMERCE_ESPIONAGE) > 0))
	{
		int iEspionageTargetRate = 0;
		std::vector<int> aiTarget(MAX_CIV_TEAMS, 0);
		std::vector<int> aiWeight(MAX_CIV_TEAMS, 0);
		int iHighestTarget = 0;
		int iMinModifier = MAX_INT;
		int iApproxTechCost = 0;
		TeamTypes eMinModTeam = NO_TEAM;
		std::set<TeamTypes> potentialTechTargets; // advc.120b
		bool bFocusEspionage = AI_isDoStrategy(AI_STRATEGY_BIG_ESPIONAGE) &&
				!AI_isFocusWar(); // advc.105
				//kTeam.getAnyWarPlanCount(true) == 0;

		// For this part of the AI, I will assume there is only mission for each purpose.
		EspionageMissionTypes eSeeDemographicsMission = NO_ESPIONAGEMISSION;
		EspionageMissionTypes eSeeResearchMission = NO_ESPIONAGEMISSION;
		EspionageMissionTypes eStealTechMission = NO_ESPIONAGEMISSION;
		EspionageMissionTypes eCityRevoltMission = NO_ESPIONAGEMISSION;

		FOR_EACH_ENUM(EspionageMission)
		{
			CvEspionageMissionInfo& kMissionInfo = GC.getInfo(eLoopEspionageMission);

			if (kMissionInfo.isSeeDemographics())
				eSeeDemographicsMission = eLoopEspionageMission;

			if (kMissionInfo.isSeeResearch())
				eSeeResearchMission = eLoopEspionageMission;

			if (kMissionInfo.getBuyTechCostFactor() != 0)
				eStealTechMission = eLoopEspionageMission;

			if (kMissionInfo.getCityRevoltCounter() > 0)
				eCityRevoltMission = eLoopEspionageMission;
		}
		CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
		for (TeamIter<CIV_ALIVE,KNOWN_POTENTIAL_ENEMY_OF> itRival(getTeam());
			itRival.hasNext(); ++itRival)
		{
			CvTeam& kRival = *itRival;
			TeamTypes eRival = kRival.getID();
			int iTheirEspPoints = kRival.getEspionagePointsAgainstTeam(getTeam());
			int iOurEspPoints = kOurTeam.getEspionagePointsAgainstTeam(eRival);
			int iDesiredMissionPoints = 0;
			int iAttitude = range(kOurTeam.AI_getAttitudeVal(eRival), -12, 12);
			WarPlanTypes eWarPlan = kOurTeam.AI_getWarPlan(eRival);

			aiWeight[eRival] = 6;
			int iRateDivisor = iTargetTurns*3;

			if (eWarPlan != NO_WARPLAN)
			{
				int iMissionCost = std::max(
						getEspionageMissionCost(eSeeResearchMission, kRival.getLeaderID()),
						getEspionageMissionCost(eSeeDemographicsMission, kRival.getLeaderID()));
				if (eWarPlan != WARPLAN_DOGPILE && AI_isDoStrategy(AI_STRATEGY_BIG_ESPIONAGE) &&
					getCapitalCity() != NULL)
				{
					CvCity* pTargetCity = getCapitalCity()->getArea().AI_getTargetCity(getID());
					if (pTargetCity && pTargetCity->getTeam() == eRival &&
						pTargetCity->getArea().getAreaAIType(getTeam()) != AREAAI_DEFENSIVE &&
						pTargetCity->getArea().getNumAIUnits(getID(), UNITAI_SPY) > 0)
					{
						iMissionCost = std::max(iMissionCost, getEspionageMissionCost(
								eCityRevoltMission, pTargetCity->getOwner(), pTargetCity->plot()));
						if (eWarPlan == WARPLAN_TOTAL || eWarPlan == WARPLAN_PREPARING_TOTAL)
							iRateDivisor -= (iTargetTurns+2) / 4;
					}
					else
					{
						iMissionCost = std::max(iMissionCost,
								getEspionageMissionCost(eCityRevoltMission, kRival.getLeaderID()));
					}
				}

				iMissionCost *= 12;
				iMissionCost /= 10;

				iDesiredMissionPoints = std::max(iDesiredMissionPoints, iMissionCost);

				iRateDivisor -= iTargetTurns/2;
				aiWeight[eRival] += 6;
			}
			else
			{
				if (iAttitude <= -3)
				{
					int iMissionCost = std::max(
							getEspionageMissionCost(eSeeResearchMission, kRival.getLeaderID()),
							getEspionageMissionCost(eSeeDemographicsMission, kRival.getLeaderID()));
					iMissionCost *= 11;
					iMissionCost /= 10;
					iDesiredMissionPoints = std::max(iDesiredMissionPoints, iMissionCost);
				}
				else if (iAttitude < 3)
				{
					int iMissionCost = getEspionageMissionCost(
							eSeeDemographicsMission, kRival.getLeaderID());
					iMissionCost *= 11;
					iMissionCost /= 10;
					iDesiredMissionPoints = std::max(iDesiredMissionPoints, iMissionCost);
				}
			}
			// + or - 1 point, with the standard target turns.
			iRateDivisor += 4*range(iAttitude, -8, 8)/(5*iTargetTurns);
			iRateDivisor += AI_totalUnitAIs(UNITAI_SPY) == 0 ? 4 : 0;
			aiWeight[eRival] -= (iAttitude/3);
			if (kOurTeam.AI_hasCitiesInPrimaryArea(eRival))
			{
				aiWeight[eRival] *= 2;
				aiWeight[eRival] = std::max(0, aiWeight[eRival]);
				iRateDivisor -= iTargetTurns/2;
			}
			else iRateDivisor += iTargetTurns/2;

			// Individual player targeting
			if (bFocusEspionage &&
				// advc.120: Capitulated vassals should wait for gifts from the master
				(!kOurTeam.isCapitulated() || getMasterTeam() != kRival.getMasterTeam()))
			{
				for (MemberIter itMember(eRival); itMember.hasNext(); ++itMember)
				{
					CvPlayer& kRivalMember = *itMember;
					std::vector<int> cityModifiers;
					FOR_EACH_CITY(pLoopCity, kRivalMember)
					{
						if (pLoopCity->isRevealed(getTeam()) &&
							AI_isPrimaryArea(pLoopCity->getArea()))
						{
							cityModifiers.push_back(getEspionageMissionCostModifier(NO_ESPIONAGEMISSION,
									kRivalMember.getID(), pLoopCity->plot()));
						}
					}
					if (cityModifiers.empty())
						continue; // advc
					// Get the average of the lowest 3 cities.
					int iSampleSize = std::min(3, (int)cityModifiers.size());
					std::partial_sort(cityModifiers.begin(), cityModifiers.begin()+iSampleSize,
							cityModifiers.end());
					int iModifier = 0;
					for (std::vector<int>::iterator it = cityModifiers.begin();
						it != cityModifiers.begin()+iSampleSize; ++it)
					{
						iModifier += *it;
					}
					iModifier /= iSampleSize;

					// do they have any techs we can steal?
					bool bValid = false;
					FOR_EACH_ENUM(Tech)
					{
						if (canStealTech(kRivalMember.getID(), eLoopTech))
						{
							bValid = iApproxTechCost > 0; // don't set it true unless there are at least 2 stealable techs.
							// get a (very rough) approximation of how much it will cost to steal a tech.
							iApproxTechCost = (kOurTeam.getResearchCost(eLoopTech) +
									iApproxTechCost) / (iApproxTechCost != 0 ? 2 : 1);
							break;
						}
					}
					if (bValid)
					{	// advc.120b: This used to be checked before bValid
						if (iModifier < iMinModifier || (iModifier == iMinModifier &&
							iAttitude < kOurTeam.AI_getAttitudeVal(eMinModTeam)))
						{
							iMinModifier = iModifier;
							eMinModTeam = eRival;
						}
						potentialTechTargets.insert(eRival); // advc.120b
					}
				}
			}
			if (iTheirEspPoints > iDesiredMissionPoints &&
				kOurTeam.AI_getAttitude(eRival) <= ATTITUDE_CAUTIOUS)
			{
				iRateDivisor += 1;
				// adjust their points based on our current relationship
				iTheirEspPoints *= 34 + (eWarPlan == NO_WARPLAN ? -iAttitude : 12);
				iTheirEspPoints /= 36; // note. the scale here set by the range of iAttitude. [-12, 12]

				// scale by esp points ever, so that we don't fall over ourselves trying to catch up with
				// a civ that happens to be running an espionage economy.
				int iOurTotal = std::max(4, kOurTeam.getEspionagePointsEver());
				int iTheirTotal = std::max(4, kRival.getEspionagePointsEver());

				iTheirEspPoints *= (3*iOurTotal + iTheirTotal);
				iTheirEspPoints /= (2*iOurTotal + 4*iTheirTotal);
				// That's a factor between 3/2 and 1/4, centered at 4/6
				iDesiredMissionPoints = std::max(iTheirEspPoints, iDesiredMissionPoints);
			}
			aiTarget[eRival] = (iDesiredMissionPoints -
					iOurEspPoints)/std::max(3*iTargetTurns/2,iRateDivisor);
			iEspionageTargetRate += std::max(0, aiTarget[eRival]);
		}

		// now, the big question is whether or not we can steal techs more easilly than we can research them.
		bool bCheapTechSteal = false;
		if (eMinModTeam != NO_TEAM && !AI_avoidScience() && !bNoResearch &&
			!AI_atVictoryStage(AI_VICTORY_SPACE3))
		{
			if (eStealTechMission != NO_ESPIONAGEMISSION)
			{
				iMinModifier *= 100 - GC.getDefineINT(CvGlobals::MAX_FORTIFY_TURNS) *
						GC.getDefineINT("ESPIONAGE_EACH_TURN_UNIT_COST_DECREASE");
				iMinModifier /= 100;
				iMinModifier *= 100 + GC.getInfo(eStealTechMission).getBuyTechCostFactor();
				iMinModifier /= 100;
				// This is the espionage cost modifier for stealing techs.

				// lets say "cheap" means 75% of the research cost.
				bCheapTechSteal = (
						(AI_isDoStrategy(AI_STRATEGY_ESPIONAGE_ECONOMY) ? 8000 : 7500) *
						AI_averageCommerceMultiplier(COMMERCE_ESPIONAGE) /
						std::max(1, iMinModifier) >
						AI_averageCommerceMultiplier(COMMERCE_RESEARCH)*
						calculateResearchModifier(getCurrentResearch()));
				if (bCheapTechSteal)
				{
					aiTarget[eMinModTeam] += iApproxTechCost / 10;
					iEspionageTargetRate += iApproxTechCost / 10;
					// I'm just using iApproxTechCost to get a rough sense of scale.
					// cf. (iDesiredEspPoints - iOurEspPoints)/std::max(6,iRateDivisor);
				}
			}
		}

		for (TeamIter<EVER_ALIVE> it; it.hasNext(); ++it)
		{
			TeamTypes eTeam = it->getID();
			if (eTeam == BARBARIAN_TEAM)
				continue;
			// <advc.opt>
			if(!GET_TEAM(eTeam).isAlive())
			{
				aiWeight[eTeam] = 0;
				setEspionageSpendingWeightAgainstTeam(eTeam, aiWeight[eTeam]);
				continue;
			} // </advc.opt>
			if (aiTarget[eTeam] > 0)
			{
				//aiWeight[eTeam] += (150*aiTarget[eTeam])/std::max(4,iEspionageTargetRate);
				aiWeight[eTeam] += (100*aiTarget[eTeam])/std::max(4,iEspionageTargetRate);
			}
			else if (aiTarget[eTeam] < 0)
			{
				if (eTeam != eMinModTeam &&
						(!AI_isMaliciousEspionageTarget(GET_TEAM(eTeam).getLeaderID()) ||
						!kOurTeam.AI_hasCitiesInPrimaryArea(eTeam)))
					aiWeight[eTeam] = 0;
			}
			if (eTeam == eMinModTeam)
			{
				aiWeight[eTeam] = std::max(1, aiWeight[eTeam]);
				aiWeight[eTeam] *= 3;
				aiWeight[eTeam] /= 2;
			}
			else if (eMinModTeam != NO_TEAM && bCheapTechSteal)
			{
				aiWeight[eTeam] /= 2; // we want to focus hard on the best target
				// <advc.120b>
				if(AI_isDoStrategy(AI_STRATEGY_ESPIONAGE_ECONOMY) &&
						potentialTechTargets.count(eTeam) <= 0)
					aiWeight[eTeam] /= 2; // </advc.120b>
			}
			// note. bounds checks are done in the set-weight function
			setEspionageSpendingWeightAgainstTeam(eTeam, aiWeight[eTeam]);
			// one last ad-hoc correction to the target espionage rate...
			if (aiWeight[eTeam] <= 0 && aiTarget[eTeam] > 0)
				iEspionageTargetRate -= aiTarget[eTeam];
		}
		// K-Mod end

		// K-Mod. Only try to control espionage rate if we can also control research,
		// and if we don't need our commerce for more important things.
		if (isCommerceFlexible(COMMERCE_ESPIONAGE) && isCommerceFlexible(COMMERCE_RESEARCH)
			&& !bFinancialTrouble && !bFirstTech && !AI_atVictoryStage(AI_VICTORY_CULTURE4))
		{
			if (!bCheapTechSteal) // K-Mod
			{
				iEspionageTargetRate *= (140 - getCommercePercent(COMMERCE_GOLD) * 2); // was 110 -
				iEspionageTargetRate /= 140; // was 110
			}
			/*  advc.001: was getLeaderType. Makes a difference when playing with
				random AI personalities. */
			iEspionageTargetRate *= GC.getInfo(getPersonalityType()).getEspionageWeight();
			iEspionageTargetRate /= 100;

			int iCap = AI_isDoStrategy(AI_STRATEGY_BIG_ESPIONAGE) ? 20 : 10;
			iCap += bCheapTechSteal || AI_avoidScience() ? 50 : 0;

			FAssert(iCap <= 100);
			iCap = std::min(iCap, 100); // just in case.

			bool bValid = true;
			while (bValid && getCommerceRate(COMMERCE_ESPIONAGE) < iEspionageTargetRate &&
				getCommercePercent(COMMERCE_ESPIONAGE) < iCap)
			{
				changeCommercePercent(COMMERCE_RESEARCH, -iCommerceIncrement);
				bValid = changeCommercePercent(COMMERCE_ESPIONAGE, iCommerceIncrement);

				if (getGold() + iTargetTurns * calculateGoldRate() < iGoldTarget)
				{
					break;
				}

				if (!AI_avoidScience() && !bCheapTechSteal)
				{
					if (getCommercePercent(COMMERCE_RESEARCH) * 2 <=
						(getCommercePercent(COMMERCE_ESPIONAGE) + iCommerceIncrement) * 3)
					{
						break;
					}
				}
			}
		}
	}
	// K-Mod. prevent the AI from stockpiling excessive amounts of gold while in avoidScience.
	if (AI_avoidScience() && isCommerceFlexible(COMMERCE_GOLD))
	{
		while (getCommercePercent(COMMERCE_GOLD) > 0 &&
			getGold() + std::min(0, calculateGoldRate()) > iGoldTarget)
		{
			if (AI_atVictoryStage(AI_VICTORY_CULTURE3) && isCommerceFlexible(COMMERCE_CULTURE))
				changeCommercePercent(COMMERCE_CULTURE, iCommerceIncrement);
			else if (isCommerceFlexible(COMMERCE_ESPIONAGE))
				changeCommercePercent(COMMERCE_ESPIONAGE, iCommerceIncrement);
			else if (isCommerceFlexible(COMMERCE_RESEARCH) && !bNoResearch) // better than nothing...
				changeCommercePercent(COMMERCE_RESEARCH, iCommerceIncrement);
			else
				break;
		}
	}
	// K-Mod end

	if (!bFirstTech && isCommerceFlexible(COMMERCE_GOLD) && getGold() < iGoldTarget &&
		//getCommercePercent(COMMERCE_RESEARCH) > 40)
		getCommercePercent(COMMERCE_GOLD) < 50) // K-Mod
	{
		bool bHurryGold = false;
		for (int iHurry = 0; iHurry < GC.getNumHurryInfos(); iHurry++)
		{
			if ((GC.getInfo((HurryTypes)iHurry).getGoldPerProduction() > 0) &&
				canHurry((HurryTypes)iHurry))
			{
				bHurryGold = true;
				break;
			}
		}
		if (bHurryGold)
		{
			if (getCommercePercent(COMMERCE_ESPIONAGE) > 0 &&
				!AI_isDoStrategy(AI_STRATEGY_ESPIONAGE_ECONOMY) &&
				isCommerceFlexible(COMMERCE_ESPIONAGE))
			{
				changeCommercePercent(COMMERCE_ESPIONAGE, -iCommerceIncrement);
			}
			else if (isCommerceFlexible(COMMERCE_RESEARCH))
				changeCommercePercent(COMMERCE_RESEARCH, -iCommerceIncrement);
			// note. gold is automatically increased when other types are decreased.
		}
	}

	// this is called on doTurn, so make sure our gold is high enough keep us above zero gold.
	verifyGoldCommercePercent();
}

// K-Mod. I've rewritten most of this function, based on edits from BBAI. I don't know what's original bts code and what's not.
// (the BBAI implementation had some bugs)
void CvPlayerAI::AI_doCivics()
{
	FAssertMsg(!isHuman(), "isHuman did not return false as expected");

	if (isBarbarian())
		return;

	if (AI_getCivicTimer() > 0)
	{
		AI_changeCivicTimer(-1);
		if (getGoldenAgeTurns() != 1) // K-Mod. If its the last turn of a golden age, consider switching civics anyway.
		{
			return;
		}
	}

	if (!canRevolution(NULL))
		return;

	// FAssertMsg(AI_getCivicTimer() == 0, "AI Civic timer is expected to be 0"); // Disabled by K-Mod

	std::vector<CivicTypes> aeBestCivic(GC.getNumCivicOptionInfos());
	std::vector<int> aiCurrentValue(GC.getNumCivicOptionInfos());

	for (int iI = 0; iI < GC.getNumCivicOptionInfos(); iI++)
	{
		aeBestCivic[iI] = getCivics((CivicOptionTypes)iI);
		aiCurrentValue[iI] = AI_civicValue(aeBestCivic[iI]);
	}

	int iAnarchyLength = 0;
	bool bWillSwitch;
	bool bWantSwitch;
	bool bFirstPass = true;
	do
	{
		bWillSwitch = false;
		bWantSwitch = false;
		for (int iI = 0; iI < GC.getNumCivicOptionInfos(); iI++)
		{
			int iBestValue=-1;
			CivicTypes eNewCivic = AI_bestCivic((CivicOptionTypes)iI, &iBestValue);
			/*  advc.001r: Same thing as under karadoc's "temporary switch" comment
				in the loop below */
			CivicTypes eOtherCivic = aeBestCivic[iI];
			if(eOtherCivic == eNewCivic)
				continue; // advc
			aeBestCivic[iI] = eNewCivic; // advc.001r
			int iTestAnarchy = getCivicAnarchyLength(&aeBestCivic[0]);
			aeBestCivic[iI] = eOtherCivic; // advc.001r
			/*  using ~30 percent as a rough estimate of revolution cost, and
				a low threshold regardless of anarchy just for a bit of inertia.
				reduced threshold if we are already going to have a revolution. */
			int iThreshold = 5;
			if(iTestAnarchy > iAnarchyLength)
			{
				iThreshold = (!bFirstPass || bWantSwitch ? 20 : 30);
				// <advc.132b>
				TeamTypes eMaster = getMasterTeam();
				if(getTeam() != eMaster && GET_TEAM(eMaster).isHuman())
					iThreshold += 15;
				// </advc.132b>
			}
			if (100*iBestValue > (100 + /* advc.131: */ (iBestValue >= 0 ? 1 : -1) *
				iThreshold)*aiCurrentValue[iI])
			{
				FAssert(aeBestCivic[iI] != NO_CIVIC);
				if (gPlayerLogLevel > 0) logBBAI("    %S decides to switch to %S (value: %d vs %d%S)", getCivilizationDescription(0), GC.getInfo(eNewCivic).getDescription(0), iBestValue, aiCurrentValue[iI], bFirstPass?"" :", on recheck");
				iAnarchyLength = iTestAnarchy;
				aeBestCivic[iI] = eNewCivic;
				aiCurrentValue[iI] = iBestValue;
				bWillSwitch = true;
			}
			else
			{
				if (100 * iBestValue > /* advc.131: */ (iBestValue >= 0 ? 120 : 80)
						* aiCurrentValue[iI])
					bWantSwitch = true;
			}
		}
		bFirstPass = false;
	} while (bWillSwitch && bWantSwitch);
	// Recheck, just in case we can switch another good civic without adding more anarchy.


	// finally, if our current research would give us a new civic, consider waiting for that.
	if (iAnarchyLength > 0 && bWillSwitch)
	{
		TechTypes eResearch = getCurrentResearch();
		if (eResearch != NO_TECH)
		{	// <advc.004x>
			int iResearchTurns = getResearchTurnsLeft(eResearch, true);
			if(iResearchTurns >= 0 && // </advc.004x>
				iResearchTurns < 2*CIVIC_CHANGE_DELAY/3)
			{
				for (int iI = 0; iI < GC.getNumCivicInfos(); iI++)
				{
					const CvCivicInfo& kCivic = GC.getInfo((CivicTypes)iI);
					if(kCivic.getTechPrereq() != eResearch || canDoCivics((CivicTypes)iI))
						continue; // advc
					CivicTypes eOtherCivic = aeBestCivic[kCivic.getCivicOptionType()];
					// temporary switch just to test the anarchy length
					aeBestCivic[kCivic.getCivicOptionType()] = (CivicTypes)iI;
					if (getCivicAnarchyLength(&aeBestCivic[0]) <= iAnarchyLength)
					{
						// if the anarchy length would be the same, consider waiting for the new civic..
						int iValue = AI_civicValue((CivicTypes)iI);
						if (100 * iValue > (102+2*iResearchTurns) * aiCurrentValue[kCivic.getCivicOptionType()]
								&& iValue > 0) // advc.131: Better to be safe
						{
							if(gPlayerLogLevel > 0)
								logBBAI("    %S delays revolution to wait for %S (value: %d vs %d)", getCivilizationDescription(0), kCivic.getDescription(0), iValue, aiCurrentValue[kCivic.getCivicOptionType()]);
							AI_setCivicTimer(iResearchTurns*2/3);
							return;
						}
					}
					aeBestCivic[kCivic.getCivicOptionType()] = eOtherCivic;
				}
			}
		} // <advc.131>
		if(iAnarchyLength > 0)
		{
			if(getGold() < (iAnarchyLength + std::min(0, getStrikeTurns() - 1)) *
					-getGoldPerTurn())
				return;
		} // </advc.131>
	}

	if (canRevolution(&aeBestCivic[0]))
	{
		revolution(&aeBestCivic[0]);
		AI_setCivicTimer((getMaxAnarchyTurns() == 0) ? (GC.getDefineINT("MIN_REVOLUTION_TURNS") * 2) : CIVIC_CHANGE_DELAY);
	}
}

void CvPlayerAI::AI_doReligion()
{
	ReligionTypes eBestReligion;

	FAssertMsg(!isHuman(), "isHuman did not return false as expected");

	// BETTER_BTS_AI_MOD, Barbarian AI, efficiency, 07/20/09, jdog5000
	if (isBarbarian())
		return; // BETTER_BTS_AI_MOD: END

	if (AI_getReligionTimer() > 0)
	{
		AI_changeReligionTimer(-1);
		return;
	}

	if (!canChangeReligion())
	{
		return;
	}

	FAssertMsg(AI_getReligionTimer() == 0, "AI Religion timer is expected to be 0");

	// <advc.131>
	int const iAnarchyLength = getReligionAnarchyLength();
	if(isAnarchy() || iAnarchyLength > 1)
	{
		if(getGold() < (iAnarchyLength - 1) * -getGoldPerTurn())
			return;
	} // </advc.131>

	eBestReligion = AI_bestReligion();

	if (eBestReligion == NO_RELIGION)
		eBestReligion = getStateReligion();
	if (canConvert(eBestReligion))
	{	// <advc.131>
		double prConvert = 1;
		if(iAnarchyLength > 0 && eBestReligion != NO_RELIGION &&
				getStateReligion() != NO_RELIGION)
			prConvert = 1 - AI_religionValue(getStateReligion()) /
					(double)AI_religionValue(eBestReligion);
		/*  AI_religionValue fluctuates a lot; not sure why.
			Square pr to reduce switching. */
		prConvert *= prConvert;
		if(::bernoulliSuccess(prConvert, "advc.131")) // </advc.131>
		{
			if (gPlayerLogLevel > 0)
				logBBAI("    %S decides to convert to %S (value: %d vs %d)", getCivilizationDescription(0), GC.getInfo(eBestReligion).getDescription(0), eBestReligion == NO_RELIGION ? 0 : AI_religionValue(eBestReligion), getStateReligion() == NO_RELIGION ? 0 : AI_religionValue(getStateReligion()));
			convert(eBestReligion);
			AI_setReligionTimer((getMaxAnarchyTurns() == 0) ? (GC.getDefineINT("MIN_CONVERSION_TURNS") * 2) : RELIGION_CHANGE_DELAY);
		}
	}
}

/*  advc.133: Partly cut from AI_doDiplo. The caller ensures that the deal can
	be canceled; in particular, that it's a deal between this player and ePlayer. */
CvPlayerAI::CancelCode CvPlayerAI::AI_checkCancel(CvDeal const& d, PlayerTypes ePlayer)
{
	PROFILE_FUNC(); // advc.opt

	// K-Mod. getTradeDenial is not equipped to consider deal cancelation properly.
	if(!AI_considerOffer(ePlayer, d.getGivesList(ePlayer), d.getGivesList(getID()), -1, d.getAge()))
	{
		if (gDealCancelLogLevel > 0) logBBAICancel(d, getID(), L"trade value");
		return RENEGOTIATE;
	}
	if(d.getGivesList(getID()).getLength() <= 0)
		return NO_CANCEL;
	/*  @"not equipped"-comment above: Resource trades do need special treatment.
		For duals (Open Borders, Def. Pact, Perm. Alliance), getTradeDenial
		is all right. AI_considerOffer will not cancel these b/c the trade values
		of dual trades are always considered fair.
		Worst enmity is treated separately elsewhere. */
	CLLNode<TradeData> const* pNode = d.headGivesNode(getID());
	if(pNode != NULL && CvDeal::isDual(pNode->m_data.m_eItemType, true))
	{
		DenialTypes eDenial = getTradeDenial(ePlayer, pNode->m_data);
		if(eDenial != NO_DENIAL &&
				// <dlph.3> Cancel DP immediately when war no longer shared
				(((pNode->m_data.m_eItemType == TRADE_DEFENSIVE_PACT &&
				eDenial == DENIAL_JOKING)) || // </dlph.3>
				::bernoulliSuccess(0.2, "advc.133"))) {
			if (gDealCancelLogLevel > 1) logBBAICancel(d, getID(), L"dual denial");
			return DO_CANCEL;
		}
		else return NO_CANCEL;
	}
	/*  getTradeDenial will always return DENIAL_JOKING. Instead, call
		AI_bonusTrade explicitly and tell it that this is about cancellation. */
	for(pNode = d.headGivesNode(getID()); pNode != NULL; pNode = d.nextGivesNode(pNode, getID()))
	{
		TradeData data = pNode->m_data;
		if(data.m_eItemType != TRADE_RESOURCES)
			continue;
		if(AI_bonusTrade((BonusTypes)data.m_iData, ePlayer, 0) != NO_DENIAL)
		{
			if (gDealCancelLogLevel > 0) logBBAICancel(d, getID(), L"resource - denial");
			return DO_CANCEL;
		}
	}
	/*	Need to check their DENIAL_JOKING in case they're giving us a resource that
		we no longer need */
	for(pNode = d.headGivesNode(ePlayer); pNode != NULL; pNode = d.nextGivesNode(pNode, ePlayer))
	{
		TradeData data = pNode->m_data;
		if(data.m_eItemType != TRADE_RESOURCES)
			continue;
		if(GET_PLAYER(ePlayer).AI_bonusTrade((BonusTypes)data.m_iData, getID(), 0) == DENIAL_JOKING)
		{
			if (gDealCancelLogLevel > 0) logBBAICancel(d, getID(), L"resource - joking");
			return DO_CANCEL;
		}
	}
	return NO_CANCEL;
}

// advc: Body cut and pasted from AI_doDiplo. Returns true iff eOther contacted.
bool CvPlayerAI::AI_doDeals(PlayerTypes eOther)
{
	bool r = false;
	// <advc.133>
	CLinkList<TradeData> humanReceived;
	CLinkList<TradeData> humanGave;
	int iHumanReceivedGold = 0;
	int iHumanGaveGold = 0; // </advc.133>
	FOR_EACH_DEAL_VAR(pLoopDeal)
	{
		if(!pLoopDeal->isBetween(getID(), eOther) || // advc: Ensure this upfront
				!pLoopDeal->isCancelable(getID()))
			continue;
		// if ((g.getGameTurn() - pLoopDeal->getInitialGameTurn()) >= (GC.getPEACE_TREATY_LENGTH() * 2)) // K-Mod disabled
		// (original bts code deleted)
		/*  advc: Cancellation checks moved into a subfunction
			to reduce code duplication */
		// <advc.133>
		CancelCode eCancel = AI_checkCancel(*pLoopDeal, eOther);
		if(eCancel == NO_CANCEL)
			continue;
		bool bRenegotiate = (eCancel == RENEGOTIATE &&
				/*  Canceled AI-human deals bring up the trade table anyway
					(except when a trade becomes invalid); this is about AI-AI trades. */
				!isHuman() && !GET_PLAYER(eOther).isHuman() &&
				// The rest is covered by proposeResourceTrade
				canContact(eOther, true)); // </advc.133>
		if (GET_PLAYER(eOther).isHuman() && canContact(eOther, true))
		{
			bool bVassalDeal = pLoopDeal->isVassalDeal(); // K-Mod
			CLLNode<TradeData> const* pNode;
			for (pNode = pLoopDeal->headGivesNode(getID()); pNode != NULL;
					pNode = pLoopDeal->nextGivesNode(pNode, getID()))
			{
				if (pNode->m_data.m_eItemType == TRADE_GOLD_PER_TURN)
					iHumanReceivedGold += pNode->m_data.m_iData;
				else humanReceived.insertAtEnd(pNode->m_data);
				/*  <advc.074> Remember the canceled resources to avoid
					excluding them in CvPlayer::buildTradeTable. */
				if(pNode->m_data.m_eItemType == TRADE_RESOURCES)
				{
					m_cancelingExport.insertAtEnd(std::make_pair(
							eOther, (BonusTypes)pNode->m_data.m_iData));
				} // </advc.074>
			}
			for (pNode = pLoopDeal->headGivesNode(eOther); pNode != NULL;
					pNode = pLoopDeal->nextGivesNode(pNode, eOther))
			{
				if (pNode->m_data.m_eItemType == TRADE_GOLD_PER_TURN)
					iHumanGaveGold += pNode->m_data.m_iData;
				else humanGave.insertAtEnd(pNode->m_data);
				// <advc.074>
				if(pNode->m_data.m_eItemType == TRADE_RESOURCES)
				{
					GET_PLAYER(eOther).m_cancelingExport.insertAtEnd(std::make_pair(
							getID(), (BonusTypes)pNode->m_data.m_iData));
				} // </advc.074>
			}
			// <advc.062>
			DenialTypes eVassalCancelReason = (!bVassalDeal ? NO_DENIAL :
					GET_TEAM(getTeam()).AI_surrenderTrade(TEAMID(eOther)));
			// </advc.062>
			pLoopDeal->kill(); // K-Mod. Kill the old deal first.
			CvDiploParameters* pDiplo = new CvDiploParameters(getID());
			if (bVassalDeal)
			{	// <advc.062>
				CvString szReason = "AI_DIPLOCOMMENT_NO_VASSAL";
				switch(eVassalCancelReason)
				{
				case DENIAL_POWER_US: szReason += "_POWER_US"; break;
				case DENIAL_POWER_YOU: szReason += "_POWER_YOU"; break;
				case DENIAL_VICTORY: szReason += "_VICTORY"; break;
				case DENIAL_WORST_ENEMY: // fall through
				case DENIAL_ATTITUDE: szReason += "_ATTITUDE"; break;
				case DENIAL_TOO_FAR: szReason += "_TOO_FAR"; break;
				case DENIAL_POWER_YOUR_ENEMIES: szReason += "_POWER_YOUR_ENEMIES"; break;
				} // </advc.062>
				pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString(
						//"AI_DIPLOCOMMENT_NO_VASSAL"
						szReason.c_str())); // advc.062
				pDiplo->setAIContact(true);
				gDLL->beginDiplomacy(pDiplo, eOther);
			}
			// advc.133: Don't contact human yet
			//pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_CANCEL_DEAL"));
			// ...
			r = true;
		}
		// K-Mod.
		else
		{
			pLoopDeal->kill(); // K-Mod end
			// <advc.133>
			if(bRenegotiate)
				r = (AI_proposeResourceTrade(eOther) || r); // </advc.133>
		}
		//pLoopDeal->kill(); // XXX test this for AI...
		/*  K-Mod. I've rearranged stuff so that we can kill the deal
			before a diplomacy window. */
	}
	// <advc.133> One diplo popup for all canceled non-vassal deals
	if (humanReceived.getLength() + humanGave.getLength() + iHumanReceivedGold + iHumanGaveGold > 0)
	{
		if (iHumanReceivedGold > iHumanGaveGold)
		{
			humanReceived.insertAtEnd(TradeData(
					TRADE_GOLD_PER_TURN, iHumanReceivedGold - iHumanGaveGold));
		}
		else if (iHumanGaveGold > iHumanReceivedGold)
		{
			humanGave.insertAtEnd(TradeData(
					TRADE_GOLD_PER_TURN, iHumanGaveGold - iHumanReceivedGold));
		}
		CvDiploParameters* pDiplo = new CvDiploParameters(getID());
		pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_CANCEL_DEAL"));
		pDiplo->setAIContact(true);
		pDiplo->setOurOfferList(humanGave);
		pDiplo->setTheirOfferList(humanReceived);
		gDLL->beginDiplomacy(pDiplo, eOther);
	} // </advc.133>
	return r;
}

// <advc.026>
int CvPlayerAI::AI_maxGoldTradeGenerous(PlayerTypes eTo) const
{
	double multiplier = 1 + GC.getDefineINT(CvGlobals::AI_OFFER_EXTRA_GOLD_PERCENT) / 100.0;
	int r = ::round(AI_maxGoldTrade(eTo) * multiplier);
	r = std::min(getGold(), r);
	AI_roundTradeVal(r);
	return std::max(0, r);
}


int CvPlayerAI::AI_maxGoldPerTurnTradeGenerous(PlayerTypes eTo) const
{
	double r = AI_maxGoldPerTurnTrade(eTo);
	double multiplier = 1 + GC.getDefineINT(CvGlobals::AI_OFFER_EXTRA_GOLD_PERCENT) / 100.0;
	r *= multiplier;
	return std::max(0, ::round(r));
}


bool CvPlayerAI::AI_checkMaxGold(CLinkList<TradeData> const& items, PlayerTypes eTo) const
{
	int const iMaxGold = AI_maxGoldTrade(eTo);
	int const iMaxGPT = AI_maxGoldPerTurnTrade(eTo);
	for(CLLNode<TradeData> const* pNode = items.head(); pNode != NULL; pNode = items.next(pNode))
	{
		if(pNode->m_data.m_eItemType == TRADE_GOLD && pNode->m_data.m_iData > iMaxGold)
			return false;
		if(pNode->m_data.m_eItemType == TRADE_GOLD_PER_TURN && pNode->m_data.m_iData > iMaxGPT)
			return false;
	}
	return true;
} // </advc.026>


void CvPlayerAI::AI_doDiplo()  // advc: style changes
{
	PROFILE_FUNC();
	CvDiploParameters* pDiplo;
	CLinkList<TradeData> ourList;
	CLinkList<TradeData> theirList;

	FAssert(!isHuman());
	FAssert(!isMinorCiv());
	FAssert(!isBarbarian());

	if (GC.getPythonCaller()->AI_doDiplo(getID()))
		return;

	int iGoldValuePercent = AI_goldTradeValuePercent();

	CvGame& g = GC.getGame();
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
	// <advc.024>
	bool abContacted[MAX_TEAMS] = { false };
	int aiContacts[MAX_CIV_PLAYERS];
	for(int i = 0; i < MAX_CIV_PLAYERS; i++)
		aiContacts[i] = i;
	::shuffleArray(aiContacts, MAX_CIV_PLAYERS, g.getSorenRand());
	// </advc.024>
	for (int iPass = 0; iPass < 2; iPass++)
	{	/*  Loop counter renamed from iI b/c it's important for advc.024 that ePlayer
			is used everywhere instead of the loop counter. */
		for (int iContact = 0; iContact < MAX_CIV_PLAYERS; iContact++)
		{
			// advc: Some refactoring in the long body of this loop.
			PlayerTypes ePlayer = (PlayerTypes)aiContacts[iContact]; // advc.024
			CvPlayerAI const& kPlayer = GET_PLAYER(ePlayer);
			if(!kPlayer.isAlive() || kPlayer.isHuman() != (iPass == 1) || ePlayer == getID() ||
				kPlayer.getNumCities() <= 0) // advc.701
			{
				continue;
			}
			/*  <advc.706> Don't want player to receive diplo msgs on the
				first turn of a chapter. */
			if(kPlayer.isHuman() && g.isOption(GAMEOPTION_RISE_FALL) &&
				g.getRiseFall().isBlockPopups())
			{
				continue;
			}
			// </advc.706>
			//if (GET_PLAYER((PlayerTypes)iI).getTeam() != getTeam()) // disabled by K-Mod
			abContacted[ePlayer] = AI_doDeals(ePlayer); // advc
			if(!canContact(ePlayer, true))
				continue;
			if((kPlayer.getTeam() == getTeam() || GET_TEAM(ePlayer).isVassal(getTeam())) &&
				canPossiblyTradeItem(ePlayer, TRADE_RESOURCES)) // advc.opt
			{
				// XXX will it cancel this deal if it loses it's first resource???
				int iBestValue = 0;
				BonusTypes eBestGiveBonus = NO_BONUS;
				FOR_EACH_ENUM(Bonus)
				{
					if (getNumTradeableBonuses(eLoopBonus) > 1)
					{
						/*if ((GET_PLAYER((PlayerTypes)iI).AI_bonusTradeVal(eLoopBonus, getID(), 1) > 0) &&
							(GET_PLAYER((PlayerTypes)iI).AI_bonusVal(eLoopBonus, 1) > AI_bonusVal(eLoopBonus, -1))) */ // BtS
						// K-Mod
						bool bHasBonus = kPlayer.getNumAvailableBonuses(eLoopBonus) > 0;
						if (kPlayer.AI_bonusTradeVal(eLoopBonus, getID(), 1) > 0 &&
							2 * kPlayer.AI_bonusVal(eLoopBonus, 1) >
							(bHasBonus? 3 : 2) * AI_bonusVal(eLoopBonus, -1)) // K-mod end
						{
							if (canTradeItem(ePlayer, TradeData(TRADE_RESOURCES, eLoopBonus), true))
							{
								int iValue = (1 + g.getSorenRandNum(10000, "AI Bonus Trading #1"));
								if (iValue > iBestValue)
								{
									iBestValue = iValue;
									eBestGiveBonus = eLoopBonus;
								}
							}
						}
					}
				}

				if (eBestGiveBonus != NO_BONUS)
				{
					ourList.clear();
					theirList.clear();
					ourList.insertAtEnd(TradeData(TRADE_RESOURCES, eBestGiveBonus));
					if (kPlayer.isHuman())
					{
						if (!abContacted[kPlayer.getTeam()])
						{
							pDiplo = new CvDiploParameters(getID());
							pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_GIVE_HELP"));
							pDiplo->setAIContact(true);
							pDiplo->setTheirOfferList(ourList);
							gDLL->beginDiplomacy(pDiplo, ePlayer);
							abContacted[kPlayer.getTeam()] = true;
						}
					}
					else g.implementDeal(getID(), ePlayer, ourList, theirList);
				}
			}

			if (kPlayer.getTeam() != getTeam() && (GET_TEAM(ePlayer).isVassal(getTeam()) ||
				GET_TEAM(getTeam()).isVassal(kPlayer.getTeam())) && // advc.112
				canPossiblyTradeItem(ePlayer, TRADE_TECHNOLOGIES)) // advc.opt
			{
				int iBestValue = MIN_INT; // advc.112
				TechTypes eBestGiveTech = NO_TECH;
				// BETTER_BTS_AI_MOD, Diplomacy, 12/06/09, jdog5000: START
				// Don't give techs for free to advanced vassals ...
				//if (GET_PLAYER((PlayerTypes)iI).getTechScore()*10 < getTechScore()*9)
				/*  <advc.112> Give tech only by and by probabilistically, especially to
					peace vassals. The longer they stay with us, the more we trust them. */
				int iTheirTechScore = 10 * kPlayer.getTechScore();
				int iOurTechScore = getTechScore() *
						(kPlayer.isHuman() ? 7 : 9); // K-Mod
				double techScoreRatio = iTheirTechScore / (iOurTechScore + 0.01);
				double prGift = 1 - techScoreRatio;
				if(GET_TEAM(ePlayer).isCapitulated())
					prGift = std::max(prGift, 1/3.0);
				else if(isAVassal())
				{
					prGift *= 0.67 * (AI_getAttitude(ePlayer, false) -
							(GET_TEAM(getTeam()).isCapitulated() ? 1 : 2));
				}
				if(techScoreRatio <= 1 && ::bernoulliSuccess(prGift, "advc.112"))
				{	// </advc.112>
					FOR_EACH_ENUM(Tech)
					{
						if (kOurTeam.AI_techTrade(eLoopTech, kPlayer.getTeam()) == NO_DENIAL)
						{
							if (canTradeItem(ePlayer, TradeData(
								TRADE_TECHNOLOGIES, eLoopTech), true))
							{
								//iValue = (1 + g.getSorenRandNum(10000, "AI Vassal Tech gift"));
								// <advc.112> Replacing the above
								int iCost = GC.getInfo(eLoopTech).getResearchCost();
								int iValue = -g.getSorenRandNum(iCost, "AI Vassal Tech gift");
								// </advc.112>
								if (iValue > iBestValue)
								{
									iBestValue = iValue;
									eBestGiveTech = eLoopTech;
								}
							}
						}
					}
				}
				// BETTER_BTS_AI_MOD: END

				if (eBestGiveTech != NO_TECH)
				{
					ourList.clear();
					theirList.clear();
					ourList.insertAtEnd(TradeData(TRADE_TECHNOLOGIES, eBestGiveTech));

					if (kPlayer.isHuman())
					{
						if (!abContacted[kPlayer.getTeam()])
						{
							pDiplo = new CvDiploParameters(getID());
							pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_GIVE_HELP"));
							pDiplo->setAIContact(true);
							pDiplo->setTheirOfferList(ourList);
							gDLL->beginDiplomacy(pDiplo, ePlayer);
							abContacted[kPlayer.getTeam()] = true;
						}
					}
					else g.implementDeal(getID(), ePlayer, ourList, theirList);
				}
			}

			if(kPlayer.getTeam() == getTeam() || kOurTeam.isHuman() ||
				(!kPlayer.isHuman() && GET_TEAM(ePlayer).isHuman()) ||
				kOurTeam.isAtWar(kPlayer.getTeam()))
			{
				continue;
			}
			if (GET_TEAM(ePlayer).isVassal(getTeam()) &&
				kPlayer.canPossiblyTradeItem(getID(), TRADE_RESOURCES)) // advc.opt
			{
				int iBestValue = 0;
				BonusTypes eBestGiveBonus = NO_BONUS;
				FOR_EACH_ENUM(Bonus)
				{
					if (kPlayer.getNumTradeableBonuses(eLoopBonus) > 0 &&
						getNumAvailableBonuses(eLoopBonus) == 0 &&
						// <advc.001> Might have no trade network
						kPlayer.canTradeItem(getID(), TradeData(
						TRADE_RESOURCES, eLoopBonus), false)) // </advc.001>
					{
						int iValue = AI_bonusTradeVal(eLoopBonus, ePlayer, 1);
						if (iValue > iBestValue)
						{
							iBestValue = iValue;
							eBestGiveBonus = (eLoopBonus);
						}
					}
				}

				if (eBestGiveBonus != NO_BONUS)
				{
					theirList.clear();
					ourList.clear();
					theirList.insertAtEnd(TradeData(TRADE_RESOURCES, eBestGiveBonus));
					if (kPlayer.isHuman())
					{
						if (!abContacted[kPlayer.getTeam()])
						{
							CvPopupInfo* pInfo = new CvPopupInfo(BUTTONPOPUP_VASSAL_GRANT_TRIBUTE, getID(), eBestGiveBonus);
							if (pInfo)
							{
								gDLL->getInterfaceIFace()->addPopup(pInfo, ePlayer);
								abContacted[kPlayer.getTeam()] = true;
							}
						}
					}
					else g.implementDeal(getID(), ePlayer, ourList, theirList);
				}
			}

			if (AI_getAttitude(ePlayer) >= ATTITUDE_CAUTIOUS &&
				canPossiblyTradeItem(ePlayer, TRADE_CITIES)) // advc.opt
			{
				FOR_EACH_CITY(pLoopCity, *this)
				{
					if (pLoopCity->getPreviousOwner() == ePlayer)
						continue;

					if (((pLoopCity->getGameTurnAcquired() + 4) % 20) != (g.getGameTurn() % 20))
						continue;

					int iCount = 0;
					int iPossibleCount = 0;
					for (CityPlotIter itPlot(*pLoopCity); itPlot.hasNext(); ++itPlot)
					{
						if (itPlot->getOwner() == ePlayer)
							iCount++;
						iPossibleCount++;
					}

					if (iCount >= iPossibleCount / 2)
					{
						TradeData item(TRADE_CITIES, pLoopCity->getID());
						if (canTradeItem(ePlayer, item, true))
						{
							ourList.clear();
							theirList.clear();
							ourList.insertAtEnd(item);
							if (kPlayer.isHuman())
							{
								//if (!(abContacted[GET_PLAYER((PlayerTypes)iI).getTeam()])) {
								pDiplo = new CvDiploParameters(getID());
								pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_OFFER_CITY"));
								pDiplo->setAIContact(true);
								pDiplo->setTheirOfferList(ourList);
								gDLL->beginDiplomacy(pDiplo, ePlayer);
								abContacted[kPlayer.getTeam()] = true;
							}
							else g.implementDeal(getID(), ePlayer, ourList,
									theirList); // advc: was NULL
						}
					}
				}
			}

			if (kOurTeam.getLeaderID() == getID() &&
				!isAVassal()) // advc.opt: Avoid costly TradeDenial check (in canTradeItem)
			{
				if (AI_getContactTimer(ePlayer, CONTACT_PERMANENT_ALLIANCE) == 0)
				{
					bool bOffered = false;
					if (g.getSorenRandNum(GC.getInfo(getPersonalityType()).getContactRand(CONTACT_PERMANENT_ALLIANCE), "AI Diplo Alliance") == 0)
					{
						TradeData item(TRADE_PERMANENT_ALLIANCE);
						if (canTradeItem(ePlayer, item, true) &&
							kPlayer.canTradeItem(getID(), item, true))
						{
							ourList.clear();
							theirList.clear();
							ourList.insertAtEnd(item);
							theirList.insertAtEnd(item);
							bOffered = true;
							if (kPlayer.isHuman())
							{
								if (!abContacted[kPlayer.getTeam()])
								{
									AI_changeContactTimer(ePlayer, CONTACT_PERMANENT_ALLIANCE, GC.getInfo(getPersonalityType()).getContactDelay(CONTACT_PERMANENT_ALLIANCE));
									pDiplo = new CvDiploParameters(getID());
									pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_OFFER_DEAL"));
									pDiplo->setAIContact(true);
									pDiplo->setOurOfferList(theirList);
									pDiplo->setTheirOfferList(ourList);
									gDLL->beginDiplomacy(pDiplo, ePlayer);
									abContacted[kPlayer.getTeam()] = true;
								}
							}
							else
							{
								g.implementDeal(getID(), ePlayer, ourList, theirList);
								GC.getAgents().allianceFormed(); // advc.agent
								break; // move on to next player since we are on the same team now
							}
						}
					}

					if(bOffered)
						continue;
					// <advc.112>
					int iDiv = GC.getInfo(getPersonalityType()).
							getContactRand(CONTACT_PERMANENT_ALLIANCE); // 80
					double prVassal = 0;
					if(iDiv > 0)
					{
						prVassal = (8.0 / iDiv) * ::dRange(g.getPlayerRank(ePlayer) /
								(double)g.countCivPlayersAlive(), 0.25, 0.5);
						if(kOurTeam.getNumWars() > 0)
							prVassal *= 4;
					}
					if(::bernoulliSuccess(prVassal, "advc.112")) // </advc.112>
					{
						TradeData item(TRADE_VASSAL);
						if (canTradeItem(ePlayer, item, true))
						{
							ourList.clear();
							theirList.clear();
							ourList.insertAtEnd(item);
							if (kPlayer.isHuman())
							{
								if (!abContacted[kPlayer.getTeam()])
								{
									AI_changeContactTimer(ePlayer, CONTACT_PERMANENT_ALLIANCE, GC.getInfo(getPersonalityType()).getContactDelay(CONTACT_PERMANENT_ALLIANCE));
									pDiplo = new CvDiploParameters(getID());
									pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_OFFER_VASSAL"));
									pDiplo->setAIContact(true);
									pDiplo->setOurOfferList(theirList);
									pDiplo->setTheirOfferList(ourList);
									gDLL->beginDiplomacy(pDiplo, ePlayer);
									abContacted[kPlayer.getTeam()] = true;
								}
							}
							else
							{
								bool bAccepted = true;
								/*  advc.104o: The bAccepted condition below is
									already checked by canTradeItem I think.
									(Should perhaps skip it regardless of
									whether UWAI is enabled.) */
								if(!getUWAI.isEnabled())
								{
									TeamTypes eMasterTeam = kPlayer.getTeam();
									for (int i = 0; i < MAX_CIV_TEAMS; i++)
									{
										TeamTypes eWarTarget = (TeamTypes)i;
										if(!GET_TEAM(eWarTarget).isAlive() || eWarTarget == getTeam() ||
											eWarTarget == eMasterTeam || !::atWar(getTeam(), eWarTarget) ||
											::atWar(eMasterTeam, eWarTarget))
										{
											continue; // advc
										}
										if (GET_TEAM(eMasterTeam).AI_declareWarTrade(eWarTarget, getTeam(), false) != NO_DENIAL)
										{
											bAccepted = false;
											break;
										}
									}
								}
								if(bAccepted)
									g.implementDeal(getID(), ePlayer, ourList, theirList);
							}
						}
					} // advc.112
				}
			}

			if (kPlayer.isHuman() && kOurTeam.getLeaderID() == getID() &&
				!kOurTeam.isVassal(kPlayer.getTeam()))
			{
				if (!abContacted[kPlayer.getTeam()])
				{
					if (g.getSorenRandNum(GC.getInfo(getPersonalityType()).
						getContactRand(CONTACT_RELIGION_PRESSURE), "AI Diplo Religion Pressure") == 0)
					{
						abContacted[kPlayer.getTeam()] = AI_contactReligion(ePlayer);
					}
				}
				if (!abContacted[kPlayer.getTeam()])
				{
					if (g.getSorenRandNum(GC.getInfo(getPersonalityType()).
						getContactRand(CONTACT_CIVIC_PRESSURE), "AI Diplo Civic Pressure") == 0)
					{
						abContacted[kPlayer.getTeam()] = AI_contactCivics(ePlayer);
					}
				}
			}

			if (kPlayer.isHuman() && (kOurTeam.getLeaderID() == getID()))
			{
				if(!abContacted[kPlayer.getTeam()])
				{	/*  advc.130m: ContactRand check moved into proposeJointWar;
						now a bit more complicated. */
					abContacted[kPlayer.getTeam()] = AI_proposeJointWar(ePlayer);
				}
			}

			if (kPlayer.isHuman() && kOurTeam.getLeaderID() == getID() &&
				!kOurTeam.isVassal(kPlayer.getTeam()))
			{
				if (g.getSorenRandNum(GC.getInfo(getPersonalityType()).getContactRand(CONTACT_STOP_TRADING), "AI Diplo Stop Trading") == 0)
				{	// <advc> Moved into new function
					if(!abContacted[kPlayer.getTeam()])
						abContacted[kPlayer.getTeam()] = AI_proposeEmbargo(ePlayer);
					// </advc>
				}
			}
			// <advc.130z> Do this for non-humans too
			if (/*kPlayer.isHuman() &&*/ kOurTeam.getLeaderID() == getID() &&
				// Vassal-master handled above
				!GET_TEAM(ePlayer).isAVassal() && !isAVassal() &&
				kOurTeam.getID() != kPlayer.getTeam() &&
				!kPlayer.AI_atVictoryStage3() && // </advc.130z>
				canPossiblyTradeItem(ePlayer, TRADE_TECHNOLOGIES)) // advc.opt
			{
				if (GET_TEAM(ePlayer).getAssets() < kOurTeam.getAssets() / 2)
				{
					if (AI_getAttitude(ePlayer) > GC.getInfo(
						/*  advc.001: (Doesn't really matter b/c NoGiveHelpAttitudeThreshold
							is the same for all leaders.) */
						//GET_PLAYER((PlayerTypes)iI).
						getPersonalityType()).getNoGiveHelpAttitudeThreshold())
					{
						if (AI_getContactTimer(ePlayer, CONTACT_GIVE_HELP) == 0)
						{	// <advc.130z>
							int iRandMax = GC.getInfo(getPersonalityType()).
									getContactRand(CONTACT_GIVE_HELP);
							double div = 0.85;
							if(GC.getInfo(kPlayer.getHandicapType()).getDifficulty() < 30 ||
									AI_getAttitude(ePlayer) >= ATTITUDE_FRIENDLY)
								div = 1.25;
							else if(AI_atVictoryStage(AI_VICTORY_DIPLOMACY1))
							{
								div += 0.1;
								if(AI_atVictoryStage(AI_VICTORY_DIPLOMACY2))
								{
									div += 0.1;
									if(AI_atVictoryStage(AI_VICTORY_DIPLOMACY3))
									{
										div += 0.1;
										if(AI_atVictoryStage(AI_VICTORY_DIPLOMACY4))
											div += 0.1;
									}
								}
							}
							iRandMax = ::round(iRandMax / div);
							// </advc.130z>
							if (g.getSorenRandNum(iRandMax, "AI Diplo Give Help") == 0)
							{	// XXX maybe do gold instead???
								/*  advc.130z: The values are going to be negative.
									Btw the XXX above is a bad idea I think. */
								int iBestValue = MIN_INT;//0;
								TechTypes eBestGiveTech = NO_TECH;
								FOR_EACH_ENUM(Tech)
								{
									if (canTradeItem(ePlayer, TradeData(
										TRADE_TECHNOLOGIES, eLoopTech), true))
									{
										//iValue = (1 + g.getSorenRandNum(10000, "AI Giving Help"));
										// <advc.130z> Replacing the above
										int iCost = GC.getInfo(eLoopTech).getResearchCost();
										int iValue = -g.getSorenRandNum(iCost, "AI Giving Help");
										// </advc.130z>
										if (iValue > iBestValue)
										{
											iBestValue = iValue;
											eBestGiveTech = (eLoopTech);
										}
									}
								}

								if (eBestGiveTech != NO_TECH)
								{
									ourList.clear();
									ourList.insertAtEnd(TradeData(TRADE_TECHNOLOGIES, eBestGiveTech));
									if(kPlayer.isHuman()) // advc.130z
									{
										if (!abContacted[kPlayer.getTeam()])
										{
											AI_changeContactTimer(ePlayer, CONTACT_GIVE_HELP, GC.getInfo(getPersonalityType()).getContactDelay(CONTACT_GIVE_HELP));
											pDiplo = new CvDiploParameters(getID());
											pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_GIVE_HELP"));
											pDiplo->setAIContact(true);
											pDiplo->setTheirOfferList(ourList);
											gDLL->beginDiplomacy(pDiplo, ePlayer);
											abContacted[kPlayer.getTeam()] = true;
										} // <avdc.130z>
									}
									else g.implementDeal(getID(), ePlayer, ourList, theirList);
									// </advc.130z>
								}
							}
						}
					}
				}
			} /* advc.130z: Same condition as in BtS, but no longer applied to
				 to the part above. */
			if(kPlayer.isHuman() && kOurTeam.getLeaderID() == getID())
			{
				if (!abContacted[kPlayer.getTeam()] &&   // advc.130v:
					(!isAVassal() || kOurTeam.getMasterTeam() == kPlayer.getTeam()))
				{
					int iRand = GC.getInfo(getPersonalityType()).
							getContactRand(CONTACT_ASK_FOR_HELP);
					// BETTER_BTS_AI_MOD, Diplomacy, 02/12/10, jdog5000: START
					int iTechPerc = kOurTeam.getBestKnownTechScorePercent();
					if (iTechPerc < 90)
					{
						iRand *= std::max(1, iTechPerc - 60);
						iRand /= 30;
					} // BETTER_BTS_AI_MOD: END
					// <advc.104m> Reduce pr to 80% b/c UWAI also calls AI_askHelp
					if(getUWAI.isEnabled())
						iRand = (iRand * 5) / 4; // </advc.104m>
					if (g.getSorenRandNum(iRand, "AI Diplo Ask For Help") == 0)
						abContacted[kPlayer.getTeam()] = AI_askHelp(ePlayer);
				}
				/*if (kOurTeam.canDeclareWar(GET_PLAYER((PlayerTypes)iI).getTeam()) && !kOurTeam.AI_isSneakAttackPreparing(GET_PLAYER((PlayerTypes)iI).getTeam()))*/ // BtS
				// UNOFFICIAL_PATCH: when team was sneak attack ready but hadn't declared, could demand tribute
				// If other team accepted, it blocked war declaration for 10 turns but AI didn't react.
				if (kOurTeam.canDeclareWar(kPlayer.getTeam()) && !kOurTeam.AI_isChosenWar(kPlayer.getTeam()))
				{
					if (GET_TEAM(ePlayer).getDefensivePower() < kOurTeam.getPower(true) &&
						!AI_isFocusWar() && // advc.105
						!kOurTeam.isForcePeace(TEAMID(ePlayer)) && // advc.104m
						// <advc.104g>
						(!getUWAI.isEnabled() || uwai().getCache().
						numReachableCities(ePlayer) > 0)) // </advc.104g>
					{
						if (!abContacted[kPlayer.getTeam()])
						{	// Each type of tribute request uses the same probability
							FOR_EACH_ENUM(AIDemand)
							{
								if(g.getSorenRandNum(GC.getInfo(getPersonalityType()).
									getContactRand(CONTACT_DEMAND_TRIBUTE)
									// advc.104m: Probability halved b/c UWAI also calls AI_demandTribute
									* (getUWAI.isEnabled() ? 2 : 1),
									"AI Diplo Demand Tribute") == 0)
								{
									abContacted[kPlayer.getTeam()] = AI_demandTribute(ePlayer, eLoopAIDemand);
									if(abContacted[kPlayer.getTeam()])
										break;
								}
							}
						}
					}
				}
			}

			if(kOurTeam.getLeaderID() == getID())
			{
				if (AI_getContactTimer(ePlayer, CONTACT_OPEN_BORDERS) == 0 &&
					g.getSorenRandNum(GC.getInfo(getPersonalityType()).
					getContactRand(CONTACT_OPEN_BORDERS), "AI Diplo Open Borders") == 0)
				{
					TradeData item(TRADE_OPEN_BORDERS);
					if (canTradeItem(ePlayer, item, true) && kPlayer.canTradeItem(getID(), item, true))
					{
						ourList.clear();
						theirList.clear();
						ourList.insertAtEnd(item);
						theirList.insertAtEnd(item);
						if (kPlayer.isHuman() && !abContacted[kPlayer.getTeam()])
						{	// <advc.124>
							double speedAdjust = ::range(GC.getInfo(g.getGameSpeedType()).
									getResearchPercent(), 100, 200) / 100.0;
							// </advc.124>
							AI_changeContactTimer(ePlayer, CONTACT_OPEN_BORDERS,
									::round(speedAdjust * // advc.124
									GC.getInfo(getPersonalityType()).getContactDelay(CONTACT_OPEN_BORDERS)));
							pDiplo = new CvDiploParameters(getID());
							pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_OFFER_DEAL"));
							pDiplo->setAIContact(true);
							pDiplo->setOurOfferList(theirList);
							pDiplo->setTheirOfferList(ourList);
							gDLL->beginDiplomacy(pDiplo, ePlayer);
							abContacted[kPlayer.getTeam()] = true;
						}
						else g.implementDeal(getID(), ePlayer, ourList, theirList);
					}
				}

				if (AI_getContactTimer(ePlayer, CONTACT_DEFENSIVE_PACT) == 0)
				{	// <cdtw.5>
					int iRand = GC.getInfo(getPersonalityType()).getContactRand(CONTACT_DEFENSIVE_PACT);
					if(AI_isDoStrategy(AI_STRATEGY_ALERT2) && !AI_isFocusWar() &&
						!kPlayer.isHuman())
					{
						iRand = iRand / std::max(1, 10 - 2 * kOurTeam.getDefensivePactCount());
					}
					if(g.getSorenRandNum(iRand, "AI Diplo Defensive Pact") == 0)
					// Replacing this: // </cdtw.5>
					//if (g.getSorenRandNum(GC.getInfo(getPersonalityType()).getContactRand(CONTACT_DEFENSIVE_PACT), "AI Diplo Defensive Pact") == 0)
					{
						TradeData item(TRADE_DEFENSIVE_PACT);
						if (canTradeItem(ePlayer, item, true) &&
							kPlayer.canTradeItem(getID(), item, true))
						{
							ourList.clear();
							theirList.clear();
							ourList.insertAtEnd(item);
							theirList.insertAtEnd(item);
							if (kPlayer.isHuman())
							{
								if (!abContacted[kPlayer.getTeam()])
								{
									AI_changeContactTimer(ePlayer, CONTACT_DEFENSIVE_PACT, GC.getInfo(getPersonalityType()).getContactDelay(CONTACT_DEFENSIVE_PACT));
									pDiplo = new CvDiploParameters(getID());
									pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_OFFER_DEAL"));
									pDiplo->setAIContact(true);
									pDiplo->setOurOfferList(theirList);
									pDiplo->setTheirOfferList(ourList);
									gDLL->beginDiplomacy(pDiplo, ePlayer);
									abContacted[kPlayer.getTeam()] = true;
								}
							}
							else g.implementDeal(getID(), ePlayer, ourList, theirList);
						}
					}
				}
			}

			if (!kPlayer.isHuman() || kOurTeam.getLeaderID() == getID())
			{
				if (AI_getContactTimer(ePlayer, CONTACT_TRADE_TECH) == 0)
				{
					// BETTER_BTS_AI_MOD, Diplomacy, 04/24/10, jdog5000: START
					int iRand = GC.getInfo(getPersonalityType()).getContactRand(CONTACT_TRADE_TECH);
					int iTechPerc = kOurTeam.getBestKnownTechScorePercent();
					if (iTechPerc < 90)
					{	/* BBAI
						iRand *= std::max(1, iTechPerc - 60);
						iRand /= 30; */
						// K-Mod. not so extreme.
						//iRand = (iRand * (10 + iTechPerc) + 50)/100;
						// advc: Equivalent I think, but easier to read.
						iRand = ::round(iRand * (10 + iTechPerc) / 100.0);
						// K-Mod end
					}

					if (AI_atVictoryStage(AI_VICTORY_SPACE1))
					{
						//iRand /= 2;
						/*  advc.550a: That seems like a bit of an unfair advantage
							for civs pursuing a Space victory. */
						iRand = ::round(0.75 * iRand);
					}
					iRand = std::max(1, iRand);
					if (g.getSorenRandNum(iRand, "AI Diplo Trade Tech") == 0)
					{	// BETTER_BTS_AI_MOD: END
						TechTypes eBestReceiveTech = NO_TECH;
						/*  advc.550f: This is normally going to be
							getCurrentResearch, but it doesn't hurt to check if
							we have more progress on some other tech. */
						TechTypes eBestProgressTech = NO_TECH;
						if (kPlayer.canPossiblyTradeItem(getID(), TRADE_TECHNOLOGIES)) // advc.opt
						{
							int iBestValue = 0;
							int iBestProgress = 0; // advc.550f
							FOR_EACH_ENUM(Tech)
							{
								if (kPlayer.canTradeItem(getID(), TradeData(
									TRADE_TECHNOLOGIES, eLoopTech), true))
								{
									/*  advc.001: Value can be 0 if tech already almost researched
										and iI is human. Will then ask human to impart the tech for
										nothing in return, which is pointless and confusing. */
									if(kOurTeam.AI_techTradeVal(eLoopTech, kPlayer.getTeam()) == 0)
										continue;
									int iValue = (1 + g.getSorenRandNum(10000, "AI Tech Trading #1"));
									// <advc.550f>
									int iProgress = kOurTeam.getResearchProgress(eLoopTech);
									if(iProgress > iBestProgress &&
										!kPlayer.isHuman()) // This is only for inter-AI deals
									{
										iBestProgress = iProgress;
										eBestProgressTech = eLoopTech;
									} // </advc.550f>
									if (iValue > iBestValue)
									{
										iBestValue = iValue;
										eBestReceiveTech = eLoopTech;
									}
								}
							}
						}
						if (eBestReceiveTech != NO_TECH)
						{
							TechTypes eBestGiveTech = NO_TECH;
							// K-Mod
							int iOurValue = kOurTeam.AI_techTradeVal(eBestReceiveTech, kPlayer.getTeam());
							int iTheirValue = 0;
							// K-Mod end
							if (canPossiblyTradeItem(ePlayer, TRADE_TECHNOLOGIES))
							{	// int iBestValue=0;
								int iBestDelta = iOurValue; // K-Mod
								FOR_EACH_ENUM(Tech)
								{
									if (canTradeItem(ePlayer, TradeData(
										TRADE_TECHNOLOGIES, eLoopTech), true))
									{	/*iValue = (1 + g.getSorenRandNum(10000, "AI Tech Trading #2"));
										if (iValue > iBestValue) {
											iBestValue = iValue;
											eBestGiveTech = eLoopTech;
										} */ // BtS
										// K-Mod
										int iValue = GET_TEAM(ePlayer).AI_techTradeVal(eLoopTech, getTeam());
										int iDelta = std::abs(iOurValue - ((90 + g.getSorenRandNum(
												21, "AI Tech Trading #2")) * iValue) / 100);
										if (iDelta < iBestDelta) // aim to trade values as close as possible
										{
											iBestDelta = iDelta;
											iTheirValue = iValue;
											eBestGiveTech = eLoopTech;
										} // K-Mod end
									}
								}
							}

							// (original bts code deleted)

							ourList.clear();
							theirList.clear();
							if (eBestGiveTech != NO_TECH)
								ourList.insertAtEnd(TradeData(TRADE_TECHNOLOGIES, eBestGiveTech));
							theirList.insertAtEnd(TradeData(TRADE_TECHNOLOGIES, eBestReceiveTech));

							/*if (iGiveGold != 0) {
								setTradeItem(&item, TRADE_GOLD, iGiveGold);
								ourList.insertAtEnd(item);
							} if (iReceiveGold != 0) {
								setTradeItem(&item, TRADE_GOLD, iReceiveGold);
								theirList.insertAtEnd(item);
							}*/ // BtS
							// K-Mod
							/* bool bDeal = GET_PLAYER((PlayerTypes)iI).isHuman()
								? 6*iOurValue > 5*iTheirValue && 3*iTheirValue > 2*iOurValue
								: 3*iOurValue > 2*iTheirValue && 3*iTheirValue > 2*iOurValue; */
							bool bDeal = 100 * iOurValue >= AI_tradeAcceptabilityThreshold(ePlayer) * iTheirValue &&
									100 * iTheirValue >= kPlayer.AI_tradeAcceptabilityThreshold(getID()) * iOurValue;
							if (!bDeal
								// <advc.550b> Unlikely to lead to an attractive offer
								&& (!kPlayer.isHuman() ||
								0.5 < iTheirValue / (double)iOurValue ||
								::bernoulliSuccess(0.36, "advc.550b")))
								// </advc.550b>
							{	// advc.opt: Pushed this down into CvPlayer::buildTradeTable and CvPlayerAI::AI_counterPropose
								//PROFILE("AI_doDiplo self-counter-propose");
								// let the other team counter-propose (even if the other team is a human player)
								/*  Note: AI_counterPropose tends to favour the caller, which in this case is the other player.
									ie. we asked for the trade, but they set the final terms. (unless the original deal is accepted) */
								// advc: K-Mod code moved into new function
								if(kPlayer.AI_counterPropose(getID(), ourList, theirList, true, true))
								{
									bDeal = true;
									// K-Mod note: the following log call will miss the second name if it is a non-standard name (for example, a human player name).
									// This is a problem in the way the getName function is implimented. Unfortunately, the heart of the problem is in the signature of
									// some of the dllexport functions - and so it can't easily be fixed.
									if (gTeamLogLevel >= 2) logBBAI("    %S makes a deal with %S using AI_counterPropose. (%d : %d)", getName(0), kPlayer.getName(0), ourList.getLength(), theirList.getLength());
								}
							}
							if (bDeal)
							{
							// K-Mod end
								if (kPlayer.isHuman())
								{
									if (!abContacted[kPlayer.getTeam()])
									{
										AI_changeContactTimer(ePlayer, CONTACT_TRADE_TECH, GC.getInfo(getPersonalityType()).getContactDelay(CONTACT_TRADE_TECH));
										pDiplo = new CvDiploParameters(getID());
										pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_OFFER_DEAL"));
										pDiplo->setAIContact(true);
										pDiplo->setOurOfferList(theirList);
										pDiplo->setTheirOfferList(ourList);
										gDLL->beginDiplomacy(pDiplo, ePlayer);
										abContacted[kPlayer.getTeam()] = true;
									}
								}
								else
								{
									g.implementDeal(getID(), ePlayer, ourList, theirList);
									// advc.550f: I.e. we're done trading tech
									eBestProgressTech = NO_TECH;
								}
							}
						}
						// <advc.550f>
						if(eBestProgressTech != NO_TECH && eBestProgressTech != eBestReceiveTech)
						{
							int iOurValue = kOurTeam.AI_techTradeVal(eBestProgressTech,
									kPlayer.getTeam());
							int iPrice = (100 * iOurValue) / kPlayer.AI_goldTradeValuePercent();
							if(iPrice <= AI_maxGoldTrade(kPlayer.getID()))
							{
								TradeData item(TRADE_GOLD, iPrice);
								if(canTradeItem(kPlayer.getID(), item))
								{
									ourList.clear();
									ourList.insertAtEnd(item);
									theirList.clear();
									setTradeItem(&item, TRADE_TECHNOLOGIES, eBestProgressTech);
									theirList.insertAtEnd(item);
									g.implementDeal(getID(), ePlayer, ourList, theirList);
								}
							}
						} // </advc.550f>
					}
				}
			}
			int iContactRand = GC.getInfo(getPersonalityType()).
					getContactRand(CONTACT_TRADE_BONUS);
			// <advc.036> Make AI to human offers a bit more frequent
			if (iContactRand >= 8 && kPlayer.isHuman())
				iContactRand = (iContactRand * 4) / 5; // </advc.036>
			/*  <advc.133> Moved the cheap abContacted check up, and the rest
				of the resource trade code into a new function. */
			if((!abContacted[kPlayer.getTeam()] || !kPlayer.isHuman()) &&
				g.getSorenRandNum(iContactRand, "AI Diplo Trade Bonus") == 0)
			{
				abContacted[kPlayer.getTeam()] = AI_proposeResourceTrade(ePlayer);
			} // </advc.133>
			if (AI_getContactTimer(ePlayer, CONTACT_TRADE_MAP) == 0)
			{
				if (g.getSorenRandNum(GC.getInfo(getPersonalityType()).getContactRand(CONTACT_TRADE_MAP), "AI Diplo Trade Map") == 0)
				{
					TradeData item(TRADE_MAPS);
					if (kPlayer.canTradeItem(getID(), item, true) && canTradeItem(ePlayer, item, true))
					{	// <advc.136b>
						int iValForThem = GET_TEAM(ePlayer).AI_mapTradeVal(getTeam());
						int iThresh = GC.getDefineINT(CvGlobals::DIPLOMACY_VALUE_REMAINDER);
						if (!kPlayer.isHuman() ||
							(kOurTeam.AI_mapTradeVal(kPlayer.getTeam()) >= iValForThem &&
							iValForThem > iThresh)) // <advc.136b>
						{
							ourList.clear();
							theirList.clear();
							setTradeItem(&item, TRADE_MAPS);
							ourList.insertAtEnd(item);
							setTradeItem(&item, TRADE_MAPS);
							theirList.insertAtEnd(item);

							if (kPlayer.isHuman())
							{
								if (!abContacted[kPlayer.getTeam()])
								{
									AI_changeContactTimer(ePlayer, CONTACT_TRADE_MAP, GC.getInfo(getPersonalityType()).getContactDelay(CONTACT_TRADE_MAP));
									pDiplo = new CvDiploParameters(getID());
									pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString("AI_DIPLOCOMMENT_OFFER_DEAL"));
									pDiplo->setAIContact(true);
									pDiplo->setOurOfferList(theirList);
									pDiplo->setTheirOfferList(ourList);
									gDLL->beginDiplomacy(pDiplo, ePlayer);
									abContacted[kPlayer.getTeam()] = true;
								}
							}
							else g.implementDeal(getID(), ePlayer, ourList, theirList);
						}
					}
				}
			}

			/*  advc.001: The only trade left is WarTrade. The AI doesn't offer
				war trades to humans. The BtS code kills such trades through
				canTradeItem, so it's not a real issue; just pointless. */
			if(kPlayer.isHuman())// && abContacted[kPlayer.getTeam()])
				continue;
			/*  <advc.130v> Capitulated vassals certainly shouldn't hire
				war allies. Peace vassals would be OK if it weren't for
				advc.146 which implements a peace treaty between the war allies;
				can't have a vassal force its master into a peace treaty. */
			if(isAVassal())
				continue; // </advc.130v>
			int iMinAtWarCounter = MAX_INT;
			for (int i = 0; i < MAX_CIV_TEAMS; i++)
			{
				TeamTypes eEnemy = (TeamTypes)i;
				if (!GET_TEAM(eEnemy).isAlive() || !::atWar(eEnemy, getTeam()))
					continue;
				int iAtWarCounter = kOurTeam.AI_getAtWarCounter(eEnemy);
				if (kOurTeam.AI_getWarPlan(eEnemy) == WARPLAN_DOGPILE)
				{
					iAtWarCounter *= 2;
					iAtWarCounter += 5;
				}
				iMinAtWarCounter = std::min(iAtWarCounter, iMinAtWarCounter);
			}

			int iDeclareWarTradeRand = GC.getInfo(getPersonalityType()).getDeclareWarTradeRand();
			/*if (iMinAtWarCounter < 10) {
				iDeclareWarTradeRand *= iMinAtWarCounter;
				iDeclareWarTradeRand /= 10;
				iDeclareWarTradeRand ++;
			} if (iMinAtWarCounter < 4) {
				iDeclareWarTradeRand /= 4;
				iDeclareWarTradeRand ++;
			}*/
			// <advc.161> Replacing the above
			if(iMinAtWarCounter == MAX_INT)
				continue;
			iDeclareWarTradeRand = iDeclareWarTradeRand / 10 +
					std::min(10, iMinAtWarCounter);
			double prOffer = (iDeclareWarTradeRand <= 0 ? 1.0 :
					1.0 / iDeclareWarTradeRand);
			// BtS probability test:
			//if(g.getSorenRandNum(iDeclareWarTradeRand, "AI Diplo Declare War Trade") != 0)
			if(!::bernoulliSuccess(prOffer, "advc.161"))
				continue; // </advc.161>
			// <advc.104o>
			int iBestTargetValue = 0;
			int iBestTeamPrice = -1;
			// </advc.104o>
			TeamTypes eBestTeam = NO_TEAM;
			{ // advc: scope for iBestValue
				int iBestValue = 0;
				for (TeamIter<MAJOR_CIV,ENEMY_OF> itTarget(getTeam());
					itTarget.hasNext(); ++itTarget)
				{
					CvTeamAI const& kTarget = *itTarget;
					if(kTarget.isAtWar(kPlayer.getTeam()))
						continue;
					// <advc.104o>
					if(getUWAI.isEnabled())
					{
						/*  AI_declareWarTrade checks attitude toward the target;
							no war if the target is liked. Shouldn't matter for
							capitulated vassals, but an unpopular voluntary vassal
							should be a valid target even if the master would not be.
							Attitude toward the master still factors into
							tradeValJointWar. */
						if(kTarget.isCapitulated())
							continue;
						if(!kPlayer.canTradeItem(getID(), TradeData(
							TRADE_WAR, kTarget.getID())))
						{
							continue;
						}
						/*  Test denial separately (not through canTradeItem) b/c
							the denial test is costlier and produces log output */
						if(GET_TEAM(ePlayer).AI_declareWarTrade(
							kTarget.getID(), getTeam()) != NO_DENIAL)
						{
							continue;
						}
						int iValue = kOurTeam.uwai().tradeValJointWar(
								kTarget.getID(), kPlayer.getTeam());
						if(iValue <= iBestTargetValue)
							continue;
						/*  This call doesn't compute how much our team values the DoW
							by civ (that's iValue), it computes how much civ needs to be
							payed for the DoW. Has to work this way b/c AI_declareWarTradeVal
							is also used for human-AI war trades. */
						int iTheirPrice = kOurTeam.AI_declareWarTradeVal(
								kTarget.getID(), kPlayer.getTeam());
						/*  Don't try to make the trade if the DoW by ePlayer isn't
							nearly as valuable to us as what they'll charge */
						if(4 * iValue >= 3 * iTheirPrice)
						{
							iBestTargetValue = iValue;
							eBestTeam = kTarget.getID();
							iBestTeamPrice = iTheirPrice;
						}
					}
					else // </advc.104o>
					if (kTarget.getNumWars() < std::max(2, g.countCivTeamsAlive() / 2))
					{
						if (kPlayer.canTradeItem(getID(), TradeData(
							TRADE_WAR, kTarget.getID()), true))
						{
							int iValue = (1 + g.getSorenRandNum(1000, "AI Declare War Trading"));
							iValue *= (101 + kTarget.AI_getAttitudeWeight(getTeam()));
							iValue /= 100;
							if (iValue > iBestValue)
							{
								iBestValue = iValue;
								eBestTeam = kTarget.getID();
							}
						}
					}
				}
			}
			if (eBestTeam != NO_TEAM)
			{
				std::pair<TechTypes,TechTypes> eeBestGiveTech(NO_TECH, NO_TECH); // advc
				if (canPossiblyTradeItem(ePlayer, TRADE_TECHNOLOGIES)) // advc.opt
				{
					int iBestValue = 0;
					FOR_EACH_ENUM(Tech)
					{
						if (canTradeItem(ePlayer, TradeData(
							TRADE_TECHNOLOGIES, eLoopTech), true))
						{
							int iValue = (1 + g.getSorenRandNum(100, "AI Tech Trading #2"));
							iValue *= GET_TEAM(eBestTeam).getResearchLeft(eLoopTech);
							if (iValue > iBestValue)
							{
								iBestValue = iValue;
								eeBestGiveTech.first = eLoopTech;
							}
						}
					}
				}
				int iOurValue = iBestTeamPrice;
				int iTheirValue = 0;
				if(eeBestGiveTech.first != NO_TECH)
				{
					iTheirValue = GET_TEAM(ePlayer).AI_techTradeVal(
							eeBestGiveTech.first, getTeam());
				}
				/*	advc.001: Was iBestValue2 and had gotten mixed up with iBestValue
					from above (now in a separate scope). */
				int iBestValue = 0;
				if (iTheirValue < iOurValue && eeBestGiveTech.first != NO_TECH)
				{
					FOR_EACH_ENUM(Tech)
					{
						if(eLoopTech == eeBestGiveTech.first)
							continue;
						if (canTradeItem(ePlayer, TradeData(
							TRADE_TECHNOLOGIES, eLoopTech), true))
						{
							int iValue = (1 + g.getSorenRandNum(100, "AI Tech Trading #2"));
							iValue *= GET_TEAM(eBestTeam).getResearchLeft(eLoopTech);
							if (iValue > iBestValue)
							{
								iBestValue = iValue;
								eeBestGiveTech.second = eLoopTech;
							}
						}
					}
					if(eeBestGiveTech.second != NO_TECH)
					{
						iTheirValue += GET_TEAM(ePlayer).AI_techTradeVal(
								eeBestGiveTech.second, getTeam());
					}
				}

				int iReceiveGold = 0;
				int iGiveGold = 0;
				if (iTheirValue > iOurValue &&
					/*  advc.104o: Don't worry about giving them more if it's
						still worth it for us. (No effect if UWAI is disabled
						b/c then iBestTargetValue remains 0.) */
					iTheirValue > iBestTargetValue)
				{
					int iGold = std::min(((iTheirValue - iOurValue) * 100) /
							iGoldValuePercent, kPlayer.AI_maxGoldTrade(getID()));
					if (iGold > 0)
					{
						if (kPlayer.canTradeItem(getID(), TradeData(
							TRADE_GOLD, iGold), true))
						{
							iReceiveGold = iGold;
							iOurValue += (iGold * iGoldValuePercent) / 100;
						}
					}
				}
				else if (iOurValue > iTheirValue)
				{
					int iGold = std::min(((iOurValue - iTheirValue) * 100) /
							iGoldValuePercent, AI_maxGoldTrade(ePlayer));
					if (iGold > 0)
					{
						if (canTradeItem(ePlayer, TradeData(
							TRADE_GOLD, iGold), true))
						{
							iGiveGold = iGold;
							iTheirValue += (iGold * iGoldValuePercent) / 100;
						}
					}
				} //if (iTheirValue > (iOurValue * 3 / 4))
				if (4 * iTheirValue > 3 * iOurValue) // advc
				{
					ourList.clear();
					theirList.clear();
					if (eeBestGiveTech.first != NO_TECH)
					{
						ourList.insertAtEnd(TradeData(
							TRADE_TECHNOLOGIES, eeBestGiveTech.first));
					}
					if (eeBestGiveTech.second != NO_TECH)
					{
						ourList.insertAtEnd(TradeData(
							TRADE_TECHNOLOGIES, eeBestGiveTech.second));
					}
					theirList.insertAtEnd(TradeData(TRADE_WAR, eBestTeam));
					if (iGiveGold != 0)
						ourList.insertAtEnd(TradeData(TRADE_GOLD, iGiveGold));
					if (iReceiveGold != 0)
						theirList.insertAtEnd(TradeData(TRADE_GOLD, iReceiveGold));
					g.implementDeal(getID(), ePlayer, ourList, theirList);
				}
			}
		}
	}
}

// advc: This functions and the next few contain code cut from AI_doDiplo
/*  Caller ensures that eHuman isn't us, not on our team, not a vassal
	and can be contacted. We're alive, not human, not a vassal, team leader. */
bool CvPlayerAI::AI_proposeJointWar(PlayerTypes eHuman)
{
	if(AI_getContactTimer(eHuman, CONTACT_JOIN_WAR) > 0)
		return false;
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
	if(kOurTeam.getNumWars(true, true) <= 0)
		return false;
	CvPlayerAI const& kHuman = GET_PLAYER(eHuman);
	CvGame& g = GC.getGame();
	// <advc.130m> Replacing this with an attitude check at the end:
		//AI_getMemoryCount(eHuman, MEMORY_DECLARED_WAR) > 0 ||
		//AI_getMemoryCount(eHuman, MEMORY_HIRED_WAR_ALLY) > 0
	/*  Want contact probability to increase with war duration, but we haven't
		decided on a war target yet. Use the average, and later pick the target
		based on war duration. */
	double totalAtWarTurns = 0;
	int iWars = 0;
	for(int i = 0; i < MAX_CIV_TEAMS; i++)
	{
		CvTeam const& t = GET_TEAM((TeamTypes)i);
		if(t.isAlive() && !t.isAVassal() && t.isAtWar(getTeam()))
		{
			double awt = std::min(20, kOurTeam.AI_getAtWarCounter(t.getID()));
			/*  Already sharing a war with eHuman makes us reluctant to ask them
				to join another war. */
			if(t.isAtWar(TEAMID(eHuman)))
				awt *= -0.5;
			totalAtWarTurns += awt;
			iWars++;
		}
	}
	FAssert(iWars > 0);
	double avgAtWarTurns = std::max(1.0, totalAtWarTurns / iWars);
	if(g.getSorenRandNum(::round(GC.getInfo(getPersonalityType()).
			/*  That's one tenth of the BtS probability on turn one of a war,
				the BtS probability after 10 turns, and then grows to twice that. */
			getContactRand(CONTACT_JOIN_WAR) * 10 / avgAtWarTurns), // </advc.130m>
			"AI Diplo Join War") != 0)
		return false;
	int iBestTargetVal = -1;
	TeamTypes eBestTarget = NO_TEAM;
	for(int i = 0; i < MAX_CIV_TEAMS; i++)
	{
		TeamTypes eTarget = (TeamTypes)i;
		CvTeamAI const& kTarget = GET_TEAM(eTarget);
		if(!kTarget.isAlive() || !::atWar(eTarget, getTeam()) ||
				::atWar(eTarget, TEAMID(eHuman)) ||
				!GET_TEAM(eHuman).isHasMet(eTarget) ||
				!GET_TEAM(eHuman).canDeclareWar(eTarget) ||
				// <advc.130m>
				/*  Asking for a human-human war (or embargo) isn't smart;
					they could immediately reconcile */
				kTarget.isHuman() ||
				// Give eHuman some time to prepare before we press them
				kOurTeam.AI_getAtWarCounter(eTarget) < 5 ||
				kTarget.isAVassal())
			continue;
		// Avoid asking if they've recently made peace
		double prSkip = 1 - (GET_TEAM(eHuman).AI_getAtPeaceCounter(eTarget) -
				GC.getDefineINT(CvGlobals::PEACE_TREATY_LENGTH)) * 0.067;
		if(::bernoulliSuccess(prSkip, "advc.130m (peace)"))
			continue; // </advc.130m>
		int iTargetVal = g.getSorenRandNum(10000, "AI Joining War");
		// advc.130m:
		iTargetVal += std::min(20, kOurTeam.AI_getAtWarCounter(eTarget)) * 1000;
		if(iTargetVal > iBestTargetVal)
		{
			iBestTargetVal = iTargetVal;
			eBestTarget = eTarget;
		}
	}
	// <advc.130m> Ask for embargo instead
	AttitudeTypes eTowardHuman = AI_getAttitude(eHuman);
	if(eTowardHuman <= ATTITUDE_ANNOYED)
	{
		if(kOurTeam.AI_getWorstEnemy() == eBestTarget && eTowardHuman == ATTITUDE_ANNOYED)
 			return AI_proposeEmbargo(eHuman);
		return false;
	}
	if(getUWAI.isEnabled() && eBestTarget != NO_TEAM)
	{
		if(GET_TEAM(eHuman).uwai().declareWarTrade(eBestTarget, getTeam()) != NO_DENIAL)
			return AI_proposeEmbargo(eHuman);
	} // </advc.130m>
	if(eBestTarget == NO_TEAM)
		return false;
	AI_changeContactTimer(eHuman, CONTACT_JOIN_WAR,
			GC.getInfo(getPersonalityType()).
			getContactDelay(CONTACT_JOIN_WAR));
	CvDiploParameters* pDiplo = new CvDiploParameters(getID());
	/*  NB: The DLL never deletes CvDiploParameters objects; presumably
		handled by beginDiplomacy */
	pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString(
			"AI_DIPLOCOMMENT_JOIN_WAR"), GET_PLAYER(GET_TEAM(eBestTarget).
			getLeaderID()).getCivilizationAdjectiveKey());
	pDiplo->setAIContact(true);
	pDiplo->setData(eBestTarget);
	gDLL->beginDiplomacy(pDiplo, eHuman);
	return true;
}

//  Same assumptions as for contactReligion, plus can't be between vassal + master.
bool CvPlayerAI::AI_proposeEmbargo(PlayerTypes eHuman)
{
	if(AI_getContactTimer(eHuman, CONTACT_STOP_TRADING) > 0 ||
			AI_getAttitude(eHuman) <= ATTITUDE_FURIOUS) // advc.130f
		return false;
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
	TeamTypes eBestTeam = kOurTeam.AI_getWorstEnemy();
	if(eBestTeam == NO_TEAM || !GET_TEAM(eHuman).isHasMet(eBestTeam) ||
			GET_TEAM(eBestTeam).isVassal(TEAMID(eHuman)) ||
			!GET_PLAYER(eHuman).canStopTradingWithTeam(eBestTeam) ||
			// advc.135: Nothing stops the two humans from immediately resuming trade
			GET_TEAM(eBestTeam).isHuman() ||
			// advc.104m:
			kOurTeam.AI_isSneakAttackReady(TEAMID(eHuman)) ||
			// <advc.130f>
			kOurTeam.AI_getAttitudeVal(TEAMID(eHuman)) -
			kOurTeam.AI_getAttitudeVal(eBestTeam) < 2) // </advc.130f>
		return false;
	FAssert(!atWar(TEAMID(eHuman), eBestTeam));
	FAssert(TEAMID(eHuman) != eBestTeam);
	// <advc.130f>
	/*  During war preparation against human, ask to stop trading only if this
		will improve our attitude. */
	/*if(kOurTeam.AI_getWarPlan(TEAMID(eHuman)) != NO_WARPLAN &&
			AI_getAttitude(eHuman) >= AI_getAttitudeFromValue(AI_getAttitudeVal(eHuman))
			+ GC.getInfo(getPersonalityType()).
			getMemoryAttitudePercent(MEMORY_ACCEPTED_STOP_TRADING) / 100)
		return false;*/
	/*  ^ Never mind; leave it up to the human to figure out if the AI might
		declare war anyway. */
	/*  Mustn't propose embargo if our deals can't be canceled.
		Perhaps this check should be in CvPlayer::canTradeItem instead? */
	FOR_EACH_DEAL(d)
	{
		if((d->getFirstPlayer() != getID() || TEAMID(d->getSecondPlayer()) != eBestTeam) &&
				(d->getSecondPlayer() != getID() || TEAMID(d->getFirstPlayer()) != eBestTeam))
			continue;
		if(!d->isCancelable(getID()))
			return false;
	} // </advc.130f>
	AI_changeContactTimer(eHuman, CONTACT_STOP_TRADING,
			GC.getInfo(getPersonalityType()).
			getContactDelay(CONTACT_STOP_TRADING));
	CvDiploParameters* pDiplo = new CvDiploParameters(getID());
	pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString(
			"AI_DIPLOCOMMENT_STOP_TRADING"),
			GET_PLAYER(GET_TEAM(eBestTeam).getLeaderID()).
			getCivilizationAdjectiveKey());
	pDiplo->setAIContact(true);
	pDiplo->setData(eBestTeam);
	gDLL->beginDiplomacy(pDiplo, eHuman);
	return true;
}

/*  Caller ensures isAlive, canContactAndTalk, not at war, not Minor/ Barbarian,
	!this->isHuman. If eTo is human, !abContacted is ensured, but eTo may
	also be an AI civ.
	eTo is the player to whom this player proposes a trade; eTo isn't necessarily
	offered a resource. */
bool CvPlayerAI::AI_proposeResourceTrade(PlayerTypes eTo)
{
	PROFILE_FUNC(); // advc

	if(AI_getContactTimer(eTo, CONTACT_TRADE_BONUS) > 0)
		return false;
	CvPlayerAI const& kTo = GET_PLAYER(eTo);
	CvGame& g = GC.getGame();
	int const iDealLen = GC.getDefineINT(CvGlobals::PEACE_TREATY_LENGTH);
	// Resource that this player wants to receive from eTo
	BonusTypes eBestReceiveBonus = NO_BONUS;
	if (kTo.canPossiblyTradeItem(getID(), TRADE_RESOURCES)) // advc.opt
	{
		int iBestValue = 0;
		FOR_EACH_ENUM(Bonus)
		{
			/*  <advc.036> For AI-AI trades the conditions below are better handled
				by AI_getTradeDenial. */
			if(kTo.isHuman() && (kTo.getNumTradeableBonuses(eLoopBonus) <= 1 ||
				// || AI_bonusTradeVal(eLoopBonus, eOther, 1) <= 0)
				kTo.AI_corporationBonusVal(eLoopBonus, true) > 0))
			{
				continue;
			}
			/*  Probably cheaper to check canTradeItem before calling AI_bonusTradeVal;
				a resource they don't have is unlikely to have a cached bonusTradeVal.
				But getTradeDenial needs to be tested last (bTestDenial=false). */
			TradeData item(TRADE_RESOURCES, eLoopBonus);
			if(!kTo.canTradeItem(getID(), item, false))
				continue;
			int iBias = AI_bonusTradeVal(eLoopBonus, eTo, 1) -
					// Estimate of how much kTo would rather keep eLoopBonus
					kTo.AI_bonusVal(eLoopBonus, -1, false, true) * iDealLen *
					(getNumCities() + kTo.getNumCities()) / 5;
			if(!::bernoulliSuccess((iBias - 15) / 90.0, "advc.036"))
				continue;
			if(kTo.getTradeDenial(getID(), item) == NO_DENIAL)
			{
				// Was completely random before
				int iValue = g.getSorenRandNum(25, "AI Bonus Trading #1")
						+ iBias; // </advc.036>
				if(iValue > iBestValue)
				{
					iBestValue = iValue;
					eBestReceiveBonus = eLoopBonus;
				}
			}
		}
	}
	// <advc.036> Commented out
	/*if(eBestReceiveBonus != NO_BONUS) {
	iBestValue = 0;*/
	/*  If human doesn't have a surplus resource we want, then don't try to sell
		him/her something. */
	if(eBestReceiveBonus == NO_BONUS && kTo.isHuman())
		return false;
	BonusTypes eBestGiveBonus = NO_BONUS;
	if (canPossiblyTradeItem(eTo, TRADE_RESOURCES)) // advc.opt
	{
		int iBestValue = 0;
		FOR_EACH_ENUM(Bonus)
		{
			// <advc.036> See comments about eBestReceiveBonus
			TradeData item(TRADE_RESOURCES, eLoopBonus);
			if(!canTradeItem(eTo, item, false))
				continue;
			/*if(i == eBestReceiveBonus || getNumTradeableBonuses(eLoopBonus) <= 1 ||
					kTo.AI_bonusTradeVal(eLoopBonus, getID(), 1) <= 0)
				continue;*/
			int iBias = kTo.AI_bonusTradeVal(eLoopBonus, getID(), 1) -
					// How much this player would rather keep eLoopBonus
					AI_bonusVal(eLoopBonus, -1, false, true) * iDealLen *
					(getNumCities() + kTo.getNumCities()) / 5;
			/*	Less likely to skip than above - now that one sensible trade item
				is already locked in. And we don't want to pay cash for it. */
			if(!::bernoulliSuccess((iBias - 15) / 35.0, "advc.036"))
				continue;
			if(getTradeDenial(eTo, item) == NO_DENIAL)
			{
				int iValue = (g.getSorenRandNum(30, "AI Bonus Trading #2")
						+ iBias); // </advc.036>
				if(iValue > iBestValue)
				{
					iBestValue = iValue;
					eBestGiveBonus = eLoopBonus;
				}
			}
		}
	}
	CLinkList<TradeData> ourList;
	CLinkList<TradeData> theirList;
	/*if(eBestGiveBonus != NO_BONUS && (!kTo.isHuman() ||
			AI_bonusTradeVal(eBestReceiveBonus, eTo, -1) >=
			kTo.AI_bonusTradeVal(eBestGiveBonus, getID(), 1))) {
		ourList.clear();
		theirList.clear();
		setTradeItem(&item, TRADE_RESOURCES, eBestGiveBonus);
		ourList.insertAtEnd(item);
		setTradeItem(&item, TRADE_RESOURCES, eBestReceiveBonus);
		theirList.insertAtEnd(item);
	}*/
	/*  <advc.036> Replacing the above.
		Rather than trying to trade eBestGiveBonus against eBestReceiveBonus,
		pick the one with the higher trade value difference (iBestValue) and let
		AI_counterPropose produce an offer. */
	bool bDeal = false;
	if(eBestGiveBonus != NO_BONUS)
	{
		ourList.clear();
		theirList.clear();
		ourList.insertAtEnd(TradeData(TRADE_RESOURCES, eBestGiveBonus));
		if(AI_counterPropose(eTo, theirList, ourList, true, false))
			bDeal = true;
	}
	if(!bDeal && eBestReceiveBonus != NO_BONUS)
	{
		ourList.clear();
		theirList.clear();
		theirList.insertAtEnd(TradeData(TRADE_RESOURCES, eBestReceiveBonus));
		if(kTo.AI_counterPropose(getID(), ourList, theirList, true, false))
			bDeal = true;
	}
	if(!bDeal)
		return false;
	// </advc.036>
	if(kTo.isHuman())
	{
		AI_changeContactTimer(eTo, CONTACT_TRADE_BONUS, GC.getInfo(getPersonalityType()).
				getContactDelay(CONTACT_TRADE_BONUS));
		CvDiploParameters* pDiplo = new CvDiploParameters(getID());
		pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString(
				"AI_DIPLOCOMMENT_OFFER_DEAL"));
		pDiplo->setAIContact(true);
		pDiplo->setOurOfferList(theirList);
		pDiplo->setTheirOfferList(ourList);
		gDLL->beginDiplomacy(pDiplo, eTo);
		return true;
	}
	g.implementDeal(getID(), eTo, ourList, theirList);
	return false; // Only true when a human was contacted
} // </advc.133>

/*  Caller ensures isHuman/ !isHuman, isAlive, canContactAndTalk,
	unequal teams, not at war, not Minor/ Barbarian. */
bool CvPlayerAI::AI_contactReligion(PlayerTypes eHuman)
{
	if(AI_getContactTimer(eHuman, CONTACT_RELIGION_PRESSURE) > 0 ||
			getStateReligion() == NO_RELIGION)
		return false;
	CvPlayer& kHuman = GET_PLAYER(eHuman);
	if(!kHuman.canConvert(getStateReligion()))
		return false;
	AI_changeContactTimer(eHuman, CONTACT_RELIGION_PRESSURE,
			GC.getInfo(getPersonalityType()).
			getContactDelay(CONTACT_RELIGION_PRESSURE));
	CvDiploParameters* pDiplo = new CvDiploParameters(getID());
	pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString(
			"AI_DIPLOCOMMENT_RELIGION_PRESSURE"));
	pDiplo->setAIContact(true);
	gDLL->beginDiplomacy(pDiplo, eHuman);
	return true;
}

// Same conditions as contactReligion
bool CvPlayerAI::AI_contactCivics(PlayerTypes eHuman)
{
	if(AI_getContactTimer(eHuman, CONTACT_CIVIC_PRESSURE) > 0)
		return false;
	CivicTypes eFavoriteCivic = (CivicTypes)(GC.getInfo(getPersonalityType()).
			getFavoriteCivic());
	if(eFavoriteCivic == NO_CIVIC || !isCivic(eFavoriteCivic))
		return false;
	CvPlayer& kHuman = GET_PLAYER(eHuman);
	if(!kHuman.canDoCivics(eFavoriteCivic) || kHuman.isCivic(eFavoriteCivic) ||
			!kHuman.canRevolution(NULL))
		return false;
	AI_changeContactTimer(eHuman, CONTACT_CIVIC_PRESSURE,
			GC.getInfo(getPersonalityType()).
			getContactDelay(CONTACT_CIVIC_PRESSURE));
	CvDiploParameters* pDiplo = new CvDiploParameters(getID());
	pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString(
			"AI_DIPLOCOMMENT_CIVIC_PRESSURE"),
			GC.getInfo(eFavoriteCivic).getTextKeyWide());
	pDiplo->setAIContact(true);
	gDLL->beginDiplomacy(pDiplo, eHuman);
	return true;
}

/*  Same conditions as contactReligion. (AI_doDiplo checks additional ones,
	but those aren't necessary for this function to work correctly.) */
bool CvPlayerAI::AI_askHelp(PlayerTypes eHuman)
{
	// <advc.104m>
	if(GET_TEAM(getTeam()).AI_isSneakAttackReady(TEAMID(eHuman)))
		return false; // </advc.104m>
	/*  <advc.144> Don't ask for help during a peace treaty unless human has been
		given help (which may well be the reason for the peace treaty) */
	if(GET_TEAM(getTeam()).turnsOfForcedPeaceRemaining(TEAMID(eHuman)) > 3 &&
			GET_PLAYER(eHuman).AI_getMemoryCount(getID(), MEMORY_GIVE_HELP) <= 0)
		return false; // </advc.144>
	// <advc.705>
	CvGame const& g = GC.getGame();
	if(g.isOption(GAMEOPTION_RISE_FALL) && g.getRiseFall().
			isCooperationRestricted(getID()) &&
			::bernoulliSuccess(0.33, "advc.705"))
		return false; // </advc.705>
	if(AI_getContactTimer(eHuman, CONTACT_ASK_FOR_HELP) > 0 ||
			GET_TEAM(eHuman).getAssets() <=
			(2 * GET_TEAM(getTeam()).getAssets()) / 3) // advc.131: was 1/2
		return false;
	// <advc.144> Replacing the loop below
	TechTypes eBestReceiveTech = (!GET_PLAYER(eHuman).canPossiblyTradeItem(
			getID(), TRADE_TECHNOLOGIES) ? NO_TECH :
			AI_bestTech(1, false, false, NO_TECH, NO_ADVISOR, eHuman));
	// </advc.144>
	/*int iBestValue = 0;
	for(FOR_EACH:ENUM(Tech)) {
		setTradeItem(&item, TRADE_TECHNOLOGIES, eLoopTech);
		if(!GET_PLAYER(eHuman).canTradeItem(getID(), item))
			continue;
		int iValue = 1 + GC.getGame().getSorenRandNum(10000, "AI Asking For Help");
		if(iValue > iBestValue) {
			iBestValue = iValue;
			eBestReceiveTech = eLoopTech;
		}
	}*/
	if(eBestReceiveTech == NO_TECH)
		return false;
	TradeData item(TRADE_TECHNOLOGIES, eBestReceiveTech);
	// <advc.144>
	if (!GET_PLAYER(eHuman).canTradeItem(getID(), item))
		return false; // </advc.144>
	CLinkList<TradeData> theirList;
	theirList.insertAtEnd(item);
	AI_changeContactTimer(eHuman, CONTACT_ASK_FOR_HELP,
			GC.getInfo(getPersonalityType()).
			getContactDelay(CONTACT_ASK_FOR_HELP));
	CvDiploParameters* pDiplo = new CvDiploParameters(getID());
	pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString(
			"AI_DIPLOCOMMENT_ASK_FOR_HELP"));
	pDiplo->setAIContact(true);
	pDiplo->setOurOfferList(theirList);
	gDLL->beginDiplomacy(pDiplo, eHuman);
	return true;
}

// Same conditions ensured by the caller as for the functions above
bool CvPlayerAI::AI_demandTribute(PlayerTypes eHuman, AIDemandTypes eDemand)
{
	// <advc.104m>
	if(GET_TEAM(getTeam()).AI_isSneakAttackReady(TEAMID(eHuman)))
	{
		FAssert(false); // Callers ensure this I think
		return false;
	} // </advc.104m>
	// <advc.144> Not during a peace treaty
	if(GET_TEAM(getTeam()).turnsOfForcedPeaceRemaining(TEAMID(eHuman)) > 1)
		return false; // </advc.144>
	if(AI_getContactTimer(eHuman, CONTACT_DEMAND_TRIBUTE) > 0 ||
		AI_getAttitude(eHuman) > GC.getInfo(getPersonalityType()).
		getDemandTributeAttitudeThreshold())
	{
		return false;
	}
	CvPlayerAI& kHuman = GET_PLAYER(eHuman);
	CLinkList<TradeData> theirList;
	switch(eDemand)
	{
	case DEMAND_GOLD:
	{
		int iReceiveGold = range(kHuman.getGold() - 50, 0, kHuman.AI_goldTarget());
		AI_roundTradeVal(iReceiveGold);
		if(iReceiveGold <= 50)
			break;
		TradeData item(TRADE_GOLD, iReceiveGold);
		/*  advc.001: BtS checks this only for some of the items, and it does so
			with bTestDenial, which seems pointless when the callee is human. */
		if(kHuman.canTradeItem(getID(), item))
			theirList.insertAtEnd(item);
		break;
	}
	case DEMAND_MAP:
	{
		TradeData item(TRADE_MAPS);
		if(kHuman.canTradeItem(getID(), item)) // advc.001
		{
			if(GET_TEAM(getTeam()).AI_mapTradeVal(kHuman.getTeam()) > 100)
				theirList.insertAtEnd(item);
		}
		break;
	}
	case DEMAND_TECH:
	{
		if (!kHuman.canPossiblyTradeItem(getID(), TRADE_TECHNOLOGIES))
			break;
		int iBestValue = 0;
		TechTypes eBestReceiveTech = NO_TECH;
		// <advc.144>
		TechTypes eMostUsefulTech = AI_bestTech(1, false, false, NO_TECH, NO_ADVISOR,
				eHuman); // </advc.144>
		FOR_EACH_ENUM(Tech)
		{
			if(!kHuman.canTradeItem(getID(), TradeData(
				TRADE_TECHNOLOGIES, eLoopTech)))
			{
				continue;
			}
			if(GC.getGame().countKnownTechNumTeams(eLoopTech) <= 1)
				continue;
			int iValue = 1 + GC.getGame().getSorenRandNum(10000,
					"AI Demanding Tribute (Tech)");
			// <advc.144>
			if(eLoopTech == eMostUsefulTech)
				iValue *= 3; // </advc.144>
			if(iValue > iBestValue)
			{
				iBestValue = iValue;
				eBestReceiveTech = eLoopTech;
			}
		}
		if(eBestReceiveTech != NO_TECH)
			theirList.insertAtEnd(TradeData(TRADE_TECHNOLOGIES, eBestReceiveTech));
		break;
	}
	case DEMAND_BONUS:
	{
		if (!kHuman.canPossiblyTradeItem(getID(), TRADE_RESOURCES))
			break;
		int iBestValue = 0;
		BonusTypes eBestReceiveBonus = NO_BONUS;
		FOR_EACH_ENUM(Bonus)
		{
			if(!kHuman.canTradeItem(getID(), TradeData(
				TRADE_RESOURCES, eLoopBonus)))
			{
				continue;
			}
			if(kHuman.getNumTradeableBonuses(eLoopBonus) <= 1 ||
				AI_bonusTradeVal(eLoopBonus, eHuman, 1) <= 0)
			{
				continue;
			}
			int iValue = 1 + GC.getGame().getSorenRandNum(10000,
					"AI Demanding Tribute (Bonus)");
			if(iValue > iBestValue)
			{
				iBestValue = iValue;
				eBestReceiveBonus = eLoopBonus;
			}
		}
		if(eBestReceiveBonus != NO_BONUS)
			theirList.insertAtEnd(TradeData(TRADE_RESOURCES, eBestReceiveBonus));
		break; // <advc.104m> New: demand gpt
	}
	case DEMAND_GOLD_PER_TURN:
	{
		int iGPT = (kHuman.AI_getAvailableIncome() -
				kHuman.calculateInflatedCosts()) / 5;
		if(iGPT >= 2 * GC.getDefineINT(CvGlobals::DIPLOMACY_VALUE_REMAINDER)) {
			FAssertMsg(false, "Does the AI ever demand GPT? Why not?"); // advc.test
			AI_roundTradeVal(iGPT);
			TradeData item(TRADE_GOLD_PER_TURN, iGPT);
			if(kHuman.canTradeItem(getID(), item))
				theirList.insertAtEnd(item);
		}
		break; // </advc.104m>
	} default:
		FAssertMsg(false, "Unknown AI demand type");
		return false;
	}
	if(theirList.getLength() <= 0)
		return false;
	// <advc.104m> Don't ask for too little
	// Unless difficulty is low
	if(GC.getInfo(kHuman.getHandicapType()).getDifficulty() > 25)
	{
		int iDealVal = AI_dealVal(eHuman, theirList, false, 1, true);
		if(iDealVal < 50 * std::pow(2.0, getCurrentEra()))
			return false;
	} // </advc.104m>
	AI_changeContactTimer(eHuman, CONTACT_DEMAND_TRIBUTE,
			GC.getInfo(getPersonalityType()).
			getContactDelay(CONTACT_DEMAND_TRIBUTE));
	CvDiploParameters* pDiplo = new CvDiploParameters(getID());
	pDiplo->setDiploComment((DiploCommentTypes)GC.getInfoTypeForString(
			"AI_DIPLOCOMMENT_DEMAND_TRIBUTE"));
	pDiplo->setAIContact(true);
	pDiplo->setOurOfferList(theirList);
	gDLL->beginDiplomacy(pDiplo, eHuman);
	return true;
} // advc: End of functions cut from AI_doDiplo

// <advc.104> (also used for advc.031)
/*  Assets like additional cities become less valuable over the course of a game
	b/c there are fewer and fewer turns to go. Not worth considering in the
	first half of the game, but e.g. after 300 turns (normal settings),
	the multiplier will have decreased to 0.8. The current implementation isn't
	specific to a CvPlayerAI, but this could change, i.e. one could estimate the
	end turn based on the knowledge of this CvPlayerAI.
	iDelay: When an asset is not immediately acquired. */
double CvPlayerAI::AI_amortizationMultiplier(int iDelay) const
{
	return ::dRange(2.0 * // Use this coefficient to fine-tune the effect
			(1 - GC.getGame().gameTurnProgress(std::max(0, iDelay))),
			0.2, 1.0);
} // </advc.104>

// read object from a stream. used during load
void CvPlayerAI::read(FDataStreamBase* pStream)
{
	CvPlayer::read(pStream);

	uint uiFlag=0;
	pStream->Read(&uiFlag);

	pStream->Read(&m_iPeaceWeight);
	pStream->Read(&m_iEspionageWeight);
	pStream->Read(&m_iAttackOddsChange);
	pStream->Read(&m_iCivicTimer);
	pStream->Read(&m_iReligionTimer);
	pStream->Read(&m_iExtraGoldTarget);
	// K-Mod
	if (uiFlag >= 6)
		pStream->Read(&m_iCityTargetTimer);
	else AI_setCityTargetTimer(0);
	// K-Mod end

	pStream->Read((int*)&m_eStrategyHash);
	//pStream->Read(&m_iStrategyHashCacheTurn); // disabled by K-Mod
	// BBAI (edited for K-Mod)
	pStream->Read(&m_iStrategyRand);
	pStream->Read((int*)&m_eVictoryStageHash);
	// BBAI end
	//pStream->Read(&m_iAveragesCacheTurn); // disabled by K-Mod
	pStream->Read(&m_iAverageGreatPeopleMultiplier);
	pStream->Read(&m_iAverageCulturePressure); // K-Mod

	pStream->Read(NUM_YIELD_TYPES, m_aiAverageYieldMultiplier);
	pStream->Read(NUM_COMMERCE_TYPES, m_aiAverageCommerceMultiplier);
	pStream->Read(NUM_COMMERCE_TYPES, m_aiAverageCommerceExchange);

	//pStream->Read(&m_iUpgradeUnitsCacheTurn); // disabled by K-Mod
	//pStream->Read(&m_iUpgradeUnitsCachedExpThreshold); // disabled by K-Mod
	pStream->Read(&m_iUpgradeUnitsCachedGold);
	// K-Mod
	if (uiFlag >= 7)
		pStream->Read(&m_iAvailableIncome);
	// K-Mod end

	pStream->Read(NUM_UNITAI_TYPES, m_aiNumTrainAIUnits);
	pStream->Read(NUM_UNITAI_TYPES, m_aiNumAIUnits);

	pStream->Read(MAX_PLAYERS, m_aiSameReligionCounter);
	pStream->Read(MAX_PLAYERS, m_aiDifferentReligionCounter);
	pStream->Read(MAX_PLAYERS, m_aiFavoriteCivicCounter);
	pStream->Read(MAX_PLAYERS, m_aiBonusTradeCounter);
	pStream->Read(MAX_PLAYERS, m_aiPeacetimeTradeValue);
	pStream->Read(MAX_PLAYERS, m_aiPeacetimeGrantValue);
	pStream->Read(MAX_PLAYERS, m_aiGoldTradedTo);
	pStream->Read(MAX_PLAYERS, m_aiAttitudeExtra);
	// <advc.079>
	if(uiFlag >= 12)
	{
		int iTmp;
		for(int i = 0; i < MAX_CIV_PLAYERS; i++)
		{
			pStream->Read(&iTmp);
			m_aeLastBrag[i] = (UnitTypes)iTmp;
			pStream->Read(&iTmp);
			m_aeLastWarn[i] = (TeamTypes)iTmp;
		}
	} // </advc.079>
	// <advc.130c>
	if(uiFlag >= 13)
		pStream->Read(MAX_CIV_PLAYERS, m_abTheyFarAhead);
	if(uiFlag >= 14)
		pStream->Read(MAX_CIV_PLAYERS, m_abTheyBarelyAhead);
	// </advc.130c>
	// K-Mod. Load the attitude cache. (originally, in BBAI and the CAR Mod, this was not saved.
	// But there are rare situations in which it needs to be saved/read to avoid OOS errors.)
	if (uiFlag >= 4)
		pStream->Read(MAX_PLAYERS, &m_aiAttitude[0]);
	// K-Mod end
	pStream->Read(MAX_PLAYERS, m_abFirstContact);

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		pStream->Read(NUM_CONTACT_TYPES, m_aaiContactTimer[i]);
	}
	// <advc.104i>
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		int iNumMemoryTypesToRead = NUM_MEMORY_TYPES;
		if(uiFlag < 11)
			iNumMemoryTypesToRead--;
		pStream->Read(iNumMemoryTypesToRead, m_aaiMemoryCount[i]);
		if(uiFlag < 11) { // DECLARED_WAR_RECENT takes over for STOPPED_TRADING_RECENT
			FAssert(iNumMemoryTypesToRead == MEMORY_DECLARED_WAR_RECENT);
			m_aaiMemoryCount[i][iNumMemoryTypesToRead] = m_aaiMemoryCount[i]
					[MEMORY_STOPPED_TRADING_RECENT];
		}
	} // </advc.104i>
	pStream->Read(&m_bWasFinancialTrouble);
	pStream->Read(&m_iTurnLastProductionDirty);

	{
		m_aiAICitySites.clear();
		uint iSize;
		pStream->Read(&iSize);
		for (uint i = 0; i < iSize; i++)
		{
			int iCitySite;
			pStream->Read(&iCitySite);
			m_aiAICitySites.push_back(iCitySite);
		}
	}

	pStream->Read(GC.getNumBonusInfos(), m_aiBonusValue);
	// <advc.036>
	if(uiFlag >= 10)
		pStream->Read(GC.getNumBonusInfos(), m_aiBonusValueTrade); // </advc.036>
	pStream->Read(GC.getNumUnitClassInfos(), m_aiUnitClassWeights);
	pStream->Read(GC.getNumUnitCombatInfos(), m_aiUnitCombatWeights);
	// K-Mod
	m_GreatPersonWeights.clear();
	if (uiFlag >= 5)
	{
		int iItems;
		int iType, iWeight;
		pStream->Read(&iItems);
		for (int i = 0; i < iItems; i++)
		{
			pStream->Read(&iType);
			pStream->Read(&iWeight);
			m_GreatPersonWeights.insert(std::make_pair((UnitClassTypes)iType, iWeight));
		}
	}
	// K-Mod end
	//pStream->Read(MAX_PLAYERS, m_aiCloseBordersAttitude);
	pStream->Read(MAX_PLAYERS, &m_aiCloseBordersAttitude[0]); // K-Mod
	// <advc.opt>
	if(uiFlag >= 8)
	{
		int sz=0;
		pStream->Read(&sz);
		for(int i = 0; i < sz; i++)
		{
			int first = -1, second = 0;
			pStream->Read(&first);
			pStream->Read(&second);
			FAssert(first >= 0);
			m_neededExplorersByArea[first] = second;
		}
	}
	else if(isAlive())
		AI_updateNeededExplorers();
	// </advc.opt>
	// <advc.104>
	if(isMajorCiv() && (uiFlag < 15 ? isEverAlive() : isAlive()))
		m_pUWAI->read(pStream); // </advc.104>
	// <advc.148>
	if(uiFlag < 9 && getID() == MAX_PLAYERS - 1)
	{
		for(int i = 0; i < MAX_CIV_PLAYERS; i++)
		{
			CvPlayerAI& kPlayer = GET_PLAYER((PlayerTypes)i);
			if(kPlayer.isAlive())
				kPlayer.AI_updateAttitude();
		}
	} // </advc.148>
}

// save object to a stream
void CvPlayerAI::write(FDataStreamBase* pStream)
{
	PROFILE_FUNC(); // advc
	CvPlayer::write(pStream);

	uint uiFlag=7;
	uiFlag = 8; // advc.opt
	uiFlag = 9; // advc.148
	uiFlag = 10; // advc.036
	uiFlag = 11; // advc.104i
	uiFlag = 12; // advc.079
	uiFlag = 14; // advc.130c
	uiFlag = 15; // advc.104: Don't save UWAI cache of dead civ
	pStream->Write(uiFlag);

	pStream->Write(m_iPeaceWeight);
	pStream->Write(m_iEspionageWeight);
	pStream->Write(m_iAttackOddsChange);
	pStream->Write(m_iCivicTimer);
	pStream->Write(m_iReligionTimer);
	pStream->Write(m_iExtraGoldTarget);
	pStream->Write(m_iCityTargetTimer); // K-Mod. uiFlag >= 6

	pStream->Write(m_eStrategyHash);
	//pStream->Write(m_iStrategyHashCacheTurn); // disabled by K-Mod
	// BBAI
	pStream->Write(m_iStrategyRand);
	pStream->Write(m_eVictoryStageHash);
	// BBAI end
	//pStream->Write(m_iAveragesCacheTurn);
	pStream->Write(m_iAverageGreatPeopleMultiplier);
	pStream->Write(m_iAverageCulturePressure); // K-Mod

	pStream->Write(NUM_YIELD_TYPES, m_aiAverageYieldMultiplier);
	pStream->Write(NUM_COMMERCE_TYPES, m_aiAverageCommerceMultiplier);
	pStream->Write(NUM_COMMERCE_TYPES, m_aiAverageCommerceExchange);

	//pStream->Write(m_iUpgradeUnitsCacheTurn); // disabled by K-Mod
	//pStream->Write(m_iUpgradeUnitsCachedExpThreshold); // disabled by K-Mod
	pStream->Write(m_iUpgradeUnitsCachedGold);
	pStream->Write(m_iAvailableIncome); // K-Mod. uiFlag >= 7

	pStream->Write(NUM_UNITAI_TYPES, m_aiNumTrainAIUnits);
	pStream->Write(NUM_UNITAI_TYPES, m_aiNumAIUnits);
	pStream->Write(MAX_PLAYERS, m_aiSameReligionCounter);
	pStream->Write(MAX_PLAYERS, m_aiDifferentReligionCounter);
	pStream->Write(MAX_PLAYERS, m_aiFavoriteCivicCounter);
	pStream->Write(MAX_PLAYERS, m_aiBonusTradeCounter);
	pStream->Write(MAX_PLAYERS, m_aiPeacetimeTradeValue);
	pStream->Write(MAX_PLAYERS, m_aiPeacetimeGrantValue);
	pStream->Write(MAX_PLAYERS, m_aiGoldTradedTo);
	pStream->Write(MAX_PLAYERS, m_aiAttitudeExtra);
	// <advc.079>
	for(int i = 0; i < MAX_CIV_PLAYERS; i++)
	{
		pStream->Write(m_aeLastBrag[i]);
		pStream->Write(m_aeLastWarn[i]);
	} // </advc.079>
	// <advc.130c>
	pStream->Write(MAX_CIV_PLAYERS, m_abTheyFarAhead);
	pStream->Write(MAX_CIV_PLAYERS, m_abTheyBarelyAhead); // </advc.130c>
	// K-Mod. save the attitude cache. (to avoid OOS problems)
	pStream->Write(MAX_PLAYERS, &m_aiAttitude[0]); // uiFlag >= 4
	// K-Mod end
	pStream->Write(MAX_PLAYERS, m_abFirstContact);

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		pStream->Write(NUM_CONTACT_TYPES, m_aaiContactTimer[i]);
	}
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		pStream->Write(NUM_MEMORY_TYPES, m_aaiMemoryCount[i]);
	}

	pStream->Write(m_bWasFinancialTrouble);
	pStream->Write(m_iTurnLastProductionDirty);

	{
		uint iSize = m_aiAICitySites.size();
		pStream->Write(iSize);
		std::vector<int>::iterator it;
		for (it = m_aiAICitySites.begin(); it != m_aiAICitySites.end(); ++it)
		{
			pStream->Write((*it));
		}
	}

	pStream->Write(GC.getNumBonusInfos(), m_aiBonusValue);
	pStream->Write(GC.getNumBonusInfos(), m_aiBonusValueTrade); // advc.036
	pStream->Write(GC.getNumUnitClassInfos(), m_aiUnitClassWeights);
	pStream->Write(GC.getNumUnitCombatInfos(), m_aiUnitCombatWeights);
	// K-Mod. save great person weights. (uiFlag >= 5)
	{
		int iItems = m_GreatPersonWeights.size();
		pStream->Write(iItems);
		std::map<UnitClassTypes, int>::const_iterator it;
		for (it = m_GreatPersonWeights.begin(); it != m_GreatPersonWeights.end(); ++it)
		{
			pStream->Write((int)it->first);
			pStream->Write(it->second);
		}
	}
	// K-Mod end
	//pStream->Write(MAX_PLAYERS, m_aiCloseBordersAttitude);
	pStream->Write(MAX_PLAYERS, &m_aiCloseBordersAttitude[0]); // K-Mod
	// <advc.opt>
	pStream->Write((int)m_neededExplorersByArea.size());
	for(std::map<int,int>::const_iterator it = m_neededExplorersByArea.begin();
		it != m_neededExplorersByArea.end(); it++)
	{
		pStream->Write(it->first);
		pStream->Write(it->second);
	} // </advc.opt>
	// <advc.104>
	if(isAlive() && isMajorCiv())
		m_pUWAI->write(pStream); // </advc.104>
}

// (K-Mod note: this should be roughly in units of commerce.)
int CvPlayerAI::AI_eventValue(EventTypes eEvent, const EventTriggeredData& kTriggeredData) const
{
	CvEventTriggerInfo& kTrigger = GC.getInfo(kTriggeredData.m_eTrigger);
	CvEventInfo& kEvent = GC.getInfo(eEvent);

	CvTeamAI& kTeam = GET_TEAM(getTeam()); // K-Mod

	int iNumCities = getNumCities();
	CvCityAI* pCity = AI_getCity(kTriggeredData.m_iCityId);
	CvPlot* pPlot = GC.getMap().plot(kTriggeredData.m_iPlotX, kTriggeredData.m_iPlotY);
	CvUnit* pUnit = getUnit(kTriggeredData.m_iUnitId);

	int iHappy = 0;
	int iHealth = 0;
	int aiYields[NUM_YIELD_TYPES];
	int aiCommerceYields[NUM_COMMERCE_TYPES];

	for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		aiYields[iI] = 0;
	}
	for (int iI = 0; iI < NUM_COMMERCE_TYPES; iI++)
	{
		aiCommerceYields[iI] = 0;
	}

	/*if (NO_PLAYER != kTriggeredData.m_eOtherPlayer) {
		if (kEvent.isDeclareWar()) {
			switch (AI_getAttitude(kTriggeredData.m_eOtherPlayer)) {
			case ATTITUDE_FURIOUS:
			case ATTITUDE_ANNOYED:
			case ATTITUDE_CAUTIOUS:
				if (GET_TEAM(getTeam()).getDefensivePower() < GET_TEAM(GET_PLAYER(kTriggeredData.m_eOtherPlayer).getTeam()).getPower(true))
					return -MAX_INT + 1;
				break;
			case ATTITUDE_PLEASED:
			case ATTITUDE_FRIENDLY:
				return -MAX_INT + 1;
				break;
			}
		}
	} */ // this is handled later.

	//Proportional to #turns in the game...
	//(AI evaluation will generally assume proper game speed scaling!)
	int iGameSpeedPercent = GC.getInfo(GC.getGame().getGameSpeedType()).getVictoryDelayPercent();

	int iValue = GC.getGame().getSorenRandNum(kEvent.getAIValue(), "AI Event choice");
	iValue += (getEventCost(eEvent, kTriggeredData.m_eOtherPlayer, false) + getEventCost(eEvent, kTriggeredData.m_eOtherPlayer, true)) / 2;

	//iValue += kEvent.getEspionagePoints();
	iValue += kEvent.getEspionagePoints() * AI_commerceWeight(COMMERCE_ESPIONAGE) / 100; // K-Mod

	if (kEvent.getTech() != NO_TECH)
	{
		iValue += (kTeam.getResearchCost((TechTypes)kEvent.getTech()) * kEvent.getTechPercent()) / 100;
	}
	{
		UnitClassTypes eUnitClass = (UnitClassTypes)kEvent.getUnitClass(); // advc
		if (eUnitClass != NO_UNITCLASS)
		{
			UnitTypes eUnit = getCivilization().getUnit(eUnitClass);
			if (eUnit != NO_UNIT)
			{
				//Altough AI_unitValue compares well within units, the value is somewhat independent of cost
				int iUnitValue = GC.getInfo(eUnit).getProductionCost();
				if (iUnitValue > 0)
					iUnitValue *= 2;
				else if (iUnitValue == -1)
					iUnitValue = 2000; //Great Person?  // (was 200)

				iUnitValue *= GC.getInfo(GC.getGame().getGameSpeedType()).getTrainPercent();
				iUnitValue /= 100; // K-Mod
				iValue += kEvent.getNumUnits() * iUnitValue;
			}
		}
	}
	if (kEvent.isDisbandUnit() && pUnit != NULL)
	{
		int iUnitValue = pUnit->getUnitInfo().getProductionCost();
		if (iUnitValue > 0)
			iUnitValue *= 2;
		else if (iUnitValue == -1)
			iUnitValue = 2000; //Great Person?  // (was 200)

		iUnitValue *= GC.getInfo(GC.getGame().getGameSpeedType()).getTrainPercent();
		iUnitValue /= 100; // K-Mod
		iValue -= iUnitValue;
	}
	{
		BuildingClassTypes eBuildingClass = (BuildingClassTypes)kEvent.getBuildingClass(); // advc
		if (eBuildingClass != NO_BUILDINGCLASS)
		{
			BuildingTypes eBuilding = getCivilization().getBuilding(eBuildingClass);
			if (eBuilding != NO_BUILDING && pCity != NULL)
			{
				//iValue += kEvent.getBuildingChange() * pCity->AI_buildingValue(eBuilding);
				int iBuildingValue = GC.getInfo(eBuilding).getProductionCost();
				if (iBuildingValue > 0)
					iBuildingValue *= 2;
				else if (iBuildingValue == -1)
					iBuildingValue = 300; //Shrine?

				iBuildingValue *= GC.getInfo(GC.getGame().getGameSpeedType()).getConstructPercent();
				iValue += kEvent.getBuildingChange() * iBuildingValue;
			}
		}
	}

	TechTypes eBestTech = NO_TECH;
	int iBestValue = 0;
	for (int iTech = 0; iTech < GC.getNumTechInfos(); ++iTech)
	{
		if (canResearch((TechTypes)iTech))
		{
			if (NO_PLAYER == kTriggeredData.m_eOtherPlayer || GET_TEAM(GET_PLAYER(kTriggeredData.m_eOtherPlayer).getTeam()).isHasTech((TechTypes)iTech))
			{
				int iTechValue = 0;
				for (int i = 0; i < GC.getNumFlavorTypes(); ++i)
				{
					iTechValue += kEvent.getTechFlavorValue(i) * GC.getInfo((TechTypes)iTech).getFlavorValue(i);
				}

				if (iTechValue > iBestValue)
				{
					eBestTech = (TechTypes)iTech;
					iBestValue = iTechValue;
				}
			}
		}
	}

	if (eBestTech != NO_TECH)
	{
		iValue += (kTeam.getResearchCost(eBestTech) * kEvent.getTechPercent()) / 100;
	}

	if (kEvent.isGoldenAge())
	{
		iValue += AI_calculateGoldenAgeValue();
	}

	{	//Yield and other changes
		if (kEvent.getNumBuildingYieldChanges() > 0)
		{
			for (int iBuildingClass = 0; iBuildingClass < GC.getNumBuildingClassInfos(); ++iBuildingClass)
			{
				for (int iYield = 0; iYield < NUM_YIELD_TYPES; ++iYield)
				{
					aiYields[iYield] += kEvent.getBuildingYieldChange(iBuildingClass, iYield);
				}
			}
		}

		if (kEvent.getNumBuildingCommerceChanges() > 0)
		{
			for (int iBuildingClass = 0; iBuildingClass < GC.getNumBuildingClassInfos(); ++iBuildingClass)
			{
				for (int iCommerce = 0; iCommerce < NUM_COMMERCE_TYPES; ++iCommerce)
				{
					aiCommerceYields[iCommerce] += kEvent.getBuildingCommerceChange(iBuildingClass, iCommerce);
				}
			}
		}

		if (kEvent.getNumBuildingHappyChanges() > 0)
		{
			for (int iBuildingClass = 0; iBuildingClass < GC.getNumBuildingClassInfos(); ++iBuildingClass)
			{
				iHappy += kEvent.getBuildingHappyChange(iBuildingClass);
			}
		}

		if (kEvent.getNumBuildingHealthChanges() > 0)
		{
			for (int iBuildingClass = 0; iBuildingClass < GC.getNumBuildingClassInfos(); ++iBuildingClass)
			{
				iHealth += kEvent.getBuildingHealthChange(iBuildingClass);
			}
		}
	}

	if (kEvent.isCityEffect())
	{
		int iCityPopulation = -1;
		int iCityTurnValue = 0;
		if (pCity != NULL)
		{
			iCityPopulation = pCity->getPopulation();
			for (int iSpecialist = 0; iSpecialist < GC.getNumSpecialistInfos(); ++iSpecialist)
			{
				if (kEvent.getFreeSpecialistCount(iSpecialist) > 0)
				{
					//iCityTurnValue += (pCity->AI_specialistValue((SpecialistTypes)iSpecialist, false, false) / 300);
					iCityTurnValue += pCity->AI_permanentSpecialistValue((SpecialistTypes)iSpecialist) / 100; // K-Mod
				}
			}
		}

		if (iCityPopulation == -1)
		{
			//What is going on here?
			iCityPopulation = 5;
			FAssert(false); // advc
		}

		iCityTurnValue += ((iHappy + kEvent.getHappy()) * 8);
		iCityTurnValue += ((iHealth + kEvent.getHealth()) * 6);

		iCityTurnValue += aiYields[YIELD_FOOD] * 5;
		iCityTurnValue += aiYields[YIELD_PRODUCTION] * 5;
		iCityTurnValue += aiYields[YIELD_COMMERCE] * 3;

		iCityTurnValue += aiCommerceYields[COMMERCE_RESEARCH] * 3;
		iCityTurnValue += aiCommerceYields[COMMERCE_GOLD] * 3;
		iCityTurnValue += aiCommerceYields[COMMERCE_CULTURE] * 2; // was 1
		iCityTurnValue += aiCommerceYields[COMMERCE_ESPIONAGE] * 2;

		iValue += (iCityTurnValue * 20 * iGameSpeedPercent) / 100;

		iValue += kEvent.getFood();
		iValue += kEvent.getFoodPercent() / 4;
		iValue += kEvent.getPopulationChange() * 30;
		iValue -= kEvent.getRevoltTurns() * (12 + iCityPopulation * 16);
		iValue -= (kEvent.getHurryAnger() * 6 * GC.getDefineINT(CvGlobals::HURRY_ANGER_DIVISOR) *
				GC.getInfo(GC.getGame().getGameSpeedType()).getHurryConscriptAngerPercent()) / 100;
		iValue += kEvent.getHappyTurns() * 10;
		iValue += kEvent.getCulture() / 2;
	}
	else if (!kEvent.isOtherPlayerCityEffect())
	{
		int iPerTurnValue = 0;
		iPerTurnValue += iNumCities * ((iHappy * 4) + (kEvent.getHappy() * 8));
		iPerTurnValue += iNumCities * ((iHealth * 3) + (kEvent.getHealth() * 6));

		iValue += (iPerTurnValue * 20 * iGameSpeedPercent) / 100;

		iValue += (kEvent.getFood() * iNumCities);
		iValue += (kEvent.getFoodPercent() * iNumCities) / 4;
		iValue += (kEvent.getPopulationChange() * iNumCities * 40);
		iValue -= (iNumCities * kEvent.getHurryAnger() * 6 * GC.getDefineINT(CvGlobals::HURRY_ANGER_DIVISOR) *
				GC.getInfo(GC.getGame().getGameSpeedType()).getHurryConscriptAngerPercent()) / 100;
		iValue += iNumCities * kEvent.getHappyTurns() * 10;
		iValue += iNumCities * kEvent.getCulture() / 2;
	}

	int iBonusValue = 0;
	if (NO_BONUS != kEvent.getBonus())
	{
		//iBonusValue = AI_bonusVal((BonusTypes)kEvent.getBonus());
		iBonusValue = AI_bonusVal((BonusTypes)kEvent.getBonus(), 0, true); // K-Mod
	}

	if (NULL != pPlot)
	{
		if (kEvent.getFeatureChange() != 0)
		{
			int iOldFeatureValue = 0;
			int iNewFeatureValue = 0;
			if (pPlot->isFeature())
			{
				//*grumble* who tied feature production to builds rather than the feature...
				iOldFeatureValue = GC.getInfo(pPlot->getFeatureType()).getHealthPercent();
				if (kEvent.getFeatureChange() > 0)
				{
					FeatureTypes eFeature = (FeatureTypes)kEvent.getFeature();
					FAssert(eFeature != NO_FEATURE);
					if (eFeature != NO_FEATURE)
					{
						iNewFeatureValue = GC.getInfo(eFeature).getHealthPercent();
					}
				}

				iValue += ((iNewFeatureValue - iOldFeatureValue) * iGameSpeedPercent) / 100;
			}
		}

		if (kEvent.getImprovementChange() > 0)
		{
			iValue += (30 * iGameSpeedPercent) / 100;
		}
		else if (kEvent.getImprovementChange() < 0)
		{
			iValue -= (30 * iGameSpeedPercent) / 100;
		}

		if (kEvent.getRouteChange() > 0)
		{
			iValue += (10 * iGameSpeedPercent) / 100;
		}
		else if (kEvent.getRouteChange() < 0)
		{
			iValue -= (10 * iGameSpeedPercent) / 100;
		}

		if (kEvent.getBonusChange() > 0)
		{
			iValue += (iBonusValue * 15 * iGameSpeedPercent) / 100;
		}
		else if (kEvent.getBonusChange() < 0)
		{
			iValue -= (iBonusValue * 15 * iGameSpeedPercent) / 100;
		}

		for (int i = 0; i < NUM_YIELD_TYPES; ++i)
		{
			if (kEvent.getPlotExtraYield(i) != 0)
			{
				if (pPlot->getWorkingCity() != NULL)
				{
					FAssertMsg(pPlot->getWorkingCity()->getOwner() == getID(), "Event creates a boni for another player?");
					aiYields[i] += kEvent.getPlotExtraYield(i);
				}
				else
				{
					iValue += (20 * 8 * kEvent.getPlotExtraYield(i) * iGameSpeedPercent) / 100;
				}
			}
		}
	}

	if (NO_BONUS != kEvent.getBonusRevealed())
	{
		iValue += (iBonusValue * 10 * iGameSpeedPercent) / 100;
	}

	if (NULL != pUnit)
	{
		iValue += (2 * pUnit->baseCombatStr() * kEvent.getUnitExperience() * GC.getInfo(GC.getGame().getGameSpeedType()).getTrainPercent()) / 100;

		iValue -= 10 * kEvent.getUnitImmobileTurns();
	}

	{
		int iPromotionValue = 0;
		for (int i = 0; i < GC.getNumUnitCombatInfos(); ++i)
		{
			if (NO_PROMOTION != kEvent.getUnitCombatPromotion(i))
			{
				FOR_EACH_UNIT(pLoopUnit, *this)
				{
					if (pLoopUnit->getUnitCombatType() == i)
					{
						if (!pLoopUnit->isHasPromotion((PromotionTypes)kEvent.getUnitCombatPromotion(i)))
						{
							iPromotionValue += 5 * pLoopUnit->baseCombatStr();
						}
					}
				}

				iPromotionValue += iNumCities * 50;
			}
		}

		iValue += (iPromotionValue * iGameSpeedPercent) / 100;
	}

	if (kEvent.getFreeUnitSupport() != 0)
	{
		iValue += (20 * kEvent.getFreeUnitSupport() * iGameSpeedPercent) / 100;
	}

	if (kEvent.getInflationModifier() != 0)
	{
		iValue -= (20 * kEvent.getInflationModifier() * calculatePreInflatedCosts() * iGameSpeedPercent) / 100;
	}

	if (kEvent.getSpaceProductionModifier() != 0)
	{
		iValue += ((20 + iNumCities) * getSpaceProductionModifier() * iGameSpeedPercent) / 100;
	}

	int iOtherPlayerAttitudeWeight = 0;
	if (kTriggeredData.m_eOtherPlayer != NO_PLAYER &&
			kTriggeredData.m_eOtherPlayer != BARBARIAN_PLAYER) // advc.003n
	{
		iOtherPlayerAttitudeWeight = AI_getAttitudeWeight(kTriggeredData.m_eOtherPlayer);
		iOtherPlayerAttitudeWeight += 10 - GC.getGame().getSorenRandNum(20, "AI event value attitude");
	}

	//Religion
	if (kTriggeredData.m_eReligion != NO_RELIGION)
	{
		ReligionTypes eReligion = kTriggeredData.m_eReligion;

		int iReligionValue = 15;

		if (getStateReligion() == eReligion)
		{
			iReligionValue += 15;
		}
		if (hasHolyCity(eReligion))
		{
			iReligionValue += 15;
		}

		iValue += (kEvent.getConvertOwnCities() * iReligionValue * iGameSpeedPercent) / 100;

		if (kEvent.getConvertOtherCities() > 0)
		{
			//Don't like them much = fairly indifferent, hate them = negative.
			iValue += (kEvent.getConvertOtherCities() * (iOtherPlayerAttitudeWeight + 50) * iReligionValue * iGameSpeedPercent) / 15000;
		}
	}

	if (NO_PLAYER != kTriggeredData.m_eOtherPlayer)
	{
		CvPlayerAI& kOtherPlayer = GET_PLAYER(kTriggeredData.m_eOtherPlayer);

		int iDiploValue = 0;
		// BtS comment:
		/*  if we like this player then value positive attitude, if however we
			really hate them then actually value negative attitude. */
		/*iDiploValue += ((iOtherPlayerAttitudeWeight + 50) *
				kEvent.getAttitudeModifier() *
				GET_PLAYER(kTriggeredData.m_eOtherPlayer).getPower()) /
				std::max(1, getPower());*/
		/*  <advc.104z> Replacing the above. iOtherPlayerAttitudeWeight <= 50
			needs to be handled differently. And use team power instead of
			player power. Tagging advc.001 b/c the BtS code is probably not
			working as had been intended. */
		int iAttitudeMod = kEvent.getAttitudeModifier();
		if(iAttitudeMod != 0)
		{
			double tempValue = iOtherPlayerAttitudeWeight + 50;
			double powRatio = GET_TEAM(kTriggeredData.m_eOtherPlayer).getPower(true) /
					(double)std::max(1, GET_TEAM(getTeam()).getPower(true));
			if(tempValue > 0)
				tempValue *= powRatio;
			else tempValue += 100 * (powRatio - 1);
			/*  Make sure that angering a more powerful team is never treated as a
				good thing. */
			if(iAttitudeMod < 0 && tempValue < 0 && powRatio > 1)
				tempValue = 1;
			iDiploValue += ::round(tempValue * iAttitudeMod);
		} // </advc.104z>
		if (kEvent.getTheirEnemyAttitudeModifier() != 0)
		{
			//Oh wow this sure is mildly complicated.
			TeamTypes eWorstEnemy = GET_TEAM(GET_PLAYER(kTriggeredData.m_eOtherPlayer).getTeam()).AI_getWorstEnemy();
			if (NO_TEAM != eWorstEnemy && eWorstEnemy != getTeam())
			{
				int iThirdPartyAttitudeWeight = kTeam.AI_getAttitudeWeight(eWorstEnemy);
				//If we like both teams, we want them to get along.
				//If we like otherPlayer but not enemy (or vice-verca), we don't want them to get along.
				//If we don't like either, we don't want them to get along.
				//Also just value stirring up trouble in general.
				int iThirdPartyDiploValue = 50 * kEvent.getTheirEnemyAttitudeModifier();
				iThirdPartyDiploValue *= (iThirdPartyAttitudeWeight - 10);
				iThirdPartyDiploValue *= (iOtherPlayerAttitudeWeight - 10);
				iThirdPartyDiploValue /= 10000;

				if (iOtherPlayerAttitudeWeight < 0 && iThirdPartyAttitudeWeight < 0)
				{
					iThirdPartyDiploValue *= -1;
				}

				iThirdPartyDiploValue /= 2;

				iDiploValue += iThirdPartyDiploValue;
			}
		}

		iDiploValue *= iGameSpeedPercent;
		iDiploValue /= 100;

		if (NO_BONUS != kEvent.getBonusGift())
		{
			/*int iBonusValue = -AI_bonusVal((BonusTypes)kEvent.getBonusGift(), -1);
			iBonusValue += (iOtherPlayerAttitudeWeight - 40) * kOtherPlayer.AI_bonusVal((BonusTypes)kEvent.getBonusGift(), +1);
			//Positive for friends, negative for enemies.
			iDiploValue += (iBonusValue * GC.getPEACE_TREATY_LENGTH()) / 60;*/ // BtS

			// K-Mod. The original code undervalued our loss of bonus by a factor of 100.
			iValue -= AI_bonusVal((BonusTypes)kEvent.getBonusGift(), -1) * GC.getDefineINT(CvGlobals::PEACE_TREATY_LENGTH) / 4;
			int iGiftValue = kOtherPlayer.AI_bonusVal((BonusTypes)kEvent.getBonusGift(), +1) * (iOtherPlayerAttitudeWeight - 40) / 100;
			iDiploValue += iGiftValue * GC.getDefineINT(CvGlobals::PEACE_TREATY_LENGTH) / 4;
			// K-Mod end
		}

		if (GC.getGame().isOption(GAMEOPTION_AGGRESSIVE_AI))
		{
			//What is this "relationships" thing?
			iDiploValue /= 2;
		}

		if (kEvent.isGoldToPlayer())
		{
			//If the gold goes to another player instead of the void, then this is a positive
			//thing if we like the player, otherwise it's a negative thing.
			int iGiftValue = (getEventCost(eEvent, kTriggeredData.m_eOtherPlayer, false) + getEventCost(eEvent, kTriggeredData.m_eOtherPlayer, true)) / 2;
			iGiftValue *= -iOtherPlayerAttitudeWeight;
			iGiftValue /= 110;

			iValue += iGiftValue;
		}

		if (kEvent.isDeclareWar())
		{
			// K-Mod. Veto declare war events which are against our character.
			// (This is moved from the start of this function, and rewritten.)
			TeamTypes eOtherTeam = GET_PLAYER(kTriggeredData.m_eOtherPlayer).getTeam();
			if (kEvent.isDeclareWar() && kTriggeredData.m_eOtherPlayer != NO_PLAYER)
			{
				if (kTeam.AI_refuseWar(eOtherTeam))
					return MIN_INT/100; // Note: the divide by 100 is just to avoid an overflow later on.
			}
			// K-Mod end

			/*int iWarValue = GET_TEAM(getTeam()).getDefensivePower(GET_PLAYER(kTriggeredData.m_eOtherPlayer).getTeam())
				- GET_TEAM(GET_PLAYER(kTriggeredData.m_eOtherPlayer).getTeam()).getPower(true);// / std::max(1, GET_TEAM(getTeam()).getDefensivePower());
			iWarValue -= 30 * AI_getAttitudeVal(kTriggeredData.m_eOtherPlayer);*/ // BtS

			// K-Mod. Note: the original code doesn't touch iValue.
			// So whatever I do here is completely new.

			// Note: the event will make the other team declare war on us.
			// Also, I've decided not to multiply this by number of turns or anything like that.
			// The war evaluation is rough, and optimistic... and besides, we can always declare war ourselves if we want to.
			int iWarValue = kTeam.AI_startWarVal(GET_PLAYER(kTriggeredData.m_eOtherPlayer).getTeam(), WARPLAN_ATTACKED);

			iValue += iWarValue;
			// K-Mod end
		}

		if (kEvent.getMaxPillage() > 0)
		{
			int iPillageValue = (40 * (kEvent.getMinPillage() + kEvent.getMaxPillage())) / 2;
			//If we hate them, this is good to do.
			iPillageValue *= 25 - iOtherPlayerAttitudeWeight;
			iPillageValue *= iGameSpeedPercent;
			iPillageValue /= 12500;

			iValue += iPillageValue; // K-Mod!
		}

		iValue += (iDiploValue * iGameSpeedPercent) / 100;
	}

	int iThisEventValue = iValue;
	//XXX THIS IS VULNERABLE TO NON-TRIVIAL RECURSIONS!
	//Event A effects Event B, Event B effects Event A
	for (int iEvent = 0; iEvent < GC.getNumEventInfos(); ++iEvent)
	{
		if (kEvent.getAdditionalEventChance(iEvent) > 0)
		{
			if (iEvent == eEvent)
			{
				//Infinite recursion is not our friend.
				//Fortunately we have the event value for this event - sans values of other events
				//disabled or cleared. Hopefully no events will be that complicated...
				//Double the value since it's recursive.
				iValue += (kEvent.getAdditionalEventChance(iEvent) * iThisEventValue) / 50;
			}
			else
			{
				iValue += (kEvent.getAdditionalEventChance(iEvent) * AI_eventValue((EventTypes)iEvent, kTriggeredData)) / 100;
			}
		}

		if (kEvent.getClearEventChance(iEvent) > 0)
		{
			if (iEvent == eEvent)
			{
				iValue -= (kEvent.getClearEventChance(iEvent) * iThisEventValue) / 50;
			}
			else
			{
				iValue -= (kEvent.getClearEventChance(iEvent) * AI_eventValue((EventTypes)iEvent, kTriggeredData)) / 100;
			}
		}
	}

	iValue *= 100 + GC.getGame().getSorenRandNum(20, "AI Event choice");
	iValue /= 100;

	return iValue;
}

EventTypes CvPlayerAI::AI_chooseEvent(int iTriggeredId) const
{
	EventTriggeredData* pTriggeredData = getEventTriggered(iTriggeredId);
	if (pTriggeredData == NULL)
		return NO_EVENT;

	CvEventTriggerInfo& kTrigger = GC.getInfo(pTriggeredData->m_eTrigger);

	//int iBestValue = -MAX_INT;
	int iBestValue = MIN_INT; // K-Mod  (advc: K-Mod had used INT_MIN)
	EventTypes eBestEvent = NO_EVENT;
	for (int i = 0; i < kTrigger.getNumEvents(); i++) // advc: Restructured this loop
	{
		EventTypes eEvent = (EventTypes)kTrigger.getEvent(i);
		if (eEvent != NO_EVENT && canDoEvent(eEvent, *pTriggeredData))
		{
			int iValue = AI_eventValue(eEvent, *pTriggeredData);
			if (iValue > iBestValue)
			{
				iBestValue = iValue;
				eBestEvent = eEvent;
			}
		}
	}
	return eBestEvent;
}

void CvPlayerAI::AI_doSplit(/* advc.104r: */ bool bForce)  // advc: some style changes
{
	if (!canSplitEmpire())
		return;

	std::map<int,int> mapAreaValues;
	FOR_EACH_AREA(pLoopArea)
		mapAreaValues[pLoopArea->getID()] = 0;
	FOR_EACH_CITYAI(pLoopCity, *this)
	{	//mapAreaValues[pLoopCity->getArea().getID()] += pLoopCity->AI_cityValue();
		// K-Mod
		// we don't consider splitting empire while there is a land war. (this check use to be in AI_cityValue)
		if (!AI_isLandWar(pLoopCity->getArea()))
			mapAreaValues[pLoopCity->getArea().getID()] += pLoopCity->AI_cityValue();
		// K-Mod end
	}

	CvMap const& m = GC.getMap();
	std::map<int,int>::iterator it;
	for (it = mapAreaValues.begin(); it != mapAreaValues.end(); ++it)
	{
		if (it->second >= 0 /* advc.104r: */ && !bForce)
			continue;

		CvArea& kArea = *m.getArea(it->first);
		if (!canSplitArea(kArea))
			continue;

		splitEmpire(kArea);

		FOR_EACH_UNIT_VAR(pUnit, *this)
		{
			if (!pUnit->isArea(kArea))
				continue;
			TeamTypes ePlotTeam = pUnit->getPlot().getTeam();
			if (NO_TEAM != ePlotTeam)
			{
				CvTeam& kPlotTeam = GET_TEAM(ePlotTeam);
				if (kPlotTeam.isVassal(getTeam()) && GET_TEAM(getTeam()).isParent(ePlotTeam))
					pUnit->gift();
			}
		}
		break;
	}
}

// <advc.099b>
double CvPlayerAI::AI_exclusiveRadiusWeight(int iDist) const
{
	if(iDist > 2)
		return 0;
	double distMultiplier = 1.5;
	if(iDist == 0 || iDist == 1)
		distMultiplier = 2;
	else if(iDist == 2)
		distMultiplier = 1;
	double base = 1 - (distMultiplier * GC.getDefineINT(CvGlobals::CITY_RADIUS_DECAY) /
			1000.0);
	FAssertMsg(base > 0, "CITY_RADIUS_DECAY too great; negative base for std::pow.");
	/*  Between 0 and 1. Expresses our confidence about winning culturally
		contested tiles that are within the working radius of our cities
		exclusively. Since the decay of tile culture isn't based on game speed,
		that confidence is greater on the slower speed settings. */
	return 1 - std::pow(base, 25 * GC.getInfo(GC.getGame().
			getGameSpeedType()).getGoldenAgePercent() / 100.0);
} // </advc.099b>


void CvPlayerAI::AI_launch(VictoryTypes eVictory)
{
	if (GET_TEAM(getTeam()).isHuman())
		return;

	if (!GET_TEAM(getTeam()).canLaunch(eVictory))
		return;

	int iBestArrival = MAX_INT;
	TeamTypes eBestTeam = NO_TEAM;

	for (int iTeam = 0; iTeam < MAX_CIV_TEAMS; ++iTeam)
	{
		if (iTeam != getTeam())
		{
			CvTeam& kTeam = GET_TEAM((TeamTypes)iTeam);
			if (kTeam.isAlive())
			{
				int iCountdown = kTeam.getVictoryCountdown(eVictory);
				if (iCountdown > 0)
				{
					if (iCountdown < iBestArrival)
					{
						iBestArrival = iCountdown;
						eBestTeam = (TeamTypes)iTeam;
					}
					/*  BETTER_BTS_AI_MOD, 12/07/09, r_rolo1 & jdog5000: commented out b/c
						Other civs capital could still be captured, might as well send spaceship */
					/*if (iCountdown < GET_TEAM(getTeam()).getVictoryDelay(eVictory) && kTeam.getLaunchSuccessRate(eVictory) == 100) {
						bLaunch = false;
						break;
					}*/
				}
			}
		}
	}
	bool bLaunch = true;
	if (NO_TEAM == eBestTeam || iBestArrival > GET_TEAM(getTeam()).getVictoryDelay(eVictory))
	{
		if (GET_TEAM(getTeam()).getLaunchSuccessRate(eVictory) < 100)
			bLaunch = false;
	}

	if (bLaunch)
		launch(eVictory);
}

void CvPlayerAI::AI_doCheckFinancialTrouble()
{
	// if we just ran into financial trouble this turn
	bool bFinancialTrouble = AI_isFinancialTrouble();
	if (bFinancialTrouble != m_bWasFinancialTrouble)
	{
		if (bFinancialTrouble)
		{
			int iGameTurn = GC.getGame().getGameTurn();

			// only reset at most every 10 turns
			if (iGameTurn > m_iTurnLastProductionDirty + 10)
			{
				// redeterimine the best things to build in each city
				AI_makeProductionDirty();
				m_iTurnLastProductionDirty = iGameTurn;
			}
		}

		m_bWasFinancialTrouble = bFinancialTrouble;
	}
}

// bool CvPlayerAI::AI_disbandUnit(int iExpThreshold, bool bObsolete)
// K-Mod, deleted this function.

// BETTER_BTS_AI_MOD, Victory Strategy AI, 04/25/10, jdog5000: START
// heavily edited by K-Mod and bbai
int CvPlayerAI::AI_calculateCultureVictoryStage(  // advc: a few style changes
	int iCountdownThresh) const // advc.115
{
	PROFILE_FUNC();

	CvGame const& g = GC.getGame();
	if (!isHuman() && // advc.115d
			!g.isOption(GAMEOPTION_RISE_FALL) && // advc.700
			!GC.getDefineBOOL("BBAI_VICTORY_STRATEGY_CULTURE"))
		return 0;

	if (!g.culturalVictoryValid())
		return 0;

	// Necessary as capital city pointer is used later
	if (getCapitalCity() == NULL)
		return 0;

	int iHighCultureCount = 0;
	int iCloseToLegendaryCount = 0;
	int iLegendaryCount = 0;
	// K-Mod
	int iEraThresholdPercent = 80 - (AI_getStrategyRand(1) % 2)*20; // some (arbitrary) fraction of the game, after which we get more serious. (cf. old code)
	int iLegendaryCulture = g.getCultureThreshold((CultureLevelTypes)(GC.getNumCultureLevelInfos() - 1));
	int iVictoryCities = g.culturalVictoryNumCultureCities();

	int iHighCultureMark = 300; // turns
	iHighCultureMark *= (3*GC.getNumEraInfos() - 2*getCurrentEra());
	iHighCultureMark /= std::max(1, 3*GC.getNumEraInfos());
	iHighCultureMark *= GC.getInfo(g.getGameSpeedType()).getVictoryDelayPercent();
	iHighCultureMark /= 100;

	int iGoldCommercePercent = AI_estimateBreakEvenGoldPercent();

	std::vector<int> countdownList;
	// K-Mod end

	FOR_EACH_CITY(pLoopCity, *this)
	{
		if (pLoopCity->getCultureLevel() >= (g.culturalVictoryCultureLevel() - 1))
		{
			// K-Mod. Try to tell us what the culture would be like if we were to turn up the slider. (cf. AI_updateCommerceWeights)
			// (current culture rate, minus what we're current getting from raw commerce, plus what we would be getting from raw commerce at maximum percentage.)
			int iEstimatedRate = pLoopCity->getCommerceRate(COMMERCE_CULTURE);
			iEstimatedRate += (100 - iGoldCommercePercent - getCommercePercent(COMMERCE_CULTURE)) * pLoopCity->getYieldRate(YIELD_COMMERCE) * pLoopCity->getTotalCommerceRateModifier(COMMERCE_CULTURE) / 10000;
			int iCountdown = (iLegendaryCulture - pLoopCity->getCulture(getID())) / std::max(1, iEstimatedRate);
			countdownList.push_back(iCountdown);
			if (iCountdown < iHighCultureMark)
				iHighCultureCount++;

			// <advc.115> Threshold was 50%, now 67%, 60% for human.
			double thresh = 0.67;
			if(isHuman())
				thresh = 0.6;
			if (pLoopCity->getCulture(getID()) > ::round(thresh * pLoopCity->getCultureThreshold(
					g.culturalVictoryCultureLevel()))) // </ advc.115>
				iCloseToLegendaryCount++;

			// is already there?
			if (pLoopCity->getCulture(getID()) > pLoopCity->getCultureThreshold(g.culturalVictoryCultureLevel()))
				iLegendaryCount++;
		}
	}

	if (iLegendaryCount >= iVictoryCities)
	{
		// Already won, keep playing culture heavy but do some tech to keep pace if human wants to keep playing
		return 3;
	}

	// K-Mod. Originally, the BBAI condition for culture4 was just "iCloseToLegendaryCount >= iVictoryCities".
	// I think that is far too simplistic for the AI players; so I've removed that clause.
	if (isHuman()) //&& !g.isDebugMode()) // advc.115d (commented out)
	{
		if (iCloseToLegendaryCount >= iVictoryCities)
			return 4;
		else if (getCommercePercent(COMMERCE_CULTURE) > 50)
			return 3;
		return 0;
	} // K-Mod end

	// K-Mod, disabling some stuff.
	// It is still possible to get a cultural victory in advanced start games.
	// and moving your capital city certainly does not indicate that you shouldn't go for a cultural victory!
	// ... and colonies don't get their capital on turn 1 anyway.
	/*if (g.getStartEra() > 1)
		return 0;
	if (getCapitalCity()->getGameTurnFounded() > (10 + g.getStartTurn())) {
		if (iHighCultureCount < GC.getGame().culturalVictoryNumCultureCities()) {
			//the loss of the capital indicates it might be a good idea to abort any culture victory
			return 0;
		}
	}*/ // BtS

	// If we aren't already obviously close to a cultural victory, then we need to consider whether or not to aim for culture at all.
	if (iCloseToLegendaryCount < iVictoryCities)
	{
		int iValue = GC.getInfo(getPersonalityType()).getCultureVictoryWeight();

		//if (g.isOption(GAMEOPTION_ALWAYS_PEACE))
		if (!GET_TEAM(getTeam()).AI_isWarPossible()) // advc.001j
			iValue += 30;

		iValue += (g.isOption(GAMEOPTION_AGGRESSIVE_AI)
				? -10 : 0); // advc.019: was ?-20:0

		// K-Mod
		iValue += 30 * std::min(iCloseToLegendaryCount, iVictoryCities + 1);
		iValue += 10 * std::min(iHighCultureCount, iVictoryCities + 1);
		iValue += getCurrentEra() < (iEraThresholdPercent-20)*GC.getNumEraInfos()/100; // prep for culture in the early game, just in case.
		// K-Mod end
		if (iValue > 20 && getNumCities() >= iVictoryCities)
		{
			iValue += 5*countHolyCities(); // was 10*
			// K-Mod: be wary of going for a cultural victory if everyone hates us.
			int iScore = 0;
			int iTotalPop = 0;
			// advc.115e: Count our own population too
			for (PlayerIter<FREE_MAJOR_CIV,KNOWN_TO> it(getTeam()); it.hasNext(); ++it)
			{
				CvPlayerAI const& kPlayer = *it;
				int const iPop = kPlayer.getTotalPopulation();
				iTotalPop += iPop;
				// <advc.115e>
				if (kPlayer.getTeam() == getTeam())
				{
					iScore += 10 * iPop;
					continue;
				} // </advc.115e>
				AttitudeTypes eAttitude = (kPlayer.isHuman() ? AI_getAttitude(kPlayer.getID()) :
						// advc.115e: Use kPlayer's attitude (unless it's human)
						kPlayer.AI_getAttitude(getID()));
				if (eAttitude <= ATTITUDE_ANNOYED)
					iScore -= 20 * iPop;
				else if (eAttitude >= ATTITUDE_PLEASED)
					iScore += 10 * iPop;
			}
			iValue += iScore / std::max(1, iTotalPop);
			iValue -= AI_atVictoryStage(AI_VICTORY_CONQUEST1) ? 20 : 0;
			iValue -= AI_atVictoryStage(AI_VICTORY_SPACE2) ? 10 : 0;
			iValue -= AI_atVictoryStage(AI_VICTORY_SPACE3) ? 20 : 0;
			// K-Mod end
		}
		/*if (isAVassal() && getNumCities() > 5){
			int iReligionCount = countTotalHasReligion();
			if ((iReligionCount * 100) / getNumCities() > 250){
				iValue += 1;
				iValue += (2 * iReligionCount + 1) / getNumCities();
			}
		}*/
		// <advc.109>
		if(GET_TEAM(getTeam()).AI_isLonely())
			iValue -= 25; // </advc.109>
		//int iNonsense = AI_getStrategyRand() + 10;
		iValue += (AI_getStrategyRand(0) % 70); // advc.115: was 100

		if (iValue < 100)
			return 0;
	}

	int iWinningCountdown = MAX_INT;
	if ((int)countdownList.size() >= iVictoryCities && iVictoryCities > 0)
	{
		std::partial_sort(countdownList.begin(), countdownList.begin() + iVictoryCities, countdownList.end());
		iWinningCountdown = countdownList[iVictoryCities-1];
	}
	if (iCloseToLegendaryCount >= iVictoryCities || getCurrentEra() >=
			//(GC.getNumEraInfos() - (2 + AI_getStrategyRand(1) % 2))
			// K-Mod (note: this matches the above)
			iEraThresholdPercent*GC.getNumEraInfos()/100)
	{
		bool bAt3 = false;
		// if we have enough high culture cities, go to stage 3
		if (iHighCultureCount >= iVictoryCities)
			bAt3 = true;
		// if we have a lot of religion, may be able to catch up quickly
		/* disabled by K-Mod
		if (countTotalHasReligion() >= getNumCities() * 3) {
			if (getNumCities() >= g.culturalVictoryNumCultureCities())
				bAt3 = true;
		}*/

		if(bAt3)
		{
			/*if (AI_cultureVictoryTechValue(getCurrentResearch()) < 100)
				return 4;*/ // BtS
			// K-Mod. Do full culture if our winning countdown is below the countdown target.
			/*  <advc.115> Was 180. Now set to the countdownThresh param if
				it is provided, otherwise 115. */
			int iCountdownTarget = 115;
			if(iCountdownThresh >= 0)
				iCountdownTarget = iCountdownThresh; // </advc.115>
			{
				// The countdown target depends on what other victory strategies we have in mind. The target is 180 turns * 100 / iDenominator.
				// For example, if we're already at war, and going for conquest 4, the target countdown is then ~ 180 * 100 / 470 == 38 turns.
				// Note: originally culture 4 was blocked completely by stage 4 of any other victory strategy. But this is no longer the case.
				int iDemoninator = 100;
				iDemoninator += 20 * AI_atVictoryStage(AI_VICTORY_CONQUEST1 | AI_VICTORY_DOMINATION1);
				iDemoninator += 50 * AI_atVictoryStage(AI_VICTORY_CONQUEST2);
				iDemoninator += 50 * AI_atVictoryStage(AI_VICTORY_CONQUEST3);
				iDemoninator += 80 * AI_atVictoryStage(AI_VICTORY_DOMINATION3);
				iDemoninator += 70 * AI_atVictoryStage(AI_VICTORY_SPACE2);
				iDemoninator += 80 * AI_atVictoryStage(AI_VICTORY_SPACE3);
				iDemoninator += 50 * AI_atVictoryStage(AI_VICTORY_SPACE4);
				iDemoninator += AI_cultureVictoryTechValue(getCurrentResearch()) > 100 ? 80 : 0;
				iDemoninator += AI_isDoStrategy(AI_STRATEGY_GET_BETTER_UNITS)? 80 : 0;
				//if (GET_TEAM(getTeam()).getAnyWarPlanCount(true) > 0)
				if(AI_isFocusWar()) // advc.105
				{
					iDemoninator += 50;
					iDemoninator += 200 * AI_atVictoryStage(AI_VICTORY_CONQUEST4 | AI_VICTORY_DOMINATION4);
					int iWarSuccessRating = GET_TEAM(getTeam()).AI_getWarSuccessRating();
					iDemoninator += iWarSuccessRating < 0 ? 10 - 2*iWarSuccessRating : 0;
				}
				// and a little bit of personal variation.
				iDemoninator += 50 - GC.getInfo(getPersonalityType()).getCultureVictoryWeight();

				iCountdownTarget *= GC.getInfo(g.getGameSpeedType()).getVictoryDelayPercent();
				iCountdownTarget /= std::max(1, iDemoninator);
			}
			if (iWinningCountdown < iCountdownTarget)
				return 4;
			// K-Mod end

			return 3;
		}
	}

	//if (getCurrentEra() >= ((GC.getNumEraInfos() / 3) + iNonsense % 2))
	if (getCurrentEra() >= ((GC.getNumEraInfos() / 3) + AI_getStrategyRand(1) % 2) || iHighCultureCount >= iVictoryCities-1)
	{
		if (iHighCultureCount < getCurrentEra() + iVictoryCities - GC.getNumEraInfos())
			return 1;
		return 2;
	}

	return 1;
}

int CvPlayerAI::AI_calculateSpaceVictoryStage() const
{
	if (!isHuman() && // advc.115d
			!GC.getGame().isOption(GAMEOPTION_RISE_FALL) && // advc.700
			!GC.getDefineBOOL("BBAI_VICTORY_STRATEGY_SPACE"))
		return 0;

	if (getCapitalCity() == NULL)
		return 0;

	// Better way to determine if spaceship victory is valid?
	VictoryTypes eSpace = NO_VICTORY;
	for (int iI = 0; iI < GC.getNumVictoryInfos(); iI++)
	{
		if (GC.getGame().isVictoryValid((VictoryTypes) iI))
		{
			CvVictoryInfo& kVictoryInfo = GC.getInfo((VictoryTypes) iI);
			if (kVictoryInfo.getVictoryDelayTurns() > 0)
			{
				eSpace = (VictoryTypes)iI;
				break;
			}
		}
	}

	if (eSpace == NO_VICTORY)
		return 0;

	// If have built Apollo, then the race is on!
	bool bHasApollo = false;
	bool bNearAllTechs = true;
	for (int iI = 0; iI < GC.getNumProjectInfos(); iI++)
	{
		if (GC.getInfo((ProjectTypes)iI).getVictoryPrereq() == eSpace)
		{
			if (GET_TEAM(getTeam()).getProjectCount((ProjectTypes)iI) > 0)
			{
				bHasApollo = true;
			}
			else
			{
				if (!GET_TEAM(getTeam()).isHasTech((TechTypes)GC.getInfo((ProjectTypes)iI).getTechPrereq()))
				{
					if (!isResearchingTech((TechTypes)GC.getInfo((ProjectTypes)iI).getTechPrereq()))
					{
						bNearAllTechs = false;
					}
				}
			}
		}
	}
	/*  <advc.115> All the parts together cost some 15000 to 20000 production on
		Normal speed. Can't be sure that production will increase much in the future;
		most tech that could boost production is already known by the time
		Apollo is built. If it'll take more than 100 turns, then it's hopeless
		(and not all production is going to go into spaceship parts either). */
	int iTotalProduction = ::round((estimateYieldRate(YIELD_PRODUCTION) * 100) /
			std::max(1, GC.getInfo(GC.getGame().getGameSpeedType()).
			getCreatePercent()));
	bool bEnoughProduction = (iTotalProduction > 240);
	// </advc.115>
	if (bHasApollo)
	{
		if (bNearAllTechs)
		{
			bool bOtherLaunched = false;
			// K-Mod. (just tidying up a bit.)
			int iOurCountdown = GET_TEAM(getTeam()).getVictoryCountdown(eSpace);
			for (int iTeam = 0; iTeam < MAX_CIV_TEAMS; iTeam++)
			{
				if (iTeam == getTeam())
					continue;

				if (GET_TEAM((TeamTypes)iTeam).getVictoryCountdown(eSpace) >= 0 &&
					(iOurCountdown < 0 || GET_TEAM((TeamTypes)iTeam).getVictoryCountdown(eSpace) <= iOurCountdown))
				{
					bOtherLaunched = true;
					break;
				}
			}
			// K-Mod end

			if (!bOtherLaunched)
				return 4;

			if (bEnoughProduction || iOurCountdown > 0) // advc.115
				return 3;
		}

		/*if (GET_TEAM(getTeam()).getBestKnownTechScorePercent() > (m_iVictoryStrategyHash & AI_VICTORY_SPACE3 ? 80 : 85))
			return 3;*/ // BBAI
		// K-Mod. I don't think that's a good way to do it.
		// instead, compare our spaceship progress to that of our rivals.
		int iOurProgress = 0;
		for (int i = 0; i < GC.getNumProjectInfos(); i++)
		{
			if (GC.getInfo((ProjectTypes)i).isSpaceship())
			{
				int iBuilt = GET_TEAM(getTeam()).getProjectCount((ProjectTypes)i);

				if (iBuilt > 0 || GET_TEAM(getTeam()).isHasTech((TechTypes)(GC.getInfo((ProjectTypes)i).getTechPrereq())))
					iOurProgress += 2 + iBuilt;
			}
		}
		int iKnownTeams = 0;
		int iSpaceTeams = 0; // teams ahead of us in the space race
		for (int iTeam = 0; iTeam < MAX_CIV_TEAMS; iTeam++)
		{
			const CvTeam& kLoopTeam = GET_TEAM((TeamTypes)iTeam);
			if (iTeam != getTeam() && kLoopTeam.isAlive() && kLoopTeam.isHasMet(getTeam()))
			{
				int iProgress = 0;
				for (int i = 0; i < GC.getNumProjectInfos(); i++)
				{
					if (GC.getInfo((ProjectTypes)i).isSpaceship())
					{
						int iBuilt = kLoopTeam.getProjectCount((ProjectTypes)i);

						if (iBuilt > 0 || kLoopTeam.isHasTech((TechTypes)(GC.getInfo((ProjectTypes)i).getTechPrereq())))
							iProgress += 2 + iBuilt;
					}
				}
				iKnownTeams++;
				if (iProgress > iOurProgress)
					iSpaceTeams++;
			}
		}
		if (200 * iSpaceTeams / (1+iKnownTeams)
				<= GC.getInfo(getPersonalityType()).getSpaceVictoryWeight() +
				AI_getStrategyRand(3) % 100 && // note, correlated with number used lower down.
				(bEnoughProduction || iOurProgress > 10)) // advc.115
			return 3;
		// K-Mod end
	}

	if (isHuman()) //&& !GC.getGame().isDebugMode()) // advc.115d (commented out)
		return 0;
	// <advc.115>
	if (!bEnoughProduction)
		return 0; // </advc.115>

	// If can't build Apollo yet, then consider making player push for this victory type
	{
		int iValue = GC.getInfo(getPersonalityType()).getSpaceVictoryWeight();

		//if (GC.getGame().isOption(GAMEOPTION_ALWAYS_PEACE))
		if (!GET_TEAM(getTeam()).AI_isWarPossible()) // advc.003j
			iValue += 30;

		if(isAVassal())
			iValue += 20;

		iValue += (GC.getGame().isOption(GAMEOPTION_AGGRESSIVE_AI)
				? -10 : 0); // advc.019: was ?-20:0

		// K-Mod: take into account how we're doing in tech
		int iScore = 0;
		int iTotalPop = 0;
		for (int iI = 0; iI < MAX_CIV_PLAYERS; iI++)
		{
			const CvPlayer& kLoopPlayer = GET_PLAYER((PlayerTypes)iI);
			if (iI != getID() && kLoopPlayer.isAlive() && GET_TEAM(getTeam()).isHasMet(kLoopPlayer.getTeam()))
			{
				iTotalPop += kLoopPlayer.getTotalPopulation();
				iScore += (getTechScore() > kLoopPlayer.getTechScore() ? 20 : -20) * kLoopPlayer.getTotalPopulation();
			}
		}
		iValue += iScore / std::max(1, iTotalPop);
		// K-Mod end

		//int iNonsense = AI_getStrategyRand() + 50;
		iValue += (AI_getStrategyRand(3) % 100);

		if (iValue >= 100 /* advc.115: */ || bNearAllTechs && iTotalProduction > 1000)
		{
			if (getCurrentEra() >= GC.getNumEraInfos() - 3)
				return 2;
			return 1;
		}
	}

	return 0;
}

// This function has been completely rewritten for K-Mod
int CvPlayerAI::AI_calculateConquestVictoryStage() const
{
	PROFILE_FUNC();

	CvGame const& g = GC.getGame(); // advc
	const CvTeamAI& kTeam = GET_TEAM(getTeam());

	// check for validity of conquest victory
	//if (g.isOption(GAMEOPTION_ALWAYS_PEACE)
	if (!kTeam.AI_isWarPossible() || // advc.001j
			isAVassal() || (/* advc.115d: */ !isHuman() &&
			!g.isOption(GAMEOPTION_RISE_FALL) && // advc.700
			!GC.getDefineBOOL("BBAI_VICTORY_STRATEGY_CONQUEST")))
		return 0;

	{
		bool bValid = false;

		for (VictoryTypes i = (VictoryTypes)0; !bValid && i < GC.getNumVictoryInfos(); i=(VictoryTypes)(i+1))
		{
			if (g.isVictoryValid(i) && GC.getInfo(i).isConquest())
				bValid = true;
		}
		if (!bValid)
			return 0;
	}

	/* Things to consider:
	 - personality (conquest victory weight)
	 - our relationship with other civs
	 - our current power and productivity
	 - our war record. (ideally, we'd look at wars won / lost, but that info is unavailable - so wars declared will have to do.)
	*/

	// first, gather some data.
	int iDoWs = 0, iKnownCivs = 0, iRivalPop = 0, iStartCivs = 0, iConqueredCivs = 0, iAttitudeWeight = 0;

	for (PlayerTypes i = (PlayerTypes)0; i < MAX_CIV_PLAYERS; i=(PlayerTypes)(i+1))
	{
		const CvPlayerAI& kLoopPlayer = GET_PLAYER(i);
		const CvTeamAI& kLoopTeam = GET_TEAM(kLoopPlayer.getTeam());

		if (!kLoopPlayer.isEverAlive() || kLoopPlayer.getTeam() == getTeam() || kLoopTeam.isMinorCiv())
			continue;

		iDoWs += kLoopPlayer.AI_getMemoryCount(getID(), MEMORY_DECLARED_WAR);
		/*  advc.130j: DoW memory counted times 3, but there's also decay now
			and CvTeamAI::forgiveEnemies, so let's go with 2.5 */
		iDoWs = (int)ceil(iDoWs / 2.5);

		if (kLoopPlayer.getParent() != NO_PLAYER)
			continue;

		iStartCivs++;

		if (!kLoopPlayer.isAlive() || kLoopTeam.isVassal(getTeam()))
		{
			iKnownCivs++; // eliminated civs are announced.
			iConqueredCivs++;
			continue;
		}

		if (!kTeam.isHasMet(kLoopPlayer.getTeam()))
			continue;

		iKnownCivs++;
		iRivalPop += kLoopPlayer.getTotalPopulation();
		iAttitudeWeight += kLoopPlayer.getTotalPopulation() * AI_getAttitudeWeight(i);
	}
	iAttitudeWeight /= std::max(1, iRivalPop);

	// (please excuse the fact that the AI magically knows the power of all teams.)
	int iRivalTeams = 0; // number of other teams we know
	int iRivalPower = 0; // sum of their power
	int iStrongerTeams = 0; // number of teams known to have greater power than us.
	int iOurPower = kTeam.getPower(true);
	// <advc.104c>
	int iFriends = 0;
	int iRemaining = 0; // </advc.104c>
	int iOffshoreRivals = 0; // advc.115
	for (TeamTypes i = (TeamTypes)0; i < MAX_CIV_TEAMS; i=(TeamTypes)(i+1))
	{
		const CvTeamAI& kLoopTeam = GET_TEAM(i);

		if (i == getTeam() || !kLoopTeam.isAlive() || kLoopTeam.isMinorCiv())
			continue; // <advc.104c>
		if(!kLoopTeam.isAVassal())
			iRemaining++; // </advc.104c>
		if (kTeam.isHasMet(i) && !kLoopTeam.isAVassal())
		{
			iRivalTeams++;
			int p = kLoopTeam.getPower(true);
			iRivalPower += p;
			if (p >= iOurPower)
				iStrongerTeams++;
			// <advc.104c>
			if(kTeam.AI_getAttitude(i) >= ATTITUDE_FRIENDLY)
				iFriends++; // </advc.104c>
			if(!kTeam.AI_isLandTarget(i))
				iOffshoreRivals++;
		}
	}
	int iAverageRivalPower = iRivalPower / std::max(1, iRivalTeams);
	bool bManyOffshoreRivals = 100 * iOffshoreRivals / std::max(1, iKnownCivs) >= 40;

	// Ok. Now, decide whether we want to consider a conquest victory.
	// This decision is based on situation and on personality.

	// personality & randomness
	int iValue = GC.getInfo(getPersonalityType()).getConquestVictoryWeight();
	//iValue += (g.isOption(GAMEOPTION_AGGRESSIVE_AI) ? 20 : 0);
	iValue += (AI_getStrategyRand(4) % 100);

	// stats
	bool bVeryStrong = iRivalTeams > 0 && iOurPower > 2*iAverageRivalPower && iOurPower - iAverageRivalPower > 100;
	bool bTopRank = iStrongerTeams < (iRivalTeams + 4) / 5;
	bool bWarmonger = iDoWs > iStartCivs/2 || (iKnownCivs >= iStartCivs && iDoWs > iRivalTeams*3/4);
	bool bHateful = iAttitudeWeight < -25;

	iValue += iDoWs > 1 || AI_isDoStrategy(AI_STRATEGY_CRUSH) ? 10 : 0;
	iValue += bVeryStrong ? 30 : 0;
	iValue += bTopRank && bHateful ? 30 : 0;
	iValue += bWarmonger ? 30 : 0;
	iValue += iRivalTeams > 1 && (iStrongerTeams > iRivalTeams/2 || iOurPower < iAverageRivalPower * 3/4) ? -30 : 0;
	iValue += iOurPower < iAverageRivalPower * 4/3 && iAttitudeWeight >= 50 ? -30 : 0;

	if ((iKnownCivs <= iConqueredCivs || iValue < 110) &&
			(g.countCivTeamsAlive() > 2 || iValue < 50)) // advc.115c
		return 0;

	// So, we have some interest in a conquest victory. Now we just have to decide how strong the interest is.

	/*  advc.115: The "level 1" comment below was apparently intended to mean
		that we're now already at level 1; the condition below leads to level 2.
		I'm changing the code so that the first condition is needed for level 1,
		the second for 2 etc. Could get the same effect by subtracting one level
		at the end, but I'm also changing some of the conditions. */
	// level 1
	if ((iDoWs > 0 || iRivalTeams == 1 || iConqueredCivs > iStartCivs/3) &&
			(bTopRank || bVeryStrong || bWarmonger ||
			(iConqueredCivs >= iKnownCivs/3 && bHateful)))
	{
		// level 2
		if ((bVeryStrong || bWarmonger || (bTopRank && bHateful)) && iKnownCivs >= iStartCivs*3/4 && iConqueredCivs >= iKnownCivs/3)
		{
			// level 3
			if (iKnownCivs >= iStartCivs && bTopRank &&
					!bManyOffshoreRivals && // advc.115
					(bVeryStrong || (bWarmonger && bHateful && iConqueredCivs >= iKnownCivs/2))
					/*  advc.104c: The ==1 might be exploitable; by keeping some
						insignificant civ in the game. Probably no problem. */
					&& (iFriends == 0 || (iFriends == iRemaining && iFriends == 1)))
			{
				// finally, before confirming level 4, check that there is at least one team that we can declare war on.
				// <advc.115>
				if(iConqueredCivs >= iKnownCivs/2 && kTeam.getNumCities() * 2 >= g.getNumCities())
				{ // </advc.115>
					for (TeamTypes i = (TeamTypes)0; i < MAX_CIV_TEAMS; i=(TeamTypes)(i+1))
					{
						const CvTeamAI& kLoopTeam = GET_TEAM(i);
						if (kLoopTeam.isAlive() && kTeam.isHasMet(i))
						{
							if (kTeam.AI_getWarPlan(i) != NO_WARPLAN ||
								(kTeam.canEventuallyDeclareWar(i) /*&& kTeam.AI_startWarVal(i, WARPLAN_TOTAL) > 0*/))
							{
								return 4;
							}
							// I'm still not sure about the WarVal thing, because that value actually depends on conquest victory flags.
						}
					}
				// <advc.115>
				}
				return 3; // </advc.115>
			}
			return 2; // advc.115: was 3
		}
		return 1; // advc.115: was 2
	}
	return 0; // advc.115: was 1
}

int CvPlayerAI::AI_calculateDominationVictoryStage() const
{
	CvGame const& g = GC.getGame(); // advc
	const CvTeamAI& kTeam = GET_TEAM(getTeam());
	//if (g.isOption(GAMEOPTION_ALWAYS_PEACE))
	if (!kTeam.AI_isWarPossible() || // advc.001j
			isAVassal() || (/* advc.115d: */ !isHuman() &&
			!g.isOption(GAMEOPTION_RISE_FALL) && // advc.700
			GC.getDefineINT("BBAI_VICTORY_STRATEGY_DOMINATION") <= 0))
		return 0;

	VictoryTypes eDomination = NO_VICTORY;
	int iValue = 0;
	for (int iI = 0; iI < GC.getNumVictoryInfos(); iI++)
	{
		if (g.isVictoryValid((VictoryTypes) iI))
		{
			CvVictoryInfo& kVictoryInfo = GC.getInfo((VictoryTypes) iI);
			if (kVictoryInfo.getLandPercent() > 0 && kVictoryInfo.getPopulationPercentLead())
			{
				eDomination = (VictoryTypes)iI;
				break;
			}
		}
	}

	if (eDomination == NO_VICTORY)
		return 0;

	int iPercentOfDomination = 0;
	int iOurPopPercent = (100 * kTeam.getTotalPopulation()) / std::max(1, g.getTotalPopulation());
	int iOurLandPercent = (100 * kTeam.getTotalLand()) / std::max(1, GC.getMap().getLandPlots());

	// <advc.104c>
	int iPopObjective = std::max(1, g.getAdjustedPopulationPercent(eDomination));
	int iLandObjective =std::max(1, g.getAdjustedLandPercent(eDomination));
	bool bBlockedByFriend = false;
	for(int i = 0; i < MAX_CIV_TEAMS; i++)
	{
		if(i == getTeam())
			continue;
		CvTeamAI& kLoopTeam = GET_TEAM((TeamTypes)i);
		if(!kLoopTeam.isAlive() || kLoopTeam.isMinorCiv() || kLoopTeam.isBarbarian() ||
				!kTeam.isHasMet(kLoopTeam.getID()) || kLoopTeam.isAVassal())
			continue;
		int iTheirPopPercent = (100 * kLoopTeam.getTotalPopulation()) /
				std::max(1, g.getTotalPopulation());
		int iTheirLandPercent = (100 * kLoopTeam.getTotalLand()) /
				std::max(1, GC.getMap().getLandPlots());
		if(kTeam.AI_getAttitude(kLoopTeam.getID()) >= ATTITUDE_FRIENDLY &&
				(iTheirPopPercent >= iPopObjective - iOurPopPercent ||
				iTheirLandPercent >= iLandObjective - iOurLandPercent))
			bBlockedByFriend = true;
	} // </advc.104c>

	iPercentOfDomination = (100 * iOurPopPercent) /
			iPopObjective; // advc.104c
	iPercentOfDomination = std::min(iPercentOfDomination, (100 * iOurLandPercent) /
			iLandObjective); // advc.104c

	if(!bBlockedByFriend) // advc.104c
	{
		// <advc.115>
		int iEverAlive = g.getCivPlayersEverAlive();
		if(iPercentOfDomination > 87 - iEverAlive) // was 80 flat </advc.115>
			return 4;
		if (iPercentOfDomination >
				62 - iEverAlive) // advc.115: was 50 flat
			return 3;
	}

	if (isHuman()) //&& !g.isDebugMode()) // advc.115d (commented out)
		return 0;

	// Check for whether we are inclined to pursue a domination strategy
	{
		iValue = GC.getInfo(getPersonalityType()).getDominationVictoryWeight();
		iValue += (g.isOption(GAMEOPTION_AGGRESSIVE_AI)
				? 10 : 0); // advc.019: was ?20:0

		//int iNonsense = AI_getStrategyRand() + 70;
		iValue += (AI_getStrategyRand(5) % 100);

		if (iValue >= 100)
		{
			if (getNumCities() > 3 && g.getPlayerRank(getID()) <
					// advc.115: was (g.countCivPlayersAlive+1)/2
					g.countCivPlayersAlive() / 2)
				return 2;
			return 1;
		}
	}
	return 0;
}


int CvPlayerAI::AI_calculateDiplomacyVictoryStage() const
{
	//int iValue = 0; // advc.115b
	CvGame const& g = GC.getGame(); // advc
	if (!isHuman() && // advc.115d
			!g.isOption(GAMEOPTION_RISE_FALL) && // advc.700
			!GC.getDefineBOOL("BBAI_VICTORY_STRATEGY_DIPLOMACY"))
		return 0;
	// <advc.115c>
	if (g.countCivTeamsAlive() <= 2)
		return 0; // </advc.115c>
	// <advc.178> Code moved into subroutine
	if (!g.isDiploVictoryValid())
		return false; // </advc.178>
	// Check for whether we are eligible for election
	bool bVoteEligible = false;
	for (int iVoteSource = 0; iVoteSource < GC.getNumVoteSourceInfos(); iVoteSource++)
	{
		if (g.isDiploVote((VoteSourceTypes)iVoteSource))
		{
			if (g.isTeamVoteEligible(getTeam(),(VoteSourceTypes)iVoteSource))
			{
				bVoteEligible = true;
				break;
			}
		}
	}
	//bool bDiploInclined = false; // advc.115b: No longer used

	// Check for whether we are inclined to pursue a diplomacy strategy
	// <advc.115b>
	int iValue = 0;
	if(isHuman()) // Perhaps not inclined, but very capable.
		iValue += 100;
	else iValue += GC.getInfo(getPersonalityType()).getDiplomacyVictoryWeight();
	iValue = ::round(iValue * 0.67); // Victory weight too high
	double voteTarget = -2;
	double votesToGo = GET_TEAM(getTeam()).AI_votesToGoForVictory(&voteTarget);
	if(voteTarget == -1)
		return 0;
	FAssert(voteTarget > 0);
	double progressRatio = ::dRange((voteTarget - votesToGo) / voteTarget, 0.0, 1.0);
	double membersProgress = 1;
	VoteSourceTypes eVS = GET_TEAM(getTeam()).AI_getLatestVictoryVoteSource();
	if(eVS != NO_VOTESOURCE)
	{
		bool bAP = (GC.getInfo(eVS).getVoteInterval() >= 7);
		// If AP religion hasn't spread far, our high voter portion doesn't help
		int iNonMembers = GET_TEAM(getTeam()).uwai().countNonMembers(eVS);
		int iTargetMembers = g.countCivPlayersAlive();
		membersProgress = (iTargetMembers - iNonMembers) / ((double)iTargetMembers);
		progressRatio = std::min(membersProgress, progressRatio);
		/*  UWAI uses the UN vote target if neither vote source is
			active yet. Also should look at UN votes in Industrial era if AP victory
			looks infeasible. */
		if(progressRatio < 0.66 && getCurrentEra() >= 4 && bAP)
		{
			votesToGo = GET_TEAM(getTeam()).AI_votesToGoForVictory(&voteTarget, /*forceUN=*/true);
			progressRatio = ::dRange((voteTarget - votesToGo) / voteTarget, 0.0, 1.0);
			bVoteEligible = false;
			membersProgress = 1; // not a concern for UN
		}
	}
	/*  LeaderHead weight is between 0 and 70. We're adding a hash between
		0 and 50 below. Those are for flavor and randomness.
		This here is the intelligent part; put it between 0 and 180.
		Square b/c having e.g. 50% of the necessary votes isn't nearly enough;
		need to get close to the vote target. */
	iValue += ::round(200 * progressRatio * progressRatio * membersProgress * membersProgress);
	// </advc.115b>

	// advc.115b: commented out
	/*if (g.isOption(GAMEOPTION_ALWAYS_PEACE))
		iValue += 30;
	iValue += (g.isOption(GAMEOPTION_AGGRESSIVE_AI) ? -20 : 0);*/

	//int iNonsense = AI_getStrategyRand() + 90;
	iValue += (AI_getStrategyRand(6) % 30); // advc.115b: was %100 - way too random

	// BBAI TODO: Level 2?

	/*if (iValue >= 100) // advc.115b (commented out)
		bDiploInclined = true;*/

	if (bVoteEligible &&
			iValue > 150) // advc.115b: was bDiploInclined; isHuman clause removed
	{
		// BBAI TODO: Level 4 - close to enough to win a vote?
		// <advc.115b>
		if (iValue > 185 && membersProgress >= 1)
			return 4; // </advc.115b>
		return 3;
	}

	if (isHuman())
			//&& !g.isDebugMode()) // advc.115d (commented out)
		return 0;
	// <advc.115b>
	if(iValue > 140)
		return 2; // </advc.115b>

	if(iValue > 110) // advc.115b: was bDiploInclined
		return 1;

	return 0;
}

/// Returns whether player is pursuing a particular stage of a victory strategy.
/// Victory stages are computed on demand once per turn and stored for the rest
/// of the turn.  Each victory strategy type has 4 stages, the first two are
/// determined largely from AI tendencies and random dice rolls. The second
/// two are based on measurables and past actions, so the AI can use them to
/// determine what other players (including the human player) are doing.
bool CvPlayerAI::AI_atVictoryStage(AIVictoryStage eStage) const
{
	if (isBarbarian() || isMinorCiv() || !isAlive())
		return false;
	return (eStage & AI_getVictoryStageHash());
}

// K-Mod note. The bbai version of this function checked each victory type one at a time.
// I've changed it to test them all at once. This is possible since it's a bitfield.
bool CvPlayerAI::AI_atVictoryStage4() const
{
	return AI_atVictoryStage(AI_VICTORY_SPACE4 | AI_VICTORY_CONQUEST4 |
			AI_VICTORY_CULTURE4 | AI_VICTORY_DOMINATION4 | AI_VICTORY_DIPLOMACY4);
}

// (same)
bool CvPlayerAI::AI_atVictoryStage3() const
{	// advc.115b: Replaced DIPLO3 with DIPLO4 b/c Diplo3 often isn't close to victory
	return AI_atVictoryStage(AI_VICTORY_SPACE3 | AI_VICTORY_CONQUEST3 |
			AI_VICTORY_CULTURE3 | AI_VICTORY_DOMINATION3 | AI_VICTORY_DIPLOMACY4);
}


void CvPlayerAI::AI_updateVictoryStageHash()
{
	PROFILE_FUNC();

	if (isBarbarian() || isMinorCiv() || !isAlive() ||
		GET_TEAM(getTeam()).isCapitulated()) // advc.014
	{
		m_eVictoryStageHash = NO_AI_VICTORY_STAGE;
		return;
	}

	m_eVictoryStageHash = AI_DEFAULT_VICTORY_STAGE;
	//m_iVictoryStrategyHashCacheTurn = GC.getGame().getGameTurn();

	if (getCapitalCity() == NULL)
		return;

	bool bStartedOtherLevel3 = false;
	bool bStartedOtherLevel4 = false;

	// Space victory
	int iVictoryStage = AI_calculateSpaceVictoryStage();
	if (iVictoryStage >= 1)
	{
		m_eVictoryStageHash |= AI_VICTORY_SPACE1;
		if (iVictoryStage > 1)
		{
			m_eVictoryStageHash |= AI_VICTORY_SPACE2;
			if (iVictoryStage > 2)
			{
				bStartedOtherLevel3 = true;
				m_eVictoryStageHash |= AI_VICTORY_SPACE3;

				if (iVictoryStage > 3 && !bStartedOtherLevel4)
				{
					bStartedOtherLevel4 = true;
					m_eVictoryStageHash |= AI_VICTORY_SPACE4;
				}
			}
		}
	}

	// Conquest victory
	iVictoryStage = AI_calculateConquestVictoryStage();
	if (iVictoryStage >= 1)
	{
		m_eVictoryStageHash |= AI_VICTORY_CONQUEST1;
		if (iVictoryStage > 1)
		{
			m_eVictoryStageHash |= AI_VICTORY_CONQUEST2;
			if (iVictoryStage > 2)
			{
				bStartedOtherLevel3 = true;
				m_eVictoryStageHash |= AI_VICTORY_CONQUEST3;

				if (iVictoryStage > 3 && !bStartedOtherLevel4)
				{
					bStartedOtherLevel4 = true;
					m_eVictoryStageHash |= AI_VICTORY_CONQUEST4;
				}
			}
		}
	}

	// Domination victory
	iVictoryStage = AI_calculateDominationVictoryStage();
	if (iVictoryStage >= 1)
	{
		m_eVictoryStageHash |= AI_VICTORY_DOMINATION1;
		if (iVictoryStage > 1)
		{
			m_eVictoryStageHash |= AI_VICTORY_DOMINATION2;
			if (iVictoryStage > 2)
			{
				bStartedOtherLevel3 = true;
				m_eVictoryStageHash |= AI_VICTORY_DOMINATION3;

				if (iVictoryStage > 3 && !bStartedOtherLevel4)
				{
					bStartedOtherLevel4 = true;
					m_eVictoryStageHash |= AI_VICTORY_DOMINATION4;
				}
			}
		}
	}

	// Cultural victory
	// K-Mod Note: AI_calculateCultureVictoryStage now checks some of the other victory strategies,
	// so it is important that they are set first.
	iVictoryStage = AI_calculateCultureVictoryStage();
	if (iVictoryStage >= 1)
	{
		m_eVictoryStageHash |= AI_VICTORY_CULTURE1;
		if (iVictoryStage > 1)
		{
			m_eVictoryStageHash |= AI_VICTORY_CULTURE2;
			if (iVictoryStage > 2)
			{
				bStartedOtherLevel3 = true;
				m_eVictoryStageHash |= AI_VICTORY_CULTURE3;

				//if (iVictoryStage > 3 && !bStartedOtherLevel4)
				if (iVictoryStage > 3) // K-Mod. If we're close to a cultural victory - then allow Culture4 even with other stage4 strategies already running.
				{
					bStartedOtherLevel4 = true;
					m_eVictoryStageHash |= AI_VICTORY_CULTURE4;
				}
			}
		}
	}

	// Diplomacy victory
	iVictoryStage = AI_calculateDiplomacyVictoryStage();
	if (iVictoryStage >= 1)
	{
		m_eVictoryStageHash |= AI_VICTORY_DIPLOMACY1;
		if (iVictoryStage > 1)
		{
			m_eVictoryStageHash |= AI_VICTORY_DIPLOMACY2;
			if (iVictoryStage > 2)
				/*&& !bStartedOtherLevel3) advc.115b: I really don't see
				 how pursuing diplo would get in the way of other victories,
				 and UN is often easier than a military victory. */
			{
				bStartedOtherLevel3 = true;
				m_eVictoryStageHash |= AI_VICTORY_DIPLOMACY3;

				if (iVictoryStage > 3)
						//&& !bStartedOtherLevel4) // advc.115b (commented out)
				{
					bStartedOtherLevel4 = true;
					m_eVictoryStageHash |= AI_VICTORY_DIPLOMACY4;
				}
			}
		}
	}
} // BETTER_BTS_AI_MOD: END (Victory Strategy AI)

// K-Mod, based on BBAI
void CvPlayerAI::AI_initStrategyRand()
{
	const unsigned iBits = 24;
	m_iStrategyRand = GC.getGame().getSorenRandNum((1<<(iBits+1))-1, "AI Strategy Rand");
}


int CvPlayerAI::AI_getStrategyRand(int iShift) const
{
	const unsigned iBits = 24; // cf bits in AI_initStrategyRand

	iShift += getCurrentEra();
	while (iShift < 0)
		iShift += iBits;
	iShift %= iBits;

	FAssert(m_iStrategyRand != MAX_UNSIGNED_INT);
	unsigned x = 2654435761; // golden ratio of 2^32
	x *= (m_iStrategyRand << iShift) + (m_iStrategyRand >> (iBits - iShift));
	x >>= 1; // force positive;
	FAssert(x <= MAX_INT);
	return x;
}
// K-Mod end
// <advc.115b>
bool CvPlayerAI::isCloseToReligiousVictory() const
{
	if(!AI_atVictoryStage(AI_VICTORY_DIPLOMACY3))
		return false;
	VoteSourceTypes eVS = GET_TEAM(getTeam()).AI_getLatestVictoryVoteSource();
	if(eVS == NO_VOTESOURCE)
		return false;
	return GC.getGame().getVoteSourceReligion(eVS) == getStateReligion();
}// </advc.115b>

bool CvPlayerAI::AI_isDoStrategy(AIStrategy eStrategy, /* advc.007: */ bool bDebug) const
{
	if (!isAlive() || isBarbarian() || isMinorCiv() ||
		(isHuman() && /* advc.007: */ !bDebug))
	{
		return false;
	}
	return (eStrategy & AI_getStrategyHash());
}

// K-mod. The body of this function use to be inside "AI_getStrategyHash"
void CvPlayerAI::AI_updateStrategyHash()
{
// K-Mod. Macros to help log changes in the AI strategy.
#define log_strat(s) \
	if (gPlayerLogLevel >= 2) \
	{ \
		if ((m_eStrategyHash & s) != (eLastStrategyHash & s)) \
		{ \
			logBBAI( "    Player %d (%S) %s strategy "#s" on turn %d", getID(), getCivilizationDescription(0), (m_eStrategyHash & s) ? "starts" : "stops", GC.getGame().getGameTurn()); \
		} \
	}
#define log_strat2(s, x) \
	if (gPlayerLogLevel >= 2) \
	{ \
		if ((m_eStrategyHash & s) != (eLastStrategyHash & s)) \
		{ \
			logBBAI( "    Player %d (%S) %s strategy "#s" on turn %d with "#x" %d", getID(), getCivilizationDescription(0), (m_eStrategyHash & s) ? "starts" : "stops", GC.getGame().getGameTurn(), x); \
		} \
	}
//

	const CvTeamAI& kTeam = GET_TEAM(getTeam()); // K-Mod. (and replaced all through this function)
	AIStrategy eLastStrategyHash = m_eStrategyHash;
	m_eStrategyHash = AI_DEFAULT_STRATEGY;

	/*if (AI_getFlavorValue(FLAVOR_PRODUCTION) >= 2) // 0, 2, 5 or 10 in default xml [augustus 5, frederick 10, huayna 2, jc 2, chinese leader 2, qin 5, ramsess 2, roosevelt 5, stalin 2]
		m_iStrategyHash |= AI_STRATEGY_PRODUCTION;*/ // BtS
	// K-Mod. This strategy is now set later on, with new conditions.

	if (getCapitalCity() == NULL)
		return;

	//int iNonsense = AI_getStrategyRand();

	int iMetCount = kTeam.getHasMetCivCount(true);

	//Unit Analysis
	int iBestSlowUnitCombat = -1;
	int iBestFastUnitCombat = -1;

	bool bHasMobileArtillery = false;
	bool bHasMobileAntiair = false;
	bool bHasBomber = false;

	int iNukeCount = 0;

	int iAttackUnitCount = 0;
	// K-Mod
	int iAverageEnemyUnit = 0;
	int iTypicalAttack = getTypicalUnitValue(UNITAI_ATTACK);
	int iTypicalDefence = getTypicalUnitValue(UNITAI_CITY_DEFENSE);
	int iWarSuccessRating = kTeam.AI_getWarSuccessRating();
	// K-Mod end

	CvCivilization const& kCiv = getCivilization(); // advc.003w
	for (int i = 0; i < kCiv.getNumUnits(); i++)
	{
		UnitTypes eLoopUnit = kCiv.unitAt(i);
		/*if (getCapitalCity() != NULL) // advc: already guaranteed
		{*/
		if (!getCapitalCity()->canTrain(eLoopUnit))
			continue;

		CvUnitInfo& kLoopUnit = GC.getInfo(eLoopUnit);
		bool bUU = kCiv.isUnique(eLoopUnit);
		/*if (kLoopUnit.getUnitAIType(UNITAI_RESERVE) || kLoopUnit.getUnitAIType(UNITAI_ATTACK_CITY)
				|| kLoopUnit.getUnitAIType(UNITAI_COUNTER) || kLoopUnit.getUnitAIType(UNITAI_PILLAGE)) */
		// K-Mod. Original code was missing the obvious: UNITAI_ATTACK. Was this a bug? (I'm skipping "pillage".)
		if (kLoopUnit.getUnitAIType(UNITAI_ATTACK) || kLoopUnit.getUnitAIType(UNITAI_ATTACK_CITY)
				|| kLoopUnit.getUnitAIType(UNITAI_RESERVE) || kLoopUnit.getUnitAIType(UNITAI_COUNTER))
		// K-Mod end
		{
			iAttackUnitCount++;

			//UU love
			if (bUU)
			{
				if (kLoopUnit.getUnitAIType(UNITAI_ATTACK_CITY) ||
					(kLoopUnit.getUnitAIType(UNITAI_ATTACK)	&&
					!kLoopUnit.getUnitAIType(UNITAI_CITY_DEFENSE)))
				{
					iAttackUnitCount++;
				}
			}
			int iCombat = kLoopUnit.getCombat();
			int iMoves = kLoopUnit.getMoves();
			if (iMoves == 1)
			{
				iBestSlowUnitCombat = std::max(iBestSlowUnitCombat, iCombat);
			}
			else if (iMoves > 1)
			{
				iBestFastUnitCombat = std::max(iBestFastUnitCombat, iCombat);
				if (bUU)
				{
					iBestFastUnitCombat += 1;
				}
			}
		}
		//if (kLoopUnit.getMoves() > 1)
		// UNOFFICIAL_PATCH, Bugfix, 09/10/08, jdog5000:
		// Mobile anti-air and artillery flags only meant for land units
		if (kLoopUnit.getDomainType() == DOMAIN_LAND && kLoopUnit.getMoves() > 1)
		{
			if (kLoopUnit.getInterceptionProbability() > 25)
			{
				bHasMobileAntiair = true;
			}
			if (kLoopUnit.getBombardRate() > 10)
			{
				bHasMobileArtillery = true;
			}
		}

		if (kLoopUnit.getAirRange() > 1)
		{
			if (!kLoopUnit.isSuicide())
			{
				if ((kLoopUnit.getBombRate() > 10) && (kLoopUnit.getAirCombat() > 0))
				{
					bHasBomber = true;
				}
			}
		}

		if (kLoopUnit.getNukeRange() > 0)
		{
			iNukeCount++;
		}
	}

	// K-Mod
	{
		int iTotalPower = 0;
		int iTotalWeightedValue = 0;
		for (int iI = 0; iI < MAX_CIV_PLAYERS; iI++)
		{
			CvPlayer &kLoopPlayer = GET_PLAYER((PlayerTypes)iI);
			if (kLoopPlayer.getTeam() != getTeam())
			{
				if (kLoopPlayer.isAlive() && kTeam.isHasMet(kLoopPlayer.getTeam()))
				{
					// Attack units are scaled down to roughly reflect their limitations.
					// (eg. Knights (10) vs Macemen (8). Cavalry (15) vs Rifles (14). Tank (28) vs Infantry (20) / Marine (24))
					int iValue = std::max(100*kLoopPlayer.getTypicalUnitValue(UNITAI_ATTACK)/110, kLoopPlayer.getTypicalUnitValue(UNITAI_CITY_DEFENSE));

					iTotalWeightedValue += kLoopPlayer.getPower() * iValue;
					iTotalPower += kLoopPlayer.getPower();
				}
			}
		}
		if (iTotalPower == 0)
			iAverageEnemyUnit = 0;
		else
			iAverageEnemyUnit = iTotalWeightedValue / iTotalPower;
		// A bit of random variation...
		iAverageEnemyUnit *= (91+AI_getStrategyRand(1)%20);
		iAverageEnemyUnit /= 100;
	}

	//if (iAttackUnitCount <= 1)
	CvGame const& g = GC.getGame();
	if ((iAttackUnitCount <= 1 && g.getGameTurn() > GC.getInfo(g.getGameSpeedType()).getBarbPercent()/20)
		|| (100*iAverageEnemyUnit > 135*iTypicalAttack && 100*iAverageEnemyUnit > 135*iTypicalDefence))
	{
		m_eStrategyHash |= AI_STRATEGY_GET_BETTER_UNITS;
	}
	// K-Mod end
	if (iBestFastUnitCombat > iBestSlowUnitCombat)
	{
		m_eStrategyHash |= AI_STRATEGY_FASTMOVERS;
		if (bHasMobileArtillery && bHasMobileAntiair)
		{
			m_eStrategyHash |= AI_STRATEGY_LAND_BLITZ;
		}
	}
	if (iNukeCount > 0)
	{
		if ((GC.getInfo(getPersonalityType()).getBuildUnitProb() +
				AI_getStrategyRand(7) % 15) >=
				(g.isOption(GAMEOPTION_AGGRESSIVE_AI) ? 37 : 43))
			m_eStrategyHash |= AI_STRATEGY_OWABWNW;
	}
	if (bHasBomber)
	{
		if (!(m_eStrategyHash & AI_STRATEGY_LAND_BLITZ))
		{
			m_eStrategyHash |= AI_STRATEGY_AIR_BLITZ;
		}
		else
		{
			if ((AI_getStrategyRand(8) % 2) == 0)
			{
				m_eStrategyHash |= AI_STRATEGY_AIR_BLITZ;
				m_eStrategyHash &= ~AI_STRATEGY_LAND_BLITZ;
			}
		}
	}

	log_strat(AI_STRATEGY_LAND_BLITZ)
	log_strat(AI_STRATEGY_AIR_BLITZ)

	//missionary
	{
		if (getStateReligion() != NO_RELIGION)
		{
			int iHolyCityCount = countHolyCities();
			if ((iHolyCityCount > 0) && hasHolyCity(getStateReligion()))
			{
				int iMissionary = 0;
				//Missionary
				/*  <advc.020> Replaced FLAVOR_GROWTH with FLAVOR_GOLD and set the
					multipliers to 3; was 2*Growth ("up to 10"), 4*Culture ("up to 40") */
				iMissionary += AI_getFlavorValue(FLAVOR_GOLD) * 3; // up to 15
				iMissionary += AI_getFlavorValue(FLAVOR_CULTURE) * 3; // up to 30
				// </advc.020>
				iMissionary += AI_getFlavorValue(FLAVOR_RELIGION) * 6; // up to 60

				CivicTypes eCivic = (CivicTypes)GC.getInfo(getPersonalityType()).getFavoriteCivic();
				if ((eCivic != NO_CIVIC) && (GC.getInfo(eCivic).isNoNonStateReligionSpread()))
				{
					iMissionary += 20;
				}
				iMissionary += (iHolyCityCount - 1) * 5;
				iMissionary += std::min(iMetCount, 5) * 7;

				for (int iI = 0; iI < MAX_CIV_PLAYERS; iI++)
				{
					if (iI == getID())
						continue;
					if (GET_PLAYER((PlayerTypes)iI).isAlive() && kTeam.isHasMet(TEAMID((PlayerTypes)iI)))
					{
						if (kTeam.canPeacefullyEnter(TEAMID((PlayerTypes)iI))) // advc.001j:
						{
							if ((GET_PLAYER((PlayerTypes)iI).getStateReligion() == getStateReligion()))
								iMissionary += 10;
							else if(!GET_PLAYER((PlayerTypes)iI).isNoNonStateReligionSpread())
							{
								iMissionary += (GET_PLAYER((PlayerTypes)iI).countHolyCities() == 0) ? 12 : 4;
							}
						}
					}
				}

				iMissionary += (AI_getStrategyRand(9) % 7) * 3;
				// <advc.115b>
				if(iMissionary > 0 && isCloseToReligiousVictory())
					iMissionary = ::round(1.7 * iMissionary); // </advc.115b>
				if (iMissionary > 100)
					m_eStrategyHash |= AI_STRATEGY_MISSIONARY;
			}
		}
	}

	// Espionage
	// K-Mod
	// Apparently BBAI wanted to use "big espionage" to save points when our espionage is weak.
	// I've got other plans.
	if (!g.isOption(GAMEOPTION_NO_ESPIONAGE) &&
		isCommerceFlexible(COMMERCE_ESPIONAGE)) // advc.120g
	{
		// don't start espionage strategy if we have no spies
		if ((eLastStrategyHash & AI_STRATEGY_BIG_ESPIONAGE) ||
			AI_getNumAIUnits(UNITAI_SPY) > 0)
		{
			int iTempValue = 0;
			iTempValue += AI_commerceWeight(COMMERCE_ESPIONAGE) / 8;
			// Note: although AI_commerceWeight is doubled for Big Espionage,
			// the value here is unaffected because the strategy hash has been cleared.
			iTempValue += kTeam.getBestKnownTechScorePercent() < 85 ? 3 : 0;
			 // build up espionage before the start of a war
			iTempValue += kTeam.AI_countWarPlans() > kTeam.getNumWars(true, true) ? 2 : 0;
			if (iWarSuccessRating < 0)
				iTempValue += iWarSuccessRating/15 - 1;
			iTempValue += AI_getStrategyRand(10) % 8;

			if (iTempValue > 11)
			{
				// Don't allow big esp if we have no local esp targets
				for (int i = 0; i < MAX_CIV_TEAMS; i++)
				{
					if (i != getTeam() && kTeam.isHasMet((TeamTypes)i) &&
						// <advc.120>
						(!kTeam.isCapitulated() || kTeam.getMasterTeam() !=
						GET_TEAM((TeamTypes)i).getMasterTeam()) &&
						// </advc.120>
						kTeam.AI_hasCitiesInPrimaryArea((TeamTypes)i))
					{
						m_eStrategyHash |= AI_STRATEGY_BIG_ESPIONAGE;
						break;
					}
				}
			}
		}

		// The espionage economy decision is actually somewhere else [advc: namely in AI_doCommerce]. This is just a marker.
		if (getCommercePercent(COMMERCE_ESPIONAGE) > 20)
			m_eStrategyHash |= AI_STRATEGY_ESPIONAGE_ECONOMY;
	}
	log_strat(AI_STRATEGY_ESPIONAGE_ECONOMY)
	// K-Mod end

	// Turtle strategy
	if (kTeam.getNumWars() > 0 && getNumCities() > 0)
	{
		int iMaxWarCounter = 0;
		for (int iTeam = 0; iTeam < MAX_CIV_TEAMS; iTeam++)
		{
			if (iTeam != getTeam())
			{
				if (GET_TEAM((TeamTypes)iTeam).isAlive() && !GET_TEAM((TeamTypes)iTeam).isMinorCiv())
				{
					iMaxWarCounter = std::max(iMaxWarCounter, kTeam.AI_getAtWarCounter((TeamTypes)iTeam));
				}
			}
		}

		// Are we losing badly or recently attacked?
		if (iWarSuccessRating < -50 || iMaxWarCounter < 10)
		{
			if (kTeam.AI_getEnemyPowerPercent(true) >
				std::max(150, GC.getDefineINT("BBAI_TURTLE_ENEMY_POWER_RATIO")) &&
				// advc.107:
				getNumMilitaryUnits() < (5 + getCurrentEra() * 1.5) * getNumCities())
			{
				m_eStrategyHash |= AI_STRATEGY_TURTLE;
			}
		}
	}
	log_strat(AI_STRATEGY_TURTLE)

	int iCurrentEra = getCurrentEra();
	int iParanoia = 0;
	int iCloseTargets = 0;
	int iOurDefensivePower = kTeam.getDefensivePower();

	for (int iI = 0; iI < MAX_CIV_PLAYERS; iI++)
	{
		const CvPlayerAI& kLoopPlayer = GET_PLAYER((PlayerTypes)iI);
		const CvTeamAI& kLoopTeam = GET_TEAM(kLoopPlayer.getTeam());
		if (!kLoopPlayer.isAlive() || kLoopPlayer.isMinorCiv()) // advc: reduce indentation
			continue;
		if (kLoopPlayer.getTeam() == getTeam() || !kTeam.isHasMet(kLoopPlayer.getTeam()))
			continue;
		if (kLoopTeam.isAVassal() || kTeam.isVassal(kLoopPlayer.getTeam()))
			continue;
		// <advc.022> Don't fear (AI) civs that are busy
		if(!kLoopPlayer.isHuman() && kLoopPlayer.AI_isFocusWar() &&
				kLoopTeam.getNumWars() > 0 &&
				kLoopTeam.AI_getWarSuccessRating() < 50)
			continue; // </advc.022>
		bool bCitiesInPrime = kTeam.AI_hasCitiesInPrimaryArea(kLoopPlayer.getTeam()); // K-Mod

		if (kTeam.AI_getWarPlan(kLoopPlayer.getTeam()) != NO_WARPLAN)
		{
			iCloseTargets++;
			continue; // advc
		}
		// Are they a threat?
		int iTempParanoia = 0;

		int iTheirPower = kLoopTeam.getPower(true);
		if (4*iTheirPower > 3*iOurDefensivePower)
		{
			if (//kLoopTeam.getAtWarCount(true) == 0
				!kLoopPlayer.AI_isFocusWar() // advc.105: Replacing the above
				|| kLoopTeam.AI_getEnemyPowerPercent(false) < 140)
			{
				// Memory of them declaring on us and our friends
				int iWarMemory = AI_getMemoryCount((PlayerTypes)iI, MEMORY_DECLARED_WAR);
				iWarMemory += (AI_getMemoryCount((PlayerTypes)iI, MEMORY_DECLARED_WAR_ON_FRIEND) + 1)/2;
				iWarMemory = (int)ceil(iWarMemory / 2.5); // advc.130j
				if (iWarMemory > 0)
				{
					//they are a snake
					iTempParanoia += 50 + 50 * iWarMemory;

					if (gPlayerLogLevel >= 2)
					{
						logBBAI( "    Player %d (%S) wary of %S because of war memory %d", getID(), getCivilizationDescription(0), GET_PLAYER((PlayerTypes)iI).getCivilizationDescription(0), iWarMemory);
					}
				}
			}
		}

		// Do we think our relations are bad?
		int iCloseness = AI_playerCloseness((PlayerTypes)iI, DEFAULT_PLAYER_CLOSENESS);
		// <advc.022>
		if(!AI_hasSharedPrimaryArea(kLoopPlayer.getID()))
		{
			int iNoSharePenalty = 99;
			EraTypes loopEra = kLoopPlayer.getCurrentEra();
			if(loopEra >= 3)
				iNoSharePenalty -= 33;
			if(loopEra >= 4)
				iNoSharePenalty -= 33;
			if(loopEra >= 5)
				iNoSharePenalty -= 33;
			iCloseness = std::max(0, iCloseness - iNoSharePenalty);
		} // </advc.022>
		// if (iCloseness > 0)
		if (iCloseness > 0 || bCitiesInPrime) // K-Mod
		{	// <advc.022>
			// Humans tend to reciprocate our feelings
			int iHumanWarProb = 70 - AI_getAttitude(kLoopPlayer.getID()) * 10;
			int iAttitudeWarProb = (kLoopPlayer.isHuman() ? iHumanWarProb :
					// Now based on kLoopPlayer's personality and attitude
					100 - GET_TEAM(kLoopPlayer.getTeam()).
					AI_noWarProbAdjusted(getTeam())); // advc.104y
			// </advc.022>
			// (original BBAI code deleted) // advc
			// K-Mod. Paranoia gets scaled by relative power anyway...
			iTempParanoia += std::max(0, iAttitudeWarProb/2);
			/*  <advc.022> This is about us attacking someone (not relevant for
				Paranoia, but for Dagger), so our attitude should be used.
				K-Mod already did that, but iAttitudeWarProb now refers to
				kLoopPlayer. */
			//if (iAttitudeWarProb > 10 && iCloseness > 0)
			if(iCloseness > 0 && 100 - GET_TEAM(getTeam()).
					AI_noWarProbAdjusted(kLoopPlayer.getTeam()) > 10) // advc.104y
				// </advc.022>
				iCloseTargets++;
			// K-Mod end
			/*  advc.022: Commented out. Defensive measures make most sense
				around 150% power ratio; 200% is probably a lost cause. */
			/*if (iTheirPower > 2*iOurDefensivePower) {
				//if (AI_getAttitude((PlayerTypes)iI) != ATTITUDE_FRIENDLY)
				// advc.022: Replacing the above
				if(iAttitudeWarProb > 0)
					iTempParanoia += 25;
			}*/
		}

		if (iTempParanoia > 0)
		{
			iTempParanoia *= std::min(iTheirPower,
					// advc.022: At most double paranoia based on power ratio
					2 * iOurDefensivePower);
			iTempParanoia /= std::max(1, iOurDefensivePower);
			// K-Mod
			if (kLoopTeam.AI_getWorstEnemy() == getTeam())
			{
				//iTempParanoia *= 2;
				/*  advc.022: Don't give their attitude too much weight (replacing
					the above) */
				iTempParanoia = ::round(iTempParanoia * 1.5);
			}
			// K-Mod end
		}

		// Do they look like they're going for militaristic victory?
		// advc.022: New temp variable
		int iVictStratParanoia = 0;
		if (kLoopPlayer.AI_atVictoryStage(AI_VICTORY_CONQUEST4))
			iVictStratParanoia += 200;
		else if (kLoopPlayer.AI_atVictoryStage(AI_VICTORY_CONQUEST3))
			iVictStratParanoia += 100;
		else if (kLoopPlayer.AI_atVictoryStage(AI_VICTORY_DOMINATION3))
			iVictStratParanoia += 50;
		/*  advc.022: Too high in K-Mod I think; who knows when they'll get around
			to attack us. (Could count the alternative targets I guess ...). */
		iTempParanoia += iVictStratParanoia / 2;
		if (iTempParanoia > 0)
		{	// <advc.022> Replace this with something smoother
			/*if (iCloseness == 0)
				iTempParanoia /= 2;*/
			double multiplier = 2;
			/*  I don't think closeness is intended to be a percentage, but based on
				some sample values (Ctrl key on the capital in debug mode; closeness
				is shown in square brackets), it tends to be between 0 and 100. */
			multiplier *= ::dRange(iCloseness / 100.0, 0.0, 1.0) + 0.3;
			// <advc.022> Reduced paranoia if resistance futile
			double powRatio = iTheirPower / (double)iOurDefensivePower;
			/*  No change if ratio is 165% or less; 215% -> 50% reduced paranoia;
				260% -> 0 paranoia */
			multiplier *= std::max(0.0, 1 - std::max(0.0, powRatio - 1.65));
			iTempParanoia = ::round(iTempParanoia * multiplier);
			// </advc.022>
			iParanoia += iTempParanoia;
		}
	}
	/*  advc.022: Commented out. BETTER_UNITS is supposed to make us train fewer
		units (b/c they're not good), but high paranoia will lead to more units. */
	/*if (m_eStrategyHash & AI_STRATEGY_GET_BETTER_UNITS) {
		iParanoia *= 3;
		iParanoia /= 2;
	}*/

	// Scale paranoia in later eras/larger games
	//iParanoia -= (100*(iCurrentEra + 1)) / std::max(1, GC.getNumEraInfos());

	// K-Mod. You call that scaling for "later eras/larger games"? It isn't scaling, and it doesn't use the map size.
	// Lets try something else. Rough and ad hoc, but hopefully a bit better.
	iParanoia *= (3*GC.getNumEraInfos() - 2*iCurrentEra);
	iParanoia /= 3*(std::max(1, GC.getNumEraInfos()));
	// That starts as a factor of 1, and drop to 1/3.  And now for game size...
	iParanoia *= 14;
	iParanoia /= (7+std::max(kTeam.getHasMetCivCount(true),
			//GC.getInfo(GC.getMap().getWorldSize()).getDefaultPlayers()));
			g.getRecommendedPlayers())); // advc.137

	// Alert strategy
	if (iParanoia >= 200)
	{
		m_eStrategyHash |= AI_STRATEGY_ALERT1;
		if (iParanoia >= 400
				&& !AI_isFocusWar()) // advc.022: Need to focus on the war at hand
			m_eStrategyHash |= AI_STRATEGY_ALERT2;
	}
	log_strat2(AI_STRATEGY_ALERT1, iParanoia)
	log_strat2(AI_STRATEGY_ALERT2, iParanoia)

	// Economic focus (K-Mod) - This strategy is a gambit. The goal is to tech faster by neglecting military.
	//if (kTeam.getAnyWarPlanCount(true) == 0)
	if(!AI_isFocusWar()) // advc.105
	{
		int iFocus = (100 - iParanoia) / 20;
		iFocus += std::max(iAverageEnemyUnit - std::max(iTypicalAttack, iTypicalDefence), std::min(iTypicalAttack, iTypicalDefence) - iAverageEnemyUnit) / 12;
		//Note: if we haven't met anyone then average enemy is zero. So this essentially assures economic strategy when in isolation.
		iFocus += (AI_getPeaceWeight() + AI_getStrategyRand(2)%10)/3; // note: peace weight will be between 0 and 12
		if (iFocus >= 12 /* advc.109: */ || AI_feelsSafe())
			m_eStrategyHash |= AI_STRATEGY_ECONOMY_FOCUS;
	}
	log_strat(AI_STRATEGY_ECONOMY_FOCUS)

	/*  <advc.104f> Don't want dagger even if UWAI only in the background
		b/c it gets in the way of testing. If UWAI fully disabled, allow dagger
		with additional restrictions. */
	bool bNoDagger = (getUWAI.isEnabled() || getUWAI.isEnabled(true));
	if(!bNoDagger)
	{
		for(int i = 0; i < MAX_CIV_TEAMS; i++)
		{
			CvTeam const& t = GET_TEAM((TeamTypes)i);
			if(t.isAlive() && !t.isMinorCiv() && t.isAtWar(getTeam()) &&
				GET_TEAM(getTeam()).AI_isChosenWar(t.getID()))
			{
				bNoDagger = true;
				break;
			}
		}
	} // </advc.104f>

	// BBAI TODO: Integrate Dagger with new conquest victory strategy, have Dagger focus on early rushes
	//dagger
	if (!AI_atVictoryStage(AI_VICTORY_CULTURE2)
		&& !(m_eStrategyHash & AI_STRATEGY_MISSIONARY)
		&& iCurrentEra <= (2 + (AI_getStrategyRand(11) % 2)) && iCloseTargets > 0
		&& !bNoDagger) // advc.104f
	{
		int iDagger = 0;
		iDagger += 12000 / std::max(100, (50 + GC.getInfo(getPersonalityType()).getMaxWarRand()));
		iDagger *= (AI_getStrategyRand(12) % 11);
		iDagger /= 10;
		iDagger += 5 * std::min(8, AI_getFlavorValue(FLAVOR_MILITARY));

		CvCivilization const& kCiv = getCivilization(); // advc.003w
		for (int i = 0; i < kCiv.getNumUniqueUnits(); i++)
		{
			UnitTypes eUnit = kCiv.uniqueUnitAt(i);
			CvUnitInfo const& kUnit = GC.getInfo(eUnit);
			if (kUnit.getCombat() <= 0)
				continue; // advc
			UnitClassTypes eLoopUnitClass = kUnit.getUnitClassType();
			/*  (advc: A little crazy to have all this code just for counting
				unique units for Dagger - which UWAI bypasses anyway -,
				but that's how it was and is.) */
			//if ((GC.getInfo(eLoopUnitClass).getDefaultUnitIndex()) != (GC.getInfo(getCivilizationType()).getCivilizationUnits(eLoopUnitClass))) {
			bool bDefensive = (kUnit.getUnitAIType(UNITAI_CITY_DEFENSE) &&
					!kUnit.getUnitAIType(UNITAI_RESERVE));

			iDagger += bDefensive ? -10 : 0;

			if (getCapitalCity()->canTrain(eUnit))
			{
				iDagger += bDefensive ? 10 : 40;

				int iUUStr = kUnit.getCombat();
				int iNormalStr = GC.getInfo(GC.getInfo(eLoopUnitClass).
						getDefaultUnit()).getCombat();
				iDagger += 20 * range(iUUStr - iNormalStr, 0, 2);
				if (kUnit.getPrereqAndTech() == NO_TECH)
					iDagger += 20;
			}
			else
			{
				if (kUnit.getPrereqAndTech() != NO_TECH &&
					GC.getInfo((TechTypes)kUnit.getPrereqAndTech()).getEra() <= iCurrentEra + 1)
				{
					if (kTeam.isHasTech((TechTypes)kUnit.getPrereqAndTech()))
					{
						//we have the tech but can't train the unit, dejection.
						iDagger += 10;
					}
					else
					{
						//we don't have the tech, it's understandable we can't train.
						iDagger += 30;
					}
				}

				bool bNeedsAndBonus = false;
				int iOrBonusCount = 0;
				int iOrBonusHave = 0;

				FOR_EACH_ENUM(Bonus)
				{
					if (eLoopBonus != NO_BONUS)
					{
						if (kUnit.getPrereqAndBonus() == eLoopBonus)
						{
							if (getNumTradeableBonuses(eLoopBonus) == 0)
								bNeedsAndBonus = true;
						}

						for (int j = 0; j < GC.getNUM_UNIT_PREREQ_OR_BONUSES(eUnit); j++)
						{
							if (kUnit.getPrereqOrBonuses(j) == eLoopBonus)
							{
								iOrBonusCount++;
								if (getNumTradeableBonuses(eLoopBonus) > 0)
									iOrBonusHave++;
							}
						}
					}
				}


				iDagger += 20;
				if (bNeedsAndBonus)
					iDagger -= 20;
				if (iOrBonusCount > 0 && iOrBonusHave == 0)
					iDagger -= 20;
			}
		}

		if (!g.isOption(GAMEOPTION_AGGRESSIVE_AI))
			iDagger += range(100 - GC.getInfo(g.getHandicapType()).getAITrainPercent(), 0, 15);

		if (getCapitalCity()->getArea().getAreaAIType(getTeam()) == AREAAI_OFFENSIVE ||
			(getCapitalCity()->getArea().getAreaAIType(getTeam()) == AREAAI_DEFENSIVE))
		{
			iDagger += (iAttackUnitCount > 0 ? 40 : 20);
		}

		if (iDagger >= AI_DAGGER_THRESHOLD)
			m_eStrategyHash |= AI_STRATEGY_DAGGER;
		else
		{
			if (eLastStrategyHash &= AI_STRATEGY_DAGGER)
			{
				if (iDagger >= (9 * AI_DAGGER_THRESHOLD) / 10)
				{
					m_eStrategyHash |= AI_STRATEGY_DAGGER;
				}
			}
		}

		log_strat2(AI_STRATEGY_DAGGER, iDagger)
	}

	if (!(m_eStrategyHash & AI_STRATEGY_ALERT2) && !(m_eStrategyHash & AI_STRATEGY_TURTLE))
	{//Crush
		int iWarCount = 0;
		int iCrushValue = 0;


	// K-Mod. (experimental)
		//iCrushValue += (iNonsense % 4);
		// A leader dependant value. (MaxWarRand is roughly between 50 and 200. Gandi is 400.)
		//iCrushValue += (iNonsense % 3000) / (400+GC.getInfo(getPersonalityType()).getMaxWarRand());
	// On second thought, lets try this
		iCrushValue += AI_getStrategyRand(13) % (4 + AI_getFlavorValue(FLAVOR_MILITARY)/2 + (g.isOption(GAMEOPTION_AGGRESSIVE_AI) ? 2 : 0));
		iCrushValue += std::min(0, kTeam.AI_getWarSuccessRating()/15);
		// note: flavor military is between 0 and 10
		// K-Mod end
		/*  advc.018: Commented out.
			Not clear that conq3 should make the AI more willing to neglect
			its defense. Also, once crush is adopted, the AI becomes more inclined
			towards conquest; should only work one way. */
		/*if (AI_atVictoryStage(AI_VICTORY_CONQUEST3))
			iCrushValue += 1;*/

		if (m_eStrategyHash & AI_STRATEGY_DAGGER)
		{
			iCrushValue += 3;
		}

		for (int iI = 0; iI < MAX_CIV_TEAMS; iI++)
		{
			if (GET_TEAM((TeamTypes)iI).isAlive() && iI != getTeam())
			{
				if (kTeam.AI_getWarPlan((TeamTypes)iI) != NO_WARPLAN)
				{
					if (!GET_TEAM((TeamTypes)iI).isAVassal())
					{
						if (kTeam.AI_teamCloseness((TeamTypes)iI) > 0)
							iWarCount++;

						// K-Mod. (if we attack with our defenders, would they beat their defenders?)
						if (100*iTypicalDefence >= 110 * GET_TEAM((TeamTypes)iI).
							getTypicalUnitValue(UNITAI_CITY_DEFENSE))
						{
							iCrushValue += 2;
						}
					}
					int iCrushValFromWarplanType = 0; // advc.018: New temp variable
					if (kTeam.AI_getWarPlan((TeamTypes)iI) == WARPLAN_PREPARING_TOTAL)
						iCrushValFromWarplanType += 6;
					else if (kTeam.AI_getWarPlan((TeamTypes)iI) == WARPLAN_TOTAL &&
							/*  advc.104q: Was AI_getWarPlanStateCounter. Probably war duration was
								intended. That said, I'm now also resetting WarPlanStateCounter upon
								a DoW (see advc.104q in CvTeam::declareWar), so that it shouldn't make
								a difference which function is used (in this context). */
							kTeam.AI_getAtWarCounter((TeamTypes)iI) < 20)
						iCrushValFromWarplanType += 6;

					if ((kTeam.AI_getWarPlan((TeamTypes)iI) == WARPLAN_DOGPILE) &&
						kTeam.AI_getAtWarCounter((TeamTypes)iI) < 20) // advc.104q
					{
						for (int iJ = 0; iJ < MAX_CIV_TEAMS; iJ++)
						{
							if ((iJ != iI) && iJ != getTeam() && GET_TEAM((TeamTypes)iJ).isAlive())
							{
								if (::atWar((TeamTypes)iI, (TeamTypes)iJ) &&
									!GET_TEAM((TeamTypes)iI).isAVassal())
								{
									iCrushValFromWarplanType += 4;
								}
							}
						}
					} // advc.018: Halve the impact
					iCrushValue += iCrushValFromWarplanType / 2;
				}
			}
		}
		if (/* K-Mod: */ iWarCount == 1 &&
			iCrushValue >= ((eLastStrategyHash & AI_STRATEGY_CRUSH) ? 9 : 10))
		{
			m_eStrategyHash |= AI_STRATEGY_CRUSH;
		}
		log_strat2(AI_STRATEGY_CRUSH, iCrushValue)
	}

	// K-Mod
	{//production
		int iProductionValue = AI_getStrategyRand(2) % (5 + AI_getFlavorValue(FLAVOR_PRODUCTION)/2);
		iProductionValue += (eLastStrategyHash & AI_STRATEGY_PRODUCTION) ? 1 : 0;
		iProductionValue += AI_getFlavorValue(FLAVOR_PRODUCTION) > 0 ? 1 : 0;
		iProductionValue += (m_eStrategyHash & AI_STRATEGY_DAGGER) ? 1 : 0;
		// advc.018: Commented out
		//iProductionValue += (m_eStrategyHash & AI_STRATEGY_CRUSH) ? 1 : 0;
		iProductionValue += AI_atVictoryStage(AI_VICTORY_CONQUEST4 | AI_VICTORY_SPACE4) ? 3 : 0;
		// warplans. (done manually rather than using getWarPlanCount, so that we only have to do the loop once.)
		bool bAnyWarPlans = false;
		bool bTotalWar = false;
		for (int iI = 0; iI < MAX_CIV_TEAMS; iI++)
		{
			const CvTeam& kLoopTeam = GET_TEAM((TeamTypes)iI);
			if (kLoopTeam.isAlive() && !kLoopTeam.isMinorCiv())
			{
				switch (kTeam.AI_getWarPlan((TeamTypes)iI))
				{
				case NO_WARPLAN:
					break;
				case WARPLAN_PREPARING_TOTAL:
				case WARPLAN_TOTAL:
					bTotalWar = true;
				default:
					bAnyWarPlans = true;
					break;
				}
			}
		}
		iProductionValue += bAnyWarPlans ? 1 : 0;
		iProductionValue += bTotalWar ? 3 : 0;

		if (iProductionValue >= 10)
		{
			m_eStrategyHash |= AI_STRATEGY_PRODUCTION;
		}
		log_strat2(AI_STRATEGY_PRODUCTION, iProductionValue)
	}
	// K-Mod end

	{
		int iOurVictoryCountdown = kTeam.AI_getLowestVictoryCountdown();

		int iTheirVictoryCountdown = MAX_INT;

		for (int iI = 0; iI < MAX_CIV_TEAMS; iI++)
		{
			if ((GET_TEAM((TeamTypes)iI).isAlive()) && //iI != getID()
					// UNOFFICIAL_PATCH, Bugfix, 02/14/10, jdog5000:
					iI != getTeam())
			{
				CvTeamAI& kOtherTeam = GET_TEAM((TeamTypes)iI);
				iTheirVictoryCountdown = std::min(iTheirVictoryCountdown, kOtherTeam.AI_getLowestVictoryCountdown());
			}
		}

		if (MAX_INT == iTheirVictoryCountdown)
		{
			iTheirVictoryCountdown = -1;
		}

		if ((iOurVictoryCountdown >= 0) && (iTheirVictoryCountdown < 0 || iOurVictoryCountdown <= iTheirVictoryCountdown))
		{
			m_eStrategyHash |= AI_STRATEGY_LAST_STAND;
		}
		else if ((iTheirVictoryCountdown >= 0))
		{
			if((iTheirVictoryCountdown < iOurVictoryCountdown))
			{
				m_eStrategyHash |= AI_STRATEGY_FINAL_WAR;
			}
			else if(g.isOption(GAMEOPTION_AGGRESSIVE_AI))
			{
				m_eStrategyHash |= AI_STRATEGY_FINAL_WAR;
			}
			else if(AI_atVictoryStage4() || AI_atVictoryStage(AI_VICTORY_SPACE3))
			{
				m_eStrategyHash |= AI_STRATEGY_FINAL_WAR;
			}
		}

		if (iOurVictoryCountdown < 0)
		{
			if (isCurrentResearchRepeat())
			{
				int iStronger = 0;
				int iAlive = 1;
				for (int iTeam = 0; iTeam < MAX_CIV_TEAMS; iTeam++)
				{
					if (iTeam != getTeam())
					{
						CvTeamAI& kLoopTeam = GET_TEAM((TeamTypes)iTeam);
						if (kLoopTeam.isAlive())
						{
							iAlive++;
							if (kTeam.getPower(true) < kLoopTeam.getPower(true))
							{
								iStronger++;
							}
						}
					}
				}
				if ((iStronger <= 1) || (iStronger <= iAlive / 4))
				{
					m_eStrategyHash |= AI_STRATEGY_FINAL_WAR;
				}
			}
		}
	}

	if (isCurrentResearchRepeat())
	{
		int iTotalVictories = 0; // advc (comment): unused
		int iAchieveVictories = 0;
		int iWarVictories = 0;

		int iThreshold = std::max(1, (g.countCivTeamsAlive() + 1) / 4);

		for (int iVictory = 0; iVictory < GC.getNumVictoryInfos(); iVictory++)
		{
			CvVictoryInfo& kVictory = GC.getInfo((VictoryTypes)iVictory);
			if(!g.isVictoryValid((VictoryTypes)iVictory))
				continue; // advc

			iTotalVictories ++;
			if (kVictory.isDiploVote())
			{
				//
			}
			else if (kVictory.isEndScore())
			{
				int iHigherCount = 0;
				int iWeakerCount = 0;
				for (int iTeam = 0; iTeam < MAX_CIV_TEAMS; iTeam++)
				{
					if (iTeam != getTeam())
					{
						CvTeamAI& kLoopTeam = GET_TEAM((TeamTypes)iTeam);
						if (kLoopTeam.isAlive())
						{
							if (g.getTeamScore(getTeam()) <
									((g.getTeamScore((TeamTypes)iTeam) * 90) / 100))
							{
								iHigherCount++;
								if (kTeam.getPower(true) > kLoopTeam.getPower(true))
								{
									iWeakerCount++;
								}
							}
						}
					}
				}
				if (iHigherCount > 0)
				{
					if (iWeakerCount == iHigherCount)
					{
						iWarVictories++;
					}
				}
			}
			else if (kVictory.getCityCulture() > 0)
			{
				if (m_eStrategyHash & AI_VICTORY_CULTURE1)
				{
					iAchieveVictories++;
				}
			}
			else if (kVictory.getMinLandPercent() > 0 || kVictory.getLandPercent() > 0)
			{
				int iLargerCount = 0;
				for (int iTeam = 0; iTeam < MAX_CIV_TEAMS; iTeam++)
				{
					if (iTeam != getTeam())
					{
						CvTeamAI& kLoopTeam = GET_TEAM((TeamTypes)iTeam);
						if (kLoopTeam.isAlive())
						{
							if (kTeam.getTotalLand(true) < kLoopTeam.getTotalLand(true))
							{
								iLargerCount++;
							}
						}
					}
				}
				if (iLargerCount <= iThreshold)
				{
					iWarVictories++;
				}
			}
			else if (kVictory.isConquest())
			{
				int iStrongerCount = 0;
				for (int iTeam = 0; iTeam < MAX_CIV_TEAMS; iTeam++)
				{
					if (iTeam != getTeam())
					{
						CvTeamAI& kLoopTeam = GET_TEAM((TeamTypes)iTeam);
						if (kLoopTeam.isAlive())
						{
							if (kTeam.getPower(true) < kLoopTeam.getPower(true))
							{
								iStrongerCount++;
							}
						}
					}
				}
				if (iStrongerCount <= iThreshold)
				{
					iWarVictories++;
				}
			}
			else
			{
				if (kTeam.getVictoryCountdown((VictoryTypes)iVictory) > 0)
				{
					iAchieveVictories++;
				}
			}
		}
		if (iAchieveVictories == 0)
		{
			if (iWarVictories > 0)
			{
				m_eStrategyHash |= AI_STRATEGY_FINAL_WAR;
			}
		}
	}

	//Turn off inappropriate strategies.
	//if (g.isOption(GAMEOPTION_ALWAYS_PEACE))
	if (!GET_TEAM(getTeam()).AI_isWarPossible()) // advc.001j
	{
		m_eStrategyHash &= ~AI_STRATEGY_DAGGER;
		m_eStrategyHash &= ~AI_STRATEGY_CRUSH;
		m_eStrategyHash &= ~AI_STRATEGY_ALERT1;
		m_eStrategyHash &= ~AI_STRATEGY_ALERT2;
		m_eStrategyHash &= ~AI_STRATEGY_TURTLE;
		m_eStrategyHash &= ~AI_STRATEGY_FINAL_WAR;
		m_eStrategyHash &= ~AI_STRATEGY_LAST_STAND;
		m_eStrategyHash &= ~AI_STRATEGY_OWABWNW;
		m_eStrategyHash &= ~AI_STRATEGY_FASTMOVERS;
	}
#undef log_strat
#undef log_strat2
}

// K-Mod
void CvPlayerAI::AI_updateGreatPersonWeights()
{
	PROFILE_FUNC();

	m_GreatPersonWeights.clear();

	if (isHuman())
		return; // Humans can use their own reasoning for choosing specialists

	for (int i = 0; i < GC.getNumSpecialistInfos(); i++)
	{
		UnitClassTypes eGreatPersonClass = (UnitClassTypes)GC.getInfo((SpecialistTypes)i).getGreatPeopleUnitClass();

		if (eGreatPersonClass == NO_UNITCLASS)
			continue;

		FAssert(GC.getInfo((SpecialistTypes)i).getGreatPeopleRateChange() > 0);

		if (m_GreatPersonWeights.find(eGreatPersonClass) != m_GreatPersonWeights.end())
			continue; // already evaluated

		UnitTypes eGreatPerson = getCivilization().getUnit(eGreatPersonClass);
		if (eGreatPerson == NO_UNIT)
			continue;

		/* I've disabled the validity check, because 'invalid' specialists still affect the value of the things that enable them.
		bool bValid = isSpecialistValid((SpecialistTypes)i) || i == GC.getDEFAULT_SPECIALIST();
		FOR_EACH_CITY(pLoopCity, *this) {
			if (pLoopCity->getMaxSpecialistCount((SpecialistTypes)i) > 0) {
				bValid = true;
				break;
			}
		}
		if (!bValid)
			continue; */

		// We've estabilish that we can use this specialist, and that they provide great-person points for a unit which
		// we have not yet evaluated. So now we just have to evaluate the unit!
		CvUnitInfo const& kGP = GC.getInfo(eGreatPerson);

		if (kGP.getUnitCombatType() != NO_UNITCOMBAT)
			continue; // don't try to evaluate combat units.

		int iValue = 0;
		// value of joining a city as a super-specialist
		for (int j = 0; j < GC.getNumSpecialistInfos(); j++)
		{
			if (kGP.getGreatPeoples((SpecialistTypes)j))
			{
				FOR_EACH_CITYAI(pLoopCity, *this)
				{
					// Note, specialistValue is roughly 400x the commerce it provides. So /= 4 to make it 100x.
					int iTempValue = pLoopCity->AI_permanentSpecialistValue((SpecialistTypes)j)/4;
					iValue = std::max(iValue, iTempValue);
				}
			}
		} // <advc.opt>
		EraTypes iCurrentEra = getCurrentEra();
		int iCurrentGP = AI_totalUnitAIs(kGP.getDefaultUnitAIType());
		// </advc.opt>
		// value of building something.
		if (kGP.isAnyBuildings()) // advc.003t
		{
			CvCivilization const& kCiv = getCivilization(); // advc.003w 
			for (int j = 0; j < kCiv.getNumBuildings(); j++)
			{
				BuildingTypes eBuilding = kCiv.buildingAt(j);
				/*  <advc.opt> We're looking for the optimal use of the GP. A cheap
					building isn't going to be it. (Can always settle the GP.) */
				int iCost = GC.getInfo(eBuilding).getProductionCost();
				if(iCost > 0 && iCost < (iCurrentEra + 1) * 40)
					continue; // </advc.opt>
				if (//kInfo.getForceBuildings(eBuilding) || // advc.003t
					(kGP.getBuildings(eBuilding) && canConstruct(eBuilding, false, false, true)))
				{
					FOR_EACH_CITYAI(pLoopCity, *this)
					{
						// cf. conditions used in CvUnit::canConstruct
						if (pLoopCity->getNumRealBuilding(eBuilding) > 0 ||
							(/*!kGP.getForceBuildings(eBuilding) &&*/ // advc.003t
							!pLoopCity->canConstruct(eBuilding, false, false, true)))
						{
							continue;
						}
						// Note, building value is roughly 4x the value of the commerce it provides.
						// so we * 25 to match the scale of specialist value.
						int iTempValue = pLoopCity->AI_buildingValue(eBuilding) * 25;

						// if the building is a world wonder, increase the value.
						if (GC.getInfo(eBuilding).isWorldWonder())
							iTempValue = 3*iTempValue/2;

						// reduce the value if we already have great people of this type
						iTempValue /= 1 + iCurrentGP;

						iValue = std::max(iValue, iTempValue);
					}
				}
			}
		}
		// don't bother trying to evaluate bulbing etc. That kind of value is too volatile anyway.

		// store the value in the weights map - but remember, this isn't yet the actual weight.
		FAssert(iValue >= 0);
		m_GreatPersonWeights[eGreatPersonClass] = iValue;
	}

	// find the mean value.
	int iSum = 0;

	std::map<UnitClassTypes, int>::iterator it;
	for (it = m_GreatPersonWeights.begin(); it != m_GreatPersonWeights.end(); ++it)
	{
		iSum += it->second;
	}
	int iMean = iSum / std::max(1, (int)m_GreatPersonWeights.size());

	// scale the values so that they are between 50 and 500 (depending on flavour), with the mean value translating to 100.
	// (note: I don't expect it to get anywhere near the maximum. The maximum only occurs when the value is infinite!)
	const int iMin = AI_getFlavorValue(FLAVOR_SCIENCE) == 0 ? 50 : 35 - AI_getFlavorValue(FLAVOR_SCIENCE);
	FAssert(iMin > 10 && iMin < 95);
	// (smaller iMin means more focus on high-value specialists)
	for (it = m_GreatPersonWeights.begin(); it != m_GreatPersonWeights.end(); ++it)
	{
		int iValue = it->second;
		iValue = 100 * iValue / std::max(1, iMean);
		//iValue = (40000 + 500 * iValue) / (800 + iValue);
		iValue = (800*iMin + (9*100-8*iMin) * iValue) / (800 + iValue);
		it->second = iValue;
	}
}

int CvPlayerAI::AI_getGreatPersonWeight(UnitClassTypes eGreatPerson) const
{
	std::map<UnitClassTypes, int>::const_iterator it = m_GreatPersonWeights.find(eGreatPerson);
	if (it == m_GreatPersonWeights.end())
		return 100;
	else
		return it->second;
}
// K-Mod end

void CvPlayerAI::AI_nowHasTech(TechTypes eTech)
{
	// while its _possible_ to do checks, for financial trouble, and this tech adds financial buildings
	// if in war and this tech adds important war units
	// etc
	// it makes more sense to just redetermine what to produce
	// that is already done every time a civ meets a new civ, it makes sense to do it when a new tech is learned
	// if this is changed, then at a minimum, AI_isFinancialTrouble should be checked
	if (!isHuman())
	{
		int iGameTurn = GC.getGame().getGameTurn();

		// only reset at most every 10 turns
		if (iGameTurn > m_iTurnLastProductionDirty + 10)
		{
			// redeterimine the best things to build in each city
			AI_makeProductionDirty();

			m_iTurnLastProductionDirty = iGameTurn;
		}
	}

}

// K-Mod. This function use to be the bulk of AI_goldToUpgradeAllUnits()
void CvPlayerAI::AI_updateGoldToUpgradeAllUnits()
{
	/*if (m_iUpgradeUnitsCacheTurn == GC.getGame().getGameTurn() && m_iUpgradeUnitsCachedExpThreshold == iExpThreshold)
		return m_iUpgradeUnitsCachedGold;*/

	int iTotalGold = 0;

	// cache the value for each unit type
	std::vector<int> aiUnitUpgradePrice(GC.getNumUnitInfos(), 0);	// initializes to zeros

	FOR_EACH_UNITAI(pLoopUnit, *this)
	{
		/*
		// if experience is below threshold, skip this unit
		if (pLoopUnit->getExperience() < iExpThreshold)
			continue;*/

		UnitTypes eUnitType = pLoopUnit->getUnitType();

		// check cached value for this unit type
		int iCachedUnitGold = aiUnitUpgradePrice[eUnitType];
		if (iCachedUnitGold != 0)
		{
			// if positive, add it to the sum
			if (iCachedUnitGold > 0)
			{
				iTotalGold += iCachedUnitGold;
			}

			// either way, done with this unit
			continue;
		}

		int iUnitGold = 0;
		int iUnitUpgradePossibilities = 0;

		UnitAITypes eUnitAIType = pLoopUnit->AI_getUnitAIType();
		CvArea* pUnitArea = pLoopUnit->area();
		int iUnitValue = AI_unitValue(eUnitType, eUnitAIType, pUnitArea);

		CvCivilization const& kCiv = getCivilization(); // advc.003w
		for (int i = 0; i < kCiv.getNumUnits(); i++)
		{
			UnitTypes eUpgradeUnitType = kCiv.unitAt(i);
			UnitClassTypes eUpgradeUnitClassType = kCiv.unitClassAt(i);

			// is it better?
			int iUpgradeValue = AI_unitValue(eUpgradeUnitType, eUnitAIType, pUnitArea);
			if (iUpgradeValue > iUnitValue)
			{
				// is this a valid upgrade?
				if (pLoopUnit->upgradeAvailable(eUnitType, eUpgradeUnitClassType))
				{
					// can we actually make this upgrade?
					bool bCanUpgrade = false;
					CvCity* pCapitalCity = getCapitalCity();
					if (pCapitalCity != NULL && pCapitalCity->canTrain(eUpgradeUnitType))
						bCanUpgrade = true;
					else
					{
						CvCity* pCloseCity = GC.getMap().findCity(pLoopUnit->getX(), pLoopUnit->getY(),
								getID(), NO_TEAM, true, (pLoopUnit->getDomainType() == DOMAIN_SEA));
						if (pCloseCity != NULL && pCloseCity->canTrain(eUpgradeUnitType))
							bCanUpgrade = true;
					}

					if (bCanUpgrade)
					{
						iUnitGold += pLoopUnit->upgradePrice(eUpgradeUnitType);
						iUnitUpgradePossibilities++;
					}
				}
			}
		}

		// if we found any, find average and add to total
		if (iUnitUpgradePossibilities > 0)
		{
			iUnitGold /= iUnitUpgradePossibilities;

			// add to cache
			aiUnitUpgradePrice[eUnitType] = iUnitGold;

			// add to sum
			iTotalGold += iUnitGold;
		}
		else
		{
			// add to cache, dont upgrade to this type
			aiUnitUpgradePrice[eUnitType] = -1;
		}
	}

	/*m_iUpgradeUnitsCacheTurn = GC.getGame().getGameTurn();
	m_iUpgradeUnitsCachedExpThreshold = iExpThreshold; */
	m_iUpgradeUnitsCachedGold = iTotalGold;

	//return iTotalGold;
}

// K-Mod. 'available income' is meant to roughly represent the amount of gold-per-turn the player would produce with 100% gold on the commerce slider.
// (without subtracting costs)
// In the original code, this value was essentially calculated by simply adding total gold to total science.
// This new version is better able to handle civs which are using culture or espionage on their commerce slider.
void CvPlayerAI::AI_updateAvailableIncome()
{
	int iTotalRaw = calculateTotalYield(YIELD_COMMERCE);

	m_iAvailableIncome = iTotalRaw * AI_averageCommerceMultiplier(COMMERCE_GOLD) / 100;
	m_iAvailableIncome += std::max(0, getGoldPerTurn());
	m_iAvailableIncome += getCommerceRate(COMMERCE_GOLD) - iTotalRaw * AI_averageCommerceMultiplier(COMMERCE_GOLD) * getCommercePercent(COMMERCE_GOLD) / 10000;
}

// Estimate what percentage of commerce is needed on gold to cover our expenses
int CvPlayerAI::AI_estimateBreakEvenGoldPercent() const
{
	PROFILE_FUNC();
	//int iGoldCommerceRate = kPlayer.getCommercePercent(COMMERCE_GOLD); // (rough approximation)

	// (detailed approximation)
	int iTotalRaw = calculateTotalYield(YIELD_COMMERCE);
	int iExpenses = calculateInflatedCosts() * 12 / 10; // (20% increase to approximate other costs)

	// estimate how much gold we get from specialists & buildings by taking our current gold rate and subtracting what it would be from raw commerce alone.
	iExpenses -= getCommerceRate(COMMERCE_GOLD) - iTotalRaw * AI_averageCommerceMultiplier(COMMERCE_GOLD) * getCommercePercent(COMMERCE_GOLD) / 10000;

	// divide what's left to determine what our gold slider would need to be to break even.
	int iGoldCommerceRate = 10000 * iExpenses / (iTotalRaw * AI_averageCommerceMultiplier(COMMERCE_GOLD)
			+ 1); // advc.001: Caused a division by 0 in the EarthAD1000 scenario

	iGoldCommerceRate = range(iGoldCommerceRate, 0, 100); // (perhaps it would be useful to just return the unbounded value?)

	return iGoldCommerceRate;
}
// K-Mod end

int CvPlayerAI::AI_goldTradeValuePercent() const
{
	int iValue = 2;
	// advc.036: Commented out
	/*if (AI_isFinancialTrouble())
		iValue += 1;*/
	return 100 * iValue;

}

int CvPlayerAI::AI_averageYieldMultiplier(YieldTypes eYield) const
{
	FAssert(eYield > -1);
	FAssert(eYield < NUM_YIELD_TYPES);

	/*if (m_iAveragesCacheTurn != GC.getGame().getGameTurn())
		AI_calculateAverages();*/

	FAssert(m_aiAverageYieldMultiplier[eYield] > 0);
	return m_aiAverageYieldMultiplier[eYield];
}

int CvPlayerAI::AI_averageCommerceMultiplier(CommerceTypes eCommerce) const
{
	FAssert(eCommerce > -1);
	FAssert(eCommerce < NUM_COMMERCE_TYPES);

	/*if (m_iAveragesCacheTurn != GC.getGame().getGameTurn())
		AI_calculateAverages();*/

	return m_aiAverageCommerceMultiplier[eCommerce];
}

int CvPlayerAI::AI_averageGreatPeopleMultiplier() const
{
	/*if (m_iAveragesCacheTurn != GC.getGame().getGameTurn())
		AI_calculateAverages();*/
	return m_iAverageGreatPeopleMultiplier;
}

// K-Mod
int CvPlayerAI::AI_averageCulturePressure() const
{
	/*if (m_iAveragesCacheTurn != GC.getGame().getGameTurn())
		AI_calculateAverages();*/
	return m_iAverageCulturePressure;
}

//"100 eCommerce is worth (return) raw YIELD_COMMERCE
int CvPlayerAI::AI_averageCommerceExchange(CommerceTypes eCommerce) const
{
	FAssert(eCommerce > -1);
	FAssert(eCommerce < NUM_COMMERCE_TYPES);

	/*if (m_iAveragesCacheTurn != GC.getGame().getGameTurn())
		AI_calculateAverages();*/

	return m_aiAverageCommerceExchange[eCommerce];
}

void CvPlayerAI::AI_calculateAverages()
{
	for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		m_aiAverageYieldMultiplier[iI] = 0;
	}
	for (int iI = 0; iI < NUM_COMMERCE_TYPES; iI++)
	{
		m_aiAverageCommerceMultiplier[iI] = 0;
	}
	m_iAverageGreatPeopleMultiplier = 0;

	int iTotalPopulation = 0;

	FOR_EACH_CITYAI(pLoopCity, *this)
	{
		int iPopulation = std::max(pLoopCity->getPopulation(), NUM_CITY_PLOTS);
		iTotalPopulation += iPopulation;

		for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
		{
			m_aiAverageYieldMultiplier[iI] += iPopulation * pLoopCity->AI_yieldMultiplier((YieldTypes)iI);
		}
		for (int iI = 0; iI < NUM_COMMERCE_TYPES; iI++)
		{
			m_aiAverageCommerceMultiplier[iI] += iPopulation * pLoopCity->getTotalCommerceRateModifier((CommerceTypes)iI);
		}
		m_iAverageGreatPeopleMultiplier += iPopulation * pLoopCity->getTotalGreatPeopleRateModifier();
	}


	if (iTotalPopulation > 0)
	{
		for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
		{
			m_aiAverageYieldMultiplier[iI] /= iTotalPopulation;
			FAssert(m_aiAverageYieldMultiplier[iI] > 0);
		}
		for (int iI = 0; iI < NUM_COMMERCE_TYPES; iI++)
		{
			m_aiAverageCommerceMultiplier[iI] /= iTotalPopulation;
			FAssert(m_aiAverageCommerceMultiplier[iI] > 0);
		}
		m_iAverageGreatPeopleMultiplier /= iTotalPopulation;
		FAssert(m_iAverageGreatPeopleMultiplier > 0);
	}
	else
	{
		for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
		{
			m_aiAverageYieldMultiplier[iI] = 100;
		}
		for (int iI = 0; iI < NUM_COMMERCE_TYPES; iI++)
		{
			m_aiAverageCommerceMultiplier[iI] = 100;
		}
		m_iAverageGreatPeopleMultiplier = 100;
	}


	//Calculate Exchange Rate

	for (int iI = 0; iI < NUM_COMMERCE_TYPES; iI++)
	{
		m_aiAverageCommerceExchange[iI] = 0;
	}

	int iCommerce = 0;
	int iTotalCommerce = 0;
	FOR_EACH_CITY(pLoopCity, *this)
	{
		iCommerce = pLoopCity->getYieldRate(YIELD_COMMERCE);
		iTotalCommerce += iCommerce;

		int iExtraCommerce = 0;
		for (int iI = 0; iI < NUM_COMMERCE_TYPES; iI++)
		{
			iExtraCommerce +=((pLoopCity->getSpecialistPopulation() + pLoopCity->getNumGreatPeople()) * getSpecialistExtraCommerce((CommerceTypes)iI));
			iExtraCommerce += (pLoopCity->getBuildingCommerce((CommerceTypes)iI) + pLoopCity->getSpecialistCommerce((CommerceTypes)iI) + pLoopCity->getReligionCommerce((CommerceTypes)iI) + getFreeCityCommerce((CommerceTypes)iI));
		}
		iTotalCommerce += iExtraCommerce;

		for (int iI = 0; iI < NUM_COMMERCE_TYPES; iI++)
		{
			m_aiAverageCommerceExchange[iI] += ((iCommerce + iExtraCommerce) * pLoopCity->getTotalCommerceRateModifier((CommerceTypes)iI)) / 100;
		}
	}

	for (int iI = 0; iI < NUM_COMMERCE_TYPES; iI++)
	{
		if (m_aiAverageCommerceExchange[iI] > 0)
		{
			m_aiAverageCommerceExchange[iI] = (100 * iTotalCommerce) / m_aiAverageCommerceExchange[iI];
		}
		else
		{
			m_aiAverageCommerceExchange[iI] = 100;
		}
	}

	// K-Mod Culture pressure

	int iTotal = 0;
	int iWeightedTotal = 0;
	FOR_EACH_CITYAI(pLoopCity, *this)
	{
		int iCultureRate = pLoopCity->getCommerceRateTimes100(COMMERCE_CULTURE);
		iTotal += iCultureRate;
		iWeightedTotal += iCultureRate * pLoopCity->AI_culturePressureFactor();
	}
	if (iTotal == 0)
		m_iAverageCulturePressure = 100;
	else
		m_iAverageCulturePressure = iWeightedTotal / iTotal;

	//m_iAveragesCacheTurn = GC.getGame().getGameTurn();
}

// K-Mod edition
void CvPlayerAI::AI_convertUnitAITypesForCrush()
{
	std::map<int, int> spare_units;
	std::vector<std::pair<int, int> > unit_list; // { score, unitID }.
	// note unitID is used rather than CvUnit* to ensure that the list gives the same order for players on different computers.

	FOR_EACH_AREA(pLoopArea)
	{
		// Keep 1/2 of recommended floating defenders.
		if (pLoopArea == NULL || pLoopArea->getAreaAIType(getTeam()) == AREAAI_ASSAULT ||
			pLoopArea->getAreaAIType(getTeam()) == AREAAI_DEFENSIVE)
		{
			spare_units[pLoopArea->getID()] = 0;
		}
		else
		{
			spare_units[pLoopArea->getID()] = (2 * AI_getTotalFloatingDefenders(*pLoopArea) -
					AI_getTotalFloatingDefendersNeeded(*pLoopArea))/2;
		}
	}
	FOR_EACH_UNITAI(pLoopUnit, *this)
	{
		bool bValid = false;

		/* if (pLoopUnit->AI_getUnitAIType() == UNITAI_RESERVE
			|| pLoopUnit->AI_isCityAIType() && (pLoopUnit->getExtraCityDefensePercent() <= 0)) */
		// K-Mod, protective leaders might still want to use their gunpowder units...
		if (pLoopUnit->AI_getUnitAIType() == UNITAI_RESERVE || pLoopUnit->AI_getUnitAIType() == UNITAI_COLLATERAL
			|| (pLoopUnit->AI_isCityAIType() && pLoopUnit->getExtraCityDefensePercent()		
			+ pLoopUnit->getUnitInfo().getCityDefenseModifier() / 2 // cdtw
			<= 30))
		{
			bValid = true;
		}

		/*if ((pLoopUnit->getArea().getAreaAIType(getTeam()) == AREAAI_ASSAULT) ||
			(pLoopUnit->getArea().getAreaAIType(getTeam()) == AREAAI_DEFENSIVE))
		{
			bValid = false;
		}*/
		if (!pLoopUnit->canAttack() || (pLoopUnit->AI_getUnitAIType() == UNITAI_CITY_SPECIAL) ||
			pLoopUnit->getUnitInfo().isMostlyDefensive()) // advc.315
		{
			bValid = false;
		}
		if (spare_units[pLoopUnit->getArea().getID()] <= 0)
			bValid = false;

		if (bValid)
		{
			CvPlot const& kPlot = pLoopUnit->getPlot();
			CvCity const* pCity = kPlot.getPlotCity();
			if (pCity != NULL)
			{
				if (pCity->getOwner() == getID())
				{
					if (kPlot.getBestDefender(getID()) == pLoopUnit)
						bValid = false;
				}
				/*if (pLoopUnit->AI_getUnitAIType() == UNITAI_CITY_DEFENSE && GET_TEAM(getTeam()).getAtWarCount(true) > 0) {
					int iCityDefenders = pLoopUnit->getPlot().plotCount(PUF_isUnitAIType, UNITAI_CITY_DEFENSE, -1, getID());
					if (iCityDefenders <= pLoopUnit->getPlot().getPlotCity()->AI_minDefenders())
						bValid = false;*/
				if (pLoopUnit->AI_getGroup()->AI_getMissionAIType() == MISSIONAI_GUARD_CITY)
				{
					int iDefendersHere = kPlot.plotCount(PUF_isMissionAIType, MISSIONAI_GUARD_CITY, -1, getID());
					int iDefendersWant = pLoopUnit->getArea().getAreaAIType(getTeam()) == AREAAI_DEFENSIVE ?
							kPlot.AI_getPlotCity()->AI_neededDefenders() :
							kPlot.AI_getPlotCity()->AI_minDefenders();

					if (iDefendersHere <= iDefendersWant)
						bValid = false;
				}
			}
		}

		if (bValid)
		{
			int iValue = AI_unitValue(pLoopUnit->getUnitType(), UNITAI_ATTACK_CITY, pLoopUnit->area());
			unit_list.push_back(std::make_pair(iValue, pLoopUnit->getID()));
		}
	}

	// convert the highest scoring units first.
	std::sort(unit_list.begin(), unit_list.end(), std::greater<std::pair<int, int> >());
	std::vector<std::pair<int, int> >::iterator it;
	for (it = unit_list.begin(); it != unit_list.end(); ++it)
	{
		if (it->first > 0 && spare_units[getUnit(it->second)->getArea().getID()] > 0)
		{
			if (gPlayerLogLevel >= 2)
			{
				CvWString sOldType;
				getUnitAIString(sOldType, getUnit(it->second)->AI_getUnitAIType());
				logBBAI("    %S converts %S from %S to attack city for crush. (%d)", getName(), getUnit(it->second)->getName().GetCString(), sOldType.GetCString(), getUnit(it->second)->getID());
			}

			AI_getUnit(it->second)->AI_setUnitAIType(UNITAI_ATTACK_CITY);
			// only convert half of our spare units, so that we can reevaluate which units we need before converting more.
			spare_units[getUnit(it->second)->getArea().getID()]-=2;
		}
	}
}

int CvPlayerAI::AI_playerCloseness(PlayerTypes eIndex, int iMaxDistance,
	bool bConstCache) const // advc.001n
{
	//PROFILE_FUNC(); // advc.003o (the cache seems to be very effective)
	FAssert(GET_PLAYER(eIndex).isAlive());
	FAssert(eIndex != getID());

	int iValue = 0;
	FOR_EACH_CITYAI(pLoopCity, *this)
	{
		iValue += pLoopCity->AI_playerCloseness(eIndex, iMaxDistance,
				bConstCache); // advc.001n
	}
	return iValue;
}


int CvPlayerAI::AI_getTotalAreaCityThreat(CvArea const& kArea) const
{
	PROFILE_FUNC();
	int r = 0;
	FOR_EACH_CITYAI(pCity, *this)
	{
		if (pCity->isArea(kArea))
			r += pCity->AI_cityThreat();
	}
	return r;
}

// advc.099c:
int CvPlayerAI::AI_getAreaCultureDefendersNeeded(CvArea const& kArea) const
{
	int r = 0;
	FOR_EACH_CITYAI(pCity, *this)
	{
		if (pCity->isArea(kArea))
			r += pCity->AI_neededCultureDefenders();
	}
	return r;
}

int CvPlayerAI::AI_countNumAreaHostileUnits(CvArea const& kArea, bool bPlayer, bool bTeam,
	bool bNeutral, bool bHostile, /* advc.081: */ CvPlot* pCenter) const
{
	PROFILE_FUNC();

	CvMap const& m = GC.getMap();
	int iCount = 0;
	if (pCenter == NULL) // advc.081
	{
		for (int iI = 0; iI < m.numPlots(); iI++)
		{
			CvPlot const& kPlot = m.getPlotByIndex(iI);
			// <advc.081> Moved into auxiliary function
			if (kPlot.isArea(kArea))
			{
				iCount += kPlot.countHostileUnits(getID(), bPlayer, bTeam,
						bNeutral, bHostile);
			}
		}
	}
	else
	{
		// We set the range, not the caller - after all, this is an AI function.
		for (SquareIter it(*pCenter, 13 + 2 * getCurrentEra()); it.hasNext(); ++it)
		{
			if (it->isArea(kArea))
				iCount += it->countHostileUnits(getID(), bPlayer, bTeam, bNeutral, bHostile);
		}
	} // </advc.081>
	return iCount;
}

//this doesn't include the minimal one or two garrison units in each city.
int CvPlayerAI::AI_getTotalFloatingDefendersNeeded(CvArea const& kArea, // advc: was CvArea*
	bool bDebug) const // advc.007: For displaying the correct count for humans in Debug text
{
	PROFILE_FUNC();
	// <advc>
	FAssert(!isBarbarian());
	CvGame const& g = GC.getGame();
	AreaAITypes const eAreaAI = kArea.getAreaAIType(getTeam()); // advc
	/*iDefenders = 1 + ((iCurrentEra + ((g.getMaxCityElimination() > 0) ? 3 : 2)) * iAreaCities);
	iDefenders /= 3;
	iDefenders += kArea.getPopulationPerPlayer(getID()) / 7;*/ // BtS
	// K-Mod  // advc.107: Use times-1000 precision
	int const iCityFactor = ::round(1000 *
			// Don't quite want floating defenders to increase linearly with the number of cities
			std::pow((double)kArea.getCitiesPerPlayer(getID()), 0.85));
	int iDefendersPermil = iCityFactor;
	iDefendersPermil += 1000 * (1 + AI_totalAreaUnitAIs(kArea, UNITAI_SETTLE));
	iDefendersPermil += (1000 * kArea.getPopulationPerPlayer(getID())) / 8; // advc.107: was /7 (but rounding loss was much higher)
	if (AI_isLandWar(kArea) /* advc.107: */ && AI_isFocusWar(&kArea))
	{
		int iEnemyCityFactor = 1000 * GET_TEAM(getTeam()).AI_countEnemyCitiesByArea(kArea);
		// advc.107:
		iEnemyCityFactor = std::min(iEnemyCityFactor, (2 * iCityFactor) / 3);
		iDefendersPermil += 1000 + // (2000 +
				/*  advc.107: Want to focus on aggressive build-up while preparing.
					Can still train some extra defenders once war is imminent. */
				((!GET_TEAM(getTeam()).AI_isSneakAttackPreparing() ? 0 : 2000) +
				iEnemyCityFactor) / 3;
	}
	{
		int const iEra = std::max(0, getCurrentEra() - g.getStartEra() / 2);
		iDefendersPermil *= iEra + (g.getMaxCityElimination() > 0 ? 5 : 4); // advc.107: was 3:2
		iDefendersPermil /= 6; // advc.107: was /=3
	}
	// K-Mod end
	if (eAreaAI == AREAAI_DEFENSIVE)
	{
		//iDefenders *= 2;
		// <advc.107>
		iDefendersPermil *= 3;
		iDefendersPermil /= 2;
		// </advc.107>
	}
	else
	{
		if (AI_isDoStrategy(AI_STRATEGY_ALERT2, /* advc.007: */ bDebug))
		{
			//iDefendersPermil *= 2;  // <advc.107>
			iDefendersPermil *= 5;
			iDefendersPermil /= 3; // </advc.107>
		}
		else if (AI_isDoStrategy(AI_STRATEGY_ALERT1, /* advc.007: */ bDebug))
		{
			iDefendersPermil *= 3;
			iDefendersPermil /= 2;
		}
		//else // advc.022: Allow AreaAI to cancel out alertness
		if (eAreaAI == AREAAI_OFFENSIVE)
		{
			iDefendersPermil *= 2;
			iDefendersPermil /= 3;
		}
		else if (eAreaAI == AREAAI_MASSING)
		{
			if (GET_TEAM(getTeam()).AI_getEnemyPowerPercent(true) < // bbai
				10 + GC.getInfo(getPersonalityType()).getMaxWarNearbyPowerRatio())
			{
				iDefendersPermil *= 2;
				iDefendersPermil /= 3;
			}
		}
	}

	/*if (AI_getTotalAreaCityThreat(kArea) == 0)
		iDefendersPermil /= 2;*/
	/*  <advc.107> Faster. Not per-area, but I don't have much confidence in
		AI_getTotalAreaCityThreat==0 identifying safe areas either. */
	if (AI_feelsSafe())
		iDefendersPermil = (2 * iDefendersPermil) / 5; // </advc.107>

	if (!g.isOption(GAMEOPTION_AGGRESSIVE_AI))
	{
		iDefendersPermil *= 2;
		iDefendersPermil /= 3;
	}

	// BBAI: Removed AI_STRATEGY_GET_BETTER_UNITS reduction, it was reducing defenses twice

	if (AI_atVictoryStage(AI_VICTORY_CULTURE3))
	{
		iDefendersPermil += (3 * iCityFactor) / 2; // advc.107: instead of +=2*iCityFactor
		if (eAreaAI == AREAAI_DEFENSIVE
			&& AI_atVictoryStage(AI_VICTORY_CULTURE4)) // K-Mod
		{
			iDefendersPermil *= 2; //go crazy
		}
	}
	/*iDefendersPermil *= 60;
	iDefendersPermil /= std::max(30, (GC.getInfo(g.getHandicapType()).getAITrainPercent() - 20));*/
	/*  <advc.107> Replacing the above, which doesn't take into account the
		progressive AI discounts. Fewer defenders on low difficulty, more on
		high difficulty. Times 0.95 for normalization. */
	iDefendersPermil = ::round((iDefendersPermil * 0.95) /
			::dRange(trainingModifierFromHandicap(), 0.75, 1.5));
	// Replacing code below (extra defenses vs. Raging Barbarians)
	if(g.isOption(GAMEOPTION_RAGING_BARBARIANS) &&
		!GC.getInfo(getCurrentEra()).isNoBarbUnits())
	{
		int iStartEra = g.getStartEra();
		if(g.getCurrentEra() <= 1 + iStartEra)
			iDefendersPermil += 1000 * (1 + iStartEra);
	}
	/*if (iEra < 3 && GC.getGame().isOption(GAMEOPTION_RAGING_BARBARIANS))
		iDefendersPermil += 2;*/ // </advc.107>

	if (getCapitalCity() != NULL && !getCapitalCity()->isArea(kArea))
	{
		// Defend offshore islands only lightly.
		int iUpperBound = ((iCityFactor * iCityFactor) / 1000 - 1000);
		//iDefendersPermil = std::min(iDefendersPermil, iUpperBound);
		// UNOFFICIAL_PATCH, Bugfix, War tactics AI, 01/23/09, jdog5000: START
		// Lessen defensive requirements only if not being attacked locally
		/*  This may be our first city captured on a large enemy continent,
			need defenses to scale up based on total number of area cities,
			not just ours. */
		// advc.107: Check if we actually have plans to expand our presence
		if (eAreaAI == AREAAI_OFFENSIVE || eAreaAI == AREAAI_MASSING)
			iUpperBound += 1000 * (kArea.getNumCities() - kArea.getCitiesPerPlayer(getID()));
		if (eAreaAI == AREAAI_DEFENSIVE)
		{
			// UNOFFICIAL_PATCH: END
			/*  <advc.107> Still apply an upper bound in that case.
				Defending colonies may well be a lost cause. */
			iUpperBound += 1000 * kArea.getNumCities() / 2;
		}
		iDefendersPermil = std::min(iDefendersPermil, iUpperBound); // </advc.107>
	}
	// <advc.099c> A little extra for quelling revolts
	int iCultureDefendersNeeded = AI_getAreaCultureDefendersNeeded(kArea);
	if (iDefendersPermil < 2250 * iCultureDefendersNeeded)
	{
		// Proper defenders should mostly be able to double as culture defenders
		iDefendersPermil += std::min((iDefendersPermil * 25) / 100,
				iCultureDefendersNeeded * 300);
	} // </advc.099c>
	return iDefendersPermil / 1000;
}

int CvPlayerAI::AI_getTotalFloatingDefenders(CvArea const& kArea) const
{
	PROFILE_FUNC();

	int iCount = 0;
	iCount += AI_totalAreaUnitAIs(kArea, UNITAI_COLLATERAL);
	iCount += AI_totalAreaUnitAIs(kArea, UNITAI_RESERVE);
	iCount += std::max(0, (AI_totalAreaUnitAIs(kArea, UNITAI_CITY_DEFENSE) -
			kArea.getCitiesPerPlayer(getID()) * 2));
	iCount += AI_totalAreaUnitAIs(kArea, UNITAI_CITY_COUNTER);
	iCount += AI_totalAreaUnitAIs(kArea, UNITAI_CITY_SPECIAL);
	return iCount;
}

// K-Mod. (very basic just as a starting point. I'll refine this later.)
int CvPlayerAI::AI_getTotalAirDefendersNeeded() const
{
	int iNeeded = getNumCities() + 1;
	//iNeeded = iNeeded + iNeeded*(getCurrentEra()+1) / std::max(1, GC.getNumEraInfos()*2);
	// Todo. Adjust based on what other civs are doing.

	if (AI_atVictoryStage(AI_VICTORY_CULTURE4 | AI_VICTORY_SPACE4))
		iNeeded = iNeeded*3/2;
	if (AI_isDoStrategy(AI_STRATEGY_ALERT2))
		iNeeded = iNeeded*3/2;

	return iNeeded;
} // K-Mod end


RouteTypes CvPlayerAI::AI_bestAdvancedStartRoute(CvPlot* pPlot, int* piYieldValue) const
{
	RouteTypes eBestRoute = NO_ROUTE;
	int iBestValue = -1;
	ImprovementTypes const ePlotImprov = pPlot->getImprovementType();
	int aiYieldWeight[] = { 100, 60, 40 }; // advc
	FOR_EACH_ENUM(Route)
	{
		int iValue = 0;
		int iCost = getAdvancedStartRouteCost(eLoopRoute, true, pPlot);
		if (iCost < 0)
			continue; // advc
		iValue += GC.getInfo(eLoopRoute).getValue();
		if (iValue <= 0)
			continue; // advc
		int iYieldValue = 0;
		if (ePlotImprov != NO_IMPROVEMENT)
		{
			FOR_EACH_ENUM(Yield)
			{
				iYieldValue += (GC.getInfo(ePlotImprov).getRouteYieldChanges(
						eLoopRoute, eLoopYield)) * aiYieldWeight[eLoopYield];
			}
		}
		iValue *= 1000;
		iValue /= (1 + iCost);
		if (iValue > iBestValue)
		{
			iBestValue = iValue;
			eBestRoute = eLoopRoute;
			if (piYieldValue != NULL)
				*piYieldValue = iYieldValue;
		}
	}
	return eBestRoute;
}

UnitTypes CvPlayerAI::AI_bestAdvancedStartUnitAI(CvPlot const& kPlot, // advc: CvPlot const&; style changes to reduce indentation.
	UnitAITypes eUnitAI) const
{
	FAssertMsg(eUnitAI != NO_UNITAI, "UnitAI is not assigned a valid value");

	int iBestValue = 0;
	UnitTypes eBestUnit = NO_UNIT;
	CvCivilization const& kCiv = getCivilization(); // advc.003w
	for (int i = 0; i < kCiv.getNumUnits(); i++)
	{
		UnitTypes eLoopUnit = kCiv.unitAt(i);
		UnitClassTypes eLoopUnitClass = kCiv.unitClassAt(i);
		//if (!isHuman() || (GC.getInfo(eLoopUnit).getDefaultUnitAIType() == eUnitAI)) {
		int iUnitCost = getAdvancedStartUnitCost(eLoopUnit, true, &kPlot);
		if (iUnitCost < 0)
			continue;
		int iValue = AI_unitValue(eLoopUnit, eUnitAI, kPlot.area());
		if (iValue <= 0)
			continue;

		//free promotions. slow?
		//only 1 promotion per source is counted (ie protective isn't counted twice)
		int iPromotionValue = 0;

		//special to the unit
		for (int iJ = 0; iJ < GC.getNumPromotionInfos(); iJ++)
		{
			if (GC.getInfo(eLoopUnit).getFreePromotions(iJ))
			{
				iPromotionValue += 15;
				break;
			}
		}

		for (int iJ = 0; iJ < GC.getNumPromotionInfos(); iJ++)
		{
			if (isFreePromotion(GC.getInfo(eLoopUnit).getUnitCombatType(), (PromotionTypes)iJ))
			{
				iPromotionValue += 15;
				break;
			}

			if (isFreePromotion(GC.getInfo(eLoopUnit).getUnitClassType(), (PromotionTypes)iJ))
			{
				iPromotionValue += 15;
				break;
			}
		}

		//traits
		for (int iJ = 0; iJ < GC.getNumTraitInfos(); iJ++)
		{
			if (hasTrait((TraitTypes)iJ))
			{
				for (int iK = 0; iK < GC.getNumPromotionInfos(); iK++)
				{
					if (GC.getInfo((TraitTypes) iJ).isFreePromotion(iK))
					{
						if ((GC.getInfo(eLoopUnit).getUnitCombatType() != NO_UNITCOMBAT) &&
							GC.getInfo((TraitTypes) iJ).isFreePromotionUnitCombat(GC.getInfo(eLoopUnit).getUnitCombatType()))
						{
							iPromotionValue += 15;
							break;
						}
					}
				}
			}
		}

		iValue *= (iPromotionValue + 100);
		iValue /= 100;

		iValue *= (GC.getGame().getSorenRandNum(40, "AI Best Advanced Start Unit") + 100);
		iValue /= 100;

		iValue *= (getNumCities() + 2);
		iValue /= (getUnitClassCountPlusMaking(eLoopUnitClass) + getNumCities() + 2);

		FAssert((MAX_INT / 1000) > iValue);
		iValue *= 1000;

		iValue /= 1 + iUnitCost;
		iValue = std::max(1, iValue);

		if (iValue > iBestValue)
		{
			iBestValue = iValue;
			eBestUnit = eLoopUnit;
		}
	}
	return eBestUnit;
}

CvPlot* CvPlayerAI::AI_advancedStartFindCapitalPlot()  // advc: style changes
{	// Not quite what Kek-Mod does, but would have the same result:
	// <dlph.35> "Don't exchange team members starting location."
	/*CvPlot* pCurrentStart = getStartingPlot();
	if(pCurrentStart != NULL && getAdvancedStartCityCost(true, pCurrentStart) > 0)
		return pCurrentStart;*/ // </dlph.35>
	// advc: However, I don't want to make that change for now.
	CvMap const& m = GC.getMap();
	CvPlot* pBestPlot = NULL;
	int iBestValue = -1;
	for (int iPlayer = 0; iPlayer < MAX_PLAYERS; iPlayer++)
	{
		CvPlayer& kMember = GET_PLAYER((PlayerTypes)iPlayer);
		if (!kMember.isAlive() || kMember.getTeam() != getTeam())
			continue;

		CvPlot* pLoopPlot = kMember.getStartingPlot();
		if (pLoopPlot == NULL)
		{
			FAssertMsg(false, "StartingPlot for a live player is NULL!");
			continue;
		}
		if (getAdvancedStartCityCost(true, pLoopPlot) <= 0)
			continue;

		int iValue = 1000;
		if (kMember.getID() == getID())
			iValue += 1000;
		else iValue += GC.getGame().getSorenRandNum(100, "AI Advanced Start Choose Team Start");

		int iX = pLoopPlot->getX();
		int iY = pLoopPlot->getY();
		CvCity* pNearestCity = m.findCity(iX, iY, NO_PLAYER, getTeam());
		if (NULL != pNearestCity)
		{
			FAssert(pNearestCity->getTeam() == getTeam());
			int iDistance = m.stepDistance(iX, iY, pNearestCity->getX(), pNearestCity->getY());
			if (iDistance < 10)
				iValue /= 10 - iDistance;
		}
		if (iValue > iBestValue)
		{
			iBestValue = iValue;
			pBestPlot = pLoopPlot;
		}
	}

	if (pBestPlot != NULL)
		return pBestPlot;

	FAssertMsg(false, "AS: Failed to find a starting plot for a player");
	//Execution should almost never reach here.

	//Update found values just in case - particulary important for simultaneous turns.
	AI_updateFoundValues(/* dlph.35 (bugfix): */ true);

	pBestPlot = NULL;
	iBestValue = -1;

	if (getStartingPlot() != NULL)
	{
		for (int iI = 0; iI < m.numPlots(); iI++)
		{
			CvPlot* pLoopPlot = m.plotByIndex(iI);
			if (pLoopPlot->sameArea(*getStartingPlot()))
			{
				int iValue = pLoopPlot->getFoundValue(getID());
				if (iValue > 0)
				{
					if (getAdvancedStartCityCost(true, pLoopPlot) > 0)
					{
						if (iValue > iBestValue)
						{
							iBestValue = iValue;
							pBestPlot = pLoopPlot;
						}
					}
				}
			}
		}
	}

	if (pBestPlot != NULL)
		return pBestPlot;

	//Commence panic.
	FAssertMsg(false, "Failed to find an advanced start starting plot");
	return NULL;
}


bool CvPlayerAI::AI_advancedStartPlaceExploreUnits(bool bLand)  // advc: some style changes
{
	CvPlot* pBestExplorePlot = NULL;
	int iBestExploreValue = 0;
	UnitTypes eBestUnitType = NO_UNIT;

	UnitAITypes eUnitAI = NO_UNITAI;
	if (bLand)
		eUnitAI = UNITAI_EXPLORE;
	else eUnitAI = UNITAI_EXPLORE_SEA;

	FOR_EACH_CITY(pLoopCity, *this)
	{
		CvPlot& kCityPlot = *pLoopCity->plot();
		CvArea* pLoopArea = (bLand ? pLoopCity->area() : kCityPlot.waterArea());
		if (pLoopArea == NULL)
			continue;

		int iValue = std::max(0, pLoopArea->getNumUnrevealedTiles(getTeam()) - 10) * 10;
		iValue += std::max(0, pLoopArea->getNumTiles() - 50);
		if (iValue <= 0)
			continue;

		int iOtherPlotCount = 0;
		int iGoodyCount = 0;
		int iExplorerCount = 0;

		for (SquareIter it(kCityPlot, 4); it.hasNext(); ++it)
		{
			CvPlot const& p = *it;
			iExplorerCount += p.plotCount(PUF_isUnitAIType, eUnitAI, -1, NO_PLAYER, getTeam());
			if (p.isArea(*pLoopArea))
			{
				if (p.isGoody())
					iGoodyCount++;
				if (p.getTeam() != getTeam())
					iOtherPlotCount++;
			}
		}

		iValue -= 300 * iExplorerCount;
		iValue += 200 * iGoodyCount;
		iValue += 10 * iOtherPlotCount;
		if (iValue > iBestExploreValue)
		{
			UnitTypes eUnit = AI_bestAdvancedStartUnitAI(kCityPlot, eUnitAI);
			if (eUnit != NO_UNIT)
			{
				eBestUnitType = eUnit;
				iBestExploreValue = iValue;
				pBestExplorePlot = &kCityPlot;
			}
		}
	}

	if (pBestExplorePlot != NULL)
	{
		doAdvancedStartAction(ADVANCEDSTARTACTION_UNIT,
				pBestExplorePlot->getX(), pBestExplorePlot->getY(), eBestUnitType, true,
				UNITAI_EXPLORE); // advc.250c
		return true;
	}
	return false;
}


void CvPlayerAI::AI_advancedStartRevealRadius(CvPlot* pPlot, int iRadius)
{
	/*	advc.plotr (comment): The iterator uses a spiral pattern, but that's based on
		stepDistance. If we want plotDistance, then the outer loop is still needed. */
	for (int iRange = 1; iRange <= iRadius; iRange++)
	{
		for (PlotCircleIter it(*pPlot, iRange); it.hasNext(); ++it)
		{
			if (getAdvancedStartVisibilityCost(true, &(*it)) > 0)
			{
				doAdvancedStartAction(ADVANCEDSTARTACTION_VISIBILITY,
						it->getX(), it->getY(), -1, true);
			}
		}
	}
}


bool CvPlayerAI::AI_advancedStartPlaceCity(CvPlot* pPlot)
{
	//If there is already a city, then improve it.
	CvCityAI* pCity = pPlot->AI_getPlotCity();
	if (pCity == NULL)
	{
		doAdvancedStartAction(ADVANCEDSTARTACTION_CITY, pPlot->getX(), pPlot->getY(), -1, true);

		pCity = pPlot->AI_getPlotCity();
		if (pCity == NULL || pCity->getOwner() != getID())
		{
			//this should never happen since the cost for a city should be 0 if
			//the city can't be placed.
			//(It can happen if another player has placed a city in the fog)
			FAssertMsg(false, "ADVANCEDSTARTACTION_CITY failed in unexpected way");
			return false;
		}
	}
	int iCultureCost = getAdvancedStartCultureCost(true, pCity); // advc.250c
	if (pCity->getCultureLevel() <= 1 &&   // <advc.250c>
			(pCity->getCommerceRate(COMMERCE_CULTURE) <= 0 ||
			(iCultureCost >= 0 && getAdvancedStartPoints() > iCultureCost * 100)))
			// </advc.250c>
		doAdvancedStartAction(ADVANCEDSTARTACTION_CULTURE, pPlot->getX(), pPlot->getY(), -1, true);

	//to account for culture expansion.
	pCity->AI_updateBestBuild();

	int iPlotsImproved = 0;
	for (CityPlotIter it(*pPlot, false); it.hasNext(); ++it)
	{
		if (it->getWorkingCity() == pCity && it->isImproved())
			iPlotsImproved++;
	}

	int iTargetPopulation = pCity->happyLevel() + (getCurrentEra() / 2);
	while (iPlotsImproved < iTargetPopulation)
	{
		CvPlot* pBestPlot = NULL;
		ImprovementTypes eBestImprovement = NO_IMPROVEMENT;
		int iBestValue = 0;
		for (CityPlotIter it(*pCity, false); it.hasNext(); ++it)
		{
			CityPlotTypes const ePlot = it.currID();
			int iValue = pCity->AI_getBestBuildValue(ePlot);
			if (iValue <= iBestValue)
				continue; // advc
			BuildTypes eBuild = pCity->AI_getBestBuild(ePlot);
			if (eBuild == NO_BUILD)
				continue; // advc
			ImprovementTypes eImprovement = (ImprovementTypes)
					GC.getInfo(eBuild).getImprovement();
			if (eImprovement == NO_IMPROVEMENT)
				continue; // advc
			CvPlot* pLoopPlot = &(*it);
			if (pLoopPlot != NULL && pLoopPlot->isImproved())
			{
				eBestImprovement = eImprovement;
				pBestPlot = pLoopPlot;
				iBestValue = iValue;
			}
		}
		if (iBestValue > 0)
		{
			FAssert(pBestPlot != NULL);
			doAdvancedStartAction(ADVANCEDSTARTACTION_IMPROVEMENT, pBestPlot->getX(), pBestPlot->getY(), eBestImprovement, true);
			iPlotsImproved++;
			if (pCity->getPopulation() < iPlotsImproved)
			{
				doAdvancedStartAction(ADVANCEDSTARTACTION_POP, pBestPlot->getX(), pBestPlot->getY(), -1, true);
			}
		}
		else break;
	}

	while (iPlotsImproved > pCity->getPopulation())
	{
		int iPopCost = getAdvancedStartPopCost(true, pCity);
		if (iPopCost <= 0 || iPopCost > getAdvancedStartPoints())
			break;
		if (pCity->healthRate() < 0)
			break;
		doAdvancedStartAction(ADVANCEDSTARTACTION_POP, pPlot->getX(), pPlot->getY(), -1, true);
	}
	pCity->AI_updateAssignWork();

	return true;
}

//Returns false if we have no more points.
bool CvPlayerAI::AI_advancedStartDoRoute(CvPlot* pFromPlot, CvPlot* pToPlot)
{
	FAssert(pFromPlot != NULL);
	FAssert(pToPlot != NULL);

	FAStarNode* pNode;
	gDLL->getFAStarIFace()->ForceReset(&GC.getStepFinder());
	if (gDLL->getFAStarIFace()->GeneratePath(&GC.getStepFinder(), pFromPlot->getX(), pFromPlot->getY(), pToPlot->getX(), pToPlot->getY(), false, 0, true))
	{
		pNode = gDLL->getFAStarIFace()->GetLastNode(&GC.getStepFinder());
		if (pNode != NULL)
		{
			if (pNode->m_iData1 > (1 + ::stepDistance(pFromPlot->getX(), pFromPlot->getY(),
				pToPlot->getX(), pToPlot->getY())))
			{
				//Don't build convoluted paths.
				return true;
			}
		}

		while (pNode != NULL)
		{
			CvPlot* pPlot = GC.getMap().plotSoren(pNode->m_iX, pNode->m_iY);
			RouteTypes eRoute = AI_bestAdvancedStartRoute(pPlot);
			if (eRoute != NO_ROUTE)
			{
				if (getAdvancedStartRouteCost(eRoute, true, pPlot) > getAdvancedStartPoints())
				{
					return false;
				}
				doAdvancedStartAction(ADVANCEDSTARTACTION_ROUTE, pNode->m_iX, pNode->m_iY, eRoute, true);
			}
			pNode = pNode->m_pParent;
		}
	}
	return true;
}


void CvPlayerAI::AI_advancedStartRouteTerritory()
{
//	//This uses a heuristic to create a road network
//	//which is at least efficient if not all inclusive
//	//Basically a human will place roads where they connect
//	//the maximum number of trade groups and this
//	//mimics that.
//
//
//	CvPlot* pLoopPlot;
//	CvPlot* pLoopPlot2;
//	int iI, iJ;
//	int iPass;
//
//
//	std::vector<int> aiPlotGroups;
//	for (iPass = 4; iPass > 1; --iPass)
//	{
//		for (iI = 0; iI < GC.getMap().numPlots(); iI++)
//		{
//			pLoopPlot = GC.getMap().plotByIndex(iI);
//			if ((pLoopPlot != NULL) && (pLoopPlot->getOwner() == getID()) && (pLoopPlot->getRouteType() == NO_ROUTE))
//			{
//				aiPlotGroups.clear();
//				if (pLoopPlot->getPlotGroup(getID()) != NULL)
//				{
//					aiPlotGroups.push_back(pLoopPlot->getPlotGroup(getID())->getID());
//				}
//				for (iJ = 0; iJ < NUM_DIRECTION_TYPES; iJ++)
//				{
//					pLoopPlot2 = plotDirection(pLoopPlot->getX(), pLoopPlot->getY(), (DirectionTypes)iJ);
//					if ((pLoopPlot2 != NULL) && (pLoopPlot2->getRouteType() != NO_ROUTE))
//					{
//						CvPlotGroup* pPlotGroup = pLoopPlot2->getPlotGroup(getID());
//						if (pPlotGroup != NULL)
//						{
//							if (std::find(aiPlotGroups.begin(),aiPlotGroups.end(), pPlotGroup->getID()) == aiPlotGroups.end())
//							{
//								aiPlotGroups.push_back(pPlotGroup->getID());
//							}
//						}
//					}
//				}
//				if ((int)aiPlotGroups.size() >= iPass)
//				{
//					RouteTypes eBestRoute = AI_bestAdvancedStartRoute(pLoopPlot);
//					if (eBestRoute != NO_ROUTE)
//					{
//						doAdvancedStartAction(ADVANCEDSTARTACTION_ROUTE, pLoopPlot->getX(), pLoopPlot->getY(), eBestRoute, true);
//					}
//				}
//			}
//		}
//	}
//
//	//Maybe try to build road network for mobility but bearing in mind
//	//that routes can't be built outside culture atm. I think workers
//	//can do that just fine.

	for (int iI = 0; iI < GC.getMap().numPlots(); iI++)
	{
		CvPlot* pLoopPlot = GC.getMap().plotByIndex(iI);
		if (pLoopPlot != NULL && pLoopPlot->getOwner() == getID() &&
			!pLoopPlot->isRoute() && pLoopPlot->isImproved())
		{
			BonusTypes eBonus = pLoopPlot->getBonusType(getTeam());
			if (eBonus != NO_BONUS &&
				//GC.getInfo(pLoopPlot->getImprovementType()).isImprovementBonusTrade(eBonus)
				doesImprovementConnectBonus(pLoopPlot->getImprovementType(), eBonus))
			{
				int iBonusValue = AI_bonusVal(eBonus, 1, true);
				if (iBonusValue > 9)
				{
					CvPlot* pBestPlot = NULL;
					int iBestValue = 0;
					for (SquareIter it(*pLoopPlot, 2); it.hasNext(); ++it)
					{
						CvPlot& kLoopPlot2 = *it;
						if (kLoopPlot2.getOwner() == getID() &&
							(kLoopPlot2.isConnectedToCapital() || kLoopPlot2.isCity()))
						{
							int iValue = 1000;
							if (kLoopPlot2.isCity())
							{
								iValue += 100;
								if (kLoopPlot2.getPlotCity()->isCapital())
									iValue += 100;
							}
							if (kLoopPlot2.isRoute())
								iValue += 100;
							int iDistance = GC.getMap().
									calculatePathDistance(pLoopPlot, &kLoopPlot2);
							if (iDistance > 0)
							{
								iValue /= (1 + iDistance);
								if (iValue > iBestValue)
								{
									iBestValue = iValue;
									pBestPlot = &kLoopPlot2;
								}
							}
						}
					}
					if (pBestPlot != NULL)
					{
						if (!AI_advancedStartDoRoute(pLoopPlot, pBestPlot))
							return;
					}
				}
			}
			if (!pLoopPlot->isRoute())
			{
				int iRouteYieldValue = 0;
				RouteTypes eRoute = (AI_bestAdvancedStartRoute(pLoopPlot, &iRouteYieldValue));
				if (eRoute != NO_ROUTE && iRouteYieldValue > 0)
				{
					doAdvancedStartAction(ADVANCEDSTARTACTION_ROUTE,
							pLoopPlot->getX(), pLoopPlot->getY(), eRoute, true);
				}
			}
		}
	}

	//Connect Cities
	FOR_EACH_CITY(pLoopCity, *this)
	{
		if (pLoopCity->isCapital() || pLoopCity->isConnectedToCapital())
			continue;

		CvPlot* pBestPlot = NULL;
		int iBestValue = 0;
		for (SquareIter it(pLoopCity->getX(), pLoopCity->getY(), 5); it.hasNext(); ++it)
		{
			CvPlot& p = *it;
			if (p.getOwner() != getID())
				continue; // advc
			if (p.isConnectedToCapital() || p.isCity())
			{
				int iValue = 1000;
				if (p.isCity())
				{
					iValue += 500;
					if (p.getPlotCity()->isCapital())
						iValue += 500;
				}
				if (p.isRoute())
					iValue += 100;
				int iDistance = GC.getMap().calculatePathDistance(pLoopCity->plot(), &p);
				if (iDistance > 0)
				{
					iValue /= 1 + iDistance;
					if (iValue > iBestValue)
					{
						iBestValue = iValue;
						pBestPlot = &p;
					}
				}
			}
		}
		if (pBestPlot != NULL && !AI_advancedStartDoRoute(pBestPlot, pLoopCity->plot()))
			return;
	}
}


void CvPlayerAI::AI_doAdvancedStart(bool bNoExit)
{
	FAssertMsg(!isBarbarian(), "Should not be called for barbarians!");

	if (getStartingPlot() == NULL)
	{
		FAssert(false);
		return;
	}

	int iStartingPoints = getAdvancedStartPoints();
	//int iRevealPoints = (iStartingPoints * 10) / 100;
	// <advc.250c> Replacing the above
	int iRevealPoints = ::round((1.5 *
			GC.getDefineINT("ADVANCED_START_VISIBILITY_COST") *
			GC.getDefineINT("ADVANCED_START_VISIBILITY_COST_INCREASE")) / 100.0);
	// </advc.250c>
	int iMilitaryPoints = (iStartingPoints * (isHuman() ? 17 : (10 + (GC.getInfo(getPersonalityType()).getBuildUnitProb() / 3)))) / 100;
	int iCityPoints = iStartingPoints - (iMilitaryPoints + iRevealPoints);

	if (getCapitalCity() != NULL)
	{
		AI_advancedStartPlaceCity(getCapitalCity()->plot());
	}
	else
	{
		for (int iPass = 0; iPass < 2 && NULL == getCapitalCity(); ++iPass)
		{
			CvPlot* pBestCapitalPlot = AI_advancedStartFindCapitalPlot();
			if (pBestCapitalPlot != NULL)
			{
				if (!AI_advancedStartPlaceCity(pBestCapitalPlot))
				{
					FAssertMsg(false, "AS AI: Unexpected failure placing capital");
				}
				break;
			}
			else
			{	// advc: Assert added, BtS comment moved into message.
				FAssertMsg(false, "If this point is reached, the advanced start system is broken.");
				//Find a new starting plot for this player
				setStartingPlot(findStartingPlot(false), true);
				//Redo Starting visibility
				CvPlot* pStartingPlot = getStartingPlot();
				if (NULL != pStartingPlot)
				{
					for (int iPlotLoop = 0; iPlotLoop < GC.getMap().numPlots(); ++iPlotLoop)
					{
						CvPlot* pPlot = GC.getMap().plotByIndex(iPlotLoop);
						if (plotDistance(pPlot->getX(), pPlot->getY(), pStartingPlot->getX(), pStartingPlot->getY()) <= GC.getDefineINT("ADVANCED_START_SIGHT_RANGE"))
						{
							pPlot->setRevealed(getTeam(), true, false, NO_TEAM, false);
						}
					}
				}
			}
		}

		if (getCapitalCity() == NULL)
		{
			if (!bNoExit)
			{
				doAdvancedStartAction(ADVANCEDSTARTACTION_EXIT, -1, -1, -1, true);
			}
			return;
		}
	}

	iCityPoints -= (iStartingPoints - getAdvancedStartPoints());

	int iLastPointsTotal = getAdvancedStartPoints();

	for (int iPass = 0; iPass < 6; iPass++)
	{
		for (int iI = 0; iI < GC.getMap().numPlots(); iI++)
		{
			CvPlot* pLoopPlot = GC.getMap().plotByIndex(iI);
			if (!pLoopPlot->isRevealed(getTeam()))
				continue; // advc

			if (pLoopPlot->getBonusType(getTeam()) != NO_BONUS)
				AI_advancedStartRevealRadius(pLoopPlot, CITY_PLOTS_RADIUS);
			else
			{
				for (int iJ = 0; iJ < NUM_CARDINALDIRECTION_TYPES; iJ++)
				{
					CvPlot* pLoopPlot2 = plotCardinalDirection(pLoopPlot->getX(), pLoopPlot->getY(), (CardinalDirectionTypes)iJ);
					if (pLoopPlot2 != NULL && getAdvancedStartVisibilityCost(true, pLoopPlot2) > 0)
					{
						//Mildly maphackery but any smart human can see the terrain type of a tile.
						pLoopPlot2->getTerrainType();
						int iFoodYield = GC.getInfo(pLoopPlot2->getTerrainType()).getYield(YIELD_FOOD);
						if (pLoopPlot2->isFeature())
						{
							iFoodYield += GC.getInfo(pLoopPlot2->getFeatureType()).getYieldChange(YIELD_FOOD);
						}
						if (((iFoodYield >= 2) && !pLoopPlot2->isFreshWater()) || pLoopPlot2->isHills() || pLoopPlot2->isRiver())
						{
							doAdvancedStartAction(ADVANCEDSTARTACTION_VISIBILITY, pLoopPlot2->getX(), pLoopPlot2->getY(), -1, true);
						}
					}
				}
			}
			if (iLastPointsTotal - getAdvancedStartPoints() > iRevealPoints)
				break;
		}
	}

	iLastPointsTotal = getAdvancedStartPoints();
	iCityPoints = std::min(iCityPoints, iLastPointsTotal);
	int iArea = -1; //getStartingPlot()->getArea();

	//Spend econ points on a tech?
	int iTechRand = 90 + GC.getGame().getSorenRandNum(20, "AI AS Buy Tech 1");
	int iTotalTechSpending = 0;

	if (getCurrentEra() == 0)
	{
		TechTypes eTech = AI_bestTech(1);
		if ((eTech != NO_TECH) && !GC.getInfo(eTech).isRepeat())
		{
			int iTechCost = getAdvancedStartTechCost(eTech, true);
			if (iTechCost > 0)
			{
				doAdvancedStartAction(ADVANCEDSTARTACTION_TECH, -1, -1, eTech, true);
				iTechRand -= 50;
				iTotalTechSpending += iTechCost;
			}
		}
	}

	bool bDonePlacingCities = false;
	for (int iPass = 0; iPass < 100; ++iPass)
	{
		int iRand = iTechRand + 10 * getNumCities();
		if ((iRand > 0) && (GC.getGame().getSorenRandNum(100, "AI AS Buy Tech 2") < iRand))
		{
			TechTypes eTech = AI_bestTech(1);
			if ((eTech != NO_TECH) && !GC.getInfo(eTech).isRepeat())
			{
				int iTechCost = getAdvancedStartTechCost(eTech, true);
				if ((iTechCost > 0) && ((iTechCost + iTotalTechSpending) < (iCityPoints / 4)))
				{
					doAdvancedStartAction(ADVANCEDSTARTACTION_TECH, -1, -1, eTech, true);
					iTechRand -= 50;
					iTotalTechSpending += iTechCost;
					FOR_EACH_CITY(pLoopCity, *this)
						AI_advancedStartPlaceCity(pLoopCity->plot());
				}
			}
		}
		int iBestFoundValue = 0;
		CvPlot* pBestFoundPlot = NULL;
		AI_updateFoundValues(false);
		for (int iI = 0; iI < GC.getMap().numPlots(); iI++)
		{
			CvPlot* pLoopPlot = GC.getMap().plotByIndex(iI);
			//if (pLoopPlot->area() == getStartingPlot()->area())
			{
				if (plotDistance(getStartingPlot(), pLoopPlot) < 9)
				{
					if (pLoopPlot->getFoundValue(getID()) > iBestFoundValue)
					{
						if (getAdvancedStartCityCost(true, pLoopPlot) > 0)
						{
							pBestFoundPlot = pLoopPlot;
							iBestFoundValue = pLoopPlot->getFoundValue(getID());
						}
					}
				}
			}
		}

		if (iBestFoundValue < ((getNumCities() == 0) ? 1 : (500 + 250 * getNumCities())))
		{
			bDonePlacingCities = true;
		}
		if (!bDonePlacingCities)
		{
			int iCost = getAdvancedStartCityCost(true, pBestFoundPlot);
			if (iCost > getAdvancedStartPoints())
			{
				bDonePlacingCities = true;
			}// at 500pts, we have 200, we spend 100.
			else if (((iLastPointsTotal - getAdvancedStartPoints()) + iCost) > iCityPoints)
			{
				bDonePlacingCities = true;
			}
		}

		if (!bDonePlacingCities)
		{
			if (!AI_advancedStartPlaceCity(pBestFoundPlot))
			{
				FAssertMsg(false, "AS AI: Failed to place city (non-capital)");
				bDonePlacingCities = true;
			}
		}

		if (bDonePlacingCities)
		{
			break;
		}
	}


	bool bDoneWithTechs = false;
	while (!bDoneWithTechs)
	{
		bDoneWithTechs = true;
		TechTypes eTech = AI_bestTech(1);
		if (eTech != NO_TECH && !GC.getInfo(eTech).isRepeat())
		{
			int iTechCost = getAdvancedStartTechCost(eTech, true);
			if ((iTechCost > 0) && ((iTechCost + iLastPointsTotal - getAdvancedStartPoints()) <= iCityPoints))
			{
				doAdvancedStartAction(ADVANCEDSTARTACTION_TECH, -1, -1, eTech, true);
				bDoneWithTechs = false;
			}
		}
	}

	{
		//Land
		AI_advancedStartPlaceExploreUnits(true);
		if (getCurrentEra() > 2)
		{
			//Sea
			AI_advancedStartPlaceExploreUnits(false);
			if (GC.getGame().circumnavigationAvailable())
			{
				if (GC.getGame().getSorenRandNum(GC.getGame().countCivPlayersAlive(), "AI AS buy 2nd sea explorer") < 2)
				{
					AI_advancedStartPlaceExploreUnits(false);
				}
			}
		}
	}

	AI_advancedStartRouteTerritory();

	bool bDoneBuildings = (iLastPointsTotal - getAdvancedStartPoints()) > iCityPoints;
	for (int iPass = 0; iPass < 10 && !bDoneBuildings; ++iPass)
	{
		FOR_EACH_CITYAI(pLoopCity, *this)
		{
			BuildingTypes eBuilding = pLoopCity->AI_bestAdvancedStartBuilding(iPass);
			if (eBuilding != NO_BUILDING)
			{
				bDoneBuildings = (iLastPointsTotal - (getAdvancedStartPoints() - getAdvancedStartBuildingCost(eBuilding, true, pLoopCity))) > iCityPoints;
				if (!bDoneBuildings)
				{
					doAdvancedStartAction(ADVANCEDSTARTACTION_BUILDING, pLoopCity->getX(), pLoopCity->getY(), eBuilding, true);
				}
				else
				{
					//continue there might be cheaper buildings in other cities we can afford
				}
			}
		}
	}

	//Units
	std::vector<UnitAITypes> aeUnitAITypes;
	aeUnitAITypes.push_back(UNITAI_CITY_DEFENSE);
	aeUnitAITypes.push_back(UNITAI_WORKER);
	aeUnitAITypes.push_back(UNITAI_RESERVE);
	aeUnitAITypes.push_back(UNITAI_COUNTER);

	bool bDone = false;
	for (int iPass = 0; iPass < 10; ++iPass)
	{
		FOR_EACH_CITY(pLoopCity, *this)
		{
			if (iPass == 0 || pLoopCity->isArea(getStartingPlot()->getArea()))
			{
				CvPlot const& kCityPlot = pLoopCity->getPlot();
				//Token defender
				UnitTypes eBestUnit = AI_bestAdvancedStartUnitAI(kCityPlot, aeUnitAITypes[iPass % aeUnitAITypes.size()]);
				if (eBestUnit != NO_UNIT)
				{
					if (getAdvancedStartUnitCost(eBestUnit, true, &kCityPlot) > getAdvancedStartPoints())
					{
						bDone = true;
						break;
					}
					doAdvancedStartAction(ADVANCEDSTARTACTION_UNIT,
							kCityPlot.getX(), kCityPlot.getY(), eBestUnit, true);
				}
			}
		}
	}

	if (isHuman())
	{
		// remove unhappy population
		FOR_EACH_CITY(pLoopCity, *this)
		{
			while (pLoopCity->angryPopulation() > 0 && getAdvancedStartPopCost(false, pLoopCity) > 0)
			{
				doAdvancedStartAction(ADVANCEDSTARTACTION_POP, pLoopCity->getX(), pLoopCity->getY(), -1, false);
			}
		}
	}

	if (!bNoExit)
	{
		doAdvancedStartAction(ADVANCEDSTARTACTION_EXIT, -1, -1, -1, true);
	}

}


void CvPlayerAI::AI_recalculateFoundValues(int iX, int iY, int iInnerRadius, int iOuterRadius) const  // advc: some style changes
{
	for (SquareIter it(iX, iY, iOuterRadius, false); it.hasNext(); ++it)
	{
		CvPlot& p = *it;
		if (AI_isPlotCitySite(p))
			continue;
		if (it.currStepDist() <= iInnerRadius)
			p.setFoundValue(getID(), 0);
		else if (p.isRevealed(getTeam()))
		{
			short iValue = GC.getPythonCaller()->AI_foundValue(getID(), p);
			if (iValue == -1)
				iValue = AI_foundValue(p.getX(), p.getY());
			p.setFoundValue(getID(), iValue);
			if (iValue > p.getArea().getBestFoundValue(getID()))
				p.getArea().setBestFoundValue(getID(), iValue);
		}
	}
}


int CvPlayerAI::AI_getMinFoundValue() const
{
	PROFILE_FUNC();

	//int iValue = 600;
	static int const iBBAI_MINIMUM_FOUND_VALUE = GC.getDefineINT("BBAI_MINIMUM_FOUND_VALUE"); // advc.opt
	int iValue = iBBAI_MINIMUM_FOUND_VALUE; // K-Mod
	//int iNetCommerce = 1 + getCommerceRate(COMMERCE_GOLD) + getCommerceRate(COMMERCE_RESEARCH) + std::max(0, getGoldPerTurn());
	int iNetCommerce = AI_getAvailableIncome(); // K-Mod
	int iNetExpenses = calculateInflatedCosts() + std::max(0, -getGoldPerTurn());
	/*  <advc.031> Prevent the AI from stopping to expand once corporation
		maintenance becomes a major factor */
	iNetExpenses = std::max(0, iNetExpenses -
			countHeadquarters() * getNumCities() * 4); // </advc.031>
	iValue *= iNetCommerce;
	iValue /= std::max(std::max(1, iNetCommerce / 4), iNetCommerce - iNetExpenses);

	// advc.105: Don't do this at all
	/*if (GET_TEAM(getTeam()).getAnyWarPlanCount(1) > 0)
		iValue *= 2;*/

	// K-Mod. # of cities maintenance cost increase...
	int iNumCitiesPercent = 100;
	//iNumCitiesPercent *= (getAveragePopulation() + 17);
	//iNumCitiesPercent /= 18;

	iNumCitiesPercent *= GC.getInfo(GC.getMap().getWorldSize()).getNumCitiesMaintenancePercent();
	iNumCitiesPercent /= 100;

	iNumCitiesPercent *= GC.getInfo(getHandicapType()).getNumCitiesMaintenancePercent();
	iNumCitiesPercent /= 100;

	//iNumCitiesPercent *= std::max(0, getNumCitiesMaintenanceModifier() + 100);
	//iNumCitiesPercent /= 100;

	// The marginal cost increase is roughly equal to double the cost of a current city...
	// But we're really going to have to fudge it anyway, because the city value is in arbitrary units
	// lets just say each gold per turn is worth roughly 60 'value points'.
	// In the future, this could be AI flavour based.
	iValue += (iNumCitiesPercent * getNumCities() * //60) / 100;
			/*  advc.130v: We may not be paying much for cities, but our master
				will have to pay as well. */
			// advc.031: Also raise the MinFoundValue a bit overall (60->70)
			(70 + (GET_TEAM(getTeam()).isCapitulated() ? 33 : 0))) / 100;
	// K-Mod end
	// advc.031:
	iValue = ::round(iValue / std::min(1.0, 1.65 * AI_amortizationMultiplier(15)));
	return iValue;
}

void CvPlayerAI::AI_updateCitySites(int iMinFoundValueThreshold, int iMaxSites)
{
	// kmodx: redundant code removed
	// <advc>
	if (iMinFoundValueThreshold < 0)
		iMinFoundValueThreshold = AI_getMinFoundValue(); // </advc>

	// K-Mod. Always recommend the starting location on the first turn.
	// (because we don't have enough information to overrule what the game has cooked for us.)
	if (isHuman() &&  /* advc.108: OK for recommendations, but allow AI to evaluate
						 plots and settle elsewhere. */
		getNumCities() == 0 && iMaxSites > 0 && GC.getGame().getElapsedGameTurns() == 0 &&
		m_iStartingX != INVALID_PLOT_COORD && m_iStartingY != INVALID_PLOT_COORD)  {
		m_aiAICitySites.push_back(GC.getMap().plotNum(m_iStartingX, m_iStartingY));
		//AI_recalculateFoundValues(m_iStartingX, m_iStartingY, CITY_PLOTS_RADIUS, 2 * CITY_PLOTS_RADIUS);
		return; // don't bother trying to pick a secondary spot
	}
	// K-Mod end

	int iPass = 0;
	while (iPass < iMaxSites)
	{
		//Add a city to the list.
		int iBestFoundValue = 0;
		CvPlot const* pBestFoundPlot = NULL;

		for (int iI = 0; iI < GC.getMap().numPlots(); iI++)  // advc: some style changes
		{
			CvPlot const& kPlot = GC.getMap().getPlotByIndex(iI);
			if (!kPlot.isRevealed(getTeam()))
				continue;

			int iValue = kPlot.getFoundValue(getID(), /* advc.052: */ true);
			if (iValue > iMinFoundValueThreshold && !AI_isPlotCitySite(kPlot))
			{
				iValue *= std::min(NUM_CITY_PLOTS * 2, kPlot.getArea().getNumUnownedTiles()
						+ 1); /* advc.031: Just b/c all tiles in the area are
								 owned doesn't mean we can't ever settle there.
								 Probably an oversight; tagging advc.001. */
				if (iValue > iBestFoundValue)
				{
					iBestFoundValue = iValue;
					pBestFoundPlot = &kPlot;
				}
			}
		}
		if (pBestFoundPlot != NULL)
		{
			m_aiAICitySites.push_back(GC.getMap().plotNum(pBestFoundPlot->getX(), pBestFoundPlot->getY()));
			AI_recalculateFoundValues(pBestFoundPlot->getX(), pBestFoundPlot->getY(), CITY_PLOTS_RADIUS, 2 * CITY_PLOTS_RADIUS);
		}
		else break;
		iPass++;
	}
}


void CvPlayerAI::AI_invalidateCitySites(int iMinFoundValueThreshold)
{
	// m_aiAICitySites.clear(); // BtS
	// K-Mod. note: this clear-by-value stuff isn't actually used yet... but at least it works now.
	std::vector<int> keptSites;
	if (iMinFoundValueThreshold > 0) // less than zero means clear all.
	{
		for (size_t iI = 0; iI < m_aiAICitySites.size(); iI++)
		{
			if (GC.getMap().plotByIndex(m_aiAICitySites[iI])->getFoundValue(getID()) >= iMinFoundValueThreshold)
				keptSites.push_back(m_aiAICitySites[iI]);
		}
	}
	m_aiAICitySites.swap(keptSites);
	// K-Mod end
}

int CvPlayerAI::AI_getNumCitySites() const
{
	return m_aiAICitySites.size();
}

bool CvPlayerAI::AI_isPlotCitySite(/* advc: */ CvPlot const& kPlot) const
{
	std::vector<int>::const_iterator it;
	int iPlotIndex = GC.getMap().plotNum(kPlot.getX(), kPlot.getY());
	for (it = m_aiAICitySites.begin(); it != m_aiAICitySites.end(); it++)
	{
		if ((*it) == iPlotIndex)
			return true;
	}
	return false;
}

int CvPlayerAI::AI_getNumAreaCitySites(CvArea const& kArea, int& iBestValue) const
{
	std::vector<int>::const_iterator it;
	int iCount = 0;
	iBestValue = 0;
	for (it = m_aiAICitySites.begin(); it != m_aiAICitySites.end(); it++)
	{
		CvPlot* pCitySitePlot = GC.getMap().plotByIndex((*it));
		if (pCitySitePlot->isArea(kArea))
		{
			iCount++;
			iBestValue = std::max(iBestValue, pCitySitePlot->getFoundValue(getID()));
		}
	}
	return iCount;
}

int CvPlayerAI::AI_getNumAdjacentAreaCitySites(int& iBestValue, CvArea const& kWaterArea,
	CvArea const* pExcludeArea) const
{
	std::vector<int>::const_iterator it;
	int iCount = 0;
	iBestValue = 0;
	for (it = m_aiAICitySites.begin(); it != m_aiAICitySites.end(); it++)
	{
		CvPlot* pCitySitePlot = GC.getMap().plotByIndex((*it));
		if (pCitySitePlot->area() != pExcludeArea)
		{
			if (pCitySitePlot->isAdjacentToArea(kWaterArea))
			{
				iCount++;
				iBestValue = std::max(iBestValue, pCitySitePlot->getFoundValue(getID()));
			}
		}
	}
	return iCount;
}

// K-Mod. Return the number of city sites that are in a primary area, with a site value above the minimum
int CvPlayerAI::AI_getNumPrimaryAreaCitySites(int iMinimumValue) const
{
	int iCount = 0;
	std::vector<int>::const_iterator it;
	for (it = m_aiAICitySites.begin(); it != m_aiAICitySites.end(); it++)
	{
		CvPlot* pCitySitePlot = GC.getMap().plotByIndex((*it));
		if (AI_isPrimaryArea(pCitySitePlot->getArea()) &&
			pCitySitePlot->getFoundValue(getID()) >= iMinimumValue)
		{
			iCount++;
		}
	}
	return iCount;
}
// K-Mod end

CvPlot* CvPlayerAI::AI_getCitySite(int iIndex) const
{
	FAssert(iIndex < (int)m_aiAICitySites.size());
	return GC.getMap().plotByIndex(m_aiAICitySites[iIndex]);
}

// <advc.121> (Also needed for advc.117)
bool CvPlayerAI::AI_isAdjacentCitySite(CvPlot const& p, bool bCheckCenter) const
{
	if(bCheckCenter && AI_isPlotCitySite(p))
		return true;

	for(int i = 0; i < NUM_DIRECTION_TYPES; i++)
	{
		CvPlot const* pAdj = plotDirection(p.getX(), p.getY(), (DirectionTypes)i);
		if(pAdj != NULL && AI_isPlotCitySite(*pAdj))
			return true;
	}
	return false;
} // </advc.121>

/*  <advc.ctr> Says whether kCity is in a spot where probably no city belongs.
	This player is the AI civ considering to obtain the city.
	Some overlap with CvPlayerAI::AI_foundValue, but it's difficult to make
	that function work for plots with an actual city. */
bool CvPlayerAI::AI_isAwfulSite(CvCity const& kCity) const
{
	// If the city has grown, the site has somewhat proven its usefulness.
	if (kCity.getPopulation() >= 4 + 2 * GC.getGame().getCurrentEra())
		return false;
	double decentTiles = 0;
	bool const bCountCoast = kCity.isCoastal();
	for (CityPlotIter it(kCity, false); it.hasNext(); ++it)
	{
		CvPlot const& p = *it;
		if(p.getPlayerCityRadiusCount(kCity.getOwner()) > 1 || p.isImpassable())
			continue;
		// Third-party culture
		PlayerTypes eCulturalOwner = p.calculateCulturalOwner();
		if(eCulturalOwner != NO_PLAYER && eCulturalOwner != getID() &&
			eCulturalOwner != kCity.getOwner())
		{
			continue;
		}
		// Flood plains, oases
		if(p.calculateNatureYield(YIELD_FOOD, getTeam()) >= 3)
		{
			decentTiles++;
			continue;
		}
		// Not tundra, snow, desert, regardless of hill, forest or river
		if(!p.isWater() && p.calculateNatureYield(YIELD_FOOD, getTeam(), true) +
			p.calculateNatureYield(YIELD_PRODUCTION, getTeam(), true) +
			// Rounded down
			p.calculateNatureYield(YIELD_COMMERCE, getTeam(), true) / 2 >= 2)
		{
			decentTiles++;
			continue;
		}
		BonusTypes eBonus = p.getNonObsoleteBonusType(getTeam());
		if(eBonus != NO_BONUS)
		{
			decentTiles++;
			continue;
		}
		// Coast is (almost) half-decent
		if(bCountCoast && p.isWater() && p.isAdjacentToLand())
			decentTiles += 0.4;
	}
	return decentTiles < 6.99;
} // </advc.ctr>

// K-Mod
bool CvPlayerAI::AI_deduceCitySite(const CvCity* pCity) const
{
	return GET_TEAM(getTeam()).AI_deduceCitySite(pCity);
}

// K-Mod. This function is essentially a merged version of two original bts functions: CvPlayer::countPotentialForeignTradeCities and CvPlayer::countPotentialForeignTradeCitiesConnected.
// I've rewritten it to be a single function, and changed it so that when checking that he cities are connected, it does not count cities if the foreign civ has "no foreign trade".
// (it ignores that effect for our civ, but checks it for the foreign civ - the reasoning is basically that the other player's civics are out of our control.)
int CvPlayerAI::AI_countPotentialForeignTradeCities(bool bCheckConnected, bool bCheckForeignTradePolicy,
	CvArea const* pIgnoreArea) const
{
	CvCity* pCapitalCity = getCapitalCity();
	if (pCapitalCity == 0)
		return 0;

	const CvTeam& kTeam = GET_TEAM(getTeam());

	int iCount = 0;
	for (PlayerTypes i = (PlayerTypes)0; i < MAX_CIV_PLAYERS; i = (PlayerTypes)(i+1))
	{
		CvPlayer const& kLoopPlayer = GET_PLAYER(i);

		if (!kLoopPlayer.isAlive() || kLoopPlayer.getTeam() == getTeam() || !kTeam.isFreeTrade(kLoopPlayer.getTeam()))
			continue;

		if (bCheckForeignTradePolicy && kLoopPlayer.isNoForeignTrade() && !kTeam.isVassal(kLoopPlayer.getTeam()) && !GET_TEAM(kLoopPlayer.getTeam()).isVassal(getTeam()))
			continue;

		// this is a legitimate foreign trade partner. Count the number of (connected) cities.
		if (bCheckConnected)
		{
			FOR_EACH_CITY(pLoopCity, kLoopPlayer)
			{
				if ((pIgnoreArea == NULL || !pLoopCity->isArea(*pIgnoreArea)) &&
					pLoopCity->plotGroup(getID()) == pCapitalCity->plotGroup(getID()))
				{
					iCount++;
				}
			}
		}
		else
		{
			iCount += kLoopPlayer.getNumCities();
			if (pIgnoreArea != NULL)
				iCount -= pIgnoreArea->getCitiesPerPlayer(i);
		}
	}

	FAssert(iCount >= 0);

	return iCount;
}
// K-Mod end

int CvPlayerAI::AI_bestAreaUnitAIValue(UnitAITypes eUnitAI, CvArea const* pArea,
	UnitTypes* peBestUnitType) const
{
	PROFILE_FUNC(); // advc.opt

	CvCity const* pCity = NULL;
	if (pArea != NULL)
	{
		if (getCapitalCity() != NULL)
		{
			if (pArea->isWater())
			{
				if (getCapitalCity()->getPlot().isAdjacentToArea(*pArea))
					pCity = getCapitalCity();
			}
			else
			{
				if (getCapitalCity()->isArea(*pArea))
					pCity = getCapitalCity();
			}
		}

		if (pCity == NULL)
		{
			FOR_EACH_CITY(pLoopCity, *this)
			{
				if (pArea->isWater())
				{
					if (pLoopCity->getPlot().isAdjacentToArea(*pArea))
					{
						pCity = pLoopCity;
						break;
					}
				}
				else
				{
					if (pLoopCity->isArea(*pArea))
					{
						pCity = pLoopCity;
						break;
					}
				}
			}
		}
	}

	return AI_bestCityUnitAIValue(eUnitAI, pCity, peBestUnitType);
}

int CvPlayerAI::AI_bestCityUnitAIValue(UnitAITypes eUnitAI, CvCity const* pCity, UnitTypes* peBestUnitType) const // advc: const CvCity*
{
	FAssertMsg(eUnitAI != NO_UNITAI, "UnitAI is not assigned a valid value");

	int iBestValue = 0;
	CvCivilization const& kCiv = getCivilization(); // advc.003w
	for (int i = 0; i < kCiv.getNumUnits(); i++)
	{
		UnitTypes eLoopUnit = kCiv.unitAt(i);
		//if (!isHuman() || (GC.getInfo(eLoopUnit).getDefaultUnitAIType() == eUnitAI)) { // disabled by K-Mod
		if (NULL == pCity ? (canTrain(eLoopUnit) &&
			AI_haveResourcesToTrain(eLoopUnit)) : // k146
			pCity->canTrain(eLoopUnit))
		{
			int iValue = AI_unitValue(eLoopUnit,
					eUnitAI, pCity == NULL ? NULL : pCity->area());
			if (iValue > iBestValue)
			{
				iBestValue = iValue;
				if (peBestUnitType != NULL)
					*peBestUnitType = eLoopUnit;
			}
		}
	}
	return iBestValue;
}

int CvPlayerAI::AI_calculateTotalBombard(DomainTypes eDomain) const
{
	int iTotalBombard = 0;
	CvCivilization const& kCiv = getCivilization(); // advc.003w
	for (int i = 0; i < kCiv.getNumUnits(); i++)
	{
		UnitTypes eLoopUnit = kCiv.unitAt(i);
		if (GC.getInfo(eLoopUnit).getDomainType() != eDomain)
			continue;

		UnitClassTypes eLoopClass = kCiv.unitClassAt(i);

		int iBombardRate = GC.getInfo(eLoopUnit).getBombardRate();
		if (iBombardRate > 0)
		{
			if (GC.getInfo(eLoopUnit).isIgnoreBuildingDefense())
			{
				iBombardRate *= 3;
				iBombardRate /= 2;
			}
			iTotalBombard += iBombardRate * getUnitClassCount(eLoopClass);
		}

		int iBombRate = GC.getInfo(eLoopUnit).getBombRate();
		if (iBombRate > 0)
		{
			iTotalBombard += iBombRate * getUnitClassCount(eLoopClass);
		}
	}
	return iTotalBombard;
}

void CvPlayerAI::AI_updateBonusValue(BonusTypes eBonus)
{
	FAssert(m_aiBonusValue != NULL);
	/*  <advc.036> Don't just reset; recompute them all, and never update the
		cache in AI_baseBonusVal. This should make sure we're not getting OOS. */
	if(GC.getGame().isNetworkMultiPlayer())
	{
		m_aiBonusValue[eBonus] = AI_baseBonusVal(eBonus, false);
		m_aiBonusValueTrade[eBonus] = AI_baseBonusVal(eBonus, true);
	}
	else // Reset and update on demand is faster
	{
	/*  Tbd.: Could lead to undesirable side-effects in singleplayer, and
		the multiplayer code is perhaps not just slower, but also less
		accurate. Implement a bConstCache param for AI_baseBonusVal?
		Would have to add the param to AI_bonusVal and AI_corporationValue too. */
	// </advc.036>
		m_aiBonusValue[eBonus] = -1;
		m_aiBonusValueTrade[eBonus] = -1;
	}
}

void CvPlayerAI::AI_updateBonusValue()
{
	PROFILE_FUNC(); // advc.036 (comment): Performance could be an issue in multiplayer

	FAssert(m_aiBonusValue != NULL);

	for (int iI = 0; iI < GC.getNumBonusInfos(); iI++)
	{
		AI_updateBonusValue((BonusTypes)iI);
	}
}

int CvPlayerAI::AI_getUnitClassWeight(UnitClassTypes eUnitClass) const
{
	return m_aiUnitClassWeights[eUnitClass] / 100;
}

int CvPlayerAI::AI_getUnitCombatWeight(UnitCombatTypes eUnitCombat) const
{
	return m_aiUnitCombatWeights[eUnitCombat] / 100;
}

void CvPlayerAI::AI_doEnemyUnitData()
{
	std::vector<int> aiUnitCounts(GC.getNumUnitInfos(), 0);
	std::vector<int> aiDomainSums(NUM_DOMAIN_TYPES, 0);

	int iOldTotal = 0;
	int iNewTotal = 0;

	// Count enemy land and sea units visible to us
	for (int i = 0; i < GC.getMap().numPlots(); i++)
	{
		CvPlot const& kPlot = GC.getMap().getPlotByIndex(i);
		int iAdjacentAttackers = -1;
		if (!kPlot.isVisible(getTeam()))
			continue; // advc

		for (CLLNode<IDInfo> const* pUnitNode = kPlot.headUnitNode();
			pUnitNode != NULL; pUnitNode = kPlot.nextUnitNode(pUnitNode))
		{
			CvUnit* pLoopUnit = ::getUnit(pUnitNode->m_data);
			if (!pLoopUnit->canFight())
				continue; // advc

			int iUnitValue = 1;
			//if (atWar(getTeam(), pLoopUnit->getTeam()))
			// BETTER_BTS_AI_MOD, War Strategy AI, 03/18/10, jdog5000:
			if (GET_TEAM(getTeam()).AI_getWarPlan(pLoopUnit->getTeam()) != NO_WARPLAN)
			{
				iUnitValue += 10;
				if (kPlot.getOwner() == getID())
				{
					iUnitValue += 15;
					// <cdtw> try a little harder to counter city attack units
					if(pLoopUnit->combatLimit() == GC.getMAX_HIT_POINTS())
						iUnitValue += pLoopUnit->cityAttackModifier() / 10;
					// </cdtw>
				}
				else if (atWar(getTeam(), kPlot.getTeam()))
				{
					if (iAdjacentAttackers == -1)
					{
						iAdjacentAttackers = GET_PLAYER(kPlot.getOwner()).
							AI_adjacentPotentialAttackers(kPlot);
					}
					if (iAdjacentAttackers > 0)
						iUnitValue += 15;
				}
			}
			else if (pLoopUnit->getOwner() != getID())
			{
				int iTmp = ((pLoopUnit->canAttack() &&
					// advc.315:
					!pLoopUnit->getUnitInfo().isMostlyDefensive()) ?
					4 : 1);
				iUnitValue += iTmp;
				if (kPlot.getCulture(getID()) > 0)
				{
					//iUnitValue += pLoopUnit->canAttack() ? 4 : 1;
					iUnitValue += iTmp; // advc.315
				}
			}
			// If we hadn't seen any of this class before
			if (m_aiUnitClassWeights[pLoopUnit->getUnitClassType()] == 0)
				iUnitValue *= 4;

			iUnitValue *= pLoopUnit->baseCombatStr();
			aiUnitCounts[pLoopUnit->getUnitType()] += iUnitValue;
			aiDomainSums[pLoopUnit->getDomainType()] += iUnitValue;
			iNewTotal += iUnitValue;
		}
	}

	if (iNewTotal == 0)
	{
		//This should rarely happen.
		return;
	}
	//Decay
	FOR_EACH_ENUM(UnitClass)
	{
		m_aiUnitClassWeights[eLoopUnitClass] -= 100;
		m_aiUnitClassWeights[eLoopUnitClass] *= 3;
		m_aiUnitClassWeights[eLoopUnitClass] /= 4;
		m_aiUnitClassWeights[eLoopUnitClass] =
				std::max(0, m_aiUnitClassWeights[eLoopUnitClass]);
	}
	FOR_EACH_ENUM(Unit)
	{
		if (aiUnitCounts[eLoopUnit] <= 0)
			continue; // advc

		TechTypes eTech = (TechTypes)GC.getInfo(eLoopUnit).getPrereqAndTech();
		int iEraDiff = (eTech == NO_TECH) ? 4 :
				std::min(4, getCurrentEra() - GC.getInfo(eTech).getEra());
		if (iEraDiff > 1)
		{
			iEraDiff -= 1;
			aiUnitCounts[eLoopUnit] *= 3 - iEraDiff;
			aiUnitCounts[eLoopUnit] /= 3;
		}
		FAssert(aiDomainSums[GC.getInfo(eLoopUnit).getDomainType()] > 0);
		m_aiUnitClassWeights[GC.getInfo(eLoopUnit).getUnitClassType()] +=
				(5000 * aiUnitCounts[eLoopUnit]) / std::max(1,
				aiDomainSums[GC.getInfo(eLoopUnit).getDomainType()]);
	}
	FOR_EACH_ENUM(UnitCombat)
	{
		m_aiUnitCombatWeights[eLoopUnitCombat] = 0;
	}
	FOR_EACH_ENUM(UnitClass)
	{
		if (m_aiUnitClassWeights[eLoopUnitClass] > 0)
		{
			UnitTypes eUnit = GC.getInfo(eLoopUnitClass).getDefaultUnit();
			m_aiUnitCombatWeights[GC.getInfo(eUnit).getUnitCombatType()] +=
					m_aiUnitClassWeights[eLoopUnitClass];
		}
	}
	FOR_EACH_ENUM(UnitCombat)
	{
		if (m_aiUnitCombatWeights[eLoopUnitCombat] > 25)
			m_aiUnitCombatWeights[eLoopUnitCombat] += 2500;
		else if (m_aiUnitCombatWeights[eLoopUnitCombat] > 0)
			m_aiUnitCombatWeights[eLoopUnitCombat] += 1000;
	}
}

int CvPlayerAI::AI_calculateUnitAIViability(UnitAITypes eUnitAI, DomainTypes eDomain) const
{
	int iBestUnitAIStrength = 0;
	int iBestOtherStrength = 0;
	FOR_EACH_ENUM(UnitClass)
	{
		UnitTypes eLoopUnit = GC.getInfo(eLoopUnitClass).getDefaultUnit();
		const CvUnitInfo& kUnitInfo = GC.getInfo(eLoopUnit);
		if (kUnitInfo.getDomainType() == eDomain)
		{
			TechTypes eTech = (TechTypes)kUnitInfo.getPrereqAndTech();
			if (m_aiUnitClassWeights[eLoopUnitClass] > 0 ||
				GET_TEAM(getTeam()).isHasTech((eTech)))
			{
				if (kUnitInfo.getUnitAIType(eUnitAI))
					iBestUnitAIStrength = std::max(iBestUnitAIStrength, kUnitInfo.getCombat());
				iBestOtherStrength = std::max(iBestOtherStrength, kUnitInfo.getCombat());
			}
		}
	}
	return (100 * iBestUnitAIStrength) / std::max(1, iBestOtherStrength);
}

// K-Mod. Units with the lowest "disband value" will be disbanded first.
// (this code is based on the old code from CvPlayerAI::AI_disbandUnit)
int CvPlayerAI::AI_disbandValue(CvUnitAI const& kUnit, bool bMilitaryOnly) const // advc.003u: param was CvUnit const*
{
	if (kUnit.hasCargo() || kUnit.isGoldenAge() || kUnit.getUnitInfo().getProductionCost() < 0 || (bMilitaryOnly && !kUnit.canFight()))
		return MAX_INT;

	if (kUnit.isMilitaryHappiness() && kUnit.getPlot().isCity() &&
		kUnit.plot()->plotCount(PUF_isMissionAIType, MISSIONAI_GUARD_CITY, -1, getID()) < 2)
	{
		return MAX_INT;
	}
	int iValue = 1000 + GC.getGame().getSorenRandNum(200, "Disband Value");

	iValue *= 100 + (kUnit.getUnitInfo().getProductionCost() * 3);
	iValue /= 100;

	iValue *= 100 + std::max(kUnit.getExperience(),
			kUnit.getLevel() * kUnit.getLevel() * 2 / 3) * 15;
	iValue /= 100;

	/*if (getPlot().getTeam() == getTeam()) {
		iValue *= 2;
		if (canDefend() && getPlot().isCity())
			iValue *= 2;
	}*/

	switch (kUnit.AI_getGroup()->AI_getMissionAIType())
	{
	case MISSIONAI_GUARD_CITY:
	case MISSIONAI_PICKUP:
	case MISSIONAI_FOUND:
	case MISSIONAI_SPREAD:
		iValue *= 3; // high value
		break;

	case MISSIONAI_STRANDED:
		iValue /= 2;
	case MISSIONAI_RETREAT:
	case MISSIONAI_PATROL:
	case NO_MISSIONAI:
		break; // low value

	default:
		iValue *= 2; // medium
		break;
	}

	// Multiplying by higher number means unit has higher priority, less likely to be disbanded
	switch (kUnit.AI_getUnitAIType())
	{
	case UNITAI_UNKNOWN:
	case UNITAI_ANIMAL:
		break;

	case UNITAI_SETTLE:
		iValue *= 16;
		break;

	case UNITAI_WORKER:
		if (GC.getGame().getGameTurn() - kUnit.getGameTurnCreated() <= 10 ||
			!kUnit.getPlot().isCity() ||
			kUnit.getPlot().AI_getPlotCity()->AI_getWorkersNeeded() > 0)
		{
			iValue *= 10;
		}
		break;

	case UNITAI_ATTACK:
	case UNITAI_COUNTER:
		iValue *= 4;
		break;

	case UNITAI_ATTACK_CITY:
	case UNITAI_COLLATERAL:
	case UNITAI_PILLAGE:
	case UNITAI_RESERVE:
		iValue *= 3;
		break;

	case UNITAI_CITY_DEFENSE:
	case UNITAI_CITY_COUNTER:
	case UNITAI_CITY_SPECIAL:
	case UNITAI_PARADROP:
		iValue *= 6;
		break;

	case UNITAI_EXPLORE:
		if (GC.getGame().getGameTurn() - kUnit.getGameTurnCreated() < 20)
			iValue *= 2;
		if (kUnit.getPlot().getTeam() != getTeam())
			iValue *= 2;
		break;

	case UNITAI_MISSIONARY:
		if (GC.getGame().getGameTurn() - kUnit.getGameTurnCreated() < 10 ||
			kUnit.getPlot().getTeam() != getTeam())
		{
			iValue *= 8;
		}
		break;

	case UNITAI_PROPHET:
	case UNITAI_ARTIST:
	case UNITAI_SCIENTIST:
	case UNITAI_GENERAL:
	case UNITAI_MERCHANT:
	case UNITAI_ENGINEER:
	case UNITAI_GREAT_SPY:
		iValue *= 20;
		break;

	case UNITAI_SPY:
		iValue *= 10;
		break;

	case UNITAI_ICBM:
		iValue *= 5;
		break;

	case UNITAI_WORKER_SEA:
		iValue *= 15;
		break;

	case UNITAI_ATTACK_SEA:
	case UNITAI_RESERVE_SEA:
	case UNITAI_ESCORT_SEA:
		iValue *= 2;
		break;

	case UNITAI_EXPLORE_SEA:
		if (GC.getGame().getGameTurn() - kUnit.getGameTurnCreated() < 20)
			iValue *= 3;
		if (kUnit.getPlot().getTeam() != getTeam())
			iValue *= 3;
		break;

	case UNITAI_SETTLER_SEA:
		iValue *= 6;
		break;

	case UNITAI_MISSIONARY_SEA:
	case UNITAI_SPY_SEA:
		iValue *= 4;
		break;

	case UNITAI_ASSAULT_SEA:
	case UNITAI_CARRIER_SEA:
	case UNITAI_MISSILE_CARRIER_SEA:
		//if(GET_TEAM(getTeam()).getAnyWarPlanCount(true) > 0)
		if (AI_isFocusWar()) // advc.105
			iValue *= 5;
		else iValue *= 2;
		break;

	case UNITAI_PIRATE_SEA:
	case UNITAI_ATTACK_AIR:
		break;

	case UNITAI_DEFENSE_AIR:
	case UNITAI_CARRIER_AIR:
	case UNITAI_MISSILE_AIR:
		//if(GET_TEAM(getTeam()).getAnyWarPlanCount(true) > 0)
		if (AI_isFocusWar()) // advc.105
			iValue *= 5;
		else iValue *= 3;
		break;

	default:
		FAssert(false);
		break;
	}

	if (kUnit.getUnitInfo().getExtraCost() > 0)
	{
		iValue /= kUnit.getUnitInfo().getExtraCost() + 1;
	}

	return iValue;
}
// K-Mod end

ReligionTypes CvPlayerAI::AI_chooseReligion()
{
	ReligionTypes eFavorite = (ReligionTypes)GC.getInfo(getLeaderType()).getFavoriteReligion();
	if (NO_RELIGION != eFavorite && !GC.getGame().isReligionFounded(eFavorite))
		return eFavorite;

	//std::vector<ReligionTypes> aeReligions;
	// <advc.171>
	int iSmallest = MAX_INT;
	ReligionTypes eBestReligion = NO_RELIGION; // </advc.171>
	for (int iReligion = 0; iReligion < GC.getNumReligionInfos(); iReligion++)
	{
		ReligionTypes eLoopReligion = (ReligionTypes)iReligion; // advc
		if (GC.getGame().isReligionFounded(eLoopReligion))
			continue;
		//aeReligions.push_back((ReligionTypes)iReligion);
		// <advc.171> Replacing the above
		int iValue = 0;
		TechTypes eLoopTech = (TechTypes)GC.getInfo(eLoopReligion).getTechPrereq();
		if(eLoopTech != NO_TECH)
			iValue = GC.getInfo(eLoopTech).getResearchCost();
		if(iValue < iSmallest) {
			eBestReligion = eLoopReligion;
			iSmallest = iValue;
		}
	}
	return eBestReligion; // </advc.171> // BtS code:
	/*if (aeReligions.size() > 0)
		return aeReligions[GC.getGame().getSorenRandNum(aeReligions.size(), "AI pick religion")];
	return NO_RELIGION;*/
}

int CvPlayerAI::AI_getAttitudeWeight(PlayerTypes ePlayer) const
{
	int iAttitudeWeight = 0;
	switch (AI_getAttitude(ePlayer))
	{
	case ATTITUDE_FURIOUS:
		iAttitudeWeight = -100;
		break;
	case ATTITUDE_ANNOYED:
		iAttitudeWeight = -50;
		break;
	case ATTITUDE_CAUTIOUS:
		iAttitudeWeight = 0;
		break;
	case ATTITUDE_PLEASED:
		iAttitudeWeight = 50;
		break;
	case ATTITUDE_FRIENDLY:
		iAttitudeWeight = 100;
		break;
	}

	return iAttitudeWeight;
}

int CvPlayerAI::AI_getPlotAirbaseValue(CvPlot const& kPlot) const // advc: param was CvPlot*
{
	PROFILE_FUNC();

	if (kPlot.getTeam() != getTeam())
		return 0;

	if (kPlot.isCityRadius())
	{
		CvCityAI const* pWorkingCity = kPlot.AI_getWorkingCity();
		if (pWorkingCity != NULL)
		{
			/*if (pWorkingCity->AI_getBestBuild(pWorkingCity->getCityPlotIndex(&kPlot)) != NO_BUILD)
				return 0;
			if (kPlot.getImprovementType() != NO_IMPROVEMENT) {
				CvImprovementInfo &kImprovementInfo = GC.getInfo(kPlot.getImprovementType());
				if (!kImprovementInfo.isActsAsCity())
					return 0;
			}*/ // BtS
			// K-Mod
			ImprovementTypes eBestImprovement = kPlot.getImprovementType();
			BuildTypes eBestBuild = pWorkingCity->AI_getBestBuild(pWorkingCity->getCityPlotIndex(&kPlot));
			if (eBestBuild != NO_BUILD)
			{
				if (GC.getInfo(eBestBuild).getImprovement() != NO_IMPROVEMENT)
					eBestImprovement = (ImprovementTypes)GC.getInfo(eBestBuild).getImprovement();
			}
			if (eBestImprovement != NO_IMPROVEMENT)
			{
				CvImprovementInfo &kImprovementInfo = GC.getInfo(eBestImprovement);
				if (!kImprovementInfo.isActsAsCity())
				{
					return 0;
				}
			}
			// K-Mod end
		}
	}

	int iMinOtherCityDistance = MAX_INT;
	int iMinFriendlyCityDistance = MAX_INT;
	/*CvPlot const* pMinOtherCityPlot = NULL;
	CvPlot const* pMinFriendlyCityPlot = NULL;*/ // advc: unused

	int iOtherCityCount = 0;

	for (SquareIter it(kPlot, 4, false); it.hasNext(); ++it)
	{
		CvPlot const& p = *it;
		if (p.getTeam() == getTeam())
		{
			if (p.isCity(true))
			{
				int iDist = it.currPlotDist();
				if (iDist == 1)
					return 0;
				if (iDist < iMinFriendlyCityDistance)
				{
					iMinFriendlyCityDistance = iDist;
					//pMinFriendlyCityPlot = &p;
				}
			}
		}
		else
		{
			if (p.isOwned() && p.isCity(false))
			{
				int iDist = it.currPlotDist();
				if (iDist < iMinOtherCityDistance)
				{
					iMinOtherCityDistance = iDist;
					//pMinOtherCityPlot = &p;
					iOtherCityCount++;
				}
			}
		}
	}

	if (iOtherCityCount == 0)
		return 0;
	// (advc: was already commented out in BtS)
	/*if (pMinFriendlyCityPlot != NULL) {
		FAssert(pMinOtherCityPlot != NULL);
		if (plotDistance(pMinFriendlyCityPlot->getX(), pMinFriendlyCityPlot->getY(), pMinOtherCityPlot->getX(), pMinOtherCityPlot->getY()) < (iMinOtherCityDistance - 1))
			return 0;
	}
	if (pMinOtherCityPlot != NULL) {
		CvCity* pNearestCity = GC.getMap().findCity(pMinOtherCityPlot->getX(), pMinOtherCityPlot->getY(), NO_PLAYER, getTeam(), false);
		if (NULL == pNearestCity)
			return 0;
		if (plotDistance(pNearestCity->getX(), pNearestCity->getY(), pMinOtherCityPlot->getX(), pMinOtherCityPlot->getY()) < iRange)
			return 0;
	}*/
	int iDefenseModifier = kPlot.defenseModifier(getTeam(), false);
	/*if (iDefenseModifier <= 0)
		return 0;*/
	int iValue = iOtherCityCount * 50;
	iValue *= 100 + (2 * (iDefenseModifier + (kPlot.isHills() ? 25 : 0)));
	iValue /= 100;
	return iValue;
}

int CvPlayerAI::AI_getPlotCanalValue(CvPlot const& kPlot) const // advc: param was CvPlot*
{
	PROFILE_FUNC();

	if (kPlot.isOwned())
	{
		if (kPlot.getTeam() != getTeam())
			return 0;

		if (kPlot.isCityRadius())
		{
			CvCityAI const* pWorkingCity = kPlot.AI_getWorkingCity();
			if (pWorkingCity != NULL)
			{
				/*if (pWorkingCity->AI_getBestBuild(pWorkingCity->getCityPlotIndex(pPlot)) != NO_BUILD)
					return 0;
				if (pPlot->getImprovementType() != NO_IMPROVEMENT) {
					CvImprovementInfo &kImprovementInfo = GC.getInfo(pPlot->getImprovementType());
					if (!kImprovementInfo.isActsAsCity())
						return 0;
				}*/ // BtS
				// K-Mod
				ImprovementTypes eBestImprovement = kPlot.getImprovementType();
				BuildTypes eBestBuild = pWorkingCity->AI_getBestBuild(pWorkingCity->getCityPlotIndex(&kPlot));
				if (eBestBuild != NO_BUILD)
				{
					if (GC.getInfo(eBestBuild).getImprovement() != NO_IMPROVEMENT)
						eBestImprovement = (ImprovementTypes)GC.getInfo(eBestBuild).getImprovement();
				}
				if (eBestImprovement != NO_IMPROVEMENT)
				{
					CvImprovementInfo &kImprovementInfo = GC.getInfo(eBestImprovement);
					if (!kImprovementInfo.isActsAsCity())
					{
						return 0;
					}
				}
				// K-Mod end
			}
		}
	}

	for (int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
	{
		CvPlot* pLoopPlot = ::plotDirection(kPlot.getX(), kPlot.getY(), (DirectionTypes)iI);
		if (pLoopPlot != NULL && pLoopPlot->isCity(true))
			return 0;
	}

	CvArea* pSecondWaterArea = kPlot.secondWaterArea();
	if (pSecondWaterArea == NULL)
		return 0;

	//return 10 * std::min(0, pSecondWaterArea->getNumTiles() - 2);
	return 10 * std::max(0, pSecondWaterArea->getNumTiles() - 2);
}

//This returns approximately to the sum
//of the percentage values of each unit (there is no need to scale the output by iHappy)
//100 * iHappy means a high value.
int CvPlayerAI::AI_getHappinessWeight(int iHappy, int iExtraPop, bool bPercent) const
{
	if (iHappy == 0)
		iHappy = 1;

	/*  advc.036: This was counting all our cities except Globe Theater (GT).
		I think they should all count; happiness in the GT city just won't
		provide any value. */
	int iCount = getNumCities();

	int iValue = 0;
	FOR_EACH_CITY(pLoopCity, *this)
	{
		/*int iCityHappy = pLoopCity->happyLevel() - pLoopCity->unhappyLevel(iExtraPop);
		iCityHappy -= std::max(0, pLoopCity->getCommerceHappiness());
		int iHappyNow = iCityHappy;
		int iHappyThen = iCityHappy +
			(bPercent ? ROUND_DIVIDE(pLoopCity->getPopulation()*iHappy, 100) : iHappy);
		//Integration
		int iTempValue = (((100 * iHappyThen - 10 * iHappyThen * iHappyThen)) - (100 * iHappyNow - 10 * iHappyNow * iHappyNow));
		if (iHappy > 0)
			iValue += std::max(0, iTempValue);
		else iValue += std::min(0, iTempValue);*/ // BtS

		// K-Mod. I have no idea what they were trying to 'integrate', and it was giving some strange answers.
		// So lets try it my way.
		if (pLoopCity->isNoUnhappiness())
			continue;
		// <advc.036> By the time iExtraPop matters, the espionage effect will be gone.
		if(iExtraPop > 0) {
			iExtraPop = std::max(0, iExtraPop -
					pLoopCity->getEspionageHappinessCounter() / 2);
		} // </advc.036>
		int iCurrentHappy = 100*(pLoopCity->happyLevel() - pLoopCity->unhappyLevel(iExtraPop)
				// advc.036: Subtract half the anger from espionage b/c it is fleeting
				+ pLoopCity->getEspionageHappinessCounter() / 2);
		// I'm only going to subtract half of the commerce happiness, because we might not be using that commerce for only happiness.
		iCurrentHappy -= 50*std::max(0, pLoopCity->getCommerceHappiness());
		int iTestHappy = iCurrentHappy +
				(bPercent ? ((pLoopCity->getPopulation()+iExtraPop)*iHappy) : 100 * iHappy);
		iValue += std::max(0, -iCurrentHappy) - std::max(0, -iTestHappy); // change in the number of angry citizens
		// a small bonus for happiness beyond what we need
		iValue += 100*(std::max(0, iTestHappy) - std::max(0, iCurrentHappy))/(400 + std::max(0, iTestHappy) + std::max(0, iCurrentHappy));
		// K-Mod end
		/*  <advc.036> A little extra when unhappiness is very high because that
			may lead to good tiles not getting worked. */
		if (!bPercent) // Not sure how bPercent works; better exclude that.
		{
			double base = -iCurrentHappy / 100.0 - 1 - iExtraPop;
			if (base > 0)
				iValue += ::round(std::pow(base, 1.5));
		} // </advc.036>
		/*if (iCount > 6)
			break;*/ // BtS
	}

	return (iCount == 0) ? 50 * iHappy : iValue / iCount;
}

int CvPlayerAI::AI_getHealthWeight(int iHealth, int iExtraPop, bool bPercent) const
{
	if (iHealth == 0)
		iHealth = 1;

	int iCount = getNumCities(); // advc.036: See AI_getHappinessWeight

	int iValue = 0;
	FOR_EACH_CITY(pLoopCity, *this)
	{
		// original bts code: [...] deleted (advc)
		// <advc.036> See HappinessWeight
		if(iExtraPop > 0)
			iExtraPop = std::max(0, iExtraPop - pLoopCity->getEspionageHealthCounter() / 2);
		// </advc.036>
		// K-Mod. Changed to match the happiness function.
		int iCurrentHealth = 100*(pLoopCity->goodHealth() - pLoopCity->badHealth(false, iExtraPop)
				// advc.036: Subtract half the bad health from espionage b/c it is fleeting
				+ pLoopCity->getEspionageHealthCounter() / 2);
		int iTestHealth = iCurrentHealth +
			(bPercent ? ((pLoopCity->getPopulation()+iExtraPop)*iHealth) : 100 * iHealth);
		iValue += std::max(0, -iCurrentHealth) - std::max(0, -iTestHealth); // change in the number of unhealthy citizens
		// a small bonus for health beyond what we need
		iValue += 100*(std::max(0, iTestHealth) - std::max(0, iCurrentHealth))/(400 + std::max(0, iTestHealth) + std::max(0, iCurrentHealth));
		// K-Mod end
		/*  <advc.036> A little extra when health is very poor because that may
			well lead to population loss and good tiles not getting worked. */
		if (!bPercent) // Not sure how bPercent works; better exclude that.
		{
			double base = -iCurrentHealth / 100.0 - 1 - iExtraPop;
			if (base > 0)
				iValue += ::round(std::pow(base, 1.5));
		} // </advc.036>
		/*if (iCount > 6)
			break;*/
	}
	//return (0 == iCount) ? 50 : iValue / iCount;
	/*  UNOFFICIAL_PATCH, Bugfix, 10/21/09, jdog5000
		Mirror happiness valuation code */
	return (iCount == 0) ? 50 * iHealth : iValue / iCount;
}

/* void CvPlayerAI::AI_invalidateCloseBordersAttitudeCache() {
	for (int i = 0; i < MAX_PLAYERS; ++i)
		m_aiCloseBordersAttitude[i] = MAX_INT;
} */ // disabled by K-Mod

bool CvPlayerAI::AI_isPlotThreatened(CvPlot* pPlot, int iRange, bool bTestMoves) const
{
	PROFILE_FUNC();

	if(iRange == -1)
		iRange = DANGER_RANGE;
	CvArea const& kPlotArea = pPlot->getArea();
	for (SquareIter it(*pPlot, iRange); it.hasNext(); ++it)
	{
		CvPlot const& p = *it;
		//if(!p.sameArea(kPlotArea))
		// <advc.030> Replacing the above
		if(!kPlotArea.canBeEntered(p.getArea()))
			continue; // </advc.030>
		// <advc.128>
		CLLNode<IDInfo> const* pUnitNode = p.headUnitNode();
		if(pUnitNode != NULL && !p.isVisible(getTeam()))
		{
			if(isHuman() || !AI_cheatDangerVisibility(p))
				continue;
		} // </advc.128>
		for (; pUnitNode != NULL; pUnitNode = p.nextUnitNode(pUnitNode))
		{
			CvUnit* pLoopUnit = ::getUnit(pUnitNode->m_data);
			if (pLoopUnit->isEnemy(getTeam(), *pPlot) &&
				// advc.315: was pLoopUnit->canAttack()
				AI_canBeAttackedBy(*pLoopUnit) &&
				!pLoopUnit->isInvisible(getTeam(), false))
			{
				if(pLoopUnit->canMoveOrAttackInto(*pPlot, /* advc.001k: */ false, true))
				{
					if(!bTestMoves)
						return true; // advc
					/*int iPathTurns = 0;
					if(!pLoopUnit->getGroup()->generatePath(&p, pPlot, MOVE_MAX_MOVES | MOVE_IGNORE_DANGER, false, &iPathTurns))
						iPathTurns = MAX_INT;
					if(iPathTurns <= 1)
						return true;*/ // BtS
					// K-Mod. Use a temp pathfinder, so as not to interfere with the normal one.
					/*KmodPathFinder temp_finder;
					temp_finder.SetSettings(pLoopUnit->getGroup(), MOVE_MAX_MOVES | MOVE_IGNORE_DANGER, 1, GC.getMOVE_DENOMINATOR());
					if (temp_finder.GeneratePath(pPlot))
						return true;*/
					// <advc.128> Moved into CvUnit::generatePath
					return pLoopUnit->generatePath(pPlot, MOVE_MAX_MOVES |
							MOVE_IGNORE_DANGER, false, NULL, 1, true);
					// </advc.128>
				}
			}
		}
	}
	return false;
}

// <advc.315>
bool CvPlayerAI::AI_canBeAttackedBy(CvUnit const& u) const
{
	if(!u.canAttack() ||
		/*  advc.315a: Since animals don't worry about being attacked,
			OnlyAttackAnimals can be treated like !canAttack here. */
		u.getUnitInfo().isOnlyAttackAnimals())
	{
		return false;
	}
	// <advc.315b>
	if(isBarbarian())
		return true;
	return !u.getUnitInfo().isOnlyAttackBarbarians();
	// </advc.315b>
} // </advc.315>

bool CvPlayerAI::AI_isFirstTech(TechTypes eTech) const
{
	if (eTech == NO_TECH)
		return false; // K-Mod

	for (int iI = 0; iI < GC.getNumReligionInfos(); iI++)
	{
		if (GC.getInfo((ReligionTypes)iI).getTechPrereq() == eTech)
		{
			if (!GC.getGame().isReligionSlotTaken((ReligionTypes)iI))
			{
				return true;
			}
		}
	}

	if (GC.getGame().countKnownTechNumTeams(eTech) == 0)
	{
		if ((getTechFreeUnit(eTech) != NO_UNIT) ||
			(GC.getInfo(eTech).getFirstFreeTechs() > 0))
		{
			return true;
		}
	}

	return false;
}

// K-Mod
void CvPlayerAI::AI_ClearConstructionValueCache()
{
	FOR_EACH_CITYAI_VAR(pLoopCity, *this)
	{
		// Have I meantioned that the way the "AI" classes are used in this code is an abomination and an insult to OO programming?
		//static_cast<CvCityAI*>(pLoopCity)->AI_ClearConstructionValueCache();
		pLoopCity->AI_ClearConstructionValueCache(); // advc
	}
}
// K-Mod end
/*  <k146> Check that we have the required bonuses to train the given unit.
	This isn't for any particular city. It's just a rough guide for whether or
	not we could build the unit. */
bool CvPlayerAI::AI_haveResourcesToTrain(UnitTypes eUnit) const
{
	FAssertBounds(0, GC.getNumUnitInfos(), eUnit);

	const CvUnitInfo& kUnit = GC.getInfo(eUnit);
	//const CvTeam& kTeam = GET_TEAM(getTeam());

	// "and" bonus
	BonusTypes ePrereqAndBonus = kUnit.getPrereqAndBonus();
	if (ePrereqAndBonus != NO_BONUS)
	{
		if (!hasBonus(ePrereqAndBonus) && AI_countOwnedBonuses(ePrereqAndBonus) == 0)
			return false;
	}

	// "or" bonuses
	bool bMissingBonus = false;
	for (int i = 0; i < GC.getNUM_UNIT_PREREQ_OR_BONUSES(eUnit); ++i)
	{
		BonusTypes ePrereqOrBonus = kUnit.getPrereqOrBonuses(i);
		if (ePrereqOrBonus == NO_BONUS)
			continue;

		if (hasBonus(ePrereqOrBonus) || AI_countOwnedBonuses(ePrereqOrBonus) > 0)
		{
			bMissingBonus = false;
			break;
		}
		bMissingBonus = true;
	}

	return !bMissingBonus;
} // </k146>

// advc.079: Cut from CvPlayer::getBestAttackUnitKey and refactored
UnitTypes CvPlayerAI::AI_getBestAttackUnit() const
{
	UnitTypes eBestUnit = NO_UNIT;
	{
		CvCityAI const* pCapitalCity = AI_getCapitalCity();
		if(pCapitalCity != NULL)
		{
			eBestUnit = pCapitalCity->AI_bestUnitAI(UNITAI_ATTACK, true);
			if(eBestUnit != NO_UNIT)
				return eBestUnit;
		}
	}
	FOR_EACH_CITYAI(pLoopCity, *this)
	{
		eBestUnit = pLoopCity->AI_bestUnitAI(UNITAI_ATTACK, true);
		if(eBestUnit != NO_UNIT)
			break;
	}
	return eBestUnit;
}

// <advc.033> Are we willing to attack/pillage them with hidden-nationality units
bool CvPlayerAI::AI_isPiracyTarget(PlayerTypes eTarget) const
{
	if(eTarget == NO_PLAYER)
	{
		FAssert(eTarget != NO_PLAYER);
		return false;
	}
	if(TEAMID(eTarget) == getTeam())
		return false;
	if(getID() == BARBARIAN_PLAYER || eTarget == BARBARIAN_PLAYER)
	{
		/*  Can't plunder Barbs, but this function shouldn't be used for
			deciding whether to plunder. */
		return true;
	}
	if(GET_TEAM(eTarget).isAtWar(getTeam()))
		return true;
	if(GET_TEAM(getTeam()).isVassal(TEAMID(eTarget)) ||
		GET_TEAM(eTarget).isVassal(getTeam()))
	{
		return false;
	}
	return (AI_getAttitude(eTarget) <= GC.getInfo(getPersonalityType()).
			getDeclareWarThemRefuseAttitudeThreshold());
} // </advc.033>

// <advc.130h> <advc.130r>
bool CvPlayerAI::AI_atWarWithPartner(TeamTypes eOtherTeam, bool bCheckPartnerAttacked) const
{
	for(int i = 0; i < MAX_CIV_PLAYERS; i++)
	{
		CvPlayer const& kPartner = GET_PLAYER((PlayerTypes)i);
		if(kPartner.isAlive() && kPartner.getTeam() != getTeam() &&
			!kPartner.isMinorCiv() && GET_TEAM(eOtherTeam).isAtWar(kPartner.getTeam()) &&
			AI_getAttitude(kPartner.getID()) >= ATTITUDE_PLEASED)
		{
			if(!bCheckPartnerAttacked)
				return true;
			WarPlanTypes eWarPlan = GET_TEAM(kPartner.getTeam()).AI_getWarPlan(eOtherTeam);
			if(eWarPlan == WARPLAN_ATTACKED || eWarPlan == WARPLAN_ATTACKED_RECENT)
				return true;
		}
	}
	return false;
} // </advc.130r>

/*  Not a CvTeamAI function b/c I want to call atWarWithPartner, which I'd rather
	keep at CvPlayerAI. */
bool CvPlayerAI::AI_disapprovesOfDoW(TeamTypes eAggressor, TeamTypes eVictim) const
{
	if(!isAlive() || isBarbarian() || isMinorCiv() || eAggressor == getTeam() ||
			eVictim == getTeam())
		return false;
	CvTeamAI const& kAggressor = GET_TEAM(eAggressor);
	if(!kAggressor.isAlive() || kAggressor.isBarbarian() || kAggressor.isMinorCiv())
		return false;
	CvTeamAI const& kVictim = GET_TEAM(eVictim);
	if(!kVictim.isAlive() || kVictim.isBarbarian() || kVictim.isMinorCiv() ||
			kVictim.isCapitulated())
		return false;
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
	if(//!kOurTeam.isHasMet(eAggressor) || // Not checked by BtS either
			!kOurTeam.isHasMet(eVictim) || kOurTeam.isAtWar(eVictim) ||
			kVictim.getMasterTeam() == kOurTeam.getMasterTeam())
		return false;
	// Not if eTeam is also fighting a partner and (appears to have) started it
	if((kOurTeam.AI_getMemoryCount(eVictim, MEMORY_DECLARED_WAR_ON_FRIEND) > 0) &&
			AI_atWarWithPartner(eVictim, true))
		return false;
	return (kOurTeam.AI_getAttitude(eVictim) >= ATTITUDE_PLEASED);
} // </advc.130h>

// <advc.651>
void CvPlayerAI::AI_updateDangerFromSubmarines()
{
	m_bDangerFromSubmarines = false;
	for(int i = 0; i < MAX_CIV_PLAYERS; i++)
	{
		CvPlayerAI const& kWarEnemy = GET_PLAYER((PlayerTypes)i);
		if(!kWarEnemy.isAlive() || kWarEnemy.isMinorCiv() ||
				!GET_TEAM(kWarEnemy.getTeam()).isAtWar(getTeam()))
			continue;
		CvCivilization const& kCiv = kWarEnemy.getCivilization(); // advc.003w
		for (int i = 0; i < kCiv.getNumUnits(); i++)
		{
			UnitTypes eUnit = kCiv.unitAt(i);
			CvUnitInfo const& u = GC.getInfo(eUnit);
			if(u.getDomainType() == DOMAIN_SEA &&
				u.getInvisibleType() != NO_INVISIBLE &&
				kWarEnemy.AI_canBeExpectedToTrain(eUnit))
			{
				m_bDangerFromSubmarines = true;
				return;
			}
		}
	}
}

bool CvPlayerAI::AI_isDangerFromSubmarines() const
{
	return m_bDangerFromSubmarines;
} // </advc.651>

// <advc.128>
bool CvPlayerAI::AI_cheatDangerVisibility(CvPlot const& kAt) const
{
	double const pr = 0.5;
	std::vector<long> hashInputs;
	hashInputs.push_back(kAt.getX());
	hashInputs.push_back(kAt.getY());
	hashInputs.push_back(GC.getGame().getGameTurn());
	hashInputs.push_back(getID());
	return (::hash(hashInputs) < pr);

	/*  This is a bit faster, but, in the end, I still had to reduce the number of
		calls to this function, so I'm going to use the more readable (and possibly
		more reliable) code above. */
	/*	Should be possible to fit x, y, turn and id into a unique uint, but, if not,
		prime coefficients should reduce the probability of collisions (I guess). */
	/*unsigned int x = pAt.getX() + 251 * pAt.getY() +
			63029 * GC.getGame().getGameTurn() + 94543517 * getID();
	// Marsaglia Xorshift (may not work so well on 32-bit machines)
	x ^= x << 13; x ^= x >> 7; x ^= x << 17;
	return (x < MAX_UNSIGNED_INT / 2);*/
} // </advc.128>

// <advc.104> <advc.651>
bool CvPlayerAI::AI_canBeExpectedToTrain(UnitTypes eUnit) const
{
	PROFILE_FUNC();
	if(eUnit == NO_UNIT)
	{
		FAssert(false);
		return false;
	}
	CvUnitInfo& u = GC.getInfo(eUnit);
	bool bSeaUnit = (u.getDomainType() == DOMAIN_SEA);
	int iMinAreaSz = std::max(0, u.getMinAreaSize());
	/*  Should be able to train at least two units in ten turns (i.e. one in five);
		otherwise, the unit probably won't be trained at all, or just 1 or 2. */
	int iTargetProduction = ::round(getProductionNeeded(eUnit) / 5.0);
	int iPartialSum = 0;
	FOR_EACH_CITY(c, *this)
	{
		if((bSeaUnit && !c->isCoastal(iMinAreaSz)) || !c->canTrain(eUnit))
			continue;
		iPartialSum += c->getProduction();
		if(iPartialSum >= iTargetProduction)
			return true;
	}
	return false;
} // </advc.104> </advc.651>

// <advc.300>
bool CvPlayerAI::AI_isDefenseFocusOnBarbarians(CvArea const& kArea) const
{
	CvGame const& kGame = GC.getGame();
	if(kArea.getAreaAIType(getTeam()) == AREAAI_DEFENSIVE || getNumCities() <= 1 ||
		kArea.isBorderObstacle(getTeam()) || AI_isDoStrategy(AI_STRATEGY_ALERT1) ||
		GC.getGame().isOption(GAMEOPTION_NO_BARBARIANS) ||
		kGame.getGameTurn() < kGame.getBarbarianStartTurn() || AI_isFocusWar())
	{
		return false;
	}
	return kGame.isBarbarianCreationEra(); // advc.307
} // </advc.300>

/*  <advc.132> This function is going to overestimate the cost of anarchy
	when multiple TRADE_CIVIC and TRADE_RELIGION items are on the table.
	Would have to know the full list of trade items for an accurate cost. */
int CvPlayerAI::AI_anarchyTradeVal(CivicTypes eCivic) const
{
	// This omits food surplus and GPP, but also expenses.
	double r = estimateYieldRate(YIELD_COMMERCE) +
			2 * estimateYieldRate(YIELD_PRODUCTION);
	int iAnarchyLength = -1;
	int iSwitchBackAnarchyLength = -1;
	if(eCivic != NO_CIVIC)
	{
		CivicTypes* civics = new CivicTypes[GC.getNumCivicOptionInfos()];
		for(int i = 0; i < GC.getNumCivicOptionInfos(); i++)
			civics[i] = getCivics((CivicOptionTypes)i);
		CivicOptionTypes eOption = (CivicOptionTypes)GC.getInfo(eCivic).getCivicOptionType();
		if(eOption != NO_CIVICOPTION)
			civics[eOption] = eCivic;
		else FAssert(eOption != NO_CIVICOPTION);
		iAnarchyLength = getCivicAnarchyLength(civics);
		iSwitchBackAnarchyLength = getCivicAnarchyLength(civics, true);
		SAFE_DELETE_ARRAY(civics);
	}
	else
	{
		iAnarchyLength = getReligionAnarchyLength();
		iSwitchBackAnarchyLength = getReligionAnarchyLength(true);
	}
	FAssert(iAnarchyLength >= 0 && iSwitchBackAnarchyLength >= 0);
	/*  Uncertain whether we'll want to switch back; fair chance that it can
		be piggybacked on another revolution. */
	r *= (iAnarchyLength + 0.36 * iSwitchBackAnarchyLength);
	/*  Going to get only half the trade val as gpt. But we're also going to
		benefit from denying gold to a competitor. So far, only humans can
		trade for religion and civics changes, and they're strong competitors.
		Therefore multiply by a factor smaller than 2. */
	r *= 1.73;
	return ::round(r);
} // </advc.132>

// <advc.109> Akin to AI_getTotalAreaCityThreat, but much coarser.
bool CvPlayerAI::AI_feelsSafe() const
{
	CvTeamAI const& kOurTeam = GET_TEAM(getTeam());
	if(kOurTeam.AI_countWarPlans(NUM_WARPLAN_TYPES, false) > 0)
		return false;
	CvGame const& g = GC.getGame();
	CvCity* pCapital = getCapitalCity();
	EraTypes iGameEra = g.getCurrentEra();
	/*  >=3: Anyone could attack across the sea, and can't fully trust friends
		anymore either as the game progresses */
	if(pCapital == NULL || iGameEra >= 3 || AI_isThreatFromMinorCiv())
		return false;
	if(!pCapital->getArea().isBorderObstacle(getTeam()) &&
		((((iGameEra <= 2 && iGameEra > 0) ||
		(iGameEra > 2 && iGameEra == g.getStartEra())) &&
		g.isOption(GAMEOPTION_RAGING_BARBARIANS)) ||
		GET_TEAM(BARBARIAN_TEAM).countNumCitiesByArea(getCapitalCity()->getArea()) >
		::round(getNumCities() / 3.0)))
	{
		return false;
	}
	for(int i = 0; i < MAX_CIV_TEAMS; i++)
	{
		if(i == getTeam())
			continue;
		CvTeamAI const& t = GET_TEAM((TeamTypes)i);
		if(!t.isAlive() || !kOurTeam.isHasMet(t.getID()) || t.isMinorCiv())
			continue;
		if(t.isHuman())
			return false;
		if(!t.AI_isAvoidWar(getTeam()) && // advc.104y
				3 * t.getPower(true) > 2 * kOurTeam.getPower(false) &&
				(t.getCurrentEra() >= 3 || // seafaring
				t.AI_isPrimaryArea(getCapitalCity()->getArea())))
				// Check this instead? Might be a bit slow ...
				// kOurTeam.AI_hasCitiesInPrimaryArea(t.getID())
			return false;
	}
	return true;
}

bool CvPlayerAI::AI_isThreatFromMinorCiv() const
{
	for(int i = 0; i < MAX_CIV_PLAYERS; i++)
	{
		CvPlayerAI const& p = GET_PLAYER((PlayerTypes)i);
		if(p.isMinorCiv() && p.isAlive() && 2 * p.getPower() > getPower() &&
				(getCapitalCity() == NULL ||
				p.AI_isPrimaryArea(getCapitalCity()->getArea())))
			return true;
	}
	return false;
} // </advc.109>

// <dlph.16> (Though DarkLunaPhantom didn't put this in a separate function)
int CvPlayerAI::AI_nukeDangerDivisor() const
{
	if(GC.getGame().isNoNukes())
		return 15;
	bool bRemoteDanger = false;
	CvLeaderHeadInfo& ourPers = GC.getInfo(getPersonalityType());
	// "we're going to cheat a little bit, by counting nukes that we probably shouldn't know about."
	for(int i = 0; i < MAX_CIV_PLAYERS; i++)
	{
		CvPlayerAI const& kLoopPlayer = GET_PLAYER((PlayerTypes)i);
		// Vassals can't have nukes b/c of change advc.143b
		if(!kLoopPlayer.isAlive() || kLoopPlayer.isAVassal() ||
				!GET_TEAM(getTeam()).isHasMet(kLoopPlayer.getTeam()) ||
				kLoopPlayer.getTeam() == getTeam())
			continue;
		/*  advc: Avoid building shelters against friendly
			nuclear powers. This is mostly role-playing. */
		AttitudeTypes towardThem = AI_getAttitude(kLoopPlayer.getID());
		if(ourPers.getNoWarAttitudeProb(towardThem) >= 100 &&
				(kLoopPlayer.isHuman() ?
				towardThem >= ATTITUDE_FRIENDLY :
				// advc.104y:
				GET_TEAM(kLoopPlayer.getTeam()).AI_isAvoidWar(getTeam())))
			continue;
		if(kLoopPlayer.getNumNukeUnits() > 0)
			return 1; // Greatest danger, smallest divisor.
		if(kLoopPlayer.getCurrentEra() >= 5)
			bRemoteDanger = true;
	}
	if(bRemoteDanger)
		return 10;
	return 20;
} // </dlph.16>

// <advc> (various uses)  Mostly copy-pasted from the homonymous CvTeamAI function
bool CvPlayerAI::AI_hasSharedPrimaryArea(PlayerTypes eOther) const
{
	FAssert(eOther != getID());
	CvPlayerAI const& kOther = GET_PLAYER(eOther);
	FOR_EACH_AREA(a)
	{
		if (AI_isPrimaryArea(*a) && kOther.AI_isPrimaryArea(*a))
			return true;
	}
	return false;
} // <advc>

/*  <advc.127> Tbd.: There may well be other AI data to be updated when
	human control is suspended or resumed. */
void CvPlayerAI::AI_setHumanDisabled(bool bDisabled)
{
	// Some of the first-impression modifiers don't apply to human players
	for (PlayerIter<MAJOR_CIV> it; it.hasNext(); ++it)
		it->AI_updateAttitude();
} // </advc.127>

// <advc.031c>
void CvPlayerAI::logFoundValue(int iX, int iY, bool bStartingLoc) const
{
	CitySiteEvaluator eval(*this, isBarbarian() ?
			GC.getDefineINT("MIN_BARBARIAN_CITY_STARTING_DISTANCE") : -1, bStartingLoc);
	eval.log(iX, iY);
} // </advc.031c>

// BETTER_BTS_AI_MOD, General AI/ Efficiency (plot danger cache), 08/20/09, jdog5000: START

// The vast majority of checks for plot danger are boolean checks during path planning for non-combat
// units like workers, settlers, and GP.  Since these are simple checks for any danger they can be
// cutoff early if danger is found.  To this end, the two caches tracked are for whether a given plot
// is either known to be safe for the player who is currently moving, or for whether the plot is
// known to be a plot bordering an enemy of this team and therefore unsafe.
//
// The safe plot cache is only for the active moving player and is only set if this is not a
// multiplayer game with simultaneous turns.  The safety cache for all plots is reset when the active
// player changes or a new game is loaded.
//
// The border cache is done by team and works for all game types.  The border cache is reset for all
// plots when war or peace are declared, and reset over a limited range whenever a ownership over a plot
// changes.

// K-Mod. The cache also needs to be reset when routes are destroyed, because distance 2 border danger only counts when there is a route.
// Actually, the cache doesn't need to be cleared when war is declared; because false negatives have no impact with this cache.
// The safe plot cache can be invalid if we kill an enemy unit. Currently this is unaccounted for, and so the cache doesn't always match the true state.
// In general, I think this cache is a poorly planned idea. It's prone to subtle bugs if there are rule changes in seemingly independent parts of the games.
//
// I've done a bit of speed profiling and found that although the safe plot cache does shortcut around 50% of calls to AI_getAnyPlotDanger,
// that only ends up saving a few milliseconds each turn anyway. I don't really think that's worth the risk of getting problems from bad cache.
// So even though I've put a bit of work into making the cache work better, I'm just going to disable it.

bool CvPlayerAI::isSafeRangeCacheValid() const
{
	/*	advc: To turn the cache back on, the two call locations in AI_getPlotDanger
		would have to be uncommented. */
	FAssertMsg(false, "SafeRangeCache is disabled");
	return false; // K-Mod: Cache disabled. See comments above.
	//return isTurnActive() && !GC.getGame().isMPOption(MPOPTION_SIMULTANEOUS_TURNS) && GC.getGame().getNumGameTurnActive() == 1;
}

// advc.003j: Probably unused. (There's a virtual wrapper that the EXE could call.)
bool CvPlayerAI::AI_isCommercePlot(CvPlot* pPlot) const
{
	return (pPlot->getYield(YIELD_FOOD) >= GC.getFOOD_CONSUMPTION_PER_POPULATION());
}
