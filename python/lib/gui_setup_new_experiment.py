#
#   Functions to configure Cheetah for various locations
#   Extracts template skeleton and modifies configuration files accordingly
#


import os
import glob
import PyQt5.QtCore
import PyQt5.QtWidgets

import lib.cfel_filetools as cfel_file
import lib.gui_dialogs as gui_dialogs
import lib.gui_locations as gui_locations



#
#   Extract appropriate template
#
def extract_template(self):

    realdir = os.getcwd()

    # LCLS
    if realdir.startswith("/reg/"):
        print("Extracting template for LCLS...")
        extract_lcls_template(self)
        return

    # EuXFEL
    if realdir.startswith("/gpfs/exfel"):
        print("Extracting template for EuXFEL...")
        extract_euxfel_template(self)
        return

    # GMCA/APS
    if realdir.startswith("/mnt/beegfs/") and "23ID" in realdir:
        print("Extracting template for GMCA...")
        extract_gmca_template(self)
        return

    print('Unable to determine which template to extract')
    print('extract_template in gui_setup_new_experiment.py')
    self.exit_gui()

    #end extract_template()



#
#   Extract the LCLS template and modify to suit determined experiment
#
def extract_lcls_template(self):
    #   Deduce experiment number, etc using de-referenced paths
    #   Assumes the file path follows the pattern:   /reg/d/psdm/cxi/cxij4915/scratch/...
    realdir = os.getcwd()

    #   Now for some LCLS-specific stuff
    ss = realdir.split('/')
    ss = ss[1:]

    ss[1] = 'd'
    ss[2] = 'psdm'
    instr = ss[3]
    expt = ss[4]
    xtcdir = '/' + str.join('/', ss[0:5]) + '/xtc'
    userdir = '/' + str.join('/', ss) + '/cheetah'

    print('Deduced experiment information:')
    print('    Relative path: ', dir)
    print('    Absolute path: ', realdir)
    print('    Instrument: ', instr)
    print('    Experiment: ', expt)
    print('    XTC directory: ', xtcdir)
    print('    Output directory: ', userdir)

    #
    # QMessageBox for confirmation before proceeding
    #
    msgBox = PyQt5.QtWidgets.QMessageBox()
    str1 = []
    str1.append('<b>Instrument:</b> ' + str(instr))
    str1.append('<b>Experiment:</b> ' + str(expt))
    str1.append('<b>XTC directory:</b> ' + str(xtcdir))
    str1.append('<b>Output directory:</b> ' + str(userdir))
    # str1.append('<b>Relative path:</b> '+ str(dir))
    str2 = str.join('<br>', str1)
    msgBox.setText(str2)

    msgBox.setInformativeText('<b>Proceed?</b>')
    msgBox.addButton(PyQt5.QtWidgets.QMessageBox.Yes)
    msgBox.addButton(PyQt5.QtWidgets.QMessageBox.Cancel)
    msgBox.setDefaultButton(PyQt5.QtWidgets.QMessageBox.Yes)

    ret = msgBox.exec_();
    # app.exit()


    if ret == PyQt5.QtWidgets.QMessageBox.Cancel:
        print("So long and thanks for all the fish.")
        self.exit_gui()

    #
    # Now the part which extracts and re-works the template
    #

    # Unpack template
    print('>---------------------<')
    print('Extracting template...')
    cmd = ['tar', '-xf', '/reg/g/cfel/cheetah/template.tar']
    cfel_file.spawn_subprocess(cmd, wait=True)
    print("Done")

    # Fix permissions
    print('>---------------------<')
    print('Fixing permissions...')
    cmd = ['chgrp', '-R', expt, 'cheetah/']
    cfel_file.spawn_subprocess(cmd, wait=True)
    cmd = ['chmod', '-R', 'g+w', 'cheetah/']
    cfel_file.spawn_subprocess(cmd, wait=True)
    print("Done")

    # Place configuration into /res
    # print, 'Placing configuration files into /res...'
    # cmd = ['/reg/g/cfel/cheetah/cheetah-stable/bin/make-labrynth']
    # cfel_file.spawn_subprocess(cmd, wait=True)
    # print("Done")


    # Modify gui/crawler.config
    print('>---------------------<')
    file = 'cheetah/gui/crawler.config'
    print('Modifying ', file)

    # Replace XTC directory with the correct location using sed
    xtcsedstr = str.replace(xtcdir, '/', '\\/')
    cmd = ["sed", "-i", "-r", "s/(xtcdir=).*/\\1" + xtcsedstr + "/", file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    print('>-------------------------<')
    cmd = ['cat', file]
    cfel_file.spawn_subprocess(cmd, wait=True)
    print('>-------------------------<')

    # Modify process/process
    file = 'cheetah/process/process'
    print('Modifying ', file)

    cmd = ["sed", "-i", "-r", "s/(expt=).*/\\1\"" + expt + "\"/", file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    xtcsedstr = str.replace(xtcdir, '/', '\\/')
    cmd = ["sed", "-i", "-r", "s/(XTCDIR=).*/\\1\"" + xtcsedstr + "\"/", file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    h5sedstr = str.replace(userdir, '/', '\\/') + '\/hdf5'
    cmd = ["sed", "-i", "-r", "s/(H5DIR=).*/\\1\"" + h5sedstr + "\"/", file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    confsedstr = str.replace(userdir, '/', '\\/') + '\/process'
    cmd = ["sed", "-i", "-r", "s/(CONFIGDIR=).*/\\1\"" + confsedstr + "\"/", file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    print('>-------------------------<')
    cmd = ['head', file]
    cfel_file.spawn_subprocess(cmd, wait=True)
    print('>-------------------------<')

    print("Working directory: ")
    print(os.getcwd() + '/cheetah')
    #end extract_lcls_template()


    #
    # Modify the configuration files
    #
    modify_cheetah_config_files(self)

    return
    #end extract_lcls_template()



#
#   Extract the EuXFEL template and modify to suit determined experiment
#
def extract_euxfel_template(self):
    #   Deduce experiment number, etc using de-referenced paths
    realdir = os.getcwd()
    print('os.getcwd() = ', realdir)

    #   Now for some EuXFEL-specific directory stuff
    # /gpfs/exfel/u/scratch/SPB/201701/p002017/anton/cheetah-test
    # to
    # /gpfs/exfel/exp/SPB/201701/p002017
    ss = realdir.split('/')
    ss = ss[1:]
    print(ss)

    instr = ss[4]
    run = ss[5]
    expt = ss[6]



    tempdir = str.join('/', ss[7:])
    ssnew = ss
    ssnew[2] = 'exp'
    ssnew[3:6] = ss[4:7]

    absroot = '/' + str.join('/', ssnew[0:6])
    absdir = absroot + '/scratch/' + tempdir

    datadir = absroot + '/raw'
    userdir = absdir + '/cheetah'

    print('Deduced experiment information:')
    print('    Sensed directory: ', realdir)
    print('    Preferred path: ', absdir)
    print('    Instrument: ', instr)
    print('    Experiment: ', expt)
    print('    Proposal round: ', run)
    print('    Data directory: ', datadir)
    print('    Output directory: ', userdir)


    #
    # QMessageBox for confirmation before proceeding
    #
    msgBox = PyQt5.QtWidgets.QMessageBox()
    str1 = []
    str1.append('<b>Instrument:</b> ' + str(instr))
    str1.append('<b>Experiment:</b> ' + str(expt))
    str1.append('<b>Data directory:</b> ' + str(datadir))
    str1.append('<b>Output directory:</b> ' + str(userdir))
    # str1.append('<b>Relative path:</b> '+ str(dir))
    str2 = str.join('<br>', str1)
    msgBox.setText(str2)

    msgBox.setInformativeText('<b>Proceed?</b>')
    msgBox.addButton(PyQt5.QtWidgets.QMessageBox.Yes)
    msgBox.addButton(PyQt5.QtWidgets.QMessageBox.Cancel)
    msgBox.setDefaultButton(PyQt5.QtWidgets.QMessageBox.Yes)

    ret = msgBox.exec_()

    if ret == PyQt5.QtWidgets.QMessageBox.Cancel:
        print("So long and thanks for all the fish.")
        self.exit_gui()

    #
    # Extract the template
    #
    # Unpack template
    print('>---------------------<')
    print('Extracting template...')
    template = '/gpfs/cfel/cxi/common/public/development/cheetah/template.tar'
    cmd = ['tar', '-xf', template]
    cfel_file.spawn_subprocess(cmd, wait=True)
    print("Done")

    # Fix permissions
    print('>---------------------<')
    print('Fixing permissions...')
    cmd = ['chmod', '-R', 'ug+w', 'cheetah/']
    cfel_file.spawn_subprocess(cmd, wait=True)
    cmd = ['chmod', 'ug+x', 'cheetah/process/process', 'cheetah/process/hitfinder']
    cfel_file.spawn_subprocess(cmd, wait=True)
    print("Done")


    #
    # Modify configuration files
    #

    # Modify gui/crawler.config
    print('>---------------------<')
    file = 'cheetah/gui/crawler.config'
    print('Modifying ', file)

    # Replace XTC directory with the correct location using sed
    xtcsedstr = str.replace(datadir, '/', '\\/')
    cmd = ["sed", "-i", "-r", "s/(xtcdir=).*/\\1" + xtcsedstr + "/", file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    print('>-------------------------<')
    cmd = ['cat', file]
    cfel_file.spawn_subprocess(cmd, wait=True)
    print('>-------------------------<')


    # Modify process/process
    file = 'cheetah/process/process'
    print('Modifying ', file)

    exptstr = instr+'/'+run+'/'+expt
    expsedstr = str.replace(exptstr, '/', '\\/')
    cmd = ["sed", "-i", "-r", "s/(expt=).*/\\1" + expsedstr + "/", file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    print('>-------------------------<')
    cmd = ['head', file]
    cfel_file.spawn_subprocess(cmd, wait=True)
    print('>-------------------------<')

    print("Working directory: ")
    print(os.getcwd() + '/cheetah')

    return
    #end extract_euxfel_template()


#
#   Modify a bunch of configuration files
#   Separate function so that it can be called from the menu as well as on startup
#
def modify_cheetah_config_files(self):

    # Dialog box
    result, ok = gui_dialogs.configure_cheetah_lcls_gui.configure_cheetah_dialog()

    if ok == False:
        print('Configuration scripts remain unchanged')
        return

    # This is needed because on startup we are one directory up from cheetah/gui, but when running we are in cheetah/gui
    dir = os.getcwd()
    if "cheetah/gui" in dir:
        prefix = '..'
    else:
        prefix = 'cheetah'


    # Modify detector type string
    print("Modifying detector type...")
    detectorType = result["detectorType"]
    for file in glob.iglob(prefix+'/process/*.ini'):
        cmd = ["sed", "-i", "-r", "/^detectorType/s/(detectorType=).*/\\1" + detectorType + "/", file]
        cfel_file.spawn_subprocess(cmd, wait=True)
        cmd = ["sed", "-i", "-r", "/^detectorName/s/(detectorName=).*/\\1" + detectorType + "/", file]
        cfel_file.spawn_subprocess(cmd, wait=True)

    # Modify detector Z encoder string
    print("Modifying detector Z encoder...")
    detectorZencoder = result["detectorZEncoder"]
    for file in glob.iglob(prefix+'/process/*.ini'):
        cmd = ["sed", "-i", "-r", "/^detectorZpvname/s/(detectorZpvname=).*/\\1" + detectorZencoder + "/", file]
        cfel_file.spawn_subprocess(cmd, wait=True)


    # Modify data source in psana.cfg
    # Unfortunately we gave different detectors different fields so we have to do some guessing
    print("Modifying data source...")
    file = prefix+'/process/psana.cfg'
    dataSource = result["dataSource"]
    if 'cspad' in dataSource.lower():
        print('Cspad = ', dataSource)
        cmd = ["sed", "-i", "-r", "/^cspadSource0/s/(cspadSource0 = ).*/\\1DetInfo(" + dataSource + ")/", file]
        cfel_file.spawn_subprocess(cmd, wait=True)
    if 'rayonix' in dataSource.lower():
        print('Rayonix = ', dataSource)
        cmd = ["sed", "-i", "-r", "/^rayonixSource0/s/(rayonixSource0 = ).*/\\1DetInfo(" + dataSource + ")/", file]
        cfel_file.spawn_subprocess(cmd, wait=True)
    if 'pnccd' in dataSource.lower():
        print('pnCCD = ', dataSource)
        cmd = ["sed", "-i", "-r", "/^pnccdSource0/s/(pnccdSource0 = ).*/\\1DetInfo(" + dataSource + ")/", file]
        cfel_file.spawn_subprocess(cmd, wait=True)


    print("Done modifying configuration scripts")

    return
    #end modify_cheetah_config_files()


# GMCA/APS start files, assuming LCLS template
def extract_gmca_template(self):
    #   Deduce experiment number, etc using de-referenced paths
    #   Assumes the file path follows the pattern:   /mnt/beegfs/qxu/23IDD-date/expt/run0001.

    from shutil import copyfile

    realdir = os.getcwd()

    fields = realdir.split("/")

    if '23ID' in fields[-1]:
        expt = "GMCA-eid"
    else:
        expt = fields[-1]

    xtcdir = realdir

    userdir = "{}/{}".format(realdir, 'cheetah')
    instr = "eiger16M" if '23IDB' in realdir else "pilatus6M"

    print('Deduced experiment information:')
    print('    Absolute path: ', realdir)
    print('    Instrument: ', instr)
    print('    Experiment: ', expt)
    print('    XTC directory: ', xtcdir)
    print('    Output directory: ', userdir)

    #
    # QMessageBox for confirmation before proceeding
    #
    msgBox = PyQt5.QtWidgets.QMessageBox()
    str1 = []
    str1.append('<b>Instrument:</b> ' + str(instr))
    str1.append('<b>Experiment:</b> ' + str(expt))
    str1.append('<b>XTC directory:</b> ' + str(xtcdir))
    str1.append('<b>Output directory:</b> ' + str(userdir))
    # str1.append('<b>Relative path:</b> '+ str(dir))
    str2 = str.join('<br>', str1)
    msgBox.setText(str2)

    msgBox.setInformativeText('<b>Proceed?</b>')
    msgBox.addButton(PyQt5.QtWidgets.QMessageBox.Yes)
    msgBox.addButton(PyQt5.QtWidgets.QMessageBox.Cancel)
    msgBox.setDefaultButton(PyQt5.QtWidgets.QMessageBox.Yes)

    ret = msgBox.exec_();
    # app.exit()

    if ret == PyQt5.QtWidgets.QMessageBox.Cancel:
        print("So long and thanks for all the fish.")
        self.exit_gui()

    # Unpack template
    print('>---------------------<')
    print('Extracting template...')
    template_file = '/mnt/software/px/serial-x/cheetah/template.tar.gz'
    if not os.path.exist(template_file):
        template_file = '/mnt/software/px/serial-x/cheetah/SLAC-template.tar.gz'

        if not os.path.exist(template_file):
            print("starting template file does not exist. Quitting.")
            exit(0)

    cmd = ['tar', '-xf', '{}'.format(template_file)]
    cfel_file.spawn_subprocess(cmd, wait=True)
    print("Done")

    # Fix permissions
    print('>---------------------<')
    print('Fixing permissions...')
    cmd = ['chgrp', '-R', expt, 'cheetah/']
    cfel_file.spawn_subprocess(cmd, wait=True)
    cmd = ['chmod', '-R', 'g+w', 'cheetah/']
    cfel_file.spawn_subprocess(cmd, wait=True)
    print("Done")

    # Modify gui/crawler.config
    print('>---------------------<')
    file = 'cheetah/gui/crawler.config'
    print('Modifying ', file)

    # Replace XTC directory with the correct location using sed
    xtcsedstr = str.replace(xtcdir, '/', '\\/')
    cmd = ["sed", "-i", "-r", "s/(xtcdir=).*/\\1" + xtcsedstr + "/", file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    print('>-------------------------<')
    cmd = ['cat', file]
    cfel_file.spawn_subprocess(cmd, wait=True)
    print('>-------------------------<')

    # Modify process/process
    file = 'cheetah/process/process'
    print('Modifying ', file)

    cmd = ["sed", "-i", "-r", "s/(expt=).*/\\1\"" + expt + "\"/", file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    xtcsedstr = str.replace(xtcdir, '/', '\\/')
    cmd = ["sed", "-i", "-r", "s/(XTCDIR=).*/\\1\"" + xtcsedstr + "\"/", file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    h5sedstr = str.replace(userdir, '/', '\\/') + '\/hdf5'
    cmd = ["sed", "-i", "-r", "s/(H5DIR=).*/\\1\"" + h5sedstr + "\"/", file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    confsedstr = str.replace(userdir, '/', '\\/') + '\/process'
    cmd = ["sed", "-i", "-r", "s/(CONFIGDIR=).*/\\1\"" + confsedstr + "\"/", file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    print('>-------------------------<')
    cmd = ['head', file]
    cfel_file.spawn_subprocess(cmd, wait=True)
    print('>-------------------------<')

    print("Working directory: ")
    print(os.getcwd() + '/cheetah')

    # update detector & distance encoder
    result = {}
    if '23IDD' in realdir:
        result["detectorType"] = 'pilatus6M'
        result["detectorZEncoder"] = '23i:CCD:D:ActPos'
    elif '23IDB' in realdir:
        result["detectorType"] = 'eiger16M'
        result["detectorZEncoder"] = '23o:CCD:D:ActPos'

    # This is needed because on startup we are one directory up from cheetah/gui, but when running we are in cheetah/gui
    dir = os.getcwd()
    if "cheetah/gui" in dir:
        prefix = '..'
    else:
        prefix = 'cheetah'

    print("INFO current work dir=", dir, "prefix=", prefix)

    # Modify detector type string
    print("Modifying detector type...")
    detectorType = result["detectorType"]
    for file in glob.iglob(prefix+'/process/*.ini'):
        cmd = ["sed", "-i", "-r", "/^detectorType/s/(detectorType=).*/\\1" + detectorType + "/", file]
        cfel_file.spawn_subprocess(cmd, wait=True)
        cmd = ["sed", "-i", "-r", "/^detectorName/s/(detectorName=).*/\\1" + detectorType + "/", file]
        cfel_file.spawn_subprocess(cmd, wait=True)
        if 'eiger' in detectorType or 'pilatus' in detectorType:

            cmd = ["sed", "-i", "-r", "/^darkcal=/s/(.*)/##\\1/", file]
            cfel_file.spawn_subprocess(cmd, wait=True)

            cmd = ["sed", "-i", "-r", "/^badpixelmap=/s/(.*)/##\\1/", file]
            cfel_file.spawn_subprocess(cmd, wait=True)

            cmd = ["sed", "-i", "-r", "/^geometry=/s/(.*)/##\\1/", file]
            cfel_file.spawn_subprocess(cmd, wait=True)

            cmd = ["sed", "-i", "-r", "/^cameraLengthOffset=/s/(.*)/##\\1/", file]
            cfel_file.spawn_subprocess(cmd, wait=True)

            cmd = ["sed", "-i", "-r", "/^cameraLengthScale=/s/(.*)/##\\1/", file]
            cfel_file.spawn_subprocess(cmd, wait=True)

            cmd = ["sed", "-i", "-r", "/^defaultcameralengthmm=/s/(.*)/##\\1/", file]
            cfel_file.spawn_subprocess(cmd, wait=True)

            cmd = ["sed", "-i", "-r", "/^pixelSize=/s/(.*)/##\\1/", file]
            cfel_file.spawn_subprocess(cmd, wait=True)

            cmd = ["sed", "-i", "-r", "/^peakmask=/s/(.*)/##\\1/", file]
            cfel_file.spawn_subprocess(cmd, wait=True)

            cmd = ["sed", "-i", "-r", "/^hitfinderADC=/s/(.*)/hitfinderADC=0/", file]
            cfel_file.spawn_subprocess(cmd, wait=True)

            cmd = ["sed", "-i", "-r", "/^dataSaveFormat=/s/(.*)/dataSaveFormat=INT32/", file]
            cfel_file.spawn_subprocess(cmd, wait=True)

    # Modify detector Z encoder string
    print("Modifying detector Z encoder...")
    detectorZencoder = result["detectorZEncoder"]
    for file in glob.iglob(prefix+'/process/*.ini'):
        cmd = ["sed", "-i", "-r", "/^detectorZpvname/s/(detectorZpvname=).*/\\1" + detectorZencoder + "/", file]
        cfel_file.spawn_subprocess(cmd, wait=True)

    # modifying processing scripts
    process_file = prefix + "/process/process"
    cmd = ["sed", "-i", "-r", "/^export QUEUE=/s/(.*)/export QUEUE='all.q'/", process_file]
    cfel_file.spawn_subprocess(cmd, wait=True)
    cmd = ["sed", "-i", "-r", "/^export CHEETAH=/s/(.*)/export CHEETAH='cheetah-cbf'/", process_file]
    cfel_file.spawn_subprocess(cmd, wait=True)
    cmd = ["sed", "-i", "-r", "/^source /s/(.*)/##\\1/", process_file]
    cfel_file.spawn_subprocess(cmd, wait=True)

    os.system("sed -i -r '/^\$HITFINDER/s/(.*)/$HITFINDER -q -O -r $1 -j 12 -t $4 -i $2/' cheetah/process/process")
    os.system("sed -i -r '/^export CHEETAH=/s/(.*)/export CHEETAH=\/mnt\/software\/px\/serial-x\/cheetah\/bin\/cheetah-cbf/' cheetah/process/process")
    copyfile("/mnt/software/px/serial-x/cheetah/scripts/hitfinder_gmca", prefix + "/process/hitfinder")
    os.system("cp /mnt/software/px/serial-x/cheetah/gmca-examples/*.geom " + prefix + "/calib/geometry/")

    os.system("rm -rf " + prefix + "/calib/geometry/current-geometry.geom")
    if 'pilatus' in detectorType in detectorType:
        os.system("ln -sr "+ prefix+ "/calib/geometry/pilatus-cheetah-cxi.geom " + prefix + "/calib/geometry/current-geometry.geom")
    elif 'eiger' in detectorType in detectorType:
        os.system("ln -sr "+ prefix+ "/calib/geometry/eiger-cheetah-cxi.geom " + prefix + "/calib/geometry/current-geometry.geom")

    os.system("sed -i -r '/^source/s/(.*)/source \/mnt\/software\/px\/serial-x\/serialx-setup.sh/' cheetah/process/queue.sh")
    os.system("sed -i '1isource \/mnt\/software\/px\/serial-x\/serialx-setup.sh' cheetah/process/index_nocell.sh")
    os.system("sed -i '1isource \/mnt\/software\/px\/serial-x\/serialx-setup.sh' cheetah/process/index_cell.sh")
    os.system("sed -i -r 's/--indexing=mosflm-raw-nolatt-nocell/--indexing=mosflm-nolatt-nocell /' cheetah/process/index_nocell.sh")
    os.system("sed -i -r 's/--peaks=cxi/--peaks=cxi --hdf5-peaks=\/entry_1\/result_1 /' cheetah/process/index_nocell.sh")

    os.system("sed -i -r '/^QUEUE=/s/(.*)/QUEUE=all.q/' cheetah/process/queue.sh")
    os.system("sed -i -r '/^FULLCOMMAND=/s/(.*)/FULLCOMMAND=\"qsub -q all.q -j y -o bsub.log -b y -N $LABEL -wd $WORKINGDIR $COMMAND\"/' cheetah/process/queue.sh")


