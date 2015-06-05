import cv2
import pandas as pd
import numpy as np
from sklearn.neighbors import KNeighborsClassifier
from os import path
import os
import shutil
from kobra.tr_utils import append_to_arr
from kobra.imaging import show_images, pyr_blurr
from numbapro import vectorize

class KNeighborsClassify (object):
    def __init__(self, root, annotation_images_dir, annotations, masks_dir):
        '''
        root - root directory for images
        annotations_images_dir - where annotations images are
        masks_dir - where masks are
        annotations - path to the annotations file
        '''
        self._annotation_images_dir = annotation_images_dir
        self._masks_dir = masks_dir
        self._root = root
        self._annotations = annotations
        self._pd_annotations = pd.read_csv(self._annotations, sep= ' ', header = None)

        # files from which annotations were extracted
        self._files = self._pd_annotations[0].as_matrix()
        self._n_files = len(self._files)

        # number of rectangles in each file
        self._n_objects = self._pd_annotations[1][0]

        rect_frame = self._pd_annotations.ix[:, 2:].as_matrix()
        rows = rect_frame.shape[0]
        rect_frame = np.vsplit(rect_frame, rows)
        self._rects = np.array([])
        self._avg_pixels = None
        
        # convert number-by-number columns into rectangles
        for row in rect_frame:
            row = row.reshape(-1)
            rects = np.array([], dtype=('i4, i4, i4, i4'))
            for i in range(0, self._n_objects):
                idx = i * 4
                # images are loaded by OpenCV with rows and columns mapped to y and x coordinates
                # rectangles are stored by annoations tool as x, y, dx, dy -> col, row, dcol, drow
                rect = row[idx + 1], row[idx], row[idx + 1] + row[idx + 3], row[idx] + row[idx + 2]
                if rects.size == 0:
                    rects = np.array(rect, dtype=('i4, i4, i4, i4'))
                else:
                    rects = np.append(rects, np.array(rect, dtype=('i4, i4, i4, i4')))
            
            self._rects = append_to_arr(self._rects, rects)                            

    def _get_initial_classes(self):
        '''
        Averages of the pixels values of all images:
        Annotations contain:
        0 - 1: drusen/exudates
        2 - 3: background
            4: vessels
            5: haemorages   
        '''
        images = map(lambda f: cv2.imread(path.join(self._root, f)), self._files)
        self._avg_pixels = np.array([], dtype=np.uint8)

        # extract parts from each image for all of our 6 categories
        for i in range(0, self._n_objects):
            rects = self._rects[:, i]
            rows = np.max(rects['f2'] - rects['f0'])
            cols = np.max(rects['f3'] - rects['f1'])

            im_rects = map(lambda (im, r): im[r[0]:r[2],r[1]:r[3],:], zip(images, rects))
            im_rects = np.array(map(lambda im: cv2.resize(im, (cols, rows)), im_rects), dtype=np.float)
            avgs = np.around(np.average(im_rects, axis = 0))
            mn = np.around(np.array(cv2.mean(avgs), dtype='float'))[:-1].astype('uint8')

            if(self._avg_pixels.size == 0):
                self._avg_pixels = mn
            else:
                self._avg_pixels = np.vstack((self._avg_pixels, mn))
        
        self._labels = np.append(self._labels, -1)

        # append the background pixel
        self._avg_pixels = np.vstack((self._avg_pixels, np.array([0, 0, 0])))
            

    @property
    def labels(self):
        return self._labels

    @labels.setter
    def labels(self, val):
        self._labels = val

    @staticmethod
    def flip(rect):
        '''
        useful for converting between (pt1, pt2) and row x col representations of rectangles
        '''
        return rect[1], rect[0], rect[3], rect[2]
    
    def display_average_pixels(self):
        if self._avg_pixels == None:
            self._get_initial_classes()

        def stretch_image(i):
            pixel = self._avg_pixels[i]
            stretch = np.zeros((20, 20, 3), dtype='uint8')
            stretch[:, :] = pixel
            return stretch

        images = [stretch_image(i) for i in range(0, self._n_objects)]

        show_images(images)

    
    def analyze_image(self, im_file):
        '''
        Load the image and analyze it with FCM
        '''

        if self._avg_pixels == None:
            self._get_initial_classes()

        im_file_name = path.splitext(path.split(im_file)[1])[0]

        mask_file = path.join(self._masks_dir, im_file_name + ".png")

        mask = cv2.imread(mask_file, cv2.IMREAD_GRAYSCALE)
        #mask = pyr_blurr(mask)

        mask [ mask != 0 ] = 255
        rows = mask.shape[0]

        im = cv2.imread(im_file)
        #im = pyr_blurr(im)

        assert (im.shape[0] == rows), "Rows don't match between mask and image"

        im [ mask == 0, :] = 0

        clf = KNeighborsClassifier(n_neighbors = 3)
        clf.fit(self._avg_pixels, self._labels)

        im_1d = im.reshape(-1, 3)

        prediction = clf.predict(im_1d)
        prediction = prediction.reshape(rows, -1)

        # display what we have found
        im_drusen = im.copy()
        im_bg = im.copy()

        im_drusen [prediction == 0] = [255, 0, 0]
        im_bg [prediction == 1] = [0, 255, 0]

        show_images([im, im_drusen, im_bg], ["original", "disk", "background"], scale = 0.8)


