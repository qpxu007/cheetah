/*
 *  worker.h
 *  cspad_cryst
 *
 *  Created by Anton Barty on 6/2/11.
 *  Copyright 2011 CFEL. All rights reserved.
 *
 */




/*
 *	Global variables
 */
typedef struct {
	
	// Thread management
	int				nThreads;
	int				nActiveThreads;
	pthread_t		*threadID;
	pthread_mutex_t	nActiveThreads_mutex;


	// Detector geometry
	long			pix_nx;
	long			pix_ny;
	long			pix_nn;
	float			*pix_x;
	float			*pix_y;
	float			*pix_z;
	float			pix_dx;
	unsigned		module_rows;
	unsigned		module_cols;
	
} tGlobal;



/*
 *	Structure used for passing information to worker threads
 */
typedef struct {
	
	// Reference to common global structure
	tGlobal		*pGlobal;
	int			busy;
	
	// cspad data
	int			cspad_fail;
	float		quad_temperature[4];
	uint16_t	*quad_data[4];
	uint16_t	*raw_data;
	uint16_t	*image;
	
	
	// Beamline data, etc
	int			seconds;
	int			nanoSeconds;
	unsigned	fiducial;
	char		timeString[1024];
	bool		beamOn;
	
	double		gmd11;
	double		gmd12;
	double		gmd21;
	double		gmd22;
	
	double		ft1;
	double		ft2;
	double		c1;
	double		c2;

	
	
	// Thread management
	int		threadID;

	char	filename[1024];
	
	// Memory protection
	pthread_mutex_t	*nActiveThreads_mutex;
	
	
} tThreadInfo;



/*
 *	Stuff from Garth's original code
 */

// Static variables
using namespace std;
static CspadCorrector*      corrector;
static Pds::CsPad::ConfigV1 configV1;
static Pds::CsPad::ConfigV2 configV2;
static unsigned             configVsn;
static unsigned             quadMask;
static unsigned             asicMask;

static const unsigned  ROWS = 194;
static const unsigned  COLS = 185;

static uint32_t nevents = 0;

#define ERROR(...) fprintf(stderr, __VA_ARGS__)
#define STATUS(...) fprintf(stderr, __VA_ARGS__)



/*
 *	Function prototypes
 */
void *worker(void *);
int hdf5_write(const char*, const void*, int, int, int);
void setupThreads(tGlobal*);
void readDetectorGeometry(tGlobal*);

