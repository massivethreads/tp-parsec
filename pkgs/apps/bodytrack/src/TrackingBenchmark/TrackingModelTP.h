//-------------------------------------------------------------
//      ____                        _      _
//     / ___|____ _   _ ____   ____| |__  | |
//    | |   / ___| | | |  _  \/ ___|  _  \| |
//    | |___| |  | |_| | | | | |___| | | ||_|
//     \____|_|  \_____|_| |_|\____|_| |_|(_) Media benchmarks
//                  
//    2006, Intel Corporation, licensed under Apache 2.0 
//
//  file : TrackingModelOMP.h
//  author : Scott Ettinger - scott.m.ettinger@intel.com
//  description : Observation model for kinematic tree body 
//          tracking threaded with OpenMP.
//          
//  modified : 
//--------------------------------------------------------------

#ifndef TRACKINGMODELTP_H
#define TRACKINGMODELTP_H

#if defined(HAVE_CONFIG_H)
# include "config.h"
#endif

#include "TrackingModel.h"
#include <iostream>
#include <iomanip>
#include <sstream>

class TrackingModelTP : public TrackingModel {
  //Generate an edge map from the original camera image - threaded
  void CreateEdgeMap(FlexImage8u &src, FlexImage8u &dst);

 public:
  //load and process images - overloaded for future threading
  bool GetObservation(float timeval);
};


#if defined(HAVE_CONFIG_H)
# include "config.h"
#endif

#include <vector>
#include <string>
#include "system.h"

using namespace std;


//------------------------ Threaded versions of image filters --------------------

//TP threaded - 1D filter Row wise 1 channel any type data or kernel valid pixels only
template<class T, class T2>
bool FlexFilterRowVTP(FlexImage<T,1> &src, FlexImage<T,1> &dst, T2 *kernel, int kernelSize, bool allocate = true) {
  if(kernelSize % 2 == 0) //enforce odd length kernels
    return false;
  if(allocate)
    dst.Reallocate(src.Size());
  dst.Set((T)0);
  int n = kernelSize / 2, h = src.Height();
#ifdef COLLECT_LOOP_SIZES
  fprintf(out_ls, "%d\n", h);
  fprintf(stdout, "%d\n", np);
#endif  
  pfor(0, h, 1, GRAIN_SIZE,
       [n,&src,&dst,kernel] (int from, int to) {
         cilk_begin;
         for(int y = from; y < to; y++) {
           T *psrc = &src(n, y), *pdst = &dst(n, y);
           for(int x = n; x < src.Width() - n; x++) {
             int k = 0;
             T2 acc = 0;
             for(int i = -n; i <= n; i++) 
               acc += (T2)(psrc[i] * kernel[k++]);
             *pdst = (T)acc;
             pdst++;
             psrc++;
           }
         }
         cilk_void_return;
       });  
  return true;
}

//TP threaded - 1D filter Column wise 1 channel any type data or kernel valid pixels only
template<class T, class T2>
bool FlexFilterColumnVTP(FlexImage<T,1> &src, FlexImage<T,1> &dst, T2 *kernel, int kernelSize, bool allocate = true) {
  if(kernelSize % 2 == 0) //enforce odd length kernels
    return false;
  if(allocate)
    dst.Reallocate(src.Size());
  dst.Set((T)0);
  int n = kernelSize / 2;
  int sb = src.StepBytes(), h = src.Height() - n;
#ifdef COLLECT_LOOP_SIZES
  fprintf(out_ls, "%d\n", h - n);
  fprintf(stdout, "%d\n", np);
#endif  
  pfor(n, h, 1, GRAIN_SIZE,
       [n,sb,&src,&dst,kernel] (int from, int to) {
         cilk_begin;
         for(int y = from; y < to; y++) {
           T *psrc = &src(0, y), *pdst = &dst(0, y);
           for(int x = 0; x < src.Width(); x++) {
             int k = 0;
             T2 acc = 0;
             for(int i = -n; i <= n; i++) 
               acc += (T2)(*(T *)((char *)psrc + sb * i) * kernel[k++]);
             *pdst = (T)acc;
             pdst++;
             psrc++;
           }
         }
         cilk_void_return;
       });
  return true;
}

// ----------------------------------------------------------------------------------

//Generate an edge map from the original camera image
//Separable 7x7 gaussian filter - threaded
inline void GaussianBlurTP(FlexImage8u &src, FlexImage8u &dst) {
  float k[] = {0.12149085090552f, 0.14203719483447f, 0.15599734045770f, 0.16094922760463f, 0.15599734045770f, 0.14203719483447f, 0.12149085090552f};
  FlexImage8u tmp;
  FlexFilterRowVTP(src, tmp, k, 7); //separable gaussian convolution using kernel k
  FlexFilterColumnVTP(tmp, dst, k, 7);
}

//Calculate gradient magnitude and threshold to binarize - threaded
inline FlexImage8u GradientMagThresholdTP(FlexImage8u &src, float threshold) {
  FlexImage8u r(src.Size());
  ZeroBorder(r);
#ifdef COLLECT_LOOP_SIZES
  fprintf(out_ls, "%d\n", src.Height() - 2);
  fprintf(stdout, "%d\n", np);
#endif  
  pfor(1, src.Height() - 1, 1, GRAIN_SIZE,
       [&src,threshold,&r] (int from, int to) {
         cilk_begin;
         for(int y = from; y < to; y++) { //for each pixel
           Im8u *p = &src(1,y), *ph = &src(1,y - 1), *pl = &src(1,y + 1), *pr = &r(1,y);
           for(int x = 1; x < src.Width() - 1; x++) {
             float xg = -0.125f * ph[-1] + 0.125f * ph[1] - 0.250f * p[-1] + 0.250f * p[1] - 0.125f * pl[-1] + 0.125f * pl[1]; //calc x and y gradients
             float yg = -0.125f * ph[-1] - 0.250f * ph[0] - 0.125f * ph[1] + 0.125f * pl[-1] + 0.250f * pl[0] + 0.125f * pl[1];
             float mag = xg * xg + yg * yg; //calc magnitude and threshold
             *pr = (mag < threshold) ? 0 : 255;
             p++; ph++; pl++; pr++;
           }
         }
         cilk_void_return;
       });
  return r;
}

//Generate an edge map from the original camera image
void TrackingModelTP::CreateEdgeMap(FlexImage8u &src, FlexImage8u &dst) {
  FlexImage8u gr = GradientMagThresholdTP(src, 16.0f); //calc gradient magnitude and threshold
  GaussianBlurTP(gr, dst); //Blur to create distance error map
}

//templated conversion to string with field width
template<class T>
inline string str(T n, int width = 0, char pad = '0') {
  stringstream ss;
  ss << setw(width) << setfill(pad) << n;
  return ss.str();
}

//load and process all images for new observation at a given time(frame)
//Overloaded from base class for future threading to overlap disk I/O with 
//generating the edge maps
bool TrackingModelTP::GetObservation(float timeval) {
  int frame = (int)timeval; //generate image filenames
  int n = mCameras.GetCameraCount();
  vector<string> FGfiles(n), ImageFiles(n);
  for(int i = 0; i < n; i++) {
    FGfiles[i] = mPath + "FG" + str(i + 1) + DIR_SEPARATOR + "image" + str(frame, 4) + ".bmp";
    ImageFiles[i] = mPath + "CAM" + str(i + 1) + DIR_SEPARATOR + "image" + str(frame, 4) + ".bmp";
  }
  FlexImage8u im;
  for(int i = 0; i < (int)FGfiles.size(); i++) {
    if(!FlexLoadBMP(FGfiles[i].c_str(), im)) { //Load foreground maps and raw images
      cout << "Unable to load image: " << FGfiles[i].c_str() << endl;
      return false;
    }  
    mFGMaps[i].ConvertToBinary(im); //binarize foreground maps to 0 and 1
    if(!FlexLoadBMP(ImageFiles[i].c_str(), im)) {
      cout << "Unable to load image: " << ImageFiles[i].c_str() << endl;
      return false;
    }
    CreateEdgeMap(im, mEdgeMaps[i]); //Create edge maps
  }
  return true;
}


/* TrackingModelTP2: version 2 of the tpswitch-based task parallel model */

#include <deque>
typedef vector<FlexImage<Im8u,1>> ImageSet;
typedef vector<BinaryImage> BinaryImageSet;
vector<ImageSet>       mImageBuffer; /* image buffer */
vector<BinaryImageSet> mFGBuffer;    /* foreground image buffer */
  
static void load_image(string path, int cameras, int frames, int frame) {
  cilk_begin;
  int n = cameras;
  vector<string> FGfiles(n), ImageFiles(n);
  /* Generate image filenames */
  for(int i = 0; i < n; i++) {
    FGfiles[i] = path + "FG" + str(i + 1) + DIR_SEPARATOR + "image" + str(frame, 4) + ".bmp";
    ImageFiles[i] = path + "CAM" + str(i + 1) + DIR_SEPARATOR + "image" + str(frame, 4) + ".bmp";
  }
  ImageSet images(n);
  BinaryImageSet FGImages(n);
  for(int i = 0; i < (int)FGfiles.size(); i++) {
    FlexImage8u im;
    if(!FlexLoadBMP(FGfiles[i].c_str(), im)) { //Load foreground maps and raw images
      cout << "Unable to load image: " << FGfiles[i].c_str() << endl;
      exit(1);
    }  
    FGImages[i].ConvertToBinary(im); //binarize foreground maps to 0 and 1
    if(!FlexLoadBMP(ImageFiles[i].c_str(), images[i])) {
      cout << "Unable to load image: " << ImageFiles[i].c_str() << endl;
      exit(1);
    }
  }
  mImageBuffer.push_back(images);
  mFGBuffer.push_back(FGImages);
  cilk_void_return;
}

static void load_images(string path, int cameras, int frames) {
  int frame = 0;
  while (frame < frames) {
    call_task(spawn load_image(path, cameras, frames, frame));
    printf("loaded images of frame %d\n", frame);
    frame++;
  }
}

class TrackingModelTP2 : public TrackingModel {
  void CreateEdgeMap(FlexImage8u &src, FlexImage8u &dst);

 public:
  
  TrackingModelTP2() {};
  
  ~TrackingModelTP2() {};
  
  bool GetObservation(float timeval);
  void LoadImages(int frames);
};

void TrackingModelTP2::CreateEdgeMap(FlexImage8u &src, FlexImage8u &dst) {
  FlexImage8u gr = GradientMagThresholdTP(src, 16.0f); //calc gradient magnitude and threshold
  GaussianBlurTP(gr, dst); //Blur to create distance error map
}

bool TrackingModelTP2::GetObservation(float timeval) {
  /* spin-wait for images to be loaded */
  //while(mImageBuffer.size() == 0) {}
  int frame = (int) timeval;

  /* Get images for this time frame */
  ImageSet images = mImageBuffer[frame];
  mFGMaps = mFGBuffer[frame];

  /* Create edge map */
  for(int i = 0; i < images.size(); i++) {
    CreateEdgeMap(images[i], mEdgeMaps[i]);
  }
  return true;
}

#if 0
void TrackingModelTP2::LoadImages(int frames) {
  mImageBuffer.resize(0);
  mFGBuffer.resize(0);
  int frame = 0;
  while (frame < frames && !mFailed) {
    int n = mCameras.GetCameraCount();
    vector<string> FGfiles(n), ImageFiles(n);
    /* Generate image filenames */
    for(int i = 0; i < n; i++) {
      FGfiles[i] = mPath + "FG" + str(i + 1) + DIR_SEPARATOR + "image" + str(frame, 4) + ".bmp";
      ImageFiles[i] = mPath + "CAM" + str(i + 1) + DIR_SEPARATOR + "image" + str(frame, 4) + ".bmp";
    }
    ImageSet images(n);
    BinaryImageSet FGImages(n);
    for(int i = 0; i < (int)FGfiles.size(); i++) {
      FlexImage8u im;
      if(!FlexLoadBMP(FGfiles[i].c_str(), im)) { //Load foreground maps and raw images
        cout << "Unable to load image: " << FGfiles[i].c_str() << endl;
        mFailed = true;
        return;
      }  
      FGImages[i].ConvertToBinary(im); //binarize foreground maps to 0 and 1
      if(!FlexLoadBMP(ImageFiles[i].c_str(), images[i])) {
        cout << "Unable to load image: " << ImageFiles[i].c_str() << endl;
        mFailed = true;
        return;
      }
    }
    mImageBuffer.push_back(images);
    mFGBuffer.push_back(FGImages);
    printf("loaded images of frame %d\n", frame);
  }
}
#endif

#endif

