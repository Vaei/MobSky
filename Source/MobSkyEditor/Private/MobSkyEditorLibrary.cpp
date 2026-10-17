// Copyright (c) Jared Taylor

#include "MobSkyEditorLibrary.h"

#include "MobSkyModule.h"
#include "Async/ParallelFor.h"
#include "Engine/Texture2D.h"
#include "Engine/VolumeTexture.h"
#include "FileHelpers.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/Package.h"

namespace MobSkyNoise
{
	uint32 Hash(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352du;
		X ^= X >> 15;
		X *= 0x846ca68bu;
		X ^= X >> 16;
		return X;
	}

	uint32 Hash3(int32 X, int32 Y, int32 Z, uint32 Seed)
	{
		return Hash(static_cast<uint32>(X) + Hash(static_cast<uint32>(Y) + Hash(static_cast<uint32>(Z) + Seed * 0x9e3779b9u)));
	}

	float Unit(uint32 H)
	{
		return static_cast<float>(H & 0xffffffu) / static_cast<float>(0xffffff);
	}

	int32 Wrap(int32 V, int32 Period)
	{
		const int32 M = V % Period;
		return M < 0 ? M + Period : M;
	}

	/** Distance to the nearest feature point, one per cell, on a lattice that tiles every Period cells. */
	float Worley(const FVector3f& P, int32 Period, uint32 Seed)
	{
		const FVector3f Scaled = P * static_cast<float>(Period);
		const FIntVector Cell(FMath::FloorToInt(Scaled.X), FMath::FloorToInt(Scaled.Y), FMath::FloorToInt(Scaled.Z));

		float Nearest = 10.f;
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 Y = -1; Y <= 1; ++Y)
			{
				for (int32 X = -1; X <= 1; ++X)
				{
					const FIntVector C = Cell + FIntVector(X, Y, Z);
					const uint32 H = Hash3(Wrap(C.X, Period), Wrap(C.Y, Period), Wrap(C.Z, Period), Seed);
					const FVector3f Feature(C.X + Unit(H), C.Y + Unit(Hash(H)), C.Z + Unit(Hash(H ^ 0x5bd1e995u)));
					Nearest = FMath::Min(Nearest, (Feature - Scaled).SizeSquared());
				}
			}
		}
		return FMath::Clamp(FMath::Sqrt(Nearest), 0.f, 1.f);
	}

	/** Three octaves of inverted Worley, which is billows rather than cells. */
	float WorleyFbm(const FVector3f& P, int32 Period, uint32 Seed)
	{
		const float A = 1.f - Worley(P, Period, Seed);
		const float B = 1.f - Worley(P, Period * 2, Seed + 1);
		const float C = 1.f - Worley(P, Period * 4, Seed + 2);
		return A * 0.625f + B * 0.25f + C * 0.125f;
	}

	FVector3f Gradient(int32 X, int32 Y, int32 Z, int32 Period, uint32 Seed)
	{
		const uint32 H = Hash3(Wrap(X, Period), Wrap(Y, Period), Wrap(Z, Period), Seed);
		const FVector3f G(Unit(H) * 2.f - 1.f, Unit(Hash(H)) * 2.f - 1.f, Unit(Hash(H ^ 0x27d4eb2du)) * 2.f - 1.f);
		return G.GetSafeNormal();
	}

	float Fade(float T)
	{
		return T * T * T * (T * (T * 6.f - 15.f) + 10.f);
	}

	/** Gradient noise on a lattice that tiles every Period cells, in roughly -1..1. */
	float Perlin(const FVector3f& P, int32 Period, uint32 Seed)
	{
		const FVector3f Scaled = P * static_cast<float>(Period);
		const FIntVector Cell(FMath::FloorToInt(Scaled.X), FMath::FloorToInt(Scaled.Y), FMath::FloorToInt(Scaled.Z));
		const FVector3f F = Scaled - FVector3f(Cell.X, Cell.Y, Cell.Z);

		float Corners[8];
		for (int32 I = 0; I < 8; ++I)
		{
			const FIntVector O((I & 1), (I >> 1) & 1, (I >> 2) & 1);
			const FVector3f G = Gradient(Cell.X + O.X, Cell.Y + O.Y, Cell.Z + O.Z, Period, Seed);
			Corners[I] = FVector3f::DotProduct(G, F - FVector3f(O.X, O.Y, O.Z));
		}

		const FVector3f U(Fade(F.X), Fade(F.Y), Fade(F.Z));
		const float X00 = FMath::Lerp(Corners[0], Corners[1], U.X);
		const float X10 = FMath::Lerp(Corners[2], Corners[3], U.X);
		const float X01 = FMath::Lerp(Corners[4], Corners[5], U.X);
		const float X11 = FMath::Lerp(Corners[6], Corners[7], U.X);
		return FMath::Lerp(FMath::Lerp(X00, X10, U.Y), FMath::Lerp(X01, X11, U.Y), U.Z) * 1.4f;
	}

	float PerlinFbm(const FVector3f& P, int32 Period, uint32 Seed)
	{
		float Sum = 0.f;
		float Amplitude = 0.5f;
		for (int32 Octave = 0; Octave < 4; ++Octave)
		{
			Sum += Perlin(P, Period << Octave, Seed + Octave) * Amplitude;
			Amplitude *= 0.5f;
		}
		return FMath::Clamp(Sum * 0.5f + 0.5f, 0.f, 1.f);
	}

	float Remap(float X, float A, float B, float C, float D)
	{
		return C + (X - A) / FMath::Max(B - A, 1e-4f) * (D - C);
	}

	uint8 Byte(float V)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(V * 255.f), 0, 255));
	}

	/** Fills a cube of RGBA8 texels. Channels comes back as r, g, b, a in 0..1 for a point in 0..1. */
	TArray<uint8> Bake(int32 Size, TFunctionRef<FVector4f(const FVector3f&)> Channels)
	{
		TArray<uint8> Texels;
		Texels.SetNumUninitialized(Size * Size * Size * 4);

		ParallelFor(Size, [&](int32 Z)
		{
			for (int32 Y = 0; Y < Size; ++Y)
			{
				for (int32 X = 0; X < Size; ++X)
				{
					const FVector3f P((X + 0.5f) / Size, (Y + 0.5f) / Size, (Z + 0.5f) / Size);
					const FVector4f C = Channels(P);
					uint8* Out = &Texels[((Z * Size + Y) * Size + X) * 4];

					// BGRA, which is what the source format stores.
					Out[0] = Byte(C.Z);
					Out[1] = Byte(C.Y);
					Out[2] = Byte(C.X);
					Out[3] = Byte(C.W);
				}
			}
		});
		return Texels;
	}

	UVolumeTexture* Write(const FString& Name, int32 Size, const TArray<uint8>& Texels)
	{
		const FString PackageName = FString::Printf(TEXT("/MobSky/Textures/%s"), *Name);
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();

		UVolumeTexture* Texture = FindObject<UVolumeTexture>(Package, *Name);
		if (!Texture)
		{
			Texture = NewObject<UVolumeTexture>(Package, *Name, RF_Public | RF_Standalone);
		}

		Texture->PreEditChange(nullptr);
		Texture->UpdateSourceFromFunction([&Texels, Size](int32 X, int32 Y, int32 Z, void* Value)
		{
			FMemory::Memcpy(Value, &Texels[((Z * Size + Y) * Size + X) * 4], 4);
		}, Size, Size, Size, TSF_BGRA8);

		Texture->SRGB = false;
		Texture->CompressionSettings = TC_VectorDisplacementmap;
		Texture->AddressMode = TA_Wrap;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		return Texture;
	}

	/** Replaces one channel with its rank, so its values spread evenly over 0..1 whatever noise made them. */
	void Equalise(TArray<FVector4f>& Values, int32 Channel)
	{
		TArray<int32> Order;
		Order.SetNumUninitialized(Values.Num());
		for (int32 Index = 0; Index < Order.Num(); ++Index)
		{
			Order[Index] = Index;
		}
		Order.Sort([&Values, Channel](int32 A, int32 B) { return Values[A][Channel] < Values[B][Channel]; });

		// Equal values share a rank. Handing a run of them ranks in the order they sit in memory draws
		// the rows of the texture into it, and a clamped noise has a long run of zeros.
		const float Last = static_cast<float>(FMath::Max(Order.Num() - 1, 1));
		for (int32 Start = 0; Start < Order.Num();)
		{
			const float Value = Values[Order[Start]][Channel];
			int32 End = Start + 1;
			while (End < Order.Num() && Values[Order[End]][Channel] == Value)
			{
				++End;
			}
			for (int32 Rank = Start; Rank < End; ++Rank)
			{
				Values[Order[Rank]][Channel] = Start / Last;
			}
			Start = End;
		}
	}

	/**
	 * A tiling 2D texture of RGBA8 texels, uncompressed and linear.
	 *
	 * Red, green and alpha are equalised, so a threshold on them means a fraction of the texture: the
	 * clouds' Coverage is then the share of sky that is cloud, rather than a number that depends on the noise.
	 */
	UTexture2D* Write2D(const FString& Name, int32 Size, TFunctionRef<FVector4f(const FVector2f&)> Channels)
	{
		TArray<FVector4f> Values;
		Values.SetNumUninitialized(Size * Size);
		ParallelFor(Size, [&](int32 Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				Values[Y * Size + X] = Channels(FVector2f((X + 0.5f) / Size, (Y + 0.5f) / Size));
			}
		});

		Equalise(Values, 0);
		Equalise(Values, 1);
		Equalise(Values, 3);

		// A copy beside the project, for looking at when the clouds do not look right.
		{
			FImage Preview(Size, Size, ERawImageFormat::BGRA8, EGammaSpace::Linear);
			TArrayView64<FColor> Pixels = Preview.AsBGRA8();
			for (int32 Index = 0; Index < Values.Num(); ++Index)
			{
				Pixels[Index] = FColor(Byte(Values[Index].X), Byte(Values[Index].Y), Byte(Values[Index].Z), Byte(Values[Index].W));
			}
			FImageUtils::SaveImageByExtension(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MobSky"), Name + TEXT(".png")), Preview);
		}

		TArray<uint8> Texels;
		Texels.SetNumUninitialized(Size * Size * 4);
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			const FVector4f& C = Values[Index];
			uint8* Out = &Texels[Index * 4];
			Out[0] = Byte(C.Z);
			Out[1] = Byte(C.Y);
			Out[2] = Byte(C.X);
			Out[3] = Byte(C.W);
		}

		const FString PackageName = FString::Printf(TEXT("/MobSky/Textures/%s"), *Name);
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();

		UTexture2D* Texture = FindObject<UTexture2D>(Package, *Name);
		if (!Texture)
		{
			Texture = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone);
		}

		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8, Texels.GetData());
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_VectorDisplacementmap;
		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Wrap;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		return Texture;
	}
}

namespace MobSkyNoise
{
	/**
	 * Weather at the scale of a planet: storms and the clear sky between them, read at a far larger tile
	 * than the clouds themselves.
	 *
	 * The field is spun round a handful of vortices before it is read, tighter near each eye, which is
	 * what turns blotches into cyclones; then warped by itself, which draws the fronts out into bands.
	 */
	float MacroWeather(const FVector2f& UV)
	{
		FVector2f P = UV;
		for (int32 Vortex = 0; Vortex < 7; ++Vortex)
		{
			const uint32 H = Hash3(Vortex, 17, 3, 97);
			const FVector2f Centre(Unit(H), Unit(Hash(H)));

			// The shortest way round the tile, so a vortex near an edge spins what is across it too.
			FVector2f D = P - Centre;
			D.X -= FMath::RoundToFloat(D.X);
			D.Y -= FMath::RoundToFloat(D.Y);

			const float Radius = 0.05f + 0.07f * Unit(Hash(H ^ 0x1234u));
			const float Turn = (Unit(Hash(H ^ 0x9876u)) > 0.5f ? 1.f : -1.f) * 6.f;
			const float Angle = Turn * FMath::Exp(-D.SizeSquared() / (Radius * Radius));
			const float C = FMath::Cos(Angle);
			const float S = FMath::Sin(Angle);
			P = Centre + FVector2f(D.X * C - D.Y * S, D.X * S + D.Y * C);
		}

		const FVector3f A(P.X, P.Y, 0.21f);
		const float Q = PerlinFbm(A, 2, 101) - 0.5f;
		const float R = PerlinFbm(A + FVector3f(0.37f, 0.11f, 0.f), 2, 103) - 0.5f;
		return PerlinFbm(FVector3f(P.X + 0.45f * Q, P.Y + 0.45f * R, 0.53f), 3, 107);
	}
}

bool UMobSkyEditorLibrary::BuildNoiseTextures()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MobSkyEditorLibrary::BuildNoiseTextures);

	using namespace MobSkyNoise;

	constexpr int32 ShapeSize = 128;
	const TArray<uint8> Shape = Bake(ShapeSize, [](const FVector3f& P)
	{
		// Perlin for the billows, pushed up by Worley so it gathers into cells instead of smearing.
		const float Worley = WorleyFbm(P, 4, 11);
		const float PerlinWorley = FMath::Clamp(Remap(PerlinFbm(P, 4, 3), Worley - 1.f, 1.f, 0.f, 1.f), 0.f, 1.f);
		return FVector4f(PerlinWorley, WorleyFbm(P, 4, 21), WorleyFbm(P, 8, 31), WorleyFbm(P, 16, 41));
	});

	constexpr int32 DetailSize = 32;
	const TArray<uint8> Detail = Bake(DetailSize, [](const FVector3f& P)
	{
		return FVector4f(WorleyFbm(P, 2, 51), WorleyFbm(P, 4, 61), WorleyFbm(P, 8, 71), 1.f);
	});

	UVolumeTexture* ShapeTexture = Write(TEXT("T_MobCloudShape"), ShapeSize, Shape);
	UVolumeTexture* DetailTexture = Write(TEXT("T_MobCloudDetail"), DetailSize, Detail);

	// Red: Perlin-Worley, so cloud gathers into clusters of every size with clear sky between rather
	// than a sheet or a grid of equal puffs. Green: how tall each grows. Blue: broad regions of fair and
	// foul weather, read at a larger scale to break up the repeat.
	UTexture2D* WeatherTexture = Write2D(TEXT("T_MobCloudWeather"), 512, [](const FVector2f& UV)
	{
		const FVector3f P(UV.X, UV.Y, 0.37f);
		const float Billows = WorleyFbm(P, 6, 81);
		const float Clusters = FMath::Clamp(Remap(PerlinFbm(P, 3, 83), 1.f - Billows, 1.f, 0.f, 1.f), 0.f, 1.f);
		return FVector4f(Clusters, PerlinFbm(P, 5, 87), PerlinFbm(P, 2, 89), MacroWeather(UV));
	});

	const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(
		{ ShapeTexture->GetPackage(), DetailTexture->GetPackage(), WeatherTexture->GetPackage() }, false);

	UE_LOG(LogMobSky, Log, TEXT("Noise textures written%s."), bSaved ? TEXT(" and saved") : TEXT(", NOT saved"));
	return bSaved;
}
