/*---------------------------------------------------------------------------*\
    of2RigidBodyStateWriter — restart support, phase 1.

    Writes <t>/uniform/rigidBodyMotionState at every write time by invoking
    the mesh motion solver's native writeObject() (virtual dispatch reaches
    rigidBodyMeshMotion::writeObject, so the file format is exactly what
    rigidBodyMeshMotion's READ_IF_PRESENT constructor path expects).

    Why this exists: dynamicOversetFvMesh derives from
    dynamicMotionSolverListFvMesh, whose single-motionSolver path constructs
    the solver from a NO_WRITE IOdictionary; motionSolver::stealRegistration
    re-registers the solver under "dynamicMeshDict" but INHERITS that
    NO_WRITE — registered, never auto-written. Plain
    dynamicMotionSolverFvMesh (morphing-mesh cases) uses the one-argument
    motionSolver::New path with AUTO_WRITE, which is why those cases carry
    uniform/rigidBodyMotionState and overset cases do not. Patching the core
    class is not an option on module-managed clusters, but the registered
    solver IS reachable through the mesh registry — so this functionObject
    simply calls its writeObject() at write times.

    Usage (system/controlDict):
        functions
        {
            rigidBodyState
            {
                type            of2RigidBodyStateWriter;
                libs            ("libOF2.so");
                writeControl    writeTime;
            }
        }
\*---------------------------------------------------------------------------*/

#include "functionObject.H"
#include "Time.H"
#include "polyMesh.H"
#include "motionSolver.H"
#include "addToRunTimeSelectionTable.H"

namespace Foam
{
namespace functionObjects
{

/*---------------------------------------------------------------------------*\
                   Class of2RigidBodyStateWriter Declaration
\*---------------------------------------------------------------------------*/

class of2RigidBodyStateWriter
:
    public functionObject
{
    // Private data

        const Time& time_;

        //- Warn only once if no motion solver is found
        mutable bool warned_;


public:

    //- Runtime type information
    TypeName("of2RigidBodyStateWriter");


    // Constructors

        of2RigidBodyStateWriter
        (
            const word& name,
            const Time& runTime,
            const dictionary& dict
        )
        :
            functionObject(name),
            time_(runTime),
            warned_(false)
        {
            (void)dict;
        }


    // Member Functions

        virtual bool execute()
        {
            return true;
        }

        virtual bool write()
        {
            // guard against non-writeTime invocations (e.g. a controlDict
            // entry left at the default writeControl timeStep): writing
            // uniform/ data into a non-snapshot time would create stray
            // partial time directories.
            if (!time_.writeTime())
            {
                return true;
            }

            const polyMesh& mesh =
                time_.lookupObject<polyMesh>(polyMesh::defaultRegion);

            const motionSolver* msPtr =
                mesh.thisDb().cfindObject<motionSolver>("dynamicMeshDict");

            if (!msPtr)
            {
                if (!warned_)
                {
                    WarningInFunction
                        << "no motionSolver registered as 'dynamicMeshDict' "
                        << "- rigid-body state will NOT be checkpointed "
                        << "(static mesh phase?)" << endl;
                    warned_ = true;
                }
                return true;
            }

            // virtual dispatch: rigidBodyMeshMotion::writeObject writes
            // <t>/uniform/rigidBodyMotionState (q, qDot, qDdot) natively
            return msPtr->writeObject
            (
                IOstreamOption(time_.writeFormat()),
                true
            );
        }

        virtual bool read(const dictionary&)
        {
            return true;
        }
};


defineTypeNameAndDebug(of2RigidBodyStateWriter, 0);

addToRunTimeSelectionTable
(
    functionObject,
    of2RigidBodyStateWriter,
    dictionary
);

} // End namespace functionObjects
} // End namespace Foam

// ************************************************************************* //
