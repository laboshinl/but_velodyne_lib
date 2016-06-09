/*
 * Copyright (C) Brno University of Technology (BUT)
 *
 * This file is part of software developed by Robo@FIT group.
 *
 * Author: Martin Velas (ivelas@fit.vutbr.cz)
 * Supervised by: Michal Spanel & Adam Herout ({spanel|herout}@fit.vutbr.cz)
 * Date: 19/09/2014
 *
 * This file is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <but_velodyne/VelodynePointCloud.h>

#include <vector>
#include <cstdlib>
#include <cassert>

#include <cv.h>
#include <pcl/common/eigen.h>
#include <pcl/common/transforms.h>
#include <pcl/filters/filter.h>

#include <velodyne_pointcloud/point_types.h>

using namespace pcl;
using namespace velodyne_pointcloud;
using namespace std;
using namespace cv;

namespace but_velodyne {

PointXYZIR operator +(const PointXYZIR &p1, const PointXYZIR &p2) {
  PointXYZIR res;
  res.x = p1.x + p2.x;
  res.y = p1.y + p2.y;
  res.z = p1.z + p2.z;
  res.intensity = p1.intensity + p2.intensity;
  return res;
}

PointXYZIR operator *(const PointXYZIR &p1, float s) {
  PointXYZIR res;
  res.x = p1.x * s;
  res.y = p1.y * s;
  res.z = p1.z * s;
  res.intensity = p1.intensity * s;
  return res;
}

PointXYZIR operator *(float s, const PointXYZIR &p1) {
  return p1*s;
}

PointXYZIR operator -(const PointXYZIR &p1, const PointXYZIR &p2) {
  return p1 + (p2 * (-1));
}

PointXYZIR operator /(const PointXYZIR &p1, float s) {
  return p1 * (1/s);
}

void VelodynePointCloud::normalizeIntensity(float min_intensity, float max_intensity)
{
  float min = 0.0;
  float max = 1.0;
  for (PointCloud<PointXYZIR>::iterator pt = this->begin(); pt < this->end(); pt++)
  {
    pt->intensity = (pt->intensity - min_intensity) /
                    (max_intensity - min_intensity) * (max - min) + min;
  }
}

VelodynePointCloud VelodynePointCloud::discartWeakPoints(float threshold) {
  VelodynePointCloud output;
  for(PointCloud<PointXYZIR>::const_iterator pt = this->begin();
      pt < this->end();
      pt++) {
    if(pt->intensity > threshold) {
      output.push_back(*pt);
    }
  }
  return output;
}

VelodynePointCloud VelodynePointCloud::resampleTo(int final_number) {
  VelodynePointCloud resampled;
  unsigned counter = 0;
  unsigned factor = this->size()/final_number + 1;
  for(VelodynePointCloud::iterator pt = this->begin(); pt < this->end(); pt++, counter++) {
    if(counter%factor == 0) {
      resampled.push_back(*pt);
    }
  }
  cerr << resampled.size() << endl;
  assert(resampled.size() == final_number);
  return resampled;
}

VelodynePointCloud VelodynePointCloud::resampleByRatio(float preserve_ratio) {
  return resampleTo(this->size()*preserve_ratio);
}

VelodynePointCloud VelodynePointCloud::computeEdges(float threshold) const
{
  vector< vector<PointXYZIR> > rings;
  vector< vector<int> > indices;
  getRings(rings, indices);
  VelodynePointCloud edge_cloud;

  float max_difference = 0;
  float min_difference = INFINITY;
  for (vector<vector<PointXYZIR> >::iterator ring = rings.begin(); ring < rings.end(); ring++)
  {
    if(ring->size() < 2) {
      continue;
    }
    float previous_range, current_range, next_range;
    current_range = computeRange(ring->front());
    next_range = computeRange(*(ring->begin() + 1));
    for (vector<PointXYZIR>::iterator pt = ring->begin() + 1; pt + 1 < ring->end(); pt++)
    {
      previous_range = current_range;
      current_range = next_range;
      next_range = computeRange(*(pt + 1));
      PointXYZIR edge_pt;
      edge_pt.x = pt->x;
      edge_pt.y = pt->y;
      edge_pt.z = pt->z;
      edge_pt.ring = pt->ring;
      edge_pt.intensity = MAX(MAX( previous_range-current_range, next_range-current_range), 0) * 10;
      min_difference = MIN(edge_pt.intensity, min_difference);
      max_difference = MAX(edge_pt.intensity, max_difference);
      edge_cloud.push_back(edge_pt);
    }
  }
  edge_cloud.normalizeIntensity(min_difference, max_difference);
  edge_cloud = edge_cloud.discartWeakPoints(threshold);
  return edge_cloud;
}

void VelodynePointCloud::getRings(vector< vector<PointXYZIR> > &rings,
				  vector< vector<int> > &indices) const
{
  rings.clear();
  rings.resize(VELODYNE_RINGS_COUNT);
  indices.clear();
  indices.resize(VELODYNE_RINGS_COUNT);
  int id = 0;
  for (PointCloud<PointXYZIR>::const_iterator pt = this->begin();
      pt < this->end();
      pt++, id++)
  {
    assert(pt->ring < VELODYNE_RINGS_COUNT);
    rings[pt->ring].push_back(*pt);
    indices[pt->ring].push_back(id);
  }
}

PointXYZIR VelodynePointCloud::getMinValuePt() const {
  PointXYZIR min;
  min.x = min.y = min.z = min.ring = 0;
  min.intensity = INFINITY;
  for(VelodynePointCloud::const_iterator pt = this->begin(); pt < this->end(); pt++) {
    if(pt->intensity < min.intensity) {
      min = *pt;
    }
  }
  return min;
}

PointXYZIR VelodynePointCloud::getMaxValuePt() const {
  PointXYZIR max;
  max.x = max.y = max.z = max.ring = 0;
  max.intensity = -INFINITY;
  for(VelodynePointCloud::const_iterator pt = this->begin(); pt < this->end(); pt++) {
    if(pt->intensity > max.intensity) {
      max = *pt;
    }
  }
  return max;
}

PointCloud<PointXYZ>::Ptr VelodynePointCloud::getXYZCloudPtr() const {
  PointCloud<PointXYZ>::Ptr cloud_ptr(new PointCloud<PointXYZ>());
  for(VelodynePointCloud::const_iterator pt = begin(); pt < end(); pt++) {
    cloud_ptr->push_back(PointXYZIRtoPointXYZ(*pt));
  }
  return cloud_ptr;
}

float VelodynePointCloud::getMedianRange() const {
  if(this->size() == 0) {
    return NAN;
  }
  vector<float> ranges;
  for(VelodynePointCloud::const_iterator pt = begin(); pt < end(); pt++) {
    ranges.push_back(computeRange(*pt));
  }
  sort(ranges.begin(), ranges.end());
  return ranges[ranges.size()/2];
}

void VelodynePointCloud::setImageLikeAxisFromKitti() {
  Eigen::Matrix4f transformation;
    transformation <<
        0, -1,  0,  0,
        0,  0, -1,  0,
        1,  0,  0,  0,
        0,  0,  0,  1;
  transformPointCloud(*this, *this, transformation);
}

void VelodynePointCloud::setImageLikeAxisFromBut() {
  Eigen::Affine3f transformation = getTransformation(0, 0, 0, M_PI / 2, 0, 0);
  transformPointCloud(*this, *this, transformation);
}

//================// PointXYZIR //================//

PointXYZ PointXYZIRtoPointXYZ(const PointXYZIR &in)
{
  PointXYZ out;
  out.x = in.x;
  out.y = in.y;
  out.z = in.z;
  return out;
}

/**
 *  Returns false (invalid result) if the point is behind the camera or
 *  it would be projected outside the projection plane.
 */
bool projectPoint(const PointXYZIR &pt,
                  const cv::Mat &projection_matrix,
                  const Rect &plane,
                  Point2f &projected_pt)
{
  if (pt.z < 0)
  {
    return false;
  }

  cv::Mat pt_3D(4, 1, CV_32FC1);

  pt_3D.at<float>(0) = pt.x;
  pt_3D.at<float>(1) = pt.y;
  pt_3D.at<float>(2) = pt.z;
  pt_3D.at<float>(3) = 1.0f; // is homogenious coords. the point's 4. coord is 1

  cv::Mat pt_2D = projection_matrix * pt_3D;

  float w = pt_2D.at<float>(2);
  projected_pt.x = pt_2D.at<float>(0) / w;
  projected_pt.y = pt_2D.at<float>(1) / w;

  return projected_pt.inside(plane);
}

vector<float> VelodynePointCloud::getMaxOfRingRanges() const {
  vector< vector<float> > ranges(VelodyneSpecification::RINGS);
  for(const_iterator pt = begin(); pt < end(); pt++) {
    ranges[pt->ring].push_back(pt->x*pt->x + pt->z*pt->z);
  }
  vector<float> almost_maximums;
  for(int ring = 0; ring < ranges.size(); ring++) {
    if(ranges[ring].empty()) {
      almost_maximums.push_back(NAN);
    } else {
      sort(ranges[ring].begin(), ranges[ring].end());
      almost_maximums.push_back(sqrt(ranges[ring][ranges[ring].size()*0.9]));
    }
  }
  return almost_maximums;
}

float VelodynePointCloud::averageIntensity() const {
  float avg = 0.0;
  int count = 0;
  for(const_iterator p = begin(); p < end(); p++) {
    float intensity = p->intensity;
    if(!isnan(intensity) && !isinf(intensity)) {
      avg += intensity;
      count++;
    }
  }
  return (count > 0) ? (avg/count) : 0;
}

std::vector<int> VelodynePointCloud::removeNanPoints() {
  vector<int> filtered_indices;
  PointCloud<PointXYZ> dummy_filtered_out;
  removeNaNFromPointCloud(*this->getXYZCloudPtr(), dummy_filtered_out, filtered_indices);
  for(int i = 0; i < filtered_indices.size(); i++) {
    this->at(i) = this->at(filtered_indices[i]);
  }
  this->resize(filtered_indices.size());
  return filtered_indices;
}

}

