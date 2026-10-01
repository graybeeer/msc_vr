#pragma once

#include "CoreMinimal.h"

/** Planar helpers. Heading is in radians; direction = (cos, sin) in world X/Y, UE centimetres. */
namespace AgvMath
{
	inline FVector2D Dir(double Heading) { return FVector2D(FMath::Cos(Heading), FMath::Sin(Heading)); }
	inline FVector2D Normal(double Heading) { return FVector2D(-FMath::Sin(Heading), FMath::Cos(Heading)); }
	inline double Wrap(double Angle) { return FMath::UnwindRadians(Angle); }

	/** Solves A x = B (3x3, partial pivoting). A and B are overwritten. False when A is singular. */
	inline bool Solve3(double A[3][3], double B[3], double X[3])
	{
		for (int32 Col = 0; Col < 3; ++Col)
		{
			int32 Pivot = Col;
			for (int32 Row = Col + 1; Row < 3; ++Row)
			{
				if (FMath::Abs(A[Row][Col]) > FMath::Abs(A[Pivot][Col]))
				{
					Pivot = Row;
				}
			}
			if (FMath::Abs(A[Pivot][Col]) < 1e-12)
			{
				return false;
			}
			for (int32 K = 0; K < 3; ++K)
			{
				Swap(A[Col][K], A[Pivot][K]);
			}
			Swap(B[Col], B[Pivot]);
			for (int32 Row = Col + 1; Row < 3; ++Row)
			{
				const double F = A[Row][Col] / A[Col][Col];
				for (int32 K = Col; K < 3; ++K)
				{
					A[Row][K] -= F * A[Col][K];
				}
				B[Row] -= F * B[Col];
			}
		}
		for (int32 Row = 2; Row >= 0; --Row)
		{
			double Sum = B[Row];
			for (int32 K = Row + 1; K < 3; ++K)
			{
				Sum -= A[Row][K] * X[K];
			}
			X[Row] = Sum / A[Row][Row];
		}
		return true;
	}
}

/** Straight line (Curvature == 0) or circular arc. Positive curvature increases the heading. */
struct FAgvSegment
{
	FVector2D Start = FVector2D::ZeroVector;
	double StartHeading = 0.0;
	double Length = 0.0;
	double Curvature = 0.0;

	void Sample(double S, FVector2D& OutPosition, double& OutHeading) const;
	/** Distance along the segment, within [0, Length], of the point closest to P. */
	double Project(const FVector2D& P) const;
};

struct FAgvPathSample
{
	FVector2D Position = FVector2D::ZeroVector;
	double Heading = 0.0;
	double Curvature = 0.0;
};

/** Tangent-continuous chain of segments that is driven without stopping. */
struct FAgvLeg
{
	TArray<FAgvSegment> Segments;
	TArray<double> Offsets;
	double Length = 0.0;
	/** The vehicle's local -X leads along the leg. */
	bool bReverse = false;

	void Add(const FAgvSegment& Segment);
	FAgvPathSample Sample(double S) const;
	/** Closest distance along the leg, searched only in [Hint - Back, Hint + Ahead]. */
	double Project(const FVector2D& P, double Hint, double Back, double Ahead) const;
};

/** Legs are joined by pivoting in place. */
struct FAgvPath
{
	TArray<FAgvLeg> Legs;
	bool bHasFinalYaw = false;
	/** Vehicle yaw (not motion heading) to hold after the last leg. */
	double FinalYaw = 0.0;

	/**
	 * Joins Points with lines and fillet arcs. CornerRadius[i] applies to interior vertex i;
	 * a radius <= 0, or one that cannot fit above MinRadius, becomes a pivot in place.
	 */
	static void Build(const TArray<FVector2D>& Points, const TArray<double>& CornerRadius, double MinRadius, bool bReverse, FAgvPath& Out);
};
