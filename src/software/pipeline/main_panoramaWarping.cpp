/**
 * Input and geometry
*/
#include <aliceVision/sfmData/SfMData.hpp>
#include <aliceVision/sfmDataIO/sfmDataIO.hpp>

/**
 * Image stuff
 */
#include <aliceVision/image/all.hpp>
#include <aliceVision/mvsData/imageAlgo.hpp>

/*Logging stuff*/
#include <aliceVision/system/Logger.hpp>

/*Reading command line options*/
#include <boost/program_options.hpp>
#include <aliceVision/system/cmdline.hpp>
#include <aliceVision/system/main.hpp>

/*IO*/
#include <fstream>
#include <algorithm>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

// These constants define the current software version.
// They must be updated when the command line is changed.
#define ALICEVISION_SOFTWARE_VERSION_MAJOR 1
#define ALICEVISION_SOFTWARE_VERSION_MINOR 0

using namespace aliceVision;

namespace po = boost::program_options;
namespace bpt = boost::property_tree;


Eigen::VectorXf gaussian_kernel_vector(size_t kernel_length, float sigma) {
  
  Eigen::VectorXd x;
  x.setLinSpaced(kernel_length + 1, -sigma, +sigma);

  Eigen::VectorXd cdf(kernel_length + 1);
  for (int i = 0; i < kernel_length + 1; i++) {
    cdf(i) = 0.5 * (1 + std::erf(x(i)/sqrt(2.0)));
  }

  Eigen::VectorXd k1d(kernel_length);
  for (int i = 0; i < kernel_length; i++) {
    k1d(i) = cdf(i + 1) - cdf(i);
  }

  double sum = k1d.sum();
  k1d = k1d / sum;

  return k1d.cast<float>();
}

Eigen::MatrixXf gaussian_kernel(size_t kernel_length, float sigma) {
  
  Eigen::VectorXf k1d = gaussian_kernel_vector(kernel_length, sigma);
  Eigen::MatrixXf K = k1d * k1d.transpose();
  

  double sum = K.sum();
  K = K / sum;

  return K;
}



bool convolve(image::Image<float> & output, const image::Image<float> & input, const image::Image<unsigned char> & mask, const Eigen::MatrixXf & kernel) {

  if (output.size() != input.size()) {
    return false;
  }

  if (output.size() != mask.size()) {
    return false;
  }

  if (kernel.size() % 2 == 0) {
    return false;
  }

  if (kernel.rows() != kernel.cols()) {
    return false;
  }

  int radius = kernel.rows() / 2;

  Eigen::MatrixXf kernel_scaled = kernel;

  for (int i = 0; i < output.Height(); i++) {
    for (int j = 0; j < output.Width(); j++) {

      float sum = 0.0f;
      float sum_mask = 0.0f;

      if (!mask(i, j)) {
        output(i, j) = 0.0f; 
        continue;
      }

      for (int k = 0; k < kernel.rows(); k++) {
        float ni = i + k - radius;
        if (ni < 0 || ni >= output.Height()) {
          continue;
        }

        for (int l = 0; l < kernel.cols(); l++) {
          float nj = j + l - radius;
          if (nj < 0 || nj >= output.Width()) {
            continue;
          }

          if (!mask(ni, nj)) {
            continue;
          }

          float val = kernel(k, l) * input(ni, nj);
          sum += val;
          sum_mask += kernel(k, l);
        }
      } 

      output(i, j) = sum / sum_mask;
    }
  }

  return true;
}

bool convolve(image::Image<image::RGBfColor> & output, const image::Image<image::RGBfColor> & input, const image::Image<unsigned char> & mask, const Eigen::MatrixXf & kernel) {

  if (output.size() != input.size()) {
    return false;
  }

  if (output.size() != mask.size()) {
    return false;
  }

  if (kernel.size() % 2 == 0) {
    return false;
  }

  if (kernel.rows() != kernel.cols()) {
    return false;
  }

  int radius = kernel.rows() / 2;

  Eigen::MatrixXf kernel_scaled = kernel;

  for (int i = 0; i < output.Height(); i++) {
    for (int j = 0; j < output.Width(); j++) {

      image::RGBfColor sum(0.0f);
      float sum_mask = 0.0f;

      if (!mask(i, j)) {
        output(i, j) = sum; 
        continue;
      }

      for (int k = 0; k < kernel.rows(); k++) {
        float ni = i + k - radius;
        if (ni < 0 || ni >= output.Height()) {
          continue;
        }

        for (int l = 0; l < kernel.cols(); l++) {
          float nj = j + l - radius;
          if (nj < 0 || nj >= output.Width()) {
            continue;
          }

          if (!mask(ni, nj)) {
            continue;
          }

          sum.r() += kernel(k, l) * input(ni, nj).r();
          sum.g() += kernel(k, l) * input(ni, nj).g();
          sum.b() += kernel(k, l) * input(ni, nj).b();
          sum_mask += kernel(k, l);
        }
      } 

      output(i, j) = sum / sum_mask;
    }
  }

  return true;
}

bool computeDistanceMap(image::Image<int> & distance, const image::Image<unsigned char> & mask) {

  int m = mask.Height();
  int n = mask.Width();

  int maxval = m * n;

  distance = image::Image<int> (n, m, false); 
  for(int x = 0; x < n; ++x) {

    //A corner is when mask becomes 0
    bool b = !mask(0, x);
    if (b) {
      distance(0, x) = 0;
    }
    else {
      distance(0, x) = maxval * maxval;
    }

    for (int y = 1; y < m; y++) {
      bool b = !mask(y, x);
      if (b) {
        distance(y, x) = 0;
      }
      else {          
        distance(y, x) = 1 + distance(y - 1, x);
      }
    }

    for (int y = m - 2; y >= 0; y--) {
      if (distance(y + 1, x) < distance(y, x)) {
        distance(y, x) = 1 + distance(y + 1, x);
      }
    }
  }

  for (int y = 0; y < m; y++) {  
    int q;
    std::map<int, int> s;
    std::map<int, int> t;

    q = 0;
    s[0] = 0;
    t[0] = 0;

    std::function<int (int, int)> f = [distance, y](int x, int i) { 
      int gi = distance(y, i);
      return (x - i)*(x - i) + gi * gi; 
    };

    std::function<int (int, int)> sep = [distance, y](int i, int u) { 
      int gu = distance(y, u);
      int gi = distance(y, i);

      int nom = (u * u) - (i * i) + (gu * gu) - (gi * gi);
      int denom = 2 * (u - i);

      return nom / denom;
    };

    for (int u = 1; u < n; u++) {

      while (q >= 0 && (f(t[q], s[q]) > f(t[q], u))) {
        q = q - 1;
      }

      if (q < 0) {
        q = 0;
        s[0] = u;
      }
      else {
        int w = 1 + sep(s[q], u);
        if (w  < n) {
          q = q + 1;
          s[q] = u;
          t[q] = w;
        }
      }
    }

    for (int u = n - 1; u >= 0; u--) {
      distance(y, u) = f(u, s[q]);
      if (u == t[q]) {
        q = q - 1;
      }
    }
  }

  return true;
}


namespace SphericalMapping
{
  /**
   * Map from equirectangular to spherical coordinates
   * @param equirectangular equirectangular coordinates
   * @param width number of pixels used to represent longitude
   * @param height number of pixels used to represent latitude
   * @return spherical coordinates
   */
  Vec3 fromEquirectangular(const Vec2 & equirectangular, int width, int height)
  {
    const double latitude = (equirectangular(1) / double(height)) * M_PI  - M_PI_2;
    const double longitude = ((equirectangular(0) / double(width)) * 2.0 * M_PI) - M_PI;

    const double Px = cos(latitude) * sin(longitude);
    const double Py = sin(latitude);
    const double Pz = cos(latitude) * cos(longitude);

    return Vec3(Px, Py, Pz);
  }

  /**
   * Map from Spherical to equirectangular coordinates
   * @param spherical spherical coordinates
   * @param width number of pixels used to represent longitude
   * @param height number of pixels used to represent latitude
   * @return equirectangular coordinates
   */
  Vec2 toEquirectangular(const Vec3 & spherical, int width, int height) {

    double vertical_angle = asin(spherical(1));
    double horizontal_angle = atan2(spherical(0), spherical(2));

    double latitude =  ((vertical_angle + M_PI_2) / M_PI) * height;
    double longitude =  ((horizontal_angle + M_PI) / (2.0 * M_PI)) * width;

    return Vec2(longitude, latitude);
  }

  /**
   * Map from Spherical to equirectangular coordinates in radians
   * @param spherical spherical coordinates
   * @return equirectangular coordinates
   */
  Vec2 toLongitudeLatitude(const Vec3 & spherical) {
    
    double latitude = asin(spherical(1));
    double longitude = atan2(spherical(0), spherical(2));

    return Vec2(longitude, latitude);
  }
}

class GaussianPyramidNoMask {
public:
  GaussianPyramidNoMask(const size_t width_base, const size_t height_base, const size_t limit_scales = 64) :
    _width_base(width_base), 
    _height_base(height_base)
  {
    /**
     * Compute optimal scale
     * The smallest level will be at least of size min_size
     */
    size_t min_dim = std::min(_width_base, _height_base);
    size_t min_size = 32;
    _scales = std::min(limit_scales, static_cast<size_t>(floor(log2(double(min_dim) / float(min_size)))));
    

    /**
     * Create pyramid
     **/
    size_t new_width = _width_base;
    size_t new_height = _height_base;
    for (int i = 0; i < _scales; i++) {

      _pyramid_color.push_back(image::Image<image::RGBfColor>(new_width, new_height, true, image::RGBfColor(0)));
      _filter_buffer.push_back(image::Image<image::RGBfColor>(new_width, new_height, true, image::RGBfColor(0)));
      new_height /= 2;
      new_width /= 2;
    }
  }

  bool process(const image::Image<image::RGBfColor> & input) {

    if (input.Height() != _pyramid_color[0].Height()) return false;
    if (input.Width() != _pyramid_color[0].Width()) return false;


    /**
     * Kernel
     */
    oiio::ImageBuf K;
    oiio::ImageBufAlgo::make_kernel(K, "gaussian", 5, 5);

    /** 
     * Build pyramid
    */
    _pyramid_color[0] = input;
    for (int lvl = 0; lvl < _scales - 1; lvl++) {
      
      const image::Image<image::RGBfColor> & source = _pyramid_color[lvl];
      image::Image<image::RGBfColor> & dst = _filter_buffer[lvl];

      oiio::ImageSpec spec(source.Width(), source.Height(), 3, oiio::TypeDesc::FLOAT);

      const oiio::ImageBuf inBuf(spec, const_cast<image::RGBfColor*>(source.data()));
      oiio::ImageBuf outBuf(spec, dst.data());    
      oiio::ImageBufAlgo::convolve(outBuf, inBuf, K);

      downscale(_pyramid_color[lvl + 1], _filter_buffer[lvl]);      
    }

    return true;
  }


  bool downscale(image::Image<image::RGBfColor> & output, const image::Image<image::RGBfColor> & input) {

    for (int i = 0; i < output.Height(); i++) {
      int ui = i * 2;

      for (int j = 0; j < output.Width(); j++) {
        int uj = j * 2;

        output(i, j) = input(ui, uj);
      }
    }

    return true;
  }

  const size_t getScalesCount() const {
    return _scales;
  }

  const std::vector<image::Image<image::RGBfColor>> & getPyramidColor() const {
    return _pyramid_color;
  }

  std::vector<image::Image<image::RGBfColor>> & getPyramidColor() {
    return _pyramid_color;
  }

protected:
  std::vector<image::Image<image::RGBfColor>> _pyramid_color;
  std::vector<image::Image<image::RGBfColor>> _filter_buffer;
  size_t _width_base;
  size_t _height_base;
  size_t _scales;
};

class CoordinatesMap {
private:
  struct BBox {
    int left;
    int top;
    int width;
    int height;
  };

public:
  /**
   * Build coordinates map given camera properties
   * @param panoramaSize desired output panoramaSize 
   * @param pose the camera pose wrt an arbitrary reference frame
   * @param intrinsics the camera intrinsics
   */
  bool build(const std::pair<int, int> & panoramaSize, const geometry::Pose3 & pose, const aliceVision::camera::IntrinsicBase & intrinsics) {

    BBox coarse_bbox;
    if (!computeCoarseBB(coarse_bbox, panoramaSize, pose, intrinsics)) {
      return false;
    }
    

    /* Effectively compute the warping map */
    aliceVision::image::Image<Eigen::Vector2d> buffer_coordinates(coarse_bbox.width, coarse_bbox.height, false);
    aliceVision::image::Image<unsigned char> buffer_mask(coarse_bbox.width, coarse_bbox.height, true, 0);

    size_t max_x = 0;
    size_t max_y = 0;
    size_t min_x = panoramaSize.first;
    size_t min_y = panoramaSize.second;

#ifdef _MSC_VER
    // TODO
    // no support for reduction min in MSVC implementation of openmp
#else
    #pragma omp parallel for reduction(min: min_x, min_y) reduction(max: max_x, max_y)
#endif
    for (size_t y = 0; y < coarse_bbox.height; y++) {

      size_t cy = y + coarse_bbox.top;

      size_t row_max_x = 0;
      size_t row_max_y = 0;
      size_t row_min_x = panoramaSize.first;
      size_t row_min_y = panoramaSize.second;


      for (size_t x = 0; x < coarse_bbox.width; x++) {

        size_t cx = x + coarse_bbox.left;

        Vec3 ray = SphericalMapping::fromEquirectangular(Vec2(cx, cy), panoramaSize.first, panoramaSize.second);

        /**
        * Check that this ray should be visible.
        * This test is camera type dependent
        */
        Vec3 transformedRay = pose(ray);
        if (!intrinsics.isVisibleRay(transformedRay)) {
          continue;
        }

        /**
         * Project this ray to camera pixel coordinates
         */
        const Vec2 pix_disto = intrinsics.project(pose, ray, true);

        /**
         * Ignore invalid coordinates
         */
        if (!intrinsics.isVisible(pix_disto)) {
          continue;
        }


        buffer_coordinates(y, x) = pix_disto;
        buffer_mask(y, x) = 1;
  
        row_min_x = std::min(x, row_min_x);
        row_min_y = std::min(y, row_min_y);
        row_max_x = std::max(x, row_max_x);
        row_max_y = std::max(y, row_max_y);
      }

      min_x = std::min(row_min_x, min_x);
      min_y = std::min(row_min_y, min_y);
      max_x = std::max(row_max_x, max_x);
      max_y = std::max(row_max_y, max_y);
    }
   
    _offset_x = coarse_bbox.left + min_x;
    if (_offset_x > panoramaSize.first) {
      /*The coarse bounding box may cross the borders where as the true coordinates may not*/
      int ox = int(_offset_x) - int(panoramaSize.first);
      _offset_x = ox;
    }
    _offset_y = coarse_bbox.top + min_y;
    
    size_t real_width = max_x - min_x + 1;
    size_t real_height = max_y - min_y + 1;

      /* Resize buffers */
    _coordinates = aliceVision::image::Image<Eigen::Vector2d>(real_width, real_height, false);
    _mask = aliceVision::image::Image<unsigned char>(real_width, real_height, true, 0);

    _coordinates.block(0, 0, real_height, real_width) =  buffer_coordinates.block(min_y, min_x, real_height, real_width);
    _mask.block(0, 0, real_height, real_width) =  buffer_mask.block(min_y, min_x, real_height, real_width);

    return true;
  }

  bool computeScale(double & result) {
    
    std::vector<double> scales;
    size_t real_height = _coordinates.Height();
    size_t real_width = _coordinates.Width();

    for (int i = 0; i < real_height - 1; i++) {
      for (int j = 0; j < real_width - 1; j++) {
        if (!_mask(i, j) || !_mask(i, j + 1) || !_mask(i + 1, j)) {
          continue;
        }

        double dxx = _coordinates(i, j + 1).x() - _coordinates(i, j).x();
        double dxy = _coordinates(i + 1, j).x() - _coordinates(i, j).x();
        double dyx = _coordinates(i, j + 1).y() - _coordinates(i, j).y();
        double dyy = _coordinates(i + 1, j).y() - _coordinates(i, j).y();

        double det = std::abs(dxx*dyy - dxy*dyx);
        scales.push_back(det);
      }
    }

    if (scales.size() <= 1) return false;

    std::nth_element(scales.begin(), scales.begin() + scales.size() / 2, scales.end());
    result = sqrt(scales[scales.size() / 2]);
    

    return true;
  }

  size_t getOffsetX() const {
    return _offset_x;
  }

  size_t getOffsetY() const {
    return _offset_y;
  }

  const aliceVision::image::Image<Eigen::Vector2d> & getCoordinates() const {
    return _coordinates;
  }

  const aliceVision::image::Image<unsigned char> & getMask() const {
    return _mask;
  }

private:

  bool computeCoarseBB(BBox & coarse_bbox, const std::pair<int, int> & panoramaSize, const geometry::Pose3 & pose, const aliceVision::camera::IntrinsicBase & intrinsics) {

    coarse_bbox.left = 0;
    coarse_bbox.top = 0;
    coarse_bbox.width = panoramaSize.first;
    coarse_bbox.height = panoramaSize.second;

    int bbox_left, bbox_top;
    int bbox_right, bbox_bottom;
    int bbox_width, bbox_height;

    /*Estimate distorted maximal distance from optical center*/
    Vec2 pts[] = {{0.0f, 0.0f}, {intrinsics.w(), 0.0f}, {intrinsics.w(), intrinsics.h()}, {0.0f, intrinsics.h()}};
    float max_radius = 0.0;
    for (int i = 0; i < 4; i++) {

      Vec2 ptmeter = intrinsics.ima2cam(pts[i]);
      float radius = ptmeter.norm();
      max_radius = std::max(max_radius, radius);
    }

    /* Estimate undistorted maximal distance from optical center */
    float max_radius_distorted = intrinsics.getMaximalDistortion(0.0, max_radius);

    /* 
    Coarse rectangle bouding box in camera space 
    We add intermediate points to ensure arclength between 2 points is never more than 180°
    */
    Vec2 pts_radius[] = {
        {-max_radius_distorted, -max_radius_distorted}, 
        {0, -max_radius_distorted},
        {max_radius_distorted, -max_radius_distorted}, 
        {max_radius_distorted, 0},
        {max_radius_distorted, max_radius_distorted},
        {0, max_radius_distorted},
        {-max_radius_distorted, max_radius_distorted},
        {-max_radius_distorted, 0}
      };


    /* 
    Transform bounding box into the panorama frame.
    Point are on a unit sphere.
    */
    Vec3 rotated_pts[8];
    for (int i = 0; i < 8; i++) {
      Vec3 pt3d = pts_radius[i].homogeneous().normalized();
      rotated_pts[i] = pose.rotation().transpose() * pt3d;
    }

    /* Vertical Default solution : no pole*/
    bbox_top = panoramaSize.second;
    bbox_bottom = 0;

    for (int i = 0; i < 8; i++) {
      int i2 = (i + 1) % 8;
      
      Vec3 extremaY = getExtremaY(rotated_pts[i], rotated_pts[i2]);

      Vec2 res;
      res = SphericalMapping::toEquirectangular(extremaY, panoramaSize.first, panoramaSize.second);
      bbox_top = std::min(int(floor(res(1))), bbox_top);
      bbox_bottom = std::max(int(ceil(res(1))), bbox_bottom);

      res = SphericalMapping::toEquirectangular(rotated_pts[i], panoramaSize.first, panoramaSize.second);
      bbox_top = std::min(int(floor(res(1))), bbox_top);
      bbox_bottom = std::max(int(ceil(res(1))), bbox_bottom);
    }

    /* 
    Check if our region circumscribe a pole of the sphere :
    Check that the region projected on the Y=0 plane contains the point (0, 0)
    This is a special projection case
    */
    bool pole = isPoleInTriangle(rotated_pts[0], rotated_pts[1], rotated_pts[7]);
    pole |= isPoleInTriangle(rotated_pts[1], rotated_pts[2], rotated_pts[3]);
    pole |= isPoleInTriangle(rotated_pts[3], rotated_pts[4], rotated_pts[5]);
    pole |= isPoleInTriangle(rotated_pts[7], rotated_pts[5], rotated_pts[6]);
    pole |= isPoleInTriangle(rotated_pts[1], rotated_pts[3], rotated_pts[5]);
    pole |= isPoleInTriangle(rotated_pts[1], rotated_pts[5], rotated_pts[7]);
    
    
    if (pole) {
      Vec3 normal = (rotated_pts[1] - rotated_pts[0]).cross(rotated_pts[3] - rotated_pts[0]);
      if (normal(1) > 0) {
        //Lower pole
        bbox_bottom = panoramaSize.second - 1;
      }
      else {
        //upper pole
        bbox_top = 0;
      }
    }

    bbox_height = bbox_bottom - bbox_top + 1;


    /*Check if we cross the horizontal loop*/
    bool crossH = false;
    for (int i = 0; i < 8; i++) {
      int i2 = (i + 1) % 8;

      bool cross = crossHorizontalLoop(rotated_pts[i], rotated_pts[i2]);
      crossH |= cross;
    }

    if (pole) {
      /*Easy : if we cross the pole, the width is full*/
      bbox_left = 0;
      bbox_right = panoramaSize.first - 1;
      bbox_width = bbox_right - bbox_left + 1;
    }
    else if (crossH) {

      int first_cross = 0;
      for (int i = 0; i < 8; i++) {
        int i2 = (i + 1) % 8;
        bool cross = crossHorizontalLoop(rotated_pts[i], rotated_pts[i2]);
        if (cross) {
          first_cross = i;
          break;
        }
      }

      bbox_left = panoramaSize.first - 1;
      bbox_right = 0;
      bool is_right = true;
      for (int index = 0; index < 8; index++) {

        int i = (index + first_cross) % 8;
        int i2 = (i + 1) % 8;

        Vec2 res_1 = SphericalMapping::toEquirectangular(rotated_pts[i], panoramaSize.first, panoramaSize.second);
        Vec2 res_2 = SphericalMapping::toEquirectangular(rotated_pts[i2], panoramaSize.first, panoramaSize.second);

        /*[----right ////  left-----]*/
        bool cross = crossHorizontalLoop(rotated_pts[i], rotated_pts[i2]);
        if (cross) {
          if (res_1(0) > res_2(0)) { /*[----res2 //// res1----]*/
            bbox_left = std::min(int(res_1(0)), bbox_left);
            bbox_right = std::max(int(res_2(0)), bbox_right);
            is_right = true;
          }
          else { /*[----res1 //// res2----]*/
            bbox_left = std::min(int(res_2(0)), bbox_left);
            bbox_right = std::max(int(res_1(0)), bbox_right);
            is_right = false;
          }
        }
        else {
          if (is_right) {
            bbox_right = std::max(int(res_1(0)), bbox_right);
            bbox_right = std::max(int(res_2(0)), bbox_right);
          }
          else {
            bbox_left = std::min(int(res_1(0)), bbox_left);
            bbox_left = std::min(int(res_2(0)), bbox_left);
          }
        }
      }

      bbox_width = bbox_right + (panoramaSize.first - bbox_left);
    }
    else {
      /*horizontal default solution : no border crossing, no pole*/
      bbox_left = panoramaSize.first;
      bbox_right = 0;
      for (int i = 0; i < 8; i++) {
        Vec2 res = SphericalMapping::toEquirectangular(rotated_pts[i], panoramaSize.first, panoramaSize.second);
        bbox_left = std::min(int(floor(res(0))), bbox_left);
        bbox_right = std::max(int(ceil(res(0))), bbox_right);
      }
      bbox_width = bbox_right - bbox_left + 1;
    }

    /*Assign solution to result*/
    coarse_bbox.left = bbox_left;
    coarse_bbox.top = bbox_top;
    coarse_bbox.width = bbox_width;
    coarse_bbox.height = bbox_height;
    
    return true;
  }

  Vec3 getExtremaY(const Vec3 & pt1, const Vec3 & pt2) {
    Vec3 delta = pt2 - pt1;
    double dx = delta(0);
    double dy = delta(1);
    double dz = delta(2);
    double sx = pt1(0);
    double sy = pt1(1);
    double sz = pt1(2);

    double ot_y = -(dx*sx*sy - (dy*sx)*(dy*sx) - (dy*sz)*(dy*sz) + dz*sy*sz)/(dx*dx*sy - dx*dy*sx - dy*dz*sz + dz*dz*sy);

    Vec3 pt_extrema = pt1 + ot_y * delta;

    return pt_extrema.normalized();
  }

  bool crossHorizontalLoop(const Vec3 & pt1, const Vec3 & pt2) {
    Vec3 direction = pt2 - pt1;

    /*Vertical line*/
    if (std::abs(direction(0)) < 1e-12) {
      return false;
    }

    double t = - pt1(0) / direction(0); 
    Vec3 cross = pt1 + direction * t;

    if (t >= 0.0 && t <= 1.0) {
      if (cross(2) < 0.0) {
        return true;
      } 
    }

    return false;
  }

  bool isPoleInTriangle(const Vec3 & pt1, const Vec3 & pt2, const Vec3 & pt3) {
   
    double a = (pt2.x()*pt3.z() - pt3.x()*pt2.z())/(pt1.x()*pt2.z() - pt1.x()*pt3.z() - pt2.x()*pt1.z() + pt2.x()*pt3.z() + pt3.x()*pt1.z() - pt3.x()*pt2.z());
    double b = (-pt1.x()*pt3.z() + pt3.x()*pt1.z())/(pt1.x()*pt2.z() - pt1.x()*pt3.z() - pt2.x()*pt1.z() + pt2.x()*pt3.z() + pt3.x()*pt1.z() - pt3.x()*pt2.z());
    double c = 1.0 - a - b;

    if (a < 0.0 || a > 1.0) return false;
    if (b < 0.0 || b > 1.0) return false;
    if (c < 0.0 || c > 1.0) return false;
 
    return true;
  }

private:
  size_t _offset_x = 0;
  size_t _offset_y = 0;

  aliceVision::image::Image<Eigen::Vector2d> _coordinates;
  aliceVision::image::Image<unsigned char> _mask;
};

class AlphaBuilder {
public:
  virtual bool build(const CoordinatesMap & map, const aliceVision::camera::IntrinsicBase & intrinsics) {
    
    float w = static_cast<float>(intrinsics.w());
    float h = static_cast<float>(intrinsics.h());
    float cx = w / 2.0f;
    float cy = h / 2.0f;
    

    const aliceVision::image::Image<Eigen::Vector2d> & coordinates = map.getCoordinates();
    const aliceVision::image::Image<unsigned char> & mask = map.getMask();

    _weights = aliceVision::image::Image<float>(coordinates.Width(), coordinates.Height());

    for (int i = 0; i < _weights.Height(); i++) {
      for (int j = 0; j < _weights.Width(); j++) {
        
        _weights(i, j) = 0.0f;

        bool valid = mask(i, j);
        if (!valid) {
          continue;
        }

        const Vec2 & coords = coordinates(i, j);

        float x = coords(0);
        float y = coords(1);

        float wx = 1.0f - std::abs((x - cx) / cx);
        float wy = 1.0f - std::abs((y - cy) / cy);
        
        _weights(i, j) = wx * wy;
      }
    }

    return true;
  }

  const aliceVision::image::Image<float> & getWeights() const {
    return _weights;
  }

private:
  aliceVision::image::Image<float> _weights;
};

class Warper {
public:
  virtual bool warp(const CoordinatesMap & map, const aliceVision::image::Image<image::RGBfColor> & source) {

    /**
     * Copy additional info from map
     */
    _offset_x = map.getOffsetX();
    _offset_y = map.getOffsetY();
    _mask = map.getMask();

    const image::Sampler2d<image::SamplerLinear> sampler;
    const aliceVision::image::Image<Eigen::Vector2d> & coordinates = map.getCoordinates();

    /**
     * Create buffer
     * No longer need to keep a 2**x size
     */
    _color = aliceVision::image::Image<image::RGBfColor>(coordinates.Width(), coordinates.Height());
    
    /**
     * Simple warp
     */
    for (size_t i = 0; i < _color.Height(); i++) {
      for (size_t j = 0; j < _color.Width(); j++) {

        bool valid = _mask(i, j);
        if (!valid) {
          continue;
        }

        const Eigen::Vector2d & coord = coordinates(i, j);
        const image::RGBfColor pixel = sampler(source, coord(1), coord(0));

        _color(i, j) = pixel;
      }
    }

    return true;
  }

  const aliceVision::image::Image<image::RGBfColor> & getColor() const {
    return _color;
  }

  const aliceVision::image::Image<unsigned char> & getMask() const {
    return _mask;
  }
  

  size_t getOffsetX() const {
    return _offset_x;
  }

  size_t getOffsetY() const {
    return _offset_y;
  }

protected:
  size_t _offset_x = 0;
  size_t _offset_y = 0;
  
  aliceVision::image::Image<image::RGBfColor> _color;
  aliceVision::image::Image<unsigned char> _mask;
};

class GaussianWarper : public Warper {
public:
  virtual bool warp(const CoordinatesMap & map, const aliceVision::image::Image<image::RGBfColor> & source) {

    /**
     * Copy additional info from map
     */
    _offset_x = map.getOffsetX();
    _offset_y = map.getOffsetY();
    _mask = map.getMask();

    const image::Sampler2d<image::SamplerLinear> sampler;
    const aliceVision::image::Image<Eigen::Vector2d> & coordinates = map.getCoordinates();

    /**
     * Create a pyramid for input
     */
    GaussianPyramidNoMask pyramid(source.Width(), source.Height());
    pyramid.process(source);
    const std::vector<image::Image<image::RGBfColor>> & mlsource = pyramid.getPyramidColor();
    size_t max_level = pyramid.getScalesCount() - 1; 

    /**
     * Create buffer
     */
    _color = aliceVision::image::Image<image::RGBfColor>(coordinates.Width(), coordinates.Height(), true, image::RGBfColor(1.0, 0.0, 0.0));
    

    /**
     * Multi level warp
     */
    for (size_t i = 0; i < _color.Height(); i++) {
      for (size_t j = 0; j < _color.Width(); j++) {

        bool valid = _mask(i, j);
        if (!valid) {
          continue;
        }

        if (i == _color.Height() - 1 || j == _color.Width() - 1 || !_mask(i + 1, j) || !_mask(i, j + 1)) {
          const Eigen::Vector2d & coord = coordinates(i, j);
          const image::RGBfColor pixel = sampler(source, coord(1), coord(0));
          _color(i, j) = pixel;
          continue;
        }

        const Eigen::Vector2d & coord_mm = coordinates(i, j);
        const Eigen::Vector2d & coord_mp = coordinates(i, j + 1);
        const Eigen::Vector2d & coord_pm = coordinates(i + 1, j);
        
        double dxx = coord_pm(0) - coord_mm(0);
        double dxy = coord_mp(0) - coord_mm(0);
        double dyx = coord_pm(1) - coord_mm(1);
        double dyy = coord_mp(1) - coord_mm(1);
        double det = std::abs(dxx*dyy - dxy*dyx);
        double scale = sqrt(det);

        double flevel = std::max(0.0, log2(scale));
        size_t blevel = std::min(max_level, size_t(floor(flevel)));        

        double dscale, x, y;
        dscale = 1.0 / pow(2.0, blevel);
        x = coord_mm(0) * dscale;
        y = coord_mm(1) * dscale;
        /*Fallback to first level if outside*/
        if (x >= mlsource[blevel].Width() - 1 || y >= mlsource[blevel].Height() - 1) {
          _color(i, j) = sampler(mlsource[0], coord_mm(1), coord_mm(0));
          continue;
        }

        _color(i, j) = sampler(mlsource[blevel], y, x);
      }
    }

    return true;
  }
};

bool computeOptimalPanoramaSize(std::pair<int, int> & optimalSize, const sfmData::SfMData & sfmData) {

  optimalSize.first = 512;
  optimalSize.second = 256;

   /**
   * Loop over views to estimate best scale
   */
  std::vector<double> scales;
  for (auto & viewIt: sfmData.getViews()) {
  
    /**
     * Retrieve view
     */
    const sfmData::View& view = *viewIt.second.get();
    if (!sfmData.isPoseAndIntrinsicDefined(&view)) {
      continue;
    }

    /**
     * Get intrinsics and extrinsics
     */
    const geometry::Pose3  camPose = sfmData.getPose(view).getTransform();
    const camera::IntrinsicBase & intrinsic = *sfmData.getIntrinsicPtr(view.getIntrinsicId());

    /**
     * Compute map
     */
    CoordinatesMap map;
    if (!map.build(optimalSize, camPose, intrinsic)) {
      continue;
    }

    double scale;
    if (!map.computeScale(scale)) {
      continue;
    }

    scales.push_back(scale);
  }

  
  if (scales.size() > 1) {
    double median_scale;
    std::nth_element(scales.begin(), scales.begin() + scales.size() / 2, scales.end());
    median_scale = scales[scales.size() / 2];

    double multiplier = pow(2.0, int(floor(log2(median_scale))));
    
    optimalSize.first = optimalSize.first * multiplier;
    optimalSize.second = optimalSize.second * multiplier;
  }

  return true;
}

int aliceVision_main(int argc, char **argv) {

  /**
   * Program description
  */
  po::options_description allParams (
    "Perform panorama stiching of cameras around a nodal point for 360° panorama creation. \n"
    "AliceVision PanoramaWarping"
  );

  /**
   * Description of mandatory parameters
   */
  std::string sfmDataFilename;
  std::string outputDirectory;
  po::options_description requiredParams("Required parameters");
  requiredParams.add_options()
    ("input,i", po::value<std::string>(&sfmDataFilename)->required(), "SfMData file.")
    ("output,o", po::value<std::string>(&outputDirectory)->required(), "Path of the output folder.");
  allParams.add(requiredParams);

  /**
   * Description of optional parameters
   */
  std::pair<int, int> panoramaSize = {1024, 0};
  po::options_description optionalParams("Optional parameters");
  optionalParams.add_options()
    ("panoramaWidth,w", po::value<int>(&panoramaSize.first)->default_value(panoramaSize.first), "Panorama Width in pixels.");
  allParams.add(optionalParams);

  /**
   * Setup log level given command line
   */
  std::string verboseLevel = system::EVerboseLevel_enumToString(system::Logger::getDefaultVerboseLevel());
  po::options_description logParams("Log parameters");
  logParams.add_options()
    ("verboseLevel,v", po::value<std::string>(&verboseLevel)->default_value(verboseLevel), "verbosity level (fatal, error, warning, info, debug, trace).");
  allParams.add(logParams);


  /**
   * Effectively parse command line given parse options
   */
  po::variables_map vm;
  try
  {
    po::store(po::parse_command_line(argc, argv, allParams), vm);

    if(vm.count("help") || (argc == 1))
    {
      ALICEVISION_COUT(allParams);
      return EXIT_SUCCESS;
    }
    po::notify(vm);
  }
  catch(boost::program_options::required_option& e)
  {
    ALICEVISION_CERR("ERROR: " << e.what());
    ALICEVISION_COUT("Usage:\n\n" << allParams);
    return EXIT_FAILURE;
  }
  catch(boost::program_options::error& e)
  {
    ALICEVISION_CERR("ERROR: " << e.what());
    ALICEVISION_COUT("Usage:\n\n" << allParams);
    return EXIT_FAILURE;
  }

  ALICEVISION_COUT("Program called with the following parameters:");
  ALICEVISION_COUT(vm);


  /**
   * Set verbose level given command line
   */
  system::Logger::get()->setLogLevel(verboseLevel);

  /**
   * Load information about inputs
   * Camera images
   * Camera intrinsics
   * Camera extrinsics
   */
  sfmData::SfMData sfmData;
  if(!sfmDataIO::Load(sfmData, sfmDataFilename, sfmDataIO::ESfMData(sfmDataIO::VIEWS| sfmDataIO::INTRINSICS| sfmDataIO::EXTRINSICS)))
  {
    ALICEVISION_LOG_ERROR("The input SfMData file '" << sfmDataFilename << "' cannot be read.");
    return EXIT_FAILURE;
  }


  /*Order views by their image names for easier debugging*/
  std::vector<std::shared_ptr<sfmData::View>> viewsOrderedByName;
  for (auto & viewIt: sfmData.getViews()) {
    viewsOrderedByName.push_back(viewIt.second);
  }
  std::sort(viewsOrderedByName.begin(), viewsOrderedByName.end(), [](const std::shared_ptr<sfmData::View> & a, const std::shared_ptr<sfmData::View> & b) -> bool { 
    if (a == nullptr || b == nullptr) return true;
    return (a->getImagePath() < b->getImagePath());
  });


  /*If panorama width is undefined, estimate it*/
  if (panoramaSize.first <= 0) {
    std::pair<int, int> optimalPanoramaSize;
    if (computeOptimalPanoramaSize(optimalPanoramaSize, sfmData)) {
      panoramaSize = optimalPanoramaSize;
    }
  }
  else {
    double max_scale = 1.0 / pow(2.0, 10);
    panoramaSize.first = int(ceil(double(panoramaSize.first) * max_scale) / max_scale);
    panoramaSize.second = panoramaSize.first / 2;
  }



  ALICEVISION_LOG_INFO("Choosen panorama size : "  << panoramaSize.first << "x" << panoramaSize.second);

  bpt::ptree viewsTree;

  /**
   * Preprocessing per view
   */
  size_t pos = 0;
  for (const std::shared_ptr<sfmData::View> & viewIt: viewsOrderedByName) {
    
    /**
     * Retrieve view
     */
    const sfmData::View& view = *viewIt;
    if (!sfmData.isPoseAndIntrinsicDefined(&view)) {
      continue;
    }

    ALICEVISION_LOG_INFO("Processing view " << view.getViewId());

    /**
     * Get intrinsics and extrinsics
     */
    const geometry::Pose3 camPose = sfmData.getPose(view).getTransform();
    const camera::IntrinsicBase & intrinsic = *sfmData.getIntrinsicPtr(view.getIntrinsicId());

    /**
     * Prepare coordinates map
    */
    CoordinatesMap map;
    map.build(panoramaSize, camPose, intrinsic);

    /**
     * Load image and convert it to linear colorspace
     */
    std::string imagePath = view.getImagePath();
    ALICEVISION_LOG_INFO("Load image with path " << imagePath);
    image::Image<image::RGBfColor> source;
    image::readImage(imagePath, source, image::EImageColorSpace::LINEAR);

    /**
     * Warp image
     */
    GaussianWarper warper;
    warper.warp(map, source);

    /**
    * Alpha mask
    */
    AlphaBuilder alphabuilder;
    alphabuilder.build(map, intrinsic);


    /**
     * Combine mask and image
     */
    const aliceVision::image::Image<image::RGBfColor> & cam = warper.getColor();
    const aliceVision::image::Image<unsigned char> & mask = warper.getMask();
    const aliceVision::image::Image<float> & weights = alphabuilder.getWeights();

    /**
     * Store result image
     */
    bpt::ptree viewTree;
    std::string path;

    {
    std::stringstream ss;
    ss << outputDirectory << "/view_" << pos << ".exr";
    path = ss.str();
    viewTree.put("filename_view", path);
    ALICEVISION_LOG_INFO("Store view " << pos << " with path " << path);
    image::writeImage(path, cam, image::EImageColorSpace::AUTO);
    }

    {
    std::stringstream ss;
    ss << outputDirectory << "/mask_" << pos << ".png";
    path = ss.str();
    viewTree.put("filename_mask", path);
    ALICEVISION_LOG_INFO("Store mask " << pos << " with path " << path);
    image::writeImage(path, mask, image::EImageColorSpace::NO_CONVERSION);
    }

    {
    std::stringstream ss;
    ss << outputDirectory << "/weightmap_" << pos << ".exr";
    path = ss.str();
    viewTree.put("filename_weights", path);
    ALICEVISION_LOG_INFO("Store weightmap " << pos << " with path " << path);
    image::writeImage(path, weights, image::EImageColorSpace::AUTO);
    }
  
    /**
    * Store view info
    */
    viewTree.put("offsetx", warper.getOffsetX());
    viewTree.put("offsety", warper.getOffsetY());
    viewsTree.push_back(std::make_pair("", viewTree));

    pos++;
  }

  
  /**
   * Config output
   */
  bpt::ptree configTree;
  configTree.put("panoramaWidth", panoramaSize.first);
  configTree.put("panoramaHeight", panoramaSize.second);
  configTree.add_child("views", viewsTree);

  std::stringstream ss;
  ss << outputDirectory << "/config_views.json";
  ALICEVISION_LOG_INFO("Save config with path " << ss.str());
  bpt::write_json(ss.str(), configTree, std::locale(), true);

  return EXIT_SUCCESS;
}