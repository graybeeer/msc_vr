#pragma once

#include "CoreMinimal.h"

/** 2D map a vehicle localizes against: distance (cm) from each cell to the nearest obstacle, capped. */
struct FAgvDistanceMap
{
	FVector2D Origin = FVector2D::ZeroVector; // world position of cell (0, 0)'s corner
	double CellCm = 5.0;
	int32 Width = 0;
	int32 Height = 0;
	double CapCm = 50.0;
	int32 OccupiedCells = 0;
	TArray<float> Distance;

	/** Bilinear distance at a world point; CapCm outside the map. */
	double Sample(const FVector2D& Point) const;

	/** Fills Distance from an occupancy grid (Width x Height, row-major) with an exact Euclidean distance transform. */
	void BuildFromOccupancy(const TArray<bool>& Occupied);
};

/** Scan-to-map matching shared by the localizers. Points are 2D (cm) in the frame the correction maps from. */
namespace AgvScanMatch
{
	struct FParams
	{
		double VoxelCm = 10.0;
		double InlierDistanceCm = 10.0;
		/** Final width of the robust weight: points farther than this from the map have no pull. */
		double RobustScaleCm = 8.0;
		int32 Iterations = 20;
	};

	/** One point per voxel so dense nearby surfaces do not outweigh far ones. */
	void Voxelize(const TArray<FVector2D>& In, double VoxelCm, TArray<FVector2D>& Out);

	/**
	 * Gauss-Newton on the distance field from the correction (Tx, Ty, Th): map = Rotate(Th) * point + (Tx, Ty).
	 * Returns the share of points within InlierDistanceCm of a mapped surface after refinement.
	 */
	double Refine(const FAgvDistanceMap& Map, const TArray<FVector2D>& Points, const FParams& Params, double& Tx, double& Ty, double& Th);
}
