#include <iostream>
#include <cmath>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <algorithm>

// My descriptor
#include "tracker.hh"

// ROOT objects
#include "TCanvas.h"
#include "TGeoManager.h"
#include "TVirtualPad.h"
#include "TView.h"
#include "TFile.h"
#include "TPolyLine3D.h"
#include "TText.h"
#include "TFrame.h"
#include "TLegend.h"
#include "TStyle.h"

// Stuff to create directories
#include <sys/stat.h>
#include <sys/types.h>

// Date and time
#include <ctime> // for debug

// obsolete
double diffclock(clock_t clock1, clock_t clock2) {
    double diffticks=clock1-clock2;
    double diffms=(diffticks*1000)/CLOCKS_PER_SEC;
    return diffms;
}

// comparators
bool smallerRho(Layer* l1, Layer* l2) { return l1->getMinRho() < l2->getMinRho(); }
bool smallerZ(Layer* l1, Layer* l2) { return l1->getMinZ() < l2->getMinZ(); }

// Endcap Module sorting for vectors
// Returns true if m1 is "lower" than m2
bool moduleSortEndcapStyle(const Module* m1, const Module* m2) {
  XYZVector position[2];
  int radius_mm[2];
  int phi_deg[2];

  // Put position in a comfortable array
  position[0] = m1->getMeanPoint();
  position[1] = m2->getMeanPoint();

  // First sort on radius (mm precision)
  for (int i=0; i<2; ++i) {
    radius_mm[i] = int(position[i].Rho());
  }
  if (radius_mm[0]<radius_mm[1]) return true;
  if (radius_mm[0]>radius_mm[1]) return false;

  // If radius iis equal, then sort on phi (degree precision)
  for (int i=0; i<2; ++i) {
    phi_deg[i] = int(position[i].Phi()/M_PI*180);
  }
  if (phi_deg[0]<phi_deg[1]) return true;
  if (phi_deg[0]>phi_deg[1]) return false;

  // Otherwise use Z (double precision) to sort
  return (position[0].Z()<position[1].Z());
}


using namespace ROOT::Math;

Tracker::~Tracker()  {
    LayerVector::iterator layIt;
    for (layIt=layerSet_.begin(); layIt!=layerSet_.end(); layIt++) {
        if ((*layIt)!=NULL) {
            delete (*layIt);
        }
    }
    layerSet_.clear();
}

Tracker::Tracker() : MessageLogger("Tracker") {
    setDefaultParameters();
}

Tracker::Tracker(std::string trackerName) :  MessageLogger("Tracker") {
    setDefaultParameters();
    setName(trackerName);
}

void Tracker::setDefaultParameters() {
    nMB_ = defaultNMB_;
    zError_ = defaultZError_;
    smallDelta_ = defaultSmallDelta_;
    bigDelta_ = defaultBigDelta_;
    overlap_ = defaultOverlap_;
    storeDirectory_ = "store";
    summaryDirectory_ = "summaries";
    trackerName_ = "aTracker";
    arguments_= "";
    maxL_ = 0;
    maxR_ = 0;
    phiSegments_=4;
    lastPickedColor_ = STARTCOLOR; // remove (obsolete)
    colorPicker("pt"); // remove these three from here
    colorPicker("rphi");
    colorPicker("stereo");
}

void Tracker::shapeVolume() {
    // TODO:
    // Should build only the containing volume
}

void Tracker::shapeLayerVolumes() {
    // TODO:
    // Will build one volume per layer
}


void Tracker::createGeometry(bool lite /*= false*/ ) {
    
    if (!lite) {
        
        // Define the geometry
        myGeom_ = new TGeoManager("trackerGeometry", "Tracker geometry");
        // Define materials and media
        TGeoMaterial *matVacuum = new TGeoMaterial("Vacuum", 0, 0, 0);
        TGeoMaterial* matSi_ = new TGeoMaterial("Si", 26.98, 13, 2.7);
        TGeoMedium *Vacuum = new TGeoMedium("Vacuum", 1, matVacuum);
        myMed_ = new TGeoMedium("Si", 2, matSi_);
        // Define and set the top volume
        myVolume_ = myGeom_->MakeBox("TOP", Vacuum, 270., 270., 120.);
        myGeom_->SetTopVolume(myVolume_);
        // Reset the module counter
        iModule_=0;
        // DO THE THING
        shapeModuleVolumes(false);
        myGeom_->CloseGeometry();
        savingV_.push_back(myGeom_);
        
        
    } else {
        
        // ***************************************
        // *                                     *
        // * Everything here for the line drawer *
        // *                                     *
        // ***************************************
        
        // TODO: if these objects were already created they must be
        // deleted togherther with their references in the savingV_
        
        geomLite_ = new TCanvas("geometryLite", "Modules geometry", 800, 800);
        geomLite_->cd();
        shapeModuleVolumes(true);
        savingV_.push_back(geomLite_);
        
        geomLiteXY_ = new TCanvas("geometryLiteXY", "Modules geometry (XY Section)", 800, 800);
        geomLiteXY_->cd();
        shapeModuleVolumes(true, Layer::XYSection);
        savingV_.push_back(geomLiteXY_);
        
        geomLiteYZ_ = new TCanvas("geometryLiteYZ", "Modules geometry (YZ Section)", 800, 800);
        geomLiteYZ_->cd();
        shapeModuleVolumes(true, Layer::YZSection|Layer::Forward);
        savingV_.push_back(geomLiteYZ_);
        
        geomLiteEC_ = new TCanvas("geometryLiteEC", "Modules geometry (Endcap)", 800, 800);
        geomLiteEC_->cd();
        shapeModuleVolumesEndcapSample(true);
        savingV_.push_back(geomLiteEC_);
    }
}

void Tracker::shapeModuleVolumesEndcapSample(bool lite /* = false */) {
    // TODO:
    // Will build all the modules volumes / contours
    ModuleVector::iterator modIt;
    
    for (modIt=endcapSample_.begin(); modIt!=endcapSample_.end(); modIt++) {
        if (!lite) {
            placeModule(*modIt);
        } else {
            placeModuleLite(*modIt);
        }
    }
}


void Tracker::shapeModuleVolumes(bool lite /* = false */, int section /* = Layer::NoSection*/ ) {
    // TODO:
    // Will build all the modules volumes / contours
    LayerVector::iterator layIt;
    ModuleVector::iterator modIt;
    ModuleVector* aLay;
    bool placeThis;
    int realSection=section & (~Layer::Forward);
    int thisRealSection;
    
    
    for (layIt=layerSet_.begin(); layIt!=layerSet_.end(); layIt++) {
        aLay = (*layIt)->getModuleVector();
        for (modIt=aLay->begin(); modIt!=aLay->end(); modIt++) {
            thisRealSection = (*modIt)->getSection() & (~Layer::Forward);
            if ((realSection&thisRealSection)==(realSection)) {
                placeThis=true;
                if ((((section)&(Layer::Forward))!=0) && ( (*modIt)->getMeanPoint().Z()<0  )) placeThis=false;
                if (placeThis) {
                    if (!lite) {
                        placeModule(*modIt);
                    } else {
                        placeModuleLite(*modIt);
                    }
                }
            }
        }
    }
}

void Tracker::placeModule(Module* aModule) {
    std::string moduleName;
    char moduleNumber[100];
    sprintf(moduleNumber, "%d", iModule_++);
    moduleName = "mod_" + std::string(moduleNumber);
    aModule->setId(moduleName);
    aModule->shapeVolume(myVolume_, myMed_, myGeom_);
}

void Tracker::placeModuleLite(Module* aModule) {
    std::string moduleName;
    char moduleNumber[100];
    sprintf(moduleNumber, "%d", iModule_++);
    moduleName = "mod_" + std::string(moduleNumber);
    aModule->setId(moduleName);
    TPolyLine3D* contour = aModule->getContour();
    contour->Draw();
    
}

void Tracker::buildBarrel(int nLayer,
        double minRadius,
        double maxRadius,
        int nModules,
        BarrelModule* sampleModule,
        int section /* = NoSection */,
        bool compressed /* = false */) {
    
    buildBarrel(nLayer, minRadius, maxRadius, nModules, sampleModule, DEFAULTBARRELNAME, section, compressed);
    
}


// All the measures in mm as usual!
void Tracker::buildBarrel(int nLayer,
        double minRadius,
        double maxRadius,
        int nModules,
        BarrelModule* sampleModule,
        std::string barrelName,
        int section /* = NoSection */,
        bool compressed /* = false */,
        double minZ /* = 0 used to build mezzanine barrels */ ) {
    
    maxR_=(maxRadius>maxR_)?maxRadius:maxR_;
    
    int push;
    std::map<int, double>::iterator aDirective;
    std::map<int, LayerOption>::iterator anOption;
    
    LayerVector thisBarrelLayerSet;
    std::ostringstream layerName;
    BarrelLayer* aBarrelLayer;
    
    for (int i=0; i<nLayer; i++) {
        double radius = minRadius + (maxRadius-minRadius)/double(nLayer-1)*i;
        
        sampleModule->setLayer(i+1);
        
        if ((i==0)||(i==(nLayer-1))) {
            push = Layer::FIXED;
        } else {
            push = Layer::AUTO;
        }
        
        if ((i==0)||(i==(nLayer-1))) {
            push = Layer::FIXED;
        } else {
            push = Layer::AUTO;
        }
        
        aDirective = layerDirectives_.find(i+1);
        if (aDirective!=layerDirectives_.end()) {
            if  ((i==0)||(i==(nLayer-1))) {
	      addMessage("We just read a directive for the first or last layer. This will be ignored", WARNING);
                std::cout << "*******************************" << std::endl;
                std::cout << "*                             *" << std::endl;
                std::cout << "* WARNING:              /\\    *" << std::endl;
                std::cout << "*                      /!!\\   *" << std::endl;
                std::cout << "* We just read a      / !! \\  *" << std::endl;
                std::cout << "* directive for the   ^^^^^^  *" << std::endl;
                std::cout << "* first or last layer...      *" << std::endl;
                std::cout << "*                             *" << std::endl;
                std::cout << "*******************************" << std::endl;
            }
	    tempString.str(""); tempString << "Found a directive: " << layerDirectives_[i+1];
	    addMessage(tempString.str(), INFO);
            if (layerDirectives_[i+1]>0) {
                radius = layerDirectives_[i+1];
                push   = Layer::FIXED;
                tempString.str(""); tempString << "Fixing radius of layer " << i+1 << " to " << radius;
                addMessage(tempString.str(), INFO);
            } else {
                push = int(layerDirectives_[i+1]);
            }
        } else {
            addMessage("Found no directive: auto adjusting layer", INFO);
        }
        
        aBarrelLayer = new BarrelLayer(sampleModule);
        layerName.str("");
        layerName << "L" << std::dec << i+1;
        aBarrelLayer->setName(layerName.str());
	aBarrelLayer->setContainerName(barrelName);
       
        tempString.str(""); tempString << "Desired radius: " << radius;
        addMessage(tempString.str(), INFO);

        if (minZ==0) { // Standard Barrel
            aBarrelLayer->buildLayer(radius,       // averageRadius
                    getSmallDelta(i+1) ,
                    getBigDelta(i+1),
                    overlap_,     // overlap
                    zError_,      // safetyOrigin
                    nModules,     // maxZ
                    push,
                    phiSegments_, // modules multiple of ...
                    false,        // false = Strings with opposite parity
                    sampleModule, section);
            
            addLayer(aBarrelLayer, barrelName, TypeBarrel);
            thisBarrelLayerSet.push_back(aBarrelLayer);
        } else { // Mezzanine Barrel
            aBarrelLayer->buildLayer(radius,       // averageRadius
                    getSmallDelta(i+1) ,
                    getBigDelta(i+1),
                    overlap_,     // overlap
                    zError_,      // safetyOrigin
                    nModules,     // maxZ
                    push,
                    phiSegments_, // modules multiple of ...
                    false,        // false = Strings with opposite parity
                    sampleModule, section, minZ);
            
            addLayer(aBarrelLayer, barrelName, TypeBarrel);
            thisBarrelLayerSet.push_back(aBarrelLayer);
        }
        
        anOption = layerOptions_.find(i+1);
        if (anOption!=layerOptions_.end()) {
            BarrelLayer* anotherLayer;
            LayerOption myOption=layerOptions_[i+1];
            if (myOption.first==Layer::Stacked) {
                anotherLayer = new BarrelLayer(*aBarrelLayer);
                anotherLayer->shiftRho(myOption.second);
                addLayer(anotherLayer, barrelName, TypeBarrel);
                thisBarrelLayerSet.push_back(anotherLayer);
            }
        }
        
    }
    
    if (compressed) {
        compressBarrelLayers(thisBarrelLayerSet, (minZ!=0.));
    }
    
    
    // Mezzanine barrel needs to be duplicated and reflected
    if (minZ!=0.) {
        LayerVector::iterator layIt;
        BarrelLayer* anotherLayer;
        LayerVector justDoneBarrelLayerSet=thisBarrelLayerSet;
        
        for (layIt = justDoneBarrelLayerSet.begin(); layIt!= justDoneBarrelLayerSet.end(); layIt++) {
            if ( (aBarrelLayer=dynamic_cast<BarrelLayer*>(*layIt)) ) {
                anotherLayer = new BarrelLayer(*aBarrelLayer);
                anotherLayer->reflectZ();
                addLayer(anotherLayer, barrelName, TypeBarrel);
                thisBarrelLayerSet.push_back(anotherLayer);
            }
        }
    }
    
    double maxZ=getMaxBarrelZ(+1);
    maxL_=(maxZ>maxL_)?maxZ:maxL_;
    // TODO: update this value if you want an independent compacting of the barrel section
    
    LpB_.push_back(nLayer);
    rMinpB_.push_back(minRadius);
    rMaxpB_.push_back(maxRadius);
    dZpB_.push_back(maxZ);
}

// Barrel "compactification"
void Tracker::compressBarrelLayers(LayerVector aLayerSet, bool oneSided) {
    LayerVector::iterator layIt;
    BarrelLayer* aBarrelLayer;
    
    double minZm;
    double minZp;
    double aZp;
    double aZm;
    
    // Take the shortest barrel
    for (layIt = aLayerSet.begin(); layIt!= aLayerSet.end(); layIt++) {
        if ( (aBarrelLayer=dynamic_cast<BarrelLayer*>(*layIt)) ) {
            aZp = aBarrelLayer->getMaxZ(+1);
            aZm = aBarrelLayer->getMaxZ(-1);
            // std::cerr << "it's a barrel layer in the range " << aZm << ".." << aZp; // debug
            if (layIt==aLayerSet.begin()) {
                minZp=aZp;
                minZm=aZm;
            } else {
                if (aZm>minZm) {
                    minZm=aZm;
                }
                if (aZp<minZp) {
                    minZp=aZp;
                }
            }
        }
        // std::cerr << std::endl; //debug
    }
    
    // std::cerr << "Shortest layer on minus is " << minZm << std::endl; // debug
    // std::cerr << "Shortest layer on plus  is " << minZp << std::endl; // debug
    
    double minZt;
    double compactOrigin;
    if (!oneSided) { // Standard barrel compressing
        minZt = (minZp>minZm) ? minZp : minZm;
        compactOrigin=0.;
    } else { // Mezzanine barrel compressing
        // Compact to the value with higher fabs()
        // use the other one as zero reference
        if (fabs(minZp)>fabs(minZm)) {
            minZt = minZp;
            compactOrigin = minZm;
        } else {
            minZt = minZp;
            compactOrigin = minZm;
        }
    }
    
    // std::cerr << "compact origin : " << compactOrigin << std::endl; // debug
    // std::cerr << "compact to z   : " << minZt << std::endl; // debug
    
    for (layIt = aLayerSet.begin(); layIt!= aLayerSet.end(); layIt++) {
        if ( (aBarrelLayer=dynamic_cast<BarrelLayer*>(*layIt)) ) {
            if (!oneSided) { // Normal barrel
                aBarrelLayer->compressToZ(minZt);
            } else { // Mezzanine barrel
                aBarrelLayer->compressExceeding(minZt, compactOrigin);
            }
        } else {
            std::cerr << "ERROR: trying to compact a non-barrel layer" ;
        }
    }
}

void Tracker::alignShortBarrels() {
    if (barrelLayerSet_.size() > 1) {
        bool is_short, is_long;
        LayerVector::iterator iter = barrelLayerSet_.begin();
        LayerVector::iterator guard = barrelLayerSet_.end();
        LayerVector::iterator first_long = iter;
        // find first long layer for start and stop
        is_long = ((*first_long)->getMinZ() < 0) && ((*first_long)->getMaxZ() > 0);
        while (!is_long) {
            first_long++;
            if (first_long == guard) break;
            is_long = ((*first_long)->getMinZ() < 0) && ((*first_long)->getMaxZ() > 0);
        }
        while (iter != guard) {
            is_short = ((*iter)->getMaxZ() < 0) || ((*iter)->getMinZ() > 0);
            if (is_short) {
                bool change;
                LayerVector::iterator start;
                LayerVector::iterator stop;
                LayerVector::iterator cmp;
                start = first_long;
                stop = first_long;
                cmp = first_long;
                if (cmp != guard) cmp++;
                while (cmp != guard) {
                    is_long = ((*cmp)->getMinZ() < 0) && ((*cmp)->getMaxZ() > 0);
                    if (is_long) {
                        if ((*iter)->getMinRho() > (*cmp)->getMinRho()) {
                            change = ((*start)->getMinRho() > (*iter)->getMinRho()) || ((*cmp)->getMinRho() > (*start)->getMinRho());
                        }
                        else change = false;
                        if (change) start = cmp;
                    }
                    cmp++;
                }
                if ((start == first_long) && ((*start)->getMinRho() > (*iter)->getMinRho())) start = guard;
                cmp = stop;
                if (cmp != guard) cmp++;
                while (cmp != guard) {
                    is_long = ((*cmp)->getMinZ() < 0) && ((*cmp)->getMaxZ() > 0);
                    if (is_long) {
                        if ((*iter)->getMinRho() < (*cmp)->getMinRho()) {
                            change = ((*stop)->getMinRho() < (*iter)->getMinRho()) || ((*cmp)->getMinRho() < (*stop)->getMinRho());
                        }
                        else change = false;
                        if (change)  stop = cmp;
                    }
                    cmp++;
                }
                if ((stop == first_long) && ((*stop)->getMinRho() < (*iter)->getMinRho())) stop = guard;
                if ((stop == guard) && (start != guard)) {
                    if ((*iter)->getMinZ() > 0) { // right of the origin
                        XYZVector dz(0, 0, (*start)->getMaxZ() - (*iter)->getMaxZ());
                        (*iter)->translate(dz);
                    }
                    else { // left of the origin
                        XYZVector dz(0, 0, (*start)->getMinZ() - (*iter)->getMinZ());
                        (*iter)->translate(dz);
                    }
                }
                else if((start == guard) && (stop != guard)) {
                    if ((*iter)->getMinZ() > 0) { // right of the origin
                        if ((*stop)->getMaxZ() < (*iter)->getMaxZ()) {
                            XYZVector dz(0, 0, (*stop)->getMaxZ() - (*iter)->getMaxZ());
                            (*iter)->translate(dz);
                        }
                    }
                    else { // left of the origin
                        if ((*stop)->getMinZ() > (*iter)->getMinZ()) {
                            XYZVector dz(0, 0, (*stop)->getMinZ() - (*iter)->getMinZ());
                            (*iter)->translate(dz);
                        }
                    }
                }
                else if ((start != guard) && (stop != guard)) {
                    if ((*iter)->getMinZ() > 0) { // right of the origin
                        if ((*start)->getMaxZ() == (*stop)->getMaxZ()) {
                            XYZVector dz(0, 0, (*start)->getMaxZ() - (*iter)->getMaxZ());
                            (*iter)->translate(dz);
                        }
                        else {
                            double dist1, dist2;
                            XYZVector dz;
                            dist1 = (*start)->getMaxZ() - (*iter)->getMaxZ();
                            dist2 = (*stop)->getMaxZ() - (*iter)->getMaxZ();
                            if (fabs(dist1) < fabs(dist2)) dz.SetZ(dist1);
                            else dz.SetZ(dist2);
                            (*iter)->translate(dz);
                        }
                    }
                    else { // left of the origin
                        if ((*start)->getMinZ() == (*stop)->getMinZ()) {
                            XYZVector dz(0, 0, (*start)->getMinZ() - (*iter)->getMinZ());
                            (*iter)->translate(dz);
                        }
                        else {
                            double dist1, dist2;
                            XYZVector dz;
                            dist1 = (*start)->getMinZ() - (*iter)->getMinZ();
                            dist2 = (*stop)->getMinZ() - (*iter)->getMinZ();
                            if (fabs(dist1) < fabs(dist2)) dz.SetZ(dist1);
                            else dz.SetZ(dist2);
                            (*iter)->translate(dz);
                        }
                    }
                }
            }
            iter++;
        }
    }
}

void Tracker::sortLayers() {
    std::stable_sort(barrelLayerSet_.begin(), barrelLayerSet_.end(), smallerZ);
    std::stable_sort(barrelLayerSet_.begin(), barrelLayerSet_.end(), smallerRho);
    std::stable_sort(endcapLayerSet_.begin(), endcapLayerSet_.end(), smallerZ);
}

double Tracker::getMaxBarrelZ(int direction) {
    double maxZ;
    double aZ;
    LayerVector::iterator layIt;
    BarrelLayer* aBarrelLayer;
    
    if (direction==0) {
        std::cerr << "Tracker::getMaxBarrelZ was called with zero direction. Assuming +1" << std::endl;
        direction=+1;
    }
    direction/=int(fabs(direction));
    
    // Take the shortest barrel
    for (layIt = barrelLayerSet_.begin(); layIt!= barrelLayerSet_.end(); layIt++) {
        if ( (aBarrelLayer=dynamic_cast<BarrelLayer*>(*layIt)) ) {
            aZ = aBarrelLayer->getMaxZ(direction);
            if (layIt==barrelLayerSet_.begin()) {
                maxZ=aZ;
            } else {
                if (direction*aZ>direction*maxZ) {
                    maxZ=aZ;
                }
            }
        }
    }
    
    return maxZ;
}

void Tracker::buildEndcaps(int nDisks, double minZ, double maxZ, double minRadius, double maxRadius,
        EndcapModule* sampleModule, int diskParity, int sectioned /* = Layer::NoSection */ ) {
    buildEndcaps(nDisks, minZ, maxZ, minRadius, maxRadius, sampleModule, DEFAULTENDCAPNAME, diskParity,  sectioned);
}

void Tracker::buildEndcapsAtEta(int nDisks, double minZ, double maxZ, double maxEta, double maxRadius,
        EndcapModule* sampleModule, std::string endcapName, int diskParity,
        int sectioned /* = Layer::NoSection */ ) {
    
    double minTheta = 2*atan(exp(-1*maxEta));
    double minRadius = minZ * tan(minTheta);
    
    buildEndcaps(nDisks, minZ, maxZ, minRadius, maxRadius,
		 sampleModule, endcapName, diskParity, sectioned );
    
}


void Tracker::buildEndcaps(int nDisks, double minZ, double maxZ, double minRadius, double maxRadius,
        EndcapModule* sampleModule, std::string endcapName, int diskParity,
        int sectioned /* = Layer::NoSection */ ) {
    
    //EndcapModule* sampleModule = new EndcapModule(*genericSampleModule);
    
    maxR_=(maxRadius>maxR_)?maxRadius:maxR_;
    maxL_=(maxZ>maxL_)?maxZ:maxL_;
    
    double thisZ;
    double deltaZ;
    
    // Geometric progression factor
    double alpha = pow(maxZ/minZ, 1/double(nDisks-1));
    
    EndcapLayer* defaultDisk = new EndcapLayer();
    EndcapLayer* anotherDisk;
    
    defaultDisk->buildSingleDisk( minRadius, maxRadius, smallDelta_,
            bigDelta_, (minZ+maxZ)/2, overlap_,
            zError_+(maxZ-minZ)/2,
            phiSegments_, // Base
            sampleModule,
            ringDirectives_,
            diskParity,
            sectioned );
    
    
    std::ostringstream layerName;
    EndcapModule* anEndcapModule;
    
    for (int iDisk=0; iDisk<nDisks; iDisk++) {
        // Set the disk number in all the modules
        for (ModuleVector::iterator modIt = defaultDisk->getModuleVector()->begin();
        modIt!=defaultDisk->getModuleVector()->end();
        modIt++) {
            if ( (anEndcapModule=dynamic_cast<EndcapModule*>(*modIt)) ) {
                anEndcapModule->setDisk(iDisk+1);
            } else {
                std::cerr << "ERROR IN Tracker::buildEndcaps this shoundn't happen!" << std::endl;
            }
        }
        
        layerName.str("");
        layerName << "D" << std::dec << iDisk+1;
        thisZ = pow(alpha, iDisk) * minZ;
        deltaZ=-1*(minZ+maxZ)/2+thisZ;
        anotherDisk = new EndcapLayer(*defaultDisk);
        anotherDisk->setName(layerName.str());
	anotherDisk->setContainerName(endcapName);
        anotherDisk->translateZ(deltaZ);
        addLayer(anotherDisk, endcapName, TypeEndcap);
        anotherDisk = new EndcapLayer(*anotherDisk);
        anotherDisk->rotateY_PI();
        addLayer(anotherDisk, endcapName, TypeEndcap);
    }
    
    for (ModuleVector::iterator modIt = defaultDisk->getModuleVector()->begin();
    modIt!=defaultDisk->getModuleVector()->end();
    modIt++) {
        endcapSample_.push_back(*modIt);
    }
    
    // TODO: decide how to handle this
    // delete defaultDisk;
    
    DpE_.push_back(nDisks);
    rMinpE_.push_back(minRadius);
    rMaxpE_.push_back(maxRadius);
    dZpE_.push_back((maxZ - minZ) / 2.0);
}

// Function used to remove some endcaps rings
// sectionName: name of the endcap to operate onto
// iDisk: number of the disk onto which operate
// iRing: number of the first ring to remove
// directionOuter: if it's true we must remove all the rings outer than iRing
// while if it's false we remove all the rings inner that iRing
void Tracker::removeDiskRings(std::string sectionName, int iDisk, int iRing, bool directionOuter) {
    LayerVector::iterator layIt;
    ModuleVector::iterator modIt;
    ModuleVector::iterator anotherModIt;
    ModuleVector* aLay;
    Module* aModule;
    EndcapModule* anEndcapModule;
    
    // Take the vector of layers in the section
    LayerVector myLayers = sectionMap_[sectionName];
    
    for (layIt=myLayers.begin(); layIt!=myLayers.end(); layIt++) {
        aLay = (*layIt)->getModuleVector();
        for (modIt=aLay->begin(); modIt!=aLay->end(); modIt++) {
            aModule=(*modIt);
            
            if ( (anEndcapModule=dynamic_cast<EndcapModule*>(aModule)) ) {
                if (directionOuter) {
                    if ((anEndcapModule->getDisk()==iDisk)
                            && (anEndcapModule->getRing()>=iRing)) {
                        delete aModule;
                        anotherModIt=modIt-1;
                        aLay->erase(modIt);
                        modIt=anotherModIt;
                        (*layIt)->decreaseModCount(anEndcapModule->getRing() - 1);
                    }
                } else {
                    if ((anEndcapModule->getDisk()==iDisk)
                            && (anEndcapModule->getRing()<=iRing)) {
                        delete aModule;
                        anotherModIt=modIt-1;
                        aLay->erase(modIt);
                        modIt=anotherModIt;
                        (*layIt)->decreaseModCount(anEndcapModule->getRing() - 1);
                    }
                }
            } else if (dynamic_cast<BarrelModule*>(aModule)) {
                // ERROR: this should not happen
                std::cerr << "ERROR: a barrel module was found in section " << sectionName
                << " while we are trying to remove rings from there. It should be an endcap module!" << std::endl;
            }
        }
    }
}

// *******************************
// *                             *
// * Geometry analysis functions *
// *                             *
// *******************************

std::pair<double, double> Tracker::getEtaMinMax() {
    std::pair<double, double> result;
    LayerVector::iterator layIt;
    ModuleVector* moduleV;
    ModuleVector::iterator modIt;
    
    double theta;
    double minTheta=M_PI+1; // (!) :-)
    double maxTheta=-1;     // idem...
    
    for (layIt=layerSet_.begin(); layIt!=layerSet_.end(); layIt++) {
        moduleV = (*layIt)->getModuleVector();
        for (modIt=moduleV->begin(); modIt!=moduleV->end(); modIt++) {
            theta=(*modIt)->getMinTheta();
            if (theta<minTheta) minTheta=theta;
            theta=(*modIt)->getMaxTheta();
            if (theta>maxTheta) maxTheta=theta;
        }
    }
    
    result.first = -1*log(tan(maxTheta/2.));
    result.second = -1*log(tan(minTheta/2.));
    
    return result;
}

int Tracker::cutOverEta(double etaCut) {
    int nCut = 0;
    LayerVector::iterator layIt;
    ModuleVector::iterator modIt;
    
    for (layIt=layerSet_.begin(); layIt!=layerSet_.end(); layIt++) {
        nCut += (*layIt)->cutOverEta(etaCut);
    }
    
    return nCut;
}


ModuleVector Tracker::trackHit(const XYZVector& origin, const XYZVector& direction, ModuleVector* moduleV) {
    ModuleVector result;
    ModuleVector::iterator modIt;
    double distance;
    
    for (modIt=moduleV->begin(); modIt!=moduleV->end(); modIt++) {
        // A module can be hit if it fits the phi (precise) contraints
        // and the eta constaints (taken assuming origin within 5 sigma)
        if ((*modIt)->couldHit(direction.Eta(), direction.Phi())) {
          distance=(*modIt)->trackCross(origin, direction);
          if (distance>0) {
              result.push_back(*modIt);
          }
        }
    }
    
    return result;
}



// Resets a module type counter
void Tracker::resetTypeCounter(std::map <std::string, int> &modTypes) {
    for (std::map <std::string, int>::iterator it = modTypes.begin();
    it!=modTypes.end(); it++) {
        (*it).second = 0;
    }
}


// Shoots directions with random (flat) phi, random (flat) pseudorapidity
// gives also the direction's eta
std::pair <XYZVector, double > Tracker::shootFixedDirection(double phi, double eta) {
    std::pair <XYZVector, double> result;
    
    double theta=2*atan(exp(-1*eta));
    
    // Direction
    result.first  = XYZVector(cos(phi)*sin(theta), sin(phi)*sin(theta), cos(theta));
    result.second = eta;
    return result;
}

// Shoots directions with random (flat) phi, random (flat) pseudorapidity
// gives also the direction's eta
std::pair <XYZVector, double > Tracker::shootDirection(double minEta, double spanEta) {
    std::pair <XYZVector, double> result;
    
    double eta;
    double phi;
    double theta;
    
    // phi is random [0, 2pi)
    phi = myDice_.Rndm() * 2 * M_PI; // debug
    
    // eta is random (-4, 4]
    eta = myDice_.Rndm() * spanEta + minEta;
    theta=2*atan(exp(-1*eta));
    
    // Direction
    result.first  = XYZVector(cos(phi)*sin(theta), sin(phi)*sin(theta), cos(theta));
    result.second = eta;
    return result;
}

// Shoots directions with random (flat) phi, random (flat) pseudorapidity
// gives also the direction's eta
std::pair <XYZVector, double > Tracker::shootDirectionFixedPhi(double minEta, double spanEta) {
    std::pair <XYZVector, double> result;
    
    double eta;
    double theta;
    
    // eta is random (-4, 4]
    eta = myDice_.Rndm() * spanEta + minEta;
    theta=2*atan(exp(-1*eta));
    
    // Direction
    result.first  = XYZVector(0, sin(theta), cos(theta));
    result.second = eta;
    return result;
}

// won't fix messages here: it is obsolete anyway
void Tracker::analyze(int nTracks /*=1000*/ , int section /* = Layer::NoSection */ ) {
    // A bunch of pointers
    std::map <std::string, int> modTypes;
    std::map <std::string, TH2D*> etaType;
    TH2D* aPlot;
    TH1D* hitDistribution;
    std::string aType;
    
    // Optimize the track creation on the real tracker
    std::pair <double, double> etaMinMax = getEtaMinMax();
    double absMinEta = fabs(etaMinMax.first);
    double absMaxEta = fabs(etaMinMax.second);
    double maxEta = (absMinEta>absMaxEta) ? absMinEta : absMaxEta;
    
    // Computing the margin
    double randomPercentMargin = 0.1;
    double randomSpan = (etaMinMax.second - etaMinMax.first)*(1. + randomPercentMargin);
    double randomBase = etaMinMax.first - (etaMinMax.second - etaMinMax.first)*(randomPercentMargin)/2.;
    
    // Initialize random number generator, counters and histograms
    myDice_.SetSeed(RANDOM_SEED);
    createResetCounters(modTypes);
    for (std::map <std::string, int>::iterator it = modTypes.begin();
    it!=modTypes.end(); it++) {
        std::cerr << "creating plot " << (*it).first << std::endl;
        aPlot = new TH2D( (*it).first.c_str(), (*it).first.c_str(),
                100,
                0.,
                maxEta*1.1,
                1000,
                0., 10.);
        etaType[(*it).first]=aPlot;
    }
    TH2D* total2D = new TH2D( "total2d", "total2d",
            100,
            0.,
            maxEta*1.2,
            1000,
            0., 10.);

    LayerVector::iterator layIt;
    ModuleVector* moduleV;
    ModuleVector properModules;
    ModuleVector::iterator modIt;
    ModuleVector allModules;
    
    // Build the proper list of modules
    for (layIt=layerSet_.begin(); layIt!=layerSet_.end(); layIt++) {
        moduleV = (*layIt)->getModuleVector();
        for (modIt=moduleV->begin(); modIt!=moduleV->end(); modIt++) {
            (*modIt)->computeBoundaries(zError_); // only for smart hit-finding method
            if (section==Layer::NoSection) {
                properModules.push_back(*modIt);
            } else {
                if (((*modIt)->getSection()&section)==section) {
                    properModules.push_back(*modIt);
                }
            }
            allModules.push_back(*modIt);
        }
    }
    
    // The real simulation
    std::pair <XYZVector, double> aLine;
    ModuleVector hitMods;
    
    struct tm *localt; // timing: debug
    time_t t;          // timing: debug
    
    t = time(NULL);
    localt = localtime(&t);
    std::cout << asctime(localt) << std::endl;
    clock_t starttime = clock();
    std::cout << "Shooting tracks: ";
    int nTrackHits; // mersi mod
    
    if (section==Layer::YZSection) {
      for (int i=0; i<nTracks; i++) {
        nTrackHits=0; // mersi mod
        if (i%100==0) std::cout << "." ;
        aLine=shootDirectionFixedPhi(randomBase, randomSpan);
	
        hitMods = trackHit( XYZVector(0, 0, myDice_.Gaus(0, zError_)), aLine.first, &properModules);
        resetTypeCounter(modTypes);
        for (ModuleVector::iterator it = hitMods.begin();
        it!=hitMods.end(); it++) {
            modTypes[(*it)->getType()]++; // mersi mod
            nTrackHits++; // mersi mod
        }
        for (std::map <std::string, int>::iterator it = modTypes.begin();
        it!=modTypes.end(); it++) {
            etaType[(*it).first]->Fill(fabs(aLine.second), (*it).second);
        }
        total2D->Fill(fabs(aLine.second), hitMods.size()); // mersi mod : was using hitMods.size() in place of nTrackHits
      }
    } else {
      int nTracksPerSide = int(pow(nTracks, 0.5));
      int nBlocks = int(nTracksPerSide/2.);
      nTracks = nTracksPerSide*nTracksPerSide;
      TH2D* mapPhiEta = new TH2D( "mapPhiEta", "Number of hits;phi;eta", nBlocks, -1*M_PI, M_PI, nBlocks, -3., 3.);
      TH2I* mapPhiEtaCount = new TH2I( "mapPhiEtaCount ", "phi Eta hit count", nBlocks, -1*M_PI, M_PI, nBlocks, -3., 3.);
    

      for (int i=0; i<nTracksPerSide; i++) {
	for (int j=0; j<nTracksPerSide; j++) {
	  nTrackHits=0; // mersi mod
	  //aLine = shootFixedDirection(2*M_PI*(i+.5)/double(nTracksPerSide), randomBase + (randomSpan*j)/double(nTracksPerSide) );
	  aLine = shootDirection(randomBase, randomSpan);
	    
	  hitMods = trackHit( XYZVector(0, 0, myDice_.Gaus(0, zError_)), aLine.first, &properModules);
	  resetTypeCounter(modTypes);
	  for (ModuleVector::iterator it = hitMods.begin(); it!=hitMods.end(); it++) {
            modTypes[(*it)->getType()]++; // mersi mod
            nTrackHits++; // mersi mod
	  }

	  for (std::map <std::string, int>::iterator it = modTypes.begin(); it!=modTypes.end(); it++) {
            etaType[(*it).first]->Fill(fabs(aLine.second), (*it).second);
	  }
	  total2D->Fill(fabs(aLine.second), hitMods.size()); // mersi mod : was using hitMods.size() in place of nTrackHits
	  mapPhiEta->Fill(aLine.first.Phi(), aLine.second, hitMods.size()); // phi, eta 2d plot
	  mapPhiEtaCount->Fill(aLine.first.Phi(), aLine.second); // how many traks where shot
	}
      }
      double hitCount;
      int trackCount;
      for (int nx=0; nx<=mapPhiEtaCount->GetNbinsX()+1; nx++) {
	for (int ny=0; ny<=mapPhiEtaCount->GetNbinsY()+1; ny++) {
	  trackCount=mapPhiEtaCount->GetBinContent(nx, ny);
	  if (trackCount>0) {
	    hitCount=mapPhiEta->GetBinContent(nx, ny);
	    mapPhiEta->SetBinContent(nx, ny, hitCount/trackCount);
	  }
	}
      }
      delete mapPhiEtaCount;
      mapPhiEta_=mapPhiEta;
      savingV_.push_back(mapPhiEta);
    }
      

    std::cout << " done!" << std::endl;
    t = time(NULL);
    localt = localtime(&t);
    clock_t endtime = clock();
    std::cout << asctime(localt) << std::endl;
    std::cout << "Elapsed time: " << diffclock(endtime, starttime)/1000. << "s" << std::endl;
    t = time(NULL);
    localt = localtime(&t);
    std::cout << asctime(localt) << std::endl;
   
    TProfile *myProf;
    etaProfileCanvas_ = new TCanvas("etaProfileCanvas", "Eta Profiles", 800, 800);
    etaProfileCanvas_->cd();
    savingV_.push_back(etaProfileCanvas_);
    int plotCount=0;
    
    //for (std::map <std::string, TH2D*>::iterator it = etaType.begin();
    //    it!=etaType.end(); it++) {
    //        (*it).second->Clone();
    //    }
    TProfile* total = total2D->ProfileX("etaProfileTotal");
    savingV_.push_back(total);
    std::cout << plotCount << ": " << total->GetMaximum() << std::endl;
    total->SetMarkerStyle(8);
    total->SetMarkerColor(1);
    total->SetMarkerSize(1.5);
    total->SetTitle("Number of hit modules");
    if (total->GetMaximum()<9) total->SetMaximum(9.);
    total->Draw();
    std::string profileName;
    for (std::map <std::string, TH2D*>::iterator it = etaType.begin();
    it!=etaType.end(); it++) {
        plotCount++;
        myProf=(*it).second->ProfileX();
        savingV_.push_back(myProf);
        std::cout << plotCount << ": " << myProf->GetMaximum() << std::endl;
        myProf->SetMarkerStyle(8);
        myProf->SetMarkerColor(colorPicker((*it).first));
        myProf->SetMarkerSize(1);
        myProf->SetName((*it).first.c_str());
        profileName = "etaProfile-"+(*it).first;
        myProf->SetTitle((*it).first.c_str());
        myProf->Draw("same");
    }
    
    // Record the fraction of hits per module
    // hitDistribution = new TH1D( "hitDistribution", "Hit distribution", nTracks, -0.5, double(nTracks)-0.5 );
    hitDistribution = new TH1D( "hitDistribution", "Hit distribution", nTracks, 0 , 1);
    savingV_.push_back(hitDistribution);
    
    for (modIt=moduleV->begin(); modIt!=moduleV->end(); modIt++) {
        hitDistribution->Fill((*modIt)->getNHits()/double(nTracks));
    }
    
    return;
}

void Tracker::writeSummary(std::string fileType /* = "html" */) {
    std::string nullString("");
    writeSummary(false, nullString, nullString, fileType);
}

void Tracker::writeSummary(bool configFiles,
			   std::string configFile,
			   std::string dressFile, std::string fileType /*= "html"*/,
			   std::string barrelModuleCoordinatesFile /*=""*/,
			   std::string endcapModuleCoordinatesFile /*=""*/) {
    
    // Just to start with
    createDirectories();
    
    // Formatting parameters
    int coordPrecision = 0;
    int areaPrecision = 1;
    int occupancyPrecision = 1;
    int pitchPrecision = 0;
    int stripLengthPrecision = 1;
    int millionChannelPrecision = 2;
    int powerPrecision = 1;
    int costPrecision  = 1;
    int powerPerUnitPrecision = 2;
    int costPerUnitPrecision  = 1;
    
    // A bunch of indexes
    std::map<std::string, Module*> typeMap;
    std::map<std::string, int> typeMapCount;
    std::map<std::string, long> typeMapCountChan;
    std::map<std::string, double> typeMapMaxOccupancy;
    std::map<std::string, double> typeMapAveOccupancy;
    std::map<std::string, Module*>::iterator typeMapIt;
    std::map<int, Module*> ringTypeMap;
    std::string aSensorTag;
    LayerVector::iterator layIt;
    ModuleVector::iterator modIt;
    ModuleVector* aLay;
    double totAreaPts=0;
    double totAreaStrips=0;
    int totCountMod=0;
    int totCountSens=0;
    long totChannelStrips=0;
    long totChannelPts=0;
    
    // Generic (non format-dependent) tags for
    // text formatting
    std::string subStart="";
    std::string subEnd="";
    std::string superStart="";
    std::string superEnd="";
    std::string smallStart="";
    std::string smallEnd="";
    std::string emphStart="";
    std::string emphEnd="";
    std::string clearStart="";
    std::string clearEnd="";
    
    
    // Set the proper strings for the different filetypes
    if (fileType=="html") {
        subStart = "<sub>";
        subEnd = "</sub>";
        superStart = "<sup>";
        superEnd = "</sup>";
        smallStart = "<small>";
        smallEnd = "</small>";
        emphStart="<b>";
        emphEnd="</b>";
        clearStart="<tt>";
        clearEnd="</tt>";
    }
    
    std::vector<std::string> layerNames;
    std::vector<double> layerRho;
    std::vector<std::string> diskNames;
    std::vector<double> diskZ;
    std::vector<std::string> ringNames;
    std::vector<double> ringRho1;
    std::vector<double> ringRho2;
    Layer* aLayer;
    BarrelLayer* aBarrelLayer;
    EndcapLayer* anEndcapDisk;
    double aRingRho;
    
    // Build the module type maps
    // with a pointer to a sample module
    // Build the layer summary BTW
    diskNames.push_back("Disk Z");
    diskZ.push_back(0);
    layerNames.push_back("Layer r");
    layerRho.push_back(0);
    ringNames.push_back("Ring r");
    ringRho1.push_back(1);
    ringRho2.push_back(2);
    for (layIt=layerSet_.begin(); layIt!=layerSet_.end(); layIt++) {
        aLayer = (*layIt);
        if ( (aBarrelLayer=dynamic_cast<BarrelLayer*>(aLayer)) ) {
            if (aBarrelLayer->getMaxZ(+1)>0) {
                layerNames.push_back(aBarrelLayer->getName());
                layerRho.push_back(aBarrelLayer->getAverageRadius());
            }
        }
        if ( (anEndcapDisk=dynamic_cast<EndcapLayer*>(aLayer)) ) {
            if (anEndcapDisk->getAverageZ()>0) {
                diskNames.push_back(anEndcapDisk->getName());
                diskZ.push_back(anEndcapDisk->getAverageZ());
            }
        }
        aLay = (*layIt)->getModuleVector();
        for (modIt=aLay->begin(); modIt!=aLay->end(); modIt++) {
            aSensorTag=(*modIt)->getSensorTag();
            typeMapCount[aSensorTag]++;
            typeMapCountChan[aSensorTag]+=(*modIt)->getNChannels();
            if (((*modIt)->getOccupancyPerEvent()*nMB_)>typeMapMaxOccupancy[aSensorTag]) {
                typeMapMaxOccupancy[aSensorTag]=(*modIt)->getOccupancyPerEvent()*nMB_;
            }
            typeMapAveOccupancy[aSensorTag]+=(*modIt)->getOccupancyPerEvent()*nMB_;
            totCountMod++;
            totCountSens+=(*modIt)->getNFaces();
            if ((*modIt)->getReadoutType()==Module::Strip) {
                totChannelStrips+=(*modIt)->getNChannels();
                totAreaStrips+=(*modIt)->getArea()*(*modIt)->getNFaces();
            }
            if ((*modIt)->getReadoutType()==Module::Pt) {
                totChannelPts+=(*modIt)->getNChannels();
                totAreaPts+=(*modIt)->getArea()*(*modIt)->getNFaces();
            }
            if (typeMap.find(aSensorTag)==typeMap.end()){
                // We have a new sensor geometry
                typeMap[aSensorTag]=(*modIt);
            }
        }
    }
    
    EndcapModule* anEC;
    int aRing;
    // Look into the endcap sample in order to indentify and measure rings
    for (ModuleVector::iterator moduleIt=endcapSample_.begin(); moduleIt!=endcapSample_.end(); moduleIt++) {
        if ( (anEC=dynamic_cast<EndcapModule*>(*moduleIt)) ) {
            aRing=anEC->getRing();
            if (ringTypeMap.find(aRing)==ringTypeMap.end()){
                // We have a new sensor geometry
                ringTypeMap[aRing]=(*moduleIt);
            }
        } else {
            std::cout << "ERROR: found a non-Endcap module in the map of ring types" << std::endl;
        }
    }
    
    std::ostringstream myName;
    for (std::map<int, Module*>::iterator typeIt = ringTypeMap.begin();
    typeIt!=ringTypeMap.end(); typeIt++) {
        if ( (anEC=dynamic_cast<EndcapModule*>((*typeIt).second)) ) {
            myName.str("");
            myName << "Ring " << std::dec << (*typeIt).first;
            ringNames.push_back(myName.str());
            aRingRho = anEC->getDist();
            ringRho1.push_back(aRingRho);
            aRingRho = anEC->getDist()+anEC->getHeight();
            ringRho2.push_back(aRingRho);
        } else {
            std::cout << "ERROR: found a non-Endcap module in the map of ring types (twice...)" << std::endl;
        }
    }
    
    // Adjust sizes
    unsigned int maxSize=0;
    maxSize = (maxSize > diskNames.size()) ? maxSize : diskNames.size();
    maxSize = (maxSize > layerNames.size()) ? maxSize : layerNames.size();
    maxSize = (maxSize > ringNames.size()) ? maxSize : ringNames.size();
    for (unsigned int i=diskNames.size(); i<=maxSize; i++) {
        diskNames.push_back("");
        diskZ.push_back(0);
    }
    for (unsigned int i=layerNames.size(); i<=maxSize; i++) {
        layerNames.push_back("");
        layerRho.push_back(0);
    }
    for (unsigned int i=ringNames.size(); i<=maxSize; i++) {
        ringNames.push_back("");
        ringRho1.push_back(0);
        ringRho2.push_back(0);
    }
    
    // A bit of variables
    std::vector<std::string> names;
    std::vector<std::string> tags;
    std::vector<std::string> types;
    std::vector<std::string> areastrips;
    std::vector<std::string> areapts;
    std::vector<std::string> occupancies;
    std::vector<std::string> pitchpairs;
    std::vector<std::string> striplengths;
    std::vector<std::string> segments;
    std::vector<std::string> nstrips;
    std::vector<std::string> numbermods;
    std::vector<std::string> numbersens;
    std::vector<std::string> channelstrips;
    std::vector<std::string> channelpts;
    std::vector<std::string> powers;
    std::vector<std::string> costs;
    
    double totalPower=0;
    double totalCost=0;
    
    std::ostringstream aName;
    std::ostringstream aTag;
    std::ostringstream aType;
    std::ostringstream anArea;
    std::ostringstream anOccupancy;
    std::ostringstream aPitchPair;
    std::ostringstream aStripLength;
    std::ostringstream aSegment;
    std::ostringstream anNstrips;
    std::ostringstream aNumberMod;
    std::ostringstream aNumberSens;
    std::ostringstream aChannel;
    std::ostringstream aPower;
    std::ostringstream aCost;
    int barrelCount=0;
    int endcapCount=0;
    Module* aModule;
    
    // Row names
    names.push_back("");
    tags.push_back("Tag");
    types.push_back("Type");
    areastrips.push_back("Area (mm"+superStart+"2"+superEnd+")");
    areapts.push_back("Area (mm"+superStart+"2"+superEnd+")");
    occupancies.push_back("Occup (max/av)");
    pitchpairs.push_back("Pitch (min/max)");
    striplengths.push_back("Strip length");
    segments.push_back("Segments x Chips");
    nstrips.push_back("Chan/Sensor");
    numbermods.push_back("N. mod");
    numbersens.push_back("N. sens");
    channelstrips.push_back("Channels (M)");
    channelpts.push_back("Channels (M)");
    powers.push_back("Power (kW)");
    costs.push_back("Cost (MCHF)");
    
    int loPitch;
    int hiPitch;
    
    // Summary cycle: prepares the rows cell by cell
    for (typeMapIt=typeMap.begin(); typeMapIt!=typeMap.end(); typeMapIt++) {
        // Name
        aName.str("");
        aModule=(*typeMapIt).second;
        if (dynamic_cast<BarrelModule*>(aModule)) {
            aName << std::dec << "B" << subStart << ++barrelCount << subEnd;
        }
        if (dynamic_cast<EndcapModule*>(aModule)) {
            aName << std::dec << "E" << subStart << ++endcapCount << subEnd;
        }
        // Tag
        aTag.str("");
        aTag << smallStart << aModule->getTag() << smallEnd;
        // Type
        aType.str("");
        aType << (*typeMapIt).second->getType();
        // Area
        anArea.str("");
        anArea << std::dec << std::fixed << std::setprecision(areaPrecision) << (*typeMapIt).second->getArea();
        if ((*typeMapIt).second->getArea()<0) { anArea << "XXX"; }
        // Occupancy
        anOccupancy.str("");
        anOccupancy << std::dec << std::fixed << std::setprecision(occupancyPrecision) <<  typeMapMaxOccupancy[(*typeMapIt).first]*100<< "/" <<typeMapAveOccupancy[(*typeMapIt).first]*100/typeMapCount[(*typeMapIt).first] ; // Percentage
        // Pitches
        aPitchPair.str("");
        loPitch=int((*typeMapIt).second->getLowPitch()*1e3);
        hiPitch=int((*typeMapIt).second->getHighPitch()*1e3);
        if (loPitch==hiPitch) {
            aPitchPair << std::dec << std::fixed << std::setprecision(pitchPrecision) << loPitch;
        } else {
            aPitchPair << std::dec << std::fixed << std::setprecision(pitchPrecision)<< loPitch
            << "/" << std::fixed << std::setprecision(pitchPrecision) << hiPitch;
        }
        // Strip Lengths
        aStripLength.str("");
        aStripLength << std::fixed << std::setprecision(stripLengthPrecision)
        << (*typeMapIt).second->getHeight()/(*typeMapIt).second->getNSegments();
        // Segments
        aSegment.str("");
        aSegment << std::dec << (*typeMapIt).second->getNSegments()
        << "x" << int( (*typeMapIt).second->getNStripAcross() / 128. );
        // Nstrips
        anNstrips.str("");
        anNstrips << std::dec << (*typeMapIt).second->getNChannelsPerFace();
        // Number Mod
        aNumberMod.str("");
        aNumberMod << std::dec << typeMapCount[(*typeMapIt).first];
        // Number Sensor
        aNumberSens.str("");
        aNumberSens << std::dec << typeMapCount[(*typeMapIt).first]*((*typeMapIt).second->getNFaces());
        // Channels
        aChannel.str("");
        aChannel << std::fixed << std::setprecision(millionChannelPrecision)
        << typeMapCountChan[(*typeMapIt).first] / 1e6 ;
        // Power and cost
        aPower.str("");
        aCost.str("");
        aPower << std::fixed << std::setprecision(powerPrecision) <<
        typeMapCountChan[(*typeMapIt).first] *           // number of channels in type
        1e-3 *                                           // conversion from W to kW
        getPower((*typeMapIt).second->getReadoutType()); // power consumption in W/channel
        totalPower += typeMapCountChan[(*typeMapIt).first] * 1e-3 * getPower((*typeMapIt).second->getReadoutType());
        aCost  << std::fixed << std::setprecision(costPrecision) <<
        (*typeMapIt).second->getArea() * 1e-2 *          // area in cm^2
        (*typeMapIt).second->getNFaces() *               // number of faces
        getCost((*typeMapIt).second->getReadoutType()) * // price in CHF*cm^-2
        1e-6 *                                           // conversion CHF-> MCHF
        typeMapCount[(*typeMapIt).first];                // Number of modules
        totalCost +=(*typeMapIt).second->getArea() * 1e-2 * (*typeMapIt).second->getNFaces() * getCost((*typeMapIt).second->getReadoutType()) * 1e-6 * typeMapCount[(*typeMapIt).first];
        
        
        names.push_back(aName.str());
        tags.push_back(aTag.str());
        types.push_back(aType.str());
        occupancies.push_back(anOccupancy.str());
        pitchpairs.push_back(aPitchPair.str());
        striplengths.push_back(aStripLength.str());
        segments.push_back(aSegment.str());
        nstrips.push_back(anNstrips.str());
        numbermods.push_back(aNumberMod.str());
        numbersens.push_back(aNumberSens.str());
        powers.push_back(aPower.str());
        costs.push_back(aCost.str());
        
        if ((*typeMapIt).second->getReadoutType()==Module::Strip) {
            channelstrips.push_back(aChannel.str());
            areastrips.push_back(anArea.str());
            channelpts.push_back("--");
            areapts.push_back("--");
        } else {
            channelstrips.push_back("--");
            areastrips.push_back("--");
            channelpts.push_back(aChannel.str());
            areapts.push_back(anArea.str());
        }
        
        
    }
    
    // Score totals
    names.push_back("Total");
    types.push_back("--");
    anArea.str("");
    anArea << emphStart << std::fixed << std::setprecision(areaPrecision) << totAreaPts/1e6
    << "(m" << superStart << "2" << superEnd << ")" << emphEnd;
    areapts.push_back(anArea.str());
    anArea.str("");
    anArea << emphStart << std::fixed << std::setprecision(areaPrecision) << totAreaStrips/1e6
    << "(m" << superStart << "2" << superEnd << ")" << emphEnd;
    areastrips.push_back(anArea.str());
    occupancies.push_back("--");
    pitchpairs.push_back("--");
    striplengths.push_back("--");
    segments.push_back("--");
    nstrips.push_back("--");
    aNumberMod.str("");
    aNumberMod << emphStart << totCountMod << emphEnd;
    aNumberSens.str("");
    aNumberSens << emphStart << totCountSens << emphEnd;
    numbermods.push_back(aNumberMod.str());
    numbersens.push_back(aNumberSens.str());
    aPower.str("");
    aCost.str("");
    aPower   << std::fixed << std::setprecision(powerPrecision) << totalPower;
    aCost    << std::fixed << std::setprecision(costPrecision) << totalCost;
    powers.push_back(aPower.str());
    costs.push_back(aCost.str());
    aChannel.str("");
    aChannel << emphStart << std::fixed
    << std::setprecision(millionChannelPrecision)
    << totChannelStrips / 1e6 << emphEnd;
    channelstrips.push_back(aChannel.str());
    aChannel.str("");
    aChannel << emphStart << std::fixed
    << std::setprecision(millionChannelPrecision)
    << totChannelPts / 1e6 << emphEnd;
    
    channelpts.push_back(aChannel.str());
    
    
    // Write everything into a file in the summary dir
    std::string fileName = activeDirectory_+"/index."+fileType;
    std::cout << "$BROWSER " << fileName << std::endl;
    std::ofstream myfile;
    drawSummary(maxL_, maxR_, activeDirectory_+"/summaryPlots");
    myfile.open(fileName.c_str());
    if (fileType=="html") {
        myfile << "<html><title>"<<trackerName_<<"</title><body>" << std::endl;
        myfile << "<a href=\"../\">Summaries</a>" << std::endl;
        myfile << "<h1><a href=\"../../"<< storeDirectory_ << "/" << trackerName_ << ".root\">"<<trackerName_<<"</a></h1>" << std::endl;
        if (configFiles) {
            myfile << clearStart << emphStart << "Geometry configuration file:     " << emphEnd
                    << "<a href=\".//" << configFile << "\">" << configFile << "</a>" << clearEnd << "<br/>" << std::endl;
            myfile << clearStart << emphStart << "Module types configuration file: " << emphEnd
                    << "<a href=\".//" << dressFile << "\">" << dressFile << "</a>" << clearEnd << "<br/>" << std::endl;
            myfile << clearStart << emphStart << "Minimum bias per bunch crossing: " << emphEnd
                    << nMB_ << clearEnd << std::endl;
	    if (barrelModuleCoordinatesFile!="")
	      myfile << "<br/>" << clearStart << emphStart << "Barrel modules coordinate file:  " << emphEnd
		     << "<a href=\".//" << barrelModuleCoordinatesFile << "\">" << barrelModuleCoordinatesFile << "</a>" << clearEnd << std::endl;
	    if (endcapModuleCoordinatesFile!="")
	      myfile << "<br/>" << clearStart << emphStart << "End-cap modules coordinate file:  " << emphEnd
		     << "<a href=\".//" << endcapModuleCoordinatesFile << "\">" << endcapModuleCoordinatesFile << "</a>" << clearEnd << std::endl;
        } else {
            myfile << clearStart << emphStart << "Options: " << emphEnd << getArguments() << clearEnd << std::endl;
        }
        myfile << "<h3>Layers and disks</h3>" << std::endl;
        myfile << "<table>" << std::endl;
        printHtmlTableRow(&myfile, layerNames);
        printHtmlTableRow(&myfile, layerRho, coordPrecision, true);
        printHtmlTableRow(&myfile, diskNames);
        printHtmlTableRow(&myfile, diskZ, coordPrecision, true);
        printHtmlTableRow(&myfile, ringNames);
        printHtmlTableRow(&myfile, ringRho1, coordPrecision, true);
        printHtmlTableRow(&myfile, ringRho2, coordPrecision, true);
        myfile << "</table>"<<std::endl;
        myfile << "<h3>Modules</h3>" << std::endl;
        myfile << "<table>" << std::endl;
        printHtmlTableRow(&myfile, names);
        printHtmlTableRow(&myfile, tags);
        printHtmlTableRow(&myfile, types);
        printHtmlTableRow(&myfile, areapts);
        printHtmlTableRow(&myfile, areastrips);
        printHtmlTableRow(&myfile, occupancies);
        printHtmlTableRow(&myfile, pitchpairs);
        printHtmlTableRow(&myfile, segments);
        printHtmlTableRow(&myfile, striplengths);
        printHtmlTableRow(&myfile, nstrips);
        printHtmlTableRow(&myfile, numbermods);
        printHtmlTableRow(&myfile, numbersens);
        printHtmlTableRow(&myfile, channelstrips);
        printHtmlTableRow(&myfile, channelpts);
        printHtmlTableRow(&myfile, powers);
        printHtmlTableRow(&myfile, costs);
        myfile << "</table>"<<std::endl;
        // TODO: make an object that handles this properly:
        myfile << "<h5>Cost estimates:</h5>" << std::endl;
        myfile << "<p>Pt modules: "
        << std::fixed << std::setprecision(costPerUnitPrecision)
        << getCost(Module::Pt) << " CFH/cm2 - Strip modules: "
        << std::fixed << std::setprecision(costPerUnitPrecision)
        << getCost(Module::Strip) << " CHF/cm2</p>" << std::endl;
        myfile << "<h5>Power estimates:</h5>" << std::endl;
        myfile << "<p>Pt modules: "
        << std::fixed << std::setprecision(powerPerUnitPrecision)
        << getPower(Module::Pt)*1e3 << " mW/chan - Strip modules: "
        << std::fixed << std::setprecision(powerPerUnitPrecision)
        << getPower(Module::Strip)*1e3 << " mW/chan</p>" << std::endl;
        
        myfile << "<h3>Plots</h3>" << std::endl;
        myfile << "<img src=\"summaryPlots.png\" />" << std::endl;
        myfile << "<img src=\"summaryPlots_nhitplot.png\" />" << std::endl;
        myfile << "<img src=\"summaryPlots_hitmap.png\" />" << std::endl;
        myfile << "<img src=\"summaryPlotsYZ.png\" />" << std::endl;
        // TODO: make an object that handles this properly:
        myfile << "<h5>Bandwidth useage estimate:</h5>" << std::endl;
        myfile << "<p>(Pt modules: ignored)</p>" << std::endl
                << "<p>Sparsified (binary) bits/event: 23 bits/chip + 9 bit/hit</p>" << std::endl
                << "<p>Unsparsified (binary) bits/event: 16 bits/chip + 1 bit/channel</p>" << std::endl
                << "<p>100 kHz trigger, " << nMB_ << " minimum bias events assumed</p>" << std::endl;
        myfile << "<img src=\"summaryPlots_bandwidth.png\" />" << std::endl;
        myfile << "</body></html>" << std::endl;
    }
    myfile.close();
    
    // TODO: create a TString summary
}

void Tracker::createPackageLayout(std::string dirName) {
    std::string layoutFile = dirName + "/layout.png";
    drawLayout(maxL_, maxR_, layoutFile);
}


// Prints the positions of barrel modules to file or cout
void Tracker::printBarrelModuleZ(ostream& outfile) {
  ModuleVector* myModules;
  ModuleVector::iterator itModule;
  LayerVector* myBarrels;
  LayerVector::iterator itLayer;
  std::pair<int, int> myRZ;
  int myR, myZ;
  XYZVector meanPoint;

  outfile << "BarrelLayer name, r(mm), z(mm), number of modules" <<std::endl;
  myBarrels = getBarrelLayers();
  for (itLayer = myBarrels->begin();
       itLayer != myBarrels->end();
       itLayer++) {

    std::map< std::pair<int,int>, int > posCount;
    
    myModules = (*itLayer)->getModuleVector();
    for (itModule = myModules->begin();
	 itModule != myModules->end();
	 itModule++) {
      meanPoint = (*itModule)->getMeanPoint();
      myR = int(ceil(meanPoint.Rho()-0.5));
      myZ = int(ceil(meanPoint.Z()-0.5));
      myRZ.first=myR;
      myRZ.second=myZ;
      posCount[myRZ]++;
    }

    std::map<std::pair<int,int>, int>::iterator itPos;
    for (itPos = posCount.begin();
	 itPos != posCount.end();
	 itPos++) {
      // BarrelLayer name
      outfile << (*itLayer)->getContainerName() << "-"
	      << (*itLayer)->getName() << ", ";
      
      outfile << (*itPos).first.first << ", " // r
	      << (*itPos).first.second << ", "   // z
	      << (*itPos).second // number of modules
	      << std::endl;
    }
  }
}

// Prints the positions of endcap modules to file or cout
void Tracker::printEndcapModuleRPhiZ(ostream& outfile) {
  ModuleVector::iterator itModule;
  XYZVector meanPoint;
  double base_inner, base_outer, height;
  
  EndcapModule* anEC;
  int aRing;
  // Sort the endcap module vector to have it ordered by
  // rings (order by r, then phi, then z)
  std::sort(endcapSample_.begin(), endcapSample_.end(), moduleSortEndcapStyle);
  
  // Compute the average Z as the mean between max and min Z
  double maxZ=0;
  double minZ=0;
  bool first=true;
  for (ModuleVector::iterator moduleIt=endcapSample_.begin(); moduleIt!=endcapSample_.end(); moduleIt++) {
    if ( (anEC=dynamic_cast<EndcapModule*>(*moduleIt)) ) {
      if (first) {
	first=false;
	minZ=anEC->getMeanPoint().Z();
	maxZ=minZ;
      } else {
	if (anEC->getMeanPoint().Z()<minZ) minZ=anEC->getMeanPoint().Z();
	if (anEC->getMeanPoint().Z()>maxZ) maxZ=anEC->getMeanPoint().Z();
      }
    } else {
      std::cerr << "ERROR: found a non-Endcap module in the map of ring types. This should not happen. Contact the developers." << std::endl;
    }
  }
  double averageZ = (minZ+maxZ)/2.;

  // Look into the endcap sample to get modules' positions 
  outfile << "Ring, r(mm), phi(deg), z(mm), base_inner(mm), base_outer(mm), height(mm)" <<std::endl;
  for (ModuleVector::iterator moduleIt=endcapSample_.begin(); moduleIt!=endcapSample_.end(); moduleIt++) {
    if ( (anEC=dynamic_cast<EndcapModule*>(*moduleIt)) ) {
      aRing=anEC->getRing();
      meanPoint = anEC->getMeanPoint();
      base_inner = anEC->getWidthLo();
      base_outer = anEC->getWidthHi();
      height = anEC->getHeight();

      // Print the data in fixed-precision
      // Limit the precision to one micron for lengths and 1/1000 degree for angles
      outfile << std::fixed;
      outfile << aRing << ", " 
	      << std::fixed << std::setprecision(3) << meanPoint.Rho() << ", "
	      << std::fixed << std::setprecision(3) << meanPoint.Phi()/M_PI*180. << ", "
	      << std::fixed << std::setprecision(3) << meanPoint.Z()-averageZ << ", "
	      << std::fixed << std::setprecision(3) << base_inner << ", "
	      << std::fixed << std::setprecision(3) << base_outer << ", "
	      << std::fixed << std::setprecision(3) << height << std::endl;
    } else {
      std::cerr << "ERROR: found a non-Endcap module in the map of ring types. This should not happen. Contact the developers." << std::endl;
    }
  }
}


void Tracker::printHtmlTableRow(ofstream *output, std::vector<std::string> myRow) {
    std::vector<std::string>::iterator strIt;
    (*output) << "<tr>" << std::endl;
    for (strIt=myRow.begin(); strIt!=myRow.end(); strIt++) {
        (*output) << "<td>" << (*strIt) << "</td> ";
    }
    (*output) << "</tr>" << std::endl;
}

void Tracker::printHtmlTableRow(ofstream *output, std::vector<double> myRow, int coordPrecision /* = 0 */, bool skimZero /*=false*/) {
    std::vector<double>::iterator strIt;
    (*output) << "<tr>" << std::endl;
    for (strIt=myRow.begin(); strIt!=myRow.end(); strIt++) {
        (*output) << "<td>";
        if ((!skimZero)||((*strIt)!=0)) (*output) << std::fixed << std::setprecision(coordPrecision) << (*strIt);
        (*output)<< "</td>";
    }
    (*output) << "</tr>" << std::endl;
}

// TODO: use the boost library to handle directories
// (see http://boost.org/libs/filesystem/doc/index.htm )
void Tracker::createDirectories() {
    
    if (activeDirectory_=="") {
        activeDirectory_ = summaryDirectory_ + "/" + trackerName_;
        mkdir(summaryDirectory_.c_str(), 0755);
        mkdir(activeDirectory_.c_str(), 0755);
        mkdir(storeDirectory_.c_str(), 0755);
    } else {
        mkdir(activeDirectory_.c_str(), 0755);
        mkdir(storeDirectory_.c_str(), 0755);
    }
    
}

void Tracker::save() {
    std::vector<TObject* >::iterator itObj;
    std::string fileName;
    
    fileName = storeDirectory_ + "/" + trackerName_ + ".root";
    createDirectories();
    TFile* myFile = new TFile(fileName.c_str(), "RECREATE", trackerName_.c_str());
    for (itObj = savingV_.begin(); itObj != savingV_.end(); itObj++) {
        (*itObj)->Write();
    }
    myFile->Close();
}


// Enumerate sections by
// axis index normal to draw plane
// (if x=1, y=2, z=3)
void Tracker::drawGrid(double maxL, double maxR, int noAxis/*=1*/, double spacing /*= 100.*/, Option_t* option /*= "same"*/) {
    TPolyLine3D* aLine;
    Color_t gridColor = COLOR_GRID;
    Color_t gridColor_hard = COLOR_HARD_GRID;
    Color_t thisLineColor;
    
    std::string theOption(option);
    
    int i;
    int j;
    int k;
    
    double topMax = (maxL > maxR) ? maxL : maxR;
    topMax = ceil(topMax/spacing)*spacing;
    
    double aValue[3];
    double minValue[3];
    double maxValue[3];
    double runValue;
    int thisLineStyle;
    
    i=(noAxis)%3;
    j=(noAxis+1)%3;
    k=(noAxis+2)%3;
    
    maxL *= 1.1;
    maxR *= 1.1;
    
    if (noAxis==1) {
        minValue[0]=0;
        maxValue[0]=+maxR;
        minValue[1]=0;
        maxValue[1]=+maxR;
        minValue[2]=0;
        maxValue[2]=+maxL;
    } else {
        minValue[0]=-maxR;
        maxValue[0]=+maxR;
        minValue[1]=-maxR;
        maxValue[1]=+maxR;
        minValue[2]=0;
        maxValue[2]=+maxL;
    }
    
    aValue[k]=-topMax;
    for(runValue = -topMax; runValue<=topMax; runValue+=spacing) {
        
        // Special line for axis
        if (runValue==0) {
            thisLineStyle=1;
            thisLineColor=gridColor_hard;
        } else {
            thisLineStyle=2;
            thisLineColor=gridColor;
        }
        
        // Parallel to j
        if ((runValue<=maxValue[i])&&(runValue>=minValue[i])) {
            aValue[i] = runValue;
            aLine = new TPolyLine3D(2);
            aValue[j] = minValue[j];
            aLine->SetPoint(0, aValue[0], aValue[1], aValue[2]);
            aValue[j] = maxValue[j];
            aLine->SetPoint(1, aValue[0], aValue[1], aValue[2]);
            aLine->SetLineStyle(thisLineStyle);
            aLine->SetLineColor(thisLineColor);
            aLine->Draw(theOption.c_str());
            theOption="same";
        };
        
        // Parallel to i
        if ((runValue<=maxValue[j])&&(runValue>=minValue[j])) {
            aValue[j] = runValue;
            aLine = new TPolyLine3D(2);
            aValue[i] = minValue[i];
            aLine->SetPoint(0, aValue[0], aValue[1], aValue[2]);
            aValue[i] = maxValue[i];
            aLine->SetPoint(1, aValue[0], aValue[1], aValue[2]);
            aLine->SetLineStyle(thisLineStyle);
            aLine->SetLineColor(thisLineColor);
            aLine->Draw(theOption.c_str());
            theOption="same";
        };
        
    }
    
    
}

// Enumerate sections by
// axis index normal to draw plane
// (if x=1, y=2, z=3)
void Tracker::drawTicks(TView* myView, double maxL, double maxR, int noAxis/*=1*/, double spacing /*= 100.*/, Option_t* option /*= "same"*/) {
    TPolyLine3D* aLine;
    Color_t gridColor_hard = COLOR_HARD_GRID;
    int gridStyle_solid = 1;
    
    std::string theOption(option);
    
    int i;
    int j;
    int k;
    
    double topMax = (maxL > maxR) ? maxL : maxR;
    topMax = ceil(topMax/spacing)*spacing;
    
    double aValue[3];
    double minValue[3];
    double maxValue[3];
    
    i=(noAxis)%3;
    j=(noAxis+1)%3;
    k=(noAxis+2)%3;
    
    maxL *= 1.1;
    maxR *= 1.1;
    
    if (noAxis==1) {
        minValue[0]=0;
        maxValue[0]=+maxR;
        minValue[1]=0;
        maxValue[1]=+maxR;
        minValue[2]=0;
        maxValue[2]=+maxL;
    } else {
        minValue[0]=-maxR;
        maxValue[0]=+maxR;
        minValue[1]=-maxR;
        maxValue[1]=+maxR;
        minValue[2]=0;
        maxValue[2]=+maxL;
    }
    
    aValue[k]=-topMax;
    
    if (noAxis==1) {
        double etaStep=.2;
        double etaMax = 2.1;
        // Add the eta ticks
        double theta;
        double tickLength = 2 * spacing;
        double tickDistance = spacing;
        double startR = maxR + tickDistance;
        double startL = maxL + tickDistance;
        double endR = maxR + tickDistance + tickLength;
        double endL = maxL + tickDistance + tickLength;
        XYZVector startTick;
        XYZVector endTick;
        Double_t pw[3];
        Double_t pn[3];
        TText* aLabel;
        char labelChar[10];
        for (double eta=0; eta<etaMax; eta+=etaStep) {
            aLine = new TPolyLine3D(2);
            theta = 2 * atan(exp(-eta));
            startTick = XYZVector(0, sin(theta), cos(theta));
            startTick *= startR/startTick.Rho();
            endTick = startTick / startTick.Rho() * endR;
            if (startTick.Z()>startL) {
                startTick *= startL/startTick.Z();
                endTick *=  endL/endTick.Z();
            }
            pw[0]=0.;
            pw[1]=endTick.Y();
            pw[2]=endTick.Z();
            myView->WCtoNDC(pw, pn);
            sprintf(labelChar, "%.01f", eta);
            aLabel = new TText(pn[0], pn[1], labelChar);
            aLabel->SetTextSize(aLabel->GetTextSize()*.6);
            aLabel->SetTextAlign(21);
            aLabel->Draw(theOption.c_str());
            theOption="same";
            endTick = (endTick+startTick)/2.;
            aLine->SetPoint(0, 0., startTick.Y(), startTick.Z());
            aLine->SetPoint(1, 0., endTick.Y(), endTick.Z());
            aLine->SetLineStyle(gridStyle_solid);
            aLine->SetLineColor(gridColor_hard);
            aLine->Draw("same");
        }
        aLine = new TPolyLine3D(2);
        theta = 2 * atan(exp(-2.5));
        startTick = XYZVector(0, sin(theta), cos(theta));
        startTick *= startR/startTick.Rho();
        endTick = startTick / startTick.Rho() * endR;
        if (startTick.Z()>startL) {
            startTick *= startL/startTick.Z();
            endTick *=  endL/endTick.Z();
        }
        pw[0]=0.;
        pw[1]=endTick.Y();
        pw[2]=endTick.Z();
        myView->WCtoNDC(pw, pn);
        sprintf(labelChar, "%.01f", 2.5);
        aLabel = new TText(pn[0], pn[1], labelChar);
        aLabel->SetTextSize(aLabel->GetTextSize()*.8);
        aLabel->SetTextAlign(21);
        aLabel->Draw(theOption.c_str());
        theOption="same";
        endTick = (endTick+startTick)/2.;
        aLine->SetPoint(0, 0., 0., 0.);
        aLine->SetPoint(1, 0., endTick.Y(), endTick.Z());
        aLine->SetLineStyle(gridStyle_solid);
        aLine->SetLineColor(gridColor_hard);
        aLine->Draw("same");
        
        
        
        for (double z=0; z<=maxL ; z+=(4*spacing)) {
            aLine = new TPolyLine3D(2);
            startTick = XYZVector(0, 0, z);
            endTick = XYZVector(0, -(tickLength/2), z);
            aLine->SetPoint(0, 0., startTick.Y(), startTick.Z());
            aLine->SetPoint(1, 0., endTick.Y(), endTick.Z());
            pw[0]=0.;
            pw[1]=-tickLength;
            pw[2]=endTick.Z();
            myView->WCtoNDC(pw, pn);
            sprintf(labelChar, "%.0f", z);
            aLabel = new TText(pn[0], pn[1], labelChar);
            aLabel->SetTextSize(aLabel->GetTextSize()*.6);
            aLabel->SetTextAlign(23);
            aLabel->Draw(theOption.c_str());
            theOption="same";
            aLine->SetLineStyle(gridStyle_solid);
            aLine->SetLineColor(gridColor_hard);
            aLine->Draw("same");
        }
        
        for (double y=0; y<=maxR ; y+=(2*spacing)) {
            aLine = new TPolyLine3D(2);
            startTick = XYZVector(0, y, 0);
            endTick = XYZVector(0, y, -(tickLength/2));
            aLine->SetPoint(0, 0., startTick.Y(), startTick.Z());
            aLine->SetPoint(1, 0., endTick.Y(), endTick.Z());
            pw[0]=0.;
            pw[1]=endTick.Y();
            pw[2]=-tickLength;
            myView->WCtoNDC(pw, pn);
            sprintf(labelChar, "%.0f", y);
            aLabel = new TText(pn[0], pn[1], labelChar);
            aLabel->SetTextSize(aLabel->GetTextSize()*.6);
            aLabel->SetTextAlign(32);
            aLabel->Draw(theOption.c_str());
            theOption="same";
            aLine->SetLineStyle(gridStyle_solid);
            aLine->SetLineColor(gridColor_hard);
            aLine->Draw("same");
        }
    }
}

// This function is the place where we set the module types
// Basically the only place that is to be edited, before we
// put all the stuff in user interface
void Tracker::setModuleTypesDemo1() {
    LayerVector::iterator layIt;
    ModuleVector::iterator modIt;
    ModuleVector* aLay;
    Module* aModule;
    BarrelModule* aBarrelModule;
    EndcapModule* anEndcapModule;
    
    for (layIt=barrelLayerSet_.begin(); layIt!=barrelLayerSet_.end(); layIt++) {
        aLay = (*layIt)->getModuleVector();
        for (modIt=aLay->begin(); modIt!=aLay->end(); modIt++) {
            aModule=(*modIt);
            if ( (aBarrelModule=dynamic_cast<BarrelModule*>(aModule)) ) {
                aBarrelModule->setColor(aBarrelModule->getLayer());
            } else {
                // This shouldnt happen
                std::cerr << "ERROR! in function Tracker::setModuleTypes() "
                <<"I found a !BarrelModule in the barrel" << std::endl;
            }
        }
    }
    
    for (layIt=endcapLayerSet_.begin(); layIt!=endcapLayerSet_.end(); layIt++) {
        aLay = (*layIt)->getModuleVector();
        for (modIt=aLay->begin(); modIt!=aLay->end(); modIt++) {
            aModule=(*modIt);
            if ( (anEndcapModule=dynamic_cast<EndcapModule*>(aModule)) ) {
                anEndcapModule->setColor(anEndcapModule->getRing()+anEndcapModule->getDisk());
            } else {
                // This shouldnt happen
                std::cerr << "ERROR! in function Tracker::setModuleTypes() "
                << "I found a !EndcapModule in the end-caps" << std::endl;
            }
        }
    }
    
    for (modIt=endcapSample_.begin(); modIt!=endcapSample_.end(); modIt++) {
        aModule=(*modIt);
        if ( (anEndcapModule=dynamic_cast<EndcapModule*>(aModule)) ) {
            anEndcapModule->setColor(anEndcapModule->getRing()+anEndcapModule->getDisk());
        } else {
            // This shouldnt happen
            std::cerr << "ERROR! in function Tracker::setModuleTypes() "
            << "I found a !EndcapModule in the end cap sample" << std::endl;
        }
    }
}


// The real method used here
// Parameter set here (example)
//       sampleModule->setNStripAcross(512);
//       sampleModule->setNSegments(1);
//       sampleModule->setNFaces(2);
//       sampleModule->setType("stereo");
//       sampleModule->setTag("L2");
//       sampleModule->setColor(kBlue);
void Tracker::setModuleTypes() {
    LayerVector::iterator layIt;
    ModuleVector::iterator modIt;
    ModuleVector* aLay;
    Module* aModule;
    BarrelModule* aBarrelModule;
    EndcapModule* anEndcapModule;
    
    int nStripAcross;
    int nSegments;
    int nFaces;
    std::ostringstream myTag;;
    std::string myType;
    Color_t myColor;
    int readoutType;
    
    for (layIt=barrelLayerSet_.begin(); layIt!=barrelLayerSet_.end(); layIt++) {
        aLay = (*layIt)->getModuleVector();
        for (modIt=aLay->begin(); modIt!=aLay->end(); modIt++) {
            aModule=(*modIt);
            if ( (aBarrelModule=dynamic_cast<BarrelModule*>(aModule)) ) {
                
                nFaces = 1;
                readoutType = Module::Strip;
                switch (aBarrelModule->getLayer()) {
                    case 1:
                        nStripAcross = 9*128;
                        nSegments = 20;
                        myType = "pt";
                        nFaces = 2;
                        myColor = kRed;
                        readoutType = Module::Pt;
                        break;
                    case 2:
                        nStripAcross = 8*128;
                        nSegments = 2;
                        myType = "rphi";
                        myColor = kGreen;
                        break;
                    case 3:
                    case 4:
                        nStripAcross = 6*128;
                        nSegments = 1;
                        myType = "stereo";
                        nFaces = 2;
                        myColor = kBlue;
                        break;
                    case 5:
                    case 6:
                        nStripAcross = 6*128;
                        nSegments = 1;
                        myType = "rphi";
                        myColor = kGreen;
                        break;
                    default:
                        nStripAcross = 1;
                        nSegments = 1;
                        myType = "none";
                        myColor = kBlack;
                        break;
                }
                myTag.str("");
                myTag << "L" << std::dec << aBarrelModule->getLayer();
                aBarrelModule->setNStripAcross(nStripAcross);
                aBarrelModule->setNSegments(nSegments);
                aBarrelModule->setNFaces(nFaces);
                aBarrelModule->setType(myType);
                aBarrelModule->setTag(myTag.str());
                aBarrelModule->setColor(myColor);
                aBarrelModule->setReadoutType(readoutType);
            } else {
                // This shouldnt happen
                std::cerr << "ERROR! in function Tracker::setModuleTypes() "
                <<"I found a !BarrelModule in the barrel" << std::endl;
            }
        }
    }
    
    
    
    ModuleVector allEndcapModules;
    for (layIt=endcapLayerSet_.begin(); layIt!=endcapLayerSet_.end(); layIt++) {
        aLay = (*layIt)->getModuleVector();
        for (modIt=aLay->begin(); modIt!=aLay->end(); modIt++) {
            allEndcapModules.push_back(*modIt);
        }
    }
    for (modIt=endcapSample_.begin(); modIt!=endcapSample_.end(); modIt++) {
        allEndcapModules.push_back(*modIt);
    }
    
    
    for (modIt=allEndcapModules.begin(); modIt!=allEndcapModules.end(); modIt++) {
        aModule=(*modIt);
        if ( (anEndcapModule=dynamic_cast<EndcapModule*>(aModule)) ) {
            
            nFaces = 1;
            switch (anEndcapModule->getRing()) {
                case 1:
                    nStripAcross = 6*128;
                    nSegments = 6;
                    myType = "rphi";
                    myColor = kGreen;
                    break;
                case 2:
                    nStripAcross = 6*128;
                    nSegments = 4;
                    myType = "rphi";
                    myColor = kGreen;
                    break;
                case 3:
                case 4:
                    nStripAcross = 6*128;
                    nSegments = 2;
                    myType = "rphi";
                    myColor = kGreen;
                    break;
                case 5:
                case 6:
                    nStripAcross = 6*128;
                    nSegments = 1;
                    myType = "stereo";
                    nFaces = 2;
                    myColor = kBlue;
                    break;
                default:
                    nStripAcross = 6*128;
                    nSegments = 1;
                    myType = "rphi";
                    myColor = kGreen;
                    break;
            }
            myTag.str("");
            myTag << "R" << std::dec << anEndcapModule->getRing();
            anEndcapModule->setNStripAcross(nStripAcross);
            anEndcapModule->setNSegments(nSegments);
            anEndcapModule->setNFaces(nFaces);
            anEndcapModule->setType(myType);
            anEndcapModule->setTag(myTag.str());
            anEndcapModule->setColor(myColor);
            
        } else {
            // This shouldnt happen
            std::cerr << "ERROR! in function Tracker::setModuleTypes() "
            << "I found a !EndcapModule in the end-caps" << std::endl;
        }
    }
}

// Returns the same color for the same module type across
// all the program
Color_t Tracker::colorPicker(std::string type) {
    if (type=="") return COLOR_INVALID_MODULE;
    if (colorPickMap_[type]==0) {
        // New type! I'll pick a new color
        colorPickMap_[type]=++lastPickedColor_;
    }
    return colorPickMap_[type];
}

// Method used to assign the module types via config file
// Paramter set here (example)
//   sampleModule->setNStripAcross(512);
//   sampleModule->setNFaces(2);
//   sampleModule->setNSegments(1);
//   sampleModule->setType("stereo");
//   sampleModule->setTag("TIBL2");
//   sampleModule->setColor(kBlue);
//   sampleModule->setReadoutType(Module::Pt);

void Tracker::setModuleTypes(std::string sectionName,
        std::map<int, int> nStripsAcross,
        std::map<int, int> nFaces,
        std::map<int, int> nSegments,
        std::map<int, std::string> myType,
        std::map<int, double> dsDistance,
        std::map<int, double> dsRotation,
        std::map<std::pair<int, int>, int> nStripsAcrossSecond,
        std::map<std::pair<int, int>, int> nFacesSecond,
        std::map<std::pair<int, int>, int> nSegmentsSecond,
        std::map<std::pair<int, int>, std::string> myTypeSecond,
        std::map<std::pair<int, int>, double> dsDistanceSecond,
        std::map<std::pair<int, int>, double> dsRotationSecond,
        std::map<std::pair<int, int>, bool> specialSecond) {
    
    LayerVector::iterator layIt;
    ModuleVector::iterator modIt;
    ModuleVector* aLay;
    Module* aModule;
    BarrelModule* aBarrelModule;
    EndcapModule* anEndcapModule;
    
    std::map<int, bool> warningStrips;
    std::map<int, bool> warningFaces;
    std::map<int, bool> warningSegments;
    std::map<int, bool> warningType;
    std::map<int, bool> warningDistance;
    std::map<int, bool> warningRotation;
    
    int aStripsAcross;
    int aFaces;
    int aSegments;
    std::string aType;
    double aDistance;
    double aRotation;
    
    std::pair<int, int> mySpecialIndex;
    
    std::ostringstream myTag; // This must be set according to the sectionName and index
    int myReadoutType; // (this must be set according to the delcared "type" )
    int myIndex; // This must be set according to the module layer (barrel) or disk (endcap)
    
    // Take the vector of layers in the section
    LayerVector myLayers = sectionMap_[sectionName];
    
    for (layIt=myLayers.begin(); layIt!=myLayers.end(); layIt++) {
        aLay = (*layIt)->getModuleVector();
        for (modIt=aLay->begin(); modIt!=aLay->end(); modIt++) {
            aModule=(*modIt);
            
            // Tag definition and index picking definition
            myTag.str("");
            myTag << sectionName << std::dec ;
            myIndex = -1;
            if ( (aBarrelModule=dynamic_cast<BarrelModule*>(aModule)) ) {
                myTag << "L" << aBarrelModule->getLayer();
                myIndex = aBarrelModule->getLayer();
                mySpecialIndex.first= myIndex;
                mySpecialIndex.second = aBarrelModule->getRing();
		if (specialSecond[mySpecialIndex]) {
		  myTag << "R" << aBarrelModule->getRing();
		}
            } else if ( (anEndcapModule=dynamic_cast<EndcapModule*>(aModule)) ) {
                myTag << "R" << anEndcapModule->getRing();
                myIndex = anEndcapModule->getRing();
                // If special rules are applied here, we add the disk id to the tag
                mySpecialIndex.first = myIndex;
                mySpecialIndex.second = anEndcapModule->getDisk();
                if (specialSecond[mySpecialIndex]) {
                    myTag << "D" << anEndcapModule->getDisk();
                }
            } else {
                // This shouldnt happen
                std::cerr << "ERROR! in function Tracker::setModuleTypes() "
                << "I found a module which is not Barrel nor Endcap module. What's this?!?" << std::endl;
                mySpecialIndex.first= -1;
                mySpecialIndex.second = -1;
            }
            
            aStripsAcross = nStripsAcross[myIndex];
            aFaces = nFaces[myIndex];
            aSegments = nSegments[myIndex];
            aType = myType[myIndex];
            aDistance = dsDistance[myIndex];
            aRotation = dsRotation[myIndex];
            
            if (specialSecond[mySpecialIndex]) {
                if (nStripsAcrossSecond[mySpecialIndex]!=0) {
                    aStripsAcross = nStripsAcrossSecond[mySpecialIndex];
                }
                if (nFacesSecond[mySpecialIndex]!=0) {
                    aFaces = nFacesSecond[mySpecialIndex];
                }
                if (nSegmentsSecond[mySpecialIndex]!=0) {
                    aSegments = nSegmentsSecond[mySpecialIndex];
                }
                if (myTypeSecond[mySpecialIndex]!="") {
                    aType = myTypeSecond[mySpecialIndex];
                }
                if (dsDistanceSecond[mySpecialIndex]!=0) {
                    aDistance = dsDistanceSecond[mySpecialIndex];
                }
                if (dsRotationSecond[mySpecialIndex]!=0) {
                    aRotation = dsRotationSecond[mySpecialIndex];
                }
            }
            
            // Readout type definition, according to the module type
            if (aType == "pt") {
                myReadoutType = Module::Pt;
            } else if (myType[myIndex] == "rphi") {
                myReadoutType = Module::Strip;
            } else if (myType[myIndex] == "stereo") {
                myReadoutType = Module::Strip;
            } else if (myType[myIndex] == "pixel") {
                myReadoutType = Module::Pixel;
            } else {
                myReadoutType = Module::Undefined;
            }
            
            aModule->setNStripAcross(aStripsAcross);
            aModule->setNFaces(aFaces);
            aModule->setNSegments(aSegments);
            aModule->setType(aType);
            aModule->setStereoDistance(aDistance);
            aModule->setStereoRotation(aRotation);
            aModule->setTag(myTag.str());
            aModule->setColor(colorPicker(aType));
            aModule->setReadoutType(myReadoutType);
            
            
            // TODO: decide whether to use nStripAcross or nStripsAcross everywhere
            
            // Check if a given module type was not assigned fr a group
            // We do not check the case for special assignment (optional)
            if ((nStripsAcross[myIndex]==0)&&(!warningStrips[myIndex])) {
                std::cerr << "WARNING: undefined or zero nStripsAcross: \"" << nStripsAcross[myIndex] << "\" "
                << "for tracker section " << sectionName << "[" << myIndex << "]" << std::endl;
                warningStrips[myIndex]=true;
            }
            if ((nFaces[myIndex]==0)&&(!warningFaces[myIndex])) {
                std::cerr << "WARNING: undefined or zero nFaces: \"" << nFaces[myIndex] << "\" "
                << "for tracker section " << sectionName << "[" << myIndex << "]" << std::endl;
                warningFaces[myIndex]=true;
            }
            if ((nSegments[myIndex]==0)&&(!warningSegments[myIndex])) {
                std::cerr << "WARNING: undefined or zero nSegments: \"" << nSegments[myIndex] << "\" "
                << "for tracker section " << sectionName << "[" << myIndex << "]" << std::endl;
                warningSegments[myIndex]=true;
            }
            if ((myReadoutType==Module::Undefined)&&(!warningType[myIndex])) {
                std::cerr << "WARNING: undefined or void module type: \"" << myType[myIndex] << "\" "
                << "for tracker section " << sectionName << "[" << myIndex << "]" << std::endl;
                warningType[myIndex]=true;
            }
            if ((dsDistance[myIndex]<0)&&(!warningDistance[myIndex])) {
                std::cerr << "WARNING: negative distance for stereo sensors: \"" << dsDistance[myIndex] << "\" "
                << "for tracker section " << sectionName << "[" << myIndex << "]" << std::endl;
                warningDistance[myIndex]=true;
            }
            if ((dsRotation[myIndex]>360.0)&&(!warningRotation[myIndex])) {
                std::cerr << "WARNING: rotation for stereo sensors is greater than 360*deg: \"" << dsRotation[myIndex] << "\" "
                << "for tracker section " << sectionName << "[" << myIndex << "]" << std::endl;
                warningRotation[myIndex]=true;
            }
            
        }
    }
    
}


void Tracker::changeRingModules(std::string diskName, int ringN, std::string newType, Color_t newColor) {
    ModuleVector::iterator modIt;
    ModuleVector* moduleV;
    LayerVector::iterator itLay;
    std::string aTag;
    std::ostringstream myTag;
    
    for (itLay=endcapLayerSet_.begin(); itLay!=endcapLayerSet_.end(); itLay++) {
        if ((*itLay)->getName()==diskName) {
            moduleV = (*itLay)->getModuleVector();
            for (modIt=moduleV->begin(); modIt!=moduleV->end(); modIt++) {
                aTag = (*modIt)->getTag();
                myTag.str("");
                myTag<<"R"<<ringN;
                if (myTag.str()==aTag) {
                    (*modIt)->setType(newType);
                    (*modIt)->setColor(newColor);
                }
            }
        }
    }
}

void Tracker::drawSummary(double maxZ, double maxRho, std::string fileName) {
    TCanvas* summaryCanvas;
    TVirtualPad* myPad;
    Int_t irep;
    
    TCanvas* YZCanvas = new TCanvas("YZCanvas", "YZView Canvas", 800, 800 );
    summaryCanvas = new TCanvas("summaryCanvas", "Summary Canvas", 800, 800);
    summaryCanvas->SetFillColor(COLOR_BACKGROUND);
    summaryCanvas->Divide(2, 2);
    summaryCanvas->SetWindowSize(800, 800);
    
    for (int i=1; i<=4; i++) { myPad=summaryCanvas->GetPad(i); myPad->SetFillColor(kWhite);  }
    
    // First pad
    // YZView
    myPad = summaryCanvas->GetPad(1);
    myPad->SetFillColor(COLOR_PLOT_BACKGROUND);
    myPad->cd();
    if (geomLiteYZ_) {
        drawGrid(maxZ, maxRho, ViewSectionYZ);
        geomLiteYZ_->DrawClonePad();
        myPad->GetView()->SetParallel();
        myPad->GetView()->SetRange(0, 0, 0, maxZ, maxZ, maxZ);
        myPad->GetView()->SetView(0 /*long*/, 270/*lat*/, 270/*psi*/, irep);
        drawTicks(myPad->GetView(), maxZ, maxRho, ViewSectionYZ);
        
	YZCanvas->cd();
        myPad = YZCanvas->GetPad(0);
        myPad->SetFillColor(COLOR_PLOT_BACKGROUND);
        drawGrid(maxZ, maxRho, ViewSectionYZ);
        geomLiteYZ_->DrawClonePad();
        myPad->GetView()->SetParallel();
        myPad->GetView()->SetRange(0, 0, 0, maxZ, maxZ, maxZ);
        myPad->GetView()->SetView(0 /*long*/, 270/*lat*/, 270/*psi*/, irep);
        drawTicks(myPad->GetView(), maxZ, maxRho, ViewSectionYZ);
    }
    
    // First pad
    // XYView (barrel)
    myPad = summaryCanvas->GetPad(2);
    myPad->cd();
    myPad->SetFillColor(COLOR_PLOT_BACKGROUND);
    if (geomLiteXY_) {
        drawGrid(maxZ, maxRho, ViewSectionXY);
        geomLiteXY_->DrawClonePad();
        myPad->GetView()->SetParallel();
        myPad->GetView()->SetRange(-maxRho, -maxRho, -maxRho, maxRho, maxRho, maxRho);
        myPad->GetView()->SetView(0 /*long*/, 0/*lat*/, 270/*psi*/, irep);
    }
    
    // Third pad
    // Plots
    myPad = summaryCanvas->GetPad(3);
    myPad->cd();
    myPad->SetFillColor(COLOR_PLOT_BACKGROUND);
    if (etaProfileCanvas_) {
        etaProfileCanvas_->DrawClonePad();
    }
    
    // Fourth pad
    // XYView (EndCap)
    myPad = summaryCanvas->GetPad(4);
    myPad->cd();
    myPad->SetFillColor(COLOR_PLOT_BACKGROUND);
    if (geomLiteEC_) {
        drawGrid(maxZ, maxRho, ViewSectionXY);
        geomLiteEC_->DrawClonePad();
        myPad->GetView()->SetParallel();
        myPad->GetView()->SetRange(-maxRho, -maxRho, -maxRho, maxRho, maxRho, maxRho);
        myPad->GetView()->SetView(0 /*long*/, 0/*lat*/, 270/*psi*/, irep);
    }
    
    
    for (int i=1; i<=4; i++) { myPad=summaryCanvas->GetPad(i); myPad->SetBorderMode(0); }
    
    summaryCanvas->Modified();
    
    std::string pngFileName = fileName+".png";
    std::string YZpngFileName = fileName+"YZ.png";
    std::string svgFileName = fileName+".svg";
    std::string YZsvgFileName = fileName+"YZ.svg";
    //std::string gifFileName = fileName+".gif";
    
    summaryCanvas->SaveAs(pngFileName.c_str());
    summaryCanvas->SaveAs(svgFileName.c_str());
    YZCanvas->SaveAs(YZpngFileName.c_str());
    YZCanvas->SaveAs(YZsvgFileName.c_str());
   

    if (mapPhiEta_) {
      int prevStat = gStyle->GetOptStat();
      gStyle->SetOptStat(0);
      TCanvas* hitMapCanvas = new TCanvas("hitmapcanvas", "Hit Map", 800, 800);
      hitMapCanvas->cd();
      gStyle->SetPalette(1);
      pngFileName = fileName+"_hitmap.png";                                                                                                                                  
      svgFileName = fileName+"_hitmap.svg";
      hitMapCanvas->SetFillColor(COLOR_PLOT_BACKGROUND);
      hitMapCanvas->SetBorderMode(0);
      hitMapCanvas->SetBorderSize(0);
      mapPhiEta_->Draw("colz");
      hitMapCanvas->Modified();
      hitMapCanvas->SaveAs(pngFileName.c_str());
      hitMapCanvas->SaveAs(svgFileName.c_str());
      gStyle->SetOptStat(prevStat);
    }
 
    if (etaProfileCanvas_) {
        summaryCanvas = new TCanvas("etaprofilebig", "big etaprofile plot", 1000, 700);
        summaryCanvas->cd();
        pngFileName = fileName+"_nhitplot.png";
        svgFileName = fileName+"_nhitplot.svg";
        etaProfileCanvas_->DrawClonePad();
        etaProfileCanvas_->SetFillColor(COLOR_PLOT_BACKGROUND);
        //     TFrame* myFrame = summaryCanvas->GetFrame();
        //     if (myFrame) {
        //       myFrame->SetFillColor(kYellow-10);
        //     } else {
        //       std::cerr << "myFrame is NULL" << std::endl;
        //     }
        summaryCanvas->SetFillColor(COLOR_BACKGROUND);
        summaryCanvas->SetBorderMode(0);
        summaryCanvas->SetBorderSize(0);
        summaryCanvas->Modified();
        summaryCanvas->SaveAs(pngFileName.c_str());
        summaryCanvas->SaveAs(svgFileName.c_str());
    }
    
    
    if (bandWidthCanvas_) {
        pngFileName = fileName+"_bandwidth.png";
        svgFileName = fileName+"_bandwidth.svg";
        bandWidthCanvas_->DrawClonePad();
        bandWidthCanvas_->SaveAs(pngFileName.c_str());
        bandWidthCanvas_->SaveAs(svgFileName.c_str());
    }
    
    //summaryCanvas->SaveAs(epsFileName.c_str());
    //summaryCanvas->SaveAs(gifFileName.c_str());
}

void Tracker::drawLayout(double maxZ, double maxRho, std::string fileName) {
    TCanvas* layoutCanvas;
    Int_t irep;
    
    layoutCanvas = new TCanvas("layoutCanvas", "Layout Canvas", 400, 400);
    layoutCanvas->SetFillColor(kWhite);
    layoutCanvas->SetWindowSize(400, 400);
    
    // YZView only is our layout canvas
    layoutCanvas->cd();
    if (geomLiteYZ_) {
        drawGrid(maxZ, maxRho, ViewSectionYZ);
        geomLiteYZ_->DrawClonePad();
        layoutCanvas->GetView()->SetParallel();
        layoutCanvas->GetView()->SetRange(0, 0, 0, maxZ, maxZ, maxZ);
        layoutCanvas->GetView()->SetView(0 /*long*/, 270/*lat*/, 270/*psi*/, irep);
        drawTicks(layoutCanvas->GetView(), maxZ, maxRho, ViewSectionYZ);
    }
    
    layoutCanvas->SetBorderMode(0);
    layoutCanvas->Modified();
    
    layoutCanvas->SaveAs(fileName.c_str());
}


void Tracker::computeBandwidth() {
    LayerVector::iterator layIt;
    ModuleVector::iterator modIt;
    ModuleVector* aLay;
    double hitChannels;
    TLegend* myLegend;
    
    
    bandWidthCanvas_ = new TCanvas("ModuleBandwidthC", "Modules needed bandwidthC", 2000, 1200);
    bandWidthCanvas_->Divide(2, 2);
    bandWidthCanvas_->GetPad(1)->SetLogy(1);
    
    
    chanHitDist_     = new TH1F("NHitChannels",
            "Number of hit channels;Hit Channels;Modules", 200, 0., 400);
    bandWidthDist_   = new TH1F("BandWidthDist",
            "Module Needed Bandwidth;Bandwidth (bps);Modules", 200, 0., 6E+8);
    bandWidthDistSp_ = new TH1F("BandWidthDistSp",
            "Module Needed Bandwidth (sparsified);Bandwidth (bps);Modules", 100, 0., 6E+8);
    bandWidthDistSp_->SetLineColor(kRed);
    
    int nChips;
    for (layIt=layerSet_.begin(); layIt!=layerSet_.end(); layIt++) {
        aLay = (*layIt)->getModuleVector();
        for (modIt=aLay->begin(); modIt!=aLay->end(); modIt++) {
            if ((*modIt)->getReadoutType()==Module::Strip) {
                hitChannels = (*modIt)->getOccupancyPerEvent()*nMB_*((*modIt)->getNChannelsPerFace());
                chanHitDist_->Fill(hitChannels);
                
                for (int nFace=0; nFace<(*modIt)->getNFaces() ; nFace++) {
                    nChips=int(ceil((*modIt)->getNChannelsPerFace()/128.));
                    
                    // TODO: place the computing model choice here
                    
                    // ACHTUNG!!!! whenever you change the numbers here, you have to change
                    // also the numbers in the summary
                    
                    // Binary unsparsified (bps)
                    bandWidthDist_->Fill((16*nChips+(*modIt)->getNChannelsPerFace())*100E3);
                    // Binary sparsified
                    bandWidthDistSp_->Fill((23*nChips+hitChannels*9)*100E3);
                }
            }
        }
    }
    
    bandWidthCanvas_->cd(1);
    bandWidthDist_->Draw();
    bandWidthDistSp_->Draw("same");
    myLegend = new TLegend(0.75, 0.5, 1, .75);
    myLegend->AddEntry(bandWidthDist_, "Unsparsified", "l");
    myLegend->AddEntry(bandWidthDistSp_, "Sparsified", "l");
    myLegend->Draw();
    bandWidthCanvas_->cd(2);
    chanHitDist_->Draw();
    
    savingV_.push_back(bandWidthCanvas_);
    savingV_.push_back(chanHitDist_);
    savingV_.push_back(bandWidthDist_);
    savingV_.push_back(bandWidthDistSp_);
}

double Tracker::getSmallDelta(const int& index) {
  if (specialSmallDelta_[index]==0) {
    return smallDelta_;
  } else {
    return specialSmallDelta_[index];
  }
}

double Tracker::getBigDelta(const int& index) {
  if (specialBigDelta_[index]==0) {
    return bigDelta_;
  } else {
    return specialBigDelta_[index];
  }
}


// private
/**
 * Creates a module type map
 * It sets a different integer for each one
 * @param tracker the tracker to be analyzed
 * @param moduleTypeCount the map to count the different module types
 * @return the total number of module types
 */
int Tracker::createResetCounters(std::map <std::string, int> &moduleTypeCount) {
    ModuleVector result;
    LayerVector::iterator layIt;
    ModuleVector* moduleV;
    ModuleVector::iterator modIt;
    
    std::string aType;
    int typeCounter=0;
    
    for (layIt=layerSet_.begin(); layIt!=layerSet_.end(); layIt++) {
      moduleV = (*layIt)->getModuleVector();
      for (modIt=moduleV->begin(); modIt!=moduleV->end(); modIt++) {
	aType = (*modIt)->getType();
	(*modIt)->resetNHits();
	if (moduleTypeCount.find(aType)==moduleTypeCount.end()) {
	  moduleTypeCount[aType]=typeCounter++;
	}
      }
    }
    
    return(typeCounter);
  }

