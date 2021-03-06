#include <Arduino.h>
#include <I2Cdev.h>
#include <ms5611.h>
#include <vertaccel.h>
#include <EEPROM.h>
#include <inv_mpu.h>
#include <kalmanvert.h>
#include <beeper.h>
#include <toneAC.h>
#include <SPI.h>
#include <avr/pgmspace.h>
#include <varioscreen.h>
#include <digit.h>
#include <SdCard.h>
#include <LightFat16.h>
#include <SerialNmea.h>
#include <NmeaParser.h>
#include <LxnavSentence.h>
#include <IGCSentence.h>
#include <FirmwareUpdater.h>

/*!!!!!!!!!!!!!!!!!!!!!!!*/
/* VARIOMETER STRUCTURE  */
/*!!!!!!!!!!!!!!!!!!!!!!!*/
#define HAVE_SPEAKER
#define HAVE_ACCELEROMETER
#define HAVE_SCREEN
#define HAVE_GPS
#define HAVE_SDCARD
#define HAVE_BLUETOOTH

#define VARIOSCREEN_DC_PIN 4
#define VARIOSCREEN_CS_PIN 3
#define VARIOSCREEN_RST_PIN 2
#define SDCARD_CS_PIN 14

//adjust if needed
#define VARIOSCREEN_CONTRAST 60

//GPS and bluetooth must have the same bauds
#define GPS_BLUETOOTH_BAUDS 9600

//you can try 800 on <8mhz microcontrollers (not always work)
#define FASTWIRE_SPEED 400

// the variometer seems to be more stable at half speed
// don't hesitate to experiment
#if F_CPU >= 16000000L
#define VARIOSCREEN_SPEED SPI_CLOCK_DIV4
#define SDCARD_SPEED SPI_CLOCK_DIV4
#else
#define VARIOSCREEN_SPEED SPI_CLOCK_DIV2
#define SDCARD_SPEED SPI_CLOCK_DIV2
#endif //CPU_FREQ

/*!!!!!!!!!!!!!!!!!!!!!!!*/
/* VARIOMETER PARAMETERS */
/*!!!!!!!!!!!!!!!!!!!!!!!*/
#define VARIOMETER_BEEP_VOLUME 10

#define VARIOMETER_SINKING_THRESHOLD -2.0
#define VARIOMETER_CLIMBING_THRESHOLD 0.2
#define VARIOMETER_NEAR_CLIMBING_SENSITIVITY 0.5

//#define VARIOMETER_ENABLE_NEAR_CLIMBING_ALARM
//#define VARIOMETER_ENABLE_NEAR_CLIMBING_BEEP

/* mean filter duration = filter size * 2 seconds */
#define VARIOMETER_SPEED_FILTER_SIZE 5

/* best precision is 100 */
#define VARIOMETER_GPS_ALTI_CALIBRATION_PRECISION_THRESHOLD 200
//#define VARIOMETER_SEND_CALIBRATED_ALTITUDE 

/* flight start detection */
#define FLIGHT_START_MIN_TIMESTAMP 15000
#define FLIGHT_START_VARIO_LOW_THRESHOLD (-0.5)
#define FLIGHT_START_VARIO_HIGH_THRESHOLD 0.5
#define FLIGHT_START_MIN_SPEED 10.0
//#define VARIOMETER_RECORD_WHEN_FLIGHT_START


/*******************/
/* General objects */
/*******************/
#define VARIOMETER_STATE_INITIAL 0
#define VARIOMETER_STATE_CALIBRATED 1
#define VARIOMETER_STATE_FLIGHT_STARTED 2

#ifdef HAVE_GPS
uint8_t variometerState = VARIOMETER_STATE_INITIAL;
#else
uint8_t variometerState = VARIOMETER_STATE_CALIBRATED;
#endif //HAVE_GPS

/*****************/
/* screen objets */
/*****************/
#ifdef HAVE_SCREEN
#ifdef HAVE_GPS 
#define VARIOSCREEN_ALTI_ANCHOR_X 58
#define VARIOSCREEN_ALTI_ANCHOR_Y 0
#define VARIOSCREEN_VARIO_ANCHOR_X 58
#define VARIOSCREEN_VARIO_ANCHOR_Y 2
#define VARIOSCREEN_SPEED_ANCHOR_X 22
#define VARIOSCREEN_SPEED_ANCHOR_Y 4
#define VARIOSCREEN_GR_ANCHOR_X 72
#define VARIOSCREEN_GR_ANCHOR_Y 4
#define RATIO_MAX_VALUE 30.0
#define RATIO_MIN_SPEED 10.0
#else
#define VARIOSCREEN_ALTI_ANCHOR_X 58
#define VARIOSCREEN_ALTI_ANCHOR_Y 1
#define VARIOSCREEN_VARIO_ANCHOR_X 58
#define VARIOSCREEN_VARIO_ANCHOR_Y 3
#endif //HAVE_GPS

VarioScreen screen(VARIOSCREEN_DC_PIN,VARIOSCREEN_CS_PIN,VARIOSCREEN_RST_PIN);
MSUnit msunit(screen);
MUnit munit(screen);
ScreenDigit altiDigit(screen, VARIOSCREEN_ALTI_ANCHOR_X, VARIOSCREEN_ALTI_ANCHOR_Y, 0, false);
ScreenDigit varioDigit(screen, VARIOSCREEN_VARIO_ANCHOR_X, VARIOSCREEN_VARIO_ANCHOR_Y, 1, true);
#ifdef HAVE_GPS 
ScreenDigit speedDigit(screen, VARIOSCREEN_SPEED_ANCHOR_X, VARIOSCREEN_SPEED_ANCHOR_Y, 0, false);
ScreenDigit ratioDigit(screen, VARIOSCREEN_GR_ANCHOR_X, VARIOSCREEN_GR_ANCHOR_Y, 1, false);
KMHUnit kmhunit(screen);
GRUnit grunit(screen);
#endif //HAVE_GPS
unsigned char screenStatus;
#endif //HAVE_SCREEN

/**********************/
/* alti/vario objects */
/**********************/
#define POSITION_MEASURE_STANDARD_DEVIATION 0.1
#ifdef HAVE_ACCELEROMETER 
#define ACCELERATION_MEASURE_STANDARD_DEVIATION 0.3
#else
#define ACCELERATION_MEASURE_STANDARD_DEVIATION 0.6
#endif //HAVE_ACCELEROMETER 

kalmanvert kalmanvert;

#ifdef HAVE_SPEAKER
beeper beeper(VARIOMETER_SINKING_THRESHOLD, VARIOMETER_CLIMBING_THRESHOLD, VARIOMETER_NEAR_CLIMBING_SENSITIVITY, VARIOMETER_BEEP_VOLUME);
#endif

/***************/
/* gps objects */
/***************/
#ifdef HAVE_GPS

NmeaParser nmeaParser;
uint8_t gpsDate[6];

#ifdef HAVE_BLUETOOTH
boolean lastSentence = false;
#endif //HAVE_BLUETOOTH

unsigned long RMCSentenceTimestamp; //for the speed filter
double RMCSentenceCurrentAlti; //for the speed filter
unsigned long speedFilterTimestamps[VARIOMETER_SPEED_FILTER_SIZE];
double speedFilterSpeedValues[VARIOMETER_SPEED_FILTER_SIZE];
double speedFilterAltiValues[VARIOMETER_SPEED_FILTER_SIZE];
int8_t speedFilterPos = 0;

#ifdef HAVE_SDCARD
lightfat16 file;
IGCHeader header;
IGCSentence igc;

#define SDCARD_STATE_INITIAL 0
#define SDCARD_STATE_INITIALIZED 1
#define SDCARD_STATE_READY 2
#define SDCARD_STATE_ERROR -1
int8_t sdcardState = SDCARD_STATE_INITIAL;

#endif //HAVE_SDCARD

#endif //HAVE_GPS

/*********************/
/* bluetooth objects */
/*********************/
#ifdef HAVE_BLUETOOTH
LxnavSentence lxnav;
#endif //HAVE_BLUETOOTH


/*-----------------*/
/*      SETUP      */
/*-----------------*/
void setup() {

  /****************/
  /* init SD Card */
  /****************/
  if( file.init(SDCARD_CS_PIN, SDCARD_SPEED) >= 0 ) {
    sdcardState = SDCARD_STATE_INITIALIZED;  //useless to set error
  }
 
  /***************/
  /* init screen */
  /***************/
#ifdef HAVE_SCREEN
  screen.begin(VARIOSCREEN_SPEED, VARIOSCREEN_CONTRAST);
  munit.display(VARIOSCREEN_ALTI_ANCHOR_X, VARIOSCREEN_ALTI_ANCHOR_Y);
  msunit.display(VARIOSCREEN_VARIO_ANCHOR_X, VARIOSCREEN_VARIO_ANCHOR_Y);
#ifdef HAVE_GPS
  kmhunit.display(VARIOSCREEN_SPEED_ANCHOR_X, VARIOSCREEN_SPEED_ANCHOR_Y);
  grunit.display(VARIOSCREEN_GR_ANCHOR_X, VARIOSCREEN_GR_ANCHOR_Y);
#endif //HAVE_GPS
#endif //HAVE_SCREEN
  
  /************************************/
  /* init altimeter and accelerometer */
  /************************************/
  Fastwire::setup(FASTWIRE_SPEED, 0);
  ms5611_init();
#ifdef HAVE_ACCELEROMETER
  vertaccel_init();
  if( firmwareUpdateCond() ) {
   firmwareUpdate();
  }
#endif //HAVE_ACCELEROMETER
  
  /**************************/
  /* init gps and bluetooth */
  /**************************/
#if defined(HAVE_BLUETOOTH) || defined(HAVE_GPS)
#ifdef HAVE_GPS
  serialNmea.begin(GPS_BLUETOOTH_BAUDS, true);
  
  /* get date on RMC sentence */
  while( !serialNmea.lockRMC() ) { //wait for RMC
  }
  
  uint8_t commaCount = 1; //serialNmea don't give the first comma
  while( commaCount < NMEA_PARSER_RMC_DATE_POS ) {  //wait for date
    if( serialNmea.read() == ',' ) {
      commaCount++;
    }
  }
  
  for(int i=0; i<6; i++) {  //read date
    gpsDate[i] = serialNmea.read();
  }
  serialNmea.release();  
  
#else
  serialNmea.begin(GPS_BLUETOOTH_BAUDS, false);
#endif //HAVE_GPS
#endif //defined(HAVE_BLUETOOTH) || defined(HAVE_GPS)
  
  /******************/
  /* get first data */
  /******************/
  
  /* wait for first alti and acceleration */
  while( ! (ms5611_dataReady()
#ifdef HAVE_ACCELEROMETER
            && vertaccel_dataReady()
#endif //HAVE_ACCELEROMETER
            ) ) {
  }
  
  /* get first data */
  ms5611_updateData();
#ifdef HAVE_ACCELEROMETER
  vertaccel_updateData();
#endif //HAVE_ACCELEROMETER

  /* init kalman filter */
  kalmanvert.init(ms5611_getAltitude(),
#ifdef HAVE_ACCELEROMETER
                  vertaccel_getValue(),
#else
                  0.0,
#endif
                  POSITION_MEASURE_STANDARD_DEVIATION,
                  ACCELERATION_MEASURE_STANDARD_DEVIATION,
                  millis());
   
}


/*----------------*/
/*      LOOP      */
/*----------------*/
void loop() {
 
  /*****************************/
  /* compute vertical velocity */
  /*****************************/
#ifdef HAVE_ACCELEROMETER
  if( ms5611_dataReady() && vertaccel_dataReady() ) {
    ms5611_updateData();
    vertaccel_updateData();

    kalmanvert.update( ms5611_getAltitude(),
                       vertaccel_getValue(),
                       millis() );
#else
  if( ms5611_dataReady() ) {
    ms5611_updateData();

    kalmanvert.update( ms5611_getAltitude(),
                       0.0,
                       millis() );
#endif //HAVE_ACCELEROMETER

    /* set beeper */
#ifdef HAVE_SPEAKER
    beeper.setVelocity( kalmanvert.getVelocity() );
#endif //HAVE_SPEAKER
    
  }

  /*****************/
  /* update beeper */
  /*****************/
#ifdef HAVE_SPEAKER
  beeper.update();
#endif //HAVE_SPEAKER

  /********************/
  /* update bluetooth */
  /********************/
#ifdef HAVE_BLUETOOTH
  /* in priority send vario nmea sentence */
  if( lxnav.availiable() ) {
    while( lxnav.availiable() ) {
       serialNmea.write( lxnav.get() );
    }
    
    serialNmea.release();
  }
#endif //HAVE_BLUETOOTH


  /**************/
  /* update GPS */
  /**************/
#ifdef HAVE_GPS
#ifdef HAVE_BLUETOOTH
  /* else try to parse GPS nmea */
  else {
#endif //HAVE_BLUETOOTH
    
    /* try to lock sentences */
    if( serialNmea.lockRMC() ) {
      RMCSentenceTimestamp = millis();
      RMCSentenceCurrentAlti = kalmanvert.getPosition(); //useless to take calibrated here
      nmeaParser.beginRMC();
    } else if( serialNmea.lockGGA() ) {
      nmeaParser.beginGGA();
#ifdef HAVE_BLUETOOTH
      lastSentence = true;
#endif //HAVE_BLUETOOTH
#ifdef HAVE_SDCARD      
      /* start to write IGC B frames */
      if( sdcardState == SDCARD_STATE_READY ) {
#ifdef VARIOMETER_SEND_CALIBRATED_ALTITUDE
        file.write( igc.begin( kalmanvert.getCalibratedPosition() ) );
#else
        file.write( igc.begin( kalmanvert.getPosition() ) );
#endif
      }
#endif //HAVE_SDCARD
    }
  
    /* parse if needed */
    if( nmeaParser.isParsing() ) {
      while( nmeaParser.isParsing() ) {
        uint8_t c = serialNmea.read();
        
        /* parse sentence */        
        nmeaParser.feed( c );

#ifdef HAVE_SDCARD          
        /* if GGA, convert to IGC and write to sdcard */
        if( sdcardState == SDCARD_STATE_READY && nmeaParser.isParsingGGA() ) {
          igc.feed(c);
          while( igc.availiable() ) {
            file.write( igc.get() );
          }
        }
#endif //HAVE_SDCARD
      }
      serialNmea.release();
    
#ifdef HAVE_BLUETOOTH   
      /* if this is the last GPS sentence */
      /* we can send our sentences */
      if( lastSentence ) {
          lastSentence = false;
#ifdef VARIOMETER_SEND_CALIBRATED_ALTITUDE
          lxnav.begin(kalmanvert.getCalibratedPosition(), kalmanvert.getVelocity());
#else
          lxnav.begin(kalmanvert.getPosition(), kalmanvert.getVelocity());
#endif
          serialNmea.lock(); //will be writed at next loop
      }
#endif //HAVE_BLUETOOTH
    }
  
  
    /***************************/
    /* update variometer state */
    /*    (after parsing)      */
    /***************************/
    if( variometerState < VARIOMETER_STATE_FLIGHT_STARTED ) {
      
      /* check if we need to calibrate */
      if( variometerState == VARIOMETER_STATE_INITIAL ) {
        
        /* we need a good quality value */
        if( nmeaParser.haveNewAltiValue() && nmeaParser.precision < VARIOMETER_GPS_ALTI_CALIBRATION_PRECISION_THRESHOLD ) {
          
          /* calibrate */
          double gpsAlti = nmeaParser.getAlti();
          kalmanvert.calibratePosition(gpsAlti);
          variometerState = VARIOMETER_STATE_CALIBRATED;
          
         
#if defined(HAVE_SDCARD) && ! defined(VARIOMETER_RECORD_WHEN_FLIGHT_START)
          /* start the sdcard record */
          if( sdcardState == SDCARD_STATE_INITIALIZED ) {
            if( file.begin() >= 0 ) {
              sdcardState = SDCARD_STATE_READY;
            
              /* write the header */
              int16_t datePos = header.begin();
              if( datePos >= 0 ) {
                while( datePos ) {
                  file.write(header.get());
                  datePos--;
                }
            
                for(datePos = 0; datePos < 6; datePos++) {
                  file.write(gpsDate[datePos]);
                  header.get();
                }
            
                while( header.availiable() ) {
                  file.write(header.get());
                }
              }
            } else {
              sdcardState = SDCARD_STATE_ERROR; //avoid retry 
            }
          }
#endif //defined(HAVE_SDCARD) && ! defined(VARIOMETER_RECORD_WHEN_FLIGHT_START)
        }
      }
      
      /* else check if the flight have started */
      else {  //variometerState == VARIOMETER_STATE_CALIBRATED
        
        /* check flight start condition */
        if( (millis() > FLIGHT_START_MIN_TIMESTAMP) &&
            (kalmanvert.getVelocity() < FLIGHT_START_VARIO_LOW_THRESHOLD || kalmanvert.getVelocity() > FLIGHT_START_VARIO_HIGH_THRESHOLD) &&
            (nmeaParser.getSpeed() > FLIGHT_START_MIN_SPEED) ) {
          variometerState = VARIOMETER_STATE_FLIGHT_STARTED;
              
          /* enable near climbing */
#ifdef VARIOMETER_ENABLE_NEAR_CLIMBING_ALARM
          beeper.setGlidingAlarmState(true);
#endif
#ifdef VARIOMETER_ENABLE_NEAR_CLIMBING_BEEP
          beeper.setGlidingBeepState(true);
#endif
#if defined(HAVE_SDCARD) && defined(VARIOMETER_RECORD_WHEN_FLIGHT_START)
          /* start the sdcard record */
          if( sdcardState == SDCARD_STATE_INITIALIZED ) {
            if( file.begin() >= 0 ) {
              sdcardState = SDCARD_STATE_READY;
            
              /* write the header */
              int16_t datePos = header.begin();
              if( datePos >= 0 ) {
                while( datePos ) {
                  file.write(header.get());
                  datePos--;
                }
            
                for(datePos = 0; datePos < 6; datePos++) {
                  file.write(gpsDate[datePos]);
                  header.get();
                }
            
                while( header.availiable() ) {
                  file.write(header.get());
                }
              }
            } else {
              sdcardState = SDCARD_STATE_ERROR; //avoid retry 
            }
          }
#endif//defined(HAVE_SDCARD) && defined(VARIOMETER_RECORD_WHEN_FLIGHT_START)
        }
      }
    }
#ifdef HAVE_BLUETOOTH
  }
#endif //HAVE_BLUETOOTH
#endif //HAVE_GPS

   
  /*****************/
  /* update screen */
  /*****************/
#ifdef HAVE_SCREEN
  /* alternate display : alti / vario */
  if( screenStatus == 0 ) {
    altiDigit.display( kalmanvert.getCalibratedPosition() );
    screenStatus = 1;
  } else {
    varioDigit.display( kalmanvert.getVelocity() );
    screenStatus = 0;
  }
  
#ifdef HAVE_GPS
  /* when getting speed from gps, display speed and ratio */
  if ( nmeaParser.haveNewSpeedValue() ) {

    /* get new values */
    unsigned long baseTime = speedFilterTimestamps[speedFilterPos];
    unsigned long deltaTime = RMCSentenceTimestamp; //delta computed later
    speedFilterTimestamps[speedFilterPos] = RMCSentenceTimestamp;
    
    double deltaAlti = speedFilterAltiValues[speedFilterPos]; //computed later
    speedFilterAltiValues[speedFilterPos] = RMCSentenceCurrentAlti; 

    double currentSpeed = nmeaParser.getSpeed();
    speedFilterSpeedValues[speedFilterPos] = currentSpeed;

    speedFilterPos++;
    if( speedFilterPos >= VARIOMETER_SPEED_FILTER_SIZE )
      speedFilterPos = 0;

    /* compute deltas */
    deltaAlti -= RMCSentenceCurrentAlti;
    deltaTime -= baseTime;
    
    /* compute mean distance */
    double meanDistance = 0;
    int step = 0;
    while( step < VARIOMETER_SPEED_FILTER_SIZE ) {

      /* compute distance */
      unsigned long currentTime = speedFilterTimestamps[speedFilterPos];
      meanDistance += speedFilterSpeedValues[speedFilterPos] * (double)(currentTime - baseTime);
      baseTime = currentTime;

      /* next */
      speedFilterPos++;
      if( speedFilterPos >= VARIOMETER_SPEED_FILTER_SIZE )
        speedFilterPos = 0;
      step++;
    }

    /* compute glide ratio */
    double ratio = (meanDistance/3600.0)/deltaAlti;

    /* display speed and ratio */    
    speedDigit.display( currentSpeed );
    if( currentSpeed >= RATIO_MIN_SPEED && ratio >= 0.0 && ratio < RATIO_MAX_VALUE ) {
      ratioDigit.display(ratio);
    } else {
      ratioDigit.display(0.0);
    }
  }
#endif //HAVE_GPS
#endif //HAVE_SCREEN 
}
