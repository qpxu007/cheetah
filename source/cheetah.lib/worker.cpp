/*
 *  worker.cpp
 *  cheetah
 *
 *  Created by Anton Barty on 6/2/11.
 *  Copyright 2011 CFEL. All rights reserved.
 *
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <hdf5.h>
#include <stdlib.h>

#include "cheetah.h"
#include "cheetahmodules.h"
#include "median.h"



/*
 *	Worker thread function for processing each cspad data frame
 */
void *worker(void *threadarg) {

	/*
	 *	Turn threadarg into a more useful form
	 */
	cGlobal			*global;
	cEventData		*eventData;
	eventData = (cEventData*) threadarg;
	global = eventData->pGlobal;
	int	hit = 0;


	long		pix_nn = global->detector[0].pix_nn;
	long		asic_nx = global->detector[0].asic_nx;
	long		asic_ny = global->detector[0].asic_ny;	
	printf("************>>> %li, %li, %li\n", asic_nx, asic_ny, pix_nn);

	
	/*
	 * Nasty fudge for evr41 (i.e. "optical pump laser is on") signal when only 
	 * Acqiris data (i.e. temporal profile of the laser diode signal) is available...
	 * Hopefully this never happens again... 
	 */
	if ( global->fudgeevr41 == 1 ) {
		evr41fudge(eventData,global);	
	}

	/*
	 *	Create arrays for intermediate detector data, etc 
	 */
	DETECTOR_LOOP {
		long	pix_nn = global->detector[detID].pix_nn;
		long	image_nn = global->detector[detID].image_nn;
		long	radial_nn = global->detector[detID].radial_nn;
		
		eventData->detector[detID].corrected_data = (float*) calloc(pix_nn,sizeof(float));
		eventData->detector[detID].corrected_data_int16 = (int16_t*) calloc(pix_nn,sizeof(int16_t));
		eventData->detector[detID].detector_corrected_data = (float*) calloc(pix_nn,sizeof(float));
		eventData->detector[detID].saturatedPixelMask = (int16_t *) calloc(pix_nn,sizeof(int16_t));
		eventData->detector[detID].image = (int16_t*) calloc(image_nn,sizeof(int16_t));
		
		eventData->detector[detID].radialAverage = (float *) calloc(radial_nn, sizeof(float));
		eventData->detector[detID].radialAverageCounter = (float *) calloc(radial_nn, sizeof(float));
	}	
	
	
	
	/*
	 *	Create arrays for remembering Bragg peak data
	 */
	long NpeaksMax = global->hitfinderNpeaksMax;
	eventData->peak_com_index = (long *) calloc(NpeaksMax, sizeof(long));
	eventData->peak_intensity = (float *) calloc(NpeaksMax, sizeof(float));	
	eventData->peak_npix = (float *) calloc(NpeaksMax, sizeof(float));	
	eventData->peak_snr = (float *) calloc(NpeaksMax, sizeof(float));
	eventData->peak_com_x = (float *) calloc(NpeaksMax, sizeof(float));
	eventData->peak_com_y = (float *) calloc(NpeaksMax, sizeof(float));
	eventData->peak_com_x_assembled = (float *) calloc(NpeaksMax, sizeof(float));
	eventData->peak_com_y_assembled = (float *) calloc(NpeaksMax, sizeof(float));
	eventData->peak_com_r_assembled = (float *) calloc(NpeaksMax, sizeof(float));
	eventData->good_peaks = (int *) calloc(NpeaksMax, sizeof(int));
	

	
	/*
	 *	Copy across detector data into corrected array
	 */
    DETECTOR_LOOP {
		for(long i=0;i<global->detector[detID].pix_nn;i++){
			eventData->detector[detID].corrected_data[i] = eventData->detector[detID].raw_data[i];
			eventData->detector[detID].saturatedPixelMask[i] = 1;
		}
	}


	/*
	 *	Create a unique name for this event
	 */
	nameEvent(eventData, global);
	

	/*
	 * Check for saturated pixels, before any other corrections
	 */
    DETECTOR_LOOP {
        if ( global->detector[detID].maskSaturatedPixels == 1 ) 
			checkSaturatedPixels(eventData, global, detID);
	}
	
	/*
	 *	Subtract darkcal image (static electronic offsets)
	 */
    DETECTOR_LOOP {
        if(global->detector[detID].useDarkcalSubtraction) 
            subtractDarkcal(eventData, global, detID);
            //subtractDarkcal(eventData->detector[detID], global->detector[detID]);
	}

	
	/*
	 *	Subtract common mode offsets (electronic offsets)
	 */
	DETECTOR_LOOP {
        if(global->detector[detID].cmModule) 
			cmModuleSubtract(eventData, global, detID);
	}
    DETECTOR_LOOP {
        if(global->detector[detID].cmSubtractUnbondedPixels) 
			cmSubtractUnbondedPixels(eventData, global, detID);
	}
    DETECTOR_LOOP {
        if(global->detector[detID].cmSubtractBehindWires) 
			cmSubtractBehindWires(eventData, global, detID);
	}
	
	/*
	 *	Apply gain correction
	 */
    DETECTOR_LOOP {
        if(global->detector[detID].useGaincal) 
            applyGainCorrection(eventData, global, detID);
            //applyGainCorrection(eventData->detector[detID], global->detector[detID]);
	}

	/*
	 *	Apply bad pixel map
	 */
    DETECTOR_LOOP {
        if(global->detector[detID].useBadPixelMask) 
            applyBadPixelMask(eventData, global, detID);
			//applyBadPixelMask(eventData->detector[detID], global->detector[detID]);
	} 
	
	
	/*
	 *	Recalculate running background from time to time
	 */
    DETECTOR_LOOP {
        if(global->detector[detID].useSubtractPersistentBackground){
            pthread_mutex_lock(&global->bgbuffer_mutex);
            if( ( (global->detector[detID].bgCounter % global->detector[detID].bgRecalc) == 0 || global->detector[detID].bgCounter == global->detector[detID].bgMemory) && global->detector[detID].bgCounter != global->detector[detID].last_bg_update ) {
                    calculatePersistentBackground(global, detID);
            }
            pthread_mutex_unlock(&global->bgbuffer_mutex);
        }
    }	
    
	/*
	 *	Recalculate hot pixel maskfrom time to time
	 */
    DETECTOR_LOOP {
        if(global->detector[detID].useAutoHotpixel) {
            if( ( (global->detector[detID].hotpixCounter % global->detector[detID].hotpixRecalc) == 0 || global->detector[detID].hotpixCounter == global->detector[detID].hotpixMemory) && global->detector[detID].hotpixCounter != global->detector[detID].last_hotpix_update ) {
                    calculateHotPixelMask(global, detID);
            }
        }	
	}
    
	/* 
	 *	Keep memory of data with only detector artefacts subtracted (needed for later reference)
	 */
	DETECTOR_LOOP {
		memcpy(eventData->detector[detID].detector_corrected_data, eventData->detector[detID].corrected_data, global->detector[detID].pix_nn*sizeof(float));
		for(long i=0;i<global->detector[detID].pix_nn;i++){
			eventData->detector[detID].corrected_data_int16[i] = (int16_t) lrint(eventData->detector[detID].corrected_data[i]);
		}
	}

	/*
	 *	Subtract running photon background
	 */
    DETECTOR_LOOP {
        if(global->detector[detID].useSubtractPersistentBackground) 
			subtractPersistentBackground(eventData, global, detID);
	}
	

	/*
	 *	Local background subtraction
	 */
    DETECTOR_LOOP {
        if(global->detector[detID].useLocalBackgroundSubtraction) 
			subtractLocalBackground(eventData, global, detID);
	}
		

	/*
	 *	Identify and remove hot pixels
	 */
    DETECTOR_LOOP {
        if(global->detector[detID].useAutoHotpixel)
			killHotpixels(eventData, global, detID);
	}
    DETECTOR_LOOP {
        if(global->detector[detID].useBadPixelMask) 
            applyBadPixelMask(eventData, global, detID);
	} 
		

	/*
	 *	Skip first set of frames to build up running estimate of background...
	 */
    DETECTOR_LOOP {
        if (eventData->threadNum < global->detector[detID].startFrames || 
            (global->detector[detID].useSubtractPersistentBackground && global->detector[detID].bgCounter < global->detector[detID].bgMemory) || 
            (global->detector[detID].useAutoHotpixel && global->detector[detID].hotpixCounter < global->detector[detID].hotpixRecalc) ) {
                    updateBackgroundBuffer(eventData, global, detID); 
                printf("r%04u:%li (%3.1fHz): Digesting initial frames\n", global->runNumber, eventData->threadNum,global->datarate);
                goto cleanup;
        }
	}
    
    
	/*
	 *	Hitfinding
	 */
	if(global->hitfinder){
		hit = hitfinder(eventData, global, 0);
		eventData->hit = hit;
	}
	
	
	/*
	 *	Update running backround estimate based on non-hits
	 */
    DETECTOR_LOOP {
        if (global->detector[detID].useSubtractPersistentBackground) 
            if (hit==0 || global->detector[detID].bgIncludeHits) {
                updateBackgroundBuffer(eventData, global, detID); 
        }		
    }
	
	/*
	 *	Revert to detector-corrections-only data if we don't want to export data with photon bacground subtracted
	 */
    DETECTOR_LOOP {
        if(global->detector[detID].saveDetectorCorrectedOnly) 
			memcpy(eventData->detector[detID].corrected_data, eventData->detector[detID].detector_corrected_data, global->detector[detID].pix_nn*sizeof(float));
	}
	
	/*
	 *	If using detector raw, do it here
	 */
    DETECTOR_LOOP {
        if(global->detector[detID].saveDetectorRaw) 
			for(long i=0;i<global->detector[detID].pix_nn;i++)
				eventData->detector[detID].corrected_data[i] = eventData->detector[detID].raw_data[i];
	}
	
	
	/*
	 *	Keep int16 copy of corrected data (needed for saving images)
	 */
	DETECTOR_LOOP {
		for(long i=0;i<global->detector[detID].pix_nn;i++){
			eventData->detector[detID].corrected_data_int16[i] = (int16_t) lrint(eventData->detector[detID].corrected_data[i]);
		}
	}
	
	
	/*
	 *	Assemble quadrants into a 'realistic' 2D image
	 */
	DETECTOR_LOOP
		assemble2Dimage(eventData, global, detID);

	
	/*
	 *	Calculate radial average
	 */
	DETECTOR_LOOP
		calculateRadialAverage(eventData->detector[detID].corrected_data, eventData->detector[detID].radialAverage, eventData->detector[detID].radialAverageCounter, global, detID);
	
	
	
	/*
	 *	Maintain a running sum of data (powder patterns)
	 */
    if(hit && global->powderSumHits) {
        DETECTOR_LOOP
            addToPowder(eventData, global, hit, detID);
    }
    if(!hit && global->powderSumBlanks){
        DETECTOR_LOOP
            addToPowder(eventData, global, hit, detID);
    } 
    if(global->generateDarkcal || global->generateGaincal){
        DETECTOR_LOOP
            addToPowder(eventData, global, 0, detID);
    } 
    
    
    /*
     *  Maintain radial average stack
     */
    if(global->saveRadialStacks) {
        DETECTOR_LOOP
            addToRadialAverageStack(eventData, global, hit, detID);
    }
	
	
	/*
	 *	If this is a hit, write out to our favourite HDF5 format
	 */
	save:
	if(hit && global->savehits) {
		writeHDF5(eventData, global);
        printf("r%04u:%li (%2.1f Hz): Writing data to: %s\n",global->runNumber, eventData->threadNum,global->datarate, eventData->eventname);
    }

	else if((global->hdf5dump > 0) && ((eventData->threadNum % global->hdf5dump) == 0)) {        
		writeHDF5(eventData, global);
        printf("r%04u:%li (%2.1f Hz): Writing data to: %s\n",global->runNumber, eventData->threadNum,global->datarate, eventData->eventname);
    }
	else
		printf("r%04u:%li (%3.1fHz): Processed (npeaks=%i)\n", global->runNumber,eventData->threadNum,global->datarate, eventData->nPeaks);

	
	/*
	 *	If this is a hit, write out peak info to peak list file
	 */
	if(hit && global->savePeakInfo) {
		writePeakFile(eventData, global);
	}

	
	

	/*
	 *	Write out information on each frame to a log file
	 */
	pthread_mutex_lock(&global->framefp_mutex);
	fprintf(global->framefp, "%s, %li, %i, %g, %g, %g, %g, %g, %d, %d, %g, %g, %g, %d, %g, %d\n", eventData->eventname, eventData->threadNum, eventData->hit, eventData->photonEnergyeV, eventData->wavelengthA, eventData->gmd1, eventData->gmd2, eventData->detector[0].detectorZ, eventData->nPeaks, eventData->peakNpix, eventData->peakTotal, eventData->peakResolution, eventData->peakDensity, eventData->laserEventCodeOn, eventData->laserDelay, eventData->samplePumped);
	pthread_mutex_unlock(&global->framefp_mutex);
	
	// Keep track of what has gone into each image class
	pthread_mutex_lock(&global->powderfp_mutex);
	fprintf(global->powderlogfp[hit], "%s, %li, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g\n", eventData->eventname, eventData->threadNum, eventData->photonEnergyeV, eventData->wavelengthA, eventData->detector[0].detectorZ, eventData->gmd1, eventData->gmd2, eventData->nPeaks, eventData->peakNpix, eventData->peakTotal, eventData->peakResolution, eventData->peakDensity, eventData->laserEventCodeOn, eventData->laserDelay);
	pthread_mutex_unlock(&global->powderfp_mutex);
	
	
	/*
	 *	Cleanup and exit
	 */
	cleanup:
	// Decrement thread pool counter by one
	pthread_mutex_lock(&global->nActiveThreads_mutex);
	global->nActiveThreads -= 1;
	pthread_mutex_unlock(&global->nActiveThreads_mutex);
	
    
	// Free memory only if running multi-threaded
    if(eventData->useThreads == 1) {
        cheetahDestroyEvent(eventData);
        pthread_exit(NULL);
    }
    else {
        return(NULL);
    }
}


/*
 * Nasty little bit of code that aims to toggle the evr41 signal based on the Acqiris
 * signal.  Very simple: scan along the Acqiris trace (starting from the ini keyword 
 * hitfinderTOFMinSample and ending at hitfinderTOFMaxSample) and check that there is 
 * at least one sample above the threshold set by the hitfinderTOFThresh keyword. Oh,
 * and don't forget to specify the Acqiris channel with:
 * tofChannel=0
 * And you'll need to do this as well:
 * fudgeevr41=1
 * tofName=CxiSc1
 *
 * Agreed - doesn't sound very robust.  As far as I can tell at the moment, only a single
 * sample rises above threshold when the laser trigger is on... maybe bad sampling interval?
 * Or, I could be wrong...
 * 
 * -Rick  
 */
void evr41fudge(cEventData *t, cGlobal *g){
	
	if ( g->TOFPresent == 0 ) {
		//printf("Acqiris not present; can't fudge EVR41...\n");
		return;
	}
 
	int nCh = g->AcqNumChannels;
	int nSamp = g->AcqNumSamples;
	double * Vtof = t->TOFVoltage;
	int i;
	double Vtot = 0;
	double Vmax = 0;
	int tCounts = 0;
	for(i=g->hitfinderTOFMinSample; i<g->hitfinderTOFMaxSample; i++){
	//for ( i=0; i<nSamp; i++){
		Vtot += Vtof[i];
		if ( Vtof[i] > Vmax ) Vmax = Vtof[i];
		if ( Vtof[i] >= g->hitfinderTOFThresh ) tCounts++;
		//printf("%f\n",Vtof[i]);
	}
	
	//printf("================================================================\n"); 
	//printf("tCounts = %d\n",tCounts);
	/*
	if ( t->laserEventCodeOn ) {
		printf("%d channels, %d samples, Vtot = %f, Vmax = %f, evr41 = 1\n",nCh,nSamp,Vtot,Vmax);
	} else {
		printf("%d channels, %d samples, Vtot = %f, Vmax = %f, evr41 = 0\n",nCh,nSamp,Vtot,Vmax);
	}
	*/

	bool acqLaserOn = false;
	if ( tCounts >= 1 ) {
		acqLaserOn = true;
	}
	//if ( acqLaserOn ) printf("acqLaserOn = true\n"); else printf("acqLaserOn = false\n");
	//if ( t->laserEventCodeOn ) printf("laserEventCodeOn = true\n"); else printf("laserEventCodeOn = false\n");
	if ( acqLaserOn != t->laserEventCodeOn ) {
		if ( acqLaserOn ) {
			printf("MESSAGE: Acqiris and evr41 disagree.  We trust acqiris (set evr41 = 1 )\n");
		} else {
			printf("MESSAGE: Acqiris and evr41 disagree.  We trust acqiris (set evr41 = 0 )\n");
		}
		t->laserEventCodeOn = acqLaserOn;
	}
	//printf("================================================================\n");
}


	

/*
 *	Interpolate raw (corrected) cspad data into a physical 2D image
 *	using pre-defined pixel mapping (as loaded from .h5 file)
 */
void assemble2Dimage(cEventData *eventData, cGlobal *global, int detID){
	
	// Dereference datector arrays
	long		pix_nn = global->detector[detID].pix_nn;
	long		image_nx = global->detector[detID].image_nx;
	long		image_nn = global->detector[detID].image_nn;
	float		*pix_x = global->detector[detID].pix_x;
	float		*pix_y = global->detector[detID].pix_y;
	float		*corrected_data = eventData->detector[detID].corrected_data;
	int16_t		*image = eventData->detector[detID].image;

	
	// Allocate temporary arrays for pixel interpolation (needs to be floating point)
	float	*data = (float*) calloc(image_nn,sizeof(float));
	float	*weight = (float*) calloc(image_nn,sizeof(float));
	for(long i=0; i<image_nn; i++){
		data[i] = 0;
		weight[i]= 0;
	}
	
	
	// Loop through all pixels and interpolate onto regular grid
	float	x, y;
	float	pixel_value, w;
	long	ix, iy;
	float	fx, fy;
	long	image_index;

	for(long i=0;i<pix_nn;i++){
		// Pixel location with (0,0) at array element (0,0) in bottom left corner
		x = pix_x[i] + image_nx/2;
		y = pix_y[i] + image_nx/2;
		pixel_value = corrected_data[i];
		
		// Split coordinate into integer and fractional parts
		ix = (long) floor(x);
		iy = (long) floor(y);
		fx = x - ix;
		fy = y - iy;
		
		// Interpolate intensity over adjacent 4 pixels using fractional overlap as the weighting factor
		// (0,0)
		if(ix>=0 && iy>=0 && ix<image_nx && iy<image_nx) {
			w = (1-fx)*(1-fy);
			image_index = ix + image_nx*iy;
			data[image_index] += w*pixel_value;
			weight[image_index] += w;
		}
		// (+1,0)
		if((ix+1)>=0 && iy>=0 && (ix+1)<image_nx && iy<image_nx) {
			w = (fx)*(1-fy);
			image_index = (ix+1) + image_nx*iy;
			data[image_index] += w*pixel_value;
			weight[image_index] += w;
		}
		// (0,+1)
		if(ix>=0 && (iy+1)>=0 && ix<image_nx && (iy+1)<image_nx) {
			w = (1-fx)*(fy);
			image_index = ix + image_nx*(iy+1);
			data[image_index] += w*pixel_value;
			weight[image_index] += w;
		}
		// (+1,+1)
		if((ix+1)>=0 && (iy+1)>=0 && (ix+1)<image_nx && (iy+1)<image_nx) {
			w = (fx)*(fy);
			image_index = (ix+1) + image_nx*(iy+1);
			data[image_index] += w*pixel_value;
			weight[image_index] += w;
		}
	}
	
	
	// Reweight pixel interpolation
	for(long i=0; i<image_nn; i++){
		if(weight[i] < 0.05)
			data[i] = 0;
		else
			data[i] /= weight[i];
	}

	

	// Check for int16 overflow
	for(long i=0;i<image_nn;i++){
		if(lrint(data[i]) > 32767) 
			data[i]=32767;
		if(lrint(data[i]) < -32767) 
			data[i]=-32767;
	}
	
	
	// Copy interpolated image across into int_16 image array
	for(long i=0;i<image_nn;i++){
		image[i] = (int16_t) lrint(data[i]);
	}
	
	
	// Free temporary arrays
	free(data);
	free(weight);
	
}