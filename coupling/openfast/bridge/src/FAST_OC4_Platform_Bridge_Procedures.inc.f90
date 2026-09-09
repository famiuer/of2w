! OC4 bridge procedures for inclusion inside OpenFAST's FAST_Data module.
!
! Integration point:
!   external/openfast/modules/openfast-library/src/FAST_Library.f90
!
! Insert this include near the end of MODULE FAST_Data before END MODULE.
!
! These procedures intentionally follow the WES/OF2 split:
!   1. OpenFOAM owns the platform motion.
!   2. FAST_OC4_Platform_SetMotion stores that imposed 6-DOF motion.
!   3. OpenFAST-side helper routines map platform motion into the internal
!      structural/aero representation, following the WaveTank pattern:
!         PRP -> Tower
!         PRP -> Hub
!         Hub -> Blade roots
!   4. OpenFAST-side helper routines aggregate returned loads back to one
!      platform/interface wrench.

subroutine FAST_OC4_Platform_SetMotion(iTurb_c, displacement_c, velocity_c, acceleration_c, ErrStat_c, ErrMsg_c) bind (C, name='FAST_OC4_Platform_SetMotion')
   implicit none
#ifndef IMPLICIT_DLLEXPORT
!DEC$ ATTRIBUTES DLLEXPORT :: FAST_OC4_Platform_SetMotion
!GCC$ ATTRIBUTES DLLEXPORT :: FAST_OC4_Platform_SetMotion
#endif
   integer(C_INT),         intent(in   ) :: iTurb_c
   real(C_DOUBLE),         intent(in   ) :: displacement_c(6)
   real(C_DOUBLE),         intent(in   ) :: velocity_c(6)
   real(C_DOUBLE),         intent(in   ) :: acceleration_c(6)
   integer(C_INT),         intent(  out) :: ErrStat_c
   character(kind=C_CHAR), intent(  out) :: ErrMsg_c(IntfStrLen)

   integer(IntKi)                          :: iTurb
   integer(IntKi)                          :: ErrStat2
   character(IntfStrLen-1)                 :: ErrMsg2
   character(*), parameter                 :: RoutineName = 'FAST_OC4_Platform_SetMotion'

   iTurb = int(iTurb_c, IntKi) + 1
   call OC4Bridge_EnsureState(iTurb, ErrStat2, ErrMsg2)
   if (ErrStat2 >= AbortErrLev) then
      ErrStat_c = ErrStat2
      ErrMsg_c = transfer(trim(ErrMsg2)//C_NULL_CHAR, ErrMsg_c)
      return
   end if

   OC4BridgeState(iTurb)%displacement = displacement_c
   OC4BridgeState(iTurb)%velocity = velocity_c
   OC4BridgeState(iTurb)%acceleration = acceleration_c
   OC4BridgeState(iTurb)%motionIsAvailable = .true.

   ErrStat_c = ErrID_None
   ErrMsg_c = transfer(C_NULL_CHAR, ErrMsg_c)
end subroutine FAST_OC4_Platform_SetMotion


subroutine FAST_OC4_Platform_GetReactionLoad(iTurb_c, applicationPoint_c, force_c, moment_c, ErrStat_c, ErrMsg_c) bind (C, name='FAST_OC4_Platform_GetReactionLoad')
   implicit none
#ifndef IMPLICIT_DLLEXPORT
!DEC$ ATTRIBUTES DLLEXPORT :: FAST_OC4_Platform_GetReactionLoad
!GCC$ ATTRIBUTES DLLEXPORT :: FAST_OC4_Platform_GetReactionLoad
#endif
   integer(C_INT),         intent(in   ) :: iTurb_c
   real(C_DOUBLE),         intent(  out) :: applicationPoint_c(3)
   real(C_DOUBLE),         intent(  out) :: force_c(3)
   real(C_DOUBLE),         intent(  out) :: moment_c(3)
   integer(C_INT),         intent(  out) :: ErrStat_c
   character(kind=C_CHAR), intent(  out) :: ErrMsg_c(IntfStrLen)

   integer(IntKi)                          :: iTurb
   integer(IntKi)                          :: ErrStat2
   character(IntfStrLen-1)                 :: ErrMsg2

   iTurb = int(iTurb_c, IntKi) + 1
   call OC4Bridge_EnsureState(iTurb, ErrStat2, ErrMsg2)
   if (ErrStat2 >= AbortErrLev) then
      ErrStat_c = ErrStat2
      ErrMsg_c = transfer(trim(ErrMsg2)//C_NULL_CHAR, ErrMsg_c)
      return
   end if

   applicationPoint_c = OC4BridgeState(iTurb)%applicationPoint
   force_c = OC4BridgeState(iTurb)%force
   moment_c = OC4BridgeState(iTurb)%moment

   ErrStat_c = ErrID_None
   ErrMsg_c = transfer(C_NULL_CHAR, ErrMsg_c)
end subroutine FAST_OC4_Platform_GetReactionLoad


subroutine FAST_OC4_Platform_GetDiagnostics(iTurb_c, rotorSpeedRadPerSec_c, generatorPowerW_c, bladePitchRad_c, towerTopDisplacement_c, controllerOk_c, ErrStat_c, ErrMsg_c) bind (C, name='FAST_OC4_Platform_GetDiagnostics')
   implicit none
#ifndef IMPLICIT_DLLEXPORT
!DEC$ ATTRIBUTES DLLEXPORT :: FAST_OC4_Platform_GetDiagnostics
!GCC$ ATTRIBUTES DLLEXPORT :: FAST_OC4_Platform_GetDiagnostics
#endif
   integer(C_INT),         intent(in   ) :: iTurb_c
   real(C_DOUBLE),         intent(  out) :: rotorSpeedRadPerSec_c
   real(C_DOUBLE),         intent(  out) :: generatorPowerW_c
   real(C_DOUBLE),         intent(  out) :: bladePitchRad_c(3)
   real(C_DOUBLE),         intent(  out) :: towerTopDisplacement_c(3)
   integer(C_INT),         intent(  out) :: controllerOk_c
   integer(C_INT),         intent(  out) :: ErrStat_c
   character(kind=C_CHAR), intent(  out) :: ErrMsg_c(IntfStrLen)

   integer(IntKi)                          :: iTurb
   integer(IntKi)                          :: ErrStat2
   character(IntfStrLen-1)                 :: ErrMsg2

   iTurb = int(iTurb_c, IntKi) + 1
   call OC4Bridge_EnsureState(iTurb, ErrStat2, ErrMsg2)
   if (ErrStat2 >= AbortErrLev) then
      ErrStat_c = ErrStat2
      ErrMsg_c = transfer(trim(ErrMsg2)//C_NULL_CHAR, ErrMsg_c)
      return
   end if

   rotorSpeedRadPerSec_c = OC4BridgeState(iTurb)%rotorSpeedRadPerSec
   generatorPowerW_c = OC4BridgeState(iTurb)%generatorPowerW
   bladePitchRad_c = OC4BridgeState(iTurb)%bladePitchRad
   towerTopDisplacement_c = OC4BridgeState(iTurb)%towerTopDisplacement
   controllerOk_c = OC4BridgeState(iTurb)%controllerOk

   ErrStat_c = ErrID_None
   ErrMsg_c = transfer(C_NULL_CHAR, ErrMsg_c)
end subroutine FAST_OC4_Platform_GetDiagnostics


subroutine OC4Bridge_EnsureState(iTurb, ErrStat, ErrMsg)
   implicit none
   integer(IntKi),       intent(in   ) :: iTurb
   integer(IntKi),       intent(  out) :: ErrStat
   character(*),         intent(  out) :: ErrMsg
   integer(IntKi)                      :: nStates
   type(OC4_PlatformBridgeStateType), allocatable :: tmp(:)

   ErrStat = ErrID_None
   ErrMsg = ''

   if (NumTurbines <= 0) then
      ErrStat = ErrID_Fatal
      ErrMsg = 'OC4 bridge state requested before FAST_AllocateTurbines.'
      return
   end if

   if (.not. allocated(OC4BridgeState)) then
      allocate(OC4BridgeState(NumTurbines), stat=ErrStat)
      if (ErrStat /= 0) then
         ErrStat = ErrID_Fatal
         ErrMsg = 'Unable to allocate OC4 bridge state.'
         return
      end if
      call OC4Bridge_ResetAllStates()
   else if (size(OC4BridgeState) < NumTurbines) then
      nStates = size(OC4BridgeState)
      allocate(tmp(NumTurbines), stat=ErrStat)
      if (ErrStat /= 0) then
         ErrStat = ErrID_Fatal
         ErrMsg = 'Unable to grow OC4 bridge state.'
         return
      end if
      tmp = OC4_PlatformBridgeStateType()
      tmp(1:nStates) = OC4BridgeState
      call move_alloc(tmp, OC4BridgeState)
   end if

   if (iTurb < 1 .or. iTurb > size(OC4BridgeState)) then
      ErrStat = ErrID_Fatal
      ErrMsg = 'Requested OC4 bridge turbine index is out of range.'
      return
   end if

   if (.not. OC4BridgeState(iTurb)%isActive) then
      OC4BridgeState(iTurb)%isActive = .true.
      OC4BridgeState(iTurb)%platformReferencePoint = (/ 0.0_C_DOUBLE, 0.0_C_DOUBLE, 0.54_C_DOUBLE /)
      OC4BridgeState(iTurb)%towerBasePoint = (/ 0.0_C_DOUBLE, 0.0_C_DOUBLE, 10.0_C_DOUBLE /)
      OC4BridgeState(iTurb)%applicationPoint = OC4BridgeState(iTurb)%towerBasePoint
      OC4BridgeState(iTurb)%openFoamToOpenFASTOffset = 0.0_C_DOUBLE
   end if
end subroutine OC4Bridge_EnsureState


subroutine OC4Bridge_ResetAllStates()
   implicit none
   integer(IntKi) :: i

   if (.not. allocated(OC4BridgeState)) then
      return
   end if

   do i = 1, size(OC4BridgeState)
      OC4BridgeState(i) = OC4_PlatformBridgeStateType()
   end do
end subroutine OC4Bridge_ResetAllStates


subroutine OC4Bridge_ApplyStoredPlatformMotion(iTurb, ErrStat, ErrMsg)
   implicit none
   integer(IntKi), intent(in   ) :: iTurb
   integer(IntKi), intent(  out) :: ErrStat
   character(*),   intent(  out) :: ErrMsg

   ErrStat = ErrID_None
   ErrMsg = ''

   ! Cluster-side implementation hook:
   !
   ! This routine should be called from FAST_CFD_Solution0 and FAST_CFD_UpdateStates
   ! after FAST_OC4_Platform_SetMotion has stored the imposed motion. The real
   ! implementation should mirror the WaveTank structural path:
   !
   ! 1. Populate the OpenFAST platform reference-point motion mesh from:
   !      OC4BridgeState(iTurb)%displacement
   !      OC4BridgeState(iTurb)%velocity
   !      OC4BridgeState(iTurb)%acceleration
   ! 2. Transfer PRP motion to tower motion.
   ! 3. Transfer PRP motion to hub motion.
   ! 4. Transfer hub motion to blade-root motion.
   !
   ! Relevant upstream references:
   !   glue-codes/labview/src/WaveTank_Struct.f90
   !     - StructMotionUpdate
   !     - MeshMapCreate(PRP->Tower, PRP->Hub, Hub->BladeRoot)
end subroutine OC4Bridge_ApplyStoredPlatformMotion


subroutine OC4Bridge_RefreshReactionLoadAndDiagnostics(iTurb, ErrStat, ErrMsg)
   implicit none
   integer(IntKi), intent(in   ) :: iTurb
   integer(IntKi), intent(  out) :: ErrStat
   character(*),   intent(  out) :: ErrMsg

   ErrStat = ErrID_None
   ErrMsg = ''

   ! Cluster-side implementation hook:
   !
   ! This routine should be called after the OpenFAST CFD/structural update has
   ! produced new loads. The real implementation should mirror the WaveTank load
   ! aggregation path:
   !
   ! 1. Pull blade-root loads and transfer them to the hub.
   ! 2. Transfer hub loads to the platform reference point.
   ! 3. If tower loads are available, transfer them to the same point.
   ! 4. Store one returned reaction wrench in:
   !      OC4BridgeState(iTurb)%applicationPoint
   !      OC4BridgeState(iTurb)%force
   !      OC4BridgeState(iTurb)%moment
   ! 5. Refresh diagnostics such as blade pitch, rotor speed, tower-top
   !    displacement, and controller status.
   !
   ! Relevant upstream references:
   !   glue-codes/labview/src/WaveTank_Struct.f90
   !     - StructLoadsMeshTransfer
   !   glue-codes/labview/src/WaveTank.f90
   !     - ADI_C_GetRotorLoads
   !     - CalcStepIO%FrcMom_C assignment
end subroutine OC4Bridge_RefreshReactionLoadAndDiagnostics
