#include <memory>

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Framework/interface/ESHandle.h"

#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/Common/interface/OwnVector.h"
#include "DataFormats/TrackCandidate/interface/TrackCandidateCollection.h"
#include "DataFormats/TrajectorySeed/interface/TrajectorySeedCollection.h"
#include "DataFormats/TrackerRecHit2D/interface/SiTrackerGSRecHit2DCollection.h" 
#include "DataFormats/TrackerRecHit2D/interface/SiTrackerGSMatchedRecHit2DCollection.h" 
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackReco/interface/TrackExtraFwd.h"

#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/Records/interface/TrackerTopologyRcd.h"

#include "FastSimulation/Tracking/interface/TrajectorySeedHitCandidate.h"
//#include "FastSimulation/Tracking/interface/TrackerRecHitSplit.h"

#include "FastSimulation/Tracking/plugins/TrackCandidateProducer.h"

#include <vector>
#include <map>

#include "TrackingTools/TrajectoryState/interface/TrajectoryStateTransform.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "MagneticField/Engine/interface/MagneticField.h"

#include "SimDataFormats/Track/interface/SimTrack.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateOnSurface.h"
#include "TrackingTools/TrajectoryState/interface/FreeTrajectoryState.h"
#include "TrackingTools/GeomPropagators/interface/Propagator.h"
#include "TrackingTools/Records/interface/TrackingComponentsRecord.h"

//Propagator withMaterial
#include "TrackingTools/MaterialEffects/interface/PropagatorWithMaterial.h"

TrackCandidateProducer::TrackCandidateProducer(const edm::ParameterSet& conf)
{  
  // products
  produces<TrackCandidateCollection>();
  
  // general parameters
  minNumberOfCrossedLayers = conf.getParameter<unsigned int>("MinNumberOfCrossedLayers");
  rejectOverlaps = conf.getParameter<bool>("OverlapCleaning");
  splitHits = conf.getParameter<bool>("SplitHits");

  // input tags, labels, tokens
  hitMasks_exists = conf.exists("hitMasks");
  if (hitMasks_exists){
    edm::InputTag hitMasksTag = conf.getParameter<edm::InputTag>("hitMasks");
    hitMasksToken = consumes<std::vector<bool> >(hitMasksTag);
  }

  edm::InputTag simTrackLabel = conf.getParameter<edm::InputTag>("simTracks");
  simVertexToken = consumes<edm::SimVertexContainer>(simTrackLabel);
  simTrackToken = consumes<edm::SimTrackContainer>(simTrackLabel);

  edm::InputTag seedLabel = conf.getParameter<edm::InputTag>("src");
  seedToken = consumes<edm::View<TrajectorySeed> >(seedLabel);

  edm::InputTag recHitLabel = conf.getParameter<edm::InputTag>("recHits");
  recHitToken = consumes<FastTMRecHitCombinations>(recHitLabel);
  
  propagatorLabel = conf.getParameter<std::string>("propagator");
}
  
void 
TrackCandidateProducer::produce(edm::Event& e, const edm::EventSetup& es) {        

  // get services
  edm::ESHandle<MagneticField>          magneticField;
  es.get<IdealMagneticFieldRecord>().get(magneticField);

  edm::ESHandle<TrackerGeometry>        trackerGeometry;
  es.get<TrackerDigiGeometryRecord>().get(trackerGeometry);

  edm::ESHandle<TrackerTopology>        trackerTopology;
  es.get<TrackerTopologyRcd>().get(trackerTopology);

  edm::ESHandle<Propagator>             propagator;
  es.get<TrackingComponentsRecord>().get(propagatorLabel,propagator);
  Propagator* thePropagator = propagator.product()->clone();

  // get products
  edm::Handle<edm::View<TrajectorySeed> > seeds;
  e.getByToken(seedToken,seeds);

  edm::Handle<FastTMRecHitCombinations> recHitCombinations;
  e.getByToken(recHitToken, recHitCombinations);

  edm::Handle<edm::SimVertexContainer> simVertices;
  e.getByToken(simVertexToken,simVertices);

  edm::Handle<edm::SimTrackContainer> simTracks;
  e.getByToken(simTrackToken,simTracks);

  std::auto_ptr<std::vector<bool> > hitMasks(new std::vector<bool>());

  // the hits to be skipped
  if (hitMasks_exists == true){
    edm::Handle<std::vector<bool> > hitMasks;
    e.getByToken(hitMasksToken,hitMasks);
  }
  
  // output collection
  std::auto_ptr<TrackCandidateCollection> output(new TrackCandidateCollection);    

  // loop over the seeds
  for (unsigned seednr = 0; seednr < seeds->size(); ++seednr){
    
    const BasicTrajectorySeed seed = seeds->at(seednr);
    if(seed.nHits()==0){
      edm::LogError("TrackCandidateProducer") << "empty trajectory seed in TrajectorySeedCollection" << std::endl;
      return;
    }

    // Get the combination of hits that produced the seed
    int32_t hitCombinationId =  ((const SiTrackerGSMatchedRecHit2D*) (&*(seed.recHits().first)))->hitCombinationId();
    const FastTMRecHitCombination & recHitCombination = recHitCombinations->at(hitCombinationId);

    // Count number of crossed layers, apply overlap rejection
    std::vector<TrajectorySeedHitCandidate> recHitCandidates;
    TrajectorySeedHitCandidate recHitCandidate;
    unsigned numberOfCrossedLayers = 0;      
    for (const auto & _hit : recHitCombination) {

      if(hitMasks_exists
	 && size_t(_hit.id()) < hitMasks->size() 
	 && hitMasks->at(_hit.id()))
	{
	  continue;
	}
      recHitCandidate = TrajectorySeedHitCandidate(&_hit,trackerGeometry.product(),trackerTopology.product());
      if ( recHitCandidates.size() == 0 || !recHitCandidate.isOnTheSameLayer(recHitCandidates.back()) ) {
	++numberOfCrossedLayers;
      }

      if( recHitCandidates.size() == 0 ||                                                // add the first seeding hit in any case
	  !rejectOverlaps ||                                                             // without overlap rejection:   add each hit
	  recHitCandidate.subDetId()    != recHitCandidates.back().subDetId() ||         // with overlap rejection:      only add if hits are not on the same layer
	  recHitCandidate.layerNumber() != recHitCandidates.back().layerNumber() ){
	recHitCandidates.push_back(recHitCandidate);
      }
      else if ( recHitCandidate.localError() < recHitCandidates.back().localError() ){
	recHitCandidates.back() = recHitCandidate;
      }
    }
    if ( numberOfCrossedLayers < minNumberOfCrossedLayers ) {
      continue;
    }

    // Convert TrajectorySeedHitCandidate to TrackingRecHit and split hits
    edm::OwnVector<TrackingRecHit> trackRecHits;
    for ( unsigned index = 0; index<recHitCandidates.size(); ++index ) {
      if(splitHits && recHitCandidates[index].matchedHit()->isMatched()){
	trackRecHits.push_back(recHitCandidates[index].matchedHit()->firstHit().clone());
	trackRecHits.push_back(recHitCandidates[index].matchedHit()->secondHit().clone());
      }
      else {
	trackRecHits.push_back(recHitCandidates[index].hit()->clone());
      }
    }
    // reverse order if needed
    // when is this relevant? perhaps for the cases when track finding goes backwards?
    if (seed.direction()==oppositeToMomentum){
      LogDebug("FastTracking")<<"reversing the order of the hits";
      std::reverse(recHitCandidates.begin(),recHitCandidates.end());
    }
    
    // create track candidate

    //Get seedTSOS from seed PTSOD//---------------------------------------------------------------------------
    DetId seedDetId(seed.startingState().detId());
    const GeomDet* gdet = trackerGeometry->idToDet(seedDetId);
    TrajectoryStateOnSurface seedTSOS = trajectoryStateTransform::transientState(seed.startingState(), &(gdet->surface()),magneticField.product());
    //---------------------------------------------------------------------------------------------------------
    //backPropagate seedState to front recHit and get a new initial TSOS at front recHit//---------------------
    const GeomDet* initialLayer = trackerGeometry->idToDet(trackRecHits.front().geographicalId());
    thePropagator->setPropagationDirection(oppositeToMomentum);
    const TrajectoryStateOnSurface initialTSOS = thePropagator->propagate(seedTSOS,initialLayer->surface()) ;
    //---------------------------------------------------------------------------------------------------------
    //Check if the TSOS is valid .
    if (!initialTSOS.isValid()) continue; 
    PTrajectoryStateOnDet PTSOD = trajectoryStateTransform::persistentState(initialTSOS,trackRecHits.front().geographicalId().rawId()); 
    TrackCandidate newTrackCandidate(trackRecHits,seed,PTSOD,edm::RefToBase<TrajectorySeed>(seeds,seednr));

    // add track candidate to output collection
    output->push_back(newTrackCandidate);
    
  }
  
  // Save the track candidates
  e.put(output);
}
