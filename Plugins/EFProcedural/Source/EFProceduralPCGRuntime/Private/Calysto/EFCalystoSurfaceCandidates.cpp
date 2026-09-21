#include "Calysto/EFCalystoSurfaceCandidates.h"
#include "Calysto/EFCalystoBakedArchitecture.h"
#include "Calysto/EFCalystoContentCollisionContract.h"

#include "AI/Navigation/NavQueryFilter.h"
#include "Algo/Reverse.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Math/RotationMatrix.h"
#include "Misc/SecureHash.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PCGDataAsset.h"

namespace EFCalystoSurfaceCandidatesPrivate
{
	constexpr double PlaneTolerance = 0.05; // Centimetres; geometry remains inset, never enlarged by this tolerance.
	// Original native Forge feet sit 0.193752cm above the exported construction origin.
	// Permit at most 2.5mm of authored attachment gap; never move the mesh, bridge a
	// floor hole or loosen the independent 0.5mm collision-penetration/plane checks.
	constexpr double AttachmentContactTolerance = 0.25;
	constexpr double Separation = 0.25;
	/** Native surface origins may be inside their owning wall/floor thickness, but they must still
	 * project onto the verified collision plane. This is never used to enlarge a footprint. */
	constexpr double NativeOpportunityPlaneTolerance = 100.0;
	constexpr int32 MaximumDecalCoverageFaces = 64;
	constexpr int32 MaximumDecalCoveragePieces = 256;
	/** Room ownership is queried once for every complete floor-lattice point. A
	 * linear walk of every native room makes a normal 286-room floor exhaust the
	 * finite work budget before it can issue a single collision query. This is a
	 * conservative local-space index: every room whose 2D bounds can intersect a
	 * query is retained, and RoomFor still applies the existing exact overlap and
	 * boundary rules. The index therefore changes work, never feasibility. */
	constexpr double RoomIndexCellSizeCm = 2000.0;
	constexpr int32 MaximumRoomIndexReferences = 65536;
	constexpr int32 MaximumRoomIndexQueryCells = 4096;
	bool Finite(const FVector& V) { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); }
	bool ValidBox(const FBox& B) { return B.IsValid && Finite(B.Min) && Finite(B.Max) && B.Min.X <= B.Max.X && B.Min.Y <= B.Max.Y && B.Min.Z <= B.Max.Z; }
	bool RoomIndexCoordinate(double Coordinate, int32& Result)
	{
		if (!FMath::IsFinite(Coordinate)) return false;
		const double Cell = FMath::FloorToDouble(Coordinate / RoomIndexCellSizeCm);
		if (Cell < double(TNumericLimits<int32>::Min()) || Cell > double(TNumericLimits<int32>::Max())) return false;
		Result = int32(Cell); return true;
	}
	bool GuidLess(const FGuid& A, const FGuid& B) { return A.ToString(EGuidFormats::Digits) < B.ToString(EGuidFormats::Digits); }
	bool Corner(EEFCalystoPlacementZone Zone)
	{ return Zone == EEFCalystoPlacementZone::CornerBottom || Zone == EEFCalystoPlacementZone::CornerMiddle || Zone == EEFCalystoPlacementZone::CornerTop; }
	bool DecalSurface(EEFCalystoPlacementZone Zone, EEFCalystoDecalSurface& Surface)
	{
		if (Zone == EEFCalystoPlacementZone::Floor) { Surface = EEFCalystoDecalSurface::Floor; return true; }
		if (Zone == EEFCalystoPlacementZone::Roof) { Surface = EEFCalystoDecalSurface::Roof; return true; }
		if (Zone == EEFCalystoPlacementZone::WallBottom || Zone == EEFCalystoPlacementZone::WallMiddle || Zone == EEFCalystoPlacementZone::WallTop)
		{ Surface = EEFCalystoDecalSurface::Wall; return true; }
		return false;
	}
	FString VectorKey(const FVector& V) { return FString::Printf(TEXT("%.17g,%.17g,%.17g"), V.X == 0 ? 0 : V.X, V.Y == 0 ? 0 : V.Y, V.Z == 0 ? 0 : V.Z); }
	FGuid Identity(const FString& Key)
	{ FGuid Id; FGuid::ParseExact(FMD5::HashAnsiString(*Key), EGuidFormats::Digits, Id); return Id; }
	TArray<FVector> Corners(const FBox& B)
	{
		TArray<FVector> Result; Result.Reserve(8);
		for (int32 I = 0; I < 8; ++I) Result.Emplace(I & 1 ? B.Max.X : B.Min.X, I & 2 ? B.Max.Y : B.Min.Y, I & 4 ? B.Max.Z : B.Min.Z);
		return Result;
	}
	// Native cooked unions can retain collinear boundary vertices. The first three
	// vertices therefore do not necessarily span their plane, even on valid geometry.
	FVector PlaneNormal(const TArray<FVector>& Vertices)
	{
		if (Vertices.Num() < 3) return FVector::ZeroVector;
		FVector Edge = FVector::ZeroVector, Area = FVector::ZeroVector;
		for (const auto& V : Vertices)
		{
			if (!Finite(V)) return FVector::ZeroVector;
			const FVector D = V - Vertices[0]; if (D.SizeSquared() > Edge.SizeSquared()) Edge = D;
		}
		for (const auto& V : Vertices)
		{
			const FVector C = FVector::CrossProduct(Edge, V - Vertices[0]);
			if (C.SizeSquared() > Area.SizeSquared()) Area = C;
		}
		return Area.GetSafeNormal();
	}
	// Ordered convex polygons only. Half-space tests prove a complete projected rectangle, not sampled support.
	bool Contains(const TArray<FVector>& Polygon, const FVector& Normal, const FVector& Point, double Inset)
	{
		if (Polygon.Num() < 3) return false;
		for (int32 I = 0; I < Polygon.Num(); ++I)
		{
			const FVector Edge = Polygon[(I + 1) % Polygon.Num()] - Polygon[I];
			if (FVector::DotProduct(FVector::CrossProduct(Edge, Point - Polygon[I]), Normal) < Inset * Edge.Size()) return false;
		}
		return true;
	}
	struct FFace
	{
		FEFCalystoSurfaceSupportReceipt Support;
		EEFCalystoStructuralRole Role = EEFCalystoStructuralRole::OtherStructure;
		TArray<FVector> Vertices;
		FBox Bounds = FBox(ForceInit);
	};
	// Convex clipping/subtraction proves coverage of every point in the projected
	// envelope. A union's bounding box or a handful of support rays cannot prove it.
	using FPolygon = TArray<FVector>;
	double PolygonArea(const FPolygon& P, const FVector& N)
	{
		double Area = 0;
		for (int32 I = 1; I + 1 < P.Num(); ++I)
			Area += FVector::DotProduct(FVector::CrossProduct(P[I] - P[0], P[I + 1] - P[0]), N);
		return FMath::Abs(Area) * 0.5;
	}
	FPolygon ClipPolygon(const FPolygon& P, const FVector& Origin, const FVector& Inward, bool Inside)
	{
		FPolygon Result; if (P.IsEmpty()) return Result;
		FVector A = P.Last(); double DA = FVector::DotProduct(A - Origin, Inward) * (Inside ? 1 : -1);
		for (const FVector& B : P)
		{
			const double DB = FVector::DotProduct(B - Origin, Inward) * (Inside ? 1 : -1);
			if ((DA >= 0) != (DB >= 0)) Result.Add(A + (B - A) * (DA / (DA - DB)));
			if (DB >= 0) Result.Add(B);
			A = B; DA = DB;
		}
		return Result;
	}
	FPolygon ProjectedEnvelope(const FBox& Envelope, const FVector& Origin, const FVector& N)
	{
		FVector A, B; N.FindBestAxisVectors(A, B); B = FVector::CrossProduct(N, A);
		TArray<FVector2D> Points;
		for (const FVector& V : Corners(Envelope)) Points.AddUnique(FVector2D(FVector::DotProduct(V - Origin, A), FVector::DotProduct(V - Origin, B)));
		Points.Sort([](const auto& X, const auto& Y) { return X.X == Y.X ? X.Y < Y.Y : X.X < Y.X; });
		const auto Turn = [](const FVector2D& X, const FVector2D& Y, const FVector2D& Z)
		{ return (Y.X - X.X) * (Z.Y - X.Y) - (Y.Y - X.Y) * (Z.X - X.X); };
		TArray<FVector2D> Hull;
		for (const auto& P : Points) { while (Hull.Num() > 1 && Turn(Hull[Hull.Num() - 2], Hull.Last(), P) <= 0) Hull.Pop(EAllowShrinking::No); Hull.Add(P); }
		const int32 Lower = Hull.Num();
		for (int32 I = Points.Num() - 2; I >= 0; --I)
		{ while (Hull.Num() > Lower && Turn(Hull[Hull.Num() - 2], Hull.Last(), Points[I]) <= 0) Hull.Pop(EAllowShrinking::No); Hull.Add(Points[I]); }
		if (!Hull.IsEmpty()) Hull.Pop(EAllowShrinking::No);
		FPolygon Result; for (const auto& P : Hull) Result.Add(Origin + A * P.X + B * P.Y); return Result;
	}
	struct FContract
	{
		FEFCalystoPlacement Placement;
		FEFCalystoContentCollisionContract Collision;
		/** The attempt's leased ActorClass owns this CDO for the synchronous
		 * feasibility pass. A weak pointer may report a valid CDO as stale during
		 * isolated automation worlds, which would turn a real contract into a false
		 * configuration failure. */
		AActor* CollisionTemplate = nullptr;
		FString Key;
		TSet<FGuid> Entries;
		TSet<EEFCalystoGameplayRole> Roles;
	};
	struct FIndexedRoom
	{
		int64 Id = 0;
		FBox LocalBounds = FBox(ForceInit);
	};
	struct FNavPolygon { uint64 Id = 0; TArray<FVector> Vertices; FBox Bounds = FBox(ForceInit); };
	struct FCookedPatch { TArray<FVector> Vertices; FVector Normal = FVector::ZeroVector; };
	struct FMeshGeometry
	{
		UStaticMesh* Mesh = nullptr;
		FBox Bounds = FBox(ForceInit);
		TArray<FVector> CollisionVertices;
	};
	struct FArchitectureChild { FEFCalystoBakedMesh Definition; TSharedPtr<FMeshGeometry> Geometry; };
	struct FArchitecturePayload { TArray<FArchitectureChild> Children; };
	struct FAssembler
	{
		const FEFCalystoSurfaceCandidateRequest& Input;
		FEFCalystoSurfaceCandidateResult& Output;
		const FEFCalystoStructuralNavigationInput& NI;
		const FEFCalystoStructuralNavigationResult& Nav;
		UWorld& World;
		const FEFCalystoStyle& Style;
		TArray<FFace> Faces;
		TArray<FBox> DoorBounds;
		TMap<int64, TArray<FContract>> Contracts;
		TArray<FNavPolygon> Reachable;
		TMap<FGuid, FString> CandidateKeys;
		TMap<FGuid, FString> DecalKeys;
		TSet<int64> DecalNativeOpportunityIds;
		TArray<FIndexedRoom> IndexedRooms;
		TMap<FIntPoint, TArray<int32>> RoomIndex;
		TMap<FSoftObjectPath, UObject*> ArchitectureResources;
		TMap<UStaticMesh*, TSharedPtr<FMeshGeometry>> MeshGeometry;
		TMap<FGuid, FArchitecturePayload> ArchitecturePayloads;
		int32 CompatiblePairs = 0;
		double NavigationHeightTolerance = 0;
		bool bFailed = false;
		bool Fail(FName Code, const FString& Message, EEFCalystoAttemptFailure Kind = EEFCalystoAttemptFailure::Configuration, bool bPending = false)
		{
			if (!bFailed) { Output.FailureCode = Code; Output.Message = Message; Output.Failure = Kind; Output.bPending = bPending; }
			bFailed = true; return false;
		}
		bool Work(int64 Count = 1)
		{
			if (bFailed) return false;
			Output.WorkUnits += Count;
			if (Output.WorkUnits > Input.Limits.MaximumWorkUnits) return Fail(TEXT("SurfaceWorkBound"), TEXT("Complete candidate feasibility exceeded its finite work bound; no partial catalog was accepted."));
			if (FPlatformTime::Seconds() >= NI.RequestDeadlineSeconds) return Fail(TEXT("SurfaceDeadline"), TEXT("The unchanged floor deadline expired during candidate feasibility."), EEFCalystoAttemptFailure::Deadline);
			return true;
		}
		bool Query()
		{
			if (!Work()) return false;
			if (++Output.PhysicsQueries > Input.Limits.MaximumPhysicsQueries) return Fail(TEXT("SurfacePhysicsBound"), TEXT("Candidate feasibility exceeded the explicit physics-query capacity."));
			return true;
		}
		bool AddFace(const FEFCalystoSurfaceSupportReceipt& Support, EEFCalystoStructuralRole Role, TArray<FVector> Vertices, const FVector& Interior)
		{
			if (!Work(3 * Vertices.Num())) return false;
			if (Vertices.Num() < 3 || Vertices.Num() > 64) return Fail(TEXT("UnsupportedSupportFace"), TEXT("A simple collision face exceeds the bounded convex-plane contract."));
			FVector Normal = PlaneNormal(Vertices);
			if (!Finite(Normal) || Normal.IsNearlyZero()) return Fail(TEXT("UnsupportedSupportFace"), TEXT("Simple collision contains a degenerate face."));
			if (FVector::DotProduct(Normal, Vertices[0] - Interior) < 0) Normal *= -1;
			// This first consumer supports horizontal floors/roofs and upright walls, not ramp content.
			if ((Role == EEFCalystoStructuralRole::Floor && Normal.Z < 0.999999)
				|| (Role == EEFCalystoStructuralRole::Roof && Normal.Z > -0.999999)
				|| (Role == EEFCalystoStructuralRole::Wall && FMath::Abs(Normal.Z) > 0.000001)) return true;
			FVector Center = FVector::ZeroVector; for (const auto& V : Vertices) Center += V; Center /= Vertices.Num();
			FVector A, B; Normal.FindBestAxisVectors(A, B);
			// FindBestAxisVectors returns A cross Normal. Use the right-handed basis
			// directly instead of inferring winding from a possibly collinear triple.
			B = FVector::CrossProduct(Normal, A);
			Vertices.Sort([&](const FVector& X, const FVector& Y)
			{ return FMath::Atan2(FVector::DotProduct(X - Center, B), FVector::DotProduct(X - Center, A)) < FMath::Atan2(FVector::DotProduct(Y - Center, B), FVector::DotProduct(Y - Center, A)); });
			for (const FVector& V : Vertices)
				if (!Finite(V) || FMath::Abs(FVector::DotProduct(V - Center, Normal)) > PlaneTolerance || !Contains(Vertices, Normal, V, -PlaneTolerance))
					return Fail(TEXT("UnsupportedSupportFace"), FString::Printf(
						TEXT("Collision face is not finite, planar and convex: mesh=%s instance=%d role=%d vertices=%d normal=%s deviation=%.9g transform=%s."),
						*GetPathNameSafe(Support.Component.IsValid() ? Support.Component->GetStaticMesh() : nullptr), Support.InstanceIndex,
						int32(Role), Vertices.Num(), *Normal.ToString(), FMath::Abs(FVector::DotProduct(V - Center, Normal)),
						*Support.InstanceTransform.ToHumanReadableString()));
			if (Faces.Num() >= Input.Limits.MaximumFaces) return Fail(TEXT("SurfaceFaceBound"), TEXT("Native collision face capacity exceeded; geometry was not truncated."));
			FFace& Face = Faces.AddDefaulted_GetRef(); Face.Support = Support; Face.Role = Role;
			Face.Support.Point = Center; Face.Support.Normal = Normal; Face.Vertices = MoveTemp(Vertices);
			for (const auto& V : Face.Vertices) Face.Bounds += V;
			return true;
		}
		bool CookedPatches(const UBodySetup& Body, TArray<FCookedPatch>& Patches)
		{
			if (Body.TriMeshGeometries.IsEmpty() || Body.TriMeshGeometries.Num() > 8)
				return Fail(TEXT("UnsupportedCookedSupport"), TEXT("Complex-as-simple support requires its actual resident bounded cooked triangle geometry; inactive boxes are never a fallback."));
			for (const auto& Geometry : Body.TriMeshGeometries)
			{
				if (!Geometry) return Fail(TEXT("UnsupportedCookedSupport"), TEXT("Cooked triangle collision is absent."));
				const auto& Particles = Geometry->Particles(); const auto& Elements = Geometry->Elements();
				const int32 Count = Elements.GetNumTriangles();
				if (Count < 1 || Count > 256 || Particles.Size() > 768)
					return Fail(TEXT("UnsupportedCookedSupport"), TEXT("Cooked support exceeds the narrow 256-triangle/768-vertex patch contract."));
				if (!Work(Count * 3)) return false;
				Output.CookedCollisionTriangles += Count;
				for (int32 I = 0; I < Count; ++I)
				{
					FCookedPatch Patch;
					for (int32 J = 0; J < 3; ++J)
					{
						const int32 Index = Elements.RequiresLargeIndices() ? Elements.GetLargeIndexBuffer()[I][J] : int32(Elements.GetSmallIndexBuffer()[I][J]);
						if (Index < 0 || Index >= int32(Particles.Size())) return Fail(TEXT("UnsupportedCookedSupport"), TEXT("Cooked collision triangle index is invalid."));
						const FVector V(Particles.GetX(Index)); if (!Finite(V)) return Fail(TEXT("UnsupportedCookedSupport"), TEXT("Cooked collision contains a nonfinite vertex."));
						Patch.Vertices.Add(V);
					}
					Patch.Normal = FVector::CrossProduct(Patch.Vertices[1] - Patch.Vertices[0], Patch.Vertices[2] - Patch.Vertices[0]).GetSafeNormal();
					if (Patch.Normal.IsNearlyZero()) return Fail(TEXT("UnsupportedCookedSupport"), TEXT("Degenerate cooked collision triangle cannot establish continuous support."));
					// Canonical winding and start vertex make patch assembly independent of cooked triangle order.
					const FVector N = Patch.Normal;
					if (N.Z < 0 || (N.Z == 0 && (N.Y < 0 || (N.Y == 0 && N.X < 0)))) { Algo::Reverse(Patch.Vertices); Patch.Normal *= -1; }
					int32 First = 0; for (int32 J = 1; J < 3; ++J) if (VectorKey(Patch.Vertices[J]) < VectorKey(Patch.Vertices[First])) First = J;
					const auto V = Patch.Vertices; for (int32 J = 0; J < 3; ++J) Patch.Vertices[J] = V[(First + J) % 3];
					Patches.Add(MoveTemp(Patch));
				}
			}
			Patches.Sort([](const FCookedPatch& A, const FCookedPatch& B)
			{
				for (int32 I = 0; I < 3; ++I) { const FString X = VectorKey(A.Vertices[I]), Y = VectorKey(B.Vertices[I]); if (X != Y) return X < Y; }
				return false;
			});
			// Merge only two convex patches sharing one complete oppositely directed edge.
			// Their interiors must lie on opposite sides of that edge and the resulting boundary
			// must remain convex. This is their exact union, never a hull over a hole or a seam.
			bool bMerged = true;
			while (bMerged)
			{
				bMerged = false;
				for (int32 I = 0; I < Patches.Num() && !bMerged; ++I) for (int32 J = I + 1; J < Patches.Num() && !bMerged; ++J)
				{
					if (!Work()) return false;
					const auto& A = Patches[I]; const auto& B = Patches[J];
					if (!A.Normal.Equals(B.Normal, 1e-8) || A.Vertices.Num() + B.Vertices.Num() - 2 > 64) continue;
					bool bCoplanar = true; for (const auto& V : B.Vertices) bCoplanar &= FMath::Abs(FVector::DotProduct(V - A.Vertices[0], A.Normal)) <= 1e-5;
					if (!bCoplanar) continue;
					for (int32 EA = 0; EA < A.Vertices.Num() && !bMerged; ++EA) for (int32 EB = 0; EB < B.Vertices.Num() && !bMerged; ++EB)
					{
						if (!Work()) return false;
						const FVector P = A.Vertices[EA], Q = A.Vertices[(EA + 1) % A.Vertices.Num()];
						if (P != B.Vertices[(EB + 1) % B.Vertices.Num()] || Q != B.Vertices[EB]) continue;
						bool bOpposite = true;
						for (const auto& V : A.Vertices) bOpposite &= FVector::DotProduct(FVector::CrossProduct(Q - P, V - P), A.Normal) >= 0;
						for (const auto& V : B.Vertices) bOpposite &= FVector::DotProduct(FVector::CrossProduct(Q - P, V - P), A.Normal) <= 0;
						if (!bOpposite) continue;
						TArray<FVector> Union;
						for (int32 K = 0; K < A.Vertices.Num(); ++K) Union.Add(A.Vertices[(EA + 1 + K) % A.Vertices.Num()]);
						for (int32 K = 0; K < B.Vertices.Num() - 2; ++K) Union.Add(B.Vertices[(EB + 2 + K) % B.Vertices.Num()]);
						if (!Work(Union.Num() * Union.Num())) return false;
						bool bConvex = true; for (const auto& V : Union) bConvex &= Contains(Union, A.Normal, V, 0);
						if (!bConvex) continue;
						Patches[I].Vertices = MoveTemp(Union); Patches.RemoveAt(J); bMerged = true;
					}
				}
			}
			Output.CookedConvexPatches += Patches.Num(); return true;
		}
		bool Geometry()
		{
			FEFCalystoStructuralEvidence Current; FString Error;
			if (NI.MaximumComponents < 1 || NI.MaximumComponents > 1024 || NI.MaximumInstances < 1 || NI.MaximumInstances > 100000
				|| !Work(NI.StructuralSources.Num() + Nav.Structure.TotalInstanceCount)) return Fail(TEXT("SurfaceStructureBound"), TEXT("Cached native geometry exceeds its bounded structural contract."));
			if (!FEFCalystoStructuralNavigation::CollectStructuralEvidence(NI.StructuralSources, NI.MaximumComponents, NI.MaximumInstances, Current, Error))
				return Fail(TEXT("SurfaceStructureChanged"), Error, EEFCalystoAttemptFailure::Spatial);
			if (Current.ObservationFingerprint != Nav.Structure.ObservationFingerprint)
				return Fail(TEXT("SurfaceStructureChanged"), TEXT("Cached structural observation no longer matches actual registered collision."), EEFCalystoAttemptFailure::Spatial);
			if (Current.Components.Num() != Nav.Structure.Components.Num()) return Fail(TEXT("SurfaceStructureChanged"), TEXT("The frozen structural component count changed."), EEFCalystoAttemptFailure::Spatial);
			for (int32 I = 0; I < Current.Components.Num(); ++I)
			{
				const auto& A = Current.Components[I]; const auto& B = Nav.Structure.Components[I];
				if (!Work(A.WorldInstanceTransforms.Num())) return false;
				if (A.ComponentPath != B.ComponentPath || A.Mesh != B.Mesh || A.Role != B.Role || A.bRegistered != B.bRegistered
					|| A.bQueryCollision != B.bQueryCollision || A.bBlocksPawn != B.bBlocksPawn || A.bPhysicsStateCreated != B.bPhysicsStateCreated
					|| A.bNavigationRelevant != B.bNavigationRelevant || A.WorldInstanceTransforms.Num() != B.WorldInstanceTransforms.Num()
					|| A.NativeZeroHeightWallExtensionIndices != B.NativeZeroHeightWallExtensionIndices)
					return Fail(TEXT("SurfaceStructureChanged"), TEXT("Exact native structural bindings changed; a matching CRC alone is insufficient."), EEFCalystoAttemptFailure::Spatial);
				for (int32 J = 0; J < A.WorldInstanceTransforms.Num(); ++J)
					if (!A.WorldInstanceTransforms[J].Equals(B.WorldInstanceTransforms[J], 0)) return Fail(TEXT("SurfaceStructureChanged"), TEXT("An exact frozen native instance transform changed."), EEFCalystoAttemptFailure::Spatial);
			}
			TSet<FSoftObjectPath> DoorMeshes;
			for (const auto& D : Style.Architecture.Doorways) { DoorMeshes.Add(D.WallMesh.ToSoftObjectPath()); DoorMeshes.Add(D.FrameMesh.ToSoftObjectPath()); }
			for (const auto& D : Style.Architecture.DoorFrames) DoorMeshes.Add(D.Mesh.ToSoftObjectPath());
			for (const auto& Source : NI.StructuralSources)
			{
				if (!Work()) return false;
				UStaticMeshComponent* Component = Source.Component.Get();
				if (!Component || Component->GetWorld() != &World || !Component->GetStaticMesh()) return Fail(TEXT("SurfaceOwnerLost"), TEXT("An exact cached native structural component disappeared."), EEFCalystoAttemptFailure::Spatial);
				const bool bDoor = DoorMeshes.Contains(FSoftObjectPath(Component->GetStaticMesh()));
				const bool bSupportRole = Source.Role == EEFCalystoStructuralRole::Floor || Source.Role == EEFCalystoStructuralRole::Wall || Source.Role == EEFCalystoStructuralRole::Roof;
				if (!bSupportRole && !bDoor) continue;
				UBodySetup* Body = Component->GetBodySetup();
				const bool bCooked = bSupportRole && Body && Body->GetCollisionTraceFlag() == CTF_UseComplexAsSimple;
				TArray<FCookedPatch> Patches;
				if (bCooked && !CookedPatches(*Body, Patches)) return false;
				if (bSupportRole && !bCooked && (!Body || Body->AggGeom.BoxElems.Num() + Body->AggGeom.ConvexElems.Num() == 0
					|| Body->AggGeom.GetElementCount() != Body->AggGeom.BoxElems.Num() + Body->AggGeom.ConvexElems.Num()))
					return Fail(TEXT("UnsupportedSupportCollision"), TEXT("Candidate support requires actual simple box/convex collision; complex or curved support is not treated as an AABB."));
				auto* ISM = Cast<UInstancedStaticMeshComponent>(Component); const int32 Count = ISM ? ISM->GetInstanceCount() : 1;
				for (int32 Instance = 0; Instance < Count; ++Instance)
				{
					if (!Work()) return false;
					if (Source.NativeZeroHeightWallExtensionIndices.Contains(Instance)) continue;
					FTransform Transform = Component->GetComponentTransform();
					if (ISM && !ISM->GetInstanceTransform(Instance, Transform, true)) return Fail(TEXT("SurfaceOwnerLost"), TEXT("A cached native instance disappeared."), EEFCalystoAttemptFailure::Spatial);
					if (Transform.GetScale3D().GetMin() <= 0) return Fail(TEXT("UnsupportedSupportScale"), TEXT("Mirrored or degenerate collision is outside the supported planar contract."));
					if (bDoor) DoorBounds.Add(Component->GetStaticMesh()->GetBoundingBox().TransformBy(Transform));
					if (!bSupportRole) continue; // Frames provide protected bounds, not invented structural support.
					FEFCalystoSurfaceSupportReceipt Support; Support.Component = Component; Support.InstanceIndex = ISM ? Instance : INDEX_NONE; Support.InstanceTransform = Transform;
					if (bCooked)
					{
						for (const auto& Patch : Patches)
						{
							TArray<FVector> V; for (const auto& P : Patch.Vertices) V.Add(Transform.TransformPosition(P));
							if (!Work(2 * V.Num())) return false;
							const FVector Normal = PlaneNormal(V);
							// Geometry supplies both possible planes; actual component/instance traces decide
							// which side participates in query collision (including one-sided cooked triangles).
							if (!AddFace(Support, Source.Role, V, V[0] - Normal) || !AddFace(Support, Source.Role, V, V[0] + Normal)) return false;
						}
						continue;
					}
					for (const FKBoxElem& Box : Body->AggGeom.BoxElems)
					{
						if (!CollisionEnabledHasQuery(Box.GetCollisionEnabled())) continue;
						if (!Work(6)) return false;
						const FVector Scale = Transform.GetScale3D();
						if (!Box.Rotation.IsNearlyZero() && !Scale.Equals(FVector(Scale.X), UE_KINDA_SMALL_NUMBER))
							return Fail(TEXT("UnsupportedSupportScale"), TEXT("A rotated simple box under nonuniform instance scale requires unsupported shear interpretation."));
						const FVector Half(Box.X * 0.5, Box.Y * 0.5, Box.Z * 0.5);
						const FTransform Element = Box.GetTransform(); const FVector Interior = Transform.TransformPosition(Element.GetLocation());
						for (int32 Axis = 0; Axis < 3; ++Axis) for (int32 Sign : {-1, 1})
						{
							TArray<FVector> V;
							for (int32 J = 0; J < 4; ++J)
							{ FVector P; P[Axis] = Sign * Half[Axis]; P[(Axis + 1) % 3] = J & 1 ? Half[(Axis + 1) % 3] : -Half[(Axis + 1) % 3]; P[(Axis + 2) % 3] = J & 2 ? Half[(Axis + 2) % 3] : -Half[(Axis + 2) % 3]; V.Add(Transform.TransformPosition(Element.TransformPosition(P))); }
							if (!AddFace(Support, Source.Role, MoveTemp(V), Interior)) return false;
						}
					}
					for (const FKConvexElem& Convex : Body->AggGeom.ConvexElems)
					{
						if (!CollisionEnabledHasQuery(Convex.GetCollisionEnabled())) continue;
						if (Convex.VertexData.Num() < 4 || Convex.VertexData.Num() > 256 || Convex.IndexData.IsEmpty() || Convex.IndexData.Num() > 3072 || Convex.IndexData.Num() % 3)
							return Fail(TEXT("UnsupportedConvexTopology"), TEXT("Loaded convex collision must expose bounded face indices and vertices."));
						if (!Work(Convex.VertexData.Num() * int64(Convex.IndexData.Num()))) return false;
						TArray<FVector> Vertices; FVector Interior = FVector::ZeroVector;
						for (const FVector& V : Convex.VertexData) { const FVector P = Transform.TransformPosition(Convex.GetTransform().TransformPosition(V)); Vertices.Add(P); Interior += P; }
						Interior /= Vertices.Num(); TArray<FPlane> Planes;
						for (int32 I = 0; I < Convex.IndexData.Num(); I += 3)
						{
							const int32 A = Convex.IndexData[I], B = Convex.IndexData[I + 1], C = Convex.IndexData[I + 2];
							if (!Vertices.IsValidIndex(A) || !Vertices.IsValidIndex(B) || !Vertices.IsValidIndex(C)) return Fail(TEXT("UnsupportedConvexTopology"), TEXT("Convex collision face indices are invalid."));
							FVector N = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]).GetSafeNormal();
							if (N.IsNearlyZero()) continue;
							if (FVector::DotProduct(N, Vertices[A] - Interior) < 0) N *= -1;
							const FPlane Plane(Vertices[A], N); bool bSeen = false;
							for (const FPlane& Existing : Planes) if (FVector(Existing).Equals(N, 1e-6) && FMath::Abs(Existing.W - Plane.W) < PlaneTolerance) { bSeen = true; break; }
							if (bSeen) continue; Planes.Add(Plane);
							TArray<FVector> Face; for (const FVector& V : Vertices) if (FMath::Abs(Plane.PlaneDot(V)) <= PlaneTolerance) Face.Add(V);
							if (!AddFace(Support, Source.Role, MoveTemp(Face), Interior)) return false;
						}
					}
				}
			}
			return !Faces.IsEmpty() || Fail(TEXT("NoPlanarNativeSupport"), TEXT("The actual native structure has no supported planar content faces."), EEFCalystoAttemptFailure::Spatial);
		}
		bool Catalog()
		{
			TSet<int64> RoomIds; int32 PairCount = 0;
			IndexedRooms.Reset(); RoomIndex.Reset();
			for (const auto& Room : Input.Native->Rooms)
			{
				if (!Work()) return false;
				if (Room.RoomId <= 0 || RoomIds.Contains(Room.RoomId) || Room.StyleId != Style.Selection.Id || !ValidBox(Room.LocalBounds))
					return Fail(TEXT("SurfaceRoomIdentity"), TEXT("Native room identity, Style and dungeon-local bounds must be exact and unique."));
				RoomIds.Add(Room.RoomId); IndexedRooms.Add({Room.RoomId, Room.LocalBounds}); auto& ContentRoom = Output.Rooms.AddDefaulted_GetRef();
				ContentRoom.RoomId = Room.RoomId; ContentRoom.ThemeId = Room.ThemeId; ContentRoom.Protection = Room.Protection;
				TArray<FEFCalystoResolvedContentGroup> Groups; FString Error;
				if (!Input.Configuration->ResolveContent(Style.Selection.Id, Room.ThemeId, Groups, Error)) return Fail(TEXT("SurfaceContentResolution"), Error);
				auto& RoomContracts = Contracts.Add(Room.RoomId);
				for (const auto& Group : Groups)
				{
					ContentRoom.AllowedRoles.Add(Group.Content.Role);
					if (Group.Content.Role == EEFCalystoGameplayRole::ContainerContent) continue;
					for (const auto& Entry : Group.Content.Entries)
					{
						if (!Work()) return false;
						if (!Entry.Selection.bEnabled || Entry.Selection.Weight <= 0
							|| !FEFCalystoDirectorProbability::IsEligible(Entry.Selection, Input.RoomConfiguration->FloorNumber, {})) continue;
						if (++PairCount > Input.Limits.MaximumCompatiblePairs) return Fail(TEXT("SurfaceCatalogBound"), TEXT("Resolved room entry contracts exceed the finite compatibility capacity."));
						const auto& P = Entry.Placement;
						UClass* const ActorClass = Cast<UClass>(Entry.ActorClass.ToSoftObjectPath().ResolveObject());
						if (!ActorClass)
							return Fail(TEXT("ContentCollisionClassUnresolved"), TEXT("An eligible content ActorClass was not resident before collision feasibility: ") + Entry.ActorClass.ToSoftObjectPath().ToString(), EEFCalystoAttemptFailure::Resource);
						FEFCalystoContentCollisionContract Collision;
						if (!FEFCalystoContentCollisionContracts::Build(ActorClass, Collision, Error))
							return Fail(TEXT("ContentCollisionContractInvalid"), Error, EEFCalystoAttemptFailure::Configuration);
						const FString Key = FString::Printf(TEXT("%u|%s|%.17g|%.17g|%u|%s"), uint8(P.Zone), *VectorKey(P.FootprintHalfExtent), P.Clearance, P.PositionVariationCm, P.bRequiresNavigation, *Collision.Hash);
						FContract* Contract = RoomContracts.FindByPredicate([&](const FContract& C) { return C.Key == Key; });
						if (!Contract)
						{
							AActor* const Template = ActorClass->GetDefaultObject<AActor>();
							if (!IsValid(Template))
								return Fail(TEXT("ContentCollisionTemplateUnavailable"), TEXT("The resident content ActorClass has no CDO template for exact spawn feasibility."), EEFCalystoAttemptFailure::Resource);
							Contract = &RoomContracts.AddDefaulted_GetRef(); Contract->Key = Key; Contract->Placement = P;
							Contract->Collision = MoveTemp(Collision); Contract->CollisionTemplate = Template;
						}
						Contract->Entries.Add(Entry.Selection.Id); Contract->Roles.Add(Group.Content.Role);
					}
				}
				RoomContracts.Sort([](const FContract& A, const FContract& B) { return A.Key < B.Key; });
			}
			IndexedRooms.Sort([](const FIndexedRoom& A, const FIndexedRoom& B) { return A.Id < B.Id; });
			int32 ReferenceCount = 0;
			for (int32 RoomIndexIndex = 0; RoomIndexIndex < IndexedRooms.Num(); ++RoomIndexIndex)
			{
				const FBox& Bounds = IndexedRooms[RoomIndexIndex].LocalBounds;
				int32 MinimumX = 0, MaximumX = 0, MinimumY = 0, MaximumY = 0;
				if (!RoomIndexCoordinate(Bounds.Min.X, MinimumX) || !RoomIndexCoordinate(Bounds.Max.X, MaximumX)
					|| !RoomIndexCoordinate(Bounds.Min.Y, MinimumY) || !RoomIndexCoordinate(Bounds.Max.Y, MaximumY))
					return Fail(TEXT("SurfaceRoomIndexCoordinate"), TEXT("Native room bounds cannot be represented by the finite local-space ownership index."));
				const int64 XCount = int64(MaximumX) - int64(MinimumX) + 1;
				const int64 YCount = int64(MaximumY) - int64(MinimumY) + 1;
				if (XCount < 1 || YCount < 1 || XCount > MaximumRoomIndexReferences || YCount > MaximumRoomIndexReferences
					|| XCount > (MaximumRoomIndexReferences - ReferenceCount) / YCount)
					return Fail(TEXT("SurfaceRoomIndexBound"), TEXT("Native room bounds exceed the finite complete ownership-index capacity; no room was omitted."));
				const int32 References = int32(XCount * YCount);
				if (!Work(References)) return false;
				for (int64 X = MinimumX; X <= MaximumX; ++X) for (int64 Y = MinimumY; Y <= MaximumY; ++Y)
					RoomIndex.FindOrAdd(FIntPoint(int32(X), int32(Y))).Add(RoomIndexIndex);
				ReferenceCount += References;
			}
			Output.Rooms.Sort([](const auto& A, const auto& B) { return A.RoomId < B.RoomId; });
			return true;
		}
		bool Navigation()
		{
			bool bRequired = false; for (const auto& Room : Contracts) for (const auto& C : Room.Value) bRequired |= C.Placement.bRequiresNavigation;
			UNavigationSystemV1* System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
			if (System && (UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(&World) || System->HasDirtyAreasQueued()))
				return Fail(TEXT("SurfaceNavigationPending"), TEXT("Actual navigation build, lock or queued dirty areas require another bounded observation before compatibility."), EEFCalystoAttemptFailure::Spatial, true);
			if (!bRequired) return true;
			if (!System || !Nav.StartNavigationNode)
				return Fail(TEXT("SurfacePlayerNavigationMissing"), TEXT("Required Player navigation lacks its system authority or accepted start polygon."));
			ANavigationData* Data = System->GetNavDataForProps(NI.AgentProperties, Nav.ValidatedEntryTransform.GetLocation());
			if (!Data) return Fail(TEXT("SurfacePlayerNavigationLost"), TEXT("The previously accepted Player navigation data is no longer registered."), EEFCalystoAttemptFailure::Spatial);
			ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Data);
			if (!Recast || !FMath::IsNearlyEqual(Recast->GetConfig().AgentRadius, NI.AgentProperties.AgentRadius)
				|| !FMath::IsNearlyEqual(Recast->GetConfig().AgentHeight, NI.AgentProperties.AgentHeight))
				return Fail(TEXT("SurfacePlayerNavigationAuthorityInvalid"), TEXT("Required Player navigation must use supported Recast authority with the exact capsule agent."));
			if (Recast->GetPathName() != Nav.NavigationDataPath)
				return Fail(TEXT("SurfaceNavigationChanged"), TEXT("The previously accepted Player navigation data has been replaced."), EEFCalystoAttemptFailure::Spatial);
			const auto Filter = Recast->GetDefaultQueryFilter(); if (!Filter.IsValid()) return Fail(TEXT("SurfaceNavigationFilterMissing"), TEXT("Player navigation lacks its real default query filter."));
			NavigationHeightTolerance = 2.0 * FMath::Max3(Recast->GetCellHeight(ENavigationDataResolution::Low),
				Recast->GetCellHeight(ENavigationDataResolution::Default), Recast->GetCellHeight(ENavigationDataResolution::High)) + PlaneTolerance;
			float Costs[RECAST_MAX_AREAS], Fixed[RECAST_MAX_AREAS]; Filter->GetAllAreaCosts(Costs, Fixed, RECAST_MAX_AREAS);
			TArray<NavNodeRef> Queue{Nav.StartNavigationNode}; TSet<NavNodeRef> Seen{Nav.StartNavigationNode};
			for (int32 Index = 0; Index < Queue.Num(); ++Index)
			{
				if (!Work()) return false;
				if (Queue.Num() > Input.Limits.MaximumNavigationPolygons) return Fail(TEXT("SurfaceNavigationBound"), TEXT("Player polygon reachability exceeded its explicit finite capacity."));
				const NavNodeRef Id = Queue[Index]; FNavMeshNodeFlags Flags; uint16 PolyFlags = 0, AreaFlags = 0;
				if (!Recast->GetPolyFlags(Id, Flags) || !Recast->GetPolyFlags(Id, PolyFlags, AreaFlags)) return Fail(TEXT("SurfaceNavigationChanged"), TEXT("Accepted Player navigation polygons changed during feasibility."), EEFCalystoAttemptFailure::Spatial);
				if (Flags.IsNavLink() || Flags.Area >= RECAST_MAX_AREAS || !(PolyFlags & Filter->GetIncludeFlags()) || (PolyFlags & Filter->GetExcludeFlags())
					|| !FMath::IsFinite(Costs[Flags.Area]) || Costs[Flags.Area] >= TNumericLimits<float>::Max()
					|| !FMath::IsFinite(Fixed[Flags.Area]) || Fixed[Flags.Area] >= TNumericLimits<float>::Max()) continue;
				FNavPolygon Poly; Poly.Id = Id;
				if (!Recast->GetPolyVerts(Id, Poly.Vertices) || Poly.Vertices.Num() < 3 || Poly.Vertices.Num() > 12) return Fail(TEXT("SurfaceNavigationChanged"), TEXT("Player navigation polygon geometry is unavailable or unsupported."), EEFCalystoAttemptFailure::Spatial);
				for (const auto& V : Poly.Vertices) { if (!Finite(V)) return Fail(TEXT("SurfaceNavigationChanged"), TEXT("Nonfinite navigation polygon."), EEFCalystoAttemptFailure::Spatial); Poly.Bounds += V; }
				if (FVector::CrossProduct(Poly.Vertices[1] - Poly.Vertices[0], Poly.Vertices[2] - Poly.Vertices[0]).Z < 0) Algo::Reverse(Poly.Vertices);
				Reachable.Add(MoveTemp(Poly)); TArray<NavNodeRef> Neighbors;
				if (!Recast->GetPolyNeighbors(Id, Neighbors)) return Fail(TEXT("SurfaceNavigationChanged"), TEXT("Player polygon adjacency is unavailable."), EEFCalystoAttemptFailure::Spatial);
				if (!Work(Neighbors.Num())) return false;
				for (NavNodeRef Neighbor : Neighbors) if (!Seen.Contains(Neighbor)) { Seen.Add(Neighbor); Queue.Add(Neighbor); }
			}
			Output.NavigationPolygons = Reachable.Num();
			Reachable.Sort([](const FNavPolygon& A, const FNavPolygon& B) { return A.Id < B.Id; });
			return !Reachable.IsEmpty() || Fail(TEXT("SurfaceNavigationUnreachable"), TEXT("Accepted Player start polygon has no traversable ordinary polygon closure."), EEFCalystoAttemptFailure::Spatial);
		}
		int64 RoomFor(const FBox& Bounds)
		{
			FBox LocalBounds(ForceInit);
			for (const auto& P : Corners(Bounds)) LocalBounds += Input.RoomConfiguration->DungeonTransform.InverseTransformPosition(P);
			if (!ValidBox(LocalBounds)) { Fail(TEXT("SurfaceRoomIndexCoordinate"), TEXT("A candidate ownership envelope has invalid local-space bounds.")); return 0; }
			int32 MinimumX = 0, MaximumX = 0, MinimumY = 0, MaximumY = 0;
			if (!RoomIndexCoordinate(LocalBounds.Min.X, MinimumX) || !RoomIndexCoordinate(LocalBounds.Max.X, MaximumX)
				|| !RoomIndexCoordinate(LocalBounds.Min.Y, MinimumY) || !RoomIndexCoordinate(LocalBounds.Max.Y, MaximumY))
			{ Fail(TEXT("SurfaceRoomIndexCoordinate"), TEXT("A candidate ownership envelope cannot be represented by the finite local-space index.")); return 0; }
			const int64 XCount = int64(MaximumX) - int64(MinimumX) + 1;
			const int64 YCount = int64(MaximumY) - int64(MinimumY) + 1;
			if (XCount < 1 || YCount < 1 || XCount > MaximumRoomIndexQueryCells || YCount > MaximumRoomIndexQueryCells
				|| XCount > MaximumRoomIndexQueryCells / YCount)
			{ Fail(TEXT("SurfaceRoomIndexQueryBound"), TEXT("A candidate ownership envelope exceeds the finite complete room-index query capacity.")); return 0; }
			TSet<int32> CandidateIndices;
			for (int64 X = MinimumX; X <= MaximumX; ++X) for (int64 Y = MinimumY; Y <= MaximumY; ++Y)
			{
				if (!Work()) return 0;
				if (const TArray<int32>* Bucket = RoomIndex.Find(FIntPoint(int32(X), int32(Y))))
					for (const int32 Index : *Bucket) CandidateIndices.Add(Index);
			}
			int64 Owner = 0;
			for (const int32 Index : CandidateIndices)
			{
				if (!Work()) return 0;
				const FIndexedRoom& Room = IndexedRooms[Index]; const FBox& B = Room.LocalBounds;
				if (LocalBounds.Max.X < B.Min.X || LocalBounds.Min.X > B.Max.X || LocalBounds.Max.Y < B.Min.Y || LocalBounds.Min.Y > B.Max.Y) continue;
				// Partial overlap with another room is ambiguous too. Merely fitting completely in one
				// room does not authorize the part of a swept envelope shared by a second room.
				if (Owner || LocalBounds.Min.X <= B.Min.X || LocalBounds.Max.X >= B.Max.X || LocalBounds.Min.Y <= B.Min.Y || LocalBounds.Max.Y >= B.Max.Y) return 0;
				Owner = Room.Id;
			}
			return Owner;
		}
		bool Protected(const FBox& Bounds)
		{
			const FVector Player(NI.CapsuleRadius, NI.CapsuleRadius, NI.CapsuleHalfHeight);
			for (const auto& Marker : {Input.Native->NativeStartMarkers[0], Input.Native->NativeEndMarkers[0]})
			{
				if (!Work()) return true;
				const FBox Actual = Marker->GetComponentsBoundingBox(true);
				const FBox MarkerBounds = ValidBox(Actual) ? Actual : FBox(Marker->GetActorLocation(), Marker->GetActorLocation());
				if (Bounds.Intersect(MarkerBounds.ExpandBy(Player))) return true;
			}
			const FVector Entry = Nav.ValidatedEntryTransform.GetLocation(), End = Nav.ValidatedEndApproach + FVector(0, 0, NI.CapsuleHalfHeight);
			if (Bounds.Intersect(FBox(Entry - Player, Entry + Player)) || Bounds.Intersect(FBox(End - Player, End + Player))) return true;
			for (const auto& D : DoorBounds) { if (!Work()) return true; if (Bounds.Intersect(D.ExpandBy(Player))) return true; }
			const FBox Corridor = Bounds.ExpandBy(Player);
			for (int32 I = 1; I < Nav.CompleteRoute.Num(); ++I)
			{
				if (!Work()) return true;
				const FVector A = Nav.CompleteRoute[I - 1] + FVector(0, 0, NI.CapsuleHalfHeight), B = Nav.CompleteRoute[I] + FVector(0, 0, NI.CapsuleHalfHeight);
				if (Corridor.IsInsideOrOn(A) || Corridor.IsInsideOrOn(B) || FMath::LineBoxIntersection(Corridor, A, B, B - A)) return true;
			}
			return false;
		}
		bool Supports(const FFace& Face, const FBox& Envelope, double Inset = Separation)
		{
			if (!Work(Face.Vertices.Num() * 8)) return false;
			for (const auto& V : Corners(Envelope))
			{
				const FVector Projected = V - Face.Support.Normal * FVector::DotProduct(V - Face.Support.Point, Face.Support.Normal);
				if (!Contains(Face.Vertices, Face.Support.Normal, Projected, Inset)) return false;
			}
			return true;
		}
		bool TraceSupport(const FFace& Face, const FVector& Point)
		{
			if (!Query()) return false;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CalystoCandidateSupport), false); if (NI.ProtectedPlayer.IsValid()) Params.AddIgnoredActor(NI.ProtectedPlayer.Get());
			FHitResult Hit; const FVector N = Face.Support.Normal;
			return World.LineTraceSingleByChannel(Hit, Point + N * 2, Point - N * 2, ECC_Pawn, Params)
				&& Hit.GetComponent() == Face.Support.Component.Get() && (Face.Support.InstanceIndex == INDEX_NONE || Hit.Item == Face.Support.InstanceIndex)
				&& Hit.ImpactPoint.Equals(Point, PlaneTolerance) && FVector::DotProduct(Hit.ImpactNormal, N) > 0.999;
		}
		/** Match the pooled-decal contract: a support face must be owned by the native structure and
		 * must block the same Visibility trace that the pool will recheck on realization. */
		bool TraceDecalSupport(const FFace& Face, const FVector& Point)
		{
			if (!Query()) return false;
			UStaticMeshComponent* Component = Face.Support.Component.Get();
			if (!IsValid(Component) || !Component->IsRegistered() || !Component->IsQueryCollisionEnabled()
				|| Component->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block) return false;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CalystoDecalCandidateSupport), false);
			if (NI.ProtectedPlayer.IsValid()) Params.AddIgnoredActor(NI.ProtectedPlayer.Get());
			FHitResult Hit; const FVector N = Face.Support.Normal;
			return World.LineTraceSingleByChannel(Hit, Point + N * 2, Point - N * 2, ECC_Visibility, Params)
				&& Hit.GetComponent() == Component && (Face.Support.InstanceIndex == INDEX_NONE || Hit.Item == Face.Support.InstanceIndex)
				&& Hit.ImpactPoint.Equals(Point, PlaneTolerance) && FVector::DotProduct(Hit.ImpactNormal, N) > 0.999;
		}
		/** Proves an exact planar decal rectangle is covered by the union of real collision faces.
		 * A projected native transform, a support AABB, or a center trace is deliberately insufficient. */
		bool DecalCoverage(const FFace& Plane, const FPolygon& Footprint, TArray<FEFCalystoSurfaceSupportReceipt>& SupportsOut)
		{
			const FVector N = Plane.Support.Normal;
			constexpr double ZeroArea = 1e-8; // Square centimetres; never fills a physical gap.
			if (Footprint.Num() != 4 || PolygonArea(Footprint, N) <= ZeroArea) return false;
			TArray<const FFace*> Coverage;
			TArray<FVector> Witnesses;
			for (const FFace& Face : Faces)
			{
				if (!Work()) return false;
				if (Face.Role != Plane.Role || !Face.Support.Normal.Equals(N, 1e-8)
					|| FMath::Abs(FVector::DotProduct(Face.Support.Point - Plane.Support.Point, N)) > 1e-5) continue;
				FPolygon Intersection = Footprint;
				for (int32 Index = 0; Index < Face.Vertices.Num() && !Intersection.IsEmpty(); ++Index)
				{
					if (!Work(Intersection.Num())) return false;
					const FVector Inward = FVector::CrossProduct(N, Face.Vertices[(Index + 1) % Face.Vertices.Num()] - Face.Vertices[Index]);
					Intersection = ClipPolygon(Intersection, Face.Vertices[Index], Inward, true);
				}
				if (PolygonArea(Intersection, N) <= ZeroArea) continue;
				if (Coverage.Num() >= MaximumDecalCoverageFaces)
					return Fail(TEXT("DecalSupportBound"), TEXT("One decal footprint exceeds 64 actual contributing collision faces; no support was truncated."));
				Coverage.Add(&Face);
				FVector Witness = FVector::ZeroVector;
				for (const FVector& Vertex : Intersection) Witness += Vertex;
				Witnesses.Add(Witness / Intersection.Num());
			}
			TArray<FPolygon> Remaining{Footprint};
			for (const FFace* Face : Coverage)
			{
				TArray<FPolygon> Next;
				for (FPolygon Piece : Remaining)
				{
					for (int32 Index = 0; Index < Face->Vertices.Num() && !Piece.IsEmpty(); ++Index)
					{
						if (!Work(2 * Piece.Num())) return false;
						const FVector Inward = FVector::CrossProduct(N, Face->Vertices[(Index + 1) % Face->Vertices.Num()] - Face->Vertices[Index]);
						auto Outside = ClipPolygon(Piece, Face->Vertices[Index], Inward, false);
						if (PolygonArea(Outside, N) > ZeroArea) Next.Add(MoveTemp(Outside));
						if (Next.Num() > MaximumDecalCoveragePieces)
							return Fail(TEXT("DecalSupportBound"), TEXT("Exact decal support subtraction exceeds 256 remaining convex pieces; incomplete coverage was not accepted."));
						Piece = ClipPolygon(Piece, Face->Vertices[Index], Inward, true);
					}
				}
				Remaining = MoveTemp(Next);
				if (Remaining.IsEmpty()) break;
			}
			if (!Remaining.IsEmpty() || Coverage.IsEmpty()) return false;
			for (int32 Index = 0; Index < Coverage.Num(); ++Index)
			{
				if (!TraceDecalSupport(*Coverage[Index], Witnesses[Index])) return false;
				auto Receipt = Coverage[Index]->Support;
				Receipt.Point = Witnesses[Index];
				SupportsOut.Add(MoveTemp(Receipt));
			}
			return true;
		}
		bool Candidate(int64 RoomId, const FContract& Contract, const FVector& Anchor, const FQuat& Rotation, const FFace& Face, const FFace* Second = nullptr)
		{
			if (!Work()) return false;
			const auto& P = Contract.Placement; const FVector N = Face.Support.Normal;
			const auto RejectGeometry = [&](FName Reason)
			{
				if (!Output.FirstCandidateRejectionByZone.Contains(P.Zone)) Output.FirstCandidateRejectionByZone.Add(P.Zone, Reason);
				++Output.RejectedGeometry;
			};
			FVector TangentA, TangentB; N.FindBestAxisVectors(TangentA, TangentB);
			FBox LocalBounds(-P.FootprintHalfExtent, P.FootprintHalfExtent);
			if (Contract.Collision.bHasPrimitiveBounds) LocalBounds += Contract.Collision.LocalPrimitiveBounds;
			else if (Contract.Collision.bHasQueryCollision) LocalBounds += Contract.Collision.LocalRootBounds;
			const FBox RotatedBounds = LocalBounds.TransformBy(FTransform(Rotation));
			const auto MinimumProjection = [](const FBox& Bounds, const FVector& Normal)
			{
				double Result = TNumericLimits<double>::Max();
				for (const FVector& Corner : Corners(Bounds)) Result = FMath::Min(Result, double(FVector::DotProduct(Corner, Normal)));
				return Result;
			};
			FVector Center = Anchor + N * (P.Clearance + Separation - MinimumProjection(RotatedBounds, N));
			if (Second)
			{
				const FVector N2 = Second->Support.Normal;
				const double CurrentMinimum = FVector::DotProduct(Center, N2) + MinimumProjection(RotatedBounds, N2);
				Center += N2 * (FVector::DotProduct(Second->Support.Point, N2) + P.Clearance + Separation - CurrentMinimum);
			}
			const FBox ReservedBounds = RotatedBounds.ShiftBy(Center).ExpandBy(P.Clearance);
			const FVector JitterHalf = (TangentA.GetAbs() + TangentB.GetAbs()) * P.PositionVariationCm;
			const FBox Envelope(ReservedBounds.Min - JitterHalf, ReservedBounds.Max + JitterHalf);
			if (RoomFor(Envelope) != RoomId) { RejectGeometry(TEXT("RoomOwnership")); return !bFailed; }
			if (!Supports(Face, Envelope)) { RejectGeometry(TEXT("PrimarySupport")); return !bFailed; }
			if (Second && !Supports(*Second, Envelope)) { RejectGeometry(TEXT("SecondarySupport")); return !bFailed; }
			if (Protected(Envelope)) { ++Output.RejectedProtection; return !bFailed; }
			const FVector SupportPoint = Center - N * FVector::DotProduct(Center - Face.Support.Point, N);
			if (!TraceSupport(Face, SupportPoint)) { RejectGeometry(TEXT("PrimaryTrace")); return !bFailed; }
			if (Second)
			{
				const FVector N2 = Second->Support.Normal, Point2 = Center - N2 * FVector::DotProduct(Center - Second->Support.Point, N2);
				if (!TraceSupport(*Second, Point2)) { RejectGeometry(TEXT("SecondaryTrace")); return !bFailed; }
			}
			if (!Query()) return false;
			// This is the exact engine precheck used by SpawnActorDeferred with
			// DontSpawnIfColliding. It includes a movement component's updated
			// primitive and query-collision children, which a generic AABB cannot.
			const AActor* const Template = Contract.CollisionTemplate;
			const USceneComponent* const Root = Template ? Template->GetRootComponent() : nullptr;
			if (!IsValid(Template) || (Root && !IsValid(Root)))
				return Fail(TEXT("ContentCollisionTemplateChanged"), TEXT("The preloaded content CDO collision template was released before candidate feasibility."), EEFCalystoAttemptFailure::Resource);
			const FTransform UserTransform(Rotation, Center);
			// Match UE 5.8 LevelActor.cpp exactly: a rootless native actor uses the
			// supplied user transform directly for DontSpawnIfColliding preflight.
			const FTransform FinalRootTransform = Root
				? FTransform(Root->GetRelativeRotation(), Root->GetRelativeLocation(), Root->GetRelativeScale3D()) * UserTransform
				: UserTransform;
			if (World.EncroachingBlockingGeometry(Template, FinalRootTransform.GetLocation(), FinalRootTransform.Rotator())) { RejectGeometry(TEXT("SpawnEncroachment")); return true; }
			uint64 Node = 0;
			if (P.bRequiresNavigation)
			{
				// Navigation is a floor standing contract. Wall/ceiling attachment does not masquerade as walkable support.
				if (P.Zone == EEFCalystoPlacementZone::Floor) for (const auto& Poly : Reachable)
				{
					if (!Work(Poly.Vertices.Num() * 4)) return false;
					if (FMath::Abs(Poly.Bounds.GetCenter().Z - SupportPoint.Z) > NavigationHeightTolerance || Poly.Bounds.GetSize().Z > PlaneTolerance) continue;
					bool bInside = true; for (int32 I = 0; I < 4; ++I)
						bInside &= Contains(Poly.Vertices, FVector::UpVector, FVector(I & 1 ? Envelope.Max.X : Envelope.Min.X, I & 2 ? Envelope.Max.Y : Envelope.Min.Y, Poly.Bounds.GetCenter().Z), Separation);
					if (bInside) { Node = Poly.Id; break; }
				}
				if (!Node) { ++Output.RejectedNavigation; return true; }
			}
			const FVector Euler = Rotation.Euler();
			const FString Key = FString::Printf(TEXT("%lld|%s|%s|%s|%s"), RoomId, *Contract.Key, *VectorKey(Center), *VectorKey(Euler), *VectorKey(N));
			const FGuid Id = Identity(Key);
			if (const FString* Existing = CandidateKeys.Find(Id))
			{ return *Existing == Key || Fail(TEXT("SurfaceIdentityCollision"), TEXT("Distinct geometry produced a repeated canonical candidate identity.")); }
			const bool bCandidateCapacityExceeded = Output.Surfaces.Num() >= Input.Limits.MaximumCandidates;
			const bool bCompatibilityCapacityExceeded = CompatiblePairs + Contract.Entries.Num() > Input.Limits.MaximumCompatiblePairs;
			if (bCandidateCapacityExceeded || bCompatibilityCapacityExceeded)
				return Fail(TEXT("SurfaceCandidateBound"), FString::Printf(
					TEXT("The complete feasible set exceeds declared candidate/entry capacity; no truncation was accepted. surfaces=%d/%d compatible_pairs=%d/%d next_entries=%d floor_grid_spacing_cm=%.3f candidate_capacity_exceeded=%d compatibility_capacity_exceeded=%d."),
					Output.Surfaces.Num(), Input.Limits.MaximumCandidates, CompatiblePairs, Input.Limits.MaximumCompatiblePairs,
					Contract.Entries.Num(), Input.Limits.FloorGridSpacingCm, bCandidateCapacityExceeded, bCompatibilityCapacityExceeded));
			CandidateKeys.Add(Id, Key); CompatiblePairs += Contract.Entries.Num();
			auto& Surface = Output.Surfaces.AddDefaulted_GetRef(); Surface.Id = Id; Surface.RoomId = RoomId; Surface.Zone = P.Zone;
			Surface.Transform = FTransform(Rotation, Center); Surface.Normal = N; Surface.AvailableClearanceCm = P.Clearance;
			FBox Local(ForceInit); for (const auto& V : Corners(Envelope)) Local += Surface.Transform.InverseTransformPosition(V);
			Surface.AvailableHalfExtent = Local.GetExtent(); Surface.AllowedRoles = Contract.Roles; Surface.CompatibleEntryIds = Contract.Entries;
			Surface.bCollisionValidated = true; Surface.bCollisionContractValidated = true; Surface.ReservedLocalBounds = LocalBounds.ExpandBy(P.Clearance);
			Surface.CollisionContractHash = Contract.Collision.Hash; Surface.bNavigationValidated = Node != 0;
			auto& Receipt = Output.Receipts.AddDefaulted_GetRef(); Receipt.CandidateId = Id; Receipt.SweptWorldBounds = Envelope;
			Receipt.Supports.Add(Face.Support); if (Second) Receipt.Supports.Add(Second->Support); Receipt.NavigationPolygon = Node;
			return true;
		}
		TSharedPtr<FMeshGeometry> LoadedGeometry(UStaticMesh* Mesh)
		{
			if (const auto* Found = MeshGeometry.Find(Mesh)) return *Found;
			auto G = MakeShared<FMeshGeometry>(); G->Mesh = Mesh;
			if (!IsValid(Mesh))
			{ Fail(TEXT("ArchitectureMeshUnavailable"), TEXT("The required resident architecture mesh is no longer available."), EEFCalystoAttemptFailure::Resource); return nullptr; }
			const UBodySetup* Body = Mesh->GetBodySetup();
			if (!Body) { Fail(TEXT("ArchitectureCollisionMissing"), TEXT("The loaded architecture mesh has no authored collision setup.")); return nullptr; }
			if (!ValidBox(Mesh->GetBoundingBox()))
			{ Fail(TEXT("ArchitectureGeometryInvalid"), TEXT("The loaded architecture mesh has invalid render bounds.")); return nullptr; }
			G->Bounds = Mesh->GetBoundingBox();
			const auto Add = [&](const FVector& V)
			{
				if (!Work() || !Finite(V) || G->CollisionVertices.Num() >= 32768)
					return Fail(TEXT("ArchitectureCollisionBound"), TEXT("Actual architecture collision exceeds its finite 32768-vertex contract or contains invalid geometry."));
				G->CollisionVertices.Add(V); G->Bounds += V; return true;
			};
			if (Body->GetCollisionTraceFlag() == CTF_UseComplexAsSimple)
			{
				if (Body->TriMeshGeometries.IsEmpty())
				{ Fail(TEXT("ArchitectureCookedCollisionUnavailable"), TEXT("Active cooked architecture collision is absent; inactive simple shapes are not a substitute."), EEFCalystoAttemptFailure::Resource); return nullptr; }
				if (Body->TriMeshGeometries.Num() > 8)
				{ Fail(TEXT("ArchitectureCollisionBound"), TEXT("Architecture exceeds the finite eight-cooked-geometry contract.")); return nullptr; }
				for (const auto& Geometry : Body->TriMeshGeometries)
				{
					if (!Geometry) { Fail(TEXT("ArchitectureCookedCollisionUnavailable"), TEXT("A required resident cooked triangle geometry is null."), EEFCalystoAttemptFailure::Resource); return nullptr; }
					const auto& P = Geometry->Particles(); const auto& E = Geometry->Elements();
					if (P.Size() > 32768 || E.GetNumTriangles() < 1 || E.GetNumTriangles() > 65536 || !Work(E.GetNumTriangles() * 3))
					{ Fail(TEXT("ArchitectureCollisionBound"), TEXT("Cooked architecture collision exceeds the bounded vertex/triangle contract.")); return nullptr; }
					TSet<int32> Referenced;
					for (int32 I = 0; I < E.GetNumTriangles(); ++I) for (int32 J = 0; J < 3; ++J)
					{
						const int32 Index = E.RequiresLargeIndices() ? E.GetLargeIndexBuffer()[I][J] : int32(E.GetSmallIndexBuffer()[I][J]);
						if (Index < 0 || Index >= int32(P.Size())) { Fail(TEXT("ArchitectureGeometryInvalid"), TEXT("Cooked architecture collision index is invalid.")); return nullptr; }
						if (!Referenced.Contains(Index)) { Referenced.Add(Index); if (!Add(FVector(P.GetX(Index)))) return nullptr; }
					}
				}
			}
			else
			{
				const auto& A = Body->AggGeom;
				if (A.SphereElems.Num() + A.SphylElems.Num() + A.TaperedCapsuleElems.Num() + A.LevelSetElems.Num()
					+ A.SkinnedLevelSetElems.Num() + A.MLLevelSetElems.Num() + A.SkinnedTriangleMeshElems.Num())
				{ Fail(TEXT("UnsupportedArchitectureCollision"), TEXT("The current architecture proof supports active boxes, convexes and cooked triangles; curved/deforming collision requires an explicit additional contract.")); return nullptr; }
				if (!Work(A.BoxElems.Num() * 8 + A.ConvexElems.Num())) return nullptr;
				for (const auto& B : A.BoxElems) if (CollisionEnabledHasQuery(B.GetCollisionEnabled()))
				{
					const FVector Half(B.X * 0.5, B.Y * 0.5, B.Z * 0.5);
					if (!Finite(Half) || Half.GetMin() <= 0) { Fail(TEXT("ArchitectureGeometryInvalid"), TEXT("Architecture contains a degenerate collision box.")); return nullptr; }
					for (const auto& V : Corners(FBox(-Half, Half))) if (!Add(B.GetTransform().TransformPosition(V))) return nullptr;
				}
				for (const auto& C : A.ConvexElems) if (CollisionEnabledHasQuery(C.GetCollisionEnabled()))
				{
					if (C.VertexData.Num() < 4 || C.VertexData.Num() > 32768)
					{ Fail(TEXT("UnsupportedArchitectureCollision"), TEXT("Authored convex vertices are unavailable or exceed the bounded architecture contract.")); return nullptr; }
					for (const auto& V : C.VertexData) if (!Add(C.GetTransform().TransformPosition(V))) return nullptr;
				}
			}
			if (G->CollisionVertices.IsEmpty()) { Fail(TEXT("ArchitectureCollisionMissing"), TEXT("Architecture has no authored active query collision to establish attachment support.")); return nullptr; }
			MeshGeometry.Add(Mesh, G); return G;
		}
		const FArchitecturePayload* Payload(const FEFCalystoArchitectureEntry& Entry)
		{
			if (const auto* Found = ArchitecturePayloads.Find(Entry.Selection.Id)) return Found;
			FArchitecturePayload P; FString Error;
			if (Entry.Payload == EEFCalystoArchitecturePayload::Actor)
			{ Fail(TEXT("UnsupportedArchitectureActor"), TEXT("Actor architecture requires a tracked template/bridge geometry contract; it cannot be silently removed from the weighted alternatives.")); return nullptr; }
			if (Entry.Payload == EEFCalystoArchitecturePayload::Mesh)
			{
				auto& C = P.Children.AddDefaulted_GetRef(); C.Definition.Id = Entry.Selection.Id; C.Definition.Mesh = Entry.Mesh.ToSoftObjectPath();
			}
			else if (Entry.Payload == EEFCalystoArchitecturePayload::BakedPCG)
			{
				UObject* Resource = ArchitectureResources.FindRef(Entry.BakedPCG.ToSoftObjectPath());
				if (!IsValid(Resource))
				{ Fail(TEXT("ArchitecturePayloadUnavailable"), TEXT("The exact baked payload is absent from retained dependency resources."), EEFCalystoAttemptFailure::Resource); return nullptr; }
				const auto* Data = Cast<UPCGDataAsset>(Resource);
				if (!Data) { Fail(TEXT("UnsupportedArchitecturePayload"), TEXT("The retained baked resource has an incompatible data-asset class.")); return nullptr; }
				TArray<FEFCalystoBakedMesh> Children; TArray<FSoftObjectPath> Dependencies;
				if (!FEFCalystoBakedArchitecture::Decode(*Data, Children, Dependencies, Error))
				{ Fail(TEXT("ArchitecturePayloadInvalid"), Error); return nullptr; }
				if (!Work(Children.Num() + Dependencies.Num())) return nullptr;
				for (const auto& Path : Dependencies) if (!IsValid(ArchitectureResources.FindRef(Path)))
				{ Fail(TEXT("ArchitectureDependencyMissing"), TEXT("Every decoded mesh/material dependency must already belong to the caller's retained load closure."), EEFCalystoAttemptFailure::Resource); return nullptr; }
				for (const auto& C : Children) { auto& Child = P.Children.AddDefaulted_GetRef(); Child.Definition = C; }
			}
			else { Fail(TEXT("UnsupportedArchitecturePayload"), TEXT("Unknown nonempty architecture payload.")); return nullptr; }
			if (Entry.Payload == EEFCalystoArchitecturePayload::BakedPCG && !P.Children.ContainsByPredicate([](const auto& C)
				{ return C.Definition.ChancePercent == 100 && C.Definition.Rotation == EEFCalystoBakedRotation::Preserve; }))
			{
				Fail(TEXT("UnsupportedArchitectureAttachment"), TEXT("Baked attachment currently requires a guaranteed fixed-rotation child with a persistent collision contact witness. All-conditional/all-yaw attachment needs an additional contract; the alternative was not silently removed."));
				return nullptr;
			}
			for (auto& C : P.Children)
			{
				UObject* Resource = ArchitectureResources.FindRef(C.Definition.Mesh);
				if (!IsValid(Resource))
				{ Fail(TEXT("ArchitectureDependencyMissing"), TEXT("The exact mesh is absent from the retained architecture closure."), EEFCalystoAttemptFailure::Resource); return nullptr; }
				auto* Mesh = Cast<UStaticMesh>(Resource);
				if (!Mesh) { Fail(TEXT("ArchitectureDependencyTypeInvalid"), TEXT("The retained architecture mesh resource has an incompatible class.")); return nullptr; }
				C.Geometry = LoadedGeometry(Mesh);
				if (!C.Geometry) return nullptr;
				// Retain the authored envelope, but never use it in place of actual render/collision geometry.
				C.Definition.ExportedLocalBounds += C.Geometry->Bounds;
			}
			return &ArchitecturePayloads.Add(Entry.Selection.Id, MoveTemp(P));
		}
		bool ArchitectureEnvelopes(const FEFCalystoArchitectureEntry& Entry, const FTransform& Native,
			const FArchitecturePayload& P, TArray<FTransform>& Variants, FBox& Translation,
			TArray<FEFCalystoArchitectureMeshSupportReceipt>& Meshes, FBox& Envelope, bool& Circular)
		{
			const auto& V = Entry.Variation;
			if (V.RotationMinimum.Pitch != V.RotationMaximum.Pitch || V.RotationMinimum.Roll != V.RotationMaximum.Roll)
				return Fail(TEXT("UnsupportedArchitectureRotation"), TEXT("Continuous pitch/roll contact requires an additional geometry contract; no alternative was silently reweighted."));
			Circular = V.RotationMinimum.Yaw != V.RotationMaximum.Yaw || Entry.Rotation == EEFCalystoArchitectureRotation::Full360;
			if (Circular && !Native.GetRotation().RotateVector(FVector::UpVector).Equals(FVector::UpVector, 1e-8))
				return Fail(TEXT("UnsupportedArchitectureRotation"), TEXT("Continuous parent yaw currently requires a vertical native basis."));
			const int32 Rotations = Circular ? 1 : Entry.Rotation == EEFCalystoArchitectureRotation::Degrees45 ? 8 : Entry.Rotation == EEFCalystoArchitectureRotation::Degrees90 ? 4 : 1;
			Translation = FBox(Entry.Transform.LocationOffset + V.LocationMinimum, Entry.Transform.LocationOffset + V.LocationMaximum)
				.TransformBy(FTransform(Native.GetRotation(), FVector::ZeroVector, Native.GetScale3D()));
			for (int32 R = 0; R < Rotations; ++R) for (int32 S = 0; S < (V.bUniformScale ? 2 : 8); ++S)
			{
				FVector Scale(S & 1 ? V.ScaleMaximum.X : V.ScaleMinimum.X, S & 2 ? V.ScaleMaximum.Y : V.ScaleMinimum.Y, S & 4 ? V.ScaleMaximum.Z : V.ScaleMinimum.Z);
				if (V.bUniformScale) Scale = FVector(Scale.X);
				Scale *= Entry.Transform.bUniformScale ? FVector(Entry.Transform.Scale.X) : Entry.Transform.Scale;
				FRotator Rotation = V.RotationMinimum + Entry.Transform.RotationOffset;
				if (Circular) Rotation.Yaw = 0; else if (Rotations > 1) Rotation.Yaw += 360.0 * R / Rotations;
				const FTransform Parent = FTransform(Rotation, FVector::ZeroVector, Scale) * Native;
				if (!Parent.IsValid() || Parent.GetScale3D().GetMin() <= 0) return Fail(TEXT("ArchitectureVariationInvalid"), TEXT("Architecture variation produced an invalid complete-transform endpoint."));
				Variants.Add(Parent);
			}
			for (const auto& C : P.Children)
			{
				auto& Receipt = Meshes.AddDefaulted_GetRef(); Receipt.ChildId = C.Definition.Id; Receipt.Mesh = C.Geometry->Mesh; Receipt.ActualLocalBounds = C.Geometry->Bounds;
				for (const auto& Parent : Variants)
				{
					if (!Work(8)) return false;
					FBox B; FString Error;
					if (!FEFCalystoBakedArchitecture::GetEnvelope(C.Definition, Parent, B, Error)) return Fail(TEXT("ArchitectureEnvelopeInvalid"), Error);
					if (Circular)
					{
						const FVector Pivot = Parent.GetLocation(); double Radius = 0;
						for (const auto& Point : Corners(B)) Radius = FMath::Max(Radius, FVector2D(Point.X - Pivot.X, Point.Y - Pivot.Y).Size());
						B.Min.X = Pivot.X - Radius; B.Max.X = Pivot.X + Radius; B.Min.Y = Pivot.Y - Radius; B.Max.Y = Pivot.Y + Radius;
					}
					Receipt.SweptWorldBounds += FBox(B.Min + Translation.Min, B.Max + Translation.Max);
				}
				Envelope += Receipt.SweptWorldBounds;
			}
			return ValidBox(Envelope) && Envelope.GetSize().GetMin() > 0
				? true : Fail(TEXT("ArchitectureEnvelopeInvalid"), TEXT("The complete actual geometry envelope is degenerate."));
		}
		bool ArchitectureCoverage(const FFace& Plane, const FBox& Envelope, TArray<FEFCalystoSurfaceSupportReceipt>& SupportsOut)
		{
			const FVector N = Plane.Support.Normal;
			const FPolygon Footprint = ProjectedEnvelope(Envelope, Plane.Support.Point, N);
			// Only discard numerical zero-area boundaries, never expand support into a gap.
			constexpr double ZeroArea = 1e-8; // Square centimetres.
			if (PolygonArea(Footprint, N) <= ZeroArea) return false;
			TArray<const FFace*> Coverage;
			TArray<FVector> Witnesses;
			for (const auto& Face : Faces)
			{
				if (!Work()) return false;
				if (Face.Role != Plane.Role || !Face.Support.Normal.Equals(N, 1e-8)
					|| FMath::Abs(FVector::DotProduct(Face.Support.Point - Plane.Support.Point, N)) > 1e-5) continue;
				FPolygon Intersection = Footprint;
				for (int32 I = 0; I < Face.Vertices.Num() && !Intersection.IsEmpty(); ++I)
				{
					if (!Work(Intersection.Num())) return false;
					const FVector Inward = FVector::CrossProduct(N, Face.Vertices[(I + 1) % Face.Vertices.Num()] - Face.Vertices[I]);
					Intersection = ClipPolygon(Intersection, Face.Vertices[I], Inward, true);
				}
				if (PolygonArea(Intersection, N) <= ZeroArea) continue;
				if (Coverage.Num() >= 64) return Fail(TEXT("ArchitectureSupportBound"), TEXT("One architecture envelope exceeds 64 actual contributing collision faces; no support was truncated."));
				Coverage.Add(&Face); FVector Center = FVector::ZeroVector;
				for (const auto& V : Intersection) Center += V; Witnesses.Add(Center / Intersection.Num());
			}
			TArray<FPolygon> Remaining{Footprint};
			for (const auto* Face : Coverage)
			{
				TArray<FPolygon> Next;
				for (auto Piece : Remaining)
				{
					for (int32 I = 0; I < Face->Vertices.Num() && !Piece.IsEmpty(); ++I)
					{
						if (!Work(2 * Piece.Num())) return false;
						const FVector Inward = FVector::CrossProduct(N, Face->Vertices[(I + 1) % Face->Vertices.Num()] - Face->Vertices[I]);
						auto Outside = ClipPolygon(Piece, Face->Vertices[I], Inward, false);
						if (PolygonArea(Outside, N) > ZeroArea) Next.Add(MoveTemp(Outside));
						if (Next.Num() > 256) return Fail(TEXT("ArchitectureSupportBound"), TEXT("Exact support subtraction exceeds 256 remaining convex pieces; incomplete coverage was not accepted."));
						Piece = ClipPolygon(Piece, Face->Vertices[I], Inward, true);
					}
				}
				Remaining = MoveTemp(Next); if (Remaining.IsEmpty()) break;
			}
			if (!Remaining.IsEmpty() || Coverage.IsEmpty()) return false;
			for (int32 I = 0; I < Coverage.Num(); ++I)
			{
				const auto& Face = *Coverage[I];
				const auto G = LoadedGeometry(Face.Support.Component->GetStaticMesh()); if (!G) return false;
				for (const auto& V : G->CollisionVertices)
				{
					if (!Work()) return false;
					if (FVector::DotProduct(Face.Support.InstanceTransform.TransformPosition(V) - Face.Support.Point, N) > PlaneTolerance) return false;
				}
				if (!TraceSupport(Face, Witnesses[I])) return false;
				auto Receipt = Face.Support; Receipt.Point = Witnesses[I]; SupportsOut.Add(Receipt);
			}
			return true;
		}
		bool ArchitectureContact(const FFace& Face, const FArchitecturePayload& P,
			const TArray<FTransform>& Variants, const FBox& Translation, const FBox& Envelope, bool Circular,
			TArray<FEFCalystoSurfaceSupportReceipt>& SupportsOut)
		{
			const FVector N = Face.Support.Normal;
			if (Circular && FMath::Abs(N.Z) < 0.999999) return false;
			const FVector Center = Envelope.GetCenter(), Projected = Center - N * FVector::DotProduct(Center - Face.Support.Point, N);
			if (!Contains(Face.Vertices, N, Projected, 0)) return false;
			for (const auto& Point : Corners(Envelope)) if (FVector::DotProduct(Point - Face.Support.Point, N) < -PlaneTolerance) return false;
			// A real collision vertex must remain on the plane over every scale/location endpoint.
			// Positions are affine between these endpoints. Requiring one persistent witness also
			// rules out an envelope that touches the floor only at one end of a floating variation.
			const double Offset = FVector::DotProduct(Translation.GetCenter(), N), Spread = FVector::DotProduct(Translation.GetExtent(), N.GetAbs());
			if (Spread > PlaneTolerance) return false;
			bool bContact = false;
			for (const auto& C : P.Children)
			{
				if (C.Definition.Rotation != EEFCalystoBakedRotation::Preserve || C.Definition.ChancePercent != 100) continue;
				for (const auto& Vertex : C.Geometry->CollisionVertices)
				{
					bool bPersistent = true;
					for (const auto& Parent : Variants)
					{
						if (!Work()) return false;
						const FVector Point = (C.Definition.LocalTransform * Parent).TransformPosition(Vertex);
						if (FMath::Abs(FVector::DotProduct(Point - Face.Support.Point, N) + Offset) + Spread > AttachmentContactTolerance) { bPersistent = false; break; }
					}
					if (bPersistent) { bContact = true; break; }
				}
				if (bContact) break;
			}
			if (!bContact) return false;
			return ArchitectureCoverage(Face, Envelope, SupportsOut);
		}
		bool ArchitectureClear(const FBox& Envelope, const TArray<FEFCalystoSurfaceSupportReceipt>& Supports)
		{
			if (!Query()) return false;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CalystoArchitectureEnvelope), false);
			if (NI.ProtectedPlayer.IsValid()) Params.AddIgnoredActor(NI.ProtectedPlayer.Get());
			TArray<FOverlapResult> Overlaps;
			World.OverlapMultiByChannel(Overlaps, Envelope.GetCenter(), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeBox(Envelope.GetExtent()), Params);
			if (Overlaps.Num() > 1024 || !Work(Overlaps.Num())) return Fail(TEXT("ArchitectureOverlapBound"), TEXT("Complete architecture overlap evidence exceeded its finite capacity."));
			for (const auto& Hit : Overlaps) if (Hit.bBlockingHit)
			{
				if (!Supports.ContainsByPredicate([&](const auto& Support)
				{ return Hit.GetComponent() == Support.Component.Get() && (Support.InstanceIndex == INDEX_NONE || Hit.GetItemIndex() == Support.InstanceIndex); })) return false;
			}
			return true;
		}
		bool Architecture()
		{
			for (UObject* Resource : Input.LoadedArchitectureResources)
			{
				if (!Work()) return false;
				if (!IsValid(Resource)) return Fail(TEXT("ArchitectureDependencyMissing"), TEXT("A borrowed architecture dependency is no longer valid."), EEFCalystoAttemptFailure::Resource);
				const FSoftObjectPath Path(Resource); if (UObject* Existing = ArchitectureResources.FindRef(Path); Existing && Existing != Resource)
					return Fail(TEXT("ArchitectureDependencyIdentity"), TEXT("Two retained objects claim the same architecture resource path."));
				ArchitectureResources.Add(Path, Resource);
			}
			TMap<FGuid, FString> Keys;
			for (const auto& Slot : Input.Native->Surfaces)
			{
				if (!Work()) return false;
				const auto* Room = Input.Native->Rooms.FindByPredicate([&](const auto& R) { return R.RoomId == Slot.RoomId; });
				if (!Room) return Fail(TEXT("ArchitectureRoomMissing"), TEXT("Native architecture opportunity has no exact room owner."));
				if (Room->Protection != EEFCalystoProtectedRoom::None) continue;
				const auto* Theme = Input.Configuration->FindTheme(Room->ThemeId);
				const auto FindRule = [&](const TArray<FEFCalystoSurfaceDecoration>& Rules)
				{ return Rules.FindByPredicate([&](const auto& R) { return R.Zone == Slot.Zone; }); };
				const auto* Rule = Theme ? FindRule(Theme->Architecture) : nullptr; if (!Rule) Rule = FindRule(Style.Architecture.Decoration);
				if (!Rule) continue;
				if (!Slot.Transform.IsValid() || Slot.Transform.GetScale3D().GetMin() <= 0 || !Slot.Normal.IsNormalized() || !Finite(Slot.Normal)
					|| uint8(Slot.Zone) > uint8(EEFCalystoPlacementZone::Roof) || Rule->Alternatives.Num() > 256)
					return Fail(TEXT("ArchitectureSlotInvalid"), TEXT("Native architecture transform, normal, zone or alternative capacity is invalid."));
				const FString Key = FString::Printf(TEXT("architecture|%lld|%u|%s|%s|%s|%s"), Slot.RoomId, uint8(Slot.Zone), *VectorKey(Slot.Transform.GetLocation()),
					*VectorKey(Slot.Transform.GetRotation().Euler()), *VectorKey(Slot.Transform.GetScale3D()), *VectorKey(Slot.Normal));
				const FGuid Id = Identity(Key);
				if (const auto* Existing = Keys.Find(Id)) { if (*Existing != Key) return Fail(TEXT("ArchitectureIdentityCollision"), TEXT("Distinct native slots have collided canonical identities.")); else continue; }
				Keys.Add(Id, Key);
				if (Output.ArchitectureOpportunities.Num() >= Input.Limits.MaximumCandidates) return Fail(TEXT("ArchitectureCandidateBound"), TEXT("Native architecture opportunities exceed the finite capacity; no truncation was accepted."));
				auto& O = Output.ArchitectureOpportunities.AddDefaulted_GetRef(); O.Id = Id; O.RoomId = Slot.RoomId; O.Zone = Slot.Zone; O.NativeTransform = Slot.Transform;
				TArray<const FEFCalystoArchitectureEntry*> Entries; for (const auto& Entry : Rule->Alternatives) Entries.Add(&Entry);
				Entries.Sort([](const auto& A, const auto& B) { return GuidLess(A.Selection.Id, B.Selection.Id); });
				for (const auto* E : Entries)
				{
					if (!Work()) return false;
					if (!FEFCalystoDirectorProbability::IsEligible(E->Selection, Input.RoomConfiguration->FloorNumber, Input.ArchitectureCoolingDownIds)
						|| E->Payload == EEFCalystoArchitecturePayload::Empty) continue;
					const auto* P = Payload(*E); if (!P) return false;
					FEFCalystoArchitectureSupportReceipt Receipt; Receipt.OpportunityId = Id; Receipt.EntryId = E->Selection.Id; Receipt.NativeTransform = Slot.Transform;
					TArray<FTransform> Variants; FBox Translation; bool Circular = false;
					if (!ArchitectureEnvelopes(*E, Slot.Transform, *P, Variants, Translation, Receipt.Meshes, Receipt.SweptWorldBounds, Circular)) return false;
					const auto Rejection = [&](const TCHAR* Reason)
					{
						if (Output.FirstArchitectureRejection.IsEmpty()) Output.FirstArchitectureRejection = FString::Printf(
							TEXT("%s: room=%lld entry=%s bounds_min=%s bounds_max=%s native_slot=%s"), Reason, Slot.RoomId,
							*E->Selection.Id.ToString(), *Receipt.SweptWorldBounds.Min.ToString(), *Receipt.SweptWorldBounds.Max.ToString(), *Slot.Transform.ToString());
					};
					if (RoomFor(Receipt.SweptWorldBounds) != Slot.RoomId) { Rejection(TEXT("RoomOwnership")); ++Output.RejectedGeometry; if (bFailed) return false; continue; }
					if (Protected(Receipt.SweptWorldBounds)) { Rejection(TEXT("ProtectedRouteOrDoor")); ++Output.RejectedProtection; if (bFailed) return false; continue; }
					const EEFCalystoStructuralRole Role = Slot.Zone == EEFCalystoPlacementZone::Floor ? EEFCalystoStructuralRole::Floor
						: Slot.Zone == EEFCalystoPlacementZone::Roof ? EEFCalystoStructuralRole::Roof : EEFCalystoStructuralRole::Wall;
					for (const auto& Face : Faces)
					{
						if (!Work()) return false;
						if (Face.Role != Role || FVector::DotProduct(Face.Support.Normal, Slot.Normal) < (Corner(Slot.Zone) ? 0.6 : 0.999)) continue;
						TArray<FEFCalystoSurfaceSupportReceipt> Supports;
						if (!ArchitectureContact(Face, *P, Variants, Translation, Receipt.SweptWorldBounds, Circular, Supports)) { if (bFailed) return false; continue; }
						const FFace* Second = nullptr;
						if (Corner(Slot.Zone)) for (const auto& Other : Faces)
						{
							if (!Work()) return false;
							if (Other.Role != Role || FMath::Abs(FVector::DotProduct(Face.Support.Normal, Other.Support.Normal)) > 0.001 || FVector::DotProduct(Other.Support.Normal, Slot.Normal) < 0.6) continue;
							TArray<FEFCalystoSurfaceSupportReceipt> OtherSupports;
							if (ArchitectureContact(Other, *P, Variants, Translation, Receipt.SweptWorldBounds, Circular, OtherSupports)) { Second = &Other; Supports.Append(OtherSupports); break; }
							if (bFailed) return false;
						}
						if (Corner(Slot.Zone) && !Second) continue;
						if (!ArchitectureClear(Receipt.SweptWorldBounds, Supports)) { if (bFailed) return false; continue; }
						Receipt.Supports = MoveTemp(Supports); break;
					}
					if (Receipt.Supports.IsEmpty()) { Rejection(TEXT("ActualSupportOrClearance")); ++Output.RejectedGeometry; continue; }
					if (++CompatiblePairs > Input.Limits.MaximumCompatiblePairs) return Fail(TEXT("ArchitectureCandidateBound"), TEXT("Combined content/architecture compatibility exceeds its finite capacity."));
					O.CompatibleEntryBounds.Add(E->Selection.Id, Receipt.SweptWorldBounds); Output.ArchitectureReceipts.Add(MoveTemp(Receipt));
				}
			}
			return true;
		}
		bool Candidates()
		{
			for (const auto& Face : Faces) if (Face.Role == EEFCalystoStructuralRole::Floor)
			{
				const double Step = Input.Limits.FloorGridSpacingCm;
				const double MinX = FMath::CeilToDouble(Face.Bounds.Min.X / Step), MaxX = FMath::FloorToDouble(Face.Bounds.Max.X / Step);
				const double MinY = FMath::CeilToDouble(Face.Bounds.Min.Y / Step), MaxY = FMath::FloorToDouble(Face.Bounds.Max.Y / Step);
				if (FMath::Max(FMath::Abs(MinX), FMath::Abs(MaxX)) > 10000000 || FMath::Max(FMath::Abs(MinY), FMath::Abs(MaxY)) > 10000000)
					return Fail(TEXT("SurfaceCoordinateBound"), TEXT("Native surface coordinates exceed the finite grid indexing contract."));
				if (!Work(int64(FMath::Min((MaxX - MinX + 1) * (MaxY - MinY + 1), double(Input.Limits.MaximumWorkUnits + 1))))) return false;
				for (double X = MinX; X <= MaxX; ++X) for (double Y = MinY; Y <= MaxY; ++Y)
				{
					const FVector Anchor(X * Step, Y * Step, Face.Support.Point.Z);
					const int64 RoomId = RoomFor(FBox(Anchor, Anchor)); const auto* RoomContracts = Contracts.Find(RoomId);
					if (!RoomContracts) continue;
					for (const auto& C : *RoomContracts) if (C.Placement.Zone == EEFCalystoPlacementZone::Floor)
						if (!Candidate(RoomId, C, Anchor, Input.RoomConfiguration->DungeonTransform.GetRotation(), Face)) return false;
				}
			}
			for (const auto& Opportunity : Input.Native->Surfaces)
			{
				if (!Work()) return false;
				if (Opportunity.Zone == EEFCalystoPlacementZone::Floor) continue;
				const auto* RoomContracts = Contracts.Find(Opportunity.RoomId); if (!RoomContracts) return Fail(TEXT("SurfaceRoomIdentity"), TEXT("A native opportunity names an absent room."));
				if (!Finite(Opportunity.Normal) || !Opportunity.Normal.IsNormalized() || !Opportunity.Transform.IsValid()) return Fail(TEXT("SurfaceOpportunityInvalid"), TEXT("Native surface normal/transform is invalid."));
				for (const auto& C : *RoomContracts) if (C.Placement.Zone == Opportunity.Zone)
				{
					for (const auto& Face : Faces)
					{
						if (!Work()) return false;
						if (Face.Role != (Opportunity.Zone == EEFCalystoPlacementZone::Roof ? EEFCalystoStructuralRole::Roof : EEFCalystoStructuralRole::Wall)) continue;
						const FVector N = Face.Support.Normal; const double Alignment = FVector::DotProduct(N, Opportunity.Normal);
						if (Alignment < (Corner(Opportunity.Zone) ? 0.6 : 0.999)) continue;
						const FVector Location = Opportunity.Transform.GetLocation(); const double Distance = FVector::DotProduct(Location - Face.Support.Point, N);
						if (FMath::Abs(Distance) > 100.0) continue; // Native wall origins can lie inside their real wall thickness.
						const FVector Anchor = Location - N * Distance;
						if (!Contains(Face.Vertices, N, Anchor, 0)) continue;
						if (!Corner(Opportunity.Zone)) { if (!Candidate(Opportunity.RoomId, C, Anchor, Opportunity.Transform.GetRotation(), Face)) return false; }
						else for (const auto& Other : Faces)
						{
							if (!Work()) return false;
							if (Other.Role != EEFCalystoStructuralRole::Wall || FMath::Abs(FVector::DotProduct(N, Other.Support.Normal)) > 0.001
								|| FVector::DotProduct(Other.Support.Normal, Opportunity.Normal) < 0.6
								|| FMath::Abs(FVector::DotProduct(Location - Other.Support.Point, Other.Support.Normal)) > 100.0) continue;
							if (!Candidate(Opportunity.RoomId, C, Anchor, Opportunity.Transform.GetRotation(), Face, &Other)) return false;
						}
					}
				}
			}
			return !bFailed;
		}
		/** Build only finite, physically proved decal candidates.  This deliberately does not roll
		 * Chance, choose a variant, sample a size, load an asset, or alter the native transform. */
		bool Decals()
		{
			struct FDecalRoomContract
			{
				FGuid ThemeId;
				double MaximumSizeCm = 0.0;
				bool bSupportsSurface[3] = {};
			};
			TMap<int64, FDecalRoomContract> RoomContracts;
			for (const FEFCalystoNativeRoom& Room : Input.Native->Rooms)
			{
				if (!Work()) return false;
				// These flags are topology/progression authority. Decal configuration cannot opt a room back in.
				if (Room.Protection != EEFCalystoProtectedRoom::None || Room.bMainPath || Room.bDoorClearance) continue;
				FEFCalystoDecals Effective;
				FString Error;
				if (!Input.Configuration->ResolveDecals(Style.Selection.Id, Room.ThemeId, Effective, Error))
					return Fail(TEXT("DecalResolutionInvalid"), Error);
				if (Effective.Mode != EEFCalystoDecalMode::Replace) continue;
				if (!FMath::IsFinite(Effective.ChancePercent) || Effective.ChancePercent < 0.0 || Effective.ChancePercent > 100.0
					|| Effective.MaximumPerRoom < 0 || Effective.MaximumPerRoom > 1 || Effective.ActiveFloorBudget < 0
					|| Effective.ActiveFloorBudget > 24 || Effective.FloorLimit < 0 || Effective.WallLimit < 0 || Effective.RoofLimit < 0
					|| Effective.Variants.Num() > 24)
					return Fail(TEXT("DecalConfigurationInvalid"), TEXT("Resolved decals exceed the supported finite pool, probability or surface-capacity contract."));
				if (Effective.ChancePercent <= 0.0 || Effective.MaximumPerRoom == 0 || Effective.ActiveFloorBudget == 0
					|| (Effective.FloorLimit == 0 && Effective.WallLimit == 0 && Effective.RoofLimit == 0)) continue;
				const double MaximumSize = Effective.SizeCm.Distribution == EEFCalystoDistribution::Fixed ? Effective.SizeCm.Value : Effective.SizeCm.Maximum;
				if (!FMath::IsFinite(MaximumSize) || MaximumSize <= 0.0)
					return Fail(TEXT("DecalConfigurationInvalid"), TEXT("Resolved decals require a finite positive maximum size before physical footprint feasibility."));
				FDecalRoomContract& Contract = RoomContracts.FindOrAdd(Room.RoomId);
				Contract.ThemeId = Room.ThemeId;
				Contract.MaximumSizeCm = MaximumSize;
				for (const FEFCalystoDecalVariant& Variant : Effective.Variants)
				{
					if (!Work()) return false;
					if (!Variant.Selection.bEnabled || Variant.Selection.Weight <= 0.0
						|| !FEFCalystoDirectorProbability::IsEligible(Variant.Selection, Input.RoomConfiguration->FloorNumber, {})) continue;
					Contract.bSupportsSurface[int32(EEFCalystoDecalSurface::Floor)] |= Variant.bFloor && Effective.FloorLimit > 0;
					Contract.bSupportsSurface[int32(EEFCalystoDecalSurface::Wall)] |= Variant.bWall && Effective.WallLimit > 0;
					Contract.bSupportsSurface[int32(EEFCalystoDecalSurface::Roof)] |= Variant.bRoof && Effective.RoofLimit > 0;
				}
				if (!Contract.bSupportsSurface[0] && !Contract.bSupportsSurface[1] && !Contract.bSupportsSurface[2]) RoomContracts.Remove(Room.RoomId);
			}
			if (RoomContracts.IsEmpty()) return true;

			for (const FEFCalystoNativeSurface& Slot : Input.Native->Surfaces)
			{
				if (!Work()) return false;
				const FDecalRoomContract* Contract = RoomContracts.Find(Slot.RoomId);
				if (!Contract) continue;
				EEFCalystoDecalSurface Surface;
				if (!DecalSurface(Slot.Zone, Surface) || !Contract->bSupportsSurface[int32(Surface)]) continue;
				if (Slot.OpportunityId <= 0 || !Slot.Transform.IsValid() || Slot.Transform.GetScale3D().GetMin() <= 0.0
					|| !Finite(Slot.Transform.GetLocation()) || !Finite(Slot.Normal) || !Slot.Normal.IsNormalized())
				{ ++Output.RejectedDecalGeometry; continue; }
				const EEFCalystoStructuralRole Role = Surface == EEFCalystoDecalSurface::Floor ? EEFCalystoStructuralRole::Floor
					: Surface == EEFCalystoDecalSurface::Roof ? EEFCalystoStructuralRole::Roof : EEFCalystoStructuralRole::Wall;
				bool bAccepted = false;
				bool bProtected = false;
				for (const FFace& Face : Faces)
				{
					if (!Work()) return false;
					if (Face.Role != Role || FVector::DotProduct(Face.Support.Normal, Slot.Normal) < 0.999) continue;
					const double PlaneDistance = FVector::DotProduct(Slot.Transform.GetLocation() - Face.Support.Point, Face.Support.Normal);
					if (FMath::Abs(PlaneDistance) > NativeOpportunityPlaneTolerance) continue;
					const FVector Anchor = Slot.Transform.GetLocation() - Face.Support.Normal * PlaneDistance;
					if (!Contains(Face.Vertices, Face.Support.Normal, Anchor, 0.0)) continue;

					const FTransform WorldTransform(FRotationMatrix::MakeFromX(Face.Support.Normal).ToQuat(), Anchor);
					if (!WorldTransform.IsValid() || FVector::DotProduct(WorldTransform.GetUnitAxis(EAxis::X), Face.Support.Normal) < 0.999999)
						return Fail(TEXT("DecalOrientationInvalid"), TEXT("A verified decal surface could not preserve its exact physical normal as the projection X axis."));
					FPolygon Footprint;
					Footprint.Reserve(4);
					Footprint.Add(WorldTransform.TransformPosition(FVector(0.0, -Contract->MaximumSizeCm, -Contract->MaximumSizeCm)));
					Footprint.Add(WorldTransform.TransformPosition(FVector(0.0, Contract->MaximumSizeCm, -Contract->MaximumSizeCm)));
					Footprint.Add(WorldTransform.TransformPosition(FVector(0.0, Contract->MaximumSizeCm, Contract->MaximumSizeCm)));
					Footprint.Add(WorldTransform.TransformPosition(FVector(0.0, -Contract->MaximumSizeCm, Contract->MaximumSizeCm)));
					FBox FootprintBounds(ForceInit);
					for (const FVector& CornerPoint : Footprint) FootprintBounds += CornerPoint;
					if (!ValidBox(FootprintBounds) || RoomFor(FootprintBounds) != Slot.RoomId)
					{
						if (bFailed) return false;
						break;
					}
					if (Protected(FootprintBounds))
					{
						if (bFailed) return false;
						bProtected = true;
						break;
					}
					// The pool later rechecks this exact center binding. Full coverage below independently proves the whole maximum footprint.
					if (!TraceDecalSupport(Face, Anchor))
					{
						if (bFailed) return false;
						break;
					}
					TArray<FEFCalystoSurfaceSupportReceipt> Coverage;
					if (!DecalCoverage(Face, Footprint, Coverage))
					{
						if (bFailed) return false;
						break;
					}
					const FString Key = FString::Printf(TEXT("decal|%lld|%lld|%s|%u|%s|%s|%.17g"), Slot.RoomId, Slot.OpportunityId,
						*Contract->ThemeId.ToString(EGuidFormats::Digits), uint8(Surface), *VectorKey(Anchor), *VectorKey(Face.Support.Normal), Contract->MaximumSizeCm);
					const FGuid Id = Identity(Key);
					if (DecalNativeOpportunityIds.Contains(Slot.OpportunityId))
						return Fail(TEXT("DecalNativeIdentityCollision"), TEXT("Distinct physically feasible decals share one native opportunity identity."));
					if (const FString* Existing = DecalKeys.Find(Id))
						return *Existing == Key
							? Fail(TEXT("DecalIdentityDuplicate"), TEXT("One native decal opportunity was observed more than once after physical proof."))
							: Fail(TEXT("DecalIdentityCollision"), TEXT("Distinct decal opportunities collided canonical identities."));
					if (Output.DecalOpportunities.Num() >= Input.Limits.MaximumDecalOpportunities)
						return Fail(TEXT("DecalOpportunityBound"), TEXT("Native decal opportunities exceed the explicit finite capacity; no truncation was accepted."));
					DecalNativeOpportunityIds.Add(Slot.OpportunityId);
					DecalKeys.Add(Id, Key);
					auto& Opportunity = Output.DecalOpportunities.AddDefaulted_GetRef();
					Opportunity.Id = Id;
					Opportunity.RoomId = Slot.RoomId;
					Opportunity.NativeOpportunityId = Slot.OpportunityId;
					Opportunity.ThemeId = Contract->ThemeId;
					Opportunity.Surface = Surface;
					Opportunity.WorldTransform = WorldTransform;
					Opportunity.SurfaceNormal = Face.Support.Normal;
					Opportunity.MaximumSizeCm = Contract->MaximumSizeCm;
					Opportunity.Support = Face.Support.Component.Get();
					Opportunity.InstanceIndex = Face.Support.InstanceIndex;
					Opportunity.SupportTransform = Face.Support.InstanceTransform;
					auto& Receipt = Output.DecalReceipts.AddDefaulted_GetRef();
					Receipt.OpportunityId = Id;
					Receipt.FootprintWorldBounds = FootprintBounds;
					Receipt.Supports = MoveTemp(Coverage);
					Opportunity.CoverageSupports.Reserve(Receipt.Supports.Num());
					for (const FEFCalystoSurfaceSupportReceipt& Support : Receipt.Supports)
					{
						FEFCalystoDecalCoverageSupport& Witness = Opportunity.CoverageSupports.AddDefaulted_GetRef();
						Witness.Support = Support.Component.Get();
						Witness.InstanceIndex = Support.InstanceIndex;
						Witness.SupportTransform = Support.InstanceTransform;
						Witness.Point = Support.Point;
						Witness.Normal = Support.Normal;
					}
					bAccepted = true;
					break;
				}
				if (!bAccepted)
				{
					if (bProtected) ++Output.RejectedDecalProtection;
					else ++Output.RejectedDecalGeometry;
				}
			}
			return !bFailed;
		}
	};
}

bool FEFCalystoSurfaceCandidates::Build(const FEFCalystoSurfaceCandidateRequest& Request, FEFCalystoSurfaceCandidateResult& Result)
{
	using namespace EFCalystoSurfaceCandidatesPrivate;
	Result = {};
	const auto* NI = Request.NavigationInput; const auto* Nav = Request.Navigation; const auto& L = Request.Limits;
	const auto* Style = Request.Configuration && Request.RoomConfiguration ? Request.Configuration->FindStyle(Request.RoomConfiguration->StyleId) : nullptr;
	if (!IsInGameThread() || !Request.Configuration || !Request.Configuration->IsValid() || !Style || !NI || !Nav || !Request.Native
		|| !NI->World.IsValid() || !NI->DungeonOwner.IsValid() || NI->DungeonOwner->GetWorld() != NI->World.Get() || !NI->AttemptId.IsValid()
		|| !Request.Native->bGraphCompleted || Request.Native->GenerateRequests != 1 || Request.Native->Rooms.IsEmpty() || Request.Native->Rooms.Num() > 2048
		|| Request.Native->Surfaces.Num() > 65536 || NI->StructuralSources.IsEmpty() || NI->StructuralSources.Num() > 1024
		|| Request.LoadedArchitectureResources.Num() > 4096 || Request.ArchitectureCoolingDownIds.Num() > 4096 || Request.RoomConfiguration->FloorNumber < 1
		|| Nav->State != EEFCalystoNavigationObservation::Ready || !Nav->bEntryTransformValid || Nav->CompleteRoute.Num() < 2 || Nav->CompleteRoute.Num() > 8192
		|| Request.Native->NativeStartMarkers.Num() != 1 || Request.Native->NativeEndMarkers.Num() != 1
		|| NI->StartMarkers != Request.Native->NativeStartMarkers || NI->EndMarkers != Request.Native->NativeEndMarkers
		|| !Nav->ValidatedEntryTransform.IsValid() || !Finite(Nav->ValidatedEndApproach)
		|| !FMath::IsFinite(NI->CapsuleRadius) || !FMath::IsFinite(NI->CapsuleHalfHeight) || NI->CapsuleRadius < 1 || NI->CapsuleHalfHeight < NI->CapsuleRadius || NI->CapsuleHalfHeight > 10000
		|| !FMath::IsFinite(NI->RequestDeadlineSeconds) || !Request.RoomConfiguration->DungeonTransform.IsValid()
		|| !FMath::IsFinite(L.FloorGridSpacingCm) || L.FloorGridSpacingCm < 25 || L.FloorGridSpacingCm > 400
		|| L.MaximumFaces < 1 || L.MaximumFaces > 65536 || L.MaximumCandidates < 1 || L.MaximumCandidates > 16384
		|| L.MaximumDecalOpportunities < 1 || L.MaximumDecalOpportunities > 4096
		|| L.MaximumCompatiblePairs < 1 || L.MaximumCompatiblePairs > 32768 || L.MaximumPhysicsQueries < 1 || L.MaximumPhysicsQueries > 65536
		|| L.MaximumNavigationPolygons < 1 || L.MaximumNavigationPolygons > 32768 || L.MaximumWorkUnits < 1 || L.MaximumWorkUnits > 2000000)
	{
		Result.FailureCode = TEXT("SurfaceInputInvalid"); Result.Message = TEXT("Exact completed native geometry, accepted navigation, bounded limits and a live same-attempt world are required."); return false;
	}
	for (const FVector& P : Nav->CompleteRoute) if (!Finite(P)) { Result.FailureCode = TEXT("SurfaceRouteInvalid"); Result.Message = TEXT("Protected route contains a nonfinite point."); return false; }
	for (const auto& Marker : {Request.Native->NativeStartMarkers[0], Request.Native->NativeEndMarkers[0]})
		if (!Marker.IsValid() || Marker->IsActorBeingDestroyed() || Marker->GetWorld() != NI->World.Get() || !Finite(Marker->GetActorLocation()))
		{ Result.FailureCode = TEXT("SurfaceProtectedOwnerLost"); Result.Message = TEXT("An exact native Start/End marker lost its live world ownership."); Result.Failure = EEFCalystoAttemptFailure::Spatial; return false; }
	FAssembler Assembler{Request, Result, *NI, *Nav, *NI->World.Get(), *Style};
	if (!Assembler.Catalog() || !Assembler.Geometry() || !Assembler.Navigation() || !Assembler.Candidates() || !Assembler.Architecture() || !Assembler.Decals())
	{
		Result.Rooms.Reset(); Result.Surfaces.Reset(); Result.Receipts.Reset(); Result.ArchitectureOpportunities.Reset(); Result.ArchitectureReceipts.Reset();
		Result.DecalOpportunities.Reset(); Result.DecalReceipts.Reset();
		return false;
	}
	Result.Surfaces.Sort([](const auto& A, const auto& B) { return GuidLess(A.Id, B.Id); });
	Result.Receipts.Sort([](const auto& A, const auto& B) { return GuidLess(A.CandidateId, B.CandidateId); });
	Result.ArchitectureOpportunities.Sort([](const auto& A, const auto& B) { return GuidLess(A.Id, B.Id); });
	Result.ArchitectureReceipts.Sort([](const auto& A, const auto& B) { return A.OpportunityId == B.OpportunityId ? GuidLess(A.EntryId, B.EntryId) : GuidLess(A.OpportunityId, B.OpportunityId); });
	Result.DecalOpportunities.Sort([](const auto& A, const auto& B) { return GuidLess(A.Id, B.Id); });
	Result.DecalReceipts.Sort([](const auto& A, const auto& B) { return GuidLess(A.OpportunityId, B.OpportunityId); });
	Result.Message = TEXT("Complete finite compatibility set proved against actual native planar collision and required Player navigation; decals include only fully supported maximum footprints. No selection or realization performed.");
	return true;
}
