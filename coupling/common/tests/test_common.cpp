#include "coupling/Formatting.hpp"
#include "coupling/Math.hpp"

#include <cstdlib>
#include <iostream>

namespace {

using oc4::coupling::FrameId;
using oc4::coupling::Matrix3;
using oc4::coupling::ReferencePoint;
using oc4::coupling::Vector3;
using oc4::coupling::Wrench;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "Test failure: " << message << '\n';
        std::exit(1);
    }
}

void testRotationMatrixValidation()
{
    const Matrix3 identity = Matrix3::identity();
    require(oc4::coupling::isProperRotationMatrix(identity), "identity should be a valid rotation matrix");

    Matrix3 invalid = Matrix3::identity();
    invalid(0, 0) = 2.0;
    require(!oc4::coupling::isProperRotationMatrix(invalid), "scaled identity should be invalid");
}

void testVectorTransform()
{
    Matrix3 yaw90{};
    yaw90(0, 0) = 0.0;
    yaw90(0, 1) = -1.0;
    yaw90(0, 2) = 0.0;
    yaw90(1, 0) = 1.0;
    yaw90(1, 1) = 0.0;
    yaw90(1, 2) = 0.0;
    yaw90(2, 0) = 0.0;
    yaw90(2, 1) = 0.0;
    yaw90(2, 2) = 1.0;

    const Vector3 bodyX{1.0, 0.0, 0.0};
    const Vector3 globalX = oc4::coupling::bodyToGlobal(yaw90, bodyX);

    require(oc4::coupling::nearlyEqual(globalX, Vector3{0.0, 1.0, 0.0}, 1e-12),
            "body x should rotate to global y");

    const Vector3 recovered = oc4::coupling::globalToBody(yaw90, globalX);
    require(oc4::coupling::nearlyEqual(recovered, bodyX, 1e-12),
            "inverse transform should recover original vector");
}

void testWrenchShift()
{
    Wrench wrench{};
    wrench.force = {10.0, 0.0, 0.0};
    wrench.moment = {0.0, 0.0, 0.0};
    wrench.applicationPoint = {{0.0, 0.0, 1.0}, FrameId::global, "source"};
    wrench.expressedInFrame = FrameId::global;

    const ReferencePoint target{{0.0, 0.0, 0.0}, FrameId::global, "target"};
    const Wrench shifted = oc4::coupling::shiftWrenchReferencePoint(wrench, target);

    require(oc4::coupling::nearlyEqual(shifted.force, wrench.force, 1e-12),
            "force should not change when shifting reference point");
    require(oc4::coupling::nearlyEqual(shifted.moment, Vector3{0.0, 10.0, 0.0}, 1e-12),
            "moment should include lever-arm cross force");
}

void testFormatting()
{
    Wrench wrench{};
    wrench.force = {1.0, 2.0, 3.0};
    wrench.moment = {4.0, 5.0, 6.0};
    wrench.applicationPoint = {{0.0, 0.0, 0.54}, FrameId::global, "platformRef"};
    wrench.expressedInFrame = FrameId::global;

    const std::string text = oc4::coupling::toString(wrench);
    require(text.find("platformRef") != std::string::npos, "formatted wrench should contain reference-point name");
    require(text.find("global") != std::string::npos, "formatted wrench should contain frame name");
}

void testEulerMatrixRoundTrip()
{
    const Vector3 euler{0.2, -0.15, 0.35};
    const Matrix3 matrix = oc4::coupling::eulerXyzToMatrix(euler);

    require(oc4::coupling::isProperRotationMatrix(matrix),
            "eulerXyzToMatrix should produce a valid rotation matrix");

    const Vector3 recovered = oc4::coupling::matrixToEulerXyz(matrix);
    require(oc4::coupling::nearlyEqual(recovered, euler, 1e-12),
            "matrixToEulerXyz should invert eulerXyzToMatrix");
}

void testAngularVelocityToEulerRates()
{
    const Vector3 euler{0.1, -0.2, 0.3};
    const Vector3 expectedRates{0.4, -0.5, 0.6};

    const double roll = euler.x;
    const double pitch = euler.y;
    const double rollDot = expectedRates.x;
    const double pitchDot = expectedRates.y;
    const double yawDot = expectedRates.z;

    const double sinRoll = std::sin(roll);
    const double cosRoll = std::cos(roll);
    const double sinPitch = std::sin(pitch);
    const double cosPitch = std::cos(pitch);

    const Vector3 omegaGlobal{
        rollDot + yawDot * sinPitch,
        pitchDot * cosRoll - yawDot * sinRoll * cosPitch,
        pitchDot * sinRoll + yawDot * cosRoll * cosPitch,
    };

    const Vector3 recoveredRates =
        oc4::coupling::globalAngularVelocityToEulerXyzRates(euler, omegaGlobal);

    require(oc4::coupling::nearlyEqual(recoveredRates, expectedRates, 1e-12),
            "globalAngularVelocityToEulerXyzRates should invert the XYZ angular-velocity relation");
}

}  // namespace

int main()
{
    testRotationMatrixValidation();
    testVectorTransform();
    testWrenchShift();
    testFormatting();
    testEulerMatrixRoundTrip();
    testAngularVelocityToEulerRates();

    std::cout << "All coupling common tests passed.\n";
    return 0;
}
