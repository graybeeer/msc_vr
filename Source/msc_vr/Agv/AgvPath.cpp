#include "AgvPath.h"

void FAgvSegment::Sample(double S, FVector2D& OutPosition, double& OutHeading) const
{
	if (FMath::IsNearlyZero(Curvature))
	{
		OutHeading = StartHeading;
		OutPosition = Start + AgvMath::Dir(StartHeading) * S;
		return;
	}
	OutHeading = StartHeading + Curvature * S;
	OutPosition = Start + (AgvMath::Normal(StartHeading) - AgvMath::Normal(OutHeading)) / Curvature;
}

double FAgvSegment::Project(const FVector2D& P) const
{
	if (FMath::IsNearlyZero(Curvature))
	{
		return FMath::Clamp(FVector2D::DotProduct(P - Start, AgvMath::Dir(StartHeading)), 0.0, Length);
	}
	const FVector2D Centre = Start + AgvMath::Normal(StartHeading) / Curvature;
	const FVector2D Radial = P - Centre;
	const double Turn = Curvature > 0.0 ? 1.0 : -1.0;
	// A point on the arc at heading H lies at Centre - Normal(H) / Curvature.
	const double Heading = FMath::Atan2(Turn * Radial.X, -Turn * Radial.Y);
	double Swept = FMath::Fmod(Turn * (Heading - StartHeading), UE_DOUBLE_TWO_PI);
	if (Swept < 0.0)
	{
		Swept += UE_DOUBLE_TWO_PI;
	}
	const double S = Swept / FMath::Abs(Curvature);
	if (S <= Length)
	{
		return S;
	}
	FVector2D End;
	double EndHeading;
	Sample(Length, End, EndHeading);
	return FVector2D::DistSquared(P, Start) <= FVector2D::DistSquared(P, End) ? 0.0 : Length;
}

void FAgvLeg::Add(const FAgvSegment& Segment)
{
	Offsets.Add(Length);
	Segments.Add(Segment);
	Length += Segment.Length;
}

FAgvPathSample FAgvLeg::Sample(double S) const
{
	FAgvPathSample Result;
	if (Segments.IsEmpty())
	{
		return Result;
	}
	S = FMath::Clamp(S, 0.0, Length);
	int32 Index = Segments.Num() - 1;
	while (Index > 0 && Offsets[Index] > S)
	{
		--Index;
	}
	Segments[Index].Sample(S - Offsets[Index], Result.Position, Result.Heading);
	Result.Curvature = Segments[Index].Curvature;
	return Result;
}

double FAgvLeg::Project(const FVector2D& P, double Hint, double Back, double Ahead) const
{
	const double Low = FMath::Max(0.0, Hint - Back);
	const double High = FMath::Min(Length, Hint + Ahead);
	double Best = FMath::Clamp(Hint, Low, High);
	double BestDistance = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		const FAgvSegment& Segment = Segments[Index];
		if (Offsets[Index] + Segment.Length < Low || Offsets[Index] > High)
		{
			continue;
		}
		const double S = FMath::Clamp(Offsets[Index] + Segment.Project(P), Low, High);
		FVector2D Point;
		double Heading;
		Segment.Sample(S - Offsets[Index], Point, Heading);
		const double Distance = FVector2D::DistSquared(P, Point);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = S;
		}
	}
	return Best;
}

void FAgvPath::Build(const TArray<FVector2D>& Points, const TArray<double>& CornerRadius, double MinRadius, bool bReverse, FAgvPath& Out)
{
	Out.Legs.Reset();

	TArray<FVector2D> Vertices;
	TArray<double> Radii;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (Vertices.IsEmpty() || !Vertices.Last().Equals(Points[Index], 1.0))
		{
			Vertices.Add(Points[Index]);
			Radii.Add(CornerRadius.IsValidIndex(Index) ? CornerRadius[Index] : 0.0);
		}
	}
	if (Vertices.Num() < 2)
	{
		return;
	}

	FAgvLeg Leg;
	Leg.bReverse = bReverse;
	FVector2D Cursor = Vertices[0];
	auto LineTo = [&Leg, &Cursor](const FVector2D& Target)
	{
		const FVector2D Delta = Target - Cursor;
		if (Delta.Size() > 0.01)
		{
			FAgvSegment Line;
			Line.Start = Cursor;
			Line.StartHeading = FMath::Atan2(Delta.Y, Delta.X);
			Line.Length = Delta.Size();
			Leg.Add(Line);
		}
		Cursor = Target;
	};
	auto CloseLeg = [&Leg, &Out, bReverse]()
	{
		if (!Leg.Segments.IsEmpty())
		{
			Out.Legs.Add(Leg);
		}
		Leg = FAgvLeg();
		Leg.bReverse = bReverse;
	};

	const int32 Last = Vertices.Num() - 1;
	for (int32 Index = 1; Index < Last; ++Index)
	{
		const FVector2D In = (Vertices[Index] - Vertices[Index - 1]).GetSafeNormal();
		const FVector2D Exit = (Vertices[Index + 1] - Vertices[Index]).GetSafeNormal();
		const double Turn = FMath::Atan2(FVector2D::CrossProduct(In, Exit), FVector2D::DotProduct(In, Exit));
		if (FMath::Abs(Turn) < FMath::DegreesToRadians(0.5))
		{
			continue;
		}

		double Radius = Radii[Index];
		double Tangent = 0.0;
		bool bPivot = Radius <= 0.0 || FMath::Abs(Turn) > FMath::DegreesToRadians(170.0);
		if (!bPivot)
		{
			const double HalfTan = FMath::Tan(FMath::Abs(Turn) * 0.5);
			// Leave half of a shared edge for the next corner.
			const double ExitShare = Index + 1 == Last ? 1.0 : 0.5;
			const double Available = FMath::Min(FVector2D::Distance(Cursor, Vertices[Index]), FVector2D::Distance(Vertices[Index], Vertices[Index + 1]) * ExitShare);
			Tangent = Radius * HalfTan;
			if (Tangent > Available)
			{
				Tangent = Available;
				Radius = Available / HalfTan;
			}
			bPivot = Radius < MinRadius;
		}
		if (bPivot)
		{
			LineTo(Vertices[Index]);
			CloseLeg();
			continue;
		}

		LineTo(Vertices[Index] - In * Tangent);
		FAgvSegment Arc;
		Arc.Start = Cursor;
		Arc.StartHeading = FMath::Atan2(In.Y, In.X);
		Arc.Curvature = (Turn > 0.0 ? 1.0 : -1.0) / Radius;
		Arc.Length = Radius * FMath::Abs(Turn);
		Leg.Add(Arc);
		Cursor = Vertices[Index] + Exit * Tangent;
	}
	LineTo(Vertices[Last]);
	CloseLeg();
}
