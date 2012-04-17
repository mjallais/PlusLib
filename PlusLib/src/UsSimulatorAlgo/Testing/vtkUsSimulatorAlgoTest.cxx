/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/ 

#include "PlusConfigure.h"
#include "vtksys/CommandLineArguments.hxx"
#include <iomanip>
#include <iostream>
#include "vtkXMLUtilities.h"


#include "vtkSmartPointer.h"
#include "vtkMatrix4x4.h"
#include "vtkMetaImageSequenceIO.h"
#include "vtkTrackedFrameList.h"
#include "vtkTransformRepository.h"
#include "TrackedFrame.h"
#include <vtkSTLReader.h>
#include "vtkPolyData.h"
#include "vtkTransform.h"
#include "vtkUsSimulatorAlgo.h"
#include "vtkImageData.h" 
#include "vtkMetaImageWriter.h"
#include "vtkPointData.h"
#include "vtkAppendPolyData.h"
#include "vtkTransformPolyDataFilter.h"
#include "vtkCubeSource.h"
#include "vtkJPEGWriter.h"
#include "vtkMetaImageWriter.h"
#include "vtkXMLImageDataWriter.h"
//display
#include "vtkImageActor.h"
#include "vtkActor.h"
#include "vtkRenderWindow.h"
#include "vtkRenderer.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkPolyDataMapper.h"
#include "vtkProperty.h"
#include "vtkInteractorStyleImage.h"

void CreateSliceModels(vtkTrackedFrameList *trackedFrameList, vtkTransformRepository *transformRepository, PlusTransformName &imageToReferenceTransformName, vtkPolyData *outputPolyData)
{
  // Prepare the output polydata.

  vtkSmartPointer< vtkAppendPolyData > appender = vtkSmartPointer< vtkAppendPolyData >::New();

  // Loop over each tracked image slice.
  for ( int frameIndex = 0; frameIndex < trackedFrameList->GetNumberOfTrackedFrames(); ++ frameIndex )
  {
    TrackedFrame* frame = trackedFrameList->GetTrackedFrame( frameIndex );

    // Update transform repository 
    if ( transformRepository->SetTransforms(*frame) != PLUS_SUCCESS )
    {
      LOG_ERROR("Failed to set repository transforms from tracked frame!"); 
      continue; 
    }

    vtkSmartPointer<vtkMatrix4x4> tUserDefinedMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
    if ( transformRepository->GetTransform(imageToReferenceTransformName, tUserDefinedMatrix) != PLUS_SUCCESS )
    {
      std::string strTransformName; 
      imageToReferenceTransformName.GetTransformName(strTransformName); 
      LOG_ERROR("Failed to get transform from repository: " << strTransformName ); 
      continue; 
    }

    vtkSmartPointer< vtkTransform > tUserDefinedTransform = vtkSmartPointer< vtkTransform >::New();
    tUserDefinedTransform->SetMatrix( tUserDefinedMatrix );    

    int* frameSize = frame->GetFrameSize();

    vtkSmartPointer< vtkTransform > tCubeToImage = vtkSmartPointer< vtkTransform >::New();
    tCubeToImage->Scale( frameSize[ 0 ], frameSize[ 1 ], 1 );
    tCubeToImage->Translate( 0.5, 0.5, 0.5 );  // Moving the corner to the origin.

    vtkSmartPointer< vtkTransform > tCubeToTracker = vtkSmartPointer< vtkTransform >::New();
    tCubeToTracker->Identity();
    tCubeToTracker->Concatenate( tUserDefinedTransform );
    tCubeToTracker->Concatenate( tCubeToImage );

    vtkSmartPointer< vtkTransformPolyDataFilter > CubeToTracker = vtkSmartPointer< vtkTransformPolyDataFilter >::New();
    CubeToTracker->SetTransform( tCubeToTracker );
    vtkSmartPointer< vtkCubeSource > source = vtkSmartPointer< vtkCubeSource >::New();
    CubeToTracker->SetInput( source->GetOutput() );
    CubeToTracker->Update();

    appender->AddInputConnection( CubeToTracker->GetOutputPort() );

  }  

  appender->Update();
  outputPolyData->DeepCopy(appender->GetOutput());

}



int main(int argc, char **argv)
{
  std::string inputModelFile;
  std::string inputTransformsFile;
  std::string inputConfigFile;
  std::string outputUsImageFile;


  int verboseLevel=vtkPlusLogger::LOG_LEVEL_DEFAULT;

  int numberOfFailures(0); 

  vtksys::CommandLineArguments args;
  args.Initialize(argc, argv);

  args.AddArgument("--input-model-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputModelFile, "File name of the input model, for which ultrasound images will be generated.");
  args.AddArgument("--input-config-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputConfigFile, "Config file containing the image to probe and phantom to reference transformations  ");
  args.AddArgument("--input-transforms-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputTransformsFile, "File containing coordinate frames and the associated model to image transformations"); 
  args.AddArgument("--output-us-img-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &outputUsImageFile, "File name of the generated output ultrasound image.");
  args.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &verboseLevel, "Verbose level (1=error only, 2=warning, 3=info, 4=debug, 5=trace)");

  ///////////////////////////////////////

  // Input arguments error checking

  if ( !args.Parse() )
  {
    std::cerr << "Problem parsing arguments" << std::endl;
    std::cout << "Help: " << args.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }

  vtkPlusLogger::Instance()->SetLogLevel(verboseLevel);

  if (inputModelFile.empty())
  {
    std::cerr << "--input-model-file required" << std::endl;
    exit(EXIT_FAILURE);
  }

  if (inputTransformsFile.empty())
  {
    std::cerr << "--input-config-file required " << std::endl;
    exit(EXIT_FAILURE);
  }
  if (inputConfigFile.empty())
  {
    std::cerr << "--input-transforms-file required" << std::endl;
    exit(EXIT_FAILURE);
  }
  if (outputUsImageFile.empty())
  {
    std::cerr << "--output-us-img-file required" << std::endl;
    exit(EXIT_FAILURE);
  }




  //Read transformations data 


  LOG_DEBUG("Reading input meta file..."); 
  vtkSmartPointer< vtkTrackedFrameList > trackedFrameList = vtkSmartPointer< vtkTrackedFrameList >::New(); 				
  trackedFrameList->ReadFromSequenceMetafile( inputTransformsFile.c_str() );
  LOG_DEBUG("Reading input meta file completed"); 

  // create repository for ultrasound images correlated to the iput tracked frames
  vtkSmartPointer<vtkTrackedFrameList> simulatedUltrasoundFrameList = vtkSmartPointer<vtkTrackedFrameList>::New(); 
  


  ///// Take out when implementing read all frames !!! 
  //int testFrameIndex=30;
  //trackedFrameList->RemoveTrackedFrameRange(0,testFrameIndex-1);
  //trackedFrameList->RemoveTrackedFrameRange(1,trackedFrameList->GetNumberOfTrackedFrames()-1);


  // Read config file
  LOG_DEBUG("Reading config file...")

  vtkSmartPointer<vtkXMLDataElement> configRead = vtkSmartPointer<vtkXMLDataElement>::Take(::vtkXMLUtilities::ReadElementFromFile(inputConfigFile.c_str())); 

  vtkSmartPointer<vtkTransformRepository> transformRepository = vtkSmartPointer<vtkTransformRepository>::New(); 
  if ( transformRepository->ReadConfiguration(configRead) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to read transforms for transform repository!"); 
    return EXIT_FAILURE; 
  }
  LOG_DEBUG("Reading config file finished.");

  PlusTransformName imageToReferenceTransformName; 
  const char transformNameString[]="ImageToPhantom";
  if ( imageToReferenceTransformName.SetTransformName(transformNameString)!= PLUS_SUCCESS )
  {
    LOG_ERROR("Invalid transform name: " << transformNameString ); 
    return EXIT_FAILURE; 
  }


  //Read model

  LOG_DEBUG("Reading in model stl file...");
  vtkSmartPointer<vtkSTLReader> modelReader = vtkSmartPointer<vtkSTLReader>::New();
  modelReader->SetFileName(inputModelFile.c_str());
  modelReader->Update();
  LOG_DEBUG("Finished reading model stl file."); 

  // Acquire modeldata in appropriate containers to prepare for filter

  vtkSmartPointer<vtkPolyData> model = vtkSmartPointer<vtkPolyData>::New(); 

  model= modelReader->GetOutput(); 

  //Setup Renderer to visualize surface model and ultrasound planes
  vtkSmartPointer<vtkRenderer> rendererPoly = vtkSmartPointer<vtkRenderer>::New();
  vtkSmartPointer<vtkRenderWindow> renderWindowPoly = vtkSmartPointer<vtkRenderWindow>::New();
  renderWindowPoly->AddRenderer(rendererPoly);
  vtkSmartPointer<vtkRenderWindowInteractor> renderWindowInteractorPoly = vtkSmartPointer<vtkRenderWindowInteractor>::New();
  renderWindowInteractorPoly->SetRenderWindow(renderWindowPoly);
  {
    vtkSmartPointer<vtkInteractorStyleTrackballCamera> style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New(); 
    renderWindowInteractorPoly->SetInteractorStyle(style);
  }

  // Visualization of the surface model
  {
    vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInput(model);  
    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    rendererPoly->AddActor(actor);
  }

  // Visualization of the image planes
  {
    vtkSmartPointer< vtkPolyData > slicesPolyData = vtkSmartPointer< vtkPolyData >::New();
    CreateSliceModels(trackedFrameList, transformRepository, imageToReferenceTransformName, slicesPolyData);
    vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInput(slicesPolyData);  
    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    rendererPoly->AddActor(actor);
  }

  renderWindowPoly->Render();
  //renderWindowInteractorPoly->Start();

 int x = trackedFrameList->GetNumberOfTrackedFrames();

  for(int i = 0; i<trackedFrameList->GetNumberOfTrackedFrames(); i++)
  {

    TrackedFrame* frame = trackedFrameList->GetTrackedFrame(i);
    // Update transform repository 
    if ( transformRepository->SetTransforms(*frame) != PLUS_SUCCESS )
    {
      LOG_ERROR("Failed to set repository transforms from tracked frame!"); 
      //continue;
    }   

       // We use the model coordinate system as reference coordinate system
    /*
      Alter to get new position? Or does it do it automatically ?
    */
    vtkSmartPointer<vtkMatrix4x4> imageToReferenceMatrix = vtkSmartPointer<vtkMatrix4x4>::New();   
    if ( transformRepository->GetTransform(imageToReferenceTransformName, imageToReferenceMatrix) != PLUS_SUCCESS )
    {
      std::string strTransformName; 
      imageToReferenceTransformName.GetTransformName(strTransformName); 
      LOG_ERROR("Failed to get transform from repository: " << strTransformName ); 
      //continue; 
    }
   

    vtkSmartPointer<vtkImageData> stencilBackgroundImage = vtkSmartPointer<vtkImageData>::New(); 
    stencilBackgroundImage->SetSpacing(1,1,1);
    stencilBackgroundImage->SetOrigin(0,0,0);

    int* frameSize = frame->GetFrameSize();
    stencilBackgroundImage->SetExtent(0,frameSize[0]-1,0,frameSize[1]-1,0,0);

    stencilBackgroundImage->SetScalarTypeToUnsignedChar();
    stencilBackgroundImage->SetNumberOfScalarComponents(1);
    stencilBackgroundImage->AllocateScalars(); 

    int* extent = stencilBackgroundImage->GetExtent();
    memset(stencilBackgroundImage->GetScalarPointer(), 0,
      ((extent[1]-extent[0]+1)*(extent[3]-extent[2]+1)*(extent[5]-extent[4]+1)*stencilBackgroundImage->GetScalarSize()*stencilBackgroundImage->GetNumberOfScalarComponents()));

    //{
    //  vtkSmartPointer<vtkMetaImageWriter> writer=vtkSmartPointer<vtkMetaImageWriter>::New();
    //  writer->SetInput(stencilBackgroundImage);
    //  writer->SetFileName("c:\\Users\\lasso\\devel\\PlusExperimental-bin\\PlusLib\\data\\TestImages\\stencilBackgroundImage.mha ");
    //  writer->Write();
    //}

    //prepare  filter and filter input 
    vtkSmartPointer< vtkUsSimulatorAlgo >  usSimulator ; 
    usSimulator = vtkSmartPointer<vtkUsSimulatorAlgo>::New(); 

    usSimulator->SetInput(model); 
    vtkSmartPointer<vtkMatrix4x4> modelToImageMatrix=vtkSmartPointer<vtkMatrix4x4>::New();
    modelToImageMatrix->DeepCopy(imageToReferenceMatrix);
    modelToImageMatrix->Invert();
    usSimulator->SetModelToImageMatrix(modelToImageMatrix); 
    usSimulator->SetStencilBackgroundImage(stencilBackgroundImage); 
    usSimulator->Update();

    vtkImageData* simOutput=usSimulator->GetOutputImage();

  

    double origin[3]={
      imageToReferenceMatrix->Element[0][3],
      imageToReferenceMatrix->Element[1][3],
      imageToReferenceMatrix->Element[2][3]};
      simOutput->SetOrigin(origin);
      double spacing[3]={
        sqrt(imageToReferenceMatrix->Element[0][0]*imageToReferenceMatrix->Element[0][0]+
        imageToReferenceMatrix->Element[1][0]*imageToReferenceMatrix->Element[1][0]+
        imageToReferenceMatrix->Element[2][0]*imageToReferenceMatrix->Element[2][0]),
        sqrt(imageToReferenceMatrix->Element[0][1]*imageToReferenceMatrix->Element[0][1]+
        imageToReferenceMatrix->Element[1][1]*imageToReferenceMatrix->Element[1][1]+
        imageToReferenceMatrix->Element[2][1]*imageToReferenceMatrix->Element[2][1]),
        1.0};
        simOutput->SetSpacing(spacing);
   
   
    PlusVideoFrame * simulatorOutputPlusVideoFrame = new PlusVideoFrame(); 
    simulatorOutputPlusVideoFrame->DeepCopyFrom(simOutput); 
    
    frame->SetImageData(*simulatorOutputPlusVideoFrame); 

  /*      {
          vtkSmartPointer<vtkMetaImageWriter> writer=vtkSmartPointer<vtkMetaImageWriter>::New();
          writer->SetInput(simOutput);
          writer->SetFileName("c:\\Users\\bartha\\devel\\PlusExperimental-bin\\PlusLib\\data\\TestImages\\simoutput1.mha");
          writer->Write();
        }*/

        vtkSmartPointer<vtkImageData> usImage = vtkSmartPointer<vtkImageData>::New(); 
        usImage->DeepCopy(simOutput);

        //-----------------------------------------------------------------------------------

        //display output of filter
        vtkSmartPointer<vtkImageActor> redImageActor = vtkSmartPointer<vtkImageActor>::New();

        redImageActor->SetInput(simOutput);


        // Visualize
        vtkSmartPointer<vtkRenderer> renderer = vtkSmartPointer<vtkRenderer>::New();

        // Red image is displayed
        renderer->AddActor(redImageActor);


        renderer->ResetCamera();

        vtkSmartPointer<vtkRenderWindow> renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
        renderWindow->AddRenderer(renderer);
        renderer->SetBackground(0, 72, 0);

        vtkSmartPointer<vtkRenderWindowInteractor> renderWindowInteractor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
        vtkSmartPointer<vtkInteractorStyleImage> style = vtkSmartPointer<vtkInteractorStyleImage>::New();

        renderWindowInteractor->SetInteractorStyle(style);

        renderWindowInteractor->SetRenderWindow(renderWindow);
        //renderWindowInteractor->Initialize();
        //renderWindowInteractor->Start();

        ////cleanup
        //simulatorOutputFrame->Delete(); 
        //simulatorOutputFrame = NULL; 
        //simulatorOutputPlusVideoFrame->Delete(); 
        //simulatorOutputPlusVideoFrame = NULL; 

  }


 
  vtkSmartPointer<vtkMetaImageSequenceIO> simulatedUsSequenceFileWriter = vtkSmartPointer<vtkMetaImageSequenceIO>::New(); 
  simulatedUsSequenceFileWriter->SetFileName(outputUsImageFile.c_str()); 
  simulatedUsSequenceFileWriter->SetTrackedFrameList(trackedFrameList); 
  simulatedUsSequenceFileWriter->Write(); 
  // final output writer 
  //vtkSmartPointer<vtkMetaImageWriter> usImageWriter=vtkSmartPointer<vtkMetaImageWriter>::New();
  //usImageWriter->SetFileName(outputUsImageFile.c_str());
  //
  //usImageWriter->SetInput(simOutput); 
  //usImageWriter->Write();



}