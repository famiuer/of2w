! OC4 bridge module data for inclusion inside OpenFAST's FAST_Data module.
!
! Integration point:
!   external/openfast/modules/openfast-library/src/FAST_Library.f90
!
! Insert this include inside MODULE FAST_Data before the CONTAINS statement.
!
! The bridge is intentionally colocated with FAST_Data because the stock
! OpenFAST library keeps Turbine(:) private inside that module. Following the
! WES/OF2 logic, the custom bridge needs direct access to the evolving turbine
! state to map imposed platform motion in and aggregated structural loads out.

TYPE, PRIVATE :: OC4_PlatformBridgeStateType
   LOGICAL :: isActive = .false.
   LOGICAL :: motionIsAvailable = .false.
   REAL(C_DOUBLE) :: platformReferencePoint(3) = 0.0_C_DOUBLE
   REAL(C_DOUBLE) :: towerBasePoint(3) = 0.0_C_DOUBLE
   REAL(C_DOUBLE) :: openFoamToOpenFASTOffset(3) = 0.0_C_DOUBLE
   REAL(C_DOUBLE) :: displacement(6) = 0.0_C_DOUBLE
   REAL(C_DOUBLE) :: velocity(6) = 0.0_C_DOUBLE
   REAL(C_DOUBLE) :: acceleration(6) = 0.0_C_DOUBLE
   REAL(C_DOUBLE) :: applicationPoint(3) = 0.0_C_DOUBLE
   REAL(C_DOUBLE) :: force(3) = 0.0_C_DOUBLE
   REAL(C_DOUBLE) :: moment(3) = 0.0_C_DOUBLE
   REAL(C_DOUBLE) :: towerTopDisplacement(3) = 0.0_C_DOUBLE
   REAL(C_DOUBLE) :: bladePitchRad(3) = 0.0_C_DOUBLE
   REAL(C_DOUBLE) :: rotorSpeedRadPerSec = 0.0_C_DOUBLE
   REAL(C_DOUBLE) :: generatorPowerW = 0.0_C_DOUBLE
   INTEGER(C_INT) :: controllerOk = 1_C_INT
END TYPE OC4_PlatformBridgeStateType

TYPE(OC4_PlatformBridgeStateType), ALLOCATABLE, PRIVATE :: OC4BridgeState(:)
