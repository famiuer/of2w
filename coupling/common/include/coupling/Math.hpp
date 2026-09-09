#pragma once

#include "coupling/Types.hpp"

#include <cmath>
#include <stdexcept>

namespace oc4::coupling {

inline Vector3 operator+(const Vector3& a, const Vector3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vector3 operator-(const Vector3& a, const Vector3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vector3 operator*(double scalar, const Vector3& v)
{
    return {scalar * v.x, scalar * v.y, scalar * v.z};
}

inline Vector3 operator*(const Vector3& v, double scalar)
{
    return scalar * v;
}

inline double dot(const Vector3& a, const Vector3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vector3 cross(const Vector3& a, const Vector3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

inline double norm(const Vector3& v)
{
    return std::sqrt(dot(v, v));
}

inline bool nearlyEqual(double a, double b, double tolerance = 1e-12)
{
    return std::abs(a - b) <= tolerance;
}

inline bool nearlyEqual(const Vector3& a, const Vector3& b, double tolerance = 1e-12)
{
    return nearlyEqual(a.x, b.x, tolerance) &&
           nearlyEqual(a.y, b.y, tolerance) &&
           nearlyEqual(a.z, b.z, tolerance);
}

inline Matrix3 transpose(const Matrix3& matrix)
{
    Matrix3 result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            result(row, col) = matrix(col, row);
        }
    }
    return result;
}

inline Vector3 multiply(const Matrix3& matrix, const Vector3& vector)
{
    return {
        matrix(0, 0) * vector.x + matrix(0, 1) * vector.y + matrix(0, 2) * vector.z,
        matrix(1, 0) * vector.x + matrix(1, 1) * vector.y + matrix(1, 2) * vector.z,
        matrix(2, 0) * vector.x + matrix(2, 1) * vector.y + matrix(2, 2) * vector.z,
    };
}

inline bool isProperRotationMatrix(const Matrix3& matrix, double tolerance = 1e-10)
{
    const Matrix3 matrixT = transpose(matrix);

    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            double entry = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                entry += matrix(row, k) * matrixT(k, col);
            }

            const double expected = (row == col) ? 1.0 : 0.0;
            if (!nearlyEqual(entry, expected, tolerance)) {
                return false;
            }
        }
    }

    const Vector3 c0{matrix(0, 0), matrix(1, 0), matrix(2, 0)};
    const Vector3 c1{matrix(0, 1), matrix(1, 1), matrix(2, 1)};
    const Vector3 c2{matrix(0, 2), matrix(1, 2), matrix(2, 2)};
    const double determinant = dot(c0, cross(c1, c2));

    return nearlyEqual(determinant, 1.0, tolerance);
}

inline Wrench shiftWrenchReferencePoint(
    const Wrench& input,
    const ReferencePoint& targetPoint)
{
    Wrench result = input;
    const Vector3 leverArm = input.applicationPoint.position - targetPoint.position;
    result.moment = input.moment + cross(leverArm, input.force);
    result.applicationPoint = targetPoint;
    return result;
}

inline Vector3 globalToBody(const Matrix3& orientationGlobalFromBody, const Vector3& vectorGlobal)
{
    return multiply(transpose(orientationGlobalFromBody), vectorGlobal);
}

inline Vector3 bodyToGlobal(const Matrix3& orientationGlobalFromBody, const Vector3& vectorBody)
{
    return multiply(orientationGlobalFromBody, vectorBody);
}

inline Matrix3 eulerXyzToMatrix(const Vector3& eulerAnglesRad)
{
    const double cr = std::cos(eulerAnglesRad.x);
    const double sr = std::sin(eulerAnglesRad.x);
    const double cp = std::cos(eulerAnglesRad.y);
    const double sp = std::sin(eulerAnglesRad.y);
    const double cy = std::cos(eulerAnglesRad.z);
    const double sy = std::sin(eulerAnglesRad.z);

    Matrix3 matrix{};
    matrix(0, 0) = cp * cy;
    matrix(0, 1) = -cp * sy;
    matrix(0, 2) = sp;
    matrix(1, 0) = cr * sy + sr * sp * cy;
    matrix(1, 1) = cr * cy - sr * sp * sy;
    matrix(1, 2) = -sr * cp;
    matrix(2, 0) = sr * sy - cr * sp * cy;
    matrix(2, 1) = sr * cy + cr * sp * sy;
    matrix(2, 2) = cr * cp;
    return matrix;
}

inline Vector3 matrixToEulerXyz(const Matrix3& orientationGlobalFromBody, double singularityTolerance = 1e-10)
{
    const double sinPitch = orientationGlobalFromBody(0, 2);
    const double clampedSinPitch = std::max(-1.0, std::min(1.0, sinPitch));
    const double pitch = std::asin(clampedSinPitch);
    const double cosPitch = std::cos(pitch);

    if (std::abs(cosPitch) <= singularityTolerance) {
        throw std::runtime_error(
            "matrixToEulerXyz encountered a near-gimbal-lock orientation");
    }

    const double roll = std::atan2(
        -orientationGlobalFromBody(1, 2),
        orientationGlobalFromBody(2, 2));
    const double yaw = std::atan2(
        -orientationGlobalFromBody(0, 1),
        orientationGlobalFromBody(0, 0));

    return {roll, pitch, yaw};
}

inline Vector3 globalAngularVelocityToEulerXyzRates(
    const Vector3& eulerAnglesRad,
    const Vector3& angularVelocityGlobal,
    double singularityTolerance = 1e-10)
{
    const double roll = eulerAnglesRad.x;
    const double pitch = eulerAnglesRad.y;
    const double cosPitch = std::cos(pitch);

    if (std::abs(cosPitch) <= singularityTolerance) {
        throw std::runtime_error(
            "globalAngularVelocityToEulerXyzRates encountered a near-gimbal-lock orientation");
    }

    const double sinRoll = std::sin(roll);
    const double cosRoll = std::cos(roll);
    const double tanPitch = std::tan(pitch);

    const double rollDot =
        angularVelocityGlobal.x
        + tanPitch * (sinRoll * angularVelocityGlobal.y - cosRoll * angularVelocityGlobal.z);
    const double pitchDot =
        cosRoll * angularVelocityGlobal.y + sinRoll * angularVelocityGlobal.z;
    const double yawDot =
        (-sinRoll * angularVelocityGlobal.y + cosRoll * angularVelocityGlobal.z) / cosPitch;

    return {rollDot, pitchDot, yawDot};
}

}  // namespace oc4::coupling
