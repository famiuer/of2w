/*---------------------------------------------------------------------------*\
    OC4 OF2-method MoorDyn coupling restraint (RBD framework). See the header
    for the design notes. Wraps the solver-agnostic coupling core behind the
    Foam::RBD::restraints::restraint interface.
\*---------------------------------------------------------------------------*/

#include "moorDynRestraint.H"

#include "rigidBodyModel.H"
#include "addToRunTimeSelectionTable.H"

#include "coupling/MoorDynCApiAdapter.hpp"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
namespace RBD
{
namespace restraints
{
    defineTypeNameAndDebug(moorDynRestraint, 0);

    addToRunTimeSelectionTable
    (
        restraint,
        moorDynRestraint,
        dictionary
    );
}
}
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::RBD::restraints::moorDynRestraint::moorDynRestraint
(
    const word& name,
    const dictionary& dict,
    const rigidBodyModel& model
)
:
    restraint(name, dict, model),
    inputFilePath_(),
    libraryPath_(),
    logFilePath_(),
    refPointBody_(Zero),
    openFoamToMoorDynOffsetGlobal_(Zero),
    referencePointName_("oc4ReferencePoint"),
    shiftResultToPlatformReferencePoint_(true),
    debugCsvPath_(),
    verbosity_(0),
    skipInitialConditionSolve_(false),
    bridge_(nullptr),
    hasStepped_(false),
    lastStepTime_(-GREAT),
    cachedWrench_(Zero)
{
    read(dict);
}


Foam::RBD::restraints::moorDynRestraint::moorDynRestraint
(
    const moorDynRestraint& other
)
:
    restraint(other),
    inputFilePath_(other.inputFilePath_),
    libraryPath_(other.libraryPath_),
    logFilePath_(other.logFilePath_),
    refPointBody_(other.refPointBody_),
    openFoamToMoorDynOffsetGlobal_(other.openFoamToMoorDynOffsetGlobal_),
    referencePointName_(other.referencePointName_),
    shiftResultToPlatformReferencePoint_(other.shiftResultToPlatformReferencePoint_),
    debugCsvPath_(other.debugCsvPath_),
    verbosity_(other.verbosity_),
    skipInitialConditionSolve_(other.skipInitialConditionSolve_),
    bridge_(nullptr),
    hasStepped_(false),
    lastStepTime_(-GREAT),
    cachedWrench_(Zero)
{}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::RBD::restraints::moorDynRestraint::~moorDynRestraint()
{}


// * * * * * * * * * * * * * * Conversion helpers  * * * * * * * * * * * * * //

oc4::coupling::Vector3
Foam::RBD::restraints::moorDynRestraint::toCouplingVector(const vector& v)
{
    return {v.x(), v.y(), v.z()};
}


oc4::coupling::Matrix3
Foam::RBD::restraints::moorDynRestraint::toCouplingMatrix(const tensor& t)
{
    oc4::coupling::Matrix3 m{};
    m(0, 0) = t.xx(); m(0, 1) = t.xy(); m(0, 2) = t.xz();
    m(1, 0) = t.yx(); m(1, 1) = t.yy(); m(1, 2) = t.yz();
    m(2, 0) = t.zx(); m(2, 1) = t.zy(); m(2, 2) = t.zz();
    return m;
}


Foam::vector
Foam::RBD::restraints::moorDynRestraint::toFoamVector
(
    const oc4::coupling::Vector3& v
)
{
    return vector(v.x, v.y, v.z);
}


// * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * * //

oc4::coupling::MoorDynLoadProviderConfig
Foam::RBD::restraints::moorDynRestraint::makeProviderConfig() const
{
    oc4::coupling::MoorDynLoadProviderConfig config{};
    config.moorDyn.inputFilePath = inputFilePath_.c_str();
    config.moorDyn.libraryPath = libraryPath_.c_str();
    config.moorDyn.logFilePath = logFilePath_.c_str();

    // platformReferencePoint here is metadata only (frame check + name); the
    // actual MoorDyn body position comes from the per-step snapshot.
    config.moorDyn.platformReferencePoint.position = toCouplingVector(refPointBody_);
    config.moorDyn.platformReferencePoint.frame = oc4::coupling::FrameId::global;
    config.moorDyn.platformReferencePoint.name = referencePointName_.c_str();

    config.moorDyn.openFoamToMoorDynOffsetGlobal =
        toCouplingVector(openFoamToMoorDynOffsetGlobal_);
    config.moorDyn.skipInitialConditionSolve = skipInitialConditionSolve_;
    config.moorDyn.verbosity = verbosity_;
    config.shiftResultToPlatformReferencePoint =
        shiftResultToPlatformReferencePoint_;
    config.debugCsvPath = debugCsvPath_.c_str();
    return config;
}


void Foam::RBD::restraints::moorDynRestraint::rebuildBridge() const
{
    auto adapter = std::make_unique<oc4::coupling::MoorDynCApiAdapter>();
    auto provider = std::make_unique<oc4::coupling::MoorDynLoadProvider>
    (
        makeProviderConfig(),
        std::move(adapter)
    );
    bridge_ = std::make_unique<oc4::coupling::MoorDynRestraintBridge>
    (
        std::move(provider)
    );
    hasStepped_ = false;
}


void Foam::RBD::restraints::moorDynRestraint::restrain
(
    scalarField& tau,
    Field<spatialVector>& fx,
    const rigidBodyModelState& state
) const
{
    if (!bridge_)
    {
        rebuildBridge();
    }

    const scalar t = state.t();
    const scalar dt = state.deltaT();

    // Loose (staggered) coupling: advance MoorDyn exactly ONCE per CFD time
    // step. restrain() may be invoked several times per step (RBD nIter and/or
    // PIMPLE outer correctors); step only when the solution time advances and
    // reuse the cached wrench within a step. dt must be positive (MoorDyn_Step
    // requires it) and is the CFD step that integrated to this state.
    if ((!hasStepped_ || t > lastStepTime_ + 0.5*dt) && dt > SMALL)
    {
        oc4::coupling::RigidBodyMotionSnapshot snapshot{};
        snapshot.time = t;
        snapshot.dt = dt;

        const point refGlobal = bodyPoint(refPointBody_);
        snapshot.referencePoint.position = toCouplingVector(refGlobal);
        snapshot.referencePoint.frame = oc4::coupling::FrameId::global;
        snapshot.referencePoint.name = referencePointName_.c_str();

        // X0(body) maps global->body; its rotation E = R_{body<-global}, so the
        // global-from-body orientation fed to the core is its transpose.
        snapshot.orientationGlobalFromBody =
            toCouplingMatrix(model_.X0(bodyID_).E().T());

        // Spatial velocity of the reference point, expressed in the global
        // frame: linear part = point velocity, angular part = body omega.
        const spatialVector vRef = bodyPointVelocity(refPointBody_);
        snapshot.velocityGlobal = toCouplingVector(vRef.l());
        snapshot.angularVelocityGlobal = toCouplingVector(vRef.w());

        const oc4::coupling::Wrench load = bridge_->evaluate(snapshot);

        const vector force = toFoamVector(load.force);
        const vector momentAtRef = toFoamVector(load.moment);
        const point appPt = toFoamVector(load.applicationPoint.position);

        // RBD external force fx is a wrench about the GLOBAL ORIGIN (see
        // linearSpring/externalForce). Shift the reference-point moment to it:
        // M_origin = appPt x F + M_ref.
        cachedWrench_ = spatialVector((appPt ^ force) + momentAtRef, force);

        lastStepTime_ = t;
        hasStepped_ = true;
    }

    fx[bodyIndex_] += cachedWrench_;
}


bool Foam::RBD::restraints::moorDynRestraint::read(const dictionary& dict)
{
    restraint::read(dict);

    coeffs_.readEntry("inputFile", inputFilePath_);
    coeffs_.readIfPresent("libraryPath", libraryPath_);
    coeffs_.readIfPresent("logFilePath", logFilePath_);
    coeffs_.readEntry("fromJtoPtfmReferencePoint", refPointBody_);
    coeffs_.readIfPresent("openFoamToMoorDynOffset", openFoamToMoorDynOffsetGlobal_);
    coeffs_.readIfPresent("referencePointName", referencePointName_);
    coeffs_.readIfPresent
    (
        "shiftResultToPlatformReferencePoint",
        shiftResultToPlatformReferencePoint_
    );
    coeffs_.readIfPresent("debugCsvPath", debugCsvPath_);
    coeffs_.readIfPresent("verbosity", verbosity_);
    coeffs_.readIfPresent("skipInitialConditionSolve", skipInitialConditionSolve_);

    // Force a rebuild on the next restrain() so dictionary edits take effect.
    bridge_.reset();
    hasStepped_ = false;

    return true;
}


void Foam::RBD::restraints::moorDynRestraint::write(Ostream& os) const
{
    restraint::write(os);   // writes "type" and "body"

    os.writeEntry("inputFile", inputFilePath_);
    os.writeEntry("libraryPath", libraryPath_);
    os.writeEntry("logFilePath", logFilePath_);
    os.writeEntry("fromJtoPtfmReferencePoint", refPointBody_);
    os.writeEntry("openFoamToMoorDynOffset", openFoamToMoorDynOffsetGlobal_);
    os.writeEntry("referencePointName", referencePointName_);
    os.writeEntry
    (
        "shiftResultToPlatformReferencePoint",
        shiftResultToPlatformReferencePoint_
    );
    os.writeEntry("debugCsvPath", debugCsvPath_);
    os.writeEntry("verbosity", verbosity_);
    os.writeEntry("skipInitialConditionSolve", skipInitialConditionSolve_);
}


// ************************************************************************* //
