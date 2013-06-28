/*--------------------------------------------------------------------------
   Author: Thomas Nowotny
  
   Institute: Institute for Nonlinear Dynamics
              University of California San Diego
              La Jolla, CA 92093-0402
  
   email to:  tnowotny@ucsd.edu
  
   initial version: 2002-09-26
  
--------------------------------------------------------------------------*/

#ifndef _POISSONIZH_CC_
#define _POISSONIZH_CC_

#include "IzhEx_CODE/runner.cc"

classol::classol()
{
  modelDefinition(model);
  pattern= new unsigned int[model.neuronN[0]*PATTERNNO];
  baserates= new unsigned int[model.neuronN[0]];
  allocateMem();
  initialize();
  sumPN= 0;
  sumIzh1= 0;
}

void classol::init(unsigned int which)
{
  if (which == CPU) {
    theRates= baserates;
  }
  if (which == GPU) {
    copyGToDevice(); 
    copyStateToDevice();
    theRates= d_baserates;
  }
}

void classol::allocate_device_mem_patterns()
{
  unsigned int size;

  // allocate device memory for input patterns
  size= model.neuronN[0]*PATTERNNO*sizeof(unsigned int);
  checkCudaErrors(cudaMalloc((void**) &d_pattern, size));
  fprintf(stderr, "allocated %u elements for pattern.\n", size/sizeof(unsigned int));
  checkCudaErrors(cudaMemcpy(d_pattern, pattern, size, cudaMemcpyHostToDevice));
  size= model.neuronN[0]*sizeof(unsigned int);
  checkCudaErrors(cudaMalloc((void**) &d_baserates, size));
  checkCudaErrors(cudaMemcpy(d_baserates, baserates, size, cudaMemcpyHostToDevice)); 
}


void classol::free_device_mem()
{
  // clean up memory                          
                                       
  checkCudaErrors(cudaFree(d_pattern));
  checkCudaErrors(cudaFree(d_baserates));
}



classol::~classol()
{
  free(pattern);
  free(baserates);
}


void classol::read_PNIzh1syns(FILE *f)
{
  // version 1
  fprintf(stderr, "%u\n", model.neuronN[0]*model.neuronN[1]*sizeof(float));
  fread(gpPNIzh1, model.neuronN[0]*model.neuronN[1]*sizeof(float),1,f); //
  // version 2
  /*  unsigned int UIntSz= sizeof(unsigned int)*8;   // in bit!
  unsigned int logUIntSz= (int) (logf((float) UIntSz)/logf(2.0f)+1e-5f);
  unsigned int tmp= model.neuronN[0]*model.neuronN[1];
  unsigned size= (tmp >> logUIntSz);
  if (tmp > (size << logUIntSz)) size++;
  size= size*sizeof(unsigned int);
  is.read((char *)gpPNIzh1, size);*/

  // general:
  //assert(is.good());
  fprintf(stderr,"read PNIzh1 ... \n");
  fprintf(stderr, "values start with: \n");
  for(int i= 0; i < 20; i++) {
    fprintf(stderr, "%f ", gpPNIzh1[i]);
  }
  fprintf(stderr,"\n\n");
}


void classol::write_PNIzh1syns(FILE *f)
{
  fwrite(gpPNIzh1, model.neuronN[0]*model.neuronN[1]*sizeof(float),1,f);
  fprintf(stderr, "wrote PNIzh1 ... \n");
}


void classol::read_input_patterns(FILE *f)
{
  // we use a predefined pattern number
  fread(pattern, model.neuronN[0]*PATTERNNO*sizeof(unsigned int),1,f);
  fprintf(stderr, "read patterns ... \n");
  fprintf(stderr, "values start with: \n");
  for(int i= 0; i < 20; i++) {
    fprintf(stderr, "%d ", pattern[i]);
  }
  fprintf(stderr, "\n\n");
}

void classol::generate_baserates()
{
  // we use a predefined pattern number
  for (int i= 0; i < model.neuronN[0]; i++) {
    baserates[i]= INPUTBASERATE;
  }
  fprintf(stderr, "generated basereates ... \n");
  fprintf(stderr, "baserate value: %d ", INPUTBASERATE);
  fprintf(stderr, "\n\n");  
}

void classol::run(float runtime, unsigned int which)
{
  unsigned int pno;
  unsigned int offset= 0;
  int riT= (int) (runtime/DT);

  for (int i= 0; i < riT; i++) {
    if (iT%PAT_SETTIME == 0) {
      pno= (iT/PAT_SETTIME)%PATTERNNO;
      if (which == CPU)
	theRates= pattern;
      if (which == GPU)
	theRates= d_pattern;
      offset= pno*model.neuronN[0];
      fprintf(stderr, "setting pattern, pattern offset: %d\n", offset);
    }
    if (iT%PAT_SETTIME == PAT_FIRETIME) {
      if (which == CPU)
	theRates= baserates;
      if (which == GPU)
	theRates= d_baserates;
      offset= 0;
    }
    if (which == GPU)
       stepTimeGPU(theRates, offset, t);
    if (which == CPU)
       stepTimeCPU(theRates, offset, t);
    t+= DT;
    iT++;
  }
}

//--------------------------------------------------------------------------
// output functions

void classol::output_state(FILE *f, unsigned int which)
{
  if (which == GPU) 
    copyStateFromDevice();

  fprintf(f, "%f ", t);
  for (int i= 0; i < model.neuronN[0]; i++) {
    fprintf(f, "%f ", VPN[i]);
   }

   for (int i= 0; i < model.neuronN[1]; i++) {
     fprintf(f, "%f ", VIzh1[i]);
   }

  fprintf(f,"\n");
}

void classol::getSpikesFromGPU()
{
  copySpikesFromDevice();
}

void classol::getSpikeNumbersFromGPU() 
{
  copySpikeNFromDevice();
}

void classol::output_spikes(FILE *f, unsigned int which)
{
  for (int i= 0; i < glbscntPN; i++) {
    fprintf(f, "%f %d\n", t, glbSpkPN[i]);
  }
  for (int i= 0; i < glbscntIzh1; i++) {
    fprintf(f,  "%f %d\n", t, model.sumNeuronN[0]+glbSpkIzh1[i]);
  }
}

void classol::sum_spikes()
{
  sumPN+= glbscntPN;
  sumIzh1+= glbscntIzh1;
}


#endif	

