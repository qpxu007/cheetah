
/*
 *  saveCXI.cpp
 *  cheetah
 *
 *  Create by Jing Liu on 05/11/12.
 *  Copyright 2012 Biophysics & TDB @ Uppsala University. All rights reserved.
 *
 */
										
#include <string>
#include <vector>
#include <pthread.h>
#include <math.h>
#include <fstream> 
#include <unistd.h>

#include <saveCXI.h>

namespace CXI{
	
	Node *Node::createDataset(const char *s, hid_t dataType, hsize_t width, hsize_t height,
							   hsize_t length, hsize_t stackSize, int chunkSize, int heightChunkSize, const char *userAxis){
		hid_t loc = hid();

		// Exception for char arrays
		if(dataType == H5T_NATIVE_CHAR){
			dataType = H5Tcopy(H5T_C_S1);
			if(H5Tset_size(dataType, width) < 0){
				ERROR("Cannot set type size.\n");
			}
			width = height;
			height = 0;
		}

		
		// Count dimensions
		// First right shift the dimensions
		hsize_t *dimsP[4] = {&width, &height, &length, &stackSize};
		for (int i=2; i>=0; i--){
			for (int j=i+1; j<4; j++){
				if(*dimsP[j]==0){
					*dimsP[j]=*dimsP[j-1];
					*dimsP[j-1]=0;
				}
			}
		}
		// Count dimensions from dimsP
		int ndims = 0;
		for (int i=0; i<4; i++){
			if (*dimsP[i]>0){
				ndims++;
			}
		}

		// Make sure every dimension has at least length 1
		if(length == 0){
			length = 1;
		}
		if(height == 0){
			height = 1;
		}
		if(width == 0){
			width = 1;
		}
		// Build the dimensions list
		hsize_t dims[4] = {0, length, height, width};

		
		// Define the default chunk size for stacks
		if(stackSize == H5S_UNLIMITED && chunkSize <= 0){
			chunkSize = CXI::chunkSize1D;
			if(ndims == 3){
				chunkSize = CXI::chunkSize2D;
			}
			else if(ndims == 4){
				chunkSize = CXI::chunkSize2D;
			}
		}

		// Calculate initial array dimensions based on chunk size
		if(heightChunkSize == 0){
			dims[0] = lrintf(((float)chunkSize)/H5Tget_size(dataType)/height/length);
		}
		else {
			dims[0] = chunkSize/heightChunkSize;
			dims[1] = lrintf(((float)heightChunkSize)/H5Tget_size(dataType)/height);
		}

		if(!chunkSize){
			if(stackSize == 0){
				stackSize = 1;
			}
			dims[0] = stackSize;
		}

		// Make sure the size is not 0
		if(dims[0] == 0){
			dims[0] = 1;
		}

		printf("    + %s (%iD: %llu x %llu x %llu x %llu)\n",s, ndims, dims[0], dims[1], dims[2], dims[3]);
		hsize_t maxdims[4] = {stackSize,length,height,width};
		

		// Set chunk size array
		// 1D stacks will have chunkSize1D, Stacks (H5S_UNLIMITED) will be read in slices but 2D images (not H5S_UNLIMITED) will not be read as a stack
		hsize_t chunkdims[4] = {dims[0], dims[1], dims[2], dims[3]};
		if(stackSize == H5S_UNLIMITED && ndims >= 2) chunkdims[0] = 1;
		
		
		
		hid_t dataspace = H5Screate_simple(ndims, dims, maxdims);
		if( dataspace<0 ) {ERROR("Cannot create dataspace.\n");}

		// Set chunking and compression (only makes sense for datasets of dimension > 2)
		// (optimise for reading one event at a time, ie: avoid decompressing multiple frames to read one)
		hid_t cparms = H5Pcreate(H5P_DATASET_CREATE);
		if(chunkSize) {
			H5Pset_chunk(cparms, ndims, chunkdims);
			if (ndims >= 3 && CXI::h5compress != 0) {
				H5Pset_deflate(cparms, CXI::h5compress);
			}
		}
		
		// Set optimal chunk cache size
		hid_t dapl_id = H5Pcreate(H5P_DATASET_ACCESS);
		if(chunkSize && ndims >= 2) {
			//long	opt_cachesize = 1024*1024*16;
			long	opt_cachesize = 4*chunkdims[0]*chunkdims[1]*chunkdims[2]*chunkdims[3]*H5Tget_size(dataType);
			H5Pset_chunk_cache(dapl_id,H5D_CHUNK_CACHE_NSLOTS_DEFAULT,opt_cachesize,1);
		}
		
		// Create data set
		hid_t dataset = H5Dcreate(loc, s, dataType, dataspace, H5P_DEFAULT, cparms, dapl_id);
		if( dataset<0 ) {ERROR("Cannot create dataset.\n");}
		
		H5Sclose(dataspace);
		H5Pclose(cparms);		
		H5Pclose(dapl_id);		


		if(stackSize == H5S_UNLIMITED){
			addStackAttributes(dataset,ndims,userAxis);
		}
		return addNode(s, dataset, Dataset);    
	}

	
	H5T_conv_ret_t handle_conversion_exceptions( H5T_conv_except_t except_type, hid_t , hid_t,
												 void *, void *, void *op_data){
		int ignoreFlags = *((int *)op_data);
		if(except_type == H5T_CONV_EXCEPT_RANGE_HI ||
		   except_type == H5T_CONV_EXCEPT_RANGE_LOW){
			if((ignoreFlags & IgnoreOverflow) == 0){							
				printf("WARNING: Datatype conversion exception (overflow)!\n");
				printf("Changing dataSaveFormat in cheetah.ini to a larger type might avoid the exception.\n");
				printf("Add ignoreConversionOverflow=1 to your cheetah.ini to supress this warning\n");
			}
		}
		if(except_type == H5T_CONV_EXCEPT_TRUNCATE){
			if((ignoreFlags & IgnoreTruncate) == 0){							
				printf("WARNING: Datatype conversion exception (truncation of fractional part)!\n");
				printf("Changing dataSaveFormat in cheetah.ini to float might avoid the exception.\n");
				printf("Add ignoreConversionTruncate=1 to your cheetah.ini to supress this warning\n");
			}
		}
		if(except_type == H5T_CONV_EXCEPT_PRECISION){
			if((ignoreFlags & IgnorePrecision) == 0){							
				printf("WARNING: Datatype conversion exception (loss of precision)!\n");
				printf("Changing dataSaveFormat in cheetah.ini to a larger type might avoid the exception.\n");
				printf("Add ignoreConversionPrecision=1 to your cheetah.ini to supress this warning\n");
			}
		}
		if(except_type == H5T_CONV_EXCEPT_PINF ||
				 except_type == H5T_CONV_EXCEPT_NINF){
			if((ignoreFlags & IgnoreNAN) == 0){							
				printf("WARNING: Datatype conversion exception (input is infinity)!\n");
				printf("Add ignoreConversionNAN=1 to your cheetah.ini to supress this warning\n");
			}
		}
		if(except_type == H5T_CONV_EXCEPT_NAN){
			if((ignoreFlags & IgnoreNAN) == 0){							
				printf("WARNING: Datatype conversion exception (input is NAN)!\n");
				printf("Add ignoreConversionNAN=1 to your cheetah.ini to supress this warning\n");
			}
		}
		return H5T_CONV_UNHANDLED;
	}


	template <class T> 
	void Node::write(T *data, int stackSlice, int sliceSize, bool variableSlice){
		bool sliced = true;
		if(stackSlice == -1){
			stackSlice = 0;
			sliced = false;
		}

		hid_t hs,w;
		hsize_t count[4] = {1,1,1,1};
		hsize_t offset[4] = {static_cast<hsize_t>(stackSlice),0,0,0};
		/* stride is irrelevant in this case */
		hsize_t stride[4] = {1,1,1,1};
		hsize_t block[4];
		/* dummy */
		hsize_t mdims[4];
		hid_t dataset = hid();
		/* Use the existing dimensions as block size */
		hid_t dataspace = H5Dget_space(dataset);
		if( dataspace<0 ) {ERROR("Cannot get dataspace.\n");}
		int ndims = H5Sget_simple_extent_ndims(dataspace);
		H5Sget_simple_extent_dims(dataspace, block, mdims);
		
		/*
		 * check if we need to extend the dataset 
		 */
		if(ndims > 0 && (int)block[0] <= stackSlice){
			while((int)block[0] <= stackSlice){
				if(block[0] < 1024) {
					block[0] *= 2;
				}
				else {
					block[0] += 1024;
				}
			}
			H5Dset_extent (dataset, block);
			/* get enlarged dataspace */
			H5Sclose(dataspace);
			dataspace = H5Dget_space (dataset);
			if( dataspace<0 ) {ERROR("Cannot get dataspace.\n");}
		}
		if(sliced){
			block[0] = 1;
		}

		/* 
		 *	check if we need to extend the dataset in the second dimension 
		 */
		if(variableSlice && (int)block[1] <= sliceSize){
			int tmp_block = block[0];
			H5Sget_simple_extent_dims(dataspace, block, mdims);
			while((int)block[1] <= sliceSize){
				if(block[1] < 1024) {
					block[1] *= 2;
				}
				else {
					block[1] += 1024;
				}
			}
			H5Dset_extent (dataset, block);
			block[0] = tmp_block;
			/* get enlarged dataspace */
			H5Sclose(dataspace);
			dataspace = H5Dget_space (dataset);
			if( dataspace<0 ) {ERROR("Cannot get dataspace.\n");}
		}
		if(variableSlice){
			block[1] = sliceSize;
		}

		if(!variableSlice && (sliceSize != 0)) {
			// Check whether given sliceSize extends the size of the dataspace
			int ds_sliceSize = 0;
			if (ndims > 0) {
				ds_sliceSize = 1;
				for (int i=0; i<ndims; i++) {
					ds_sliceSize *=  ((int) block[i]);
				}
			}
			if (ds_sliceSize != sliceSize) {
				ERROR("Trying to write slice of %i elements to a dataset that was allocated for slices of a size of %i elements.",sliceSize,ds_sliceSize);
			}
		}

		hid_t memspace = H5Screate_simple (ndims, block, NULL);
		hid_t type = get_datatype(data);
		if(type == H5T_NATIVE_CHAR){
			type = H5Dget_type(dataset);
		}
		if (sliced){
			hs = H5Sselect_hyperslab (dataspace, H5S_SELECT_SET, offset,stride, count, block);
			if( hs<0 ) {
				ERROR("Cannot select hyperslab.\n");
			}
		}

		hid_t xfer_plist_id = H5Pcreate(H5P_DATASET_XFER);
		H5Pset_type_conv_cb(xfer_plist_id, handle_conversion_exceptions, &ignoreConversionExceptions);


		w = H5Dwrite (dataset, type, memspace, dataspace, xfer_plist_id, data);
		if( w<0 ){
 			ERROR("Cannot write to file.\n");
		}
		if(sliced){
			writeNumEvents(dataset,stackSlice);
		}
		H5Sclose(memspace);
		H5Sclose(dataspace);
		H5Pclose(xfer_plist_id);
	}

	Node * Node::addClass(const char * s){
		std::string key = nextKey(s);
		return createGroup(key.c_str());
	}
    Node * Node::addCXIClass(const char * s){
        std::string key = nextCXIKey(s);
        return createGroup(key.c_str());
    }

    std::string Node::nextKey(const char * s){
        int i = 0;
        char buffer[1024];
        for(;;i++){
            //sprintf(buffer,"%s%d",s,i);
            sprintf(buffer,"%s%d",s,i);
            if(children.find(buffer) == children.end()){
                break;
            }
        }
        return std::string(buffer);
    }
    std::string Node::nextCXIKey(const char * s){
        int i = 1;
        char buffer[1024];
        for(;;i++){
            //sprintf(buffer,"%s%d",s,i);
            sprintf(buffer,"%s_%d",s,i);
            if(children.find(buffer) == children.end()){
                break;
            }
        }
        return std::string(buffer);
    }
    
	Node * Node::createGroup(const char * s){
		hid_t gid = H5Gcreate(hid(),s, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		if(gid < 0){
			return NULL;
		}
		return addNode(s,gid, Group);
	}
    
	Node * Node::createGroup(const char * prefix, int n){
		char buffer[1024];
		sprintf(buffer,"%s%d",prefix,n);
		hid_t gid = H5Gcreate(hid(),buffer, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
		if(gid < 0){
			return NULL;
		}
		return addNode(buffer,gid, Group);
	}
    // CXI format specifies underscore and starting from 1 not 0
    // So we have a duplicate set of entries here to follow that format
    Node * Node::createCXIGroup(const char * prefix, int n){
        char buffer[1024];
        sprintf(buffer,"%s_%d",prefix,n);
        hid_t gid = H5Gcreate(hid(),buffer, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        if(gid < 0){
            return NULL;
        }
        return addNode(buffer,gid, Group);
    }
    

    Node * Node::addClassLink(const char * s, std::string target){
        std::string key = nextKey(s);
        return createLink(key.c_str(), target);
    }
    Node * Node::addCXIClassLink(const char * s, std::string target){
        std::string key = nextCXIKey(s);
        return createLink(key.c_str(), target);
    }

    

	Node * Node::addDatasetLink(const char * s, std::string target){
		char buffer[1024];
		sprintf(buffer,"%s/%s",target.c_str(),s);
		hid_t lid = H5Lcreate_soft(buffer,hid(),s,H5P_DEFAULT,H5P_DEFAULT);
		if(lid < 0){
			return NULL;
		}
		return addNode(s, lid, Link);
	}

	Node * Node::createLink(const char * s, std::string target){
		hid_t lid = H5Lcreate_soft(target.c_str(),hid(),s,H5P_DEFAULT,H5P_DEFAULT);
		if(lid < 0){
			return NULL;
		}
		return addNode(s, lid, Link);
	}

	void Node::closeAll(){
		// close all non-root open objects
		if(parent && hid() >= 0 && type != Link){
			H5Oclose(hid());
			id = -1;
		}
		for(Iter it = children.begin(); it != children.end(); it++) {
			it->second->closeAll();
		}
	}

	void Node::openAll(){
		if(parent && parent->hid() < 0){
			ERROR("Parent not open");
		}
		// open all non-root closed objects
		if(hid() < 0 && parent && type != Link){
			id = H5Oopen(parent->hid(),name.c_str(),H5P_DEFAULT);
		}
		for(Iter it = children.begin(); it != children.end(); it++) {
			it->second->openAll();
		}
	}

	std::string Node::path(){
		if(parent){
			return parent->path()+std::string("/")+name;
		}
		else{
			return name;
		}
	}

	Node & Node::child(std::string prefix, int n){
		char buffer[1024];
		sprintf(buffer,"%s%d",prefix.c_str(),n);
		return (*this)[buffer];
	}

    
    Node & Node::cxichild(std::string prefix, int n){
        char buffer[1024];
        sprintf(buffer,"%s_%d",prefix.c_str(),n);
        return (*this)[buffer];
    }

    
	void Node::trimAll(int stackSize){
		if(stackSize < 0){
			stackSize = stackCounter;
		}


		if(hid() >= 0 && type == Dataset){
			hsize_t block[4];
			hsize_t mdims[4];
			int ndims;
			hid_t dataspace = H5Dget_space(hid());
			if( dataspace<0 ) {ERROR("Cannot get dataspace.\n");}
			
			H5Sget_simple_extent_dims(dataspace, block, mdims);
			ndims = H5Sget_simple_extent_ndims(dataspace);
			if(ndims > 0 && mdims[0] == H5S_UNLIMITED){
				writeNumEvents(hid(), stackSize);
				block[0] = stackSize;
				H5Dset_extent(hid(), block);
			}
		}

		for(Iter it = children.begin(); it != children.end(); it++) {
			it->second->trimAll(stackSize);
		}
	}


	Node * Node::addNode(const char * s, hid_t oid, Type t){
		Node * n = new Node(s, oid, this, t, ignoreConversionExceptions);
		children[s] = n;
		return n;
	}


	void Node::addStackAttributes(hid_t dataset, int ndims, const char * userAxis){
		const char * axis_1d = "experiment_identifier";
		const char * axis_2d = "experiment_identifier:coordinate";
		const char * axis_3d = "experiment_identifier:y:x";
		const char * axis_4d = "experiment_identifier:module_identifier:y:x";
		const char * axis;
		if(userAxis != NULL){
			axis = userAxis;
		}
		else if(ndims == 1){
			axis = axis_1d;
		}
		else if(ndims == 2){
			axis = axis_2d;
		}
		else if(ndims == 3){
			axis = axis_3d;
		}
		else if(ndims == 4){
			axis = axis_4d;
	    }
		
		hsize_t one = 1;
		hid_t datatype = H5Tcopy(H5T_C_S1);
		H5Tset_size(datatype, strlen(axis));
		hid_t memspace = H5Screate_simple(1,&one,NULL);
		hid_t attr = H5Acreate(dataset,"axes",datatype,memspace,H5P_DEFAULT,H5P_DEFAULT);
		H5Awrite(attr,datatype,axis);
		H5Aclose(attr);
		attr = H5Acreate(dataset,CXI::ATTR_NAME_NUM_EVENTS,H5T_NATIVE_INT32,memspace,H5P_DEFAULT,H5P_DEFAULT);
		int zero = 0;
		H5Awrite(attr,H5T_NATIVE_INT32,&zero);
		H5Tclose(datatype);
		H5Aclose(attr);
		H5Sclose(memspace);
	}

	hid_t Node::writeNumEvents(hid_t dataset, int stackSlice){
		hid_t a = H5Aopen(dataset, CXI::ATTR_NAME_NUM_EVENTS, H5P_DEFAULT);
		hid_t w = -1;
		if(a>=0) {
			w = H5Awrite (a, H5T_NATIVE_INT32, &stackSlice);
			H5Aclose(a);
		}
		return w;
	}

	template <class T>
	hid_t Node::get_datatype(const T *){
		hid_t datatype = 0;
		if(typeid(T) == typeid(bool) && sizeof(bool) == 1){
			datatype = H5T_NATIVE_INT8;
		}
		else if(typeid(T) == typeid(short)){
			datatype = H5T_NATIVE_INT16;
		}
		else if(typeid(T) == typeid(unsigned short)){
			datatype = H5T_NATIVE_UINT16;
		}
		else if((typeid(T) == typeid(int))) {
			datatype = H5T_NATIVE_INT32;
		}
		else if(typeid(T) == typeid(unsigned int)){
			datatype = H5T_NATIVE_UINT32;
		}
		else if(typeid(T) == typeid(long)){
			datatype = H5T_NATIVE_LONG;
		}
		else if(typeid(T) == typeid(unsigned long)){
			datatype = H5T_NATIVE_ULONG;
		}
		else if(typeid(T) == typeid(float)){
			datatype = H5T_NATIVE_FLOAT;
		}
		else if(typeid(T) == typeid(double)){
			datatype = H5T_NATIVE_DOUBLE;
		}
		else if(typeid(T) == typeid(char)){
			datatype = H5T_NATIVE_CHAR;
		}
		else{
			ERROR("Do not understand type: %s",typeid(T).name());
		}
		return datatype;
	}

	
	// Needs global->saveCXI_mutex unlocked
	uint Node::getStackSlice(){
		#ifdef __GNUC__
		return __sync_fetch_and_add(&stackCounter,1);
		#else
		pthread_mutex_lock(&global->saveCXI_mutex);
		uint ret = stackCounter;
		cxi->stackCounter++;
		pthread_mutex_unlock(&global->saveCXI_mutex);
		return ret;
		#endif
    }

    // Manually set stack slice (used for synchronising stack position across multiple files)
    void Node::setStackSlice(uint slice){
        stackCounter = slice;
    }

}




template <class T>
static T * generateThumbnail(const T * src,const int srcWidth, const int srcHeight, const int scale)
{
	int dstWidth = srcWidth/scale;
	int dstHeight = srcHeight/scale;
	T * dst = new T [srcWidth*srcHeight];
	for(int x = 0; x <dstWidth; x++){
		for(int y = 0; y<dstHeight; y++){
			double res=0;
			for (int xx = x*scale; xx <x*scale+scale; xx++){
				for(int yy = y*scale; yy <y*scale+scale; yy++){
					res += src[yy*srcWidth+xx];
				} 
			}
			dst[y*dstWidth+x] = (short int) res/(scale*scale);
		}
	}
	return dst;
}


/*

  CXI file skeleton

  |
  LCLS
  |  |
  |  |-... (just copied from XTC)
  |
  entry_1
  |
  |- data_1 ---------\
  |                  |
  |- instrument_1    | symlink
  |   |              |
  |   |-detector_1 <---------------------------------------\
  |   |   |                                                |
  |   |   |- data [non-assembled data, 3D array]           |
  |   |   |- (mask) [non-asslembled masks, 3D array]       |
  |   |   |- mask_shared [non-assembled mask, 2D array]    | 
  |   |   |- ...                                           | symlink
  |   .   .                                                |
  |                                                        |
  |- (image_1)                                             |
  |    |                                                   |
  |    |- detector_1 --------------------------------------/
  |    |- data [assembled data, 3D array]        |
  |    |- (mask) [assembled masks, 3D array]     |
  |    |- mask_shared [assembled mask, 2D array] |
  |    |- ...                                    |
  |    .                                         |
  |                                              |
  |- (image_2)                                   |
  |    |                                         |
  |    |- detector_1 ----------------------------
  |    |- data [downsampled assembled data, 3D array]
  |    |- (mask) [downsampled assembled masks, 3D array]
  |    |- mask_shared [downsampled assembled mask, 2D array] 
  |    |- ...
  .    .
 
*/



/*
 *	Create the initial skeleton for the CXI file.
 *  We'll rely on HDF5 automatic error reporting. It's usually loud enough.
 */
//long    cxiLegacyFileFormat=0;
//bool    cxiSaveFrames;

static CXI::Node *createCXISkeleton(const char *filename, cGlobal *global){
	
	//pthread_mutex_lock(&global->saveCXI_mutex);
    int debugLevel = global->debugLevel;
	DEBUGL2_ONLY{ DEBUG("Create Skeleton."); }

    using CXI::Node;
	CXI::h5compress = global->h5compress;

    // Conversion flags
	int ignoreConversionFlags = 0;
	if(global->ignoreConversionOverflow){
		ignoreConversionFlags |= CXI::IgnoreOverflow;
	}
	if(global->ignoreConversionTruncate){
		ignoreConversionFlags |= CXI::IgnoreTruncate;
	}
	if(global->ignoreConversionPrecision){
		ignoreConversionFlags |= CXI::IgnorePrecision;
	}
	if(global->ignoreConversionNAN){
		ignoreConversionFlags |= CXI::IgnoreNAN;
	}

    // Check what data type format we want to save things in. Defaults to float
    hid_t h5type = H5T_NATIVE_FLOAT;
    if(!strcasecmp(global->dataSaveFormat,"INT16")){
        h5type = H5T_STD_I16LE;
    }
    else if(!strcasecmp(global->dataSaveFormat,"INT32")){
        h5type = H5T_STD_I32LE;
    }
    else if(!strcasecmp(global->dataSaveFormat,"float")){
        h5type = H5T_NATIVE_FLOAT;
    }

    // Create root node
    CXI::Node *root = new Node(filename,global->cxiSWMR,ignoreConversionFlags);
    char sBuffer[1024];

    
    // Create top level entries
	Node *entry = root->addCXIClass("entry");
	Node *instrument = entry->addCXIClass("instrument");
	Node *source = instrument->addCXIClass("source");

    source->createStack("energy",H5T_NATIVE_DOUBLE);
    source->createLink("experiment_identifier", "/entry_1/experiment_identifier");
    entry->createStack("experiment_identifier",H5T_NATIVE_CHAR,CXI::stringSize);


    
    /*
     *  Write instrument information
     */
    Node *facility = root->createGroup("instrument");

    // LCLS
    if(!strcmp(global->facility, "LCLS") ) {
        //Node *lcls = root->createGroup("LCLS");
        Node *lcls = facility;
        root->createLink("LCLS","instrument");
        
        lcls->createStack("photon_energy_eV",H5T_NATIVE_DOUBLE);
        lcls->createStack("photon_wavelength_A",H5T_NATIVE_DOUBLE);
        lcls->createStack("eventTimeString",H5T_NATIVE_CHAR,26);
        lcls->createStack("machineTime",H5T_NATIVE_INT32);
		lcls->createStack("machineTimeNanoSeconds",H5T_NATIVE_INT32);
        lcls->createStack("fiducial",H5T_NATIVE_INT32);
        DETECTOR_LOOP{
            Node* detector = lcls->createCXIGroup("detector",detIndex+1);
            detector->createStack("position",H5T_NATIVE_DOUBLE);
            detector->createStack("EncoderValue",H5T_NATIVE_DOUBLE);
        }
        instrument->createLink("experiment_identifier","/entry_1/experiment_identifier");
        
        if(global->cxiLegacyFileFormat == 2015) {
            lcls->createStack("ebeamCharge",H5T_NATIVE_DOUBLE);
            lcls->createStack("ebeamL3Energy",H5T_NATIVE_DOUBLE);
            lcls->createStack("ebeamPkCurrBC2",H5T_NATIVE_DOUBLE);
            lcls->createStack("ebeamLTUPosX",H5T_NATIVE_DOUBLE);
            lcls->createStack("ebeamLTUPosY",H5T_NATIVE_DOUBLE);
            lcls->createStack("ebeamLTUAngX",H5T_NATIVE_DOUBLE);
            lcls->createStack("ebeamLTUAngY",H5T_NATIVE_DOUBLE);
            lcls->createStack("phaseCavityTime1",H5T_NATIVE_DOUBLE);
            lcls->createStack("phaseCavityTime2",H5T_NATIVE_DOUBLE);
            lcls->createStack("phaseCavityCharge1",H5T_NATIVE_DOUBLE);
            lcls->createStack("phaseCavityCharge2",H5T_NATIVE_DOUBLE);
            lcls->createStack("f_11_ENRC",H5T_NATIVE_DOUBLE);
            lcls->createStack("f_12_ENRC",H5T_NATIVE_DOUBLE);
            lcls->createStack("f_21_ENRC",H5T_NATIVE_DOUBLE);
            lcls->createStack("f_22_ENRC",H5T_NATIVE_DOUBLE);
            lcls->createStack("evr41",H5T_NATIVE_INT32);
            lcls->createLink("eventTime","eventTimeString");

            // TimeTool
            if(global->useTimeTool) {
                lcls->createStack("timeToolTrace", H5T_NATIVE_FLOAT, global->TimeToolStackWidth);
            }
            // FEE spectrum
            if(global->useFEEspectrum) {
                lcls->createStack("FEEspectrum", H5T_NATIVE_FLOAT, global->FEEspectrumWidth);
            }
            // eSpectrum in CXI hutch
            if(global->espectrum) {
                lcls->createStack("CXIespectrum", H5T_NATIVE_FLOAT, global->espectrumLength);
            }
            
            // EPICS
            for (int i=0; i < global->nEpicsPvFloatValues; i++ ) {
                lcls->createStack(&global->epicsPvFloatAddresses[i][0], H5T_NATIVE_FLOAT);
            }
        }
    }
	
	// European XFEL
	if(!strcmp(global->facility, "EuXFEL") ) {
		//Node *lcls = root->createGroup("LCLS");
		Node *euxfel = facility;
		root->createLink("EuXFEL","instrument");
		
		euxfel->createStack("photon_energy_eV",H5T_NATIVE_DOUBLE);
		euxfel->createStack("photon_wavelength_A",H5T_NATIVE_DOUBLE);
		euxfel->createStack("machineTime",H5T_NATIVE_INT32);
		
		euxfel->createStack("trainID",H5T_NATIVE_UINT64);
		euxfel->createStack("pulseID",H5T_NATIVE_UINT64);
		euxfel->createStack("cellID",H5T_NATIVE_UINT64);
		
		DETECTOR_LOOP{
			Node* detector = euxfel->createCXIGroup("detector",detIndex+1);
			detector->createStack("position",H5T_NATIVE_DOUBLE);
			detector->createStack("EncoderValue",H5T_NATIVE_DOUBLE);
		}
	}
	
	
	
    if(!strcmp(global->facility, "APS") || !strcmp(global->facility, "GMCA")) {
        Node *aps = facility;
        root->createLink("APS","instrument");
        aps->createStack("timestamp",H5T_NATIVE_CHAR,26);
        aps->createStack("photon_energy_eV",H5T_NATIVE_DOUBLE);
        aps->createStack("photon_wavelength_A",H5T_NATIVE_DOUBLE);
        aps->createStack("detectorDistance",H5T_NATIVE_DOUBLE);

        if(global->cxiLegacyFileFormat == 2015) {
            aps->createStack("exposureTime",H5T_NATIVE_DOUBLE);
            aps->createStack("exposurePeriod",H5T_NATIVE_DOUBLE);
            aps->createStack("tau",H5T_NATIVE_DOUBLE);
            aps->createStack("countCutoff",H5T_NATIVE_INT32);
            aps->createStack("nExcludedPixels",H5T_NATIVE_INT32);
            aps->createStack("beamX",H5T_NATIVE_DOUBLE);
            aps->createStack("beamY",H5T_NATIVE_DOUBLE);
            aps->createStack("startAngle",H5T_NATIVE_DOUBLE);
            aps->createStack("detector2Theta",H5T_NATIVE_DOUBLE);
            aps->createStack("angleIncrement",H5T_NATIVE_DOUBLE);
            aps->createStack("shutterTime",H5T_NATIVE_DOUBLE);
            aps->createStack("threshold",H5T_NATIVE_DOUBLE);
            
            aps->createStack("detector",H5T_NATIVE_CHAR,255);
        }
    }

    
    //
    //  Detector images (main purpose of CXI file format)
    //
    if(global->cxiSaveFrames) {

        DETECTOR_LOOP{
            DEBUGL2_ONLY{ DEBUG("Create Skeleton for detector %ld.",detIndex); }

            // For convenience define some detector specific variables
            int asic_nx = global->detector[detIndex].asic_nx;
            int asic_ny = global->detector[detIndex].asic_ny;
            int asic_nn = asic_nx * asic_ny;
            int nasics_x = global->detector[detIndex].nasics_x;
            int nasics_y = global->detector[detIndex].nasics_y;
            int nasics = nasics_x * nasics_y;
            long pix_nn = global->detector[detIndex].pix_nn;
            long pix_nx = global->detector[detIndex].pix_nx;
            long pix_ny = global->detector[detIndex].pix_ny;
            float* pix_x = global->detector[detIndex].pix_x;
            float* pix_y = global->detector[detIndex].pix_y;
            float* pix_r = global->detector[detIndex].pix_r;
            long image_nn = global->detector[detIndex].image_nn;
            long image_nx = global->detector[detIndex].image_nx;
            long image_ny = global->detector[detIndex].image_ny;
            long imageXxX_nn = global->detector[detIndex].imageXxX_nn;
            long imageXxX_nx = global->detector[detIndex].imageXxX_nx;
            long imageXxX_ny = global->detector[detIndex].imageXxX_ny;
            long radial_nn = global->detector[detIndex].radial_nn;
            uint16_t* pixelmask_shared = global->detector[detIndex].pixelmask_shared;
            uint16_t* pixelmask_shared_min = global->detector[detIndex].pixelmask_shared_min;
            uint16_t* pixelmask_shared_max = global->detector[detIndex].pixelmask_shared_max;
            int downsampling = global->detector[detIndex].downsampling;

            // /entry_1/instrument_1/detector_[i]/
            Node * detector = instrument->createCXIGroup("detector",detIndex+1);
            // Create symbolic link /entry_1/data_[i]/ which points to /entry_1/instrument_1/detector_[i]/
            entry->addCXIClassLink("data",detector->path().c_str());

            detector->createStack("distance",H5T_NATIVE_DOUBLE);
            detector->createStack("x_pixel_size",H5T_NATIVE_DOUBLE);
            detector->createStack("y_pixel_size",H5T_NATIVE_DOUBLE);

            detector->createLink("experiment_identifier", "/entry_1/experiment_identifier");

            int sBufferLen = sprintf(sBuffer,"%s [%s]",global->detector[detIndex].detectorType,global->detector[detIndex].detectorName);
            detector->createDataset("description",H5T_NATIVE_CHAR,sBufferLen)->write(sBuffer);

            // DATA_FORMAT_NON_ASSEMBLED
            DEBUGL2_ONLY{ DEBUG("Data format non-assembled."); }
            if (isBitOptionSet(global->detector[detIndex].saveFormat, cDataVersion::DATA_FORMAT_NON_ASSEMBLED)) {
                DEBUGL2_ONLY{ DEBUG("Initialize event groups and datasets for writing non-assembled data."); }
                cDataVersion dataV(NULL, &global->detector[detIndex], global->detector[detIndex].saveVersion, cDataVersion::DATA_FORMAT_NON_ASSEMBLED);
                while (dataV.next()) {

                    // Non-assembled images, modular (4D: N_frames x N_modules x Ny_module x Nx_module)
                    if (global->saveModular) {
                        
                        // Create group /entry_1/instrument_1/detector_[i]/modular_[datver]/
                        sprintf(sBuffer,"modular_%s",dataV.name);
                        Node * data_node = detector->createGroup(sBuffer);
                        data_node->createLink("experiment_identifier", "/entry_1/experiment_identifier");
                        data_node->createStack("data", h5type, asic_nx, asic_ny, nasics);
                        data_node->createStack("corner_positions",H5T_NATIVE_FLOAT, 3, nasics, H5S_UNLIMITED, 0, 0, 0, "experiment_identifier:module_identifier:coordinate");
                        data_node->createStack("basis_vectors", H5T_NATIVE_FLOAT, 3, 2, nasics, H5S_UNLIMITED, 0, 0, "experiment_identifier:module_identifier:dimension:coordinate");
                        data_node->createStack("module_identifier", H5T_NATIVE_CHAR, CXI::stringSize, nasics, 0, H5S_UNLIMITED, 0,0,"experiment_identifier:module_identifier");
                        if(global->detector[detIndex].savePixelmask){
                            data_node->createStack("mask",H5T_NATIVE_UINT16,asic_nx, asic_ny, nasics);
                        }				
                        long nn = asic_nn*nasics_x*nasics_y;
                        uint16_t* mask = (uint16_t *) calloc(nn, sizeof(uint16_t));
                        stackModulesMask(pixelmask_shared, mask, asic_nx, asic_ny, nasics_x, nasics_y);
                        data_node->createDataset("mask_shared",H5T_NATIVE_UINT16,asic_nx, asic_ny, nasics)->write(mask, -1, nn);
                        stackModulesMask(pixelmask_shared_max, mask, asic_nx, asic_ny, nasics_x, nasics_y);
                        data_node->createDataset("mask_shared_max",H5T_NATIVE_UINT16,asic_nx, asic_ny, nasics)->write(mask, -1, nn);
                        stackModulesMask(pixelmask_shared_min, mask, asic_nx, asic_ny, nasics_x, nasics_y);
                        data_node->createDataset("mask_shared_min",H5T_NATIVE_UINT16,asic_nx, asic_ny, nasics)->write(mask, -1, nn);					
                        free(mask);

                        // If this is the main data version we create symbolic links
                        if (dataV.isMainVersion == 1) {
                            detector->addDatasetLink("data",data_node->path().c_str());
                            detector->addDatasetLink("corner_positions",data_node->path().c_str());
                            detector->addDatasetLink("basis_vectors",data_node->path().c_str());
                            detector->addDatasetLink("module_identifier",data_node->path().c_str());
                            if(global->detector[detIndex].savePixelmask){
                                detector->addDatasetLink("mask",data_node->path().c_str());
                            }
                            detector->addDatasetLink("mask_shared",data_node->path().c_str());
                            detector->addDatasetLink("mask_shared_max",data_node->path().c_str());
                            detector->addDatasetLink("mask_shared_min",data_node->path().c_str());
                        }
                    }

                    // Non-assembled images (3D: N_frames x Ny_frame x Nx_frame)
                    else {
                        // Create group /entry_1/instrument_1/detector_[i]/[datver]/
                        Node * data_node = detector->createGroup(dataV.name_version);
                        data_node->createLink("experiment_identifier", "/entry_1/experiment_identifier");
                        data_node->createStack("data", h5type,pix_nx, pix_ny);
                        if(global->detector[detIndex].savePixelmask){
                            data_node->createStack("mask",H5T_NATIVE_UINT16,pix_nx, pix_ny);
                        }
                        data_node->createDataset("mask_shared",H5T_NATIVE_UINT16,pix_nx, pix_ny)->write(pixelmask_shared, -1, pix_nn);
                        data_node->createDataset("mask_shared_max",H5T_NATIVE_UINT16,pix_nx, pix_ny)->write(pixelmask_shared_max, -1, pix_nn);
                        data_node->createDataset("mask_shared_min",H5T_NATIVE_UINT16,pix_nx, pix_ny)->write(pixelmask_shared_min, -1, pix_nn);
                        if(global->detector[detIndex].saveThumbnail) {
                            data_node->createStack("thumbnail",H5T_STD_I16LE, pix_nx/CXI::thumbnailScale, pix_ny/CXI::thumbnailScale);
                        }
                        
                        // If this is the main data version we create links to all datasets
                        if (dataV.isMainVersion) {
                            detector->addDatasetLink("data",data_node->path().c_str());
                            if(global->detector[detIndex].savePixelmask){
                                detector->addDatasetLink("mask",data_node->path().c_str());
                            }
                            detector->addDatasetLink("mask_shared",data_node->path().c_str());
                            detector->addDatasetLink("mask_shared_max",data_node->path().c_str());
                            detector->addDatasetLink("mask_shared_min",data_node->path().c_str());
                        }
                    }
                }
            }
            
            int image_counter = 0;
            // DATA_FORMAT_ASSEMBLED
            // Assembled images (3D: N_frames x Ny_image x Nx_image)
            DEBUGL2_ONLY{ DEBUG("Data format assembled."); }
            
            if (isBitOptionSet(global->detector[detIndex].saveFormat, cDataVersion::DATA_FORMAT_ASSEMBLED)) {
                DEBUGL2_ONLY{ DEBUG("Initialize event groups and datasets for writing assembled data."); }
                // Create group /entry_1/image_i
                int i_image = 1+global->nDetectors*image_counter+detIndex;
                image_counter += 1;
                Node * image_node = entry->createCXIGroup("image",i_image);
                image_node->addClassLink("detector",detector->path());
                image_node->addClassLink("source",source->path());
                cDataVersion dataV(NULL, &global->detector[detIndex], global->detector[detIndex].saveVersion, cDataVersion::DATA_FORMAT_ASSEMBLED);
                while (dataV.next()) {
                    // Create group /entry_1/image_i/data_[datver]/
                    Node * data_node = image_node->createGroup(dataV.name_version);		
                    data_node->createStack("data", h5type, image_nx, image_ny);
                    if(global->detector[detIndex].savePixelmask){
                        data_node->createStack("mask",H5T_NATIVE_UINT16, image_nx, image_ny);
                    }
                    uint16_t *image_pixelmask_shared = (uint16_t*) calloc(image_nn,sizeof(uint16_t));
                    assemble2DMask(image_pixelmask_shared, pixelmask_shared, 
                                   pix_x, pix_y, pix_nn, image_nx, image_nn, global->assembleInterpolation);
                    data_node->createDataset("mask_shared",H5T_NATIVE_UINT16,image_nx, image_ny)->write(image_pixelmask_shared, -1, image_nn);
                    free(image_pixelmask_shared);      
                    data_node->createStack("data_type",H5T_NATIVE_CHAR,CXI::stringSize);
                    data_node->createStack("data_space",H5T_NATIVE_CHAR,CXI::stringSize);
                    data_node->createStack("thumbnail",H5T_NATIVE_FLOAT, image_nx/CXI::thumbnailScale, image_nx/CXI::thumbnailScale);
                    data_node->createLink("experiment_identifier", "/entry_1/experiment_identifier");
                    // If this is the main data version we create links to all datasets
                    if (dataV.isMainVersion == 1) {
                        image_node->addDatasetLink("data",data_node->path().c_str());
                        if(global->detector[detIndex].savePixelmask){
                            image_node->addDatasetLink("mask",data_node->path().c_str());
                        }
                        image_node->addDatasetLink("mask_shared",data_node->path().c_str());
                        image_node->addDatasetLink("data_type",data_node->path().c_str());
                        image_node->addDatasetLink("data_space",data_node->path().c_str());
                        if(global->detector[detIndex].saveThumbnail) {
                            image_node->addDatasetLink("thumbnail",data_node->path().c_str());
                        }
                        image_node->createLink("experiment_identifier", "/entry_1/experiment_identifier");
                    }
                }
            }	

            // DATA_FORMAT_ASSEMBLED_AND_DOWNSAMPLED
            // Assembled images (3D: N_frames x Ny_imageXxX x Nx_imageXxX)
            DEBUGL2_ONLY{ DEBUG("Data format assembled and downsampled."); }
            if (isBitOptionSet(global->detector[detIndex].saveFormat, cDataVersion::DATA_FORMAT_ASSEMBLED_AND_DOWNSAMPLED)) {
                DEBUGL2_ONLY{ DEBUG("Initialize event groups and datasets for writing assembled and downsampled data."); }
                int i_image = 1+global->nDetectors*image_counter+detIndex;
                image_counter += 1;
                // Create group /entry_1/image_[i]
                Node * image_node;
                image_node = entry->createCXIGroup("image",i_image);
                image_node->addClassLink("detector",detector->path());
                image_node->addClassLink("source",source->path());
                cDataVersion dataV(NULL, &global->detector[detIndex], global->detector[detIndex].saveVersion, cDataVersion::DATA_FORMAT_ASSEMBLED_AND_DOWNSAMPLED);
                while (dataV.next()) {
                    // Create group /entry_1/image_i/[datver]/
                    Node * data_node = image_node->createGroup(dataV.name_version);			
                    data_node->createStack("data", h5type, imageXxX_nx, imageXxX_ny);
                    if(global->detector[detIndex].savePixelmask){
                        data_node->createStack("mask",H5T_NATIVE_UINT16, imageXxX_nx, imageXxX_ny);
                    }
                    uint16_t *image_pixelmask_shared = (uint16_t*) calloc( image_nn,sizeof(uint16_t));
                    assemble2DMask(image_pixelmask_shared, pixelmask_shared,
                                   pix_x, pix_y, pix_nn, image_nx, image_nn, global->assembleInterpolation);
                    uint16_t *imageXxX_pixelmask_shared = (uint16_t*) calloc(imageXxX_nn, sizeof(uint16_t));
                    if(global->detector[detIndex].downsamplingConservative==1){
                        downsampleMaskConservative(image_pixelmask_shared,imageXxX_pixelmask_shared, image_nn, image_nx, imageXxX_nn, imageXxX_nx, downsampling, debugLevel);
                    } else {
                        downsampleMaskNonConservative(image_pixelmask_shared,imageXxX_pixelmask_shared, image_nn, image_nx, imageXxX_nn, imageXxX_nx, downsampling, debugLevel);
                    }
                    data_node->createDataset("mask_shared", H5T_NATIVE_UINT16, imageXxX_nx, imageXxX_ny)->write(imageXxX_pixelmask_shared, -1, imageXxX_nn);
                    free(imageXxX_pixelmask_shared);
                    free(image_pixelmask_shared);
                    data_node->createStack("data_type",H5T_NATIVE_CHAR,CXI::stringSize);
                    data_node->createStack("data_space",H5T_NATIVE_CHAR,CXI::stringSize);
                    data_node->createStack("thumbnail",H5T_NATIVE_FLOAT, imageXxX_nx/CXI::thumbnailScale, imageXxX_ny/CXI::thumbnailScale);
                    data_node->createLink("experiment_identifier", "/entry_1/experiment_identifier");
                    // If this is the main data version we create links to all datasets
                    if (dataV.isMainVersion == 1) {
                        image_node->addDatasetLink("data",data_node->path().c_str());
                        if(global->detector[detIndex].savePixelmask){
                            image_node->addDatasetLink("mask",data_node->path().c_str());
                        }
                        image_node->addDatasetLink("mask_shared",data_node->path().c_str());
                        image_node->addDatasetLink("data_type",data_node->path().c_str());
                        image_node->addDatasetLink("data_space",data_node->path().c_str());
                        if(global->detector[detIndex].saveThumbnail) {
                            image_node->addDatasetLink("thumbnail",data_node->path().c_str());
                        }
                        image_node->createLink("experiment_identifier", "/entry_1/experiment_identifier");
                    }
                }
            }

            // DATA_FORMAT_RADIAL_AVERAGE
            // Radial average (2D: N_frames x N_radial)
            DEBUGL2_ONLY{ DEBUG("Data format radial average."); }
            if(global->cxiLegacyFileFormat == 2015) {

                if (isBitOptionSet(global->detector[detIndex].saveFormat, cDataVersion::DATA_FORMAT_RADIAL_AVERAGE)) {
                    DEBUGL2_ONLY{ DEBUG("Initialize event groups and datasets for writing radially averaged data."); }
                    // Create group /entry_1/image_[i]
                    int i_image = 1+global->nDetectors*image_counter+detIndex;
                    image_counter += 1;
                    Node *image_node = entry->createCXIGroup("image",i_image);
                    image_node->addClassLink("detector",detector->path());
                    image_node->addClassLink("source",source->path());			
                    cDataVersion dataV(NULL, &global->detector[detIndex], global->detector[detIndex].saveVersion, cDataVersion::DATA_FORMAT_RADIAL_AVERAGE);
                    while (dataV.next()) {
                        // Create group /entry_1/image_i/[datver]/
                        Node *data_node = image_node->createGroup(dataV.name_version);
                        data_node->createStack("data", H5T_NATIVE_FLOAT, radial_nn);
                        if(global->detector[detIndex].savePixelmask){
                            data_node->createStack("mask",H5T_NATIVE_UINT16, radial_nn);
                        }
                        uint16_t *radial_pixelmask_shared = (uint16_t*) calloc(radial_nn,sizeof(uint16_t));
                        float *foo1 = (float *) calloc(pix_nn,sizeof(float));
                        float *foo2 = (float *) calloc(radial_nn,sizeof(float));
                        calculateRadialAverage(foo1, pixelmask_shared, foo2, radial_pixelmask_shared, pix_r, radial_nn, pix_nn);
                        data_node->createDataset("mask_shared",H5T_NATIVE_UINT16, radial_nn)->write(radial_pixelmask_shared, -1, radial_nn);
                        free(radial_pixelmask_shared);
                        free(foo1);
                        free(foo2);
                        data_node->createStack("data_type",H5T_NATIVE_CHAR,CXI::stringSize);
                        data_node->createStack("data_space",H5T_NATIVE_CHAR,CXI::stringSize);
                        data_node->createLink("experiment_identifier", "/entry_1/experiment_identifier");
                        // If this is the main data version we create links to all datasets
                        if (dataV.isMainVersion == 1) {
                            image_node->addDatasetLink("data",data_node->path().c_str());
                            if(global->detector[detIndex].savePixelmask){
                                image_node->addDatasetLink("mask",data_node->path().c_str());
                            }
                            image_node->addDatasetLink("mask_shared",data_node->path().c_str());
                            image_node->addDatasetLink("data_type",data_node->path().c_str());
                            image_node->addDatasetLink("data_space",data_node->path().c_str());
                            image_node->createLink("experiment_identifier", "/entry_1/experiment_identifier");
                        }
                    }
                }
            }
        }
    }
    // End of save frames
	if (global->debugLevel > 2) DEBUG("Detector skeleton created.");

    //
    //  Peak lists - always saved for CrystFEL
    //
    int resultIndex = 1;
    if(global->savePeakInfo && global->hitfinder){
        Node * result = entry->createCXIGroup("result",resultIndex);
        
        result->createStack("powderClass", H5T_NATIVE_INT);
        
        result->createStack("nPeaks", H5T_NATIVE_INT);
        
        result->createStack("peakXPosAssembled", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        result->createStack("peakYPosAssembled",H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        
        result->createStack("peakXPosRaw", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        result->createStack("peakYPosRaw", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        
        result->createStack("peakTotalIntensity", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        result->createStack("peakMaximumValue", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        result->createStack("peakSNR", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        result->createStack("peakNPixels", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        
        result->createLink("data", "peakTotalIntensity");
        resultIndex++;
    }
    
    
    
    //
    // TOF data
    //
    if(global->cxiLegacyFileFormat == 2015) {
        if(global->TOFPresent){
            for(int i = 0;i<global->nTOFDetectors;i++){
                char buffer[1024];
                Node * detector = instrument->createCXIGroup("detector",1+i+global->nDetectors);
                detector->createStack("data",H5T_NATIVE_DOUBLE,global->tofDetector[i].numSamples);
                detector->createStack("tofTime",H5T_NATIVE_DOUBLE,global->tofDetector[i].numSamples);
                int buffLen = sprintf(buffer,"TOF detector\nSource identifier: %s\nChannel number: %d\nDescription: %s\n",global->tofDetector[i].sourceIdentifier,
                                      global->tofDetector[i].channel, global->tofDetector[i].description);
                detector->createDataset("description",H5T_NATIVE_CHAR,buffLen)->write(buffer);
            }
        }
    }
    

    
    //
	// Save cheetah information
    //

    if(global->cxiLegacyFileFormat == 2015) {

        Node * cheetah = root->createGroup("cheetah");
        Node * event_data = cheetah->createGroup("event_data");
        Node *global_data = cheetah->createGroup("global_data");

        cheetah->createDataset("cxi_version",H5T_NATIVE_INT,1)->write(&CXI::version);
        cheetah->createDataset("cheetah_version_commit",H5T_NATIVE_CHAR,strlen(GIT_SHA1))->write(GIT_SHA1);
        char *psana_git_sha = getenv("PSANA_GIT_SHA");
        if (psana_git_sha)
            cheetah->createDataset("psana_version_commit",H5T_NATIVE_CHAR,strlen(psana_git_sha))->write(psana_git_sha);
        
        /* For some reason the swmr version of hdf5 can't cope with string stacks larger than 255 characters */
        event_data->createStack("eventName",H5T_NATIVE_CHAR,255);
        event_data->createStack("frameNumber",H5T_NATIVE_LONG);
        event_data->createStack("frameNumberIncludingSkipped",H5T_NATIVE_LONG);
        event_data->createStack("threadID",H5T_NATIVE_LONG);
        event_data->createStack("nPeaks",H5T_NATIVE_INT);
        event_data->createStack("peakNpix",H5T_NATIVE_FLOAT);
        event_data->createStack("peakTotal",H5T_NATIVE_FLOAT);
        event_data->createStack("peakResolution",H5T_NATIVE_FLOAT);
        event_data->createStack("peakResolutionA",H5T_NATIVE_FLOAT);
        event_data->createStack("peakDensity",H5T_NATIVE_FLOAT);
        event_data->createStack("imageClass",H5T_NATIVE_INT);
        event_data->createStack("hit",H5T_NATIVE_INT);
        DETECTOR_LOOP{
            Node * detector = event_data->createCXIGroup("detector",detIndex+1);
            detector->createStack("sum",H5T_NATIVE_FLOAT);
        }

        //Node *global_data = cheetah->createGroup("global_data");
        global_data->createStack("hit",H5T_NATIVE_INT);
        global_data->createStack("nPeaks",H5T_NATIVE_INT);

        // First read configuration file to memory
        std::ifstream file(global->configFile, std::ios::binary);
        file.seekg(0, std::ios::end);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::vector<char> buffer(size);
        if(!file.read(buffer.data(), size)){
            ERROR("Could not read configuration file");
        }
        
        // Just write out input configuration file
        Node * configuration = cheetah->createGroup("configuration");
        configuration->createDataset("input",H5T_NATIVE_CHAR,size)->write(&(buffer[0]));
    
    
        //
        //  Accumulated detector stuff
        //
        DETECTOR_LOOP{
            Node * det_node = global_data->createCXIGroup("detector",detIndex+1);
            det_node->createStack("lastBgUpdate",H5T_NATIVE_LONG);
            det_node->createStack("nHot",H5T_NATIVE_LONG);
            det_node->createStack("lastHotPixUpdate",H5T_NATIVE_LONG);
            det_node->createStack("hotPixBufferCounter",H5T_NATIVE_LONG);
            det_node->createStack("nNoisy",H5T_NATIVE_LONG);
            det_node->createStack("lastNoisyPixUpdate",H5T_NATIVE_LONG);
            det_node->createStack("noisyPixBufferCounter",H5T_NATIVE_LONG);

            POWDER_LOOP{
                Node * cl = det_node->createCXIGroup("class",powderClass+1);
                // Mean and sigma
                FOREACH_DATAFORMAT_T(i_f, cDataVersion::DATA_FORMATS) {
                    if (isBitOptionSet(global->detector[detIndex].powderFormat,*i_f)) {
                        cDataVersion dataV(NULL, &global->detector[detIndex], global->detector[detIndex].powderVersion, *i_f);
                        while (dataV.next()) {
                            if (*i_f != cDataVersion::DATA_FORMAT_RADIAL_AVERAGE) {
                                sprintf(sBuffer,"mean_%s",dataV.name);
                                cl->createDataset(sBuffer,H5T_NATIVE_DOUBLE,dataV.pix_nx,dataV.pix_ny);
                                sprintf(sBuffer,"sigma_%s",dataV.name);
                                cl->createDataset(sBuffer,H5T_NATIVE_DOUBLE,dataV.pix_nx,dataV.pix_ny);				
                            } else {
                                sprintf(sBuffer,"mean_%s",dataV.name);
                                cl->createDataset(sBuffer,H5T_NATIVE_DOUBLE,dataV.pix_nn);
                                sprintf(sBuffer,"sigma_%s",dataV.name);
                                cl->createDataset(sBuffer,H5T_NATIVE_DOUBLE,dataV.pix_nn);									
                            }
                        }
                    }
                }
            }
        
            // Persistent background (median)
            if (global->detector[detIndex].useSubtractPersistentBackground) {
                sprintf(sBuffer,"perisistent_background");
                det_node->createDataset(sBuffer,H5T_NATIVE_FLOAT,global->detector[detIndex].pix_nx,global->detector[detIndex].pix_ny);
            }

            // Pixel value histogram
            if (global->detector[detIndex].histogram) {
                sprintf(sBuffer,"pixel_histogram");
                Node * hist_node = det_node->createGroup(sBuffer);
                sprintf(sBuffer,"histogram");
                hist_node->createDataset(sBuffer, H5T_NATIVE_UINT16, global->detector[detIndex].histogramNbins, global->detector[detIndex].histogram_nfs, global->detector[detIndex].histogram_nss);
                sprintf(sBuffer,"histogram_scale");
                hist_node->createDataset(sBuffer, H5T_NATIVE_FLOAT, global->detector[detIndex].histogramNbins)->write(global->detector[detIndex].histogramScale);
            }
        }
        
	}
	
    
    //
    // Sample translation or electrojet voltage
    //
    //if(global->samplePosXPV[0] || global->samplePosYPV[0] || global->samplePosZPV[0] || global->sampleVoltage[0]){
    //    Node * sample = entry->addClass("sample");
    //    sample->addClass("geometry")->createDataset("translation",H5T_NATIVE_FLOAT,3,0,H5S_UNLIMITED);
    //    sample->addClass("injection")->createStack("voltage",H5T_NATIVE_FLOAT);
    //}

    
	if (global->debugLevel > 2) {
		DEBUG("Skeleton created.");
	}

	#if defined H5F_ACC_SWMR_READ
	if(global->cxiSWMR){  
		root->closeAll();
		if(H5Fstart_swmr_write(root->hid()) < 0){
			ERROR("Cannot change to SWMR mode.\n");			
		}
		puts("Changed to SWMR mode.");
		root->openAll();
	}
	#endif

	H5Fflush(root->hid(), H5F_SCOPE_GLOBAL);
	//pthread_mutex_unlock(&global->saveCXI_mutex);

	return root;
}


/*
 *	Create the HDF5 skeleton for the Results file.
 */
static CXI::Node *createResultsSkeleton(const char *filename, cGlobal *global){
    
    //pthread_mutex_lock(&global->saveCXI_mutex);
    //int debugLevel = global->debugLevel;
    DEBUGL2_ONLY{ DEBUG("Create Skeleton."); }
    
    using CXI::Node;
    CXI::h5compress = global->h5compress;
    
    // Conversion flags
    int ignoreConversionFlags = 0;
    if(global->ignoreConversionOverflow){
        ignoreConversionFlags |= CXI::IgnoreOverflow;
    }
    if(global->ignoreConversionTruncate){
        ignoreConversionFlags |= CXI::IgnoreTruncate;
    }
    if(global->ignoreConversionPrecision){
        ignoreConversionFlags |= CXI::IgnorePrecision;
    }
    if(global->ignoreConversionNAN){
        ignoreConversionFlags |= CXI::IgnoreNAN;
    }
    
    // Check what data type format we want to save things in. Defaults to float
    hid_t h5type = H5T_NATIVE_FLOAT;
    if(!strcasecmp(global->dataSaveFormat,"INT16")){
        h5type = H5T_STD_I16LE;
    }
    else if(!strcasecmp(global->dataSaveFormat,"INT32")){
        h5type = H5T_STD_I32LE;
    }
    else if(!strcasecmp(global->dataSaveFormat,"float")){
        h5type = H5T_NATIVE_FLOAT;
    }
    
    // Create root node
    CXI::Node *root = new Node(filename,global->cxiSWMR,ignoreConversionFlags);
    char sBuffer[1024];
    
    
    // Information about the instrument/beamline (such as encoder values)
    Node *facility = root->createGroup("instrument");
    
    // LCLS
    if(!strcmp(global->facility, "LCLS") ) {
        Node *lcls = facility;
        root->createLink("LCLS","instrument");
        //Node *lcls = root->createGroup("LCLS");
        lcls->createStack("machineTime",H5T_NATIVE_INT32);
        lcls->createStack("machineTimeNanoSeconds",H5T_NATIVE_INT32);
        lcls->createStack("fiducial",H5T_NATIVE_INT32);
        lcls->createStack("ebeamCharge",H5T_NATIVE_DOUBLE);
        lcls->createStack("ebeamL3Energy",H5T_NATIVE_DOUBLE);
        lcls->createStack("ebeamPkCurrBC2",H5T_NATIVE_DOUBLE);
        lcls->createStack("ebeamLTUPosX",H5T_NATIVE_DOUBLE);
        lcls->createStack("ebeamLTUPosY",H5T_NATIVE_DOUBLE);
        lcls->createStack("ebeamLTUAngX",H5T_NATIVE_DOUBLE);
        lcls->createStack("ebeamLTUAngY",H5T_NATIVE_DOUBLE);
        lcls->createStack("phaseCavityTime1",H5T_NATIVE_DOUBLE);
        lcls->createStack("phaseCavityTime2",H5T_NATIVE_DOUBLE);
        lcls->createStack("phaseCavityCharge1",H5T_NATIVE_DOUBLE);
        lcls->createStack("phaseCavityCharge2",H5T_NATIVE_DOUBLE);
        lcls->createStack("photon_energy_eV",H5T_NATIVE_DOUBLE);
        lcls->createStack("photon_wavelength_A",H5T_NATIVE_DOUBLE);
        lcls->createStack("f_11_ENRC",H5T_NATIVE_DOUBLE);
        lcls->createStack("f_12_ENRC",H5T_NATIVE_DOUBLE);
        lcls->createStack("f_21_ENRC",H5T_NATIVE_DOUBLE);
        lcls->createStack("f_22_ENRC",H5T_NATIVE_DOUBLE);
        lcls->createStack("eventTimeString",H5T_NATIVE_CHAR,26);
        //lcls->createLink("eventTime","eventTimeString");
        
        // TimeTool
        if(global->useTimeTool) {
            lcls->createStack("timeToolTrace", H5T_NATIVE_FLOAT, global->TimeToolStackWidth);
        }
        // FEE spectrum
        if(global->useFEEspectrum) {
            lcls->createStack("FEEspectrum", H5T_NATIVE_FLOAT, global->FEEspectrumWidth);
        }
        // eSpectrum in CXI hutch
        if(global->espectrum) {
            lcls->createStack("CXIespectrum", H5T_NATIVE_FLOAT, global->espectrumLength);
        }
        
        // EPICS
        for (int i=0; i < global->nEpicsPvFloatValues; i++ ) {
            lcls->createStack(&global->epicsPvFloatAddresses[i][0], H5T_NATIVE_FLOAT);
        }

		// EVR codes
		for (int i=0; i < global->nEvrValuesToSave; i++ ) {
			char evrStr[100];
			sprintf(evrStr, "evr%i", global->evrValuesToSave[i]);
			lcls->createStack(&evrStr[0], H5T_NATIVE_INT32);
		}
		
		// Laser stuff
		lcls->createStack("pumpLaserOn",H5T_NATIVE_INT32);
		lcls->createStack("pumpLaserCode",H5T_NATIVE_INT32);

		
        DETECTOR_LOOP{
            Node* detector = lcls->createGroup("detector",detIndex);
            detector->createStack("position",H5T_NATIVE_DOUBLE);
            detector->createStack("EncoderValue",H5T_NATIVE_DOUBLE);
        }
    }
	
	// European XFEL
	if(!strcmp(global->facility, "EuXFEL") ) {
		//Node *lcls = root->createGroup("LCLS");
		Node *euxfel = facility;
		root->createLink("EuXFEL","instrument");
		
		euxfel->createStack("photon_energy_eV",H5T_NATIVE_DOUBLE);
		euxfel->createStack("photon_wavelength_A",H5T_NATIVE_DOUBLE);
		euxfel->createStack("machineTime",H5T_NATIVE_INT32);
		
		euxfel->createStack("trainID",H5T_NATIVE_UINT64);
		euxfel->createStack("pulseID",H5T_NATIVE_UINT64);
		euxfel->createStack("cellID",H5T_NATIVE_UINT64);
		
		DETECTOR_LOOP{
			Node* detector = euxfel->createGroup("detector",detIndex);
			detector->createStack("EncoderValue",H5T_NATIVE_DOUBLE);
		}
	}

	
    // APS
    if(!strcmp(global->facility, "APS") || !strcmp(global->facility, "GMCA") ) {
        Node *aps = facility;
        root->createLink("APS","instrument");
        aps->createStack("exposureTime",H5T_NATIVE_DOUBLE);
        aps->createStack("exposurePeriod",H5T_NATIVE_DOUBLE);
        aps->createStack("tau",H5T_NATIVE_DOUBLE);
        aps->createStack("countCutoff",H5T_NATIVE_INT32);
        aps->createStack("nExcludedPixels",H5T_NATIVE_INT32);
        aps->createStack("detectorDistance",H5T_NATIVE_DOUBLE);
        aps->createStack("beamX",H5T_NATIVE_DOUBLE);
        aps->createStack("beamY",H5T_NATIVE_DOUBLE);
        aps->createStack("startAngle",H5T_NATIVE_DOUBLE);
        aps->createStack("detector2Theta",H5T_NATIVE_DOUBLE);
        aps->createStack("angleIncrement",H5T_NATIVE_DOUBLE);
        aps->createStack("shutterTime",H5T_NATIVE_DOUBLE);
        aps->createStack("timestamp",H5T_NATIVE_CHAR,26);
        aps->createStack("photon_energy_eV",H5T_NATIVE_DOUBLE);
        aps->createStack("photon_wavelength_A",H5T_NATIVE_DOUBLE);
        aps->createStack("threshold",H5T_NATIVE_DOUBLE);

        aps->createStack("detector",H5T_NATIVE_CHAR,255);
    }

	
	// Create top level entries
    Node *event_data = root->createGroup("event_data");
    Node *run_data = root->createGroup("run_data");
    //Node *entry = root->addClass("entry");
    
    
    event_data->createStack("event_identifier",H5T_NATIVE_CHAR,CXI::stringSize);
    event_data->createStack("hit",H5T_NATIVE_INT);
    event_data->createStack("nPeaks",H5T_NATIVE_INT);
    event_data->createStack("hitScore",H5T_NATIVE_FLOAT);

	
    //
    //  Detector event data (radial averages, etc)
    //
    DETECTOR_LOOP{
        DEBUGL2_ONLY{ DEBUG("Create Skeleton for detector %ld.",detIndex); }
        
        // For convenience define some detector specific variables
        //int asic_nx = global->detector[detIndex].asic_nx;
        //int asic_ny = global->detector[detIndex].asic_ny;
        //int asic_nn = asic_nx * asic_ny;
        //int nasics_x = global->detector[detIndex].nasics_x;
        //int nasics_y = global->detector[detIndex].nasics_y;
        //int nasics = nasics_x * nasics_y;
        long pix_nn = global->detector[detIndex].pix_nn;
        //long pix_nx = global->detector[detIndex].pix_nx;
        //long pix_ny = global->detector[detIndex].pix_ny;
        //float* pix_x = global->detector[detIndex].pix_x;
        //float* pix_y = global->detector[detIndex].pix_y;
        float* pix_r = global->detector[detIndex].pix_r;
        //long image_nn = global->detector[detIndex].image_nn;
        //long image_nx = global->detector[detIndex].image_nx;
        //long image_ny = global->detector[detIndex].image_ny;
        //long imageXxX_nn = global->detector[detIndex].imageXxX_nn;
        //long imageXxX_nx = global->detector[detIndex].imageXxX_nx;
        //long imageXxX_ny = global->detector[detIndex].imageXxX_ny;
        long radial_nn = global->detector[detIndex].radial_nn;
        uint16_t* pixelmask_shared = global->detector[detIndex].pixelmask_shared;
        //uint16_t* pixelmask_shared_min = global->detector[detIndex].pixelmask_shared_min;
        //uint16_t* pixelmask_shared_max = global->detector[detIndex].pixelmask_shared_max;
        //int downsampling = global->detector[detIndex].downsampling;
        int sBufferLen = sprintf(sBuffer,"%s [%s]",global->detector[detIndex].detectorType,global->detector[detIndex].detectorName);
        
        Node * detector = event_data->createGroup("detector",detIndex);
        //Node * detector = instrument->createGroup("detector",detIndex+1);
        
        detector->createStack("distance",H5T_NATIVE_DOUBLE);
        detector->createStack("x_pixel_size",H5T_NATIVE_DOUBLE);
        detector->createStack("y_pixel_size",H5T_NATIVE_DOUBLE);
        detector->createDataset("description",H5T_NATIVE_CHAR,sBufferLen)->write(sBuffer);
        
        detector->createStack("lastBgUpdate",H5T_NATIVE_LONG);
        detector->createStack("nHot",H5T_NATIVE_LONG);
        //detector->createStack("lastHotPixUpdate",H5T_NATIVE_LONG);
        //detector->createStack("hotPixBufferCounter",H5T_NATIVE_LONG);
        detector->createStack("nNoisy",H5T_NATIVE_LONG);
        //detector->createStack("lastNoisyPixUpdate",H5T_NATIVE_LONG);
        //detector->createStack("noisyPixBufferCounter",H5T_NATIVE_LONG);

        
        // DATA_FORMAT_RADIAL_AVERAGE
        // Radial average (2D: N_frames x N_radial)
        int image_counter = 0;
        DEBUGL2_ONLY{ DEBUG("Data format radial average."); }
        if (isBitOptionSet(global->detector[detIndex].saveFormat, cDataVersion::DATA_FORMAT_RADIAL_AVERAGE)) {
            DEBUGL2_ONLY{ DEBUG("Initialize event groups and datasets for writing radially averaged data."); }
            image_counter += 1;
            Node *image_node = detector->createGroup("radialaverage");
            cDataVersion dataV(NULL, &global->detector[detIndex], global->detector[detIndex].saveVersion, cDataVersion::DATA_FORMAT_RADIAL_AVERAGE);
            while (dataV.next()) {
                // Create group /entry_1/image_i/[datver]/
                Node *data_node = image_node->createGroup(dataV.name_version);
                data_node->createStack("data", H5T_NATIVE_FLOAT, radial_nn);
                if(global->detector[detIndex].savePixelmask){
                    data_node->createStack("mask",H5T_NATIVE_UINT16, radial_nn);
                }
                uint16_t *radial_pixelmask_shared = (uint16_t*) calloc(radial_nn,sizeof(uint16_t));
                float *foo1 = (float *) calloc(pix_nn,sizeof(float));
                float *foo2 = (float *) calloc(radial_nn,sizeof(float));
                calculateRadialAverage(foo1, pixelmask_shared, foo2, radial_pixelmask_shared, pix_r, radial_nn, pix_nn);
                data_node->createDataset("mask_shared",H5T_NATIVE_UINT16, radial_nn)->write(radial_pixelmask_shared, -1, radial_nn);
                free(radial_pixelmask_shared);
                free(foo1);
                free(foo2);
                data_node->createStack("data_type",H5T_NATIVE_CHAR,CXI::stringSize);
                data_node->createStack("data_space",H5T_NATIVE_CHAR,CXI::stringSize);
            }
        }
    }
    
    if (global->debugLevel > 2) DEBUG("Detector skeleton created.");
    
    
    //
    // TOF data (Aqiris)
    //
    if(global->TOFPresent){
        for(int i = 0;i<global->nTOFDetectors;i++){
            char buffer[1024];
            Node * detector = event_data->createGroup("detector",i+global->nDetectors);
            detector->createStack("data",H5T_NATIVE_DOUBLE,global->tofDetector[i].numSamples);
            detector->createStack("tofTime",H5T_NATIVE_DOUBLE,global->tofDetector[i].numSamples);
            int buffLen = sprintf(buffer,"TOF detector\nSource identifier: %s\nChannel number: %d\nDescription: %s\n",global->tofDetector[i].sourceIdentifier,
                                  global->tofDetector[i].channel, global->tofDetector[i].description);
            detector->createDataset("description",H5T_NATIVE_CHAR,buffLen)->write(buffer);
        }
    }
    
    

    //
    // Sample translation or electrojet voltage
    //
    //if(global->samplePosXPV[0] || global->samplePosYPV[0] || global->samplePosZPV[0] || global->sampleVoltage[0]){
    //    Node * sample = entry->addClass("sample");
    //    sample->addClass("geometry")->createDataset("translation",H5T_NATIVE_FLOAT,3,0,H5S_UNLIMITED);
    //    sample->addClass("injection")->createStack("voltage",H5T_NATIVE_FLOAT);
    //}
    
    
    
    
    
    /*
     *  Peak lists
     */
    int resultIndex = 0;
    if(global->savePeakInfo && global->hitfinder){
        Node *result = event_data->createGroup("peaks",resultIndex);
        
        result->createStack("powderClass", H5T_NATIVE_INT);
        result->createStack("nPeaks", H5T_NATIVE_INT);
        result->createStack("peakTotal",H5T_NATIVE_FLOAT);
        result->createStack("peakResolution",H5T_NATIVE_FLOAT);
        result->createStack("peakResolutionA",H5T_NATIVE_FLOAT);
        result->createStack("peakDensity",H5T_NATIVE_FLOAT);
        result->createStack("imageClass",H5T_NATIVE_INT);
        result->createStack("hit",H5T_NATIVE_INT);
        
        result->createStack("peakXPosAssembled", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        result->createStack("peakYPosAssembled",H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        result->createStack("peakXPosRaw", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        result->createStack("peakYPosRaw", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        result->createStack("peakTotalIntensity", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        result->createStack("peakMaximumValue", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        result->createStack("peakSNR", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        result->createStack("peakNPixels", H5T_NATIVE_FLOAT, 0,H5S_UNLIMITED,H5S_UNLIMITED,0,
                            CXI::peaksChunkSize[0],CXI::peaksChunkSize[1],"experiment_identifier:nPeaks");
        resultIndex++;
    }
    
    
    
    /*
     *  Run data (accumulated over all processing)
     */
    DETECTOR_LOOP{
        Node *det_node = run_data->createGroup("detector",detIndex);
        
        
        POWDER_LOOP{
            Node * cl = det_node->createGroup("class",powderClass);
            // Mean and sigma
            FOREACH_DATAFORMAT_T(i_f, cDataVersion::DATA_FORMATS) {
                if (isBitOptionSet(global->detector[detIndex].powderFormat,*i_f)) {
                    cDataVersion dataV(NULL, &global->detector[detIndex], global->detector[detIndex].powderVersion, *i_f);
                    while (dataV.next()) {
                        if (*i_f != cDataVersion::DATA_FORMAT_RADIAL_AVERAGE) {
                            sprintf(sBuffer,"mean_%s",dataV.name);
                            cl->createDataset(sBuffer,H5T_NATIVE_DOUBLE,dataV.pix_nx,dataV.pix_ny);
                            sprintf(sBuffer,"sigma_%s",dataV.name);
                            cl->createDataset(sBuffer,H5T_NATIVE_DOUBLE,dataV.pix_nx,dataV.pix_ny);
                        } else {
                            sprintf(sBuffer,"mean_%s",dataV.name);
                            cl->createDataset(sBuffer,H5T_NATIVE_DOUBLE,dataV.pix_nn);
                            sprintf(sBuffer,"sigma_%s",dataV.name);
                            cl->createDataset(sBuffer,H5T_NATIVE_DOUBLE,dataV.pix_nn);
                        }
                    }
                }
            }
        }
        
        // Persistent background (median)
        if (global->detector[detIndex].useSubtractPersistentBackground) {
            sprintf(sBuffer,"perisistent_background");
            det_node->createDataset(sBuffer,H5T_NATIVE_FLOAT,global->detector[detIndex].pix_nx,global->detector[detIndex].pix_ny);
        }
        
        // Pixel value histogram
        if (global->detector[detIndex].histogram) {
            sprintf(sBuffer,"pixel_histogram");
            Node * hist_node = det_node->createGroup(sBuffer);
            sprintf(sBuffer,"histogram");
            hist_node->createDataset(sBuffer, H5T_NATIVE_UINT16, global->detector[detIndex].histogramNbins, global->detector[detIndex].histogram_nfs, global->detector[detIndex].histogram_nss);
            sprintf(sBuffer,"histogram_scale");
            hist_node->createDataset(sBuffer, H5T_NATIVE_FLOAT, global->detector[detIndex].histogramNbins)->write(global->detector[detIndex].histogramScale);
        }
        
    }
    
    
    
    /*
     *  Save cheetah information
     *  (processing and configuration information that is not really a result)
     */
    Node *cheetah = root->createGroup("cheetah");
    Node *configuration = cheetah->createGroup("configuration");

    // Write configuration file to file
    std::ifstream file(global->configFile, std::ios::binary);
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if(!file.read(buffer.data(), size)){
        ERROR("Could not read configuration file");
    }
    configuration->createDataset("input",H5T_NATIVE_CHAR,size)->write(&(buffer[0]));

    configuration->createDataset("cxi_version",H5T_NATIVE_INT,1)->write(&CXI::version);
    configuration->createDataset("cheetah_version_commit",H5T_NATIVE_CHAR,strlen(GIT_SHA1))->write(GIT_SHA1);
    char *psana_git_sha = getenv("PSANA_GIT_SHA");
    if (psana_git_sha)
        configuration->createDataset("psana_version_commit",H5T_NATIVE_CHAR,strlen(psana_git_sha))->write(psana_git_sha);

    
    
    
    /* For some reason the swmr version of hdf5 can't cope with string stacks larger than 255 characters */
    cheetah->createStack("eventName",H5T_NATIVE_CHAR,255);
    cheetah->createStack("frameNumber",H5T_NATIVE_LONG);
    cheetah->createStack("frameNumberIncludingSkipped",H5T_NATIVE_LONG);
    cheetah->createStack("threadID",H5T_NATIVE_LONG);
    
    //event_data->createStack("nPeaks",H5T_NATIVE_INT);
    //event_data->createStack("peakNpix",H5T_NATIVE_FLOAT);
    //DETECTOR_LOOP{
    //    Node * detector = event_data->createGroup("detector",detIndex);
    //    detector->createStack("sum",H5T_NATIVE_FLOAT);
    //}
    
    
    
  
    if (global->debugLevel > 2) {
        DEBUG("Skeleton created.");
    }
    
    #if defined H5F_ACC_SWMR_READ
    if(global->cxiSWMR){  
        root->closeAll();
        if(H5Fstart_swmr_write(root->hid()) < 0){
            ERROR("Cannot change to SWMR mode.\n");			
        }
        puts("Changed to SWMR mode.");
        root->openAll();
    }
    #endif
    
    H5Fflush(root->hid(), H5F_SCOPE_GLOBAL);
    //pthread_mutex_unlock(&global->saveCXI_mutex);
    
    return root;
}




/*
 *	CXI file handling
 */
static std::vector<std::string> openCXIFilenames = std::vector<std::string>();
static std::vector<CXI::Node* > openCXIFiles = std::vector<CXI::Node *>();

static std::vector<std::string> openResultsFilenames = std::vector<std::string>();
static std::vector<CXI::Node* > openResultsFiles = std::vector<CXI::Node *>();


// CXI file
static CXI::Node *getCXIFileByName(cGlobal *global, cEventData *eventData, int powderClass){
	char filename[MAX_FILENAME_LENGTH];
	long chunk;
	
	if(global->saveByPowderClass){
		// Powder class chunks according to number of frames in each powder class
		//chunk = global->detector[0].nPowderFrames[powderClass];
		chunk = global->nFramesSavedPerClass[powderClass];
		chunk = (long) floorf(chunk / (float) global->cxiChunkSize);
		sprintf(filename,"%s-r%04d-class%d-c%02ld.cxi", global->experimentID, global->runNumber, powderClass, chunk);
	}
	else{
		// All-in-one file chunks by number of hits
		chunk = global->nhits;
		chunk = (long) floorf(chunk / (float) global->cxiChunkSize);
		sprintf(filename,"%s-r%04d-c%02ld.cxi", global->experimentID, global->runNumber, chunk);
	}
	if (eventData != NULL)
		strcpy(eventData->filename, filename);


	/* search for filename in list */
	pthread_mutex_lock(&global->saveCXI_mutex);
	for(uint i=0; i<openCXIFilenames.size(); i++){
		if(openCXIFilenames[i] == std::string(filename)){
			DEBUG2("Found file pointer to already opened file.");
			pthread_mutex_unlock(&global->saveCXI_mutex);
			return openCXIFiles[i];
		}
	}

	/* Create new file if none found in list */
	DEBUG2("Creating a new file.");
	printf("Creating %s\n",filename);
	CXI::Node *cxi = createCXISkeleton(filename, global);
	openCXIFilenames.push_back(filename);
	openCXIFiles.push_back(cxi);
	
	pthread_mutex_unlock(&global->saveCXI_mutex);
	return cxi;
}

// Results file
static CXI::Node *getResultsFileByName(cGlobal *global, cEventData *eventData, int powderClass){
    char filename[MAX_FILENAME_LENGTH];
    long chunk;
    
    if(global->saveByPowderClass){
        // Powder class chunks according to number of frames in each powder class
        //chunk = global->detector[0].nPowderFrames[powderClass];
		chunk = global->nFramesSavedPerClass[powderClass];
        chunk = (long) floorf(chunk / (float) global->cxiChunkSize);
        sprintf(filename,"%s-r%04d-class%d-c%02ld.h5", global->experimentID, global->runNumber, powderClass, chunk);
    }
    else{
        // All-in-one file chunks by number of hits
        chunk = global->nhits;
        chunk = (long) floorf(chunk / (float) global->cxiChunkSize);
        sprintf(filename,"%s-r%04d-c%02ld.h5", global->experimentID, global->runNumber, chunk);
    }
    if (eventData != NULL)
        strcpy(eventData->filename, filename);
    
    
    /* search for filename in list */
    pthread_mutex_lock(&global->saveCXI_mutex);
    for(uint i=0; i<openResultsFilenames.size(); i++){
        if(openResultsFilenames[i] == std::string(filename)){
            DEBUG2("Found file pointer to already opened file.");
            pthread_mutex_unlock(&global->saveCXI_mutex);
            return openResultsFiles[i];
        }
    }
    
    /* Create new file if none found in list */
    DEBUG2("Creating a new file.");
    printf("Creating %s\n",filename);
    CXI::Node *results = createResultsSkeleton(filename, global);
    openResultsFilenames.push_back(filename);
    openResultsFiles.push_back(results);
    
    pthread_mutex_unlock(&global->saveCXI_mutex);
    return results;
}


void writeAccumulatedCXI(cGlobal * global){
	using CXI::Node;

	#ifdef H5F_ACC_SWMR_WRITE
	if(global->cxiSWMR){
		pthread_mutex_lock(&global->swmr_mutex);
	}
	#endif
	char    sBuffer[1024];


	DETECTOR_LOOP{
		POWDER_LOOP {
			
            CXI::Node *cxi = getResultsFileByName(global, NULL, powderClass);
			//CXI::Node *cxi = getCXIFileByName(global, NULL, powderClass);

			//if( cxi->stackCounter == 0)
			//	continue;
			
			pthread_mutex_lock(&global->saveCXI_mutex);
			Node & det_node = (*cxi)["run_data"].child("detector",detIndex);
			Node & cl = det_node.child("class",powderClass);
			FOREACH_DATAFORMAT_T(i_f, cDataVersion::DATA_FORMATS) {
				if (isBitOptionSet(global->detector[detIndex].powderFormat,*i_f)) {
					cDataVersion dataV(NULL, &global->detector[detIndex], global->detector[detIndex].powderVersion, *i_f);
					while (dataV.next()) {
						// mean and sigma
						long pix_nn =  dataV.pix_nn;
						double * powder = dataV.getPowder(powderClass);
						double * powder_squared = dataV.getPowderSquared(powderClass);     
						double * mean = (double*) calloc(pix_nn, sizeof(double));
						double * sigma = (double *) calloc(pix_nn,sizeof(double));
						for(long i = 0; i<pix_nn; i++){
							mean[i] = powder[i]/(1.*global->detector[detIndex].nPowderFrames[powderClass]);
                            // removed the absolute square since the rounding errors should now be negligible so that the variance is always positive
							sigma[i] =	sqrt((powder_squared[i] - powder[i]*powder[i]/(1.*global->detector[detIndex].nPowderFrames[powderClass]))/(1.*global->detector[detIndex].nPowderFrames[powderClass]));
                            //sigma[i] =	sqrt( fabs(powder_squared[i] - powder[i]*powder[i]/(1.*global->detector[detIndex].nPowderFrames[powderClass])) / (1.*global->detector[detIndex].nPowderFrames[powderClass]) );
						}
						sprintf(sBuffer,"mean_%s",dataV.name); 
						cl[sBuffer].write(mean, -1, pix_nn);
						sprintf(sBuffer,"sigma_%s",dataV.name); 
						cl[sBuffer].write(sigma, -1, pix_nn);
						free(mean);
						free(sigma);
					}      
				}
			}
			// Persistent background (median)
			if (global->detector[detIndex].useSubtractPersistentBackground) {
				long	pix_nn = global->detector[detIndex].pix_nn;
				float	*background = (float *) malloc(pix_nn*sizeof(float));
				if (global->detector[detIndex].subtractPersistentBackgroundMean) {
					global->detector[detIndex].frameBufferBlanks->copyMean(background);
				} else { 
					global->detector[detIndex].frameBufferBlanks->copyMedian(background);
				}
				sprintf(sBuffer,"perisistent_background"); 
				det_node[sBuffer].write(background, -1, pix_nn);
				free(background);
			}
			// Pixel value histogram
			if (global->detector[detIndex].histogram) {
				long     N = global->detector[detIndex].histogram_nfs *
					         global->detector[detIndex].histogram_nss *
					         global->detector[detIndex].histogramNbins;
				uint16_t *histData = global->detector[detIndex].histogramData;
				det_node["pixel_histogram"]["histogram"].write(histData, -1, N);
			}
			pthread_mutex_unlock(&global->saveCXI_mutex);
		}
	}
	
	#ifdef H5F_ACC_SWMR_WRITE
	if(global->cxiSWMR){
		pthread_mutex_unlock(&global->swmr_mutex);
	}
	#endif

}


/*
 *	Flush CXI file data
 */
static void  flushCXI(CXI::Node *cxi){
	//if( cxi->stackCounter == 0)
	//	return;
	H5Fflush(cxi->hid(), H5F_SCOPE_GLOBAL);
}


/* Flush each open file */
void flushCXIFiles(cGlobal * global){
	
    pthread_mutex_lock(&global->saveCXI_mutex);

    // CXI files
	for(uint i=0; i<openCXIFilenames.size(); i++){
		printf("Flushing %s\n",openCXIFilenames[i].c_str());
		flushCXI(openCXIFiles[i]);
		//usleep(100);
	}

    // Results files
    for(uint i=0; i<openResultsFilenames.size(); i++){
        printf("Flushing %s\n",openResultsFilenames[i].c_str());
        flushCXI(openResultsFiles[i]);
        //usleep(100);
    }
    pthread_mutex_unlock(&global->saveCXI_mutex);

}



/*
 *	Close CXI and results files
 */
static void  closeCXI(CXI::Node *cxi){

	//if( cxi->stackCounter == 0)
	//	return;

	cxi->trimAll();
	H5Fflush(cxi->hid(), H5F_SCOPE_GLOBAL);
	H5Fclose(cxi->hid());
	delete cxi;
}

/* Close each open file */
void closeCXIFiles(cGlobal * global){

	#if H5_VERS_MAJOR == 1 && H5_VERS_MINOR == 8 && H5_VERS_RELEASE < 9
	#warning "HDF5 < 1.8.9 contains a bug which makes it impossible to shrink certain datasets.\n"
	#warning "Please update your HDF5 to get properly truncated output files.\n"
	fprintf(stderr,"HDF5 < 1.8.9 contains a bug which makes it impossible to shrink certain datasets.\n");
	fprintf(stderr,"Please update your HDF5 to get properly truncated output files.\n");
	//#else
	#endif
	

	/* CXI: Go through each file and resize them to their right size */
	pthread_mutex_lock(&global->saveCXI_mutex);
	for(uint i=0; i<openCXIFilenames.size(); i++){
		printf("Closing %s\n",openCXIFilenames[i].c_str());
		closeCXI(openCXIFiles[i]);
	}
	openCXIFiles.clear();
	openCXIFilenames.clear();

	
    /* Results: Go through each file and resize them to their right size */
    for(uint i=0; i<openResultsFilenames.size(); i++){
        printf("Closing %s\n",openResultsFilenames[i].c_str());
        closeCXI(openResultsFiles[i]);
    }
    openResultsFiles.clear();
    openResultsFilenames.clear();

    pthread_mutex_unlock(&global->saveCXI_mutex);

	//#endif
	
	H5close();
}




void writeCXIHitstats(cEventData *eventData, cGlobal *global ){
	DEBUG2("Writing Hitstats.");

	#ifdef H5F_ACC_SWMR_WRITE
	if(global->cxiSWMR){
		pthread_mutex_lock(&global->swmr_mutex);
	}
	#endif
	/* Get the existing CXI file or open a new one */
	CXI::Node *results = getResultsFileByName(global, eventData, eventData->powderClass);

	pthread_mutex_lock(&global->saveCXI_mutex);
	(*results)["event_data"]["hit"].write(&eventData->hit,global->nCXIEvents);
	(*results)["event_data"]["nPeaks"].write(&eventData->nPeaks,global->nCXIEvents);
    (*results)["event_data"]["hitScore"].write(&eventData->hitScore,global->nCXIEvents);

	global->nCXIEvents += 1;
	#ifdef H5F_ACC_SWMR_WRITE
	if(global->cxiSWMR){
		pthread_mutex_unlock(&global->swmr_mutex);
	}
	#endif
	pthread_mutex_unlock(&global->saveCXI_mutex);
}


/*
 *  Write event data to CXI file
 */
void writeCXI(cEventData *eventData, cGlobal *global ){
	DEBUG2("Write a data of one frame to CXI file.");

    using CXI::Node;
    char sBuffer[1024];
	
    cMyTimer timer_cxiWait;
    cMyTimer timer_cxiWrite;

    timer_cxiWait.start();
    
    
    // Stuff only needed for SWMR mode
    #ifdef H5F_ACC_SWMR_WRITE
	bool didDecreaseActive = false;
	if(global->cxiSWMR){
		pthread_mutex_lock(&global->nActiveThreads_mutex);
		if (global->nActiveCheetahThreads) {
			global->nActiveCheetahThreads--;
			didDecreaseActive = true;
		}
		pthread_mutex_unlock(&global->nActiveThreads_mutex);
		pthread_mutex_lock(&global->swmr_mutex);
	}
	#endif

    
    
    /*
	 *	Get the existing CXI and Results file or open a new one
	 *	(needs &global->saveCXI_mutex unlocked)
	 */
	CXI::Node *cxi = getCXIFileByName(global, eventData, eventData->powderClass);
    CXI::Node *results = getResultsFileByName(global, eventData, eventData->powderClass);

	/*
	 *	Get position in CXI stack
	 *	And set same stack slice for results file to ensure synchronisation
	 */
	uint stackSlice;
	stackSlice = cxi->getStackSlice();
    eventData->stackSlice = stackSlice;
    //results->setStackSlice(stackSlice);
	results->stackCounter = cxi->stackCounter;


    /*
     *	Lock writing to one thread at a time to ensure stack synchronisation
     */
    pthread_mutex_lock(&global->saveCXI_mutex);
	global->nFramesSavedPerClass[eventData->powderClass] += 1;
    global->nCXIHits += 1;
    pthread_mutex_unlock(&global->saveCXI_mutex);       // Moved up here on 23 May, should work.... revert if problems
    timer_cxiWait.stop();

    
    /*
     *  Write CXI and results data
     */
    timer_cxiWrite.start();
    writeCXIData(cxi, eventData, global, stackSlice);
    writeResultsData(results, eventData, global, stackSlice);
    timer_cxiWrite.stop();
    global->timeProfile.addToTimer(timer_cxiWait.duration, global->timeProfile.TIMER_H5WAIT);
    global->timeProfile.addToTimer(timer_cxiWrite.duration, global->timeProfile.TIMER_H5WRITE);


    
    
    #ifdef H5F_ACC_SWMR_WRITE
        if(global->cxiSWMR){
            if(global->cxiFlushPeriod && (stackSlice % global->cxiFlushPeriod) == 0){
                H5Fflush(cxi->hid(),H5F_SCOPE_LOCAL);
            }
            
            if (didDecreaseActive) {
                pthread_mutex_lock(&global->nActiveThreads_mutex);
                global->nActiveCheetahThreads++;
                pthread_mutex_unlock(&global->nActiveThreads_mutex);
            }
            pthread_mutex_unlock(&global->swmr_mutex);
        }
    #endif

	
	/*
	 *	Update text file log
	 *  (If changing what's in the file, paste the new version into function saveCXI.cpp-->writeCXI() and saveFrame.cpp-->writeHDF5 to avoid incompatibilities)
	 *  Beamtime hack at 2am - fix this with one function later.
	 */
	pthread_mutex_lock(&global->framefp_mutex);
	fprintf(global->cleanedfp, "r%04u/%s/%s, %li, %i, %g, %g, %g, %g, %g\n",global->runNumber, eventData->eventSubdir, eventData->eventname, eventData->frameNumber, eventData->nPeaks, eventData->peakNpix, eventData->peakTotal, eventData->peakResolution, eventData->peakResolutionA, eventData->peakDensity);
	pthread_mutex_unlock(&global->framefp_mutex);
    
    
    // Stuff only needed for SWMR mode
    #ifdef H5F_ACC_SWMR_WRITE
        if(global->cxiSWMR){
            if(global->cxiFlushPeriod && (stackSlice % global->cxiFlushPeriod) == 0){
                H5Fflush(cxi->hid(),H5F_SCOPE_LOCAL);
            }
            
            if (didDecreaseActive) {
                pthread_mutex_lock(&global->nActiveThreads_mutex);
                global->nActiveCheetahThreads++;
                pthread_mutex_unlock(&global->nActiveThreads_mutex);
            }
            pthread_mutex_unlock(&global->swmr_mutex);
        }
    #endif

}




/*
 *  Write data into the CXI file
 */
void writeCXIData(CXI::Node *cxi, cEventData *eventData, cGlobal *global, uint stackSlice ){
    
    using CXI::Node;
    Node &root = *cxi;

    char sBuffer[1024];
    
    
    //printf("WriteCXI: powderClass=%i, stackSlice=%u\n",eventData->powderClass, stackSlice);
    
    
    double en = eventData->photonEnergyeV * 1.60217646e-19;
    root["entry_1"]["instrument_1"]["source_1"]["energy"].write(&en,stackSlice);
    root["entry_1"]["experiment_identifier"].write(eventData->eventname,stackSlice);

    
    /*
     *  Write instrument information
     */
    // LCLS
    if(!strcmp(global->facility, "LCLS")) {
        //Node &lcls = root["LCLS"];
        Node &lcls = root["instrument"];

        lcls["photon_energy_eV"].write(&eventData->photonEnergyeV,stackSlice);
        lcls["photon_wavelength_A"].write(&eventData->wavelengthA,stackSlice);
        lcls["machineTime"].write(&eventData->seconds,stackSlice);
		lcls["machineTimeNanoSeconds"].write(&eventData->nanoSeconds, stackSlice);
        lcls["fiducial"].write(&eventData->fiducial,stackSlice);
        DETECTOR_LOOP{
            lcls.cxichild("detector",detIndex+1)["position"].write(&global->detector[detIndex].detectorZ,stackSlice);
            lcls.cxichild("detector",detIndex+1)["EncoderValue"].write(&global->detector[detIndex].detectorEncoderValue,stackSlice);
        }
        
        if(global->cxiLegacyFileFormat == 2015) {
            lcls["ebeamCharge"].write(&eventData->fEbeamCharge,stackSlice);
            lcls["ebeamL3Energy"].write(&eventData->fEbeamL3Energy,stackSlice);
            lcls["ebeamLTUAngX"].write(&eventData->fEbeamLTUAngX,stackSlice);
            lcls["ebeamLTUAngY"].write(&eventData->fEbeamLTUAngY,stackSlice);
            lcls["ebeamLTUPosX"].write(&eventData->fEbeamLTUPosX,stackSlice);
            lcls["ebeamLTUPosY"].write(&eventData->fEbeamLTUPosY,stackSlice);
            lcls["ebeamPkCurrBC2"].write(&eventData->fEbeamPkCurrBC2,stackSlice);
            lcls["phaseCavityTime1"].write(&eventData->phaseCavityTime1,stackSlice);
            lcls["phaseCavityTime2"].write(&eventData->phaseCavityTime2,stackSlice);
            lcls["phaseCavityCharge1"].write(&eventData->phaseCavityCharge1,stackSlice);
            lcls["phaseCavityCharge2"].write(&eventData->phaseCavityCharge2,stackSlice);
            lcls["f_11_ENRC"].write(&eventData->gmd11,stackSlice);
            lcls["f_12_ENRC"].write(&eventData->gmd12,stackSlice);
            lcls["f_21_ENRC"].write(&eventData->gmd12,stackSlice);
            lcls["f_22_ENRC"].write(&eventData->gmd22,stackSlice);
        
            // Time tool trace
            if(eventData->TimeTool_present && eventData->TimeTool_hproj != NULL) {
                lcls["timeToolTrace"].write(&(eventData->TimeTool_hproj[0]), stackSlice);
            }
            // FEE spectrometer
            if(eventData->FEEspec_present) {
                lcls["FEEspectrum"].write(&(eventData->FEEspec_hproj[0]), stackSlice);
            }
            // eSpectrum in CXI hutch
            if(global->espectrum) {
                lcls["CXIespectrum"].write(&(eventData->energySpectrum1D[0]), stackSlice);
            }
            
            // EPICS
            for (int i=0; i < global->nEpicsPvFloatValues; i++ ) {
                lcls[&global->epicsPvFloatAddresses[i][0]].write(&(eventData->epicsPvFloatValues[i]), stackSlice);
            }
            
            // Laser on code
            int LaserOnVal = (eventData->pumpLaserCode)?1:0;
            lcls["evr41"].write(&LaserOnVal,stackSlice);

            // Event time string
            char timestr[26];
            time_t eventTime = eventData->seconds;
            ctime_r(&eventTime,timestr);
            lcls["eventTimeString"].write(timestr,stackSlice);
        
        }
        
    }

	// European XFEL
	if(!strcmp(global->facility, "EuXFEL") ) {
		//Node *lcls = root->createGroup("LCLS");
		Node &euxfel = root["instrument"];

		euxfel["photon_energy_eV"].write(&eventData->photonEnergyeV,stackSlice);
		euxfel["photon_wavelength_A"].write(&eventData->wavelengthA,stackSlice);
		euxfel["machineTime"].write(&eventData->seconds,stackSlice);

		euxfel["trainID"].write(&eventData->trainID,stackSlice);
		euxfel["pulseID"].write(&eventData->pulseID,stackSlice);
		euxfel["cellID"].write(&eventData->cellID,stackSlice);
		
		DETECTOR_LOOP{
			euxfel.cxichild("detector",detIndex+1)["position"].write(&global->detector[detIndex].detectorZ,stackSlice);
			euxfel.cxichild("detector",detIndex+1)["EncoderValue"].write(&global->detector[detIndex].detectorEncoderValue,stackSlice);
		}
	}
	

    // APS
    if(!strcmp(global->facility, "APS") || !strcmp(global->facility, "GMCA")) {
        Node &aps = root["instrument"];

        aps["timestamp"].write(eventData->timeString,stackSlice);
        aps["photon_energy_eV"].write(&eventData->photonEnergyeV,stackSlice);
        aps["photon_wavelength_A"].write(&eventData->wavelengthA,stackSlice);
        aps["detectorDistance"].write(&eventData->detectorDistance,stackSlice);

        if(global->cxiLegacyFileFormat == 2015) {
            aps["exposureTime"].write(&eventData->exposureTime,stackSlice);
            aps["exposurePeriod"].write(&eventData->exposurePeriod, stackSlice);
            aps["tau"].write(&eventData->tau,stackSlice);
            aps["countCutoff"].write(&eventData->countCutoff,stackSlice);
            aps["nExcludedPixels"].write(&eventData->nExcludedPixels,stackSlice);
            aps["beamX"].write(&eventData->beamX,stackSlice);
            aps["beamY"].write(&eventData->beamY,stackSlice);
            aps["startAngle"].write(&eventData->startAngle,stackSlice);
            aps["detector2Theta"].write(&eventData->detector2Theta,stackSlice);
            aps["angleIncrement"].write(&eventData->angleIncrement,stackSlice);
            aps["shutterTime"].write(&eventData->shutterTime,stackSlice);
            aps["threshold"].write(&eventData->threshold,stackSlice);

            aps["detector"].write(eventData->detectorName,stackSlice);
        }
    }

    
    /*
     *  Write detector frames (main purpose of CXI file format)
     */
    if(global->cxiSaveFrames) {
        DETECTOR_LOOP {
            /* Save assembled image under image groups */
            Node & detector = root["entry_1"]["instrument_1"].cxichild("detector",detIndex+1);
            double tmp = global->detector[detIndex].detectorZ/1000.0;
            
            // For convenience dereference some detector specific variables
            int asic_nx = global->detector[detIndex].asic_nx;
            int asic_ny = global->detector[detIndex].asic_ny;
            int asic_nn = asic_nx * asic_ny;
            int nasics_x = global->detector[detIndex].nasics_x;
            int nasics_y = global->detector[detIndex].nasics_y;
            int nasics = nasics_x * nasics_y;
            long pix_nx = global->detector[detIndex].pix_nx;
            long pix_ny = global->detector[detIndex].pix_ny;
            long pix_nn = global->detector[detIndex].pix_nn;
            float* pix_x = global->detector[detIndex].pix_x;
            float* pix_y = global->detector[detIndex].pix_y;
            float* pix_z = global->detector[detIndex].pix_z;
            float pixelSize = global->detector[detIndex].pixelSize;
            long image_nx = global->detector[detIndex].image_nx;
            long image_ny = global->detector[detIndex].image_ny;
            long image_nn = global->detector[detIndex].image_nn;
            long imageXxX_nx = global->detector[detIndex].imageXxX_nx;
            long imageXxX_ny = global->detector[detIndex].imageXxX_ny;
            long imageXxX_nn = global->detector[detIndex].imageXxX_nn;
            long radial_nn = global->detector[detIndex].radial_nn;
            
            detector["distance"].write(&tmp,stackSlice);
            if (!strcmp(global->facility, "APS") || !strcmp(global->facility, "GMCA")) {
                detector["distance"].write(&eventData->detectorDistance, stackSlice);
            }
            detector["x_pixel_size"].write(&pixelSize,stackSlice);
            detector["y_pixel_size"].write(&pixelSize,stackSlice);
            
            // DATA_FORMAT_NON_ASSEMBLED
            if (isBitOptionSet(global->detector[detIndex].saveFormat, cDataVersion::DATA_FORMAT_NON_ASSEMBLED)) {
                cDataVersion dataV(&eventData->detector[detIndex], &global->detector[detIndex], global->detector[detIndex].saveVersion, cDataVersion::DATA_FORMAT_NON_ASSEMBLED);
                while (dataV.next()) {
                    float * data = dataV.getData();
                    uint16_t * pixelmask = dataV.getPixelmask();

                    // Non-assembled images, modular (4D: N_frames x N_modules x Ny_module x Nx_module)
                    if (global->saveModular){
                        sprintf(sBuffer,"modular_%s",dataV.name);
                        Node & data_node = detector[sBuffer];
                        
                        long nn = asic_nn*nasics;
                        float * dataModular = (float *) calloc(nn, sizeof(float));
                        stackModulesData(data, dataModular, asic_nx, asic_ny, nasics_x, nasics_y);
                        data_node["data"].write(dataModular, stackSlice, nn);
                        free(dataModular);
                        
                        nn = nasics*3;
                        float * cornerPos = (float *) calloc(nn, sizeof(float));
                        cornerPositions(cornerPos, pix_x, pix_y, pix_z, pixelSize, asic_nx, asic_ny, nasics_x, nasics);
                        detector["corner_positions"].write(cornerPos, stackSlice, nn);
                        free(cornerPos);
                        
                        nn = nasics*2*3;
                        float * basisVec = (float *) calloc(nn, sizeof(float));
                        basisVectors(basisVec, pix_x, pix_y, pix_z, asic_nx, asic_ny, nasics_x, nasics);
                        data_node["basis_vectors"].write(basisVec, stackSlice, nn);
                        free(basisVec);
                        
                        nn = nasics*CXI::stringSize;
                        char * moduleId = (char *) calloc(nn, sizeof(char));
                        moduleIdentifier(moduleId, nasics_x*nasics_y, CXI::stringSize);
                        data_node["module_identifier"].write(moduleId, stackSlice, nn);
                        free(moduleId);
                        
                        if(global->detector[detIndex].savePixelmask){
                            nn = asic_nn*nasics_x*nasics_y;
                            uint16_t* maskModular = (uint16_t *) calloc(nn, sizeof(uint16_t));
                            stackModulesMask(eventData->detector[detIndex].pixelmask, maskModular, asic_nx, asic_ny, nasics_x, nasics_y);
                            data_node["mask"].write(maskModular,stackSlice, nn);
                            free(maskModular);
                        }
                    }

                    // Non-assembled images (3D: N_frames x Ny_frame x Nx_frame)
                    else {
                        Node &data_node = detector[dataV.name_version];
                        data_node["data"].write(data, stackSlice, pix_nn);
                        if(global->detector[detIndex].savePixelmask) {
                            data_node["mask"].write(pixelmask, stackSlice, pix_nn);
                        }
                        if(global->detector[detIndex].saveThumbnail) {
                            long nn = (pix_nx/CXI::thumbnailScale) * (pix_ny/CXI::thumbnailScale);
                            float *thumbnail = generateThumbnail(data, pix_nx, pix_ny, CXI::thumbnailScale);
                            data_node["thumbnail"].write(thumbnail, stackSlice, nn);
                            delete [] thumbnail;
                        }
                    }
                }
            }
            
            // DATA_FORMAT_ASSEMBLED
            int image_counter = 0;
            if (isBitOptionSet(global->detector[detIndex].saveFormat, cDataVersion::DATA_FORMAT_ASSEMBLED)) {
                int i_image = detIndex+1;
                image_counter += 1;
                cDataVersion dataV(&eventData->detector[detIndex], &global->detector[detIndex], global->detector[detIndex].saveVersion, cDataVersion::DATA_FORMAT_ASSEMBLED);
                while (dataV.next()) {
                    // Assembled images (3D: N_frames x Ny_image x Nx_image)
                    float * data = dataV.getData();
                    uint16_t * pixelmask = dataV.getPixelmask();
                    Node & data_node = root["entry_1"].cxichild("image",i_image)[dataV.name_version];
                    data_node["data"].write(data, stackSlice, image_nn);
                    if(global->detector[detIndex].savePixelmask){
                        data_node["mask"].write(pixelmask, stackSlice, image_nn);
                    }
                    if(global->detector[detIndex].saveThumbnail) {
                        long nn = (image_nx/CXI::thumbnailScale) * (image_ny/CXI::thumbnailScale);
                        float * thumbnail = generateThumbnail(data,image_nx,image_ny,CXI::thumbnailScale);
                        data_node["thumbnail"].write(thumbnail, stackSlice, nn);
                        data_node["data_type"].write("intensities", stackSlice);
                        data_node["data_space"].write("diffraction", stackSlice);
                        delete [] thumbnail;
                    }
                }
            }
            
            // DATA_FORMAT_ASSEMBLED_AND_DOWNSAMPLED
            if (isBitOptionSet(global->detector[detIndex].saveFormat, cDataVersion::DATA_FORMAT_ASSEMBLED_AND_DOWNSAMPLED)) {
                int i_image = global->nDetectors*image_counter+detIndex+1;
                image_counter += 1;
                cDataVersion dataV(&eventData->detector[detIndex], &global->detector[detIndex], global->detector[detIndex].saveVersion, cDataVersion::DATA_FORMAT_ASSEMBLED_AND_DOWNSAMPLED);
                while (dataV.next()) {
                    // Assembled images (3D: N_frames x Ny_imageXxX x Nx_imageXxX)
                    float * data = dataV.getData();
                    uint16_t * pixelmask = dataV.getPixelmask();
                    Node & data_node = root["entry_1"].cxichild("image",i_image)[dataV.name_version];
                    data_node["data"].write(data, stackSlice, imageXxX_nn);
                    if(global->detector[detIndex].savePixelmask){
                        data_node["mask"].write(pixelmask, stackSlice, imageXxX_nn);
                    }
                    if(global->detector[detIndex].saveThumbnail) {
                        long nn = (imageXxX_nx/CXI::thumbnailScale) * (imageXxX_ny/CXI::thumbnailScale);
                        float * thumbnail = generateThumbnail(data,imageXxX_nx,imageXxX_ny,CXI::thumbnailScale);
                        data_node["thumbnail"].write(thumbnail, stackSlice, nn);
                        data_node["data_type"].write("intensities", stackSlice);
                        data_node["data_space"].write("diffraction", stackSlice);
                        delete [] thumbnail;
                    }
                }
            }
            
            // DATA_FORMAT_RADIAL_AVERAGE
            if(global->cxiLegacyFileFormat == 2015) {
                if (isBitOptionSet(global->detector[detIndex].saveFormat, cDataVersion::DATA_FORMAT_RADIAL_AVERAGE)) {
                    int i_image = global->nDetectors*image_counter+detIndex+1;
                    image_counter += 1;
                    cDataVersion dataV(&eventData->detector[detIndex], &global->detector[detIndex], global->detector[detIndex].saveVersion, cDataVersion::DATA_FORMAT_RADIAL_AVERAGE);
                    while (dataV.next()) {
                        // Radial average (2D: N_frames x N_radial)
                        float * data = dataV.getData();
                        uint16_t * pixelmask = dataV.getPixelmask();
                        Node & data_node = root["entry_1"].cxichild("image",i_image)[dataV.name_version];
                        data_node["data"].write(data, stackSlice, radial_nn);
                        if(global->detector[detIndex].savePixelmask){
                            data_node["mask"].write(pixelmask, stackSlice, radial_nn);
                        }
                        data_node["data_type"].write("intensities", stackSlice);
                        data_node["data_space"].write("diffraction", stackSlice);
                    }
                }
            }
        }
    }
    // End of save frames
    
    /*
     *  Peak information 
     */
    int resultIndex = 1;
    if(global->savePeakInfo && global->hitfinder) {
        long nPeaks = eventData->peaklist.nPeaks;
        long powderClass = eventData->powderClass;
        
        Node & result = root["entry_1"].cxichild("result",resultIndex);
        
        result["powderClass"].write(&powderClass, stackSlice);
        result["nPeaks"].write(&nPeaks, stackSlice);
        
        result["peakXPosAssembled"].write(eventData->peaklist.peak_com_x_assembled, stackSlice, nPeaks, true);
        result["peakYPosAssembled"].write(eventData->peaklist.peak_com_y_assembled, stackSlice, nPeaks, true);
        
        result["peakXPosRaw"].write(eventData->peaklist.peak_com_x, stackSlice, nPeaks, true);
        result["peakYPosRaw"].write(eventData->peaklist.peak_com_y, stackSlice, nPeaks, true);
        
        result["peakTotalIntensity"].write(eventData->peaklist.peak_totalintensity, stackSlice, nPeaks, true);
        result["peakMaximumValue"].write(eventData->peaklist.peak_maxintensity, stackSlice, nPeaks, true);
        result["peakSNR"].write(eventData->peaklist.peak_snr, stackSlice, nPeaks, true);
        result["peakNPixels"].write(eventData->peaklist.peak_npix, stackSlice, nPeaks, true);
    }
    

    /*
     *  TOF spectrometer
     */
    if(global->cxiLegacyFileFormat == 2015) {
        if(eventData->TOFPresent){
            for(int i = 0; i<global->nTOFDetectors;i++){
                int tofDetIndex = i+global->nDetectors;
                Node & detector = root["entry_1"]["instrument_1"].cxichild("detector",tofDetIndex+1);
                detector["data"].write(&(eventData->tofDetector[i].voltage[0]),stackSlice);
                detector["tofTime"].write(&(eventData->tofDetector[i].time[0]),stackSlice);
            }
        }
    }
    
    
    /*
     *  Cheetah info
     */
    if(global->cxiLegacyFileFormat == 2015) {
        Node & event_data = root["cheetah"]["event_data"];
        event_data["eventName"].write(eventData->eventname,stackSlice);
        event_data["frameNumber"].write(&eventData->frameNumber,stackSlice);
        event_data["frameNumberIncludingSkipped"].write(&eventData->frameNumberIncludingSkipped,stackSlice);
        event_data["threadID"].write(&eventData->threadNum,stackSlice);
        event_data["nPeaks"].write(&eventData->nPeaks,stackSlice);
        event_data["peakNpix"].write(&eventData->peakNpix,stackSlice);
        
        event_data["peakTotal"].write(&eventData->peakTotal,stackSlice);
        event_data["peakResolution"].write(&eventData->peakResolution,stackSlice);
        event_data["peakResolutionA"].write(&eventData->peakResolutionA,stackSlice);
        event_data["peakDensity"].write(&eventData->peakDensity,stackSlice);
        event_data["imageClass"].write(&eventData->powderClass,stackSlice);
        
        event_data["hit"].write(&eventData->hit,stackSlice);
        
        DETECTOR_LOOP{
            Node & detector = root["cheetah"]["global_data"].cxichild("detector",detIndex+1);
            detector["lastBgUpdate"].write(&global->detector[detIndex].bgLastUpdate,stackSlice);
            detector["nHot"].write(&global->detector[detIndex].nHot,stackSlice);
            detector["lastHotPixUpdate"].write(&global->detector[detIndex].hotPixLastUpdate,stackSlice);
            detector["nNoisy"].write(&global->detector[detIndex].nNoisy,stackSlice);
            detector["lastNoisyPixUpdate"].write(&global->detector[detIndex].noisyPixLastUpdate,stackSlice);
            Node & detector2 = root["cheetah"]["event_data"].cxichild("detector",detIndex+1);
            detector2["sum"].write(&eventData->detector[detIndex].sum,stackSlice);		
        }
    }
    
    
    /*
     *  Some specific sample info
     */
    //if(global->samplePosXPV[0] || global->samplePosYPV[0] || global->samplePosZPV[0] || global->sampleVoltage[0]){
    //    root["entry_1"]["sample_1"]["geometry_1"]["translation"].write(eventData->samplePos,stackSlice);
    //    root["entry_1"]["sample_1"]["injection_1"]["voltage"].write(eventData->sampleVoltage,stackSlice);
    //}
    


}

/*
 *  Write data into the results file
 */
void writeResultsData(CXI::Node *results, cEventData *eventData, cGlobal *global, uint stackSlice ){
 
    using CXI::Node;
    Node &root = *results;
    
    
    // Event identifier
    root["event_data"]["event_identifier"].write(eventData->eventname,stackSlice);

    
    /*
     *  Write instrument information
     */
    // LCLS
    if(!strcmp(global->facility, "LCLS")) {
        Node &lcls = root["instrument"];
        DETECTOR_LOOP{
            lcls.child("detector",detIndex)["position"].write(&global->detector[detIndex].detectorZ,stackSlice);
            lcls.child("detector",detIndex)["EncoderValue"].write(&global->detector[detIndex].detectorEncoderValue,stackSlice);
            //lcls.child("detector",detIndex)["SolidAngleConst"].write(&global->detector[detIndex].solidAngleConst,stackSlice);
        }
        lcls["machineTime"].write(&eventData->seconds,stackSlice);
        lcls["machineTimeNanoSeconds"].write(&eventData->nanoSeconds, stackSlice);
        lcls["fiducial"].write(&eventData->fiducial,stackSlice);
        lcls["ebeamCharge"].write(&eventData->fEbeamCharge,stackSlice);
        lcls["ebeamL3Energy"].write(&eventData->fEbeamL3Energy,stackSlice);
        lcls["ebeamLTUAngX"].write(&eventData->fEbeamLTUAngX,stackSlice);
        lcls["ebeamLTUAngY"].write(&eventData->fEbeamLTUAngY,stackSlice);
        lcls["ebeamLTUPosX"].write(&eventData->fEbeamLTUPosX,stackSlice);
        lcls["ebeamLTUPosY"].write(&eventData->fEbeamLTUPosY,stackSlice);
        lcls["ebeamPkCurrBC2"].write(&eventData->fEbeamPkCurrBC2,stackSlice);
        lcls["phaseCavityTime1"].write(&eventData->phaseCavityTime1,stackSlice);
        lcls["phaseCavityTime2"].write(&eventData->phaseCavityTime2,stackSlice);
        lcls["phaseCavityCharge1"].write(&eventData->phaseCavityCharge1,stackSlice);
        lcls["phaseCavityCharge2"].write(&eventData->phaseCavityCharge2,stackSlice);
        lcls["photon_energy_eV"].write(&eventData->photonEnergyeV,stackSlice);
        lcls["photon_wavelength_A"].write(&eventData->wavelengthA,stackSlice);
        lcls["f_11_ENRC"].write(&eventData->gmd11,stackSlice);
        lcls["f_12_ENRC"].write(&eventData->gmd12,stackSlice);
        lcls["f_21_ENRC"].write(&eventData->gmd12,stackSlice);
        lcls["f_22_ENRC"].write(&eventData->gmd22,stackSlice);
        
        // Time tool trace
        if(eventData->TimeTool_present && eventData->TimeTool_hproj != NULL) {
            lcls["timeToolTrace"].write(&(eventData->TimeTool_hproj[0]), stackSlice);
        }
        // FEE spectrometer
        if(eventData->FEEspec_present) {
            lcls["FEEspectrum"].write(&(eventData->FEEspec_hproj[0]), stackSlice);
        }
        // eSpectrum in CXI hutch
        if(global->espectrum) {
            lcls["CXIespectrum"].write(&(eventData->energySpectrum1D[0]), stackSlice);
        }
        
        // EPICS
        for (int i=0; i < global->nEpicsPvFloatValues; i++ ) {
            lcls[&global->epicsPvFloatAddresses[i][0]].write(&(eventData->epicsPvFloatValues[i]), stackSlice);
        }
		
		for (int i=0; i < global->nEvrValuesToSave; i++ ) {
			char evrStr[100];
			sprintf(evrStr, "evr%i", global->evrValuesToSave[i]);
			int	evrVal = eventData->evrValue[i];
			lcls[evrStr].write(&(eventData->evrValue[i]), stackSlice);
		}
		
        // LaserEventCode
        //int LaserOnVal = (eventData->pumpLaserCode)?1:0;
		lcls["pumpLaserOn"].write(&eventData->pumpLaserOn,stackSlice);
        lcls["pumpLaserCode"].write(&eventData->pumpLaserCode,stackSlice);
		
        
        // Time string
        char timestr[26];
        time_t eventTime = eventData->seconds;
        ctime_r(&eventTime,timestr);
        lcls["eventTimeString"].write(timestr,stackSlice);
    }

	// European XFEL
	if(!strcmp(global->facility, "EuXFEL") ) {
		//Node *lcls = root->createGroup("LCLS");
		Node &euxfel = root["instrument"];
		
		euxfel["photon_energy_eV"].write(&eventData->photonEnergyeV,stackSlice);
		euxfel["photon_wavelength_A"].write(&eventData->wavelengthA,stackSlice);
		euxfel["machineTime"].write(&eventData->seconds,stackSlice);
		
		euxfel["trainID"].write(&eventData->trainID,stackSlice);
		euxfel["pulseID"].write(&eventData->pulseID,stackSlice);
		euxfel["cellID"].write(&eventData->cellID,stackSlice);
		
		
		//DETECTOR_LOOP{
		//	euxfel.child("detector",detIndex)["position"].write(&global->detector[detIndex].detectorZ,stackSlice);
		//	euxfel.child("detector",detIndex)["EncoderValue"].write(&global->detector[detIndex].detectorEncoderValue,stackSlice);
		//}
	}
	

	
    
    // APS
    if(!strcmp(global->facility, "APS") || !strcmp(global->facility, "GMCA")) {
        Node &aps = root["instrument"];
        aps["exposureTime"].write(&eventData->exposureTime,stackSlice);
        aps["exposurePeriod"].write(&eventData->exposurePeriod, stackSlice);
        aps["tau"].write(&eventData->tau,stackSlice);
        aps["countCutoff"].write(&eventData->countCutoff,stackSlice);
        aps["nExcludedPixels"].write(&eventData->nExcludedPixels,stackSlice);
        aps["detectorDistance"].write(&eventData->detectorDistance,stackSlice);
        aps["beamX"].write(&eventData->beamX,stackSlice);
        aps["beamY"].write(&eventData->beamY,stackSlice);
        aps["startAngle"].write(&eventData->startAngle,stackSlice);
        aps["detector2Theta"].write(&eventData->detector2Theta,stackSlice);
        aps["angleIncrement"].write(&eventData->angleIncrement,stackSlice);
        aps["shutterTime"].write(&eventData->shutterTime,stackSlice);
        aps["photon_energy_eV"].write(&eventData->photonEnergyeV,stackSlice);
        aps["photon_wavelength_A"].write(&eventData->wavelengthA,stackSlice);
        aps["timestamp"].write(eventData->timeString,stackSlice);
        aps["threshold"].write(&eventData->threshold,stackSlice);
        aps["detector"].write(eventData->detectorName,stackSlice);
    }
    

    /*
     *  Cheetah information 
     */
    Node & cheetah = root["cheetah"];
    cheetah["eventName"].write(eventData->eventname,stackSlice);
    cheetah["frameNumber"].write(&eventData->frameNumber,stackSlice);
    cheetah["frameNumberIncludingSkipped"].write(&eventData->frameNumberIncludingSkipped,stackSlice);
    cheetah["threadID"].write(&eventData->threadNum,stackSlice);
    //cheetah["nPeaks"].write(&eventData->nPeaks,stackSlice);
    //cheetah["peakNpix"].write(&eventData->peakNpix,stackSlice);
    
    DETECTOR_LOOP{
        Node & detector = root["event_data"].child("detector",detIndex);
        detector["lastBgUpdate"].write(&global->detector[detIndex].bgLastUpdate,stackSlice);
        detector["nHot"].write(&global->detector[detIndex].nHot,stackSlice);
        //detector["lastHotPixUpdate"].write(&global->detector[detIndex].hotPixLastUpdate,stackSlice);
        detector["nNoisy"].write(&global->detector[detIndex].nNoisy,stackSlice);
        //detector["lastNoisyPixUpdate"].write(&global->detector[detIndex].noisyPixLastUpdate,stackSlice);
        //Node & detector2 = root["cheetah"]["event_data"].child("detector",detIndex);
        //detector2["sum"].write(&eventData->detector[detIndex].sum,stackSlice);
    }
    
    
    /*
     *  Hit score
     * this is done in writeCXIHitstats(cEventData *eventData, cGlobal *global ){}
     */
    //float hitScore = eventData->hitScore;
    //Node &result = root["event_data"];
    //result["hitScore"].write(&hitScore, stackSlice);

  
    /*
     *  Peaks
     */
    int resultIndex = 0;
    if(global->savePeakInfo && global->hitfinder) {
        long nPeaks = eventData->peaklist.nPeaks;
        long powderClass = eventData->powderClass;
        
        Node &peaks = root["event_data"].child("peaks",resultIndex);
        
        peaks["nPeaks"].write(&nPeaks, stackSlice);
        peaks["powderClass"].write(&powderClass, stackSlice);
        peaks["peakTotal"].write(&eventData->peakTotal,stackSlice);
        peaks["peakResolution"].write(&eventData->peakResolution,stackSlice);
        peaks["peakResolutionA"].write(&eventData->peakResolutionA,stackSlice);
        peaks["peakDensity"].write(&eventData->peakDensity,stackSlice);
        peaks["imageClass"].write(&eventData->powderClass,stackSlice);
        peaks["hit"].write(&eventData->hit,stackSlice);
        
        peaks["peakXPosAssembled"].write(eventData->peaklist.peak_com_x_assembled, stackSlice, nPeaks, true);
        peaks["peakYPosAssembled"].write(eventData->peaklist.peak_com_y_assembled, stackSlice, nPeaks, true);
        
        peaks["peakXPosRaw"].write(eventData->peaklist.peak_com_x, stackSlice, nPeaks, true);
        peaks["peakYPosRaw"].write(eventData->peaklist.peak_com_y, stackSlice, nPeaks, true);
        
        peaks["peakTotalIntensity"].write(eventData->peaklist.peak_totalintensity, stackSlice, nPeaks, true);
        peaks["peakMaximumValue"].write(eventData->peaklist.peak_maxintensity, stackSlice, nPeaks, true);
        peaks["peakSNR"].write(eventData->peaklist.peak_snr, stackSlice, nPeaks, true);
        peaks["peakNPixels"].write(eventData->peaklist.peak_npix, stackSlice, nPeaks, true);
    }
    

    

    //
    // Sample position
    //
    //if(global->samplePosXPV[0] || global->samplePosYPV[0] || global->samplePosZPV[0] || global->sampleVoltage[0]){
    //    root["entry_1"]["sample_1"]["geometry_1"]["translation"].write(eventData->samplePos,stackSlice);
    //    root["entry_1"]["sample_1"]["injection_1"]["voltage"].write(eventData->sampleVoltage,stackSlice);
    //}
    
    
    /*
     *  Radial averages (NOT individual frames)
     */
    DETECTOR_LOOP {
        /* Save assembled image under image groups */
        Node & detector = root["event_data"].child("detector",detIndex);
        
        // For convenience dereference some detector specific variables
        //int asic_nx = global->detector[detIndex].asic_nx;
        //int asic_ny = global->detector[detIndex].asic_ny;
        //int asic_nn = asic_nx * asic_ny;
        //int nasics_x = global->detector[detIndex].nasics_x;
        //int nasics_y = global->detector[detIndex].nasics_y;
        //int nasics = nasics_x * nasics_y;
        //long pix_nx = global->detector[detIndex].pix_nx;
        //long pix_ny = global->detector[detIndex].pix_ny;
        //long pix_nn = global->detector[detIndex].pix_nn;
        //float* pix_x = global->detector[detIndex].pix_x;
        //float* pix_y = global->detector[detIndex].pix_y;
        //float* pix_z = global->detector[detIndex].pix_z;
        float pixelSize = global->detector[detIndex].pixelSize;
        //long image_nx = global->detector[detIndex].image_nx;
        //long image_ny = global->detector[detIndex].image_ny;
        //long image_nn = global->detector[detIndex].image_nn;
        //long imageXxX_nx = global->detector[detIndex].imageXxX_nx;
        //long imageXxX_ny = global->detector[detIndex].imageXxX_ny;
        //long imageXxX_nn = global->detector[detIndex].imageXxX_nn;
        long radial_nn = global->detector[detIndex].radial_nn;
        
        double tmp = global->detector[detIndex].detectorZ/1000.0;
        detector["distance"].write(&tmp,stackSlice);
        detector["x_pixel_size"].write(&pixelSize,stackSlice);
        detector["y_pixel_size"].write(&pixelSize,stackSlice);

        int image_counter = 0;
        
        
        // DATA_FORMAT_RADIAL_AVERAGE
        if (isBitOptionSet(global->detector[detIndex].saveFormat, cDataVersion::DATA_FORMAT_RADIAL_AVERAGE)) {
            //int i_image = global->nDetectors*image_counter+detIndex;
            image_counter += 1;
            cDataVersion dataV(&eventData->detector[detIndex], &global->detector[detIndex], global->detector[detIndex].saveVersion, cDataVersion::DATA_FORMAT_RADIAL_AVERAGE);
            while (dataV.next()) {
                // Radial average (2D: N_frames x N_radial)
                float * data = dataV.getData();
                uint16_t * pixelmask = dataV.getPixelmask();
                Node & det_node = root["event_data"].child("detector",detIndex);
                Node & data_node = det_node["radialaverage"][dataV.name_version];
                data_node["data"].write(data, stackSlice, radial_nn);
                if(global->detector[detIndex].savePixelmask){
                    data_node["mask"].write(pixelmask, stackSlice, radial_nn);
                }
                data_node["data_type"].write("intensities", stackSlice);
                data_node["data_space"].write("diffraction", stackSlice);
            }
        }
    }
    
    //
    // TOF data
    //
    if(eventData->TOFPresent){
        for(int i = 0; i<global->nTOFDetectors;i++){
            int tofDetIndex = i+global->nDetectors;
            Node &detector = root["event_data"].child("detector",tofDetIndex);
            detector["data"].write(&(eventData->tofDetector[i].voltage[0]),stackSlice);
            detector["tofTime"].write(&(eventData->tofDetector[i].time[0]),stackSlice);
        }
    }

	
    
}



