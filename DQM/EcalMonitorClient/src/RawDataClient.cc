#include "../interface/RawDataClient.h"

#include "DQM/EcalCommon/interface/EcalDQMCommonUtils.h"
#include "DQM/EcalCommon/interface/FEFlags.h"

#include "CondFormats/EcalObjects/interface/EcalDQMStatusHelper.h"

#include "FWCore/ParameterSet/interface/ParameterSet.h"

#include <cmath>

namespace ecaldqm {

  RawDataClient::RawDataClient() :
    DQWorkerClient(),
    synchErrThresholdFactor_(0.)
  {
    qualitySummaries_.insert("QualitySummary");
  }

  void
  RawDataClient::setParams(edm::ParameterSet const& _params)
  {
    synchErrThresholdFactor_ = _params.getUntrackedParameter<double>("synchErrThresholdFactor");
  }

  void
  RawDataClient::producePlots(ProcessType)
  {
    MESet& meQualitySummary(MEs_.at("QualitySummary"));
    MESet& meErrorsSummary(MEs_.at("ErrorsSummary"));

    MESet const& sEntries(sources_.at("Entries"));
    MESet const& sL1ADCC(sources_.at("L1ADCC"));
    MESet const& sFEStatus(sources_.at("FEStatus"));
    //MESet const& sFEStatusErrMapByLumi(sources_.at("FEStatusErrMapByLumi"));

    uint32_t mask(1 << EcalDQMStatusHelper::STATUS_FLAG_ERROR);

    std::vector<int> dccStatus(nDCC, 1);

    for(unsigned iDCC(0); iDCC < nDCC; ++iDCC){
      double entries(sEntries.getBinContent(iDCC + 1));
      if(entries > 1. && sL1ADCC.getBinContent(iDCC + 1) > synchErrThresholdFactor_ * std::log(entries) / std::log(10.))
        dccStatus[iDCC] = 0;
    }

    MESet::iterator meEnd(meQualitySummary.end());
    float nFE_EBm06(0.);
    float nFE_EEm08(0.);
    float nFEerr_EBm06(0.);
    float nFEerr_EEm08(0.);
    for(MESet::iterator meItr(meQualitySummary.beginChannel()); meItr != meEnd; meItr.toNextChannel()){

      DetId id(meItr->getId());

      bool doMask(meQualitySummary.maskMatches(id, mask, statusManager_));

      int dccid(dccId(id));

      if(dccStatus[dccid - 1] == 0){
        meItr->setBinContent(doMask ? kMUnknown : kUnknown);
        continue;
      }

      int towerStatus(doMask ? kMGood : kGood);
      float towerEntries(0.);
      for(unsigned iS(0); iS < nFEFlags; iS++){
        float entries(sFEStatus.getBinContent(id, iS + 1));
        towerEntries += entries;
        if(entries > 0. &&
           iS != Enabled && iS != Suppressed &&
           iS != FIFOFull && iS != FIFOFullL1ADesync && iS != ForcedZS){
          towerStatus = doMask ? kMBad : kBad;
          if (dccid-1 == kEBm06) nFEerr_EBm06 += entries;
          if (dccid-1 == kEEm08) nFEerr_EEm08 += entries;
				}
      }

      if(towerEntries < 1.) towerStatus = doMask ? kMUnknown : kUnknown;

      //if (dccid-1 == kEBm06) std::cout << " >>LS" << timestamp_.iLumi << ": RawDataClient | " << smName(dccid) << " | towerStatus=" << towerStatus << " |  nFEerrRaw=" << sFEStatusErrMapByLumi.getBinContent(id) << std::endl;
      //if (dccid-1 == kEEm08) std::cout << " >>LS" << timestamp_.iLumi << ": RawDataClient | " << smName(dccid) << " | towerStatus=" << towerStatus << " |  nFEerrRaw=" << sFEStatusErrMapByLumi.getBinContent(id) << std::endl;
      if(towerStatus == kBad){
        if (dccid-1 == kEBm06) nFE_EBm06++;
        if (dccid-1 == kEEm08) nFE_EEm08++;
      }
      meItr->setBinContent(towerStatus);
      if(towerStatus == kBad) meErrorsSummary.fill(dccid);
    }
    for (unsigned iDCC(0);iDCC < nDCC;iDCC++){
      if (iDCC == kEBm06) std::cout << " >>LS" << timestamp_.iLumi << ": RawDataClient | " << smName(iDCC+1) << " | nBad=" << nFE_EBm06 << " | nFEerr= " << nFEerr_EBm06 << std::endl;
      if (iDCC == kEEm08) std::cout << " >>LS" << timestamp_.iLumi << ": RawDataClient | " << smName(iDCC+1) << " | nBad=" << nFE_EEm08 << " | nFEerr= " << nFEerr_EEm08 << std::endl;
    }
  }

  DEFINE_ECALDQM_WORKER(RawDataClient);
}

