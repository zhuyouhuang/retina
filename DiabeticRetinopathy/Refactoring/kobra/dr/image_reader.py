import numpy as np
import pandas as pd
from os import path
import cv2

class ImageReader(object):
    '''
    Reads images and their masks
    '''

    def __init__(self, root, im_file, masks_dir, gray_scale = False):
        self._im_file = path.join(root, im_file)
        self._masks_dir = masks_dir

        assert (path.exists(self._im_file)), "Image does not exist"
        assert (path.exists(self._masks_dir)), "Masks dir does not exist"

        self._image = cv2.imread(self._im_file, cv2.IMREAD_GRAYSCALE if gray_scale else cv2.IMREAD_COLOR)
        self._mask = self.get_mask()
        self._image [ self._mask == 0] = 0

    def get_mask(self):
        mask_file = path.join(self._masks_dir, path.splitext(path.split(self._im_file)[1])[0] + ".png")
        assert (path.exists(mask_file)), "Mask does not exist"    

        self._mask = cv2.imread(mask_file, cv2.IMREAD_GRAYSCALE)
        return ImageReader.rescale_mask(self._image, self._mask)

    @staticmethod
    def rescale_mask(image, mask):
        scale = image.shape[1], image.shape[0]
        mask = cv2.resize(mask, scale)
        return mask

    @property
    def mask(self):
        return self._mask

    @property 
    def image(self):
        return self._image