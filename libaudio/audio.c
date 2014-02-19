#include "audio.h"

//************************CALLBACK FUNCTION***********************************************************//
//*
static int recordCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData)
{
  
    record_t *r = userData;               
    const sample_t *rptr = inputBuffer;  


    (void) outputBuffer; // Prevent unused variable warnings. 
    (void) framesPerBuffer;
    (void) timeInfo;
    (void) statusFlags;

    //********Pull samples from input buffer***************************
    PaUtil_WriteRingBuffer(&r->rBufFromRT, &rptr[0], r->n_samples);     
    pthread_mutex_lock(&r->mutex);
    r->wakeup = true;
    pthread_cond_signal(&r->cond);  
    pthread_mutex_unlock(&r->mutex);
    //*******************

    return paContinue; // Keep recording stream active. CLOSED ELSEWHERE 
}
//END FUNCTION

//************************INITIALIZE MIC AND AUDIO SETTINGS**********************************************//
//*
bool record_init( 
   record_t*            r,
   size_t               sample_format,     
   size_t               sample_rate,      
   size_t               n_channels,    
   size_t	        n_samples )
{

   //***Initialize PA internal data structures******
   PaError err = paNoError;
   err = Pa_Initialize();  
   if(err != paNoError) return false;
   //*************

   //*****Initialize communication ring buffers******************** 
   unsigned long rb_samples_count = 16384; // Must be a power of 2
   r->rBufFromRTData = malloc(sizeof(sample_t) * rb_samples_count); 
   PaUtil_InitializeRingBuffer(&r->rBufFromRT, sizeof(sample_t), rb_samples_count, r->rBufFromRTData);
   //**************
   
   //******Initialize input device******* 
   PaStreamParameters inputParameters;
   inputParameters.device = Pa_GetDefaultInputDevice();  
   if(inputParameters.device == paNoDevice) return false;
   
   // Specify recording settings 
   inputParameters.channelCount = n_channels;                    
   inputParameters.sampleFormat = sample_format;
   inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
   inputParameters.hostApiSpecificStreamInfo = NULL;
   //***************

   //********Open the input stream******* 
   PaStream *stream=NULL;
   err = Pa_OpenStream(
      &stream,
      &inputParameters,
      NULL,              
      sample_rate,
      n_samples,
      paClipOff,          
      recordCallback,
      r);
   if(err != paNoError) return false;
   //**************

   
   //******Initialize thread settings******
   r->wakeup = false;
   r->cond   = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
   r->mutex  = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
   //**************

   //*********Store audio settings******
   r->stream = stream;
   r->sample_rate = sample_rate;
   r->n_channels = n_channels;
   r->n_samples = n_samples;

   /*r = (record_t) {
      //******Initialize thread settings******
      .wakeup = false,
      .cond   = PTHREAD_COND_INITIALIZER,
      .mutex  = PTHREAD_MUTEX_INITIALIZER,
      //**************

      //*********Store audio settings******
      .stream      = stream,
      .sample_rate = sample_rate,
      .n_channels  = n_channels,
      .n_samples   = n_samples
      //**************
   };*/
}
//END FUNCTION

//************************************START RECORDING FROM MIC*********************************************//
//*
bool record_start(record_t *r)
{
   if(Pa_StartStream(r->stream) != paNoError) // Start recording stream
      return false;   
}
//END FUNCTION

//***********************************ACCESS AND PROCESS RECORDED SAMPLES***********************************//
//*
void record_read(record_t *r, sample_t *samples)
{
   pthread_mutex_lock(&r->mutex);
   if(!r->wakeup)
      pthread_cond_wait(&r->cond, &r->mutex);
   r->wakeup = false;
   pthread_mutex_unlock(&r->mutex);
      
   PaUtil_ReadRingBuffer(&r->rBufFromRT, &samples[0], r->n_samples); 
}
//END FUNCTION

//*******************************TERMINATE ALLOCATED DATA FOR RECORDING**************************************//
//*
bool record_stop(record_t *r)
{
   if(Pa_CloseStream(r->stream) != paNoError) 
      return false;  // Close recording stream 
   
   Pa_Terminate();
   //*******************
   if (r->rBufFromRTData)
      free(r->rBufFromRTData);

   pthread_cond_destroy(&r->cond);
   pthread_mutex_destroy(&r->mutex);
   //**********
}
//END FUNCTION
