/*---------------------------------------------------------------------------*\
    OC4 OF2-method MoorDyn coupling restraint (rigid-body-dynamics framework).

    This is the VENDOR-INDEPENDENT FALLBACK mooring path. The primary path is
    FOAMmooring's moorDynR2; this restraint wraps the repo's own tested coupling
    core (MoorDynRestraintBridge -> MoorDynLoadProvider -> MoorDynCApiAdapter)
    so a moored OC4 case can run without an external mooring library.

    It is a Foam::RBD::restraints::restraint (NOT sixDoFRigidBodyMotion): it must
    be driven by `motionSolver rigidBodyMotion` (see constant/dynamicMeshDict_rbd).
    The RBD restraint interface hands us `rigidBodyModelState::t()/deltaT()`, so
    MoorDyn can be advanced one step per CFD time step.

    Registered type name: "moorDynCoupling"   (distinct from the paper's
    "moordyn" and FOAMmooring's "moorDynR2" to avoid run-time-selection clashes).
\*---------------------------------------------------------------------------*/

#ifndef RBD_moorDynRestraint_H
#define RBD_moorDynRestraint_H

#include "rigidBodyRestraint.H"
#include "Switch.H"
#include "fileName.H"

#include "coupling/MoorDynRestraintBridge.hpp"
#include "coupling/MoorDynLoadProvider.hpp"
#include "coupling/Types.hpp"

#include <memory>

namespace Foam
{
namespace RBD
{
namespace restraints
{

/*---------------------------------------------------------------------------*\
                       Class moorDynRestraint Declaration
\*---------------------------------------------------------------------------*/

class moorDynRestraint
:
    public restraint
{
    // Private data

        //- MoorDyn input file (relative to the case directory)
        fileName inputFilePath_;

        //- Optional explicit MoorDyn shared-library path (default libmoordyn.so)
        fileName libraryPath_;

        //- Optional MoorDyn log-file path
        fileName logFilePath_;

        //- Platform reference point in the BODY frame: vector from the body
        //  joint J (root origin) to the platform reference point. Listing A1's
        //  `fromJtoPtfmReferencePoint`. Its current global position/orientation
        //  is what is fed to MoorDyn each step.
        point refPointBody_;

        //- Constant global-datum shift OpenFOAM -> MoorDyn (e.g. (0 0 -SWL) so
        //  MoorDyn sees the still-water line at z=0).
        vector openFoamToMoorDynOffsetGlobal_;

        //- Human-readable name for the coupling reference point
        word referencePointName_;

        //- Shift the MoorDyn resultant onto the reference point (default true)
        Switch shiftResultToPlatformReferencePoint_;

        //- Optional per-step CSV debug trace
        fileName debugCsvPath_;

        //- MoorDyn verbosity
        label verbosity_;

        //- Skip MoorDyn's initial-condition solve (default false)
        Switch skipInitialConditionSolve_;


    // Runtime state (mutable: restrain() is const)

        //- Solver-agnostic coupling bridge (lazily built on first restrain())
        mutable std::unique_ptr<oc4::coupling::MoorDynRestraintBridge> bridge_;

        //- Whether MoorDyn has been advanced at least once
        mutable bool hasStepped_;

        //- Solution time of the last MoorDyn step (for once-per-step gating)
        mutable scalar lastStepTime_;

        //- Cached body wrench (about the global origin) from the last step
        mutable spatialVector cachedWrench_;


    // Private Member Functions

        static oc4::coupling::Vector3 toCouplingVector(const vector&);
        static oc4::coupling::Matrix3 toCouplingMatrix(const tensor&);
        static vector toFoamVector(const oc4::coupling::Vector3&);

        oc4::coupling::MoorDynLoadProviderConfig makeProviderConfig() const;
        void rebuildBridge() const;


public:

    //- Runtime type information
    TypeName("moorDynCoupling");


    // Constructors

        moorDynRestraint
        (
            const word& name,
            const dictionary& dict,
            const rigidBodyModel& model
        );

        //- Copy constructor (copies configuration; resets runtime state so the
        //  clone lazily rebuilds its own MoorDyn instance)
        moorDynRestraint(const moorDynRestraint& other);

        //- Construct and return a clone
        virtual autoPtr<restraint> clone() const
        {
            return autoPtr<restraint>(new moorDynRestraint(*this));
        }


    //- Destructor
    virtual ~moorDynRestraint();


    // Member Functions

        //- Accumulate the mooring wrench into the external-force field
        virtual void restrain
        (
            scalarField& tau,
            Field<spatialVector>& fx,
            const rigidBodyModelState& state
        ) const;

        //- Update properties from given dictionary
        virtual bool read(const dictionary& dict);

        //- Write
        virtual void write(Ostream&) const;
};


} // End namespace restraints
} // End namespace RBD
} // End namespace Foam

#endif

// ************************************************************************* //
