#! /usr/bin/env python

import numpy as np
import sys
import math
import random
from numpy import dtype
import h5py
import cv
from __builtin__ import min
from eulerangles import mat2eulerZYX

def load_from_yaml(yaml_filename, node_name):
    return np.asarray(cv.Load(yaml_filename, cv.CreateMemStorage(), node_name))

class Odometry:
    def __init__(self, kitti_pose = [1, 0, 0, 0, 
                                     0, 1, 0, 0, 
                                     0, 0, 1, 0]):
        assert len(kitti_pose) == 12
        self.dof = [0]*6
        self.M = np.matrix([[0]*4, [0]*4, [0]*4, [0, 0, 0, 1]], dtype=np.float64)
        for i in range(12):
            self.M[i/4, i%4] = kitti_pose[i]
        self.setDofFromM()
    
    def setDofFromM(self):
        R = self.M[:3, :3]
        self.dof[0], self.dof[1], self.dof[2] = self.M[0, 3], self.M[1, 3], self.M[2, 3]
        self.dof[5], self.dof[4], self.dof[3] = mat2eulerZYX(R)
  
    def distanceTo(self, other):
        sq_dist = 0
        for i in range(3):
            sq_dist += (self.dof[i]-other.dof[i])**2
        return math.sqrt(sq_dist)

    def __mul__(self, other):
        out = Odometry()
        out.M = self.M * other.M
        out.setDofFromM()
        return out
    
    def __sub__(self, other):
        out = Odometry()
        out.M = np.linalg.inv(other.M) * self.M
        out.setDofFromM()
        return out

def gen_preserve_mask(poses, skip_prob):
    mask = [1]
    prev_pose = poses[0]
    current_pose = poses[1]
    for next_pose in poses[2:]:
        distance = next_pose.distanceTo(prev_pose)
        rndnum = random.random()
        if (distance < MAX_SPEED*0.1) and (rndnum < skip_prob):
            mask.append(0)
        else:
            mask.append(1)
            prev_pose = current_pose
        current_pose = next_pose
    mask.append(1)
    return mask

def mask_list(list, mask):
    if len(list) != len(mask):
        sys.stderr.write("Number of poses (%s) and velodyne frames (%s) differ!\n"%(len(mask), len(list)))
    output = []
    for i in range(min(len(mask), len(list))):
        if mask[i] != 0:
            output.append(list[i])
    return output

def get_delta_odometry(odometries, mask):
    if len(odometries) != len(mask):
        sys.stderr.write("Number of poses (%s) and velodyne frames (%s) differ!\n"%(len(mask), len(odometries)))
    output = [Odometry()]
    last_i = 0
    for i in range(1, min(len(mask), len(odometries))):
        if mask[i] != 0:
            output.append(odometries[i] - odometries[last_i])
            last_i = i
    return output
            
class OutputFiles:
    def __init__(self, batch_size, history_size, frames_to_join, features, output_prefix, max_seq_len):
        self.batchSize = batch_size
        self.historySize = history_size
        self.framesToJoin = frames_to_join
        self.features = features
        self.outputPrefix = output_prefix
        self.maxFramesPerFile = max_seq_len
        self.outFileSeqIndex = -1
    
    def newSequence(self, frames_count, max_in_schema):
        self.framesToWriteCount = (frames_count - max_in_schema)
        self.outFileSeqIndex += 1
        self.out_files = []
        out_files_count = self.framesToWriteCount/self.maxFramesPerFile + 1 if self.framesToWriteCount%self.maxFramesPerFile > 0 else 0
        for split_index in range(out_files_count):
            if (split_index+1)*self.maxFramesPerFile <= self.framesToWriteCount:
                frames_in_file = self.maxFramesPerFile  
            else:
                frames_in_file = self.framesToWriteCount%self.maxFramesPerFile
            new_output_file = h5py.File(self.outputPrefix + "." + str(self.outFileSeqIndex) + "." + str(split_index) + ".hdf5", 'w')
            new_output_file.create_dataset('data', (frames_in_file*self.historySize, self.features*self.framesToJoin, 64, 360), dtype='f4')
            new_output_file.create_dataset('odometry', (frames_in_file, 6), dtype='f4')
            self.out_files.append(new_output_file)
    
    def putData(self, db_name, frame_i, ch_i, data):
        if db_name == 'odometry':
            multiply = 1 
        else:
            multiply = self.historySize
        if frame_i < self.framesToWriteCount*multiply:
            file_index = frame_i/(self.maxFramesPerFile*multiply)
            self.out_files[file_index][db_name][frame_i%(self.maxFramesPerFile*multiply), ch_i] = data
            #print file_index, db_name, frame_i%(self.maxFramesPerFile*multiply), ch_i
        else:
            sys.stderr.write("Warning: frame %s out of the scope\n"%frame_i)

    def close(self):
        for f in self.out_files:
            f.close()

def schema_to_dics(data_schema, odom_schema):
    odom_dic = {i:[] for i in set(odom_schema)}
    for frame_i in range(len(odom_schema)):
        odom_dic[odom_schema[frame_i]].append(frame_i)
    
    data_dic = {i:{"slot":[], "frame":[]} for i in set(reduce(lambda x,y: x+y,data_schema))}
    for frame_i in range(len(data_schema)):
        for slot_i in range(len(data_schema[frame_i])):
            data_dic[data_schema[frame_i][slot_i]]["slot"].append(slot_i)
            data_dic[data_schema[frame_i][slot_i]]["frame"].append(frame_i)

    return data_dic, odom_dic

BATCH_SCHEMA_DATA = [[3, 0],
                     [4, 1],
                     [5, 2],
                     [6, 3],
                     
                     [3, 1],
                     [4, 2],
                     [5, 3],
                     [6, 4],
                     
                     [3, 2],
                     [4, 3],
                     [5, 4],
                     [6, 5]]
BATCH_SCHEMA_ODOM = [3, 4, 5, 6]

BATCH_SIZE = len(BATCH_SCHEMA_ODOM)
JOINED_FRAMES = len(BATCH_SCHEMA_DATA[0])
HISTORY_SIZE = len(BATCH_SCHEMA_DATA)/BATCH_SIZE
FEATURES = 3
max_in_schema = max(reduce(lambda x,y: x+y,BATCH_SCHEMA_DATA))

MIN_SKIP_PROB = 0.0
MAX_SKIP_PROB = 0.01
STEP_SKIP_PROB = 0.9
MAX_SPEED = 60/3.6
FILES_PER_HDF5 = 200

if len(sys.argv) < 2+max_in_schema+1:
    sys.stderr.write("Expected arguments: <pose-file> <out-file-prefix> <frames.yaml>^{%s+}\n"%JOINED_FRAMES)
    sys.exit(1)

poses_6dof = []
for line in open(sys.argv[1]).readlines():
    kitti_pose = map(float, line.strip().split())
    o = Odometry(kitti_pose)
    poses_6dof.append(o)

random.seed()
skip_prob = MIN_SKIP_PROB
out_files = OutputFiles(BATCH_SIZE, HISTORY_SIZE, JOINED_FRAMES, FEATURES, sys.argv[2], FILES_PER_HDF5)
data_dest_index, odom_dest_index = schema_to_dics(BATCH_SCHEMA_DATA, BATCH_SCHEMA_ODOM)
while skip_prob < MAX_SKIP_PROB:
    mask = gen_preserve_mask(poses_6dof, skip_prob)
    # TODO - maybe also duplication = no movement

    frames = sum(mask)-JOINED_FRAMES+1
    out_files.newSequence(frames, max_in_schema)
    files_to_use = mask_list(sys.argv[3:], mask)
    odometry_to_use = get_delta_odometry(poses_6dof, mask)

    for i in range(len(files_to_use)):
        data_i = np.empty([3, 64, 360])
        data_i[0] = load_from_yaml(files_to_use[i], 'range')
        data_i[1] = load_from_yaml(files_to_use[i], 'y')
        data_i[2] = load_from_yaml(files_to_use[i], 'intensity')
        odometry = np.asarray(odometry_to_use[i].dof)
        
        bias = 0
        while i-bias >= 0:
            #print "i", i, "bias", bias
            schema_i = i-bias
            # for feature data
            if schema_i in data_dest_index:
                slot_ids = data_dest_index[schema_i]["slot"]
                frame_ids = data_dest_index[schema_i]["frame"]
                for slot_i, frame_i in zip(slot_ids, frame_ids):
                    for fi in range(FEATURES):
                        out_files.putData('data', frame_i+bias*HISTORY_SIZE, slot_i*FEATURES+fi, data_i[fi])
            # for odometry data
            if schema_i in odom_dest_index:
                frame_ids = odom_dest_index[schema_i]
                for frame_i in frame_ids:
                    for ch_i in range(len(odometry_to_use[i].dof)):
                        out_files.putData('odometry', frame_i+bias, ch_i, odometry_to_use[i].dof[ch_i])
            
            bias += BATCH_SIZE
        
        if i%FILES_PER_HDF5 == 0 and i > 0:
            print i, "/", len(files_to_use)
    
    skip_prob += STEP_SKIP_PROB
    out_files.close()