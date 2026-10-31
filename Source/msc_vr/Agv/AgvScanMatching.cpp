#include "AgvScanMatching.h"
#include "AgvPath.h"

namespace AgvScanMatch
{
	/** Exact 1D squared distance transform (Felzenszwalb & Huttenlocher) of F into D, in cell units. */
	static void DistanceTransform1D(const TArray<double>& F, TArray<double>& D)
	{
		const int32 N = F.Num();
		TArray<int32> V;
		TArray<double> Z;
		V.SetNum(N);
		Z.SetNum(N + 1);
		auto Intersect = [&F](int32 Q, int32 P) { return ((F[Q] + double(Q) * Q) - (F[P] + double(P) * P)) / (2.0 * Q - 2.0 * P); };
		int32 K = 0;
		V[0] = 0;
		Z[0] = -TNumericLimits<double>::Max();
		Z[1] = TNumericLimits<double>::Max();
		for (int32 Q = 1; Q < N; ++Q)
		{
			double S = Intersect(Q, V[K]);
			while (S <= Z[K])
			{
				--K;
				S = Intersect(Q, V[K]);
			}
			++K;
			V[K] = Q;
			Z[K] = S;
			Z[K + 1] = TNumericLimits<double>::Max();
		}
		D.SetNum(N);
		K = 0;
		for (int32 Q = 0; Q < N; ++Q)
		{
			while (Z[K + 1] < Q)
			{
				++K;
			}
			D[Q] = double(Q - V[K]) * (Q - V[K]) + F[V[K]];
		}
	}

	void Voxelize(const TArray<FVector2D>& In, double VoxelCm, TArray<FVector2D>& Out)
	{
		TMap<FIntPoint, FVector2D> Voxels;
		for (const FVector2D& Point : In)
		{
			Voxels.FindOrAdd(FIntPoint(FMath::FloorToInt32(Point.X / VoxelCm), FMath::FloorToInt32(Point.Y / VoxelCm)), Point);
		}
		Voxels.GenerateValueArray(Out);
	}

	double Refine(const FAgvDistanceMap& Map, const TArray<FVector2D>& Points, const FParams& Params, double& Tx, double& Ty, double& Th)
	{
		if (Points.IsEmpty())
		{
			return 0.0;
		}
		// Robust (redescending) weights: a point counts fully when it lies on a mapped surface and not at all once it is
		// RobustScaleCm or more off it, so unmapped things near the structure (cargo on a rack shelf, a person beside a
		// column, another vehicle) do not drag the estimate towards the nearest mapped surface. A plain capped/Huber
		// weight let every such point pull with a constant force: cargo on the racks biased the estimate by ~10 cm.
		// The scale starts wide (the cap: large errors, e.g. a wrong start pose or wheel spin on a slippery floor, still
		// converge) and halves each iteration down to RobustScaleCm (slower shrinking lets points near the structure, cargo
		// on a shelf, pull for longer: 13 cm error in the warehouse tour at 0.7).
		const double Step = Map.CellCm;
		double Scale = Map.CapCm;
		for (int32 Iteration = 0; Iteration < Params.Iterations; ++Iteration, Scale = FMath::Max(Params.RobustScaleCm, Scale * 0.5))
		{
			double H[3][3] = {}, G[3] = {};
			const double C = FMath::Cos(Th), S = FMath::Sin(Th);
			for (const FVector2D& P : Points)
			{
				const FVector2D Q(C * P.X - S * P.Y + Tx, S * P.X + C * P.Y + Ty);
				const double R = Map.Sample(Q);
				if (R >= Scale)
				{
					continue;
				}
				const double GX = (Map.Sample(Q + FVector2D(Step, 0)) - Map.Sample(Q - FVector2D(Step, 0))) / (2.0 * Step);
				const double GY = (Map.Sample(Q + FVector2D(0, Step)) - Map.Sample(Q - FVector2D(0, Step))) / (2.0 * Step);
				const double J[3] = { GX, GY, GX * (-S * P.X - C * P.Y) + GY * (C * P.X - S * P.Y) };
				const double W = FMath::Square(1.0 - FMath::Square(R / Scale)); // Tukey biweight
				for (int32 I = 0; I < 3; ++I)
				{
					G[I] += W * J[I] * R;
					for (int32 K = 0; K < 3; ++K)
					{
						H[I][K] += W * J[I] * J[K];
					}
				}
			}
			const double PriorWeights[3] = { Params.TranslationWeight, Params.TranslationWeight, Params.RotationWeight };
			const double PriorResidual[3] = { Tx - Params.PriorPosition.X, Ty - Params.PriorPosition.Y, AgvMath::Wrap(Th - Params.PriorYaw) };
			for (int32 I = 0; I < 3; ++I)
			{
				const double Weight = Points.Num() * PriorWeights[I];
				H[I][I] += Weight;
				G[I] += Weight * PriorResidual[I];
			}
			// Numerical damping is separate from the odometry prior above.
			for (int32 I = 0; I < 3; ++I)
			{
				H[I][I] += 1e-3 * (1.0 + H[I][I]);
				G[I] = -G[I];
			}
			double Delta[3];
			if (!AgvMath::Solve3(H, G, Delta))
			{
				break;
			}
			Tx += Delta[0];
			Ty += Delta[1];
			Th += Delta[2];
			if (Scale <= Params.RobustScaleCm && FMath::Abs(Delta[0]) + FMath::Abs(Delta[1]) < 0.01 && FMath::Abs(Delta[2]) < 1e-5)
			{
				break;
			}
		}
		int32 Inliers = 0;
		const double C = FMath::Cos(Th), S = FMath::Sin(Th);
		for (const FVector2D& P : Points)
		{
			Inliers += Map.Sample(FVector2D(C * P.X - S * P.Y + Tx, S * P.X + C * P.Y + Ty)) <= Params.InlierDistanceCm;
		}
		return (double)Inliers / Points.Num();
	}

	bool AcceptInnovation(const FVector2D& Delta, double DeltaYaw, double PositionSigmaCm, double YawSigmaRad)
	{
		if (Delta.ContainsNaN() || !FMath::IsFinite(DeltaYaw) || !FMath::IsFinite(PositionSigmaCm)
			|| !FMath::IsFinite(YawSigmaRad) || PositionSigmaCm <= 0.0 || YawSigmaRad <= 0.0)
		{
			return false;
		}
		return Delta.SizeSquared() / FMath::Square(PositionSigmaCm)
			+ FMath::Square(AgvMath::Wrap(DeltaYaw) / YawSigmaRad) <= 16.0;
	}
}

double FAgvDistanceMap::Sample(const FVector2D& Point) const
{
	// Cell centres sit at half-cell offsets.
	const double GX = (Point.X - Origin.X) / CellCm - 0.5;
	const double GY = (Point.Y - Origin.Y) / CellCm - 0.5;
	const int32 X0 = FMath::FloorToInt32(GX);
	const int32 Y0 = FMath::FloorToInt32(GY);
	if (X0 < 0 || Y0 < 0 || X0 + 1 >= Width || Y0 + 1 >= Height)
	{
		return CapCm;
	}
	const double FX = GX - X0, FY = GY - Y0;
	const float* Row0 = &Distance[Y0 * Width + X0];
	const float* Row1 = Row0 + Width;
	return FMath::Lerp(FMath::Lerp((double)Row0[0], (double)Row0[1], FX), FMath::Lerp((double)Row1[0], (double)Row1[1], FX), FY);
}

void FAgvDistanceMap::BuildFromOccupancy(const TArray<bool>& Occupied)
{
	const int32 Count = Width * Height;
	check(Occupied.Num() == Count);
	// Exact Euclidean distance transform: columns then rows.
	const double Far = 1e12;
	TArray<double> Squared;
	Squared.SetNum(Count);
	TArray<double> F, D;
	F.SetNum(Height);
	for (int32 X = 0; X < Width; ++X)
	{
		for (int32 Y = 0; Y < Height; ++Y)
		{
			F[Y] = Occupied[Y * Width + X] ? 0.0 : Far;
		}
		AgvScanMatch::DistanceTransform1D(F, D);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			Squared[Y * Width + X] = D[Y];
		}
	}
	F.SetNum(Width);
	Distance.SetNum(Count);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			F[X] = Squared[Y * Width + X];
		}
		AgvScanMatch::DistanceTransform1D(F, D);
		for (int32 X = 0; X < Width; ++X)
		{
			// Measured to the occupied cell's edge rather than its centre, so a point on a surface reads ~0.
			Distance[Y * Width + X] = (float)FMath::Clamp(FMath::Sqrt(D[X]) * CellCm - CellCm * 0.5, 0.0, CapCm);
		}
	}
	OccupiedCells = Occupied.FilterByPredicate([](bool B) { return B; }).Num();
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgvLocalizationPriorTest, "Warehouse.Localization.MotionPrior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAgvLocalizationPriorTest::RunTest(const FString& Parameters)
{
	// A single long wall constrains its normal, but cannot reveal travel along it.
	FAgvDistanceMap Map;
	Map.Origin = FVector2D(-500, -1000);
	Map.Width = 200; Map.Height = 400;
	TArray<bool> Occupied;
	Occupied.Init(false, Map.Width * Map.Height);
	for (int32 Y = 0; Y < Map.Height; ++Y) Occupied[Y * Map.Width + 100] = true;
	Map.BuildFromOccupancy(Occupied);
	TArray<FVector2D> Points;
	for (int32 Y = -600; Y <= 600; Y += 10) Points.Add(FVector2D(2.5, Y));
	AgvScanMatch::FParams Params;
	Params.PriorPosition = FVector2D(12, 40);
	Params.TranslationWeight = 0.04;
	Params.RotationWeight = 10000.0;
	double X = 12, Y = 40, Yaw = 0;
	const double Ratio = AgvScanMatch::Refine(Map, Points, Params, X, Y, Yaw);
	TestTrue(TEXT("Wall normal is corrected"), FMath::Abs(X) < 6.0 && Ratio > 0.95);
	TestTrue(TEXT("Unobservable along-wall travel retains the prediction"), FMath::Abs(Y - 40) < 0.01);
	// Translating the entire known map cannot change a local-frame solution.
	Map.Origin += FVector2D(100000, -200000);
	Params.PriorPosition += FVector2D(100000, -200000);
	double FarX = 100012, FarY = -199960, FarYaw = 0;
	AgvScanMatch::Refine(Map, Points, Params, FarX, FarY, FarYaw);
	TestTrue(TEXT("Fit independent of global map origin"), FMath::Abs(FarX - 100000 - X) < 0.001
		&& FMath::Abs(FarY + 200000 - Y) < 0.001 && FMath::Abs(FarYaw - Yaw) < 1e-6);
	TestFalse(TEXT("85cm jump contradicts a recent stationary prediction"), AgvScanMatch::AcceptInnovation(FVector2D(0, 85), 0, 6, 0.01));
	TestTrue(TEXT("Legitimate larger recovery with grown uncertainty is allowed"), AgvScanMatch::AcceptInnovation(FVector2D(35, 0), 0, 15, 0.01));
	TestTrue(TEXT("Yaw comparison wraps across pi"), AgvScanMatch::AcceptInnovation(FVector2D::ZeroVector, 2 * PI - 0.001, 6, 0.01));
	TestFalse(TEXT("Invalid sensor estimate is rejected"), AgvScanMatch::AcceptInnovation(FVector2D::ZeroVector, std::numeric_limits<double>::infinity(), 6, 0.01));
	return true;
}
#endif
