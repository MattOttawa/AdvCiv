// advc.003x: Cut from CvInfos.cpp

#include "CvGameCoreDLL.h"
#include "CvInfo_Civics.h"
#include "CvXMLLoadUtility.h"
#include "CvDLLXMLIFaceBase.h"


CvCivicInfo::CvCivicInfo() :
m_iCivicOptionType(NO_CIVICOPTION),
m_iAnarchyLength(0),
m_iUpkeep(0),
m_iAIWeight(0),
m_iGreatPeopleRateModifier(0),
m_iGreatGeneralRateModifier(0),
m_iDomesticGreatGeneralRateModifier(0),
m_iStateReligionGreatPeopleRateModifier(0),
m_iDistanceMaintenanceModifier(0),
m_iNumCitiesMaintenanceModifier(0),
m_iCorporationMaintenanceModifier(0),
m_iExtraHealth(0),
m_iExtraHappiness(0), // K-Mod
m_iFreeExperience(0),
m_iWorkerSpeedModifier(0),
m_iImprovementUpgradeRateModifier(0),
m_iMilitaryProductionModifier(0),
m_iBaseFreeUnits(0),
m_iBaseFreeMilitaryUnits(0),
m_iFreeUnitsPopulationPercent(0),
m_iFreeMilitaryUnitsPopulationPercent(0),
m_iGoldPerUnit(0),
m_iGoldPerMilitaryUnit(0),
m_iHappyPerMilitaryUnit(0),
m_iLuxuryModifier(0), // advc.912c
m_iLargestCityHappiness(0),
m_iWarWearinessModifier(0),
m_iFreeSpecialist(0),
m_iTradeRoutes(0),
m_iTechPrereq(NO_TECH),
m_iCivicPercentAnger(0),
m_iMaxConscript(0),
m_iStateReligionHappiness(0),
m_iNonStateReligionHappiness(0),
m_iStateReligionUnitProductionModifier(0),
m_iStateReligionBuildingProductionModifier(0),
m_iStateReligionFreeExperience(0),
m_iExpInBorderModifier(0),
m_bMilitaryFoodProduction(false),
//m_bNoUnhealthyPopulation(false),
m_iUnhealthyPopulationModifier(0), // K-Mod
m_bBuildingOnlyHealthy(false),
m_bNoForeignTrade(false),
m_bNoCorporations(false),
m_bNoForeignCorporations(false),
m_bStateReligion(false),
m_bNoNonStateReligionSpread(false),
m_piYieldModifier(NULL),
m_piCapitalYieldModifier(NULL),
m_piTradeYieldModifier(NULL),
m_piCommerceModifier(NULL),
m_piCapitalCommerceModifier(NULL),
m_piSpecialistExtraCommerce(NULL),
m_paiBuildingHappinessChanges(NULL),
m_paiBuildingHealthChanges(NULL),
m_paiFeatureHappinessChanges(NULL),
m_pabHurry(NULL),
m_pabSpecialBuildingNotRequired(NULL),
m_pabSpecialistValid(NULL),
m_ppiImprovementYieldChanges(NULL)
{}

CvCivicInfo::~CvCivicInfo()
{
	SAFE_DELETE_ARRAY(m_piYieldModifier);
	SAFE_DELETE_ARRAY(m_piCapitalYieldModifier);
	SAFE_DELETE_ARRAY(m_piTradeYieldModifier);
	SAFE_DELETE_ARRAY(m_piCommerceModifier);
	SAFE_DELETE_ARRAY(m_piCapitalCommerceModifier);
	SAFE_DELETE_ARRAY(m_piSpecialistExtraCommerce);
	SAFE_DELETE_ARRAY(m_paiBuildingHappinessChanges);
	SAFE_DELETE_ARRAY(m_paiBuildingHealthChanges);
	SAFE_DELETE_ARRAY(m_paiFeatureHappinessChanges);
	SAFE_DELETE_ARRAY(m_pabHurry);
	SAFE_DELETE_ARRAY(m_pabSpecialBuildingNotRequired);
	SAFE_DELETE_ARRAY(m_pabSpecialistValid);
	if (m_ppiImprovementYieldChanges != NULL)
	{
		for (int iI = 0; iI < GC.getNumImprovementInfos(); iI++)
			SAFE_DELETE_ARRAY(m_ppiImprovementYieldChanges[iI]);
		SAFE_DELETE_ARRAY(m_ppiImprovementYieldChanges);
	}
}

int CvCivicInfo::getCivicOptionType() const
{
	return m_iCivicOptionType;
}

int CvCivicInfo::getAnarchyLength() const
{
	return m_iAnarchyLength;
}

int CvCivicInfo::getUpkeep() const
{
	return m_iUpkeep;
}

int CvCivicInfo::getAIWeight() const
{
	return m_iAIWeight;
}

int CvCivicInfo::getGreatPeopleRateModifier() const
{
	return m_iGreatPeopleRateModifier;
}

int CvCivicInfo::getGreatGeneralRateModifier() const
{
	return m_iGreatGeneralRateModifier;
}

int CvCivicInfo::getDomesticGreatGeneralRateModifier() const
{
	return m_iDomesticGreatGeneralRateModifier;
}

int CvCivicInfo::getStateReligionGreatPeopleRateModifier() const
{
	return m_iStateReligionGreatPeopleRateModifier;
}

int CvCivicInfo::getDistanceMaintenanceModifier() const
{
	return m_iDistanceMaintenanceModifier;
}

int CvCivicInfo::getNumCitiesMaintenanceModifier() const
{
	return m_iNumCitiesMaintenanceModifier;
}

int CvCivicInfo::getCorporationMaintenanceModifier() const
{
	return m_iCorporationMaintenanceModifier;
}

int CvCivicInfo::getExtraHealth() const
{
	return m_iExtraHealth;
}

int CvCivicInfo::getFreeExperience() const
{
	return m_iFreeExperience;
}

int CvCivicInfo::getWorkerSpeedModifier() const
{
	return m_iWorkerSpeedModifier;
}

int CvCivicInfo::getImprovementUpgradeRateModifier() const
{
	return m_iImprovementUpgradeRateModifier;
}

int CvCivicInfo::getMilitaryProductionModifier() const
{
	return m_iMilitaryProductionModifier;
}

int CvCivicInfo::getBaseFreeUnits() const
{
	return m_iBaseFreeUnits;
}

int CvCivicInfo::getBaseFreeMilitaryUnits() const
{
	return m_iBaseFreeMilitaryUnits;
}

int CvCivicInfo::getFreeUnitsPopulationPercent() const
{
	return m_iFreeUnitsPopulationPercent;
}

int CvCivicInfo::getFreeMilitaryUnitsPopulationPercent() const
{
	return m_iFreeMilitaryUnitsPopulationPercent;
}

int CvCivicInfo::getGoldPerUnit() const
{
	return m_iGoldPerUnit;
}

int CvCivicInfo::getGoldPerMilitaryUnit() const
{
	return m_iGoldPerMilitaryUnit;
}

int CvCivicInfo::getHappyPerMilitaryUnit() const
{
	return m_iHappyPerMilitaryUnit;
}
// <advc.912c>
int CvCivicInfo::getLuxuryModifier() const
{
	return m_iLuxuryModifier;
} // </advc.912c>

int CvCivicInfo::getLargestCityHappiness() const
{
	return m_iLargestCityHappiness;
}

int CvCivicInfo::getWarWearinessModifier() const
{
	return m_iWarWearinessModifier;
}

int CvCivicInfo::getFreeSpecialist() const
{
	return m_iFreeSpecialist;
}

int CvCivicInfo::getTradeRoutes() const
{
	return m_iTradeRoutes;
}

int CvCivicInfo::getTechPrereq() const
{
	return m_iTechPrereq;
}

int CvCivicInfo::getCivicPercentAnger() const
{
	return m_iCivicPercentAnger;
}

int CvCivicInfo::getMaxConscript() const
{
	return m_iMaxConscript;
}

int CvCivicInfo::getStateReligionHappiness() const
{
	return m_iStateReligionHappiness;
}

int CvCivicInfo::getNonStateReligionHappiness() const
{
	return m_iNonStateReligionHappiness;
}

int CvCivicInfo::getStateReligionUnitProductionModifier() const
{
	return m_iStateReligionUnitProductionModifier;
}

int CvCivicInfo::getStateReligionBuildingProductionModifier() const
{
	return m_iStateReligionBuildingProductionModifier;
}

int CvCivicInfo::getStateReligionFreeExperience() const
{
	return m_iStateReligionFreeExperience;
}

int CvCivicInfo::getExpInBorderModifier() const
{
	return m_iExpInBorderModifier;
}

bool CvCivicInfo::isMilitaryFoodProduction() const
{
	return m_bMilitaryFoodProduction;
}
/*bool CvCivicInfo::isNoUnhealthyPopulation() const
{
	return m_bNoUnhealthyPopulation;
}*/
// K-Mod, 27/dec/10: replaced NoUnhealthyPopulation with UnhealthyPopulationModifier
int CvCivicInfo::getUnhealthyPopulationModifier() const
{
	return m_iUnhealthyPopulationModifier;
} // K-Mod end

bool CvCivicInfo::isBuildingOnlyHealthy() const
{
	return m_bBuildingOnlyHealthy;
}

bool CvCivicInfo::isNoForeignTrade() const
{
	return m_bNoForeignTrade;
}

bool CvCivicInfo::isNoCorporations() const
{
	return m_bNoCorporations;
}

bool CvCivicInfo::isNoForeignCorporations() const
{
	return m_bNoForeignCorporations;
}

bool CvCivicInfo::isStateReligion() const
{
	return m_bStateReligion;
}

bool CvCivicInfo::isNoNonStateReligionSpread() const
{
	return m_bNoNonStateReligionSpread;
}

const wchar* CvCivicInfo::getWeLoveTheKing()
{
	return m_szWeLoveTheKingKey;
}

void CvCivicInfo::setWeLoveTheKingKey(const TCHAR* szVal)
{
	m_szWeLoveTheKingKey = szVal;
}

int CvCivicInfo::getYieldModifier(int i) const
{
	FAssertBounds(0, NUM_YIELD_TYPES, i);
	return m_piYieldModifier ? m_piYieldModifier[i] : 0; // advc.003t
}

int* CvCivicInfo::getYieldModifierArray() const
{
	return m_piYieldModifier;
}

int CvCivicInfo::getCapitalYieldModifier(int i) const
{
	FAssertBounds(0, NUM_YIELD_TYPES, i);
	return m_piCapitalYieldModifier ? m_piCapitalYieldModifier[i] : 0; // advc.003t
}

int* CvCivicInfo::getCapitalYieldModifierArray() const
{
	return m_piCapitalYieldModifier;
}

int CvCivicInfo::getTradeYieldModifier(int i) const
{
	FAssertBounds(0, NUM_YIELD_TYPES, i);
	return m_piTradeYieldModifier ? m_piTradeYieldModifier[i] : 0; // advc.003t
}

int* CvCivicInfo::getTradeYieldModifierArray() const
{
	return m_piTradeYieldModifier;
}

int CvCivicInfo::getCommerceModifier(int i) const
{
	FAssertBounds(0, NUM_COMMERCE_TYPES, i);
	return m_piCommerceModifier ? m_piCommerceModifier[i] : 0; // advc.003t
}

int* CvCivicInfo::getCommerceModifierArray() const
{
	return m_piCommerceModifier;
}

int CvCivicInfo::getCapitalCommerceModifier(int i) const
{
	FAssertBounds(0, NUM_COMMERCE_TYPES, i);
	return m_piCapitalCommerceModifier ? m_piCapitalCommerceModifier[i] : 0; // advc.003t
}

int* CvCivicInfo::getCapitalCommerceModifierArray() const
{
	return m_piCapitalCommerceModifier;
}

int CvCivicInfo::getSpecialistExtraCommerce(int i) const
{
	FAssertBounds(0, NUM_COMMERCE_TYPES, i);
	return m_piSpecialistExtraCommerce ? m_piSpecialistExtraCommerce[i] : 0; // advc.003t
}

int* CvCivicInfo::getSpecialistExtraCommerceArray() const
{
	return m_piSpecialistExtraCommerce;
}

int CvCivicInfo::getBuildingHappinessChanges(int i) const
{
	FAssertBounds(0, GC.getNumBuildingClassInfos(), i);
	return m_paiBuildingHappinessChanges ? m_paiBuildingHappinessChanges[i] : 0; // advc.003t
}

int CvCivicInfo::getBuildingHealthChanges(int i) const
{
	FAssertBounds(0, GC.getNumBuildingClassInfos(), i);
	return m_paiBuildingHealthChanges ? m_paiBuildingHealthChanges[i] : 0; // advc.003t
}

int CvCivicInfo::getFeatureHappinessChanges(int i) const
{
	FAssertBounds(0, GC.getNumFeatureInfos(), i);
	return m_paiFeatureHappinessChanges ? m_paiFeatureHappinessChanges[i] : 0; // advc.003t
}

bool CvCivicInfo::isHurry(int i) const
{
	FAssertBounds(0, GC.getNumHurryInfos(), i);
	return m_pabHurry ? m_pabHurry[i] : false;
}

bool CvCivicInfo::isSpecialBuildingNotRequired(int i) const
{
	FAssertBounds(0, GC.getNumSpecialBuildingInfos(), i);
	return m_pabSpecialBuildingNotRequired ? m_pabSpecialBuildingNotRequired[i] : false;
}

bool CvCivicInfo::isSpecialistValid(int i) const
{
	FAssertBounds(0, GC.getNumSpecialistInfos(), i);
	return m_pabSpecialistValid ? m_pabSpecialistValid[i] : false;
}

int CvCivicInfo::getImprovementYieldChanges(int i, int j) const
{
	FAssertBounds(0, GC.getNumImprovementInfos(), i);
	FAssertBounds(0, NUM_YIELD_TYPES, j);
	return m_ppiImprovementYieldChanges[i][j];
}
#if SERIALIZE_CVINFOS
void CvCivicInfo::read(FDataStreamBase* stream)
{
	CvInfoBase::read(stream);
	uint uiFlag=0;
	stream->Read(&uiFlag);

	stream->Read(&m_iCivicOptionType);
	stream->Read(&m_iAnarchyLength);
	stream->Read(&m_iUpkeep);
	stream->Read(&m_iAIWeight);
	stream->Read(&m_iGreatPeopleRateModifier);
	stream->Read(&m_iGreatGeneralRateModifier);
	stream->Read(&m_iDomesticGreatGeneralRateModifier);
	stream->Read(&m_iStateReligionGreatPeopleRateModifier);
	stream->Read(&m_iDistanceMaintenanceModifier);
	stream->Read(&m_iNumCitiesMaintenanceModifier);
	stream->Read(&m_iCorporationMaintenanceModifier);
	stream->Read(&m_iExtraHealth);
	if (uiFlag >= 2)
		stream->Read(&m_iExtraHappiness);
	stream->Read(&m_iFreeExperience);
	stream->Read(&m_iWorkerSpeedModifier);
	stream->Read(&m_iImprovementUpgradeRateModifier);
	stream->Read(&m_iMilitaryProductionModifier);
	stream->Read(&m_iBaseFreeUnits);
	stream->Read(&m_iBaseFreeMilitaryUnits);
	stream->Read(&m_iFreeUnitsPopulationPercent);
	stream->Read(&m_iFreeMilitaryUnitsPopulationPercent);
	stream->Read(&m_iGoldPerUnit);
	stream->Read(&m_iGoldPerMilitaryUnit);
	if (uiFlag < 1)
	{
		m_iGoldPerUnit *= 100;
		m_iGoldPerMilitaryUnit *= 100;
	}
	stream->Read(&m_iHappyPerMilitaryUnit);
	// <advc.912c>
	if(uiFlag >= 3)
		stream->Read(&m_iLuxuryModifier); // </advc.912c>
	stream->Read(&m_iLargestCityHappiness);
	stream->Read(&m_iWarWearinessModifier);
	stream->Read(&m_iFreeSpecialist);
	stream->Read(&m_iTradeRoutes);
	stream->Read(&m_iTechPrereq);
	stream->Read(&m_iCivicPercentAnger);
	stream->Read(&m_iMaxConscript);
	stream->Read(&m_iStateReligionHappiness);
	stream->Read(&m_iNonStateReligionHappiness);
	stream->Read(&m_iStateReligionUnitProductionModifier);
	stream->Read(&m_iStateReligionBuildingProductionModifier);
	stream->Read(&m_iStateReligionFreeExperience);
	stream->Read(&m_iExpInBorderModifier);
	stream->Read(&m_bMilitaryFoodProduction);
	//stream->Read(&m_bNoUnhealthyPopulation);
	stream->Read(&m_iUnhealthyPopulationModifier); // K-Mod
	stream->Read(&m_bBuildingOnlyHealthy);
	stream->Read(&m_bNoForeignTrade);
	stream->Read(&m_bNoCorporations);
	stream->Read(&m_bNoForeignCorporations);
	stream->Read(&m_bStateReligion);
	stream->Read(&m_bNoNonStateReligionSpread);
	SAFE_DELETE_ARRAY(m_piYieldModifier);
	m_piYieldModifier = new int[NUM_YIELD_TYPES];
	stream->Read(NUM_YIELD_TYPES, m_piYieldModifier);
	SAFE_DELETE_ARRAY(m_piCapitalYieldModifier);
	m_piCapitalYieldModifier = new int[NUM_YIELD_TYPES];
	stream->Read(NUM_YIELD_TYPES, m_piCapitalYieldModifier);
	SAFE_DELETE_ARRAY(m_piTradeYieldModifier);
	m_piTradeYieldModifier = new int[NUM_YIELD_TYPES];
	stream->Read(NUM_YIELD_TYPES, m_piTradeYieldModifier);
	SAFE_DELETE_ARRAY(m_piCommerceModifier);
	m_piCommerceModifier = new int[NUM_COMMERCE_TYPES];
	stream->Read(NUM_COMMERCE_TYPES, m_piCommerceModifier);
	SAFE_DELETE_ARRAY(m_piCapitalCommerceModifier);
	m_piCapitalCommerceModifier = new int[NUM_COMMERCE_TYPES];
	stream->Read(NUM_COMMERCE_TYPES, m_piCapitalCommerceModifier);
	SAFE_DELETE_ARRAY(m_piSpecialistExtraCommerce);
	m_piSpecialistExtraCommerce = new int[NUM_COMMERCE_TYPES];
	stream->Read(NUM_COMMERCE_TYPES, m_piSpecialistExtraCommerce);
	SAFE_DELETE_ARRAY(m_paiBuildingHappinessChanges);
	m_paiBuildingHappinessChanges = new int[GC.getNumBuildingClassInfos()];
	stream->Read(GC.getNumBuildingClassInfos(), m_paiBuildingHappinessChanges);
	SAFE_DELETE_ARRAY(m_paiBuildingHealthChanges);
	m_paiBuildingHealthChanges = new int[GC.getNumBuildingClassInfos()];
	stream->Read(GC.getNumBuildingClassInfos(), m_paiBuildingHealthChanges);
	SAFE_DELETE_ARRAY(m_paiFeatureHappinessChanges);
	m_paiFeatureHappinessChanges = new int[GC.getNumFeatureInfos()];
	stream->Read(GC.getNumFeatureInfos(), m_paiFeatureHappinessChanges);
	SAFE_DELETE_ARRAY(m_pabHurry);
	m_pabHurry = new bool[GC.getNumHurryInfos()];
	stream->Read(GC.getNumHurryInfos(), m_pabHurry);
	SAFE_DELETE_ARRAY(m_pabSpecialBuildingNotRequired);
	m_pabSpecialBuildingNotRequired = new bool[GC.getNumSpecialBuildingInfos()];
	stream->Read(GC.getNumSpecialBuildingInfos(), m_pabSpecialBuildingNotRequired);
	SAFE_DELETE_ARRAY(m_pabSpecialistValid);
	m_pabSpecialistValid = new bool[GC.getNumSpecialistInfos()];
	stream->Read(GC.getNumSpecialistInfos(), m_pabSpecialistValid);
	if (m_ppiImprovementYieldChanges != NULL)
	{
		for(int i = 0; i < GC.getNumImprovementInfos(); i++)
			SAFE_DELETE_ARRAY(m_ppiImprovementYieldChanges[i]);
		SAFE_DELETE_ARRAY(m_ppiImprovementYieldChanges);
	}
	m_ppiImprovementYieldChanges = new int*[GC.getNumImprovementInfos()];
	for(int i = 0;i < GC.getNumImprovementInfos(); i++)
	{
		m_ppiImprovementYieldChanges[i]  = new int[NUM_YIELD_TYPES];
		stream->Read(NUM_YIELD_TYPES, m_ppiImprovementYieldChanges[i]);
	}
	stream->ReadString(m_szWeLoveTheKingKey);
}

void CvCivicInfo::write(FDataStreamBase* stream)
{
	CvInfoBase::write(stream);
	uint uiFlag=2;
	uiFlag = 3; // advc.912c
	stream->Write(uiFlag);

	stream->Write(m_iCivicOptionType);
	stream->Write(m_iAnarchyLength);
	stream->Write(m_iUpkeep);
	stream->Write(m_iAIWeight);
	stream->Write(m_iGreatPeopleRateModifier);
	stream->Write(m_iGreatGeneralRateModifier);
	stream->Write(m_iDomesticGreatGeneralRateModifier);
	stream->Write(m_iStateReligionGreatPeopleRateModifier);
	stream->Write(m_iDistanceMaintenanceModifier);
	stream->Write(m_iNumCitiesMaintenanceModifier);
	stream->Write(m_iCorporationMaintenanceModifier);
	stream->Write(m_iExtraHealth);
	stream->Write(m_iExtraHappiness); // K-Mod, uiFlag >= 2
	stream->Write(m_iFreeExperience);
	stream->Write(m_iWorkerSpeedModifier);
	stream->Write(m_iImprovementUpgradeRateModifier);
	stream->Write(m_iMilitaryProductionModifier);
	stream->Write(m_iBaseFreeUnits);
	stream->Write(m_iBaseFreeMilitaryUnits);
	stream->Write(m_iFreeUnitsPopulationPercent);
	stream->Write(m_iFreeMilitaryUnitsPopulationPercent);
	stream->Write(m_iGoldPerUnit);
	stream->Write(m_iGoldPerMilitaryUnit);
	stream->Write(m_iHappyPerMilitaryUnit);
	stream->Write(m_iLuxuryModifier); // advc.912c
	stream->Write(m_iLargestCityHappiness);
	stream->Write(m_iWarWearinessModifier);
	stream->Write(m_iFreeSpecialist);
	stream->Write(m_iTradeRoutes);
	stream->Write(m_iTechPrereq);
	stream->Write(m_iCivicPercentAnger);
	stream->Write(m_iMaxConscript);
	stream->Write(m_iStateReligionHappiness);
	stream->Write(m_iNonStateReligionHappiness);
	stream->Write(m_iStateReligionUnitProductionModifier);
	stream->Write(m_iStateReligionBuildingProductionModifier);
	stream->Write(m_iStateReligionFreeExperience);
	stream->Write(m_iExpInBorderModifier);
	stream->Write(m_bMilitaryFoodProduction);
	//stream->Write(m_bNoUnhealthyPopulation);
	stream->Write(m_iUnhealthyPopulationModifier); // K-Mod
	stream->Write(m_bBuildingOnlyHealthy);
	stream->Write(m_bNoForeignTrade);
	stream->Write(m_bNoCorporations);
	stream->Write(m_bNoForeignCorporations);
	stream->Write(m_bStateReligion);
	stream->Write(m_bNoNonStateReligionSpread);
	stream->Write(NUM_YIELD_TYPES, m_piYieldModifier);
	stream->Write(NUM_YIELD_TYPES, m_piCapitalYieldModifier);
	stream->Write(NUM_YIELD_TYPES, m_piTradeYieldModifier);
	stream->Write(NUM_COMMERCE_TYPES, m_piCommerceModifier);
	stream->Write(NUM_COMMERCE_TYPES, m_piCapitalCommerceModifier);
	stream->Write(NUM_COMMERCE_TYPES, m_piSpecialistExtraCommerce);
	stream->Write(GC.getNumBuildingClassInfos(), m_paiBuildingHappinessChanges);
	stream->Write(GC.getNumBuildingClassInfos(), m_paiBuildingHealthChanges);
	stream->Write(GC.getNumFeatureInfos(), m_paiFeatureHappinessChanges);
	stream->Write(GC.getNumHurryInfos(), m_pabHurry);
	stream->Write(GC.getNumSpecialBuildingInfos(), m_pabSpecialBuildingNotRequired);
	stream->Write(GC.getNumSpecialistInfos(), m_pabSpecialistValid);
	for(int i = 0;i < GC.getNumImprovementInfos(); i++)
		stream->Write(NUM_YIELD_TYPES, m_ppiImprovementYieldChanges[i]);
	stream->WriteString(m_szWeLoveTheKingKey);
}
#endif
bool CvCivicInfo::read(CvXMLLoadUtility* pXML)
{
	if (!CvInfoBase::read(pXML))
		return false;

	CvString szTextVal;

	pXML->GetChildXmlValByName(szTextVal, "CivicOptionType");
	m_iCivicOptionType = pXML->FindInInfoClass(szTextVal);

	pXML->GetChildXmlValByName(szTextVal, "TechPrereq");
	m_iTechPrereq = pXML->FindInInfoClass(szTextVal);

	pXML->GetChildXmlValByName(&m_iAnarchyLength, "iAnarchyLength");

	pXML->GetChildXmlValByName(szTextVal, "Upkeep");
	m_iUpkeep = pXML->FindInInfoClass(szTextVal);

	pXML->GetChildXmlValByName(&m_iAIWeight, "iAIWeight");
	pXML->GetChildXmlValByName(&m_iGreatPeopleRateModifier, "iGreatPeopleRateModifier");
	pXML->GetChildXmlValByName(&m_iGreatGeneralRateModifier, "iGreatGeneralRateModifier");
	pXML->GetChildXmlValByName(&m_iDomesticGreatGeneralRateModifier, "iDomesticGreatGeneralRateModifier");
	pXML->GetChildXmlValByName(&m_iStateReligionGreatPeopleRateModifier, "iStateReligionGreatPeopleRateModifier");
	pXML->GetChildXmlValByName(&m_iDistanceMaintenanceModifier, "iDistanceMaintenanceModifier");
	pXML->GetChildXmlValByName(&m_iNumCitiesMaintenanceModifier, "iNumCitiesMaintenanceModifier");
	pXML->GetChildXmlValByName(&m_iCorporationMaintenanceModifier, "iCorporationMaintenanceModifier");
	pXML->GetChildXmlValByName(&m_iExtraHealth, "iExtraHealth");
	pXML->GetChildXmlValByName(&m_iExtraHappiness, "iExtraHappiness"); // K-Mod
	pXML->GetChildXmlValByName(&m_iFreeExperience, "iFreeExperience");
	pXML->GetChildXmlValByName(&m_iWorkerSpeedModifier, "iWorkerSpeedModifier");
	pXML->GetChildXmlValByName(&m_iImprovementUpgradeRateModifier, "iImprovementUpgradeRateModifier");
	pXML->GetChildXmlValByName(&m_iMilitaryProductionModifier, "iMilitaryProductionModifier");
	pXML->GetChildXmlValByName(&m_iBaseFreeUnits, "iBaseFreeUnits");
	pXML->GetChildXmlValByName(&m_iBaseFreeMilitaryUnits, "iBaseFreeMilitaryUnits");
	pXML->GetChildXmlValByName(&m_iFreeUnitsPopulationPercent, "iFreeUnitsPopulationPercent");
	pXML->GetChildXmlValByName(&m_iFreeMilitaryUnitsPopulationPercent, "iFreeMilitaryUnitsPopulationPercent");
	pXML->GetChildXmlValByName(&m_iGoldPerUnit, "iGoldPerUnit");
	pXML->GetChildXmlValByName(&m_iGoldPerMilitaryUnit, "iGoldPerMilitaryUnit");
	pXML->GetChildXmlValByName(&m_iHappyPerMilitaryUnit, "iHappyPerMilitaryUnit");
	// advc.912c:
	pXML->GetChildXmlValByName(&m_iLuxuryModifier, "iLuxuryModifier");
	pXML->GetChildXmlValByName(&m_bMilitaryFoodProduction, "bMilitaryFoodProduction");
	pXML->GetChildXmlValByName(&m_iMaxConscript, "iMaxConscript");
	//pXML->GetChildXmlValByName(&m_bNoUnhealthyPopulation, "bNoUnhealthyPopulation");
	pXML->GetChildXmlValByName(&m_iUnhealthyPopulationModifier, "iUnhealthyPopulationModifier"); // K-Mod
	pXML->GetChildXmlValByName(&m_bBuildingOnlyHealthy, "bBuildingOnlyHealthy");
	pXML->GetChildXmlValByName(&m_iLargestCityHappiness, "iLargestCityHappiness");
	pXML->GetChildXmlValByName(&m_iWarWearinessModifier, "iWarWearinessModifier");
	pXML->GetChildXmlValByName(&m_iFreeSpecialist, "iFreeSpecialist");
	pXML->GetChildXmlValByName(&m_iTradeRoutes, "iTradeRoutes");
	pXML->GetChildXmlValByName(&m_bNoForeignTrade, "bNoForeignTrade");
	pXML->GetChildXmlValByName(&m_bNoCorporations, "bNoCorporations");
	pXML->GetChildXmlValByName(&m_bNoForeignCorporations, "bNoForeignCorporations");
	pXML->GetChildXmlValByName(&m_iCivicPercentAnger, "iCivicPercentAnger");
	pXML->GetChildXmlValByName(&m_bStateReligion, "bStateReligion");
	pXML->GetChildXmlValByName(&m_bNoNonStateReligionSpread, "bNoNonStateReligionSpread");
	pXML->GetChildXmlValByName(&m_iStateReligionHappiness, "iStateReligionHappiness");
	pXML->GetChildXmlValByName(&m_iNonStateReligionHappiness, "iNonStateReligionHappiness");
	pXML->GetChildXmlValByName(&m_iStateReligionUnitProductionModifier, "iStateReligionUnitProductionModifier");
	pXML->GetChildXmlValByName(&m_iStateReligionBuildingProductionModifier, "iStateReligionBuildingProductionModifier");
	pXML->GetChildXmlValByName(&m_iStateReligionFreeExperience, "iStateReligionFreeExperience");
	pXML->GetChildXmlValByName(&m_iExpInBorderModifier, "iExpInBorderModifier");

	if (gDLL->getXMLIFace()->SetToChildByTagName(pXML->GetXML(),"YieldModifiers"))
	{
		pXML->SetYields(&m_piYieldModifier);
		gDLL->getXMLIFace()->SetToParent(pXML->GetXML());
	}
	else pXML->InitList(&m_piYieldModifier, NUM_YIELD_TYPES);

	if (gDLL->getXMLIFace()->SetToChildByTagName(pXML->GetXML(),"CapitalYieldModifiers"))
	{
		pXML->SetYields(&m_piCapitalYieldModifier);
		gDLL->getXMLIFace()->SetToParent(pXML->GetXML());
	}
	else pXML->InitList(&m_piCapitalYieldModifier, NUM_YIELD_TYPES);

	if (gDLL->getXMLIFace()->SetToChildByTagName(pXML->GetXML(),"TradeYieldModifiers"))
	{
		pXML->SetYields(&m_piTradeYieldModifier);
		gDLL->getXMLIFace()->SetToParent(pXML->GetXML());
	}
	else pXML->InitList(&m_piTradeYieldModifier, NUM_YIELD_TYPES);

	if (gDLL->getXMLIFace()->SetToChildByTagName(pXML->GetXML(),"CommerceModifiers"))
	{
		pXML->SetCommerce(&m_piCommerceModifier);
		gDLL->getXMLIFace()->SetToParent(pXML->GetXML());
	}
	else pXML->InitList(&m_piCommerceModifier, NUM_COMMERCE_TYPES);

	if (gDLL->getXMLIFace()->SetToChildByTagName(pXML->GetXML(),"CapitalCommerceModifiers"))
	{
		pXML->SetCommerce(&m_piCapitalCommerceModifier);
		gDLL->getXMLIFace()->SetToParent(pXML->GetXML());
	}
	else pXML->InitList(&m_piCapitalCommerceModifier, NUM_COMMERCE_TYPES);

	if (gDLL->getXMLIFace()->SetToChildByTagName(pXML->GetXML(),"SpecialistExtraCommerces"))
	{
		pXML->SetCommerce(&m_piSpecialistExtraCommerce);
		gDLL->getXMLIFace()->SetToParent(pXML->GetXML());
	}
	else pXML->InitList(&m_piSpecialistExtraCommerce, NUM_COMMERCE_TYPES);

	pXML->SetVariableListTagPair(&m_pabHurry, "Hurrys", GC.getNumHurryInfos());
	pXML->SetVariableListTagPair(&m_pabSpecialBuildingNotRequired, "SpecialBuildingNotRequireds", GC.getNumSpecialBuildingInfos());
	pXML->SetVariableListTagPair(&m_pabSpecialistValid, "SpecialistValids", GC.getNumSpecialistInfos());

	pXML->SetVariableListTagPair(&m_paiBuildingHappinessChanges, "BuildingHappinessChanges", GC.getNumBuildingClassInfos());
	pXML->SetVariableListTagPair(&m_paiBuildingHealthChanges, "BuildingHealthChanges", GC.getNumBuildingClassInfos());
	pXML->SetVariableListTagPair(&m_paiFeatureHappinessChanges, "FeatureHappinessChanges", GC.getNumFeatureInfos());

	FAssert(GC.getNumImprovementInfos() > 0);
	pXML->Init2DIntList(&m_ppiImprovementYieldChanges, GC.getNumImprovementInfos(), NUM_YIELD_TYPES);
	if (gDLL->getXMLIFace()->SetToChildByTagName(pXML->GetXML(), "ImprovementYieldChanges"))
	{
		if (pXML->SkipToNextVal())
		{
			int iNumSibs = gDLL->getXMLIFace()->GetNumChildren(pXML->GetXML());
			if (gDLL->getXMLIFace()->SetToChild(pXML->GetXML()))
			{
				if (iNumSibs > 0)
				{
					for (int j = 0; j < iNumSibs; j++)
					{
						pXML->GetChildXmlValByName(szTextVal, "ImprovementType");
						int iIndex = pXML->FindInInfoClass(szTextVal);
						if (iIndex > -1)
						{
							SAFE_DELETE_ARRAY(m_ppiImprovementYieldChanges[iIndex]);
							if (gDLL->getXMLIFace()->SetToChildByTagName(pXML->GetXML(),"ImprovementYields"))
							{
								pXML->SetYields(&m_ppiImprovementYieldChanges[iIndex]);
								gDLL->getXMLIFace()->SetToParent(pXML->GetXML());
							}
							else pXML->InitList(&m_ppiImprovementYieldChanges[iIndex], NUM_YIELD_TYPES);
						}
						if (!gDLL->getXMLIFace()->NextSibling(pXML->GetXML()))
							break;
					}
				}
				gDLL->getXMLIFace()->SetToParent(pXML->GetXML());
			}
		}
		gDLL->getXMLIFace()->SetToParent(pXML->GetXML());
	}

	pXML->GetChildXmlValByName(szTextVal, "WeLoveTheKing");
	setWeLoveTheKingKey(szTextVal);

	return true;
}

CvCivicOptionInfo::CvCivicOptionInfo() : m_pabTraitNoUpkeep(NULL) {}

CvCivicOptionInfo::~CvCivicOptionInfo()
{
	SAFE_DELETE_ARRAY(m_pabTraitNoUpkeep);
}

bool CvCivicOptionInfo::getTraitNoUpkeep(int i) const
{
	FAssertBounds(0, GC.getNumTraitInfos(), i);
	return m_pabTraitNoUpkeep ? m_pabTraitNoUpkeep[i] : false;
}

bool CvCivicOptionInfo::read(CvXMLLoadUtility* pXML)
{
	if (!CvInfoBase::read(pXML))
		return false;

	pXML->SetVariableListTagPair(&m_pabTraitNoUpkeep, "TraitNoUpkeeps", GC.getNumTraitInfos());

	return true;
}

CvUpkeepInfo::CvUpkeepInfo() :
m_iPopulationPercent(0),
m_iCityPercent(0)
{}

int CvUpkeepInfo::getPopulationPercent() const
{
	return m_iPopulationPercent;
}

int CvUpkeepInfo::getCityPercent() const
{
	return m_iCityPercent;
}

bool CvUpkeepInfo::read(CvXMLLoadUtility* pXml)
{
	if (!CvInfoBase::read(pXml))
		return false;

	pXml->GetChildXmlValByName(&m_iPopulationPercent, "iPopulationPercent");
	pXml->GetChildXmlValByName(&m_iCityPercent, "iCityPercent");

	return true;
}
