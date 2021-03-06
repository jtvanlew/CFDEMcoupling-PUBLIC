/*---------------------------------------------------------------------------*\
    CFDEMcoupling - Open Source CFD-DEM coupling

    CFDEMcoupling is part of the CFDEMproject
    www.cfdem.com
                                Christoph Goniva, christoph.goniva@cfdem.com
                                Copyright 2009-2012 JKU Linz
                                Copyright 2012-     DCS Computing GmbH, Linz
-------------------------------------------------------------------------------
License
    This file is part of CFDEMcoupling.

    CFDEMcoupling is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation; either version 3 of the License, or (at your
    option) any later version.

    CFDEMcoupling is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with CFDEMcoupling; if not, write to the Free Software Foundation,
    Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

Description
    This code is designed to realize coupled CFD-DEM simulations using LIGGGHTS
    and OpenFOAM(R). Note: this code is not part of OpenFOAM(R) (see DISCLAIMER).
\*---------------------------------------------------------------------------*/

#include "error.H"

#include "LaEuScalarTemp.H"
#include "addToRunTimeSelectionTable.H"
#include "dataExchangeModel.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

defineTypeNameAndDebug(LaEuScalarTemp, 0);

addToRunTimeSelectionTable
(
    forceModel,
    LaEuScalarTemp,
    dictionary
);


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

// Construct from components
LaEuScalarTemp::LaEuScalarTemp
(
    const dictionary& dict,
    cfdemCloud& sm
)
:
    forceModel(dict,sm),
    propsDict_(dict.subDict(typeName + "Props")),
    verbose_(false),
    ks_(readScalar(propsDict_.lookup("ks"))),        // added by jvl
    tempFieldName_(propsDict_.lookup("tempFieldName")),
    tempField_(sm.mesh().lookupObject<volScalarField> (tempFieldName_)),
    voidfractionFieldName_(propsDict_.lookup("voidfractionFieldName")),
    voidfraction_(sm.mesh().lookupObject<volScalarField> (voidfractionFieldName_)),
    maxSource_(1e30),
    velFieldName_(propsDict_.lookup("velFieldName")),
    U_(sm.mesh().lookupObject<volVectorField> (velFieldName_)),
    partTempName_(propsDict_.lookup("partTempName")),
    partTemp_(NULL),
    partHeatFluxName_(propsDict_.lookup("partHeatFluxName")),
    partHeatFlux_(NULL),
    lambda_(readScalar(propsDict_.lookup("lambda"))),
    Cp_(readScalar(propsDict_.lookup("Cp"))),
    densityFieldName_(propsDict_.lookup("densityFieldName")),
    rho_(sm.mesh().lookupObject<volScalarField> (densityFieldName_)),
    interpolation_(false)
{
    allocateMyArrays();

    if (propsDict_.found("maxSource"))
    {
        maxSource_=readScalar(propsDict_.lookup ("maxSource"));
        Info << "limiting eulerian source field to: " << maxSource_ << endl;
    }
    if (propsDict_.found("interpolation")) interpolation_=true;
    if (propsDict_.found("verbose")) verbose_=true;

Info << "verbose_" << verbose_ << endl;

    particleCloud_.checkCG(false);
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

LaEuScalarTemp::~LaEuScalarTemp()
{
    delete partTemp_;
    delete partHeatFlux_;
}

// * * * * * * * * * * * * * * * private Member Functions  * * * * * * * * * * * * * //
void LaEuScalarTemp::allocateMyArrays() const
{
    // get memory for 2d arrays
    double initVal=0.0;
    particleCloud_.dataExchangeM().allocateArray(partTemp_,initVal,1);  // field/initVal/with/lenghtFromLigghts
    particleCloud_.dataExchangeM().allocateArray(partHeatFlux_,initVal,1);
}
// * * * * * * * * * * * * * * * public Member Functions  * * * * * * * * * * * * * //

void LaEuScalarTemp::setForce() const
{
    // do nothing
}

void LaEuScalarTemp::manipulateScalarField(volScalarField& EuField) const
{
    // realloc the arrays
    allocateMyArrays();

    // reset Scalar field
    EuField.internalField() = 0.0;

    // get DEM data
    particleCloud_.dataExchangeM().getData(partTempName_,"scalar-atom",partTemp_);

    // get viscosity field
    #ifdef comp
        const volScalarField& nufField = particleCloud_.turbulence().mu() / rho_;
    #else
        const volScalarField& nufField = particleCloud_.turbulence().nu();
    #endif

    // calc La based heat flux
    vector position(0,0,0);
    scalar voidfraction(1);
    vector Ufluid(0,0,0);
    scalar Tfluid(0);
    label cellI=0;
    vector Us(0,0,0);
    scalar ds(0);
    scalar nuf(0);
    scalar magUr(0);
    scalar As(0);
    scalar Rep(0);
    scalar Pr(0);
    scalar Nup(0);
    scalar Bi(0);
    scalar n = 3.5; // model parameter

    interpolationCellPoint<scalar> voidfractionInterpolator_(voidfraction_);
    interpolationCellPoint<vector> UInterpolator_(U_);
    interpolationCellPoint<scalar> TInterpolator_(tempField_);

    for(int index = 0;index < particleCloud_.numberOfParticles(); ++index)
    {
        //if(particleCloud_.regionM().inRegion()[index][0])
        //{
            cellI = particleCloud_.cellIDs()[index][0];
            if(cellI >= 0)
            {
                if(interpolation_)
                {
	                position = particleCloud_.position(index);
                    voidfraction = voidfractionInterpolator_.interpolate(position,cellI);
                    Ufluid = UInterpolator_.interpolate(position,cellI);
                    Tfluid = TInterpolator_.interpolate(position,cellI);
                }else
                {
					voidfraction = voidfraction_[cellI];
                    Ufluid = U_[cellI];
                    Tfluid = tempField_[cellI];
                }

                // calc relative velocity
                Us = particleCloud_.velocity(index);
                magUr = mag(Ufluid-Us);
                ds = 2*particleCloud_.radius(index);
                As = ds*ds*M_PI;
                nuf = nufField[cellI];
                Rep = ds*magUr/nuf;
                Pr = Cp_*nuf*rho_[cellI]/lambda_;

                if (Rep < 200)
                {
                    Nup = 2+0.6*pow(voidfraction,n)*sqrt(Rep)*pow(Pr,0.33);
                }
                else if (Rep < 1500)
                {
                    Nup = 2+0.5*pow(voidfraction,n)*sqrt(Rep)*pow(Pr,0.33)
                                 +0.02*pow(voidfraction,n)*pow(Rep,0.8)*pow(Pr,0.33);
                }
                else
                {
                    Nup = 2+0.000045*pow(voidfraction,n)*pow(Rep,1.8);
                }
                scalar hp = lambda_*Nup/(ds);
                scalar h(0);
                Bi = hp*ds/ks_;

                if (Bi > 0.1)
                {
                    h = hp/(1.+Bi/5.);
                }
                else
                {
                    h = hp;
                }

                // calc convective heat flux [W]
                scalar partHeatFlux = h * As * (Tfluid - partTemp_[index][0]);
                partHeatFlux_[index][0] = partHeatFlux;


                if(verbose_ && index >=0 && index <2)
                {
                    Info << "partHeatFlux = " << partHeatFlux << endl;
                    Info << "magUr = " << magUr << endl;
                    Info << "As = " << As << endl;
                    Info << "nuf = " << nuf << endl;
                    Info << "Rep = " << Rep << endl;
                    Info << "Bi = " << Bi << endl;
                    Info << "Original h = " << hp << endl;
                    Info << "Corrected h = " << h << endl;
                    Info << "Pr = " << Pr << endl;
                    Info << "Nup = " << Nup << endl;
                    Info << "voidfraction = " << voidfraction << endl;
                    Info << "partTemp_[index][0] = " << partTemp_[index][0] << endl  ;
                    Info << "Tfluid = " << Tfluid << endl  ;
                }
            }
        //}
    }

    particleCloud_.averagingM().setScalarSum
    (
        EuField,
        partHeatFlux_,
        particleCloud_.particleWeights(),
        NULL
    );

    // scale with -1/(Vcell*rho*Cp)
    EuField.internalField() /= -rho_.internalField()*Cp_*EuField.mesh().V();

    // limit source term
    scalar EuFieldInCell;
    forAll(EuField,cellI)
    {
        EuFieldInCell = EuField[cellI];

        if(mag(EuFieldInCell) > maxSource_ )
        {
             EuField[cellI] = sign(EuFieldInCell) * maxSource_;
        }
    }

    Info << "total convective particle-fluid heat flux [W] (Eulerian) = " << gSum(EuField*rho_*Cp_*EuField.mesh().V()) << endl;

    // give DEM data
    particleCloud_.dataExchangeM().giveData(partHeatFluxName_,"scalar-atom", partHeatFlux_);
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// ************************************************************************* //
