// main.cpp
//
// Starter code for CS 148 Assignment 5.
//

//
// Include libst headers for using OpenGL and GLUT.
//
#include "st.h"
#include "stgl.h"
#include "stglut.h"
#include "STHDRImage.h"

#define WIN_WIDTH  512
#define WIN_HEIGHT 512

#include <stdlib.h>
#include <math.h>
#include "response.h"

enum MODE {
  MODE_RESPONSE,
  MODE_CREATE,
  MODE_VIEW,
  MODE_VP,
  MODE_TONEMAP
};

// delta used to make sure we don't do log(0)
#define LOG_DELTA .000001f

//
// Globals used by this application.
// As a rule, globals are Evil, but this is a small application
// and the design of GLUT makes it hard to avoid them.
//

// Current size of the OpenGL window
int gWindowSizeX;
int gWindowSizeY;

static STImage * image = NULL;

static CameraResponse cr;    // Camera response, either generated or loaded
static STHDRImage * hdr;     // The high dynamic range image

static int mode;             // The type of operation we're doing

static float shutter_time = .125f; // Shutter time for virtual photograph
static float key_lum      = .45f;  // The key value for tonemapping
static float hdr_view_max = 1.f;   // Value to map to max color

/* Recover an HDR image from a set of photos and the response curve for the camera used to
 * take the photos. To work efficiently you should only have one image open at a time.
 * For each pixel, you want to calculate the following:
 *
 * Ls(x,y) = sum_i( weight( pixel(x,y,i) ) * ( Exposure( pixel(x,y,i) ) - ln(dt_i) ) );
 * ws(x,y) = sum_i( weight( pixel(x,y,i) ) );
 * L_w(x,y) = exp( Ls(x,y) / ws(x,y) ).
 *
 * HINT: Look at the function definitions and the comments in response.h!
 */
STHDRImage* recover_hdr(vector<Photo>& photos, CameraResponse& response) {
    if (photos.size() == 0) throw "Must provide at least one photo to recover";
    
    Photo firstPhoto = photos[0];
    STImage *firstImage = new STImage(firstPhoto.filename);
    int n = photos.size(), width = firstImage->GetWidth(), height = firstImage->GetHeight();
    free(firstImage);
    STHDRImage *newImage = new STHDRImage(width, height);
    
    STColor3f *weightSum = (STColor3f*)calloc(width * height, sizeof(STColor3f));
    
    // Calculate weighted exposure sum
    for (int i = 0; i < n; i++) {
        Photo curPhoto = photos[i];
        STImage *image = new STImage(curPhoto.filename);
        float logShutter = log(curPhoto.shutter);
        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                STColor4ub pixel = image->GetPixel(x, y);
                STColor3f weightedSum = newImage->GetPixel(x, y);
                STColor3f weight = response.Weight(pixel);
                
                weightSum[y * width + x] += weight;
                weightedSum += weight * (response.GetExposure(pixel) - STColor3f(logShutter, logShutter, logShutter));
                
                newImage->SetPixel(x, y, weightedSum);
            }
        }
        free(image);
    }
    
    // Normalize all the values
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            STColor3f pixel = newImage->GetPixel(x, y);
            newImage->SetPixel(x, y, (pixel / weightSum[y * width + x]).Exp());
        }
    }
    
    free(weightSum);
    
    return newImage;
}

/* Scale an HDR image for viewing, this is just a linear map
 * of hdr such that 0 maps to 0 and max_val maps to 255.
 */
void scale_hdr(STHDRImage* hdr, float max_val, STImage* result) {
    int width = hdr->GetWidth(), height = hdr->GetHeight();
    
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            STColor3f pixel = hdr->GetPixel(x, y);
            result->SetPixel(x, y, STColor4ub(pixel / max_val));
        }
    }
}

/* Take a virtual photo - Use the camera response curve to take a virtual photo using
 * the given shutter time.  Use hdr as the source image and store the virtual photo
 * in result.  Remember that a photo is just the response of the camera to the incoming
 * radiance over a given shutter time.
 */
void virtual_photo(STHDRImage* hdr, CameraResponse& response, float shutter, STImage* result) {
    int width = hdr->GetWidth(), height = hdr->GetHeight();
    
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            STColor3f pixel = hdr->GetPixel(x, y);
            result->SetPixel(x, y, response.GetResponse(pixel, shutter));
        }
    }
}

/* Tonemap operator, tonemaps the hdr image and stores the
 * tonemapped image in result.  Use the value key as the 
 * key of the photograph.  This function should:
 * 1) Calculate the log-average luminance, Lavg
 * 2) Calculate the tonemapped image using the equation
 *    L(x,y) = L_w(x,y) * key / Lavg;
 *    C_tm(x,y) = L(x,y) / (1 + L(x,y));
 * 3) Store those tonemapped values to the result image,
 *    scaling appropriately.
 */
void tonemap(STHDRImage* hdr, float key, STImage* result) {
    // Calculate log average luminance
    float luminanceSum = 0.0;
    float delta = 0.000001;
    int width = hdr->GetWidth(), height = hdr->GetHeight();
    
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            STColor3f pixel = hdr->GetPixel(x, y);
            luminanceSum += logf(delta + pixel.Y());
        }
    }
    
    float logAverageLuminance = expf(luminanceSum / (width * height));
    
    // Scale, compress, and store pixels
    STColor3f white = STColor3f(1, 1, 1);
    
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            STColor3f pixel = hdr->GetPixel(x, y);
            STColor3f scaledPixel = pixel * (key / logAverageLuminance);
            
            result->SetPixel(x, y, STColor4ub(scaledPixel / (white + scaledPixel)));
        }
    }
}

//
// GLUT display callback
//
void display()
{
  glClearColor(.3f, .3f, .3f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  image->Draw();

  glutSwapBuffers();
}

//
// Reshape window
//
void reshape(int w, int h) {
  gWindowSizeX = w;
  gWindowSizeY = h;

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  gluOrtho2D(0., 1., 0., 1.);
  glViewport(0, 0, w, h);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glutPostRedisplay();
}

// Keyboard event handler:
static void keyboard(unsigned char key, int x, int y) {
  switch (key) {
    // quit
    case '+':
    case '=':
      if (mode == MODE_VIEW) {
        hdr_view_max /= 2.f;
        scale_hdr(hdr, hdr_view_max, image);
      }
      else if (mode == MODE_VP) {
        shutter_time *= powf(2.f, 1.f/3.f); // 1/3 stop
        virtual_photo(hdr, cr, shutter_time, image);
      }
      else if (mode == MODE_TONEMAP) {
        key_lum += .05f;
        tonemap(hdr, key_lum, image);
      }
      break;
    case '-':
    case '_':
      if (mode == MODE_VIEW) {
        hdr_view_max *= 2.f;
        scale_hdr(hdr, hdr_view_max, image);
      }
      else if (mode == MODE_VP) {
        shutter_time /= powf(2.f, 1.f/3.f); // 1/3 stop
        virtual_photo(hdr, cr, shutter_time, image);
      }
      else if (mode == MODE_TONEMAP) {
        key_lum -= .05f;
        tonemap(hdr, key_lum, image);
      }

      break;
    case 's':
    case 'S':
      if (mode == MODE_VIEW)
        image->Save("view.jpg");
      else if (mode == MODE_VP)
        image->Save("vp.jpg");
      else if (mode == MODE_TONEMAP)
        image->Save("tonemap.jpg");
      break;
    case char(27):
    case 'q':
    case 'Q':
      exit(0);
  }

  glutPostRedisplay();
}

static void usage() {
  printf("Usage:\n");
  printf("hdr -response photos.list response_out.cr\n");
  printf("hdr -create photos.list response.cr photo_out.pfm\n");
  printf("hdr -view photo.pfm\n");
  printf("hdr -vp photo.pfm response.cr\n");
  printf("hdr -tonemap photo.pfm\n");
  exit(-1);
}

int main(int argc, char** argv)
{
  //
  // Initialize GLUT.
  //
  glutInit(&argc, argv);
  glutInitDisplayMode( GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
  glutInitWindowPosition(20, 20);
  glutInitWindowSize(
      WIN_WIDTH, WIN_HEIGHT);
  glutCreateWindow("CS148 Assignment 5");


  if (argc < 2) {
    usage();
  }

  if ( strcmp(argv[1], "-response") == 0) {
    if (argc != 4)
      usage();
    mode = MODE_RESPONSE;

    // Defaults
    float lambda = 50.f;
    int nsamples = 100;

    vector<Photo> photos;
    LoadHDRStack(argv[2], photos);
    cr.SolveForResponse(photos, lambda, nsamples);

    cr.Save(argv[3]);
    exit(0);
  }
  else if ( strcmp(argv[1], "-create") == 0) {
    if (argc != 5)
      usage();
    mode = MODE_CREATE;
    vector<Photo> photos;
    LoadHDRStack(argv[2], photos);
    cr.Load(argv[3]);
    hdr = recover_hdr(photos, cr);
    hdr->Save(argv[4]);
    exit(0);
  }
  else {
    hdr = new STHDRImage(argv[2]);
    if(hdr)
      image = new STImage(hdr->GetWidth(), hdr->GetHeight());
    else
      usage();

    if ( strcmp(argv[1], "-view") == 0) {
      if (argc != 3)
        usage();
      mode = MODE_VIEW;
      scale_hdr(hdr, hdr_view_max, image);
    }
    else if ( strcmp(argv[1], "-vp") == 0) {
      if (argc != 4)
        usage();
      mode = MODE_VP;
      cr.Load(argv[3]);
      virtual_photo(hdr, cr, shutter_time, image);
    }
    else if ( strcmp(argv[1], "-tonemap") == 0) {
      if (argc != 3)
        usage();
      mode = MODE_TONEMAP;
      tonemap(hdr, key_lum, image);
    }
    else {
      usage();
    }
  }

  glutReshapeWindow(image->GetWidth(), image->GetHeight());

  glutDisplayFunc(display);
  glutKeyboardFunc(keyboard);
  glutReshapeFunc(reshape);
  glutMainLoop();

  if(image) {
    delete image;
  }

  return 0;
}
