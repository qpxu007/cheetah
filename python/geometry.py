import os, re, fabio, string, math
import numpy as np


class geometry():
    common_block = \
"""photon_energy = {energy} ; in eV
adu_per_photon = 1.0
clen = {distance}  ; in m
coffset = 0.
res = {resolution}  ; in pixels

data = {data_label} ; /entry_1/data_1/data
dim0 = %
dim1 = ss
dim2 = fs

0/min_fs = 0
0/max_fs = {nx}
0/min_ss = 0
0/max_ss = {ny}
0/corner_x = -{xbeam} ; 
0/corner_y = -{ybeam} ;
0/fs = x
0/ss = y
"""

    # generate a mask from an example image of pilatus or eiger cbf
    def __init__(self,
                 imagefile,
                 geom_file='current_geometry.geom',
                 h5geom_file='current_geometry.h5',
                 badpixelmap='current_badpixel_map.h5'
                 ):
        self.image_file = imagefile
        self.geometry_file = geom_file
        self.geometry_h5 = h5geom_file
        self.badpixelmap = badpixelmap

    def generate(self):
        img = self.get_image()
        data = img['data']
        beamx, beamy = img['beam']

        common_block = geometry.generate_common_blocks(img).split("\n")
        vertial_gaps = geometry.inter_module_gaps(data, axis=0)
        horizontal_gaps = geometry.inter_module_gaps(data, axis=1)
        beamstop_shadow = geometry.get_beamstop_shadow(beamx, beamy, radius=100)

        v_blocks = geometry.generate_bad_regions(vertial_gaps, mark='v')
        h_blocks = geometry.generate_bad_regions(horizontal_gaps, mark='h')
        b_blocks = geometry.generate_beamstop(beamstop_shadow)

        all_blocks = ["\n".join(block) for block in (common_block, b_blocks, v_blocks, h_blocks)]

        with open(self.geometry_file, "w") as fh:
            fh.write("\n\n".join(all_blocks))
        print("geometry file is written as: ", self.geometry_file, " in ", os.getcwd())

        make_pixelmap = '/mnt/software/px/serial-x/crystfel-0.8.0/bin/make_pixelmap'
        cmd1 = "{} -o {} {}".format(make_pixelmap, self.geometry_h5, self.geometry_file)
        cmd2 = "{} --badmap -o {} {}".format(make_pixelmap, self.badpixelmap, self.geometry_file)
        print("generate h5 geometry file: ", self.geometry_h5)
        os.system(cmd1)
        print("generate h5 bad pixel map file: ", self.badpixelmap)
        os.system(cmd2)

    def get_image(self):
        if not os.path.exists(self.image_file):
            printf("image file does not exist: ", image_file)
            raise Exeception("image file does not exist: ", image_file)

        fh = fabio.open(image_file)
        image = geometry.get_header_items(fh)
        image['data'] = fh.data
        return image

    @staticmethod
    def inter_module_gaps(data, axis=1):
        indexes = np.where((data == -1).all(axis=axis))[0]  # axis 0: column, axis 1: rows, -1 marks gaps
        # print(data.shape)

        d2max = data.shape[axis]

        gaps = geometry.get_limits(indexes)
        n_gap = len(gaps) // 2

        out = []
        # each tuple contains (xmin, ymin, xmax, ymax)
        for i in range(n_gap):
            if axis == 0:
                out.append(tuple([gaps[2 * i], 0, gaps[2 * i + 1], d2max - 1]))
            else:
                out.append(tuple([0, gaps[2 * i], d2max - 1, gaps[2 * i + 1]]))

        return out

    @staticmethod
    def get_beamstop_shadow(xbeam, ybeam, radius=100):
        # radius of beamstop in pixels
        xbeam = float(xbeam)
        ybeam = float(ybeam)
        return tuple([0, int(xbeam - radius), int(xbeam + radius), int(ybeam + radius)])

    @staticmethod
    def generate_common_blocks(image):
        if 'wavelength' in image:
            energy = round(12398.42 / float(image['wavelength']), 2)

        res = round(1.0 / float(image['pixel_size']), 1)  # how many pixels per meter

        return geometry.common_block.format(energy=energy,
                                            nx=int(image['size1']) - 1,
                                            ny=int(image['size2']) - 1,
                                            distance=image['distance'],
                                            xbeam=image['beam'][0],
                                            ybeam=image['beam'][1],
                                            resolution=res,
                                            data_label="/entry_1/data_1/data"
                                            )

    @staticmethod
    def generate_beamstop(beamstop_shadow):
        ''' e.g.
            bad_beamstop/min_x = -2000
            bad_beamstop/min_y = -40
            bad_beamstop/max_x = 100
            bad_beamstop/max_y = 100
        '''
        blocks = list()
        blocks.append("bad_beamstop/min_fs = {}".format(beamstop_shadow[0]))
        blocks.append("bad_beamstop/min_ss = {}".format(beamstop_shadow[1]))
        blocks.append("bad_beamstop/max_fs = {}".format(beamstop_shadow[2]))
        blocks.append("bad_beamstop/max_ss = {}".format(beamstop_shadow[3]))
        return blocks

    @staticmethod
    def generate_bad_regions(panels, mark='v'):
        labels = [x for x in string.digits + string.ascii_letters][::-1]
        direction = ['min_fs', 'min_ss', 'max_fs', 'max_ss']
        labels.pop()  # skip 0

        blocks = []
        for panel in panels:
            label = labels.pop()
            block = ["bad_{}{}/{} = {}".format(mark, label, dir, p) for dir, p in zip(direction, panel)]
            blocks.append("\n".join(block))

        return blocks

    @staticmethod
    def get_limits(arr):
        # remove elements if the numbers are adjacent to each other [1,3,4,5,6] -->[1,3,6]
        if arr.size == 0:
            return []

        out = [arr[0]]
        for i, elem in enumerate(arr[1:], 1):
            if elem - arr[i - 1] != 1:
                out.append(arr[i - 1])
                out.append(elem)

        out.append(arr[-1])

        if len(out) % 2 != 0:
            print("something is wrong, not all boundary found.")

        return out

    @staticmethod
    def get_header_items(fh):
        arr = fh.header['_array_data.header_contents'].split('\r\n')

        header = {}
        for a in arr:
            fields = re.split(r"\s+", a, 2)
            # print(fields)

            if "detector:" in fields[1].lower():
                header['detector'] = fields[-1]

            if "pixel_size" in fields[1].lower():
                header['pixel_size'] = fields[2].split()[0]

            if "distance" in fields[1].lower():
                header['distance'] = fields[2].split()[0]

            if "beam_xy" in fields[1].lower():
                header['beam'] = re.findall(r"\d+\.\d+|\d+", fields[2])

            if "wavelength" in fields[1].lower():
                header['wavelength'] = fields[2].split()[0]

        header['size1'] = fh.dim1
        header['size2'] = fh.dim2

        return header


if __name__ == '__main__':
    # image_file = sys.argv[1]
    image_file = "/mnt/beegfs/qxu/23IDD-test/4z2x-p21212/run0001/196802_1_00056.cbf"
    geometry(image_file).generate()


