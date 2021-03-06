#!/bin/bash

#===================================================================#
# allrun script for cfdemPostproc
# Christoph Goniva - Nov. 2011
#===================================================================#

#- source CFDEM env vars
. ~/.bashrc

#- include functions
source $CFDEM_SRC_DIR/lagrangian/cfdemParticle/etc/functions.sh

#- define variables
casePath="$(dirname "$(readlink -f ${BASH_SOURCE[0]})")"

liggghtsSim="true"
cfdemPostProc="true"
postproc="true"

# check if mesh was built
if [ -d "$casePath/CFD/constant/polyMesh/boundary" ]; then
    echo "mesh was built before - using old mesh"
else
    echo "mesh needs to be built"
    cd $casePath/CFD
    blockMesh
fi

if [ $liggghtsSim == "true" ]
  then
    #--------------------------------------------------------------------------------#
    #- define variables
    logpath="$casePath"
    headerText="run_liggghts_fillCylinder_DEM"
    logfileName="log_$headerText"
    solverName="in.liggghts_init"
    #--------------------------------------------------------------------------------#

    #- clean up case
    rm -r $casePath/DEM/post/*

    #- call function to run DEM case
    DEMrun $logpath $logfileName $casePath $headerText $solverName


    #- generate VTK data
    cd $casePath/DEM/post
    python $CFDEM_LPP_DIR/lpp.py  dump.liggghts_init

fi

if [ $cfdemPostProc == "true" ]
  then
    #--------------------------------------------------------------------------------#
    #- define variables
    logpath="$casePath"
    headerText="run_cfdemPostproc_fillCylinder_CFD"
    logfileName="log_$headerText"
    solverName="cfdemPostproc"
    debugMode="off"          # on | off | strict
    #--------------------------------------------------------------------------------#

    #- clean up case
    rm -r $casePath/CFD/0.*

    #- call function to run CFD cas
    CFDrun $logpath $logfileName $casePath $headerText $solverName $debugMode
fi

if [ $postproc == "true" ]
  then

    #- get VTK data from CFD sim
    #foamToVTK
    
    #- start paraview
    echo ""
    echo "trying to start paraview..."
    paraview
    read
fi

#- keep terminal open (if started in new terminal)
#echo "...press enter to clean up case"
#echo "press Ctr+C to keep data"
#read

#- clean up case
echo "deleting data at: $casePath ?"
read
rm -r $casePath/CFD/0.*
rm -r $casePath/CFD/lagrangian
rm -r $casePath/CFD/VTK
rm -r $casePath/DEM/post/*
rm -r $casePath/DEM/log.*
echo "done"

#- preserve post directory
echo "dummyfile" >> $casePath/DEM/post/dummy
